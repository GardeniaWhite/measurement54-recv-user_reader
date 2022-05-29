all: user_reader

USER_HEADR = tuple.h ringbuffer.h util.h
USER_SRC = ringbuffer_user.cpp
OBJS = algorithms.o\
	LightPart.o HeavyPart.o ElasticSketch.o BOBHash32.o
USER_LIB = -lpthread -lrt -lhiredis
INS_SET = -mbmi -msse -msse3 -msse4.1 -msse4.2 -mavx -mavx2 -march=native

user_reader: user_reader.cpp $(USER_HEADER) $(USER_SRC) $(OBJS)
	g++ -O3 -g -Wall -std=c++14 -o $@ $< $(USER_SRC) $(USER_LIB) $(OBJS) $(INS_SET)

clean:
	rm -rf user_reader