#ifndef GOTHICOS_UMBRART_HPP
#define GOTHICOS_UMBRART_HPP

#include <stddef.h>
#include <stdint.h>

namespace umbra {

enum class Status : uint32_t {
    Ok = 0, InvalidPackage, UnsupportedVersion, AbiMismatch,
    BoundsError, InvalidOpcode, StackError, RuntimeError,
    CapabilityDenied, UnsupportedNative, OutOfMemory, Halted
};

enum class TaskState : uint8_t { Empty, Ready, Running, Blocked, Stopped, Failed };

struct Result {
    Status status;
    uint32_t detail;
};

struct Limits {
    uint16_t max_stack;
    uint8_t max_call_depth;
    uint8_t max_locals;
    uint16_t max_array_length;
    uint32_t instruction_budget;
};

struct Value {
    enum Type : uint8_t { Null, Boolean, Integer, Float, String, Array } type;
    union {
        bool boolean;
        int32_t integer;
        double real;
        struct { const char *data; uint16_t length; } string;
        struct { uint8_t id; } array;
    } as;
};

class Runtime {
public:
    typedef bool (*NativeDispatch)(void *context, uint8_t native_id,
                                   const Value *args, uint8_t argc,
                                   Value *result, bool *yielded);
    Runtime();
    void set_native_dispatch(NativeDispatch dispatch, void *context);
    Result load(const void *package, size_t length);
    Result reset();
    Result step(uint32_t budget = 0);
    Result run(uint32_t max_instructions);
    void stop();

    TaskState state() const { return state_; }
    Status last_status() const { return last_status_; }
    uint32_t instructions_executed() const { return instructions_; }
    const char *error_message() const;

private:
    struct Function {
        uint8_t locals, params;
        uint16_t offset, size;
        uint8_t flags;
    };
    struct Frame {
        uint16_t function;
        uint16_t ip;
        uint16_t base;
        Value locals[255];
    };
    struct Array {
        bool used;
        uint16_t length;
        Value items[256];
    };

    const uint8_t *image_;
    size_t image_size_;
    uint16_t entry_, function_count_, constant_count_;
    uint16_t constants_size_, code_size_, metadata_size_;
    uint32_t constants_offset_, code_offset_, metadata_offset_;
    Function functions_[64];
    Value constants_[256];
    uint16_t actual_constants_;
    Value stack_[64];
    uint16_t sp_;
    Frame frames_[16];
    uint8_t frame_count_;
    Array arrays_[8];
    TaskState state_;
    Status last_status_;
    uint32_t instructions_;
    uint16_t max_stack_;
    uint8_t max_call_depth_;
    uint16_t max_array_length_;
    uint32_t budget_;
    char error_[96];
    bool capabilities_[10];
    NativeDispatch native_dispatch_;
    void *native_context_;

    bool verify();
    bool decode_constants();
    bool read_u16(size_t at, uint16_t &out) const;
    bool read_u32(size_t at, uint32_t &out) const;
    bool has_capability(const char *name) const;
    bool require_capability(uint8_t native_id);
    bool push(const Value &v);
    bool pop(Value &v);
    bool execute_one();
    bool call(uint8_t function_index);
    void do_return(const Value &v);
    bool binary(uint8_t opcode, const Value &a, const Value &b, Value &out);
    bool allocate_array(uint16_t length, Value &out);
    void fail(Status status, const char *message);
    void log_value(const Value &v);
    bool native_call(uint8_t id, const Value *args, uint8_t argc, Value &out,
                     bool &yielded);
};

} // namespace umbra
#endif
