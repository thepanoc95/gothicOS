/* NullKernRT HAL (C++) -- platform abstraction shared by the KAL and driver layers. */
#ifndef GOTHICOS_HAL_HPP
#define GOTHICOS_HAL_HPP

#include <cstdint>

namespace gothicos {
namespace hal {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

struct CpuTopology {
    unsigned total;
    unsigned first_managed;
    unsigned managed;
};

/* Minimal platform contract. Linux-module implementations should avoid RTTI and
 * virtual calls in hot paths; the host implementation is used by tests and the SDK. */
class Platform {
public:
    virtual ~Platform() {}
    virtual const char* name() const = 0;
    virtual std::uint64_t now_ns() const = 0;
    virtual CpuTopology topology() const = 0;
    virtual void log(LogLevel level, const char* message) = 0;

    /* Never bind CPU 0: it belongs to Linux in the hosted configuration. */
    virtual int bind_cpu(unsigned cpu) = 0;
};

class HostPlatform final : public Platform {
public:
    HostPlatform(unsigned total = 1, unsigned first_managed = 0);
    const char* name() const override;
    std::uint64_t now_ns() const override;
    CpuTopology topology() const override;
    void log(LogLevel level, const char* message) override;
    int bind_cpu(unsigned cpu) override;

private:
    unsigned total_;
    unsigned first_managed_;
};

/* Deterministic platform for unit tests and firmware Bring-Up. */
class NullPlatform final : public Platform {
public:
    NullPlatform(unsigned total = 1, unsigned first_managed = 0);
    const char* name() const override;
    std::uint64_t now_ns() const override;
    CpuTopology topology() const override;
    void log(LogLevel level, const char* message) override;
    int bind_cpu(unsigned cpu) override;

private:
    unsigned total_;
    unsigned first_managed_;
};

}  // namespace hal
}  // namespace gothicos

#endif  // GOTHICOS_HAL_HPP
