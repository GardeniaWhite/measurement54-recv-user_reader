#include "HeavyPart.h"
#include "param.h"

#ifdef USING_SIMD_ACCELERATION
#include <immintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>
#endif // USING_SIMD_ACCELERATION

template<int bucket_num>
HeavyPart<bucket_num>::HeavyPart()
{
    this->clear();
}

template<int bucket_num>
HeavyPart<bucket_num>::~HeavyPart(){}

template<int bucket_num>
void HeavyPart<bucket_num>::clear()
{
    memset(buckets, 0, sizeof(Bucket) * bucket_num);
}

template<int bucket_num>
// swap_key: 需要交换的小流key (当该函数返回1时，需要用到swap_key 和 swap_val)
// swap_val: 需要交换的小流val
int HeavyPart<bucket_num>::insert(uint8_t *key, uint8_t *swap_key, uint32_t &swap_val_pkts, 
	uint32_t & swap_val_sizes, uint32_t & swap_val_delay, struct Val f)
{
    uint32_t fp;
	// 选择哈希位置
	int pos = CalculateFP(key, fp);

#ifdef USING_SIMD_ACCELERATION
	/* find if there has matched bucket */
	const __m256i item = _mm256_set1_epi32((int)fp);
	__m256i *keys_p = (__m256i *)(buckets[pos].key);
	int matched = 0;

	__m256i a_comp = _mm256_cmpeq_epi32(item, keys_p[0]);
	matched = _mm256_movemask_ps((__m256)a_comp);

	/* if matched */
	if (matched != 0){
		//return 32 if input is zero;
		int matched_index = _tzcnt_u32((uint32_t)matched);
		buckets[pos].val[matched_index] += f;
		return 0;
	}
	
	/* find the minimal bucket */
	const uint32_t mask_base = 0x7FFFFFFF;
	const __m256i *counters = (__m256i *)(buckets[pos].val);
	__m256 masks = (__m256)_mm256_set1_epi32(mask_base);
	__m256 results = (_mm256_and_ps(*(__m256*)counters, masks));
	__m256 mask2 = (__m256)_mm256_set_epi32(mask_base, 0, 0, 0, 0, 0, 0, 0);
	results = _mm256_or_ps(results, mask2);

	__m128i low_part = _mm_castps_si128(_mm256_extractf128_ps(results, 0));
	__m128i high_part = _mm_castps_si128(_mm256_extractf128_ps(results, 1));

	__m128i x = _mm_min_epi32(low_part, high_part);
	__m128i min1 = _mm_shuffle_epi32(x, _MM_SHUFFLE(0,0,3,2));
	__m128i min2 = _mm_min_epi32(x,min1);
	__m128i min3 = _mm_shuffle_epi32(min2, _MM_SHUFFLE(0,0,0,1));
	__m128i min4 = _mm_min_epi32(min2,min3);
	int min_counter_val = _mm_cvtsi128_si32(min4);

	const __m256i ct_item = _mm256_set1_epi32(min_counter_val);
	int ct_matched = 0;

	__m256i ct_a_comp = _mm256_cmpeq_epi32(ct_item, (__m256i)results);
	matched = _mm256_movemask_ps((__m256)ct_a_comp);
	int min_counter = _tzcnt_u32((uint32_t)matched);

	/* if there has empty bucket */
	if(min_counter_val == 0){		// empty counter
		buckets[pos].key[min_counter] = fp;
		buckets[pos].val[min_counter] = f;
		return 0;
	}
#else // USING_SIMD_ACCELERATION
	/* find if there has matched bucket */
	// 查找是否有匹配的桶
	int matched = -1, empty = -1, min_counter = 0;
	uint32_t min_counter_val = GetCounterVal(buckets[pos].val[0].pkts);
	for(int i = 0; i < COUNTER_PER_BUCKET - 1; i++){
		// flow ID 一致
		if(buckets[pos].key[i] == fp){
			matched = i;
			break;
		}
		// 空桶
		if(buckets[pos].key[i] == 0 && empty == -1)
			empty = i;
		// 冲突桶_找到具有最小冲突的桶
		if(min_counter_val > GetCounterVal(buckets[pos].val[i].pkts)){
			min_counter = i;
			min_counter_val = GetCounterVal(buckets[pos].val[i].pkts);
		}
	}

	/* if matched */
	// 如果要插入的流与 Heavy Part 中记录的流ID一致
	// matched: 匹配桶的索引
	if(matched != -1){
		buckets[pos].val[matched].pkts += f.pkts; // 记录 vote+
		buckets[pos].val[matched].sizes += f.sizes;
		buckets[pos].val[matched].delay += f.delay;
		return 0;
	}

	/* if there has empty bucket */
	// 如果 hash 到的桶为空
	// empty: 空桶的索引
	if(empty != -1){
		buckets[pos].key[empty] = fp;  // 记录 flow ID
		buckets[pos].val[empty].pkts += f.pkts; // 记录 vote+
		buckets[pos].val[empty].sizes += f.sizes;
		buckets[pos].val[empty].delay += f.delay;
		return 0;
	}
	
#endif // USING_SIMD_ACCELERATION

    /* update guard val and comparison */
	// 更新 guard 值 并进行比较
	// min_counter_val: vote+  guard_val: vote-
    uint32_t guard_val = buckets[pos].val[MAX_VALID_COUNTER].pkts;
    guard_val = UPDATE_GUARD_VAL(guard_val);

	// 比较vote+ 和 vote-
	// vote- 没超过 vote+, 将数据插入light part
	// 增加guard_bucket 记录
    if(!JUDGE_IF_SWAP(GetCounterVal(min_counter_val), guard_val)){
        buckets[pos].val[MAX_VALID_COUNTER].pkts = guard_val;
		buckets[pos].val[MAX_VALID_COUNTER].sizes += f.sizes;
		buckets[pos].val[MAX_VALID_COUNTER].delay += f.delay;
        return 2;
    }

	// vote- 超过 vote+, 交换vote+ 和 vote-, 并把驱逐的流发送到 light part
    *((uint32_t*)swap_key) = buckets[pos].key[min_counter];
    swap_val_pkts = buckets[pos].val[min_counter].pkts;  // 驱逐包个数
	swap_val_sizes = buckets[pos].val[min_counter].sizes; // 驱逐包大小
	swap_val_delay = buckets[pos].val[min_counter].delay; // 驱逐时延之和

	// 重置guard bucket
    buckets[pos].val[MAX_VALID_COUNTER].pkts = 0;
	buckets[pos].val[MAX_VALID_COUNTER].sizes = 0;
	buckets[pos].val[MAX_VALID_COUNTER].delay = 0;
	// 当前流作为大流, 存储到第 min_counter 个 bucket 
    buckets[pos].key[min_counter] = fp;
    buckets[pos].val[min_counter].pkts = 0x80000001; // 最高位为1，表示在light part存在记录
	buckets[pos].val[min_counter].sizes = f.sizes;
	buckets[pos].val[min_counter].sizes = f.delay;
    return 1;
}

template<int bucket_num>
int HeavyPart<bucket_num>::quick_insert(uint8_t *key, uint32_t f)
{
    uint32_t fp;
	int pos = CalculateFP(key, fp);

#ifdef USING_SIMD_ACCELERATION
	/* find if there has matched bucket */
	const __m256i item = _mm256_set1_epi32((int)fp);
	__m256i *keys_p = (__m256i *)(buckets[pos].key);
	int matched = 0;

	__m256i a_comp = _mm256_cmpeq_epi32(item, keys_p[0]);
	matched = _mm256_movemask_ps((__m256)a_comp);

	/* if matched */
	if (matched != 0){
		//return 32 if input is zero;
		int matched_index = _tzcnt_u32((uint32_t)matched);
		buckets[pos].val[matched_index] += f;
		return 0;
	}
	
	/* find the minimal bucket */
	const uint32_t mask_base = 0x7FFFFFFF;
	const __m256i *counters = (__m256i *)(buckets[pos].val);
	__m256 masks = (__m256)_mm256_set1_epi32(mask_base);
	__m256 results = (_mm256_and_ps(*(__m256*)counters, masks));
	__m256 mask2 = (__m256)_mm256_set_epi32(mask_base, 0, 0, 0, 0, 0, 0, 0);
	results = _mm256_or_ps(results, mask2);

	__m128i low_part = _mm_castps_si128(_mm256_extractf128_ps(results, 0));
	__m128i high_part = _mm_castps_si128(_mm256_extractf128_ps(results, 1));

	__m128i x = _mm_min_epi32(low_part, high_part);
	__m128i min1 = _mm_shuffle_epi32(x, _MM_SHUFFLE(0,0,3,2));
	__m128i min2 = _mm_min_epi32(x,min1);
	__m128i min3 = _mm_shuffle_epi32(min2, _MM_SHUFFLE(0,0,0,1));
	__m128i min4 = _mm_min_epi32(min2,min3);
	int min_counter_val = _mm_cvtsi128_si32(min4);

	const __m256i ct_item = _mm256_set1_epi32(min_counter_val);
	int ct_matched = 0;

	__m256i ct_a_comp = _mm256_cmpeq_epi32(ct_item, (__m256i)results);
	matched = _mm256_movemask_ps((__m256)ct_a_comp);
	int min_counter = _tzcnt_u32((uint32_t)matched);

	/* if there has empty bucket */
	if(min_counter_val == 0){		// empty counter
		buckets[pos].key[min_counter] = fp;
		buckets[pos].val[min_counter] = f;
		return 0;
	}
#else // USING_SIMD_ACCELERATION
	/* find if there has matched bucket */
	int matched = -1, empty = -1, min_counter = 0;
	uint32_t min_counter_val = GetCounterVal(buckets[pos].val[0]);
	for(int i = 0; i < COUNTER_PER_BUCKET - 1; i++){
		if(buckets[pos].key[i] == fp){
			matched = i;
			break;
		}
		if(buckets[pos].key[i] == 0 && empty == -1)
			empty = i;
		if(min_counter_val > GetCounterVal(buckets[pos].val[i])){
			min_counter = i;
			min_counter_val = GetCounterVal(buckets[pos].val[i]);
		}
	}

	/* if matched */
	if(matched != -1){
		buckets[pos].val[matched] += f;
		return 0;
	}

	/* if there has empty bucket */
	if(empty != -1){
		buckets[pos].key[empty] = fp;
		buckets[pos].val[empty] = f;
		return 0;
	}
#endif // USING_SIMD_ACCELERATION

    /* update guard val and comparison */
    uint32_t guard_val = buckets[pos].val[MAX_VALID_COUNTER];
    guard_val = UPDATE_GUARD_VAL(guard_val);

    if(!JUDGE_IF_SWAP(GetCounterVal(min_counter_val), guard_val)){
        buckets[pos].val[MAX_VALID_COUNTER] = guard_val;
        return 2;
    }
    buckets[pos].val[MAX_VALID_COUNTER] = 0;
    buckets[pos].key[min_counter] = fp;
    return 1;
}

template<int bucket_num>
struct Val HeavyPart<bucket_num>::query(uint8_t *key)
{
	struct Val res = {0, 0, 0};
    uint32_t fp;
    int pos = CalculateFP(key, fp);

#ifdef USING_SIMD_ACCELERATION
    do{
        /* find if there has matched bucket */
        const __m256i item = _mm256_set1_epi32((int)fp);
        __m256i *keys_p = (__m256i *)(buckets[pos].key);
        int matched = 0;

        __m256i a_comp = _mm256_cmpeq_epi32(item, keys_p[0]);
        matched = _mm256_movemask_ps((__m256)a_comp);

        /* if matched */
        if (matched != 0){
            //return 32 if input is zero;
            int matched_index = _tzcnt_u32((uint32_t)matched);
            return buckets[pos].val[matched_index];
        }
	}while(0);
#else
    for(int i = 0; i < MAX_VALID_COUNTER; ++i)
        if(buckets[pos].key[i] == fp)
            return buckets[pos].val[i];
#endif
    return res;
}

template<int bucket_num>
int HeavyPart<bucket_num>::get_memory_usage()
{
    return bucket_num * sizeof(Bucket);
}

template<int bucket_num>
int HeavyPart<bucket_num>::get_bucket_num()
{
    return bucket_num;
}

template<int bucket_num>
int HeavyPart<bucket_num>::CalculateFP(uint8_t *key, uint32_t &fp)
{
    fp = *((uint32_t*)key);
    return CalculateBucketPos(fp) % bucket_num;
}