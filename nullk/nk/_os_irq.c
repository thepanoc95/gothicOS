#include "kern.h"

void nkk_irq_init(void) {
    for (uint32_t i = 0; i < NK_SWIRQ_COUNT; i++) {
        nkk_irqs[i].bound = 0;
        nkk_irqs[i].endpoint = 0;
        nkk_irqs[i].enabled = 0;
        nkk_irqs[i].reserved = 0;
    }
}

nk_status_t nkk_irq_bind(uint32_t source, nk_handle_t h, int task_id) {
    uint16_t idx = 0;
    nk_status_t st;

    if (source >= NK_SWIRQ_COUNT) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (h != 0) {
        st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT, NK_RIGHT_WRITE, &idx);
        if (st != NK_STATUS_OK) {
            return st;
        }
    }
    nkk_irqs[source].bound = (uint16_t)(h != 0);
    nkk_irqs[source].endpoint = idx;
    nkk_irqs[source].enabled = (uint16_t)(h != 0);
    return NK_STATUS_OK;
}

nk_status_t nkk_irq_unbind(uint32_t source, int task_id) {
    (void)task_id;
    if (source >= NK_SWIRQ_COUNT) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    nkk_irqs[source].bound = 0;
    nkk_irqs[source].endpoint = 0;
    nkk_irqs[source].enabled = 0;
    return NK_STATUS_OK;
}

nk_status_t nkk_irq_raise(uint32_t source, uint32_t payload) {
    if (source >= NK_SWIRQ_COUNT) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (!nkk_irqs[source].bound) {
        return NK_STATUS_NOT_FOUND;
    }
    nkk_ipc_post_irq(source, payload);
    return NK_STATUS_OK;
}
