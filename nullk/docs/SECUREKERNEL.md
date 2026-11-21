# SecureKernel

SecureKernel is a C++ **userspace** integrity layer for `null_kernel`. It is not
part of the ring-0 core. The tiny kernel still owns only CPU, memory,
interrupts, scheduling and the ABI; SecureKernel is linked into the userspace
image and consulted by exec callers before the kernel creates a task.

Source tree: `nullk/securekernel/`.

## Policy

| Image state | Result |
| --- | --- |
| Valid `NKSIGv1` signature, no suspicious symbols | `ALLOW` |
| Valid signature but suspicious symbol/string | `DENY` |
| Invalid signature / hash mismatch | `DENY` |
| Unsigned, clean scan | `ALLOW` with warning |
| Unsigned, suspicious scan | `DENY` |

Build with `-DNK_SECUREKERNEL_REQUIRE_SIGNED=1` to make every unsigned image a
`DENY`. The default is permissive for unsigned but clean images so the existing
initrd keeps working.

## What is scanned

SecureKernel parses ELF64 section headers directly (no libelf, no libc):

- `SHT_SYMTAB` / `SHT_DYNSYM` entries are checked. `STT_FUNC` symbols are
  counted and every symbol name is searched for suspicious tokens.
- `SHT_STRTAB` sections are searched for the same tokens, catching stripped
  images that still retain string data.
- `SHF_EXECINSTR` sections are searched for embedded token bytes.

Default tokens: `HACK`, `BREAK`, `SYSCALL`, `INJECT`, `EXPLOIT`, `ROOTKIT`,
`PAYLOAD`, `BYPASS`, `SPOOF`, `PATCH`, `TRAMPOLINE`.

Matching is case-sensitive and word-substring based, so ordinary lowercase
symbols such as `nk64_syscall` are not false positives.

## Signature format

Signatures are injected as an extra ELF section with `sh_name = 0` and
`sh_type = SHT_PROGBITS`. The signature bytes are excluded from the digest, so
verification hashes the file with that single section skipped.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `NKSIGv1\0` |
| 8 | 4 | version (1) |
| 12 | 4 | flags (reserved) |
| 16 | 8 | canonical image size |
| 24 | 32 | SHA-256 of canonical image |
| 56 | 32 | HMAC-SHA256 of canonical image |

The default key is the development string in
`nullk/securekernel/include/nullk/securekernel.h`. **Replace it for production**
and keep the signer in sync. The signer accepts `NK_SECUREKERNEL_KEY` as either
64 hex characters or an ASCII string.

## Signer

```sh
python3 nullk/securekernel/tools/sign_elf.py sign input.elf signed.elf
python3 nullk/securekernel/tools/sign_elf.py verify signed.elf
```

The signer moves the ELF section header table to the end, appends one section
header, then appends the 88-byte signature payload. Tampering with any byte
outside the signature section breaks both SHA-256 and HMAC-SHA256.

## C ABI

```c
#include "nullk/securekernel.h"

nk_secure_report_t report;
if (nk_securekernel_verify(image, size, &report) == 0 &&
    report.decision == NK_SECURE_DECISION_ALLOW) {
    /* safe to exec */
}
```

`nk_securekernel_sign()` is also exposed for host tools and tests. The
userspace shell (`apps/sh64/main.c`) stages the whole file into a bounded
buffer, verifies it, and only then calls `nk64_exec()`.

## Build and tests

```sh
make -C nullk BOARD=amd64 test
```

This runs the existing AMD64 host suite plus `test_securekernel`, which covers
SHA-256, symbol scanning, signing, tamper detection, and malformed input.

## Relationship to the kernel unification

SecureKernel is deliberately portable C++ with no platform headers in its
engine. When the AMD64 and ARM ports are collapsed onto one shared portable
kernel core, this layer remains a userspace service and needs only its small C
ABI re-linked against the unified userspace runtime.
