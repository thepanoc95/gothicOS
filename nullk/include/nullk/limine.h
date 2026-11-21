/* SPDX-License-Identifier: 0BSD
 * Trimmed subset of the official Limine boot protocol header
 * (limine-protocol/include/limine.h). Only the requests the AMD64 port uses
 * are declared. The IDs below are copied verbatim from the protocol. */
#ifndef NULLK_LIMINE_H
#define NULLK_LIMINE_H

#include <stdint.h>

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL

#define LIMINE_REQUESTS_START_MARKER                                      \
    { 0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL,                       \
      0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL }
#define LIMINE_REQUESTS_END_MARKER                                        \
    { 0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL }
#define LIMINE_BASE_REVISION(N)                                           \
    { 0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (N) }
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)
#define LIMINE_LOADED_BASE_REVISION_VALID(VAR) ((VAR)[1] != 0x6a7b384944536bdcULL)
#define LIMINE_LOADED_BASE_REVISION(VAR) ((VAR)[1])

#define LIMINE_STACK_SIZE_REQUEST_ID                                      \
    { LIMINE_COMMON_MAGIC, 0x224ef0460a8e8926ULL, 0xe1cb0fc25f46ea3dULL }
#define LIMINE_HHDM_REQUEST_ID                                            \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL }
#define LIMINE_MEMMAP_REQUEST_ID                                          \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }
#define LIMINE_MODULE_REQUEST_ID                                          \
    { LIMINE_COMMON_MAGIC, 0x3e7e279702be32afULL, 0xca1c4f3bd1280ceeULL }
#define LIMINE_RSDP_REQUEST_ID                                            \
    { LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43ULL, 0x27637845accdcf3cULL }
#define LIMINE_KERNEL_ADDRESS_REQUEST_ID                                  \
    { LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63ULL, 0xb2644a48c516a487ULL }
#define LIMINE_ENTRY_POINT_REQUEST_ID                                     \
    { LIMINE_COMMON_MAGIC, 0x13d86c035a1cd3e1ULL, 0x2b0caa89d8f3026aULL }
#define LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID                              \
    { LIMINE_COMMON_MAGIC, 0x4b161536e598651eULL, 0xb390ad4a2f1f303aULL }
#define LIMINE_FRAMEBUFFER_REQUEST_ID                                     \
    { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL }

struct limine_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *string;
    uint32_t media_type;
    uint32_t unused;
    uint8_t tftp_ipv4[4];
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct limine_uuid gpt_disk_uuid;
    struct limine_uuid gpt_part_uuid;
    struct limine_uuid part_uuid;
};

struct limine_stack_size_response { uint64_t revision; };
struct limine_stack_size_request {
    uint64_t id[4], revision;
    struct limine_stack_size_response *response;
    uint64_t stack_size;
};

struct limine_hhdm_response { uint64_t revision, offset; };
struct limine_hhdm_request {
    uint64_t id[4], revision;
    struct limine_hhdm_response *response;
};

#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_MEMMAP_RESERVED 1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2
#define LIMINE_MEMMAP_ACPI_NVS 3
#define LIMINE_MEMMAP_BAD_MEMORY 4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER 7
struct limine_memmap_entry { uint64_t base, length, type; };
struct limine_memmap_response {
    uint64_t revision, entry_count;
    struct limine_memmap_entry **entries;
};
struct limine_memmap_request {
    uint64_t id[4], revision;
    struct limine_memmap_response *response;
};

#define LIMINE_MEDIA_TYPE_GENERIC 0
#define LIMINE_MEDIA_TYPE_OPTICAL 1
#define LIMINE_MEDIA_TYPE_TFTP 2
struct limine_module_response {
    uint64_t revision, module_count;
    struct limine_file **modules;
};
struct limine_module_request {
    uint64_t id[4], revision;
    struct limine_module_response *response;
    uint64_t internal_module_count;
    const char **internal_modules;
};

struct limine_rsdp_response { uint64_t revision; void *address; };
struct limine_rsdp_request {
    uint64_t id[4], revision;
    struct limine_rsdp_response *response;
};

struct limine_kernel_address_response {
    uint64_t revision, physical_base, virtual_base;
};
struct limine_kernel_address_request {
    uint64_t id[4], revision;
    struct limine_kernel_address_response *response;
};

typedef void (*limine_entry_point_fn)(void);
struct limine_entry_point_response { uint64_t revision; };
struct limine_entry_point_request {
    uint64_t id[4], revision;
    struct limine_entry_point_response *response;
    limine_entry_point_fn entry;
};

struct limine_executable_cmdline_response { uint64_t revision; char *cmdline; };
struct limine_executable_cmdline_request {
    uint64_t id[4], revision;
    struct limine_executable_cmdline_response *response;
};

struct limine_framebuffer {
    void *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t memory_model, red_mask_size, red_mask_shift;
    uint8_t green_mask_size, green_mask_shift;
    uint8_t blue_mask_size, blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
};
struct limine_framebuffer_response {
    uint64_t revision, framebuffer_count;
    struct limine_framebuffer **framebuffers;
};
struct limine_framebuffer_request {
    uint64_t id[4], revision;
    struct limine_framebuffer_response *response;
};

struct limine_base_revision { uint64_t id[3]; };

#endif /* NULLK_LIMINE_H */
