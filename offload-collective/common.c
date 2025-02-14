#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

void *a2a_psync;

static ucc_status_t oob_allgather(void *sbuf, void *rbuf, size_t msglen,
                                   void *coll_info, void **req)
{
    MPI_Comm    comm = (MPI_Comm)(uintptr_t)coll_info;
    MPI_Request request;
    MPI_Iallgather(sbuf, msglen, MPI_BYTE, rbuf, msglen, MPI_BYTE, comm,
                   &request);
    *req = calloc(1, sizeof(ucc_status_t));
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
    ucc_context_h ucc_context;
    ucc_team_h  ucc_team;
    ucc_lib_h ucc_lib;
    ucc_team_params_t team_params = {0};
    ucc_lib_config_h lib_config;
    ucc_lib_params_t lib_params;
    ucc_context_config_h ctx_config;
    ucc_context_params_t ctx_params = {0};
    ucc_status_t status;
    ucc_mem_map_t map;

    /* make ucc here */
    lib_params.mask = UCC_LIB_PARAM_FIELD_THREAD_MODE;
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

    a2a_psync = calloc(1,128);
    map.address = a2a_psync;
    map.len = 128;

    ctx_params.mask = UCC_CONTEXT_PARAM_FIELD_OOB | UCC_CONTEXT_PARAM_FIELD_MEM_PARAMS;
    ctx_params.oob.allgather = oob_allgather;
    ctx_params.oob.req_test  = oob_allgather_test;
    ctx_params.oob.req_free  = oob_allgather_free;
    ctx_params.oob.coll_info = MPI_COMM_WORLD;
    ctx_params.oob.n_oob_eps = size;
    ctx_params.oob.oob_ep    = rank;
    ctx_params.mem_params.segments = &map;
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
    team_params.oob.req_test = oob_allgather_test;
    team_params.oob.req_free = oob_allgather_free;
    team_params.oob.coll_info = MPI_COMM_WORLD;
    team_params.oob.n_oob_eps = size;
    team_params.oob.oob_ep = rank;
    team_params.ep = rank;

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

char * get_server_name(void) {
    char *dpu_ip = NULL;
    char file_host_ip[64];
    char file_dpu_ip[64];
    char *ip = malloc(64);
    int ret;
    FILE *fp;

    ret = gethostname(ip, 64);
    if (ret != 0) {
        fprintf(stderr, "could not get hostname");
        goto out;
    }

    // open test.dat (format: hostname dpu_hostname)
    fp = fopen("test.dat", "r");
    if (!fp) {
        fprintf(stderr, "Could not open test.dat");
        goto out;
    }
    // parse until reaching our hostname
    ret = fscanf(fp, "%s %s", file_host_ip, file_dpu_ip);
    if (ret <= 0) {
        fprintf(stderr, "error when reading");
        goto out;
    }
    while (ret >= 0) {
        if (strncmp(file_host_ip, ip, 64) == 0) {
            dpu_ip = malloc(strnlen(file_dpu_ip, 64));
            strncpy(dpu_ip, file_dpu_ip, strnlen(file_dpu_ip, 64));
            free(ip);
            fclose(fp);
            return dpu_ip;
        }
        ret = fscanf(fp, "%s %s", file_host_ip, file_dpu_ip);
    }
    fclose(fp);
out:
    free(ip);
    return dpu_ip;
}
