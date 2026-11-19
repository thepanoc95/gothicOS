#include "gothicos/kal.hpp"

#include <utility>

namespace gothicos {
namespace kal {

Value::Value() : kind_(GOTHICOS_VALUE_NONE), integer_(0) {}

Value Value::integer(int value)
{
    Value result;
    result.kind_ = GOTHICOS_VALUE_INT;
    result.integer_ = value;
    return result;
}

Value Value::text(const char* value)
{
    Value result;
    result.kind_ = GOTHICOS_VALUE_STRING;
    result.text_ = value == nullptr ? "" : value;
    return result;
}

Value Value::text(const std::string& value)
{
    Value result;
    result.kind_ = GOTHICOS_VALUE_STRING;
    result.text_ = value;
    return result;
}

Value Value::from_raw(const gothicos_value_t& value)
{
    Value result;
    result.kind_ = value.kind;
    result.integer_ = value.integer;
    if (value.kind == GOTHICOS_VALUE_STRING && value.string != nullptr) {
        result.text_ = value.string;
    }
    return result;
}

gothicos_value_t Value::raw() const
{
    gothicos_value_t value;
    value.kind = kind_;
    value.integer = integer_;
    value.string = kind_ == GOTHICOS_VALUE_STRING ? text_.c_str() : nullptr;
    return value;
}

int Value::as_int() const { return integer_; }

std::string Value::as_string() const { return text_; }

bool Value::is_string() const { return kind_ == GOTHICOS_VALUE_STRING; }

Kernel::Kernel(unsigned core_count, unsigned first_managed_cpu, hal::Platform* platform)
    : storage_(nk_kernel_storage_size()), kernel_(nullptr), platform_(platform),
      owns_platform_(false)
{
    if (platform_ == nullptr) {
        platform_ = new hal::HostPlatform(core_count > 0 ? core_count : 1, first_managed_cpu);
        owns_platform_ = true;
    }
    kernel_ = nk_kernel_init(storage_.data(), storage_.size(), core_count, first_managed_cpu);
}

Kernel::~Kernel()
{
    if (owns_platform_) {
        delete platform_;
    }
}

nk_kernel_t* Kernel::native() const { return kernel_; }

hal::Platform* Kernel::platform() const { return platform_; }

int Kernel::boot()
{
    int status = nk_boot(kernel_);
    if (status == GOTHICOS_STATUS_OK) {
        platform_->log(hal::LogLevel::Info, "nullkern boot complete");
    }
    return status;
}

int Kernel::run(int max_steps) { return nk_run(kernel_, max_steps); }

int Kernel::dispatch_one() { return nk_dispatch_one(kernel_); }

Result Kernel::invoke(int opcode, const std::vector<Value>& arguments)
{
    std::vector<gothicos_value_t> raw;
    raw.reserve(arguments.size());
    for (const Value& argument : arguments) {
        raw.push_back(argument.raw());
    }
    gothicos_arguments_t payload = {raw.data(), static_cast<unsigned int>(raw.size())};
    gothicos_result_t response;
    int status = nk_invoke(kernel_, opcode, raw.empty() ? nullptr : &payload, &response);
    return Result{status, response.value.kind == GOTHICOS_VALUE_STRING
                              ? Value::text(response.value.string == nullptr
                                                ? "" : response.value.string)
                              : (response.value.kind == GOTHICOS_VALUE_INT
                                     ? Value::integer(response.value.integer)
                                     : Value())};
}

Result Kernel::invoke(int opcode, std::initializer_list<Value> arguments)
{
    return invoke(opcode, std::vector<Value>(arguments));
}

int Kernel::info(nk_info_t* info) const { return nk_info(kernel_, info); }

Task Kernel::spawn(const char* name, nk_step_fn step, void* context)
{
    return Task(*this, nk_task_spawn(kernel_, name, step, context));
}

Port Kernel::port(const char* name) { return Port(*this, name); }

int Kernel::register_driver(const char* name, nk_driver_io_fn io, void* context)
{
    return nk_driver_register(kernel_, name, io, context);
}

int Kernel::register_ability(const char* name, const char* subsystem)
{
    return nk_ability_register(kernel_, name, subsystem, nullptr, nullptr);
}

Task::Task(Kernel& kernel, int id) : kernel_(kernel), id_(id) {}

int Task::id() const { return id_; }

int Task::state() const
{
    Result result = kernel_.invoke(GOTHICOS_OP_TASK_STATE, {Value::integer(id_)});
    return result.status == GOTHICOS_STATUS_OK ? result.value.as_int() : -1;
}

int Task::stop()
{
    return kernel_.invoke(GOTHICOS_OP_TASK_STOP, {Value::integer(id_)}).status;
}

int Task::resume()
{
    return kernel_.invoke(GOTHICOS_OP_TASK_RESUME, {Value::integer(id_)}).status;
}

std::string Task::name() const
{
    Result result = kernel_.invoke(GOTHICOS_OP_TASK_INFO, {Value::integer(id_)});
    return result.status == GOTHICOS_STATUS_OK ? result.value.as_string() : std::string();
}

Port::Port(Kernel& kernel, const char* name) : kernel_(kernel), name_(name) {}

int Port::open(int capacity)
{
    return kernel_.invoke(GOTHICOS_OP_PORT_OPEN,
                          {Value::text(name_), Value::integer(capacity)}).status;
}

int Port::close()
{
    return kernel_.invoke(GOTHICOS_OP_PORT_CLOSE, {Value::text(name_)}).status;
}

int Port::send(const Value& message, int sender)
{
    std::vector<Value> arguments;
    arguments.push_back(Value::text(name_));
    arguments.push_back(message);
    if (sender >= 0) {
        arguments.push_back(Value::integer(sender));
    }
    return kernel_.invoke(GOTHICOS_OP_PORT_SEND, arguments).status;
}

Result Port::receive(int task_id)
{
    std::vector<Value> arguments;
    arguments.push_back(Value::text(name_));
    if (task_id >= 0) {
        arguments.push_back(Value::integer(task_id));
    }
    return kernel_.invoke(GOTHICOS_OP_PORT_RECEIVE, arguments);
}

int Port::info() const
{
    return kernel_.invoke(GOTHICOS_OP_PORT_INFO, {Value::text(name_)}).value.as_int();
}

}  // namespace kal
}  // namespace gothicos
