//! NullKernRT HAL (Rust): `no_std` traits for platform services and CPU topology.
//!
//! CPU 0 is reserved for Linux in the hosted configuration. The default `bind_cpu`
//! implementation rejects it, and every handoff state machine goes through this check.
#![cfg_attr(not(test), no_std)]

extern crate alloc;

use alloc::string::String;

pub const DEFAULT_INIT_PATH: &str = "@flash/data/system/init.d.elf";
pub const DEFAULT_SHELL_PATH: &str = "@flash/data/user/bin/sh.uts";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CpuTopology {
    pub total: u32,
    pub first_managed: u32,
    pub managed: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel {
    Debug,
    Info,
    Warning,
    Error,
}

pub trait Platform {
    fn name(&self) -> &'static str;
    fn now_ns(&self) -> u64;
    fn topology(&self) -> CpuTopology;
    fn log(&self, level: LogLevel, message: &str);

    /// Bind a managed CPU. CPU 0 is never available to NullKern under Linux.
    fn bind_cpu(&self, cpu: u32) -> Result<(), ()> {
        let topology = self.topology();
        if cpu == 0 || cpu < topology.first_managed || cpu >= topology.total {
            Err(())
        } else {
            Ok(())
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct NullPlatform {
    total: u32,
    first_managed: u32,
}

impl NullPlatform {
    pub const fn new(total: u32, first_managed: u32) -> Self {
        Self {
            total: if total == 0 { 1 } else { total },
            first_managed,
        }
    }
}

impl Default for NullPlatform {
    fn default() -> Self {
        Self::new(1, 0)
    }
}

impl Platform for NullPlatform {
    fn name(&self) -> &'static str {
        "null"
    }

    fn now_ns(&self) -> u64 {
        0
    }

    fn topology(&self) -> CpuTopology {
        CpuTopology {
            total: self.total,
            first_managed: self.first_managed,
            managed: if self.first_managed < self.total {
                self.total - self.first_managed
            } else {
                0
            },
        }
    }

    fn log(&self, _level: LogLevel, _message: &str) {}
}

/// Parsed form of `-v -debug=0x01 -root=@flash/data -mem=200K`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BootOptions {
    pub verbose: bool,
    pub debug_flags: u32,
    pub root: String,
    pub memory_limit_kb: u32,
}

impl Default for BootOptions {
    fn default() -> Self {
        Self {
            verbose: false,
            debug_flags: 0,
            root: String::new(),
            memory_limit_kb: 0,
        }
    }
}

impl BootOptions {
    pub fn init_path(&self) -> String {
        let mut path = self.root.clone();
        if !path.ends_with('/') {
            path.push('/');
        }
        path.push_str("system/init.d.elf");
        path
    }

    pub fn shell_path(&self) -> String {
        let mut path = self.root.clone();
        if !path.ends_with('/') {
            path.push('/');
        }
        path.push_str("user/bin/sh.uts");
        path
    }

    pub fn parse(options: &str) -> Result<Self, ()> {
        let mut result = Self::default();
        for token in options.split_whitespace() {
            if token == "-v" {
                result.verbose = true;
            } else if let Some(value) = token.strip_prefix("-debug=") {
                result.debug_flags = parse_number(value)?;
            } else if let Some(value) = token.strip_prefix("-root=") {
                if value.is_empty() {
                    return Err(());
                }
                result.root = String::from(value);
            } else if let Some(value) = token.strip_prefix("-mem=") {
                result.memory_limit_kb = parse_memory(value)?;
            } else {
                return Err(());
            }
        }
        Ok(result)
    }
}

fn parse_number(value: &str) -> Result<u32, ()> {
    let (digits, radix) = match value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        Some(rest) => (rest, 16),
        None => (value, 10),
    };
    if digits.is_empty() {
        return Err(());
    }
    u32::from_str_radix(digits, radix).map_err(|_| ())
}

fn parse_memory(value: &str) -> Result<u32, ()> {
    let (digits, multiplier) =
        if let Some(rest) = value.strip_suffix('K').or_else(|| value.strip_suffix('k')) {
            (rest, 1)
        } else if let Some(rest) = value.strip_suffix('M').or_else(|| value.strip_suffix('m')) {
            (rest, 1024)
        } else {
            (value, 1)
        };
    parse_number(digits)?.checked_mul(multiplier).ok_or(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_boot_options() {
        let options = BootOptions::parse("-v -debug=0x01 -root=@flash/data -mem=200K").unwrap();
        assert!(options.verbose);
        assert_eq!(options.debug_flags, 1);
        assert_eq!(options.memory_limit_kb, 200);
        assert_eq!(options.init_path(), "@flash/data/system/init.d.elf");
        assert_eq!(options.shell_path(), "@flash/data/user/bin/sh.uts");
    }

    #[test]
    fn never_binds_cpu_zero() {
        let platform = NullPlatform::new(4, 1);
        assert!(platform.bind_cpu(0).is_err());
        assert!(platform.bind_cpu(1).is_ok());
        assert!(platform.bind_cpu(4).is_err());
    }
}
