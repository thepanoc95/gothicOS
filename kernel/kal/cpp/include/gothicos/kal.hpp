/* NullKernRT KAL (C++) -- typed interface over the native C core. */
#ifndef GOTHICOS_KAL_HPP
#define GOTHICOS_KAL_HPP

#include <initializer_list>
#include <string>
#include <vector>

#include "gothicos/hal.hpp"
#include "gothicos/kernel_abi.h"
#include "gothicos/nk_core.h"

namespace gothicos {
namespace kal {

class Value {
public:
    Value();
    static Value integer(int value);
    static Value text(const char* value);
    static Value text(const std::string& value);
    static Value from_raw(const gothicos_value_t& value);

    gothicos_value_t raw() const;
    int as_int() const;
    std::string as_string() const;
    bool is_string() const;

private:
    gothicos_value_kind_t kind_;
    int integer_;
    std::string text_;
};

struct Result {
    int status;
    Value value;
};

class Kernel;

class Task {
public:
    Task(Kernel& kernel, int id);
    int id() const;
    int state() const;
    int stop();
    int resume();
    std::string name() const;

private:
    Kernel& kernel_;
    int id_;
};

class Port {
public:
    Port(Kernel& kernel, const char* name);
    int open(int capacity = NK_PORT_DEPTH);
    int close();
    int send(const Value& message, int sender = -1);
    Result receive(int task_id = -1);
    int info() const;

private:
    Kernel& kernel_;
    std::string name_;
};

class Kernel {
public:
    explicit Kernel(unsigned core_count = 1, unsigned first_managed_cpu = 0,
                    hal::Platform* platform = nullptr);
    ~Kernel();

    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    nk_kernel_t* native() const;
    hal::Platform* platform() const;

    int boot();
    int run(int max_steps = 8);
    int dispatch_one();
    Result invoke(int opcode, std::initializer_list<Value> arguments = {});
    Result invoke(int opcode, const std::vector<Value>& arguments);
    int info(nk_info_t* info) const;

    Task spawn(const char* name, nk_step_fn step, void* context = nullptr);
    Port port(const char* name);
    int register_driver(const char* name, nk_driver_io_fn io, void* context);
    int register_ability(const char* name, const char* subsystem);

private:
    std::vector<unsigned char> storage_;
    nk_kernel_t* kernel_;
    hal::Platform* platform_;
    bool owns_platform_;
};

}  // namespace kal
}  // namespace gothicos

#endif  // GOTHICOS_KAL_HPP
