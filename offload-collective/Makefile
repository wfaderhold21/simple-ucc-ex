CC=shmemcc
CFLAGS=-O0 -g
LDFLAGS=-lucp -lucs -lucc

.PHONY: all clean

all: host dpu

host: host.c common.c
	${CC} ${CFLAGS} host.c common.c -o $@ ${LDFLAGS}

dpu: dpu.c common.c
	${CC} -I${HOME}/build-memmap/install.arm/include ${CFLAGS} dpu.c common.c -o dpu -L${HOME}/build-memmap/install.arm/lib ${LDFLAGS}

clean:
	rm host dpu
