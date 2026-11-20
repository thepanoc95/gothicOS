#include "kern.h"

void nkk_cap_init(void) {
    /* Per-task tables are zeroed with their TCBs. */
}

nk_handle_t nkk_cap_install(int task_id, uint8_t type, uint16_t obj,
                           uint8_t rights) {
    nkk_tcb_t *t = nkk_task_by_id(task_id);
    uint32_t i;
    if (t == 0 || !t->in_use) {
        return 0;
    }
    for (i = 1; i < NK_CAP_PER_TASK; i++) {
        if (t->cap_handle[i] == 0) {
            t->cap_handle[i] = (nk_handle_t)i;
            t->cap_obj[i] = obj;
            t->cap_type[i] = type;
            t->cap_rights[i] = rights;
            return (nk_handle_t)i;
        }
    }
    return 0;
}

nk_status_t nkk_cap_lookup(int task_id, nk_handle_t h, uint8_t type,
                           uint8_t right, uint16_t *obj_out) {
    nkk_tcb_t *t;
    if (h == 0 || h >= NK_CAP_PER_TASK || obj_out == 0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    t = nkk_task_by_id(task_id);
    if (t == 0 || !t->in_use) {
        return NK_STATUS_NOT_FOUND;
    }
    if (t->cap_handle[h] != h) {
        return NK_STATUS_NOT_FOUND;
    }
    if (type != 0 && t->cap_type[h] != type) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    if ((t->cap_rights[h] & right) != right) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    *obj_out = t->cap_obj[h];
    return NK_STATUS_OK;
}

nk_status_t nkk_cap_grant(int from_id, int to_id, nk_handle_t h,
                          uint32_t rights, nk_handle_t *out) {
    uint16_t obj;
    uint8_t src_rights;
    nkk_tcb_t *src = nkk_task_by_id(from_id);
    nkk_tcb_t *dst = nkk_task_by_id(to_id);
    nk_handle_t nh;

    if (dst == 0 || !dst->in_use || out == 0 || rights == 0 ||
        (rights & ~(uint32_t)NK_RIGHT_ALL) != 0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (nkk_cap_lookup(from_id, h, 0, NK_RIGHT_GRANT, &obj) != NK_STATUS_OK) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    src = nkk_task_by_id(from_id);
    if (src == 0) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    src_rights = src->cap_rights[h];
    if ((rights & ~(uint32_t)src_rights) != 0) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    nh = nkk_cap_install(to_id, src->cap_type[h], obj, (uint8_t)rights);
    if (nh == 0) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    *out = nh;
    return NK_STATUS_OK;
}

nk_status_t nkk_cap_revoke(int from_id, int to_id, nk_handle_t h) {
    nkk_tcb_t *dst = nkk_task_by_id(to_id);
    uint16_t obj;

    if (dst == 0 || !dst->in_use || h == 0 || h >= NK_CAP_PER_TASK) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (nkk_cap_lookup(from_id, h, 0, NK_RIGHT_GRANT, &obj) != NK_STATUS_OK) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    dst->cap_handle[h] = 0;
    dst->cap_obj[h] = 0;
    dst->cap_type[h] = NKK_OBJ_NONE;
    dst->cap_rights[h] = 0;
    return NK_STATUS_OK;
}
