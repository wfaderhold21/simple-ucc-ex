#include <shmem.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <shmemx.h>
#include <ucp/api/ucp.h>
#include <assert.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sched.h>

#include "common.h"

extern void *a2a_psync;

int main(int argc, char *argv[])
{
    size_t               exchange_size = 0;
    size_t               call_size = 0;
    size_t               a2a_signal = 1;
    int                  numprocs;
    int                  me;
    char                *s_buf_heap;
    char                *r_buf_heap;
    ucc_context_h        ucc_context;
    ucc_lib_h            ucc_lib;
    ucc_team_h           ucc_team;
    ucc_mem_map_params_t map_params;
    ucc_mem_map_t        map[2];
    ucc_mem_map_mem_h    local[2];
    ucc_mem_map_mem_h   *global_source;
    ucc_mem_map_mem_h   *global_dest;
    int                  ret;
    ucc_status_t         status;
    ucc_coll_req_h       req;
    void                *packed;
    void                *rpacked;
    int sockfd;
    struct sockaddr_in server_address;
    int addrlen = sizeof(struct sockaddr_in);
    char *address;
    struct hostent *dpu;
    host_info_t host_info;
    size_t bytes_sent = 0;
    void *buf;

    shmem_init();
    me       = shmem_my_pe();
    numprocs = shmem_n_pes();

    shmem_barrier_all();

    /* connect to DPU */
    address = get_server_name();
    if (address == NULL) {
        fprintf(stderr, "failed to obtain dpu ip");
        return -1;
    }

    dpu = gethostbyname(address);
        
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }
    
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    bcopy((char *)dpu->h_addr, (char *) &server_address.sin_addr.s_addr, dpu->h_length);
    //server_address.sin_addr.s_addr = inet_addr(dpu->h_addr);
    server_address.sin_port = htons(PORT);

    if (connect(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("Could not connect to host: ");
        return -1;
    }
    /* end connect to server */

    //s_buf_heap = malloc(1024);
    //r_buf_heap = malloc(1024);
    s_buf_heap = shmem_malloc(1024);
    r_buf_heap = shmem_malloc(1024);

    ret = setup_ucc(me, numprocs, &ucc_lib, &ucc_context, &ucc_team);
    if (ret != 0) {
        printf("failure in ucc setup\n");
        return ret;
    }

    /* setup maps for export */
    map[0].address = s_buf_heap;
    map[1].address = r_buf_heap;
    map[0].len = 1024;
    map[1].len = 1024;

    /* currently limited to 1 segment per map_param for export */
    map_params.n_segments = 1;
    map_params.segments = &map[0];
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_EXPORT, &map_params, &exchange_size, &local[0]);
    if (status != UCC_OK) {
        abort();
    }
#if 0
    /* map recv buf */
    map_params.segments = &map[1];
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_EXPORT, &map_params, &call_size, &local[1]);
    if (status != UCC_OK) {
        abort();
    }
    exchange_size += call_size;
#endif
    host_info.host_sbuf_va = (uint64_t)map[0].address;
    host_info.host_sbuf_len = map[0].len;
/*    host_info.host_rbuf_va = (uint64_t)map[1].address;
    host_info.host_rbuf_len = map[1].len;*/
    host_info.exchange_size = exchange_size;
    
    packed  = malloc(exchange_size);
    memcpy(packed, local[0], exchange_size);
//    memcpy(packed, local[0], exchange_size - call_size);
//    memcpy(packed + (exchange_size - call_size), local[1], call_size);
    shmem_barrier_all();

    buf = malloc(sizeof(host_info_t));
    memcpy(buf, &host_info, sizeof(host_info_t));
    /* send packed buffers to dpu */
    bytes_sent = send(sockfd, buf, sizeof(host_info_t), 0);
    bytes_sent = send(sockfd, packed, exchange_size, 0);

    shmem_barrier_all();
    /* recv alltoall status */
    read(sockfd, &ret, sizeof(int));
    if (ret != 0) {
        fprintf(stderr, "a2a on dpu failed\n");
    } else {
        fprintf(stderr, "a2a successful\n");
    }

    /* unmap local memhs */
    ucc_mem_unmap(&local[0]);

    shmem_finalize();
    return 0;
}
