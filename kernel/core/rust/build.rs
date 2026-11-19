use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let out = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR"));
    let crate_root = manifest.parent().expect("core directory");
    let project_root = crate_root
        .parent()
        .expect("kernel directory")
        .parent()
        .expect("project root");

    let c_source = crate_root.join("c").join("nk_core.c");
    let core_include = crate_root.join("include");
    let sdk_include = project_root.join("SDK").join("include");

    let object = out.join("nk_core.o");
    let cc = env::var("CC").unwrap_or_else(|_| "cc".to_string());
    let status = Command::new(&cc)
        .arg("-std=c11")
        .arg("-O2")
        .arg("-fPIC")
        .arg("-c")
        .arg(format!("-I{}", core_include.display()))
        .arg(format!("-I{}", sdk_include.display()))
        .arg(&c_source)
        .arg("-o")
        .arg(&object)
        .status()
        .expect("unable to run cc for nk_core.c");
    assert!(status.success(), "cc failed while compiling nk_core.c");

    let library = out.join("libnk_core.a");
    let ar = env::var("AR").unwrap_or_else(|_| "ar".to_string());
    let status = Command::new(&ar)
        .arg("crs")
        .arg(&library)
        .arg(&object)
        .status()
        .expect("unable to run ar for libnk_core.a");
    assert!(status.success(), "ar failed while archiving libnk_core.a");

    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=nk_core");
    println!("cargo:rerun-if-changed={}", c_source.display());
    println!(
        "cargo:rerun-if-changed={}",
        core_include.join("gothicos/nk_core.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        sdk_include.join("gothicos/kernel_abi.h").display()
    );
}
