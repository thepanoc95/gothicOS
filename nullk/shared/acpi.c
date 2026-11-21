/*
 * null_kernel ACPI table walker.
 *
 * Reads RSDP -> RSDT/XSDT -> FADT/MADT using byte-wise little-endian loads so
 * tables can live at any alignment. No allocation, no libc.
 */
#include "nullk/acpi.h"

#include <stddef.h>

/* Maximum table size we are willing to checksum (guards corrupted lengths). */
#define NK_ACPI_TABLE_LIMIT (1u * 1024u * 1024u)
#define NK_ACPI_TABLE_HDR   36u

static uint16_t nk_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t nk_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t nk_rd64(const uint8_t *p)
{
    return (uint64_t)nk_rd32(p) | ((uint64_t)nk_rd32(p + 4) << 32);
}

static int nk_sig_eq(const uint8_t *p, const char *sig)
{
    uint32_t i;

    for (i = 0u; i < 4u; i++) {
        if (p[i] != (uint8_t)sig[i]) {
            return 0;
        }
    }
    return 1;
}

uint8_t nk_acpi_checksum(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t sum = 0u;
    uint32_t i;

    if (p == (const uint8_t *)0) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

int nk_acpi_rsdp_valid(const uint8_t *rsdp)
{
    if (rsdp == (const uint8_t *)0) {
        return 0;
    }
    if (!nk_sig_eq(rsdp, NK_ACPI_RSDP_SIG)) {
        return 0;
    }
    if (nk_acpi_checksum(rsdp, 20u) != 0u) {
        return 0;
    }
    if (rsdp[15] >= 2u) {
        if (nk_acpi_checksum(rsdp, 36u) != 0u) {
            return 0;
        }
    }
    return 1;
}

/* Validate a table header + checksum, returning its declared length. */
static int nk_acpi_table(const uint8_t *tbl, uint32_t *len_out)
{
    uint32_t len;

    if (tbl == (const uint8_t *)0) {
        return -1;
    }
    len = nk_rd32(tbl + 4);
    if ((len < NK_ACPI_TABLE_HDR) || (len > NK_ACPI_TABLE_LIMIT)) {
        return -1;
    }
    if (nk_acpi_checksum(tbl, len) != 0u) {
        return -1;
    }
    if (len_out != (uint32_t *)0) {
        *len_out = len;
    }
    return 0;
}

static void nk_acpi_zero_info(nk_acpi_info_t *out)
{
    uint8_t *p = (uint8_t *)out;
    uint32_t i;

    for (i = 0u; i < (uint32_t)sizeof(*out); i++) {
        p[i] = 0u;
    }
}

static int nk_acpi_parse_madt(const uint8_t *tbl, uint32_t len,
                              nk_acpi_info_t *out)
{
    /* MADT = 36-byte table header + 8 bytes (LAPIC address, flags). */
    uint32_t pos = 44u;

    if (len < 44u) {
        return -1;
    }
    out->madt_present = 1u;
    out->lapic_addr = (uint64_t)nk_rd32(tbl + 24);
    (void)nk_rd32(tbl + 28); /* MADT flags: reserved for future use. */

    while ((pos + 2u) <= len) {
        uint8_t type = tbl[pos];
        uint8_t rec_len = tbl[pos + 1u];

        if ((rec_len < 2u) || ((uint32_t)rec_len > (len - pos))) {
            return -1;
        }
        if (type == 0u) { /* Processor Local APIC */
            if (rec_len >= 8u) {
                uint32_t flags = nk_rd32(tbl + pos + 4u);

                /* ACPI 1.0 records are 8 bytes with no flags field: they are
                 * implicitly enabled. Otherwise bit 0 is the enabled bit. */
                if ((rec_len == 8u) || ((flags & 1u) != 0u)) {
                    if (out->cpu_count < 0xffffu) {
                        out->cpu_count++;
                    }
                }
            }
        } else if (type == 1u) { /* I/O APIC */
            if (rec_len >= 12u) {
                out->ioapic_addr = (uint64_t)nk_rd32(tbl + pos + 4u);
                out->ioapic_gsi_base = nk_rd32(tbl + pos + 8u);
            }
        }
        pos += rec_len;
    }
    return 0;
}

static uintptr_t nk_acpi_identity(uint64_t phys)
{
    return (uintptr_t)phys;
}

int nk_acpi_parse_with_map(const uint8_t *rsdp, nk_acpi_info_t *out,
                           nk_acpi_addr_fn map)
{
    uint64_t table_addr;
    uint32_t entry_size;
    const uint8_t *root;
    uint32_t root_len;
    uint32_t count;
    uint32_t i;
    int status;

    if ((rsdp == (const uint8_t *)0) || (out == (nk_acpi_info_t *)0) ||
        (map == (nk_acpi_addr_fn)0)) {
        return -1;
    }
    if (!nk_acpi_rsdp_valid(rsdp)) {
        return -1;
    }

    nk_acpi_zero_info(out);
    out->present = 1u;
    out->rsdp = (uint64_t)(uintptr_t)rsdp;

    if (rsdp[15] >= 2u) {
        uint64_t xsdt = nk_rd64(rsdp + 24);

        if (xsdt != 0u) {
            table_addr = xsdt;
            entry_size = 8u;
            if (!nk_sig_eq((const uint8_t *)(uintptr_t)map(table_addr),
                           NK_ACPI_XSDT_SIG)) {
                return -1;
            }
        } else {
            table_addr = (uint64_t)nk_rd32(rsdp + 16);
            entry_size = 4u;
        }
    } else {
        table_addr = (uint64_t)nk_rd32(rsdp + 16);
        entry_size = 4u;
    }
    if (table_addr == 0u) {
        return -1;
    }

    root = (const uint8_t *)(uintptr_t)map(table_addr);
    status = nk_acpi_table(root, &root_len);
    if (status != 0) {
        return status;
    }
    if (!nk_sig_eq(root, NK_ACPI_RSDT_SIG) &&
        !nk_sig_eq(root, NK_ACPI_XSDT_SIG)) {
        return -1;
    }
    if ((root_len - NK_ACPI_TABLE_HDR) < entry_size) {
        return -1;
    }
    count = (root_len - NK_ACPI_TABLE_HDR) / entry_size;

    for (i = 0u; i < count; i++) {
        const uint8_t *entry = root + NK_ACPI_TABLE_HDR +
                               ((uint64_t)i * (uint64_t)entry_size);
        uint64_t addr = (entry_size == 8u) ? nk_rd64(entry)
                                           : (uint64_t)nk_rd32(entry);
        const uint8_t *tbl = (const uint8_t *)(uintptr_t)map(addr);
        uint32_t len;

        if (addr == 0u) {
            continue;
        }
        status = nk_acpi_table(tbl, &len);
        if (status != 0) {
            return status;
        }
        if (out->table_count < 0xffffu) {
            out->table_count++;
        }

        if (nk_sig_eq(tbl, NK_ACPI_FADT_SIG)) {
            if (len < 68u) {
                return -1;
            }
            out->fadt_present = 1u;
            out->pm1a_io_base = nk_rd16(tbl + 64);
            out->pm1b_io_base = nk_rd16(tbl + 66);
        } else if (nk_sig_eq(tbl, NK_ACPI_MADT_SIG)) {
            status = nk_acpi_parse_madt(tbl, len, out);
            if (status != 0) {
                return status;
            }
        }
    }
    return 0;
}

int nk_acpi_parse(const uint8_t *rsdp, nk_acpi_info_t *out)
{
    return nk_acpi_parse_with_map(rsdp, out, nk_acpi_identity);
}
