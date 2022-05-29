#ifndef _LIGHT_PART_H_
#define _LIGHT_PART_H_

// #include "EMFSD.h"
#include "param.h"

template<int init_mem_in_bytes>
class LightPart
{
	static constexpr int counter_num = init_mem_in_bytes;
	BOBHash32 *bobhash = NULL;
	int mice_dist[256]; // 类似于Map, 记录不同大小的老鼠流对应的个数
	// EMFSD *em_fsd_algo = NULL;
	
public:
	uint8_t counters[counter_num]; // 记录流个数
	uint32_t sizes[counter_num]; // 记录流大小
	uint32_t delays[counter_num]; // 时延之和

	LightPart();
	~LightPart();

	void clear();

	void insert(uint8_t *key, int size, int delay, int f = 1);
	void swap_insert(uint8_t *key, int size, int delay, int f);
	int query(uint8_t *key, int *counters, int *sizes);

    void compress(int ratio, uint8_t *dst);
    int query_compressed_part(uint8_t *key, uint8_t *compress_part, int compress_counter_num);
    int get_compress_width(int ratio);
    int get_compress_memory(int ratio);
    int get_memory_usage();

   	int get_cardinality();
    void get_entropy(int &tot, double &entr);
    // void get_distribution(vector<double> &dist);
};

#endif // _LIGHT_PART_H_
