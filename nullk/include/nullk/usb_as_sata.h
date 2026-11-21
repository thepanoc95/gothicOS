/*
 * USB-as-SATA: an internal null_kernel protocol that makes an AHCI/SATA disk
 * appear to the rest of the system as a USB mass-storage device.
 *
 * The bridge accepts USB Mass Storage Class Bulk-Only Transport (BOT) command
 * blocks and translates SCSI block commands into AHCI port reads/writes. A
 * USB xHCI driver can therefore talk to an internal SATA drive using the exact
 * same code path it uses for a plug-in USB stick. In the reverse-monolithic
 * design this lives in the usb userspace service; only the block read/write
 * syscalls cross the ring boundary.
 *
 * Also included: the raw ELF-on-disk format used by the USB program loader.
 */
#ifndef NULLK_USB_AS_SATA_H
#define NULLK_USB_AS_SATA_H

#include <stdint.h>

#define NK_UAS_SIG_CBW "USBC"
#define NK_UAS_SIG_CSW "USBS"
#define NK_UAS_MAX_LUN 4u
#define NK_UAS_BLOCK_SIZE 512u

/* SCSI opcodes understood by the bridge. */
enum {
    NK_SCSI_TEST_UNIT_READY = 0x00,
    NK_SCSI_REQUEST_SENSE = 0x03,
    NK_SCSI_INQUIRY = 0x12,
    NK_SCSI_READ_CAPACITY_10 = 0x25,
    NK_SCSI_READ_10 = 0x28,
    NK_SCSI_WRITE_10 = 0x2a
};

/* BOT status codes (USB MSC spec). */
enum {
    NK_BOT_STATUS_OK = 0,
    NK_BOT_STATUS_FAILED = 1,
    NK_BOT_STATUS_PHASE_ERROR = 2
};

enum {
    NK_UAS_DIR_OUT = 0,
    NK_UAS_DIR_IN = 1
};

typedef struct {
    uint8_t signature[4];   /* "USBC" */
    uint32_t tag;
    uint32_t data_len;      /* expected transfer length */
    uint8_t flags;          /* bit 7: direction */
    uint8_t lun;
    uint8_t cdb_len;
    uint8_t cdb[16];
} __attribute__((packed)) nk_uas_cbw_t;

typedef struct {
    uint8_t signature[4];   /* "USBS" */
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed)) nk_uas_csw_t;

typedef struct {
    uint8_t peripheral;
    uint8_t removable;
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t block_size;
    uint64_t block_count;
    char vendor[8];
    char product[16];
} nk_uas_lun_info_t;

/* Block backend: implemented by the AHCI driver, a RAM disk, or a USB
 * transport. buffer is always block_size * count bytes. */
typedef int (*nk_uas_read_fn)(void *ctx, uint32_t lun, uint64_t lba,
                              uint32_t count, void *buffer);
typedef int (*nk_uas_write_fn)(void *ctx, uint32_t lun, uint64_t lba,
                               uint32_t count, const void *buffer);
typedef int (*nk_uas_info_fn)(void *ctx, uint32_t lun, nk_uas_lun_info_t *out);

typedef struct {
    void *backend_ctx;
    nk_uas_read_fn read;
    nk_uas_write_fn write;
    nk_uas_info_fn info;
    uint32_t tag;
} nk_uas_bridge_t;

/* Bridge API. */
void nk_uas_bridge_init(nk_uas_bridge_t *bridge, void *backend_ctx,
                        nk_uas_read_fn read, nk_uas_write_fn write,
                        nk_uas_info_fn info);

/* Validate a CBW (signature, LUN, CDB length, direction/data length sanity). */
int nk_uas_cbw_valid(const nk_uas_cbw_t *cbw);

/* Execute one CBW. On success writes the returned payload to out (up to
 * out_cap) and fills csw; returns 0. On failure returns negative and still
 * fills csw with the appropriate BOT failure status. */
int nk_uas_exec(nk_uas_bridge_t *bridge, const nk_uas_cbw_t *cbw,
                void *out, uint32_t out_cap, nk_uas_csw_t *csw);

/* Build a READ_CAPACITY_10 reply (8 bytes) for a LUN. */
int nk_uas_read_capacity(const nk_uas_bridge_t *bridge, uint32_t lun,
                         uint8_t out[8]);

/* USB program loader on-disk header. The ELF image follows this 512-byte
 * sector verbatim; this keeps the loader independent of any filesystem. */
#define NK_USBPROG_MAGIC "NKUSBPRG"
#define NK_USBPROG_SECTOR 1u

typedef struct {
    uint8_t magic[8];       /* "NKUSBPRG" */
    uint32_t version;       /* 1 */
    uint32_t elf_size;
    uint64_t elf_lba;       /* first 512-byte sector of ELF payload */
    uint8_t reserved[484];
} __attribute__((packed)) nk_usbprog_header_t;

/* Validate a downloaded program header. */
int nk_usbprog_valid(const nk_usbprog_header_t *hdr);

#endif /* NULLK_USB_AS_SATA_H */
