#include "gothicos/drivers.hpp"

#include <cstdint>

namespace gothicos {
namespace drivers {

int Driver::register_with(kal::Kernel& kernel)
{
    kernel_ = &kernel;
    return kernel_->register_driver(name(), &Driver::trampoline, this);
}

int Driver::trampoline(void*, const char*, const char* command,
                       const gothicos_value_t* argument, gothicos_value_t* result,
                       void* context)
{
    Driver* driver = static_cast<Driver*>(context);
    kal::Value output;
    int status = driver->io(command == nullptr ? "" : command,
                            kal::Value::from_raw(*argument), output);
    if (status == GOTHICOS_STATUS_OK && result != nullptr) {
        *result = output.raw();
    }
    return status;
}

const char* ConsoleDriver::name() const { return "console"; }

int ConsoleDriver::io(const std::string& command, const kal::Value& argument,
                      kal::Value& result)
{
    if (command == "write") {
        last_message_ = argument.as_string();
        writes_++;
        result = kal::Value::integer(static_cast<int>(last_message_.size()));
        return GOTHICOS_STATUS_OK;
    }
    if (command == "read") {
        result = kal::Value::text(last_message_);
        return GOTHICOS_STATUS_OK;
    }
    if (command == "info") {
        result = kal::Value::integer(static_cast<int>(writes_));
        return GOTHICOS_STATUS_OK;
    }
    return GOTHICOS_STATUS_UNSUPPORTED;
}

const char* TimerDriver::name() const { return "timer"; }

int TimerDriver::io(const std::string& command, const kal::Value&, kal::Value& result)
{
    if (command == "ticks") {
        result = kal::Value::integer(static_cast<int>(ticks_++));
        return GOTHICOS_STATUS_OK;
    }
    if (command == "now") {
        hal::Platform* platform = kernel_ == nullptr ? nullptr : kernel_->platform();
        std::uint64_t now = platform == nullptr ? 0 : platform->now_ns();
        result = kal::Value::integer(static_cast<int>(now & 0x7fffffffULL));
        return GOTHICOS_STATUS_OK;
    }
    return GOTHICOS_STATUS_UNSUPPORTED;
}

int DriverRegistry::install(kal::Kernel& kernel)
{
    int status = console_.register_with(kernel);
    if (status != GOTHICOS_STATUS_OK) {
        return status;
    }
    return timer_.register_with(kernel);
}

}  // namespace drivers
}  // namespace gothicos
