---
alwaysApply: false
description: QmClient项目中Rust与C++跨语言集成时触发。涵盖：双FFI机制(cxx bridge/手写C ABI)、voice语音模块、engine核心绑定、Cargo+CMake构建。铁律：Rust为非侵入式静态库；手写C ABI必须catch\_unwind+ffi\_wrap宏；禁止panic跨边界；禁止阻塞渲染线程；所有权谁分配谁释放。排除：纯C++逻辑、独立Rust工具(mastersrv/masterping)。
---


# QmClient Rust/C++ 跨语言集成规范

## 1. 双 FFI 机制

本项目存在两套独立的 Rust/C++ 互操作机制，适用场景不同：

| 机制             | 适用场景                  | 代表模块                                          | FFI 框架                                  |
| -------------- | --------------------- | --------------------------------------------- | --------------------------------------- |
| **cxx bridge** | 调用 C++ 已有类/接口         | ddnet-base, ddnet-engine, ddnet-engine-shared | `cxx` crate                             |
| **手写 C ABI**   | Rust 自主实现业务逻辑，C++ 仅调用 | ddnet-voice                                   | `#[no_mangle] extern "C"` + `ffi_wrap!` |

### 1.1 cxx bridge 规范（上游 DDNet 集成）

用于 Rust 代码需要调用 C++ 类方法（如 `IConsole`）的场景：

- Rust 侧通过 `#[cxx::bridge] mod ffi { unsafe extern "C++" { ... } }` 声明 C++ 接口
- C++ 侧桥接代码由 cxx 自动生成，存放在 `src/rust-bridge/cpp/` 和 `src/rust-bridge/engine/`
- 类型桥接：`StrRef`（对应 `const char*`）、`UserPtr`（对应 `void*`）、`ColorRGBA`/`ColorHSLA`
- **禁止修改** cxx 生成的桥接代码，如需新增接口，修改 Rust 侧 `#[cxx::bridge]` 声明后重新生成

涉及文件：

- `src/base/rust.rs` — StrRef/UserPtr 类型定义
- `src/base/rust.h` — C++ 侧 StrRef/UserPtr 类型别名
- `src/engine/console.rs` — IConsole cxx bridge
- `src/engine/shared/rust_version.rs` — RustVersionPrint/Register 导出

### 1.2 手写 C ABI 规范（QmClient 自定义模块）

用于 Rust 自主实现完整业务逻辑、C++ 仅作为调用方的场景：

**强制规则**：

- 所有导出函数必须通过 `ffi_wrap!` 宏定义，自动添加 `catch_unwind` 保护
- 返回类型仅限：`usize`（不透明句柄）、`i32`/`u32`/`f32`、`#[repr(C)]` struct
- 禁止传递 `String`、`Vec`、`&str`、`Option`、`Result`、`bool` 过边界
- 布尔值用 `i32`（0/1）传递，避免对齐问题
- 不透明句柄用 `usize`（非 `*mut c_void`），C++ 侧禁止解引用

**ffi\_wrap! 宏模板**（项目已定义于 `src/rust-bridge/voice/src/lib.rs`）：

```rust
macro_rules! ffi_wrap {
    (fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) -> $ret:ty $body:block) => {
        #[no_mangle]
        pub extern "C" fn $fn_name($($param: $ty),*) -> $ret {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
                Default::default()
            })
        }
    };
    (unsafe fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) -> $ret:ty $body:block) => {
        #[no_mangle]
        pub extern "C" fn $fn_name($($param: $ty),*) -> $ret {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
                Default::default()
            })
        }
    };
    (unsafe fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) $body:block) => {
        #[no_mangle]
        pub extern "C" fn $fn_name($($param: $ty),*) {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
            })
        }
    };
}
```

**不透明句柄模式**：

```rust
struct VoiceSystemHandle {
    system: Box<VoiceSystem>,
}

ffi_wrap!(fn voice_system_create() -> usize {
    let handle = Box::new(VoiceSystemHandle {
        system: Box::new(VoiceSystem::new()),
    });
    Box::into_raw(handle) as usize
});

ffi_wrap!(unsafe fn voice_system_destroy(handle: usize) {
    if handle != 0 {
        let _ = Box::from_raw(handle as *mut VoiceSystemHandle);
    }
});
```

**C ABI 配置结构体模式**（全 i32 字段 + 编译期断言）：

```rust
#[repr(C)]
pub struct VoiceConfigCABI {
    pub mic_volume: i32,
    pub noise_suppress: i32,
    pub noise_suppress_strength: i32,
}

const _: () = assert!(
    std::mem::size_of::<VoiceConfigCABI>() == N * std::mem::size_of::<i32>()
);
```

C++ 侧必须保持一致布局并添加 `static_assert`：

```cpp
struct VoiceConfigCABI {
    int32_t mic_volume;
    int32_t noise_suppress;
    int32_t noise_suppress_strength;
};

static_assert(sizeof(VoiceConfigCABI) == N * sizeof(int32_t));
static_assert(alignof(VoiceConfigCABI) == alignof(int32_t));
```

## 2. 所有权与生命周期

- **谁分配谁释放**：Rust `Box::into_raw` 分配 → Rust `Box::from_raw` 释放；C++ `new` 分配 → C++ `delete` 释放
- **不透明句柄**：C++ 持有 `size_t`/`usize` 句柄，禁止解引用或假设内部布局
- **字符串**：C++ → Rust 用 `*const c_char`（cxx 场景用 `StrRef`），Rust 只读不释放
- **C++ 回调**：Rust 通过 `extern "C"` 回调函数调用 C++（如 `on_voice_packet`），回调内禁止阻塞

## 3. 线程安全

- Rust 网络/IO 线程（tokio runtime）禁止直接回调 C++ 渲染主线程，必须通过无锁队列（crossbeam）投递
- C++ 调用 Rust FFI 必须为非阻塞，立即返回
- 音频处理帧函数（`voice_process_frame` 等）在渲染线程调用，必须零分配

## 4. 模块职责与约束

### 4.1 上游 cxx 模块（不可修改核心逻辑）

| Crate               | 路径                   | 职责                               | 产出               |
| ------------------- | -------------------- | -------------------------------- | ---------------- |
| ddnet-base          | `src/base/`          | 基础类型（ColorRGBA, StrRef, UserPtr） | rlib             |
| ddnet-engine        | `src/engine/`        | IConsole 接口绑定                    | rlib             |
| ddnet-engine-shared | `src/engine/shared/` | 配置常量 + rust\_version 命令          | rlib + staticlib |

### 4.2 QmClient 自定义模块

| Crate       | 路径                       | 职责             | 产出               |
| ----------- | ------------------------ | -------------- | ---------------- |
| ddnet-voice | `src/rust-bridge/voice/` | 完整语音聊天系统       | rlib + staticlib |
| ddnet-test  | `src/rust-bridge/test/`  | 测试辅助（链接 C++ 库） | rlib             |

### 4.3 ddnet-voice 内部架构约束

```
voice/
├── lib.rs          # FFI 导出 + VoiceSystem 核心 + ffi_wrap! 宏
├── bridge/         # C++ 桥接辅助
├── audio/          # 音频采集/播放/混音（禁止直接调用 OS/图形 API）
├── codec/          # Opus 编解码（opus-rs 纯 Rust 实现）
├── dsp/            # DSP 处理链（增益→降噪→高通→压缩）
├── jitter/         # 抖动缓冲区
├── network/        # RV01 协议 + UDP 客户端/服务端（独立 tokio runtime）
└── spatial/        # 3D 空间音频（距离衰减 + 立体声声像）
```

- `audio/`：禁止直接调用 OS 窗口或图形 API，通过 cpal/SDL2 后端抽象
- `network/`：独立 tokio runtime，通过 crossbeam channel 与 FFI 层通信
- `codec/`：使用纯 Rust 实现（opus-rs、nnnoiseless），避免 C 依赖链
- `dsp/`：Hot path，零堆分配，使用预分配缓冲区

## 5. 性能等级

- **Hot**（每帧调用）：`voice_process_frame`、`voice_mix_audio`、`voice_decode_jitter` — 零堆分配，< 0.1ms
- **Warm**（每秒数次）：`voice_set_config`、`voice_update_players` — 允许轻量分配
- **Cold**（初始化）：`voice_system_create`、`voice_system_init` — 无严格限制

## 6. 错误处理

- 使用 `thiserror` 定义领域错误（voice 模块已采用）
- FFI 边界错误映射为错误码（负数 i32）或零值句柄，禁止 panic 跨越边界
- `ffi_wrap!` 宏确保所有 FFI 函数被 `catch_unwind` 包裹
- cxx 桥接由框架内部处理 panic 安全

## 7. 构建集成

### 7.1 Cargo Workspace 配置

```toml
[workspace]
members = [
  "src/base",
  "src/engine",
  "src/engine/shared",
  "src/rust-bridge/test",
  "src/rust-bridge/voice",
]
resolver = "2"

[profile.dev]
panic = "abort"

[profile.release]
lto = "thin"
panic = "abort"
```

### 7.2 产出静态库的 Crate

```toml
[lib]
crate-type = ["rlib", "staticlib"]
```

仅 `ddnet-engine-shared` 和 `ddnet-voice` 需要此配置，产出：

- `libddnet_engine_shared.a` / `ddnet_engine_shared.lib`
- `libddnet_voice.a` / `ddnet_voice.lib`

### 7.3 CMake 集成要点

- Rust 目标通过 `IMPORTED STATIC` 库导入：`rust_engine_shared`、`rust_voice`
- 客户端链接：`rust_engine_shared` + `rust_voice` + Windows COM 库（ole32/oleaut32/propsys）
- 服务端链接：`rust_engine_shared`
- 头文件搜索路径：`src/rust-bridge/`（CMake `target_include_directories`）
- `rust-bridge-shared` OBJECT 库包含 cxx 生成的桥接代码

### 7.4 头文件管理

项目**不使用 cbindgen**。两种 FFI 的头文件管理方式：

- **cxx bridge**：cxx 编译时自动生成，手动复制到 `src/rust-bridge/cpp/` 和 `src/rust-bridge/engine/`
- **手写 C ABI**：C++ 侧在 `voice_rust.cpp` 中手动声明 `extern "C"` 函数原型

新增 FFI 函数时，必须同步更新 C++ 侧的 `extern "C"` 声明。

### 7.5 平台一致性

Rust target 必须与 C++ 编译器严格匹配：

- Windows: `x86_64-pc-windows-msvc`
- Linux: `x86_64-unknown-linux-gnu`
- macOS: `aarch64-apple-darwin` / `x86_64-apple-darwin`
- Android: `aarch64-linux-android` / `armv7-linux-androideabi`

## 8. 质量红线

- `cargo clippy -- -D warnings`
- `cargo test --workspace`
- `cargo fmt --check`
- FFI 函数禁止直接使用 `unwrap`/`expect`（业务逻辑内部允许，但 FFI 边界必须由 `ffi_wrap!` 保护）
- 新增 `#[repr(C)]` 结构体必须添加编译期大小断言（Rust `const _` assert + C++ `static_assert`）
- C++ 侧 `voice::VoiceSystem` 析构函数必须调用 `voice_system_destroy` 并置零句柄

### 8.1 代码修改安全规则

- **禁止使用 Shell 命令（sed/awk/pwsh -replace 等）修改代码文件** — 必须使用 `SearchReplace` 工具逐处精确替换，避免编码损坏、正则误匹配、批量误伤
- **禁止使用** **`cargo fix --allow-dirty`** — 该命令会自动修改代码但可能引入不安全的变更（如删除未使用的导入但遗漏逻辑依赖），必须手动逐个审查并使用 `SearchReplace` 修复
- **禁止使用** **`RunCommand`** **写入代码文件** — 如 `Set-Content`、`Out-File`、`echo >` 等重定向操作，会导致编码丢失（UTF-8 BOM）、换行符转换（CRLF/LF）、权限问题
- **正确的代码修改流程**：
  1. 使用 `Read` 工具读取目标文件
  2. 使用 `SearchReplace` 工具精确替换（old\_str → new\_str）
  3. 使用 `Read` 工具验证修改结果
  4. 使用 `cargo check` / `cargo clippy` 验证编译

### 8.2 文件/目录验证规范

**使用 pwsh 命令验证文件/目录存在性**（禁止仅通过 `Read` 或 `Glob` 推断）：

```powershell
# 验证文件是否存在
Test-Path "src/rust-bridge/voice/src/lib.rs"

# 验证目录是否存在（-PathType Container）
Test-Path "src/rust-bridge/voice/src" -PathType Container

# 获取目录内容（用于验证结构）
Get-ChildItem "src/rust-bridge/voice/src" -Name

# 验证后读取文件内容（必须验证存在后再 Read）
if (Test-Path "src/rust-bridge/voice/src/lib.rs") { Get-Content ... }
```

**重要**：Agent 工具 (`Read`/`Glob`/`LS`) 可能因为缓存或路径问题返回误导性结果。在关键路径操作（如创建新模块、修改 Cargo.toml）前，**必须使用 PowerShell 命令验证实际文件系统状态**。

## 9. C++ 侧封装规范

C++ 侧对 Rust 模块的封装遵循以下模式（参考 `voice_rust.h`/`voice_rust.cpp`）：

- 使用命名空间隔离（如 `namespace voice`）
- 封装类持有不透明句柄（`size_t m_Handle`），RAII 管理生命周期
- 构造函数调用 `*_create()`，析构函数调用 `*_destroy()` 并置零句柄
- 配置通过 C++ `Config` 结构体 → `toCABI()` 转换为 `VoiceConfigCABI` 再传递
- 所有方法先检查句柄有效性（`if(!m_Handle) return;`）

