//! NullKernRT KAL (Rust): safe, error-returning interface over the native core.
#![cfg_attr(not(test), no_std)]

extern crate alloc;

use alloc::boxed::Box;
use alloc::string::{String, ToString};

pub use nk_core::abi;
pub use nk_core::{status_name, Value};
use nk_core::{Kernel as NativeKernel, KernelStorage};

pub use nk_hal::{BootOptions, CpuTopology, LogLevel, NullPlatform, Platform};

/// Owns one native kernel instance. The boxed storage keeps the C pointer stable.
pub struct Kernel {
    // Drop order is declaration order: shut down the kernel before freeing its storage.
    native: NativeKernel,
    _storage: Box<KernelStorage>,
}

impl Kernel {
    pub fn new(core_count: u32, first_managed_cpu: u32) -> Option<Self> {
        let mut storage = Box::new(KernelStorage::new());
        let native = NativeKernel::init(&mut storage, core_count, first_managed_cpu)?;
        Some(Self {
            _storage: storage,
            native,
        })
    }

    /// Hosted default: Linux keeps CPU 0, NullKern owns CPUs 1..core_count.
    pub fn hosted(core_count: u32) -> Option<Self> {
        Self::new(core_count, 1)
    }

    pub fn boot(&mut self) -> Result<(), i32> {
        match self.native.boot() {
            abi::STATUS_OK => Ok(()),
            status => Err(status),
        }
    }

    pub fn run(&mut self, max_steps: i32) -> i32 {
        self.native.run(max_steps)
    }

    pub fn call(&mut self, opcode: i32, arguments: &[Value]) -> Result<Value, i32> {
        self.native.invoke(opcode, arguments)
    }

    pub fn port(&mut self, name: &str) -> Port<'_> {
        Port {
            kernel: self,
            name: name.to_string(),
        }
    }

    pub fn task(&mut self, id: i32) -> Task<'_> {
        Task { kernel: self, id }
    }
}

pub struct Port<'a> {
    kernel: &'a mut Kernel,
    name: String,
}

impl<'a> Port<'a> {
    pub fn open(&mut self, capacity: i32) -> Result<(), i32> {
        self.kernel
            .call(
                abi::OP_PORT_OPEN,
                &[Value::text(&self.name), Value::int(capacity)],
            )
            .map(|_| ())
    }

    pub fn send(&mut self, message: &str) -> Result<(), i32> {
        self.kernel
            .call(
                abi::OP_PORT_SEND,
                &[Value::text(&self.name), Value::text(message)],
            )
            .map(|_| ())
    }

    pub fn receive(&mut self) -> Result<String, i32> {
        match self
            .kernel
            .call(abi::OP_PORT_RECEIVE, &[Value::text(&self.name)])?
        {
            Value::Text(text) => Ok(text),
            _ => Err(abi::STATUS_WOULD_BLOCK),
        }
    }

    pub fn message_count(&mut self) -> Result<i32, i32> {
        match self
            .kernel
            .call(abi::OP_PORT_INFO, &[Value::text(&self.name)])?
        {
            Value::Int(value) => Ok(value),
            _ => Err(abi::STATUS_INTERNAL_ERROR),
        }
    }
}

pub struct Task<'a> {
    kernel: &'a mut Kernel,
    id: i32,
}

impl<'a> Task<'a> {
    pub fn id(&self) -> i32 {
        self.id
    }

    pub fn state(&mut self) -> Result<i32, i32> {
        match self
            .kernel
            .call(abi::OP_TASK_STATE, &[Value::int(self.id)])?
        {
            Value::Int(state) => Ok(state),
            _ => Err(abi::STATUS_INTERNAL_ERROR),
        }
    }

    pub fn stop(&mut self) -> Result<(), i32> {
        self.kernel
            .call(abi::OP_TASK_STOP, &[Value::int(self.id)])
            .map(|_| ())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hosted_kernel_boots_and_routes_ports() {
        let mut kernel = Kernel::hosted(4).expect("hosted kernel");
        kernel.boot().expect("boot");
        let mut port = kernel.port("kal.rust");
        port.open(4).expect("open");
        port.send("from-rust").expect("send");
        assert_eq!(port.receive().unwrap(), "from-rust");
        assert_eq!(port.message_count().unwrap(), 0);
    }
}
