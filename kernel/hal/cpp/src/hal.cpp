#include "gothicos/hal.hpp"

#include <ctime>

namespace gothicos {
namespace hal {

HostPlatform::HostPlatform(unsigned total, unsigned first_managed)
    : total_(total == 0 ? 1 : total), first_managed_(first_managed) {}

const char* HostPlatform::name() const { return "host"; }

std::uint64_t HostPlatform::now_ns() const
{
#if defined(__unix__) || defined(__APPLE__)
    timespec time_point = {};
    clock_gettime(CLOCK_MONOTONIC, &time_point);
    return static_cast<std::uint64_t>(time_point.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(time_point.tv_nsec);
#else
    return static_cast<std::uint64_t>(std::time(nullptr)) * 1000000000ULL;
#endif
}

CpuTopology HostPlatform::topology() const
{
    CpuTopology result = {total_, first_managed_, 0};
    if (first_managed_ < total_) {
        result.managed = total_ - first_managed_;
    }
    return result;
}

void HostPlatform::log(LogLevel, const char*) {}

int HostPlatform::bind_cpu(unsigned cpu)
{
    /* Host platforms have no RT scheduling in the reference implementation; binding is
     * reported as unsupported rather than silently claiming a core was taken. */
    return cpu == 0 || cpu < first_managed_ || cpu >= total_ ? -1 : 0;
}

NullPlatform::NullPlatform(unsigned total, unsigned first_managed)
    : total_(total == 0 ? 1 : total), first_managed_(first_managed) {}

const char* NullPlatform::name() const { return "null"; }

std::uint64_t NullPlatform::now_ns() const { return 0; }

CpuTopology NullPlatform::topology() const
{
    CpuTopology result = {total_, first_managed_, 0};
    if (first_managed_ < total_) {
        result.managed = total_ - first_managed_;
    }
    return result;
}

void NullPlatform::log(LogLevel, const char*) {}

int NullPlatform::bind_cpu(unsigned cpu)
{
    return cpu == 0 || cpu >= total_ ? -1 : 0;
}

}  // namespace hal
}  // namespace gothicos
