/*
 * usb64 - USB-as-SATA service running in ring 3.
 *
 * A small RAM disk models the internal SATA drive. The portable bridge turns
 * USB MSC Bulk-Only Transport command blocks into SCSI block commands and
 * dispatches them to that backend. The only ring crossings are the initial
 * syscall setup and periodic logs; no xHCI/AHCI hardware is required here.
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/printk.h"
#include "nullk/usb_as_sata.h"

#define RAM_BLOCKS_PER_LUN (NK_USB64_LUN_BYTES / NK_USB64_BLOCK_SIZE)
#define DEMO_ELF_SIZE 128u

static uint8_t ram_disk[NK_USB64_LUN_COUNT][NK_USB64_LUN_BYTES];
static nk_uas_bridge_t bridge;
static uint32_t cbw_tag;

/*
 * Minimal ET_EXEC ELF64 image: one PT_LOAD covering the file, a 4-byte payload
 * at file offset 120, entry 0x400078. p_offset and p_vaddr are both page
 * aligned, so the image passes nk_elf64_probe() and nk_elf64_load().
 */
static const uint8_t demo_elf[DEMO_ELF_SIZE] = {
    [0] = 0x7f, [1] = 'E', [2] = 'L', [3] = 'F',
    [4] = 2, [5] = 1, [6] = 1,
    [16] = 2,                   /* e_type = ET_EXEC */
    [18] = 62,                  /* e_machine = EM_X86_64 */
    [20] = 1,                   /* e_version */
    [24] = 0x78, [26] = 0x40,   /* e_entry = 0x400078 */
    [32] = 64,                  /* e_phoff */
    [52] = 64,                  /* e_ehsize */
    [54] = 56,                  /* e_phentsize */
    [56] = 1,                   /* e_phnum */
    [64] = 1,                   /* p_type = PT_LOAD */
    [68] = 5,                   /* p_flags = R|X */
    [82] = 0x40,                /* p_vaddr = 0x400000 */
    [96] = DEMO_ELF_SIZE,       /* p_filesz */
    [104] = DEMO_ELF_SIZE,      /* p_memsz */
    [113] = 0x10,               /* p_align = 4096 */
    [120] = 0xb8, [121] = 0x2a  /* mov eax, 42 */
};

/* ---- RAM-disk backend -------------------------------------------------- */

static int ram_bounds(uint32_t lun, uint64_t lba, uint32_t count)
{
    if (lun >= NK_USB64_LUN_COUNT || count == 0) {
        return -1;
    }
    if (lba >= RAM_BLOCKS_PER_LUN || count > RAM_BLOCKS_PER_LUN - lba) {
        return -1;
    }
    return 0;
}

int nk_ramdisk_read(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                    void *buffer)
{
    (void)ctx;
    if (ram_bounds(lun, lba, count) != 0 || buffer == NULL) {
        return -1;
    }
    nk_memcpy(buffer, &ram_disk[lun][(size_t)lba * NK_USB64_BLOCK_SIZE],
              (size_t)count * NK_USB64_BLOCK_SIZE);
    return 0;
}

int nk_ramdisk_write(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                     const void *buffer)
{
    (void)ctx;
    if (ram_bounds(lun, lba, count) != 0 || buffer == NULL) {
        return -1;
    }
    nk_memcpy(&ram_disk[lun][(size_t)lba * NK_USB64_BLOCK_SIZE], buffer,
              (size_t)count * NK_USB64_BLOCK_SIZE);
    return 0;
}

int nk_ramdisk_info(void *ctx, uint32_t lun, nk_uas_lun_info_t *out)
{
    (void)ctx;
    if (lun >= NK_USB64_LUN_COUNT || out == NULL) {
        return -1;
    }
    nk_memset(out, 0, sizeof(*out));
    out->peripheral = 0;        /* direct-access block device */
    out->removable = 0;
    out->vendor_id = 0x4e4bu;    /* "NK" */
    out->product_id = 0x5331u;   /* "S1" */
    out->block_size = NK_USB64_BLOCK_SIZE;
    out->block_count = RAM_BLOCKS_PER_LUN;
    nk_strcpy(out->vendor, "NULLK");
    nk_strcpy(out->product, "RAM-SATA");
    return 0;
}

int usb64_ramdisk_read(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                       void *buffer)
{
    return nk_ramdisk_read(ctx, lun, lba, count, buffer);
}

int usb64_ramdisk_write(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                        const void *buffer)
{
    return nk_ramdisk_write(ctx, lun, lba, count, buffer);
}

/* ---- disk image -------------------------------------------------------- */

static void init_ramdisk(void)
{
    nk_usbprog_header_t hdr;

    nk_memset(ram_disk, 0, sizeof(ram_disk));

    nk_memset(&hdr, 0, sizeof(hdr));
    nk_memcpy(hdr.magic, NK_USBPROG_MAGIC, 8);
    hdr.version = 1;
    hdr.elf_size = (uint32_t)sizeof(demo_elf);
    hdr.elf_lba = 2;
    nk_memcpy(&ram_disk[0][NK_USBPROG_SECTOR * NK_USB64_BLOCK_SIZE],
              &hdr, sizeof(hdr));
    nk_memcpy(&ram_disk[0][2u * NK_USB64_BLOCK_SIZE],
              demo_elf, sizeof(demo_elf));

    nk_strcpy((char *)&ram_disk[1][0], "null_kernel USB LUN 1");
}

/* ---- bridge exercise --------------------------------------------------- */

static void append_text(char *buf, uint32_t cap, uint32_t *len, const char *s)
{
    while (*s != '\0' && *len + 1 < cap) {
        buf[(*len)++] = *s++;
    }
    buf[*len] = '\0';
}

static void log_capacity(uint32_t lun, uint32_t blocks, uint32_t block_size)
{
    char text[128];
    char n1[24];
    char n2[24];
    char n3[24];
    uint32_t len = 0;

    text[0] = '\0';
    append_text(text, sizeof(text), &len, "usb64: lun ");
    append_text(text, sizeof(text), &len, nk_utoa_dec(lun, n1, sizeof(n1)));
    append_text(text, sizeof(text), &len, ": ");
    append_text(text, sizeof(text), &len,
                nk_utoa_dec(blocks, n2, sizeof(n2)));
    append_text(text, sizeof(text), &len, " blocks x ");
    append_text(text, sizeof(text), &len,
                nk_utoa_dec(block_size, n3, sizeof(n3)));
    append_text(text, sizeof(text), &len, " bytes (NKUSBPRG @ LBA 1)\n");
    (void)nk64_log(NK_KERN_INFO, text, len);
    nk_printf("usb64: lun %u: %u blocks x %u bytes\n",
              lun, blocks, block_size);
}

static void probe_capacity(uint32_t lun)
{
    nk_uas_cbw_t cbw;
    nk_uas_csw_t csw;
    uint8_t data[8];
    uint32_t last_lba;
    uint32_t block_size;

    nk_memset(&cbw, 0, sizeof(cbw));
    nk_memset(&csw, 0, sizeof(csw));
    nk_memset(data, 0, sizeof(data));

    nk_memcpy(cbw.signature, NK_UAS_SIG_CBW, 4);
    cbw.tag = ++cbw_tag;
    cbw.data_len = (uint32_t)sizeof(data);
    cbw.flags = (uint8_t)(NK_UAS_DIR_IN << 7);
    cbw.lun = (uint8_t)lun;
    cbw.cdb_len = 10;
    cbw.cdb[0] = NK_SCSI_READ_CAPACITY_10;

    if (nk_uas_exec(&bridge, &cbw, data, sizeof(data), &csw) != 0 ||
        csw.status != NK_BOT_STATUS_OK) {
        nk_printf("usb64: read-capacity failed on lun %u (status %u)\n",
                  lun, csw.status);
        return;
    }

    last_lba = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
               ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    block_size = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                 ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    log_capacity(lun, last_lba + 1, block_size);
}

void usb_service_entry(void)
{
    uint32_t pass = 0;

    init_ramdisk();
    (void)nk64_usb_register(ram_disk, NK_USB64_LUN_COUNT,
                            NK_USB64_BLOCK_SIZE, RAM_BLOCKS_PER_LUN);
    nk_uas_bridge_init(&bridge, NULL, nk_ramdisk_read, nk_ramdisk_write,
                       nk_ramdisk_info);
    nk_printf("usb64: bridge: online (%u LUNs, %u bytes each)\n",
              (uint32_t)NK_USB64_LUN_COUNT, (uint32_t)NK_USB64_LUN_BYTES);

    for (;;) {
        probe_capacity(pass % NK_USB64_LUN_COUNT);
        pass++;
        (void)nk64_sleep(50);
        (void)nk64_yield();
    }
}
