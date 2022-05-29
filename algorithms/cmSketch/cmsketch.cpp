#include "cmsketch.h"
#include <string.h>
// CMSketch data structure (一个一维数组)
/* counters[width*d]
 * | [0,0] |  ...  | [0,w-1] | [1,0] | ... | [1,w-1] | ...  |
 * |       |       |         |       |     |         |      |
*/


/*
 * d: CMSketch深度
 * width: CMSketch宽度
*/

CMSketch::CMSketch(int tot_mem_in_bytes, int d): Algorithm("CMSketch")
{
    this->d = d;
    this->width = tot_mem_in_bytes / d / sizeof(uint32_t);
    counters = new uint32_t[width * d];
    memset(counters, 0, sizeof(uint32_t) * width * d);  // 为counter分配空间
    hash = new BOBHash32[d];  // 生成d个hash函数
    for(int i = 0; i < d; ++i)
        hash[i].initialize(100 + i);  // 初始化哈希函数？？？
}

// 函数执行完毕，删除数据
CMSketch::~CMSketch()
{
    delete[] hash;
    delete[] counters;
}

// 请求记录
void CMSketch::clear()
{
    insertTimes = 0;
    memset(counters, 0, sizeof(uint32_t) * width * d);
}


// 插入表项
int CMSketch::insert(uint8_t *key, int keylen)
{
    for(int i = 0; i < d; ++i)
    {
        int pos = i * width + (hash[i].run((const char*)key, keylen) % width);
        counters[pos]++;
    }
    return ++insertTimes;  //返回插入次数
}


// 查询表项
int CMSketch::query(uint8_t *key, int keylen)
{
    int ret = 0x7FFFFFFF;
    for(int i = 0; i < d; ++i)
    {
        int pos = i * width + (hash[i].run((const char*)key, keylen) % width);
        ret = ret > counters[pos] ? counters[pos] : ret;
    }
    return ret;
}

void CMSketch::get_flowsize(vector<string> &flowIDs, unordered_map<string,int> &freq)
{
    freq.clear();
    uint8_t key[100] = {0};
    for(auto id : flowIDs){
        memcpy(key, id.c_str(), id.size());
        freq[id] = this->query(key, id.size());
    }
}