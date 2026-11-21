/*
 * null_kernel USB-as-SATA bridge.
 *
 * Translates USB Mass Storage Bulk-Only Transport CBWs into block backend
 * calls. The bridge is transport independent: the AMD62 port supplies AHCI
 * callbacks, the host test supplies a RAM disk. No libc, no allocation.
 */
#include "nullk/usb_as_sata.h"

#include <stddef.h>

#define NK_UAS_MAX_TRANSFER (2u * 1024u * 1024u)

static void nk_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

static void nk_memset(void *dst, int value, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = (uint8_t)value;
    }
}

static int nk_sig_eq(const uint8_t *p, const char *sig, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        if (p[i] != (uint8_t)sig[i]) {
            return 0;
        }
    }
    return 1;
}

static void nk_put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void nk_fill_csw(nk_uas_csw_t *csw, uint32_t tag, uint32_t residue,
                        uint8_t status)
{
    if (csw == (nk_uas_csw_t *)0) {
        return;
    }
    csw->signature[0] = 'U';
    csw->signature[1] = 'S';
    csw->signature[2] = 'B';
    csw->signature[3] = 'S';
    csw->tag = tag;
    csw->residue = residue;
    csw->status = status;
}

static uint32_t nk_residue(uint32_t data_len, uint32_t actual)
{
    return (data_len > actual) ? (data_len - actual) : 0u;
}

void nk_uas_bridge_init(nk_uas_bridge_t *bridge, void *backend_ctx,
                        nk_uas_read_fn read, nk_uas_write_fn write,
                        nk_uas_info_fn info)
{
    if (bridge == (nk_uas_bridge_t *)0) {
        return;
    }
    bridge->backend_ctx = backend_ctx;
    bridge->read = read;
    bridge->write = write;
    bridge->info = info;
    bridge->tag = 0u;
}

int nk_uas_cbw_valid(const nk_uas_cbw_t *cbw)
{
    if (cbw == (const nk_uas_cbw_t *)0) {
        return 0;
    }
    if (!nk_sig_eq(cbw->signature, NK_UAS_SIG_CBW, 4u)) {
        return 0;
    }
    if ((cbw->cdb_len < 6u) || (cbw->cdb_len > 16u)) {
        return 0;
    }
    if (cbw->lun >= NK_UAS_MAX_LUN) {
        return 0;
    }
    if (cbw->data_len > NK_UAS_MAX_TRANSFER) {
        return 0;
    }
    if ((cbw->cdb[0] == NK_SCSI_READ_10) ||
        (cbw->cdb[0] == NK_SCSI_WRITE_10)) {
        if ((cbw->cdb_len < 10u) || ((cbw->data_len % NK_UAS_BLOCK_SIZE) != 0u)) {
            return 0;
        }
    }
    return 1;
}

int nk_uas_read_capacity(const nk_uas_bridge_t *bridge, uint32_t lun,
                         uint8_t out[8])
{
    nk_uas_lun_info_t info;
    uint64_t block_count = 0u;
    uint32_t block_size = NK_UAS_BLOCK_SIZE;
    uint64_t last_lba;

    if ((bridge == (const nk_uas_bridge_t *)0) ||
        (out == (uint8_t *)0)) {
        return -1;
    }
    nk_memset(&info, 0, (uint32_t)sizeof(info));
    if ((bridge->info != (nk_uas_info_fn)0) &&
        (bridge->info(bridge->backend_ctx, lun, &info) == 0)) {
        block_count = info.block_count;
        if (info.block_size != 0u) {
            block_size = info.block_size;
        }
    }
    last_lba = (block_count != 0u) ? (block_count - 1u) : 0u;
    /* READ_CAPACITY_10 reports a 32-bit LBA; clamp very large disks. */
    if (last_lba > 0xffffffffu) {
        last_lba = 0xffffffffu;
    }
    nk_put_be32(out, (uint32_t)last_lba);
    nk_put_be32(out + 4, block_size);
    return 0;
}

/* Copy a small reply to the caller's buffer. Returns the number of bytes
 * actually written; sets *ok to 0 when a non-empty payload has no buffer. */
static uint32_t nk_uas_emit(const nk_uas_cbw_t *cbw, void *out,
                            uint32_t out_cap, const uint8_t *src,
                            uint32_t len, int *ok)
{
    uint32_t n = (len < cbw->data_len) ? len : cbw->data_len;

    if (n > out_cap) {
        n = out_cap;
    }
    if ((n != 0u) && (out == (void *)0)) {
        *ok = 0;
        return 0u;
    }
    if (n != 0u) {
        nk_memcpy(out, src, n);
    }
    *ok = 1;
    return n;
}

int nk_uas_exec(nk_uas_bridge_t *bridge, const nk_uas_cbw_t *cbw,
                void *out, uint32_t out_cap, nk_uas_csw_t *csw)
{
    nk_uas_cbw_t cmd;
    nk_uas_lun_info_t info;
    uint32_t tag;
    uint32_t actual = 0u;
    uint32_t lba;
    uint32_t count;
    uint32_t block_size = NK_UAS_BLOCK_SIZE;
    uint64_t needed;
    int ok = 1;
    int backend_status;

    if (bridge == (nk_uas_bridge_t *)0) {
        nk_fill_csw(csw, 0u, 0u, NK_BOT_STATUS_FAILED);
        return -1;
    }
    if (cbw == (const nk_uas_cbw_t *)0) {
        nk_fill_csw(csw, bridge->tag, 0u, NK_BOT_STATUS_FAILED);
        return -1;
    }

    /* Copy the CBW before dispatching so aliasing with `out` cannot change
     * the command mid-flight. The tag is verified by echoing it unchanged in
     * the CSW and recording it on the bridge. */
    nk_memcpy(&cmd, cbw, sizeof(cmd));
    tag = cmd.tag;
    if (nk_uas_cbw_valid(&cmd) == 0) {
        nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
        return -1;
    }
    bridge->tag = tag;

    switch (cmd.cdb[0]) {
    case NK_SCSI_TEST_UNIT_READY:
        /* No data phase: every advertised byte is residue. */
        nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_OK);
        return 0;

    case NK_SCSI_INQUIRY: {
        uint8_t reply[36];

        nk_memset(reply, 0, (uint32_t)sizeof(reply));
        nk_memset(&info, 0, (uint32_t)sizeof(info));
        if ((bridge->info != (nk_uas_info_fn)0) &&
            (bridge->info(bridge->backend_ctx, cmd.lun, &info) == 0)) {
            uint32_t i;

            for (i = 0u; (i < 8u) && (info.vendor[i] != '\0'); i++) {
                reply[8u + i] = (uint8_t)info.vendor[i];
            }
            for (i = 0u; (i < 16u) && (info.product[i] != '\0'); i++) {
                reply[16u + i] = (uint8_t)info.product[i];
            }
        } else {
            const char vendor[] = "NULLK   ";
            const char product[] = "DISK            ";
            uint32_t i;

            for (i = 0u; i < 8u; i++) {
                reply[8u + i] = (uint8_t)vendor[i];
            }
            for (i = 0u; i < 16u; i++) {
                reply[16u + i] = (uint8_t)product[i];
            }
        }
        reply[0] = 0x00u;       /* direct-access block device */
        reply[1] = 0x80u;       /* removable */
        reply[2] = 0x01u;       /* version 0x0100 */
        reply[3] = 0x00u;
        reply[4] = 31u;         /* additional length */
        reply[32] = '0';
        reply[33] = '0';
        reply[34] = '0';
        reply[35] = '1';
        actual = nk_uas_emit(&cmd, out, out_cap, reply,
                             (uint32_t)sizeof(reply), &ok);
        break;
    }

    case NK_SCSI_READ_CAPACITY_10: {
        uint8_t reply[8];

        if (nk_uas_read_capacity(bridge, cmd.lun, reply) != 0) {
            nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
            return -1;
        }
        actual = nk_uas_emit(&cmd, out, out_cap, reply,
                             (uint32_t)sizeof(reply), &ok);
        break;
    }

    case NK_SCSI_READ_10:
        lba = ((uint32_t)cmd.cdb[2] << 24) | ((uint32_t)cmd.cdb[3] << 16) |
              ((uint32_t)cmd.cdb[4] << 8) | (uint32_t)cmd.cdb[5];
        count = ((uint32_t)cmd.cdb[7] << 8) | (uint32_t)cmd.cdb[8];
        if (count == 0u) {
            count = 256u; /* SCSI READ_10: zero means 256 blocks */
        }
        nk_memset(&info, 0, (uint32_t)sizeof(info));
        if ((bridge->info != (nk_uas_info_fn)0) &&
            (bridge->info(bridge->backend_ctx, cmd.lun, &info) == 0) &&
            (info.block_size != 0u)) {
            block_size = info.block_size;
        }
        needed = (uint64_t)block_size * (uint64_t)count;
        if ((needed > (uint64_t)out_cap) || (needed > (uint64_t)cmd.data_len) ||
            (bridge->read == (nk_uas_read_fn)0)) {
            nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
            return -1;
        }
        backend_status = bridge->read(bridge->backend_ctx, cmd.lun, lba,
                                      count, out);
        if (backend_status != 0) {
            nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
            return -1;
        }
        actual = (uint32_t)needed;
        break;

    case NK_SCSI_WRITE_10:
        /* This bridge API has no data-in parameter. In the full BOT driver the
         * WRITE_10 payload arrives in a separate OUT data phase and is passed
         * to bridge->write; until such a parameter exists we parse the CDB but
         * must report failure rather than fabricate sector contents. */
        lba = ((uint32_t)cmd.cdb[2] << 24) | ((uint32_t)cmd.cdb[3] << 16) |
              ((uint32_t)cmd.cdb[4] << 8) | (uint32_t)cmd.cdb[5];
        count = ((uint32_t)cmd.cdb[7] << 8) | (uint32_t)cmd.cdb[8];
        (void)lba;
        (void)count;
        nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
        return -1;

    default:
        nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
        return -1;
    }

    if (!ok) {
        nk_fill_csw(csw, tag, cmd.data_len, NK_BOT_STATUS_FAILED);
        return -1;
    }
    nk_fill_csw(csw, tag, nk_residue(cmd.data_len, actual), NK_BOT_STATUS_OK);
    return 0;
}

int nk_usbprog_valid(const nk_usbprog_header_t *hdr)
{
    if (hdr == (const nk_usbprog_header_t *)0) {
        return 0;
    }
    if (!nk_sig_eq(hdr->magic, NK_USBPROG_MAGIC, 8u)) {
        return 0;
    }
    if (hdr->version != 1u) {
        return 0;
    }
    if ((hdr->elf_size == 0u) || (hdr->elf_size > NK_UAS_MAX_TRANSFER)) {
        return 0;
    }
    if (hdr->elf_lba < 2u) {
        return 0;
    }
    return 1;
}
