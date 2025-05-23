#include <shmem.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <shmemx.h>
#include <mpi.h>
#include <ucc/api/ucc.h>
#include <sched.h>
#include <ucp/api/ucp.h>
#include <assert.h>

#define BUFFER_SIZE     1024

void *a2a_psync;

static ucc_status_t oob_allgather(void *sbuf, void *rbuf, size_t msglen,
                                   void *coll_info, void **req)
{
    MPI_Comm    comm = (MPI_Comm)(uintptr_t)coll_info;
    MPI_Request request;
    MPI_Iallgather(sbuf, msglen, MPI_BYTE, rbuf, msglen, MPI_BYTE, comm,
                   &request);
    *req = calloc(1, sizeof(ucc_status_t));
    /* FIXME: MPI_Test in oob_allgather_test results in no completion?
     *        leave as blocking for now */
    MPI_Wait(&request, MPI_STATUS_IGNORE);
    *req = UCC_OK;
    return UCC_OK;
}

static ucc_status_t oob_allgather_test(void *req)
{
    return UCC_OK;
}

static ucc_status_t oob_allgather_free(void *req)
{
    return UCC_OK;
}

int setup_ucc(int rank, int size, 
              ucc_lib_h *lib, 
              ucc_context_h *context, ucc_team_h *team)
{
    ucc_team_params_t    team_params = {0};
    ucc_context_params_t ctx_params  = {0};
    ucc_context_h        ucc_context;
    ucc_team_h           ucc_team;
    ucc_lib_h            ucc_lib;
    ucc_lib_config_h     lib_config;
    ucc_lib_params_t     lib_params;
    ucc_context_config_h ctx_config;
    ucc_status_t         status;
    ucc_mem_map_t        map;

    /* make ucc here */
    lib_params.mask        = UCC_LIB_PARAM_FIELD_THREAD_MODE;
    lib_params.thread_mode = UCC_THREAD_SINGLE;

    if (UCC_OK != ucc_lib_config_read(NULL, NULL, &lib_config)) {
        printf("lib config error\n");
        return -1;
    }
    if (UCC_OK != ucc_init(&lib_params, lib_config, &ucc_lib)) {
        printf("lib init error\n");
        return -1;
    }
    *lib = ucc_lib;

    a2a_psync   = calloc(1,128);
    map.address = a2a_psync;
    map.len     = 128;

    ctx_params.mask          = UCC_CONTEXT_PARAM_FIELD_OOB
                             | UCC_CONTEXT_PARAM_FIELD_MEM_PARAMS;
    ctx_params.oob.allgather = oob_allgather;
    ctx_params.oob.req_test  = oob_allgather_test;
    ctx_params.oob.req_free  = oob_allgather_free;
    ctx_params.oob.coll_info = MPI_COMM_WORLD;
    ctx_params.oob.n_oob_eps = size;
    ctx_params.oob.oob_ep    = rank;
    ctx_params.mem_params.segments   = &map;
    ctx_params.mem_params.n_segments = 1;

    if (UCC_OK != ucc_context_config_read(ucc_lib, NULL, &ctx_config)) {
        printf("error ucc ctx config read\n");
        return -1;
    }
    if (UCC_OK != ucc_context_create(ucc_lib, &ctx_params, ctx_config, &ucc_context)) {
        printf("ERROR ucc ctx create\n");
        return -1;
    }
    *context = ucc_context;

    ucc_context_config_release(ctx_config);

    team_params.mask = UCC_TEAM_PARAM_FIELD_EP | UCC_TEAM_PARAM_FIELD_OOB | UCC_TEAM_PARAM_FIELD_FLAGS;
    team_params.oob.allgather = oob_allgather;
    team_params.oob.req_test  = oob_allgather_test;
    team_params.oob.req_free  = oob_allgather_free;
    team_params.oob.coll_info = MPI_COMM_WORLD;
    team_params.oob.n_oob_eps = size;
    team_params.oob.oob_ep    = rank;
    team_params.ep            = rank;

    if (UCC_OK != ucc_team_create_post(&ucc_context, 1, &team_params, &ucc_team)) {
        printf("team create post failed\n");
        return -1;
    }
    while (UCC_INPROGRESS == (status = ucc_team_create_test(ucc_team))) {}
    if (UCC_OK != status) {
        printf("team create failed\n");
        return -1;
    }
    *team = ucc_team;

    return 0;
}

int main(int argc, char *argv[])
{
    size_t               exchange_size = 0;
    size_t               call_size     = 0;
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
    ucc_mem_map_mem_h   *global_dest;
    int                  ret;
    ucc_status_t         status;
    ucc_coll_req_h       req;
    void                *packed;
    void                *rpacked;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

    s_buf_heap    = malloc(BUFFER_SIZE);
    r_buf_heap    = malloc(BUFFER_SIZE);
    global_dest   = malloc(numprocs * sizeof(ucc_mem_map_mem_h));
    
    ret = setup_ucc(me, numprocs, &ucc_lib, &ucc_context, &ucc_team);
    if (ret != 0) {
        printf("failure in ucc setup\n");
        return ret;
    }

    /* setup maps for export */
    map[0].address = s_buf_heap;
    map[0].len     = BUFFER_SIZE;
    map[1].address = r_buf_heap;
    map[1].len     = BUFFER_SIZE;

    /* currently limited to 1 segment per map_param for export */
    map_params.n_segments = 1;
    map_params.segments   = &map[0];
    /* map source buffer for offset calculation during onesided collective */
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_MODE_EXPORT, &map_params, &exchange_size, &local[0]);
    if (status != UCC_OK) {
        fprintf(stderr, "failed to export source buffer with status %d\n", status);
        abort();
    }

    /* map recv buf */
    map_params.segments = &map[1];
    status = ucc_mem_map(ucc_context, UCC_MEM_MAP_MODE_EXPORT, &map_params, &exchange_size, &local[1]);
    if (status != UCC_OK) {
        fprintf(stderr, "failed to export dest buffer with status %d\n", status);
        abort();
    }
    packed         = malloc(exchange_size);
    rpacked        = malloc(exchange_size * numprocs);
    memcpy(packed, local[1], exchange_size);
    MPI_Barrier(MPI_COMM_WORLD);

    // exchange here
    MPI_Request request;
    MPI_Allgather(packed, exchange_size, MPI_BYTE, rpacked, exchange_size, MPI_BYTE, MPI_COMM_WORLD);

    // import
    map_params.segments = &map[1];
    for (int i = 0; i < numprocs; i++) {
        size_t dummy; /* we do not need a pack size on import */
        global_dest[i] = rpacked + (exchange_size * i);
        status = ucc_mem_map(ucc_context, UCC_MEM_MAP_MODE_IMPORT, &map_params, &dummy, &global_dest[i]);
        if (status != UCC_OK) {
            abort();
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    // perform alltoall
    if (me == 0) {
        printf("performing alltoall\n");
    }
    ucc_coll_args_t a2a_coll = {
        .mask = UCC_COLL_ARGS_FIELD_FLAGS | UCC_COLL_ARGS_FIELD_GLOBAL_WORK_BUFFER | UCC_COLL_ARGS_FIELD_MEM_MAP_SRC_MEMH | UCC_COLL_ARGS_FIELD_MEM_MAP_DST_MEMH,
        .coll_type = UCC_COLL_TYPE_ALLTOALL,
        .src.info = {
            .buffer   = s_buf_heap,
            .count    = 2 * numprocs,
            .datatype = UCC_DT_INT64,
            .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
        },
        .dst.info = {
            .buffer   = r_buf_heap,
            .count    = 2 * numprocs,
            .datatype = UCC_DT_INT64,
            .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
        },
        .flags = UCC_COLL_ARGS_FLAG_MEM_MAPPED_BUFFERS | UCC_COLL_ARGS_FLAG_DST_MEMH_GLOBAL,
        .global_work_buffer   = a2a_psync, /* mapped via context */
        .src_memh.local_memh  = local[0],
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
    while (status == UCC_INPROGRESS) {
        ucc_context_progress(ucc_context);
        status = ucc_collective_test(req);
    }
    if (status != UCC_OK) {
        printf("alltoall failed\n");
    } else {
        ucc_collective_finalize(req);
        if (me == 0) {
            printf("completed alltoall\n");
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    /* unmap global memh first */
    for (int i = 0; i < numprocs; i++) {
        ucc_mem_unmap(&global_dest[i]);
    }
    /* unmap local memhs */
    ucc_mem_unmap(&local[0]);
    ucc_mem_unmap(&local[1]);

    free(packed);
    free(rpacked);
    free(global_dest);
    free(s_buf_heap);
    free(r_buf_heap);
    MPI_Finalize();
    return 0;
}
