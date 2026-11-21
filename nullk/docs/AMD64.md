# null_kernel on AMD64 (reverse-monolithic port)

## Design

The AMD64 port inverts the classic null_kernel layout. Ring 0 is a tiny
higher-half executive; almost everything else is a freestanding ring 3 task.
The kernel and userspace talk only through **NULLK ABI v3** (`syscall`:
`rax` = number, args in `rdi/rsi/rdx/r10/r8/r9`).

| Runs in ring 0 (kernel64/, arch/amd64/) | Runs in ring 3 (apps/) |
| --- | --- |
| Boot (Limine), HHDM + page tables, IDT/PIC, PIT | `init64` (first task), `nksh` shell |
| Scheduler, task switch, syscall dispatch | `usb64` USB-as-SATA service |
| Physical frames, user mapping, ELF loading | `@/` filesystem walk, program loading |
| UART driver, `printk`, emergency fallback shell | `libnk64` syscall wrappers + formatter |

Userspace binaries are freestanding ELF64 `ET_EXEC` images linked at
`0x400000` (2 MiB aligned), with `PT_LOAD` segments and `void _start(void)`.
The kernel maps a NULL pointer at the top of the user stack and jumps to
`_start`. There is no libc, no dynamic linker and no POSIX layer.

All three services are linked into one small `init.elf` for the test profile;
`_start` creates the `usb64` and `nksh` tasks and then sleeps forever.

## Build and test

```sh
make -C nullk BOARD=amd64 test
make -C nullk BOARD=amd64 all
make -C nullk BOARD=amd64 qemu LIMINE_DIR=/path/to/limine
```

Outputs (under `build/NullKernel/amd64/`):

- `null_kernel_amd64.elf` / `.bin` - tiny ring 0 kernel
- `init.elf` - combined userspace image (init + shell + USB service)
- `initrd.tar` - USTAR tree, built by `tools/mkinitrd.py`

To boot without the Make target:

```sh
qemu-system-x86_64 -m 16M -M q35 -cdrom build/NullKernel/amd64/null_kernel.iso \
    -nographic -serial mon:stdio
```

`-m 16M` is deliberate: the test profile has exactly 16 MiB of RAM and all
kernel arrays are sized for that limit.

Run the host-side tests of the portable logic with `make -C nullk BOARD=amd64
test`; they do not require Limine or QEMU.

## Limine deployment

1. Install Limine (or set `LIMINE_DIR` to a directory containing
   `limine.sys`, `limine-cd.bin` and `limine-cd-efi.bin`).
2. Edit `nullk/limine.cfg` as needed. The shipped entry expects the media to
   expose:

   ```
   boot:///build/NullKernel/amd64/null_kernel_amd64.elf
   boot:///build/NullKernel/amd64/initrd.tar
   ```

   with `CMDLINE=root=@/ loglevel=6 mem=16M`.
3. `make -C nullk BOARD=amd64 limine` assembles a hybrid ISO; `... qemu` boots
   it. `make ... qemu LIMINE_DIR=...` overrides the Limine install path.
4. A USB stick can supply modules with Limine's `usb:///` protocol, e.g.
   `MODULE_PATH=usb:///program.elf`. The USB program loader consumes the
   `NKUSBPRG` header on LBA 1 and the ELF payload starting at LBA 2.

## The `@/` filesystem

`@/` is the null_kernel namespace marker used in the bootloader config. The
kernel unpacks the USTAR initrd into it before entering userspace:

```
@/
├── data/
│   ├── system/
│   │   ├── null_kernel.elf      # AMD64 kernel image
│   │   ├── limine.cfg           # bootloader config
│   │   ├── init.elf             # first ring 3 task
│   │   ├── sh.elf               # informational shell copy
│   │   └── usb.elf              # informational USB service copy
│   └── user/
│       └── bin/
│           └── README.txt       # explains the @/ hierarchy
└── usb/
    └── program.elf              # USB/UART-loaded demo program
```

`init64` stats every standard node at boot and logs which ones are present.

## The `nk) ` shell

`nksh` uses the same command engine as the kernel fallback shell, but all
callbacks are ABI v3 syscalls. Type `help` for the authoritative list; the
common commands are:

| Command | Meaning |
| --- | --- |
| `help` | print the command list |
| `info` | ABI v3, ticks, task count, sandbox window and flags |
| `pwd` | print the current namespace root (`@/`) |
| `ls [path]` | list a directory (default `@/`) |
| `cat <path>` | print a file |
| `echo <args>` | echo text |
| `exec <path>` | load and run an ELF image in a new task |
| `mem rd <hex> [len]` | read up to 256 bytes from the granted sandbox |
| `mem wr <hex> <hexbytes>` | write hex bytes into the granted sandbox |
| `usb lun <n>` | show the NKUSBPRG header on LUN `n` |
| `usb load <n>` | read an ELF payload from LUN `n` and exec it |
| `clear` | clear the screen |
| `exit` / `quit` | terminate the shell task |

The `mem` commands are confined to a 64 KiB sandbox obtained with
`nk64_mmap(0, 64*1024, NK64_MM_SANDBOX | NK64_MM_USER)`; every access is
bounds-checked by `nk_sandbox_*`. Entering `exit` calls `nk64_exit(0)`.

## USB-as-SATA protocol

`usb64` makes an internal SATA drive look like a USB mass-storage device to
the rest of the system:

```
BOT CBW ("USBC") -> SCSI READ_10 / WRITE_10 / READ_CAPACITY_10
                 -> nk_uas_bridge_t -> AHCI port read/write (backend)
BOT CSW ("USBS") <- status + residue
```

`shared/usb_as_sata.c` implements the bridge; `apps/usb64/main.c` supplies the
block backend. The demo uses two 4 KiB RAM-disk LUNs. LUN 0 sector 1 holds a
`NKUSBPRG` header (magic `NKUSBPRG`, version 1, `elf_lba = 2`) and LBA 2 holds
a minimal `ET_EXEC` ELF64 image suitable for `nk_elf64_probe()`. Every so often
the service builds a `READ_CAPACITY_10` CBW, runs it through `nk_uas_exec()` and
logs the LUN capacity with `nk64_log()`.

**Test-profile honesty:** there is no real xHCI or AHCI hardware driver in
this profile. Both are represented by the userspace RAM-disk/bridge backends;
only block read/write syscalls cross the ring boundary. On real hardware the
same bridge would be fed by an xHCI driver and its `read`/`write` callbacks
would target an AHCI port instead of RAM.

## UART program download

The emergency loader understands a small binary framing protocol over UART:

```
"NKELFRX"  (7 ASCII bytes)
uint64_t   payload length, little-endian
uint8_t[]  payload: a static ELF64 ET_EXEC image
```

The kernel bounds-checks the declared length against available RAM, validates
the image with `nk_elf64_check_range()` / `nk_elf64_probe()`, then creates a
new userspace task from it. This path is independent of `@/` and of Limine;
it is useful for bringing up a board without storage media.
