#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <fcntl.h>	   /* open */
#include <unistd.h>	   /* exit */
#include <sys/ioctl.h> /* ioctl */
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <errno.h>
#include <hiredis/hiredis.h> // 操作数据库
#include <time.h> // 时间戳
#include <thread> // 操作线程
#include <future> // 线程
#include "util.h"
#include "tuple.h"
#include "ringbuffer.h"
#include "./algorithms/elasticSketch/ElasticSketch.h"
#include "./algorithms/elasticSketch/ElasticSketch.cpp"
#include "./algorithms/elasticSketch/param.h"

using namespace std;

// The number of ringbuffer
// **Must** be (# pmd threads + 1)
#define MAX_RINGBUFFER_NUM 2

// ESSketch
#define HEAVY_MEM (150 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 64)
#define TOT_MEM_IN_BYTES (4 * 1024 * 1024)
typedef ElasticSketch<BUCKET_NUM,TOT_MEM_IN_BYTES> Elastic;
bool flag = true;
Elastic *elastic1 = new Elastic();
Elastic *elastic2 = new Elastic();
Elastic *elastic = elastic1;
Elastic *read_elastic;

// 用于操作数据库的指针
redisContext *pContext;

static inline char *ip2a(uint32_t ip, char *addr)
{
	sprintf(addr, "%.3d.%.3d.%.3d.%.3d", (ip & 0xff), ((ip >> 8) & 0xff), ((ip >> 16) & 0xff), ((ip >> 24) & 0xff));
	return addr;
}

void print_tuple(FILE *f, tuple_t *t)
{
	char ip1[30], ip2[30];

	fprintf(f, "%s(%u) <-> %s(%u) %u %d %d\n",
			ip2a(t->key.src_ip, ip1), ntohs(t->key.src_port),
			ip2a(t->key.dst_ip, ip2), ntohs(t->key.dst_port),
			t->key.proto, ntohs(t->size), t->diff_time);
}

// 连接数据库
redisContext * connnectDB() {
	redisContext *pContext = redisConnect("192.168.1.196", 6379);
	if(pContext->err)
	{
		redisFree(pContext);
		cout << "connect to redisServer fail" << endl;
		return nullptr;
	}
	cout << "connect to redisServer success" << endl;
	redisReply *pReply = (redisReply*)redisCommand(pContext, "AUTH a123456");
	cout << pReply->str << endl;
	pReply = (redisReply*)redisCommand(pContext, "FLUSHDB");
	cout << pReply->str << endl;
	return pContext;
}

// 将数据插入数据库
void insertToDB(redisContext *pContext, Elastic *elastic) {
	redisReply *pReply = nullptr;

	char ip[30];
	char setCommand[256];
	time_t myt = time(NULL);
	int uid = 0;
    for (int i = 0; i < BUCKET_NUM; ++i) {
        for (int j = 0; j < MAX_VALID_COUNTER; ++j) 
        {
            uint32_t key = (*elastic).heavy_part.buckets[i].key[j];
            if (key != 0) {
				struct Val val = (*elastic).query((uint8_t *)&key, 32);
				sprintf(setCommand, 
					"hmset %ld %d {\"key\":\"%s\",\"pkts\":%d,\"sizes\":%d,\"delay\":%d}", 
					myt, uid, ip2a(key, ip), val.pkts, val.sizes, val.delay);
				// printf("%s\n", setCommand);
				pReply = (redisReply*)redisCommand(pContext, setCommand);
				// cout << pReply->str << endl;
				uid++;	
			}
        }
	}
	freeReplyObject(pReply);
}

void write_and_clear() {
	insertToDB(pContext, read_elastic);
	read_elastic->clear();
}

long long int counter = 0;
bool KEEP_RUNNING;
void handler(int sig)
{
	if (sig == SIGINT)
	{
		KEEP_RUNNING = false;
		return;
	}

	// 两个sketch轮换处理
	if (flag) {
		elastic = elastic2;
		read_elastic = elastic1;
	} else {
		elastic = elastic1;
		read_elastic = elastic2;
	}
	async(launch::async, write_and_clear);
	flag = !flag;

	// 只用一个sketch处理
	// insertToDB(pContext, elastic);
	// elastic->clear();

	printf("total insert: %lld\n", counter);
	counter = 0;
	alarm(1);
}

int main(int argc, char *argv[])
{
	// 连接数据库
	pContext = connnectDB();

	tuple_t t;
	LOG_MSG("Initialize the ringbuffer.\n");

	/* link to shared memory (datapath) */
	ringbuffer_t *rbs[MAX_RINGBUFFER_NUM];
	for (int i = 0; i < MAX_RINGBUFFER_NUM; ++i)
	{
		char name[30] = {0};
		sprintf(name, "/rb_%d", i);
		printf("name=%s\n", name);
		rbs[i] = connect_ringbuffer_shm(name, sizeof(tuple_t));
		// printf("%p\n", rbs[i]);
	}
	printf("connected.\n");
	fflush(stdout);

	/* print number of pkts received per seconds */
	signal(SIGALRM, handler);
	signal(SIGINT, handler);
	alarm(1);

	/* create your measurement structure (sketch) here !!! */

	/* begin polling */
	int idx = 0;
	KEEP_RUNNING = true;
	while (KEEP_RUNNING)
	{
		if (t.flag == TUPLE_TERM)
		{
			break;
		}
		else
		{
			while (read_ringbuffer(rbs[(idx) % MAX_RINGBUFFER_NUM], &t) < 0 && KEEP_RUNNING)
			{
				idx = (idx + 1) % MAX_RINGBUFFER_NUM;
			}
			counter++;
			// print_tuple(stdout, &t);
			//  Insert to sketch here
			struct Val f = {1, ntohs(t.size), t.diff_time};
			// printf("%d %d %d\n", f.pkts, f.sizes, f.delay);
			elastic->insert((uint8_t*)&t, 32, f);
		}
	}
	// printf("totally insert %d packets\n", counter);
	redisFree(pContext);
	return 0;
}
