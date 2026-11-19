//! NullKernRT native core (Rust facade over the freestanding C core).
//!
//! The crate is `no_std` and needs only an allocator for owned strings. It exposes a safe
//! `Kernel` handle over caller-provided aligned storage, so firmware can place the kernel
//! in a static while hosted tests can use `KernelStorage` on the stack.
#![cfg_attr(not(test), no_std)]

extern crate alloc;

use alloc::string::String;
use core::ffi::{c_char, c_int, c_void};
use core::ptr;

/// Stable ABI values mirrored from `SDK/include/gothicos/kernel_abi.h`.
pub mod abi {
    pub const VERSION: u32 = 0x0001_0101;
    pub const STATUS_OK: i32 = 0;
    pub const STATUS_INVALID_ARGUMENT: i32 = 1;
    pub const STATUS_NOT_FOUND: i32 = 2;
    pub const STATUS_WOULD_BLOCK: i32 = 3;
    pub const STATUS_QUEUE_FULL: i32 = 4;
    pub const STATUS_UNSUPPORTED: i32 = 5;
    pub const STATUS_INTERNAL_ERROR: i32 = 6;
    pub const STATUS_PERMISSION_DENIED: i32 = 7;
    pub const STATUS_ALREADY_EXISTS: i32 = 8;
    pub const STATUS_OUT_OF_MEMORY: i32 = 9;

    pub const OP_GET_INFO: i32 = 0;
    pub const OP_TASK_CREATE: i32 = 1;
    pub const OP_TASK_STOP: i32 = 2;
    pub const OP_TASK_RESUME: i32 = 3;
    pub const OP_TASK_STATE: i32 = 4;
    pub const OP_IPC_SEND: i32 = 5;
    pub const OP_IPC_RECEIVE: i32 = 6;
    pub const OP_YIELD: i32 = 7;
    pub const OP_RUN: i32 = 8;
    pub const OP_TASK_INFO: i32 = 9;
    pub const OP_PORT_OPEN: i32 = 10;
    pub const OP_PORT_CLOSE: i32 = 11;
    pub const OP_PORT_SEND: i32 = 12;
    pub const OP_PORT_RECEIVE: i32 = 13;
    pub const OP_PORT_INFO: i32 = 14;
    pub const OP_MEM_POOL_CREATE: i32 = 15;
    pub const OP_MEM_ALLOC: i32 = 16;
    pub const OP_MEM_FREE: i32 = 17;
    pub const OP_MEM_WRITE: i32 = 18;
    pub const OP_MEM_READ: i32 = 19;
    pub const OP_IRQ_REGISTER: i32 = 20;
    pub const OP_IRQ_UNREGISTER: i32 = 21;
    pub const OP_IRQ_RAISE: i32 = 22;
    pub const OP_IRQ_SERVICE: i32 = 23;
    pub const OP_DRIVER_INFO: i32 = 24;
    pub const OP_DRIVER_IO: i32 = 25;
    pub const OP_CAP_GRANT: i32 = 26;
    pub const OP_CAP_REVOKE: i32 = 27;
    pub const OP_CAP_INFO: i32 = 28;
    pub const OP_BOOT_INFO: i32 = 29;
    pub const OP_ABILITY_REGISTER: i32 = 30;
    pub const OP_ABILITY_START: i32 = 31;
    pub const OP_ABILITY_STOP: i32 = 32;
    pub const OP_ABILITY_STATE: i32 = 33;
    pub const OP_ABILITY_INFO: i32 = 34;
    pub const OP_SOFTBUS_SEND: i32 = 35;
    pub const OP_SOFTBUS_SUBSCRIBE: i32 = 36;
    pub const OP_SOFTBUS_INFO: i32 = 37;
    pub const OP_REMOTE_CALL: i32 = 38;
    pub const OP_REMOTE_ROUTE: i32 = 39;
    pub const OP_NODE_REGISTER: i32 = 40;
    pub const OP_NODE_INFO: i32 = 41;
    pub const OP_SCHEDULE: i32 = 42;
    pub const OP_DISTRIBUTED_INFO: i32 = 43;
}

const VALUE_NONE: i32 = 0;
const VALUE_INT: i32 = 1;
const VALUE_STRING: i32 = 2;
const MAX_ARGUMENTS: usize = 8;

#[repr(C)]
#[derive(Clone, Copy)]
struct RawValue {
    kind: i32,
    integer: i32,
    string: *const c_char,
}

#[repr(C)]
struct RawArguments {
    items: *const RawValue,
    count: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RawResult {
    status: c_int,
    value: RawValue,
}

type StepFn = unsafe extern "C" fn(*mut c_void, c_int, *mut c_void) -> c_int;

extern "C" {
    fn nk_kernel_storage_size() -> usize;
    fn nk_kernel_init(
        storage: *mut c_void,
        size: usize,
        core_count: u32,
        first_managed_cpu: u32,
    ) -> *mut c_void;
    fn nk_invoke(
        kernel: *mut c_void,
        opcode: c_int,
        arguments: *const RawArguments,
        result: *mut RawResult,
    ) -> c_int;
    fn nk_boot(kernel: *mut c_void) -> c_int;
    fn nk_run(kernel: *mut c_void, max_steps: c_int) -> c_int;
    fn nk_dispatch_one(kernel: *mut c_void) -> c_int;
    fn nk_service_irqs(kernel: *mut c_void, limit: c_int) -> c_int;
    fn nk_task_spawn(
        kernel: *mut c_void,
        name: *const c_char,
        step: Option<StepFn>,
        context: *mut c_void,
    ) -> c_int;
    fn nk_kernel_shutdown(kernel: *mut c_void);
    fn nk_status_name(status: c_int) -> *const c_char;
    fn nk_abi_version() -> u32;
}

/// Aligned backing storage for one native kernel instance.
#[repr(align(16))]
pub struct KernelStorage {
    bytes: [u8; 192 * 1024],
}

impl KernelStorage {
    pub fn new() -> Self {
        Self {
            bytes: [0; 192 * 1024],
        }
    }
}

impl Default for KernelStorage {
    fn default() -> Self {
        Self::new()
    }
}

/// Safe handle over a native kernel. The storage must outlive the handle.
pub struct Kernel {
    pointer: *mut c_void,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Value {
    None,
    Int(i32),
    Text(String),
}

impl Value {
    pub fn int(value: i32) -> Self {
        Value::Int(value)
    }

    pub fn text(value: &str) -> Self {
        Value::Text(String::from(value))
    }
}

impl Kernel {
    /// Initialise a kernel in `storage`. `first_managed_cpu` must be >= 1 when Linux is
    /// the host; CPU 0 is never handed to NullKern.
    pub fn init(
        storage: &mut KernelStorage,
        core_count: u32,
        first_managed_cpu: u32,
    ) -> Option<Self> {
        let required = unsafe { nk_kernel_storage_size() };
        if required > storage.bytes.len() || core_count == 0 {
            return None;
        }
        let pointer = unsafe {
            nk_kernel_init(
                storage.bytes.as_mut_ptr() as *mut c_void,
                required,
                core_count,
                first_managed_cpu,
            )
        };
        if pointer.is_null() {
            None
        } else {
            Some(Self { pointer })
        }
    }

    pub fn boot(&mut self) -> i32 {
        unsafe { nk_boot(self.pointer) }
    }

    pub fn run(&mut self, max_steps: i32) -> i32 {
        unsafe { nk_run(self.pointer, max_steps) }
    }

    pub fn dispatch_one(&mut self) -> i32 {
        unsafe { nk_dispatch_one(self.pointer) }
    }

    pub fn service_irqs(&mut self, limit: i32) -> i32 {
        unsafe { nk_service_irqs(self.pointer, limit) }
    }

    pub fn invoke(&mut self, opcode: i32, arguments: &[Value]) -> Result<Value, i32> {
        if arguments.len() > MAX_ARGUMENTS {
            return Err(abi::STATUS_INVALID_ARGUMENT);
        }
        let mut raw_arguments = [RawValue {
            kind: VALUE_NONE,
            integer: 0,
            string: ptr::null(),
        }; MAX_ARGUMENTS];
        let mut string_buffers: alloc::vec::Vec<alloc::vec::Vec<u8>> = alloc::vec::Vec::new();
        for (slot, value) in raw_arguments.iter_mut().zip(arguments.iter()) {
            match value {
                Value::None => {
                    slot.kind = VALUE_NONE;
                }
                Value::Int(integer) => {
                    slot.kind = VALUE_INT;
                    slot.integer = *integer;
                }
                Value::Text(text) => {
                    let mut bytes = alloc::vec::Vec::with_capacity(text.len() + 1);
                    bytes.extend_from_slice(text.as_bytes());
                    bytes.push(0);
                    slot.kind = VALUE_STRING;
                    slot.string = bytes.as_ptr() as *const c_char;
                    string_buffers.push(bytes);
                }
            }
        }
        let payload = RawArguments {
            items: raw_arguments.as_ptr(),
            count: arguments.len() as u32,
        };
        let mut result = RawResult {
            status: abi::STATUS_OK,
            value: RawValue {
                kind: VALUE_NONE,
                integer: 0,
                string: ptr::null(),
            },
        };
        let status = unsafe { nk_invoke(self.pointer, opcode, &payload, &mut result) };
        if status != abi::STATUS_OK {
            return Err(status);
        }
        Ok(match result.value.kind {
            VALUE_INT => Value::Int(result.value.integer),
            VALUE_STRING if !result.value.string.is_null() => unsafe {
                Value::Text(string_from_ptr(result.value.string))
            },
            _ => Value::None,
        })
    }

    /// Spawn a native task. `step` must be a non-capturing `extern "C"` function.
    pub fn spawn(&mut self, name: &str, step: StepFn, context: *mut c_void) -> Result<i32, i32> {
        let name = null_terminated(name);
        let id = unsafe {
            nk_task_spawn(
                self.pointer,
                name.as_ptr() as *const c_char,
                Some(step),
                context,
            )
        };
        if id < 0 {
            Err(abi::STATUS_OUT_OF_MEMORY)
        } else {
            Ok(id)
        }
    }

    pub fn raw(&mut self) -> *mut c_void {
        self.pointer
    }
}

impl Drop for Kernel {
    fn drop(&mut self) {
        unsafe { nk_kernel_shutdown(self.pointer) }
    }
}

unsafe fn string_from_ptr(pointer: *const c_char) -> String {
    let mut length = 0usize;
    while *pointer.add(length) != 0 {
        length += 1;
    }
    let slice = core::slice::from_raw_parts(pointer as *const u8, length);
    String::from_utf8_lossy(slice).into_owned()
}

fn null_terminated(value: &str) -> alloc::vec::Vec<u8> {
    let mut bytes = alloc::vec::Vec::with_capacity(value.len() + 1);
    bytes.extend_from_slice(value.as_bytes());
    bytes.push(0);
    bytes
}

pub fn status_name(status: i32) -> &'static str {
    unsafe {
        let pointer = nk_status_name(status);
        if pointer.is_null() {
            return "UNKNOWN";
        }
        core::ffi::CStr::from_ptr(pointer)
            .to_str()
            .unwrap_or("UNKNOWN")
    }
}

pub fn abi_version() -> u32 {
    unsafe { nk_abi_version() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn boots_and_reports_abi() {
        let mut storage = KernelStorage::new();
        let mut kernel = Kernel::init(&mut storage, 4, 1).expect("kernel init");
        assert_eq!(abi_version(), abi::VERSION);
        assert_eq!(kernel.boot(), abi::STATUS_OK);
        assert_eq!(
            kernel.invoke(abi::OP_BOOT_INFO, &[]).unwrap(),
            Value::Int(1)
        );
        assert_eq!(
            kernel.invoke(abi::OP_GET_INFO, &[]).unwrap(),
            Value::Int(abi::VERSION as i32)
        );
    }

    #[test]
    fn ports_round_trip() {
        let mut storage = KernelStorage::new();
        let mut kernel = Kernel::init(&mut storage, 1, 0).expect("kernel init");
        assert_eq!(
            kernel
                .invoke(abi::OP_PORT_OPEN, &[Value::text("rust.port")])
                .unwrap(),
            Value::None
        );
        assert_eq!(
            kernel
                .invoke(
                    abi::OP_PORT_SEND,
                    &[Value::text("rust.port"), Value::text("hello")]
                )
                .unwrap(),
            Value::None
        );
        assert_eq!(
            kernel
                .invoke(abi::OP_PORT_RECEIVE, &[Value::text("rust.port")])
                .unwrap(),
            Value::Text(String::from("hello"))
        );
    }
}
