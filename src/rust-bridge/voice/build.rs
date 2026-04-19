//! build.rs - 配置 SDL2 库路径
//!
//! 为 Rust sdl2 crate 配置 ddnet-libs 中的 SDL2 库路径

use std::env;
use std::path::PathBuf;

fn main() {
    // 仅在启用 sdl-backend feature 时配置
    if env::var("CARGO_FEATURE_SDL_BACKEND").is_ok() {
        configure_sdl2();
    }
}

fn configure_sdl2() {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let manifest_path = PathBuf::from(&manifest_dir);
    let project_root = manifest_path
        .parent()
        .and_then(|p| p.parent())
        .and_then(|p| p.parent())
        .expect("Failed to find project root");
    
    let ddnet_libs = project_root.join("ddnet-libs").join("sdl");
    
    // 检测目标平台
    let target = env::var("TARGET").unwrap();
    let (lib_dir, include_dir) = if target.contains("windows") {
        if target.contains("x86_64") {
            (ddnet_libs.join("windows").join("lib64"), ddnet_libs.join("include").join("windows"))
        } else if target.contains("i686") {
            (ddnet_libs.join("windows").join("lib32"), ddnet_libs.join("include").join("windows"))
        } else if target.contains("aarch64") {
            (ddnet_libs.join("windows").join("libarm64"), ddnet_libs.join("include").join("windows"))
        } else {
            panic!("Unsupported Windows target: {}", target);
        }
    } else if target.contains("linux") {
        if target.contains("x86_64") {
            (ddnet_libs.join("linux").join("lib64"), ddnet_libs.join("include"))
        } else if target.contains("i686") {
            (ddnet_libs.join("linux").join("lib32"), ddnet_libs.join("include"))
        } else {
            // 对于其他 Linux 目标，使用系统库
            println!("cargo:rustc-link-lib=SDL2");
            return;
        }
    } else if target.contains("apple") && target.contains("darwin") {
        if target.contains("x86_64") {
            (ddnet_libs.join("mac").join("lib64"), ddnet_libs.join("include").join("mac"))
        } else if target.contains("aarch64") {
            (ddnet_libs.join("mac").join("libarm64"), ddnet_libs.join("include").join("mac"))
        } else {
            (ddnet_libs.join("mac").join("libfat"), ddnet_libs.join("include").join("mac"))
        }
    } else if target.contains("android") {
        if target.contains("aarch64") {
            (ddnet_libs.join("android").join("libarm64"), ddnet_libs.join("include").join("android"))
        } else if target.contains("armv7") {
            (ddnet_libs.join("android").join("libarm"), ddnet_libs.join("include").join("android"))
        } else if target.contains("x86_64") {
            (ddnet_libs.join("android").join("lib64"), ddnet_libs.join("include").join("android"))
        } else if target.contains("i686") {
            (ddnet_libs.join("android").join("lib32"), ddnet_libs.join("include").join("android"))
        } else {
            panic!("Unsupported Android target: {}", target);
        }
    } else {
        println!("cargo:warning=Unsupported target for SDL2: {}, using system library", target);
        println!("cargo:rustc-link-lib=SDL2");
        return;
    };

    // 设置 SDL2_LIB_DIR 环境变量
    if lib_dir.exists() {
        println!("cargo:rustc-env=SDL2_LIB_DIR={}", lib_dir.display());
        println!("cargo:rerun-if-changed={}", lib_dir.display());
    } else {
        println!("cargo:warning=SDL2 lib directory not found: {}", lib_dir.display());
        println!("cargo:warning=Falling back to system SDL2");
        println!("cargo:rustc-link-lib=SDL2");
        return;
    }

    // 设置 SDL2_INCLUDE_DIR 环境变量
    if include_dir.exists() {
        println!("cargo:rustc-env=SDL2_INCLUDE_DIR={}", include_dir.display());
        println!("cargo:rerun-if-changed={}", include_dir.display());
    }

    // Windows 特定配置
    if target.contains("windows") {
        // 链接 SDL2.lib
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
        println!("cargo:rustc-link-lib=static=SDL2");
        
        // 复制 SDL2.dll 到输出目录
        let dll_path = lib_dir.join("SDL2.dll");
        if dll_path.exists() {
            let out_dir = env::var("OUT_DIR").unwrap();
            let dest = PathBuf::from(&out_dir).join("SDL2.dll");
            if !dest.exists() {
                std::fs::copy(&dll_path, &dest).expect("Failed to copy SDL2.dll");
            }
        }
    }
    
    // macOS 特定配置
    if target.contains("apple") && target.contains("darwin") {
        // Framework 路径
        let framework_path = lib_dir.join("SDL2.framework");
        if framework_path.exists() {
            println!("cargo:rustc-link-search=framework={}", lib_dir.display());
            println!("cargo:rustc-link-lib=framework=SDL2");
        }
    }
    
    // Linux 特定配置
    if target.contains("linux") {
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
        println!("cargo:rustc-link-lib=dylib=SDL2-2.0");
    }
    
    // Android 特定配置
    if target.contains("android") {
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
        println!("cargo:rustc-link-lib=static=SDL2");
    }

    println!("cargo:rerun-if-changed=build.rs");
}
