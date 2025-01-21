CC=shmemcc
CFLAGS=-O3 -g
LDFLAGS=-lucp -lucs -lucc

.PHONY: all clean

all: common.o host dpu

common.o: common.c common.h

host: host.c common.o
	${CC} ${CFLAGS} host.c common.o -o $@ ${LDFLAGS}

dpu: dpu.c common.o

clean:
	rm host dpu common.o
