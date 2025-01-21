#ifndef COMMON_H
#define COMMON_H

#include <mpi.h>
#include <ucc/api/ucc.h>

#define PORT                23500
#define MAX_EXCHANGE_SIZE   1024

typedef struct host_info {
    uint64_t host_sbuf_va;
    uint64_t host_rbuf_va;
    uint64_t host_sbuf_len;
    uint64_t host_rbuf_len;
    uint64_t exchange_size;
} host_info_t;

extern void *a2a_psync;

char * get_server_name(void);

int setup_ucc(int rank, int size, 
              ucc_lib_h *lib, 
              ucc_context_h *context, ucc_team_h *team);

#endif
