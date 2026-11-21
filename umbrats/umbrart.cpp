#include "umbrart.hpp"

#if defined(UMBRA_NULLK_AMD64)
#include "nullk/abi64.h"
#else
#include "nullk/libnk.h"
#endif

namespace umbra {

static const uint8_t MAGIC[4] = {'U','M','B','1'};
static const uint8_t OP_NOP=0, OP_PUSH_CONST=1, OP_PUSH_TRUE=2,
 OP_PUSH_FALSE=3, OP_PUSH_NULL=4, OP_POP=5, OP_DUP=6,
 OP_LOAD_LOCAL=7, OP_STORE_LOCAL=8, OP_ADD=9, OP_SUB=10,
 OP_MUL=11, OP_DIV=12, OP_MOD=13, OP_NEG=14, OP_NOT=15,
 OP_EQ=16, OP_NE=17, OP_LT=18, OP_LE=19, OP_GT=20, OP_GE=21,
 OP_JMP=22, OP_JZ=23, OP_JNZ=24, OP_CALL=25, OP_CALL_NATIVE=26,
 OP_RET=27, OP_RET_VOID=28, OP_HALT=29, OP_ARRAY_NEW=30,
 OP_ARRAY_GET=31, OP_ARRAY_SET=32, OP_YIELD=33;

static const uint8_t N_LOG=1, N_YIELD=2, N_INFO=3, N_PORT_SEND=4,
 N_PORT_RECEIVE=5, N_MEM_ALLOC=6, N_MEM_FREE=7, N_IRQ_TIMER=8,
 N_TASK_STOP=9;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4) << 32);
}
static bool same(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}
static bool contains(const char *hay, size_t n, const char *needle) {
    size_t m=0; while (needle[m]) ++m;
    if (!m || m>n) return false;
    for (size_t i=0; i+m<=n; ++i) {
        size_t j=0; while (j<m && hay[i+j]==needle[j]) ++j;
        if (j==m) return true;
    }
    return false;
}
static Value null_value() { Value v{}; v.type=Value::Null; return v; }
static Value int_value(int32_t x) { Value v{}; v.type=Value::Integer; v.as.integer=x; return v; }
static Value bool_value(bool x) { Value v{}; v.type=Value::Boolean; v.as.boolean=x; return v; }

Runtime::Runtime()
 : image_(nullptr), image_size_(0), entry_(0), function_count_(0),
   constant_count_(0), constants_size_(0), code_size_(0), metadata_size_(0),
   constants_offset_(0), code_offset_(0), metadata_offset_(0),
   actual_constants_(0), sp_(0), frame_count_(0), state_(TaskState::Empty),
   last_status_(Status::Ok), instructions_(0), max_stack_(64),
   max_call_depth_(16), max_array_length_(256), budget_(128) {
    error_[0]=0;
    for (unsigned i=0;i<10;i++) capabilities_[i]=false;
    for (unsigned i=0;i<8;i++) arrays_[i].used=false;
}

void Runtime::fail(Status s, const char *m) {
    last_status_=s; state_=TaskState::Failed;
    size_t i=0; if (m) while (m[i] && i<sizeof(error_)-1) { error_[i]=m[i]; ++i; }
    error_[i]=0;
}
const char *Runtime::error_message() const { return error_; }

bool Runtime::read_u16(size_t at, uint16_t &out) const {
    if (!image_ || at+2>image_size_) return false;
    out=le16(image_+at); return true;
}
bool Runtime::read_u32(size_t at, uint32_t &out) const {
    if (!image_ || at+4>image_size_) return false;
    out=le32(image_+at); return true;
}

bool Runtime::has_capability(const char *name) const {
    if (!image_ || !name) return false;
    size_t n=0; while(name[n]) ++n;
    return contains((const char *)(image_+metadata_offset_), metadata_size_, name);
}

bool Runtime::verify() {
    if (!image_ || image_size_<24) { fail(Status::InvalidPackage,"package shorter than header"); return false; }
    for (unsigned i=0;i<4;i++) if(image_[i]!=MAGIC[i]) { fail(Status::InvalidPackage,"bad UMB1 magic"); return false; }
    if (image_[4]!=0 || image_[5]!=1) { fail(Status::UnsupportedVersion,"unsupported Umbra bytecode version"); return false; }
    if (image_[6]!=1 || image_[7]!=1) { fail(Status::AbiMismatch,"bytecode requires ABI 1.1"); return false; }
    uint16_t flags=le16(image_+8);
    entry_=le16(image_+10); function_count_=le16(image_+12);
    constant_count_=le16(image_+14); constants_size_=le16(image_+16);
    code_size_=le16(image_+18); metadata_size_=le16(image_+20);
    uint16_t reserved=le16(image_+22);
    if(flags || reserved || !function_count_ || function_count_>64 ||
       constant_count_>256 || code_size_<2 || metadata_size_<2 ||
       entry_>=function_count_) {
        fail(Status::InvalidPackage,"invalid header fields or runtime limits"); return false;
    }
    size_t fnbytes=(size_t)function_count_*8u;
    constants_offset_=24u+(uint32_t)fnbytes;
    code_offset_=constants_offset_+constants_size_;
    metadata_offset_=code_offset_+code_size_;
    if(metadata_offset_+metadata_size_!=image_size_) {
        fail(Status::BoundsError,"section lengths do not match package size"); return false;
    }
    for(uint16_t i=0;i<function_count_;i++) {
        const uint8_t *p=image_+24u+(size_t)i*8u;
        Function &f=functions_[i];
        f.locals=p[0]; f.params=p[1]; f.offset=le16(p+2);
        f.size=le16(p+4); f.flags=p[6];
        if(p[7] || f.params>f.locals || f.size<2 || (f.size&1) ||
           (uint32_t)f.offset+f.size>code_size_ || f.flags>1) {
            fail(Status::InvalidPackage,"invalid function record"); return false;
        }
    }
    if(!(functions_[entry_].flags&1)) { fail(Status::InvalidPackage,"entry function not marked as entry"); return false; }
    if(!decode_constants()) return false;

    // Structural opcode/operand validation. The compiler remains responsible for
    // static typing; the runtime checks stack and bounds again during execution.
    for(uint16_t fi=0;fi<function_count_;fi++) {
        Function &f=functions_[fi];
        for(uint16_t pc=0;pc<f.size;pc+=2) {
            const uint8_t *p=image_+code_offset_+f.offset+pc;
            uint8_t op=p[0], arg=p[1];
            if(op>OP_YIELD) { fail(Status::InvalidOpcode,"unknown bytecode opcode"); return false; }
            if((op==OP_PUSH_CONST && arg>=actual_constants_) ||
               ((op==OP_LOAD_LOCAL || op==OP_STORE_LOCAL) && arg>=f.locals) ||
               (op==OP_CALL && arg>=function_count_) ||
               (op==OP_ARRAY_NEW && arg>max_array_length_)) {
                fail(Status::InvalidPackage,"bytecode operand out of range"); return false;
            }
            if(op==OP_CALL_NATIVE && (arg<1 || arg>9)) {
                fail(Status::InvalidPackage,"unknown native function"); return false;
            }
            if((op==OP_JMP || op==OP_JZ || op==OP_JNZ) &&
               ((uint16_t)arg*2u>=f.size)) {
                fail(Status::InvalidPackage,"branch target out of range"); return false;
            }
        }
    }
    return true;
}

bool Runtime::decode_constants() {
    size_t p=constants_offset_, end=code_offset_;
    actual_constants_=0;
    while(actual_constants_<constant_count_) {
        if(p>=end) { fail(Status::BoundsError,"truncated constant section"); return false; }
        uint8_t tag=image_[p++]; Value v{};
        if(tag==0) v.type=Value::Null;
        else if(tag==1) {
            if(p+1>end) { fail(Status::BoundsError,"truncated bool constant"); return false; }
            v.type=Value::Boolean; v.as.boolean=image_[p++]!=0;
        } else if(tag==2) {
            if(p+4>end) { fail(Status::BoundsError,"truncated integer constant"); return false; }
            v.type=Value::Integer; v.as.integer=(int32_t)le32(image_+p); p+=4;
        } else if(tag==3) {
            if(p+8>end) { fail(Status::BoundsError,"truncated float constant"); return false; }
            uint64_t bits=le64(image_+p); p+=8;
            v.type=Value::Float;
            double d; uint8_t *dst=(uint8_t *)&d;
            for(unsigned i=0;i<8;i++) dst[i]=(uint8_t)(bits>>(i*8));
            v.as.real=d;
        } else if(tag==4) {
            if(p+2>end) { fail(Status::BoundsError,"truncated string length"); return false; }
            uint16_t n=le16(image_+p); p+=2;
            if(p+n>end) { fail(Status::BoundsError,"truncated string data"); return false; }
            v.type=Value::String; v.as.string.data=(const char *)(image_+p);
            v.as.string.length=n; p+=n;
        } else { fail(Status::InvalidPackage,"unknown constant tag"); return false; }
        constants_[actual_constants_++]=v;
    }
    if(p!=end) { fail(Status::InvalidPackage,"trailing constant bytes"); return false; }
    return true;
}

Result Runtime::load(const void *pkg, size_t len) {
    image_=(const uint8_t *)pkg; image_size_=len;
    state_=TaskState::Empty; last_status_=Status::Ok; error_[0]=0;
    instructions_=0; sp_=0; frame_count_=0;
    for(unsigned i=0;i<8;i++) arrays_[i].used=false;
    if(!verify()) return {last_status_,0};
    // Metadata capabilities are declarations, not grants. Kernel-side task
    // policy must grant the corresponding rights independently.
    state_=TaskState::Ready;
    return reset();
}

Result Runtime::reset() {
    if(state_==TaskState::Empty || !image_) return {Status::InvalidPackage,0};
    sp_=0; frame_count_=0; instructions_=0; error_[0]=0;
    for(unsigned i=0;i<8;i++) arrays_[i].used=false;
    Frame &f=frames_[frame_count_++]; f.function=entry_; f.ip=0; f.base=0;
    for(unsigned i=0;i<functions_[entry_].locals;i++) f.locals[i]=null_value();
    state_=TaskState::Ready; last_status_=Status::Ok;
    return {Status::Ok,0};
}

bool Runtime::push(const Value &v) {
    if(sp_>=max_stack_) { fail(Status::StackError,"operand stack overflow"); return false; }
    stack_[sp_++]=v; return true;
}
bool Runtime::pop(Value &v) {
    if(!sp_) { fail(Status::StackError,"operand stack underflow"); return false; }
    v=stack_[--sp_]; return true;
}
bool Runtime::allocate_array(uint16_t n, Value &out) {
    if(n>max_array_length_) { fail(Status::OutOfMemory,"array exceeds runtime limit"); return false; }
    for(uint8_t i=0;i<8;i++) if(!arrays_[i].used) {
        arrays_[i].used=true; arrays_[i].length=n;
        for(uint16_t j=0;j<n;j++) arrays_[i].items[j]=null_value();
        out.type=Value::Array; out.as.array.id=i; return true;
    }
    fail(Status::OutOfMemory,"array pool exhausted"); return false;
}

bool Runtime::binary(uint8_t op,const Value &a,const Value &b,Value &o) {
    if(op==OP_EQ || op==OP_NE) {
        bool eq=false;
        if(a.type==b.type) {
            if(a.type==Value::Null) eq=true;
            else if(a.type==Value::Boolean) eq=a.as.boolean==b.as.boolean;
            else if(a.type==Value::Integer) eq=a.as.integer==b.as.integer;
            else if(a.type==Value::Float) eq=a.as.real==b.as.real;
            else if(a.type==Value::String) eq=a.as.string.length==b.as.string.length &&
                !__builtin_memcmp(a.as.string.data,b.as.string.data,a.as.string.length);
            else if(a.type==Value::Array) eq=a.as.array.id==b.as.array.id;
        }
        o=bool_value(op==OP_EQ?eq:!eq); return true;
    }
    if(a.type==Value::String && b.type==Value::String && op==OP_ADD) {
        fail(Status::RuntimeError,"string concatenation requires an allocator-enabled string pool"); return false;
    }
    bool numeric=(a.type==Value::Integer||a.type==Value::Float) &&
                 (b.type==Value::Integer||b.type==Value::Float);
    if(!numeric) { fail(Status::RuntimeError,"numeric operands required"); return false; }
    double x=a.type==Value::Integer?a.as.integer:a.as.real;
    double y=b.type==Value::Integer?b.as.integer:b.as.real;
    if(op==OP_LT) o=bool_value(x<y);
    else if(op==OP_LE) o=bool_value(x<=y);
    else if(op==OP_GT) o=bool_value(x>y);
    else if(op==OP_GE) o=bool_value(x>=y);
    else {
        if((op==OP_DIV||op==OP_MOD)&&y==0) { fail(Status::RuntimeError,"division by zero"); return false; }
        if(a.type==Value::Integer && b.type==Value::Integer && op!=OP_DIV) {
            int32_t ix=a.as.integer, iy=b.as.integer, z=0;
            if(op==OP_ADD) z=ix+iy; else if(op==OP_SUB) z=ix-iy;
            else if(op==OP_MUL) z=ix*iy; else if(op==OP_MOD) z=ix%iy;
            else { fail(Status::RuntimeError,"unsupported arithmetic"); return false; }
            o=int_value(z);
        } else {
            double z=0;
            if(op==OP_ADD) z=x+y; else if(op==OP_SUB) z=x-y;
            else if(op==OP_MUL) z=x*y; else if(op==OP_DIV) z=x/y;
            else if(op==OP_MOD) { fail(Status::RuntimeError,"float modulo unsupported"); return false; }
            else { fail(Status::RuntimeError,"unsupported arithmetic"); return false; }
            o.type=Value::Float; o.as.real=z;
        }
    }
    return true;
}

bool Runtime::call(uint8_t ix) {
    if(ix>=function_count_ || frame_count_>=max_call_depth_) {
        fail(Status::StackError,"call depth or function index invalid"); return false;
    }
    Function &fn=functions_[ix];
    if(sp_<fn.params) { fail(Status::StackError,"not enough call arguments"); return false; }
    Value args[255];
    for(uint8_t i=0;i<fn.params;i++) args[i]=stack_[sp_-fn.params+i];
    sp_-=fn.params;
    Frame &f=frames_[frame_count_++];
    f.function=ix; f.ip=0; f.base=sp_;
    for(uint16_t i=0;i<fn.locals;i++) f.locals[i]=null_value();
    for(uint8_t i=0;i<fn.params;i++) f.locals[i]=args[i];
    return true;
}
void Runtime::do_return(const Value &v) {
    if(!frame_count_) { state_=TaskState::Stopped; return; }
    uint16_t base=frames_[frame_count_-1].base;
    sp_=base; // discard the callee's temporaries, preserving only its return value
    --frame_count_;
    if(!frame_count_) { state_=TaskState::Stopped; return; }
    (void)push(v);
}

void Runtime::log_value(const Value &v) {
#if defined(UMBRA_NULLK_AMD64)
    char buf[96]; uint32_t n=0;
    if(v.type==Value::String) {
        const char *p=v.as.string.data; n=v.as.string.length;
        (void)nk64_syscall(NK64_SC_LOG,0,(uint64_t)(uintptr_t)p,n,0,0,0);
    } else if(v.type==Value::Integer) {
        int32_t x=v.as.integer; char rev[16]; uint32_t k=0;
        if(x<0) { buf[n++]='-'; x=-x; }
        do { rev[k++]=(char)('0'+x%10); x/=10; } while(x&&k<15);
        while(k) buf[n++]=rev[--k];
        (void)nk64_syscall(NK64_SC_LOG,0,(uint64_t)(uintptr_t)buf,n,0,0,0);
    } else if(v.type==Value::Boolean) {
        const char *p=v.as.boolean?"true":"false"; n=v.as.boolean?4:5;
        (void)nk64_syscall(NK64_SC_LOG,0,(uint64_t)(uintptr_t)p,n,0,0,0);
    } else {
        const char *p="null"; (void)nk64_syscall(NK64_SC_LOG,0,(uint64_t)(uintptr_t)p,4,0,0,0);
    }
#else
    char buf[32]; uint32_t n=0;
    if(v.type==Value::String) {
        (void)nk_log(v.as.string.data,v.as.string.length);
    } else if(v.type==Value::Integer) {
        (void)nk_utoa((uint32_t)v.as.integer,buf,sizeof(buf),10);
        while (buf[n] != 0 && n < sizeof(buf)) ++n;
        (void)nk_log(buf,n);
    } else if(v.type==Value::Boolean) {
        const char *p=v.as.boolean?"true":"false"; (void)nk_log(p,v.as.boolean?4:5);
    } else { const char *p="null"; (void)nk_log(p,4); }
#endif
}

bool Runtime::require_capability(uint8_t id) {
    const char *name=nullptr;
    switch(id) {
        case N_PORT_SEND: name="port.send"; break;
        case N_PORT_RECEIVE: name="port.receive"; break;
        case N_MEM_ALLOC: name="memory.alloc"; break;
        case N_MEM_FREE: name="memory.free"; break;
        case N_IRQ_TIMER: name="irq.timer"; break;
        case N_TASK_STOP: name="task.stop"; break;
        default: return true;
    }
    if(!has_capability(name)) { fail(Status::CapabilityDenied,"native call lacks declared @Capability"); return false; }
    return true;
}

bool Runtime::native_call(uint8_t id,const Value *a,uint8_t argc,Value &out,bool &yielded) {
    yielded=false; out=null_value();
    if(!require_capability(id)) return false;
    if(id==N_LOG) { if(argc!=1) return false; log_value(a[0]); return true; }
    if(id==N_YIELD) {
#if defined(UMBRA_NULLK_AMD64)
        (void)nk64_syscall(NK64_SC_YIELD,0,0,0,0,0,0);
#else
        (void)nk_yield();
#endif
        yielded=true; return true;
    }
    if(id==N_INFO) {
#if defined(UMBRA_NULLK_AMD64)
        nk64_info_t info{};
        uint64_t rc=nk64_syscall(NK64_SC_GETINFO,(uint64_t)(uintptr_t)&info,0,0,0,0,0);
        if(rc!=0) { fail(Status::RuntimeError,"System.info syscall failed"); return false; }
        out=int_value((int32_t)info.task_count);
#else
        nk_info_t info{}; nk_get_info(&info); out=int_value((int32_t)info.task_count);
#endif
        return true;
    }
    // The ABI v2 kernel exposes IPC/memory/IRQ calls through libnk. The v3
    // AMD64 header currently has a different IPC contract; those bindings are
    // deliberately rejected until its endpoint/message ABI is specified.
#if !defined(UMBRA_NULLK_AMD64)
    if(id==N_MEM_ALLOC && argc==2 && a[0].type==Value::Integer && a[1].type==Value::Integer) {
        nk_mem_desc_t desc{};
        nk_status_t rc=nk_mem_alloc(&desc);
        if(rc!=NK_STATUS_OK) { fail(Status::OutOfMemory,"nk_mem_alloc failed"); return false; }
        out=int_value((int32_t)desc.block_id); return true;
    }
    if(id==N_MEM_FREE && argc==2 && a[0].type==Value::Integer) {
        nk_status_t rc=nk_mem_free((uint32_t)a[0].as.integer);
        if(rc!=NK_STATUS_OK) { fail(Status::RuntimeError,"nk_mem_free failed"); return false; }
        return true;
    }
    if(id==N_IRQ_TIMER) {
        nk_status_t rc=nk_irq_raise(0,0);
        if(rc!=NK_STATUS_OK) { fail(Status::RuntimeError,"nk_irq_raise failed"); return false; }
        return true;
    }
#endif
    fail(Status::UnsupportedNative,"native binding is not implemented for this ABI");
    return false;
}

bool Runtime::execute_one() {
    if(!frame_count_) { state_=TaskState::Stopped; return false; }
    Frame &fr=frames_[frame_count_-1];
    Function &fn=functions_[fr.function];
    if((uint32_t)fr.ip*2u>=fn.size) { fail(Status::RuntimeError,"instruction pointer escaped function"); return false; }
    const uint8_t *p=image_+code_offset_+fn.offset+(uint32_t)fr.ip*2u;
    uint8_t op=p[0], arg=p[1]; uint16_t pc=fr.ip++;
    Value a{},b{},o{};
    if(op==OP_NOP) return true;
    if(op==OP_PUSH_CONST) return push(constants_[arg]);
    if(op==OP_PUSH_TRUE) return push(bool_value(true));
    if(op==OP_PUSH_FALSE) return push(bool_value(false));
    if(op==OP_PUSH_NULL) return push(null_value());
    if(op==OP_POP) return pop(a);
    if(op==OP_DUP) { if(!sp_) { fail(Status::StackError,"dup on empty stack"); return false; } return push(stack_[sp_-1]); }
    if(op==OP_LOAD_LOCAL) return push(fr.locals[arg]);
    if(op==OP_STORE_LOCAL) { if(!pop(a)) return false; fr.locals[arg]=a; return true; }
    if(op>=OP_ADD && op<=OP_GE) {
        if(!pop(b)||!pop(a)||!binary(op,a,b,o)) return false;
        return push(o);
    }
    if(op==OP_NEG) {
        if(!pop(a)) return false;
        if(a.type==Value::Integer) a.as.integer=-a.as.integer;
        else if(a.type==Value::Float) a.as.real=-a.as.real;
        else { fail(Status::RuntimeError,"unary minus requires number"); return false; }
        return push(a);
    }
    if(op==OP_NOT) {
        if(!pop(a)) return false;
        bool truth=a.type==Value::Boolean?a.as.boolean:
                   a.type==Value::Null?false:
                   a.type==Value::Integer?a.as.integer!=0:true;
        return push(bool_value(!truth));
    }
    if(op==OP_JMP) { fr.ip=arg; return true; }
    if(op==OP_JZ || op==OP_JNZ) {
        if(!pop(a)) return false;
        bool truth=a.type==Value::Boolean?a.as.boolean:
                   a.type==Value::Null?false:
                   a.type==Value::Integer?a.as.integer!=0:true;
        if((op==OP_JZ&&!truth)||(op==OP_JNZ&&truth)) fr.ip=arg;
        return true;
    }
    if(op==OP_CALL) return call(arg);
    if(op==OP_CALL_NATIVE) {
        static const uint8_t arity[10]={0,1,0,0,2,1,2,2,0,1};
        uint8_t argc=arity[arg];
        if(sp_<argc) { fail(Status::StackError,"native argument underflow"); return false; }
        Value args[2]; for(uint8_t i=0;i<argc;i++) args[i]=stack_[sp_-argc+i];
        bool yielded=false;
        if(!native_call(arg,args,argc,o,yielded)) return false;
        sp_-=argc;
        if(!push(o)) return false;
        if(yielded) { state_=TaskState::Ready; return false; }
        return true;
    }
    if(op==OP_RET || op==OP_RET_VOID) {
        if(op==OP_RET) { if(!pop(a)) return false; } else a=null_value();
        do_return(a); return state_!=TaskState::Failed;
    }
    if(op==OP_HALT) { state_=TaskState::Stopped; return false; }
    if(op==OP_ARRAY_NEW) { if(!allocate_array(arg,o)) return false; return push(o); }
    if(op==OP_ARRAY_GET) {
        if(!pop(b)||!pop(a)||a.type!=Value::Array||b.type!=Value::Integer) {
            fail(Status::RuntimeError,"invalid array access"); return false;
        }
        Array &ar=arrays_[a.as.array.id];
        if(!ar.used||b.as.integer<0||(uint16_t)b.as.integer>=ar.length) {
            fail(Status::RuntimeError,"array index out of range"); return false;
        }
        return push(ar.items[b.as.integer]);
    }
    if(op==OP_ARRAY_SET) {
        Value value,index,array;
        if(!pop(value)||!pop(index)||!pop(array)||array.type!=Value::Array||index.type!=Value::Integer) {
            fail(Status::RuntimeError,"invalid array assignment"); return false;
        }
        Array &ar=arrays_[array.as.array.id];
        if(!ar.used||index.as.integer<0||(uint16_t)index.as.integer>=ar.length) {
            fail(Status::RuntimeError,"array index out of range"); return false;
        }
        ar.items[index.as.integer]=value; return true;
    }
    if(op==OP_YIELD) {
#if defined(UMBRA_NULLK_AMD64)
        (void)nk64_syscall(NK64_SC_YIELD,0,0,0,0,0,0);
#else
        (void)nk_yield();
#endif
        state_=TaskState::Ready; return false;
    }
    fail(Status::InvalidOpcode,"invalid opcode"); (void)pc; return false;
}

Result Runtime::step(uint32_t budget) {
    if(state_==TaskState::Stopped) return {Status::Halted,instructions_};
    if(state_==TaskState::Failed) return {last_status_,instructions_};
    if(state_==TaskState::Empty) return {Status::InvalidPackage,0};
    state_=TaskState::Running;
    uint32_t limit=budget?budget:budget_;
    uint32_t done=0;
    while(done<limit && state_==TaskState::Running) {
        if(!execute_one()) break;
        ++done; ++instructions_;
    }
    if(state_==TaskState::Running) state_=TaskState::Ready;
    return {last_status_,done};
}
Result Runtime::run(uint32_t max_instructions) {
    uint32_t total=0;
    while(state_!=TaskState::Stopped && state_!=TaskState::Failed &&
          total<max_instructions) {
        Result r=step(128); total+=r.detail;
        if(r.detail==0 && state_==TaskState::Ready) break;
    }
    return {last_status_,total};
}
void Runtime::stop() {
    state_=TaskState::Stopped; frame_count_=0; sp_=0;
}

} // namespace umbra
