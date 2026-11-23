#include "os_modules.hpp"

namespace umbra { namespace os {
namespace {
static const Export system_exports[] = {
    {"log", 1, 1, "system.logging"},
    {"yield", 2, 0, "system.yield"},
    {"info", 3, 0, "system.info"},
    {"sleep", SystemSleep, 1, "system.sleep"},
    {"uptime", SystemUptime, 0, "system.info"},
    {"exit", SystemExit, 1, "task.stop"}
};
static const Export memory_exports[] = {
    {"alloc", 6, 2, "memory.alloc"},
    {"free", 7, 2, "memory.free"},
    {"stats", MemoryStats, 0, "memory.info"}
};
static const Export io_exports[] = {
    {"read", IoRead, 2, "io.read"},
    {"write", IoWrite, 2, "io.write"}
};
static const Export fs_exports[] = {
    {"open", FsOpen, 2, "filesystem.open"},
    {"read", FsRead, 2, "filesystem.read"},
    {"write", FsWrite, 2, "filesystem.write"},
    {"close", FsClose, 1, "filesystem.close"},
    {"stat", FsStat, 1, "filesystem.stat"}
};
static const Export network_exports[] = {
    {"status", NetworkStatus, 0, "network.status"},
    {"connect", NetworkConnect, 1, "network.connect"},
    {"send", NetworkSend, 2, "network.send"},
    {"receive", NetworkReceive, 2, "network.receive"},
    {"close", NetworkClose, 1, "network.close"}
};
static const Export async_exports[] = {
    {"yield", 2, 0, "system.yield"},
    {"sleep", SystemSleep, 1, "system.sleep"},
    {"cancel", AsyncCancel, 1, "task.cancel"}
};
static const Export device_exports[] = {
    {"query", DeviceQuery, 1, "device.query"},
    {"invoke", DeviceInvoke, 3, "device.command"}
};
static const Export math_exports[] = {
    {"abs", MathAbs, 1, nullptr},
    {"min", MathMin, 2, nullptr},
    {"max", MathMax, 2, nullptr}
};
static const Export logging_exports[] = {
    {"debug", LogDebug, 1, "system.logging"},
    {"info", LogInfo, 1, "system.logging"},
    {"warn", LogWarn, 1, "system.logging"},
    {"error", LogError, 1, "system.logging"}
};
static const Module module_table[] = {
    {"@gothic/core", nullptr, 0},
    {"@gothic/system", system_exports, (uint8_t)(sizeof(system_exports)/sizeof(system_exports[0]))},
    {"@gothic/memory", memory_exports, (uint8_t)(sizeof(memory_exports)/sizeof(memory_exports[0]))},
    {"@gothic/io", io_exports, (uint8_t)(sizeof(io_exports)/sizeof(io_exports[0]))},
    {"@gothic/fs", fs_exports, (uint8_t)(sizeof(fs_exports)/sizeof(fs_exports[0]))},
    {"@gothic/network", network_exports, (uint8_t)(sizeof(network_exports)/sizeof(network_exports[0]))},
    {"@gothic/async", async_exports, (uint8_t)(sizeof(async_exports)/sizeof(async_exports[0]))},
    {"@gothic/device", device_exports, (uint8_t)(sizeof(device_exports)/sizeof(device_exports[0]))},
    {"@gothic/logging", logging_exports, (uint8_t)(sizeof(logging_exports)/sizeof(logging_exports[0]))},
    {"@gothic/math", math_exports, (uint8_t)(sizeof(math_exports)/sizeof(math_exports[0]))}
};
static bool eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}
}

const Module *modules(size_t &count) {
    count = sizeof(module_table)/sizeof(module_table[0]);
    return module_table;
}
const Module *find_module(const char *name) {
    size_t n=0; const Module *m=modules(n);
    for(size_t i=0;i<n;i++) if(eq(name,m[i].specifier)) return &m[i];
    return nullptr;
}
const Export *find_export(const Module &m, const char *name) {
    for(uint8_t i=0;i<m.export_count;i++) if(eq(name,m.exports[i].name)) return &m.exports[i];
    return nullptr;
}
const Export *find_native(uint8_t id) {
    size_t n=0; const Module *m=modules(n);
    for(size_t i=0;i<n;i++) for(uint8_t j=0;j<m[i].export_count;j++)
        if(m[i].exports[j].native_id==id) return &m[i].exports[j];
    return nullptr;
}

}} // namespace umbra::os
