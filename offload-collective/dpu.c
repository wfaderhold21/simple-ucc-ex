#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sched.h>

#include "common.h"

int main(void)
{
    int sockfd, new_socket;
    struct sockaddr_in server_address;
    int opt = 1;
    int addrlen = sizeof(struct sockaddr_in);
    size_t exchange_size;
    size_t handle_size;
    size_t call_size;
    int rank, size;
    void *buffer;
    void *packed, *rpacked;
    int ok = 0;
    int not_ok = -1;
    ucc_mem_map_params_t  map_params;
    ucc_mem_map_t         map[2];
    ucc_mem_map_mem_h    *import_source;
    ucc_mem_map_mem_h     local[2];
    ucc_mem_map_mem_h    *global_source;
    ucc_mem_map_mem_h    *global_dest;
    host_info_t           host_info;
    uint64_t              host_sbuf_va;
    uint64_t              host_rbuf_va;
    size_t                host_sbuf_len;
    size_t                host_rbuf_len;
    ucc_context_h        ucc_context;
    ucc_lib_h            ucc_lib;
    ucc_team_h           ucc_team;
    ucc_coll_req_h       req;
    ucc_status_t         status;
    int ret;

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    ret = setup_ucc(rank, size, &ucc_lib, &ucc_context, &ucc_team);
    if (ret != 0) {
        printf("failure in ucc setup\n");
        return ret;
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("sockopt failed");
        return -1;
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("bind failed");
        return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen failed:");
        return -1;
    }

    printf("listening, start the host processes\n");
    if ((new_socket = accept(sockfd, (struct sockaddr *) &server_address, 
                                     (socklen_t *) &addrlen)) < 0) {
        perror("accept failed: ");
        return -1;
    }
    read(new_socket, &host_info, sizeof(host_info_t));
    if (host_info.exchange_size > MAX_EXCHANGE_SIZE) {
        fprintf(stderr, "exchange size is more than 1024 bytes");
        return -1;
    }
    buffer = calloc(1, host_info.exchange_size);
    if (!buffer) {
        fprintf(stderr, "Out of memory");
        return -1;
    }

    read(new_socket, buffer, host_info.exchange_size);
    map[0].address = (void *)host_info.host_sbuf_va;
    map[1].address = (void *)host_info.host_rbuf_va;
    map[0].len = host_info.host_sbuf_len;
    map[1].len = host_info.host_rbuf_len;

    handle_size = exchange_size / 2; /* just an assumption */

    /* import and map */
    map_params.n_segments = 1;
    // import
    for (int j = 0; j < 2; j++) {
        size_t dummy; /* we do not need a pack size on import */
        map_params.segments = &map[j]; // on import this is ignored, but we should set this at some point
        if (j == 0) {
            import_source[j] = buffer;
            status = ucc_mem_map(ucc_context, UCC_MEM_MAP_IMPORT, &map_params, &dummy, &import_source[j]);
            if (status != UCC_OK) {
                abort();
            }
        } else {
            import_source[j] = buffer + handle_size;
            status = ucc_mem_map(ucc_context, UCC_MEM_MAP_IMPORT, &map_params, &dummy, &import_source[j]);
            if (status != UCC_OK) {
                abort();
            }
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    
    /* export */
    map_params.segments = &map[0];
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_EXPORT, &map_params, &exchange_size, &local[0]);
    if (status != UCC_OK) {
        abort();
    }

    /* map recv buf */
    map_params.segments = &map[1];
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_EXPORT, &map_params, &call_size, &local[1]);
    if (status != UCC_OK) {
        abort();
    }
    exchange_size += call_size;
    packed  = malloc(exchange_size);
    rpacked = malloc(exchange_size * size);
    memcpy(packed, local[0], exchange_size - call_size);
    memcpy(packed + (exchange_size - call_size), local[1], call_size);
    MPI_Barrier(MPI_COMM_WORLD);

    /* exchange with other DPUs processes */
    MPI_Allgather(packed, exchange_size, MPI_BYTE, rpacked, exchange_size, MPI_BYTE, MPI_COMM_WORLD);

    /* import */
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < 2; j++) {
            size_t dummy; /* we do not need a pack size on import */
            map_params.segments = &map[j];
            if (j == 0) {
                global_source[i] = rpacked + (exchange_size * i);
                status = ucc_mem_map(ucc_context, UCC_MEM_MAP_IMPORT, &map_params, &dummy, &global_source[i]);
                if (status != UCC_OK) {
                    abort();
                }
            } else {
                global_dest[i] = rpacked + (exchange_size * i) + call_size;
                status = ucc_mem_map(ucc_context, UCC_MEM_MAP_IMPORT, &map_params, &dummy, &global_dest[i]);
                if (status != UCC_OK) {
                    abort();
                }
            }
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // perform alltoall

{
    if (rank == 0) {
        printf("performing alltoall\n");
    }
    ucc_coll_args_t a2a_coll = {
        .mask = UCC_COLL_ARGS_FIELD_FLAGS | UCC_COLL_ARGS_FIELD_GLOBAL_WORK_BUFFER | UCC_COLL_ARGS_FIELD_MEM_MAP_SRC_MEMH | UCC_COLL_ARGS_FIELD_MEM_MAP_DST_MEMH,
        .coll_type = UCC_COLL_TYPE_ALLTOALL,
        .src.info = {
            .buffer = (void *)host_info.host_sbuf_va,
            .count = 2 * size,
            .datatype = UCC_DT_INT64,
            .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
        },
        .dst.info = {
            .buffer = (void *)host_info.host_rbuf_va,
            .count = 2 * size,
            .datatype = UCC_DT_INT64,
            .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
        },
        .flags = UCC_COLL_ARGS_FLAG_MEM_MAPPED_BUFFERS,
        .global_work_buffer = a2a_psync, /* mapped via context */
        .src_memh.global_memh = global_source, 
        .dst_memh.global_memh = global_dest,
    };
    status = ucc_collective_init(&a2a_coll, &req, ucc_team);
    if (status != UCC_OK) {
        abort();
    }
    status = ucc_collective_post(req);
    if (status != UCC_OK) {
        abort();
    }
    status = ucc_collective_test(req);
    while (status != UCC_OK) {
        ucc_context_progress(ucc_context);
        status = ucc_collective_test(req);
    }
    ucc_collective_finalize(req);
    if (rank == 0) {
        printf("completed alltoall\n");
    }
}
    MPI_Barrier(MPI_COMM_WORLD);
    if (status == UCC_OK) {
        send(new_socket, &ok, sizeof(int), 0);
    } else {
        send(new_socket, &not_ok, sizeof(int), 0);
    }

    /* unmap global memh first */
    for (int i = 0; i < size; i++) {
        ucc_mem_unmap(&global_source[i]);
        ucc_mem_unmap(&global_dest[i]);
    }
    /* unmap local memhs */
    ucc_mem_unmap(&local[0]);
    ucc_mem_unmap(&local[1]);
    close(new_socket);
    return 0;
}
