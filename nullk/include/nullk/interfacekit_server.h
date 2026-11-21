/*
 * InterfaceKit AMD64 server API.
 *
 * The portable drawing core is in interfacekit.h. This header adds the ring-3
 * desktop server contract: bounded windows and routed ABI v3 input events.
 * It is only used by AMD64 services; embedded and ARM64 profiles can build
 * InterfaceKit widgets without any of these types.
 */
#ifndef NULLK_INTERFACEKIT_SERVER_H
#define NULLK_INTERFACEKIT_SERVER_H

#include <stdint.h>

#include "nullk/abi64.h"
#include "nullk/interfacekit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INTERFACEKIT_MAX_WINDOWS 8u
#define INTERFACEKIT_TITLE_MAX   24u
#define INTERFACEKIT_EVENT_QUEUE 16u
#define INTERFACEKIT_POOL_BYTES  (1024u * 1024u)

uint32_t interfacekit_server_ready(void);
uint32_t interfacekit_desktop_width(void);
uint32_t interfacekit_desktop_height(void);
uint32_t interfacekit_window_count(void);

uint32_t interfacekit_window_create(const char *title, int32_t x, int32_t y,
                                    uint32_t width, uint32_t height);
int interfacekit_window_destroy(uint32_t id);
int interfacekit_window_move(uint32_t id, int32_t x, int32_t y);
int interfacekit_window_raise(uint32_t id);

/* Returns the drawable back buffer, or NULL for an invalid window. */
interfacekit_pixmap_t *interfacekit_window_backbuffer(uint32_t id);
int interfacekit_window_present(uint32_t id);
int interfacekit_window_push_event(uint32_t id,
                                   const nk64_input_event_t *event);
uint32_t interfacekit_window_pop_event(uint32_t id, nk64_input_event_t *out);

/* Ring-3 service entry point, created by init64. */
void interfacekit_server_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_INTERFACEKIT_SERVER_H */
