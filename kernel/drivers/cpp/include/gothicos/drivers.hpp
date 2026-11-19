/* NullKernRT driver layer (C++). Drivers are ABI clients of the native core. */
#ifndef GOTHICOS_DRIVERS_HPP
#define GOTHICOS_DRIVERS_HPP

#include <string>

#include "gothicos/kal.hpp"

namespace gothicos {
namespace drivers {

class Driver {
public:
    virtual ~Driver() {}
    virtual const char* name() const = 0;
    virtual int io(const std::string& command, const kal::Value& argument,
                   kal::Value& result) = 0;

    int register_with(kal::Kernel& kernel);

protected:
    kal::Kernel* kernel_ = nullptr;

private:
    static int trampoline(void* kernel, const char* name, const char* command,
                          const gothicos_value_t* argument, gothicos_value_t* result,
                          void* context);
};

class ConsoleDriver final : public Driver {
public:
    const char* name() const override;
    int io(const std::string& command, const kal::Value& argument,
           kal::Value& result) override;

private:
    std::string last_message_;
    unsigned writes_ = 0;
};

class TimerDriver final : public Driver {
public:
    const char* name() const override;
    int io(const std::string& command, const kal::Value& argument,
           kal::Value& result) override;

private:
    unsigned ticks_ = 0;
};

class DriverRegistry {
public:
    int install(kal::Kernel& kernel);

private:
    ConsoleDriver console_;
    TimerDriver timer_;
};

}  // namespace drivers
}  // namespace gothicos

#endif  // GOTHICOS_DRIVERS_HPP
