//! ddnet-voice 构建脚本
//!
//! 配置链接到 ddnet-libs 中的 Opus 库

fn main() {
    // 检测目标平台
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap_or_default();
    
    // 获取项目根目录 (ddnet-libs 的父目录)
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let project_root = std::path::Path::new(&manifest_dir)
        .parent()
        .and_then(|p| p.parent())
        .and_then(|p| p.parent())
        .expect("Failed to find project root");
    
    let ddnet_libs = project_root.join("ddnet-libs");
    let opus_dir = ddnet_libs.join("opus");
    
    // 根据平台选择库目录
    let (lib_dir, lib_prefix) = match (target_os.as_str(), target_arch.as_str()) {
        ("windows", "x86_64") => (opus_dir.join("windows").join("lib64"), ""),
        ("windows", "x86") => (opus_dir.join("windows").join("lib32"), ""),
        ("windows", "aarch64") => (opus_dir.join("windows").join("libarm64"), ""),
        ("linux", "x86_64") => (opus_dir.join("linux").join("lib64"), "lib"),
        ("linux", "x86") => (opus_dir.join("linux").join("lib32"), "lib"),
        ("linux", "aarch64") => (opus_dir.join("linux").join("lib64"), "lib"),
        ("macos", "x86_64") => (opus_dir.join("mac").join("lib64"), "lib"),
        ("macos", "aarch64") => (opus_dir.join("mac").join("libarm64"), "lib"),
        ("android", _) => {
            let arch_dir = match target_arch.as_str() {
                "x86_64" => "lib64",
                "x86" => "lib32",
                "aarch64" => "libarm64",
                "arm" => "libarm",
                _ => "lib64",
            };
            (opus_dir.join("android").join(arch_dir), "lib")
        }
        _ => {
            println!("cargo:warning=Unsupported platform: {}-{}", target_os, target_arch);
            return;
        }
    };
    
    // 添加头文件路径
    let include_dir = opus_dir.join("include");
    
    // 设置 opus crate 的环境变量
    // 告诉 audiopus_sys 使用预编译的库
    std::env::set_var("OPUS_LIB_DIR", lib_dir.to_str().unwrap());
    std::env::set_var("OPUS_INCLUDE_DIR", include_dir.to_str().unwrap());
    
    // 打印调试信息
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=TARGET");
    println!("cargo:warning=ddnet-voice: Using Opus from {}", lib_dir.display());
    println!("cargo:warning=ddnet-voice: Using Opus headers from {}", include_dir.display());
}
