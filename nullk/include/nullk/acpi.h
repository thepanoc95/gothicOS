/*
 * ACPI table walker (RSDP -> RSDT/XSDT -> FADT/MADT). Host-testable and
 * independence from the boot loader: the caller supplies a pointer to the
 * RSDP (in the AMD64 port this is the Limine RSDP response, HHDM mapped).
 */
#ifndef NULLK_ACPI_H
#define NULLK_ACPI_H

#include <stdint.h>

#define NK_ACPI_RSDP_SIG "RSD PTR "
#define NK_ACPI_RSDT_SIG "RSDT"
#define NK_ACPI_XSDT_SIG "XSDT"
#define NK_ACPI_FADT_SIG "FADT"
#define NK_ACPI_MADT_SIG "APIC"

typedef struct {
    uint8_t present;
    uint8_t madt_present;
    uint8_t fadt_present;
    uint8_t reserved;
    uint64_t rsdp;
    uint64_t lapic_addr;
    uint64_t ioapic_addr;
    uint32_t ioapic_gsi_base;
    uint16_t cpu_count;
    uint16_t table_count;
    uint16_t pm1a_io_base;
    uint16_t pm1b_io_base;
} nk_acpi_info_t;

/* Standard ACPI checksum: sum of all bytes must be zero mod 256. */
uint8_t nk_acpi_checksum(const void *data, uint32_t len);

/* Returns 1 when the 20-byte legacy RSDP or 36-byte ACPI2 RSDP is valid. */
int nk_acpi_rsdp_valid(const uint8_t *rsdp);

/*
 * Translate a physical address embedded in an ACPI table into an address the
 * walker can dereference. Hosted builds use identity translation; the AMD64
 * port supplies its HHDM mapping.
 */
typedef uintptr_t (*nk_acpi_addr_fn)(uint64_t phys);

/* Walk the tables. Returns 0 on success, negative on malformed data. */
int nk_acpi_parse_with_map(const uint8_t *rsdp, nk_acpi_info_t *out,
                           nk_acpi_addr_fn map);
int nk_acpi_parse(const uint8_t *rsdp, nk_acpi_info_t *out);

#endif /* NULLK_ACPI_H */
