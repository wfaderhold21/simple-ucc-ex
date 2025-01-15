CC=shmemcc
CFLAGS=-O3 -g
LDFLAGS=-lucp -lucs -lucc

.PHONY: clean

simple: simple.c
	${CC} ${CFLAGS} simple.c -o $@ ${LDFLAGS}

clean:
	rm simple
