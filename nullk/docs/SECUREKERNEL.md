# SecureKernel

SecureKernel is a C++ **userspace** security layer for `null_kernel`. It is not
part of the ring-0 core: the tiny kernel still owns only CPU, memory,
interrupts, scheduling and the ABI. SecureKernel is linked into the userspace
image and performs two jobs before a program is allowed to run:

1. **Type enforcement** — an SELinux-style policy decides whether a labelled
   subject may perform an operation on a labelled object (a file path).
2. **Integrity** — the ELF is scanned for suspicious symbols/strings and an
   optional `NKSIGv1` signature is verified.

Source tree: `nullk/securekernel/`.

## Architecture

All ring 3 services live in the same `init.elf` image, so the policy and audit
ring are shared state in that address space:

- `init64` creates the `SecureKernel` task; the optional InterfaceKit server and
  demo are started later by `nksh` through `ui start`.
- The service installs the built-in policy, **locks it**, and logs policy
  decisions drained from the audit ring.
- The `nksh` shell calls `nk_securekernel_enforce()` on every `exec`, passing
  subject `shell_t` and the target path.
- On an ALLOW the shell calls `nk64_exec_mem()` with the exact bytes it
  verified. The kernel creates the task from that buffer, so there is no
  check/use window between verification and loading.

Because policy is locked after boot, later tasks cannot weaken it. The kernel
has no policy code; it only provides the bounded `EXEC_MEM` syscall.

## Type-enforcement policy

**Subjects** are labels presented by the caller (for example `shell_t`,
`init_t`, `service_t`, `untrusted_t`). **Objects** are file contexts derived
from the longest matching path prefix:

| Path prefix | Object context |
| --- | --- |
| `@/data/system/` | `system_t` |
| `@/data/user/` | `user_t` |
| `@/usb/` | `usb_t` |
| `@/data/` | `config_t` |
| anything else | `unknown_t` |

**Operations** are `EXEC`, `READ`, `WRITE` and `LOAD` (USB/UART payloads). A
rule grants or denies a set of operations for a subject/object pair. An
explicit DENY wins; if no rule applies the decision is **DENY** (fail-closed).

The built-in policy grants:

| Subject | Object | Allowed operations |
| --- | --- | --- |
| `init_t` | `system_t`, `config_t` | READ |
| `shell_t` | `system_t`, `config_t` | READ |
| `shell_t` | `user_t` | READ, EXEC |
| `shell_t` | `usb_t` | READ, LOAD |
| `service_t` | `system_t` | READ |
| `service_t` | `usb_t` | READ, LOAD |
| `untrusted_t` | `system_t` | READ |
| `untrusted_t` | `user_t` | READ, EXEC |
| `untrusted_t` | `usb_t` | READ, LOAD |

Notably the shell can read the kernel image but can never execute it. An
unknown subject or an unlabelled object is denied by default.

Applications can install a different policy with `nk_securekernel_policy_set()`
before tasks start, or inspect the default with
`nk_securekernel_policy_builtin()`. Policies are validated (label, operation
and decision bounds) and rejected if malformed.

## Integrity policy

Type enforcement is combined with the existing image checks:

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

### What is scanned

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

### Signer

```sh
python3 nullk/securekernel/tools/sign_elf.py sign input.elf signed.elf
python3 nullk/securekernel/tools/sign_elf.py verify signed.elf
```

The signer moves the ELF section header table to the end, appends one section
header, then appends the 88-byte signature payload. Tampering with any byte
outside the signature section breaks both SHA-256 and HMAC-SHA256.

## Audit

Every policy decision (including decisions made by `enforce`) is recorded in a
16-entry bounded ring with a sequence number, subject, object, operation,
decision and reason. The service drains it every 50 ticks and writes each
entry to the kernel log:

```
[securekernel] DENY shell_t/system_t op=exec: blocked by policy: shell_t/system_t op=exec
```

Host callers can use `nk_securekernel_audit_count()`,
`nk_securekernel_audit_last()`, `nk_securekernel_audit_drain()` and
`nk_securekernel_audit_format()`.

## C ABI

```c
#include "nullk/securekernel.h"

nk_secure_report_t report;
if (nk_securekernel_enforce(NK_SECURE_SUBJECT_SHELL, path,
                            image, image_size, &report) == 0 &&
    report.decision == NK_SECURE_DECISION_ALLOW) {
    nk64_exec_mem(image, image_size);   /* exec exactly what was checked */
}
```

`nk_securekernel_verify()` remains available for integrity-only checks, and
`nk_securekernel_sign()` is exposed for host tools and tests.

## Enforcement boundaries

- The userspace `nksh` shell is fully gated: type policy, integrity scan and
  verified-buffer exec.
- The ring-0 fallback shell and the UART/`NKUSBPRG` loaders are recovery paths
  and are deliberately outside the userspace policy; they cannot be reached
  from a normal booted session.
- Subjects are supplied by the caller. Tasks share one address space in this
  profile, so SecureKernel is a cooperative mandatory layer for ring 3, not a
  hardware-enforced hypervisor boundary.

## Build and tests

```sh
make -C nullk BOARD=amd64 test
```

This runs the AMD64 host suite plus `test_securekernel`, which covers SHA-256,
symbol scanning, signing, tamper detection, malformed input, built-in and
custom policies, path labelling, policy locking, combined enforcement and the
audit ring.
