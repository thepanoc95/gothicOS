#include "kern.h"

void nkk_mem_init(void) {
    for (uint32_t i = 0; i < NK_MEM_BLOCK_COUNT; i++) {
        nkk_blocks[i].used = 0;
        nkk_blocks[i].owner = 0;
        for (uint32_t j = 0; j < NK_MEM_BLOCK_SIZE; j++) {
            nkk_blocks[i].data[j] = 0;
        }
    }
}

nk_status_t nkk_mem_alloc(int owner, nk_mem_desc_t *out) {
    uint32_t id;
    if (out == 0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    for (id = 0; id < NK_MEM_BLOCK_COUNT; id++) {
        if (!nkk_blocks[id].used) {
            break;
        }
    }
    if (id >= NK_MEM_BLOCK_COUNT) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    nkk_blocks[id].used = 1;
    nkk_blocks[id].owner = (uint16_t)owner;
    for (uint32_t j = 0; j < NK_MEM_BLOCK_SIZE; j++) {
        nkk_blocks[id].data[j] = 0;
    }
    out->block_id = id;
    out->ptr = nkk_blocks[id].data;
    return NK_STATUS_OK;
}

nk_status_t nkk_mem_free(uint32_t block_id) {
    if (block_id >= NK_MEM_BLOCK_COUNT || !nkk_blocks[block_id].used) {
        return NK_STATUS_NOT_FOUND;
    }
    for (uint32_t j = 0; j < NK_MEM_BLOCK_SIZE; j++) {
        nkk_blocks[block_id].data[j] = 0;
    }
    nkk_blocks[block_id].used = 0;
    nkk_blocks[block_id].owner = 0;
    return NK_STATUS_OK;
}

void nkk_mem_info(nk_mem_info_t *out) {
    uint32_t used = 0;
    for (uint32_t i = 0; i < NK_MEM_BLOCK_COUNT; i++) {
        if (nkk_blocks[i].used) {
            used++;
        }
    }
    out->block_size = NK_MEM_BLOCK_SIZE;
    out->block_count = NK_MEM_BLOCK_COUNT;
    out->used = used;
    out->reserved = 0;
}
