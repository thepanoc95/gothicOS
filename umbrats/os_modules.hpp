#ifndef GOTHICOS_UMBRA_OS_MODULES_HPP
#define GOTHICOS_UMBRA_OS_MODULES_HPP

#include <stddef.h>
#include <stdint.h>

namespace umbra { namespace os {

// Stable native IDs 1..9 are reserved by Umbra Bytecode 0.1.
// These IDs are compiler/linker ABI symbols, not kernel syscall numbers.
enum NativeId : uint8_t {
    SystemSleep = 10, SystemUptime = 11, SystemExit = 12,
    MemoryStats = 13,
    IoRead = 14, IoWrite = 15,
    FsOpen = 16, FsRead = 17, FsWrite = 18, FsClose = 19, FsStat = 20,
    NetworkStatus = 21, NetworkConnect = 22, NetworkSend = 23,
    NetworkReceive = 24, NetworkClose = 25,
    AsyncCancel = 26,
    DeviceQuery = 27, DeviceInvoke = 28,
    LogDebug = 29, LogInfo = 30, LogWarn = 31, LogError = 32,
    MathAbs = 33, MathMin = 34, MathMax = 35
};

struct Export {
    const char *name;
    uint8_t native_id;
    uint8_t argument_count;
    const char *capability;
};

struct Module {
    const char *specifier;
    const Export *exports;
    uint8_t export_count;
};

// Returns the compile-time module manifest. Import resolution is performed by
// the UmbraTS compiler/linker; this table is also usable by SDK tooling.
const Module *modules(size_t &count);
const Module *find_module(const char *specifier);
const Export *find_export(const Module &module, const char *export_name);
const Export *find_native(uint8_t native_id);

}} // namespace umbra::os
#endif
