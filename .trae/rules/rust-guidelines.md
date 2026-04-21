---

alwaysApply: false
description: QmClient 项目 Rust 编码规范。涵盖：工具与格式、内存与性能、错误处理、类型设计、模式匹配、模块组织、函数逻辑、文档与安全。适用于所有 Rust 模块开发，包括 ddnet-voice、ddnet-base、ddnet-engine 等。强制要求：cargo fmt 格式化、cargo clippy 无告警、库代码使用 thiserror、应用代码使用 anyhow。

---

# QmClient Rust 编码规范

本文档整合了 Rust 官方 API 指南、性能优化指南、错误处理最佳实践，并结合 QmClient 项目特点制定的全面编码规范。

## 1. 工具与格式 (Tools & Format)

### 1.1 代码格式化

**强制要求**：
- 所有代码必须通过 `cargo fmt` 格式化
- 提交前必须运行 `cargo fmt --check` 验证

**配置文件** (`rustfmt.toml`):
```toml
edition = "2021"
max_width = 100
tab_spaces = 4
use_small_heuristics = "Default"
```

### 1.2 代码检查

**强制要求**：
- 代码必须通过 `cargo clippy` 检查且无告警
- 项目初期建议开启 `#![deny(clippy::all)]`
- 推荐的 clippy 配置：

```rust
#![deny(clippy::all)]
#![warn(clippy::pedantic)]
#![allow(clippy::module_name_repetitions)]
#![allow(clippy::too_many_arguments)]
```

**检查命令**：
```bash
cargo clippy -- -D warnings -W clippy::all
```

### 1.3 命名规范

遵循 Rust 官方命名约定 [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/naming.html)：

| 类型 | 命名风格 | 示例 |
|------|----------|------|
| 类型、Trait、枚举 | `PascalCase` | `VoiceSystem`, `AudioEngine`, `Error` |
| 函数、变量、模块 | `snake_case` | `process_audio()`, `sample_rate`, `voice_system` |
| 常量 | `SCREAMING_SNAKE_CASE` | `MAX_BUFFER_SIZE`, `DEFAULT_SAMPLE_RATE` |
| 生命周期参数 | 短小写字母 | `'a`, `'b`, `'static` |

**缩写词处理**：
- **CamelCase 中**: 保持大写 `HttpRequest`, `Uuid` (而非 `HTTPRequest`, `UUID`)
- **snake_case 中**: 小写 `is_xid_start`, `to_utf8`

**转换方法命名**：
- `as_` 前缀: 廉价引用转换 (如 `as_str()`, `as_bytes()`)
- `to_` 前缀: 昂贵转换 (如 `to_string()`, `to_vec()`)
- `into_` 前缀: 消费 self 的转换 (如 `into_iter()`, `into_boxed_slice()`)

## 2. 内存与性能 (Memory & Performance)

### 2.1 避免不必要的克隆

**原则**：优先考虑所有权转移或借用，仅在必要时使用 `.clone()`。

```rust
// 推荐：借用
fn process_data(data: &str) -> usize {
    data.len()
}

// 不推荐：不必要的克隆
fn process_data(data: String) -> usize {
    data.clone().len()  // ❌ 多余的克隆
}

// 推荐：所有权转移
fn consume_data(data: Vec<u8>) -> Vec<u8> {
    data  // 直接返回，无需克隆
}
```

### 2.2 接口参数设计

**原则**：优先使用切片（`&str`, `&[T]`）而非包装类型（`String`, `Vec`）。

```rust
// 推荐：接受切片
fn find_item(items: &[Item], id: u32) -> Option<&Item> {
    items.iter().find(|item| item.id == id)
}

// 不推荐：接受 Vec
fn find_item(items: Vec<Item>, id: u32) -> Option<Item> {
    items.into_iter().find(|item| item.id == id)  // ❌ 消费所有权
}
```

### 2.3 迭代器优先

**原则**：优先使用迭代器处理集合，利用其特性减少手动边界检查。

```rust
// 推荐：迭代器链
let result: Vec<_> = data.iter()
    .filter(|x| x.is_valid())
    .map(transform)
    .collect();

// 不推荐：中间集合
let filtered: Vec<_> = data.iter().filter(|x| x.is_valid()).collect();
let result: Vec<_> = filtered.iter().map(transform).collect();  // ❌ 多余分配
```

**迭代器优势**：
- 编译器优化更好（零成本抽象）
- 减少手动边界检查
- 代码更简洁
- 消除中间分配

### 2.4 集合预分配

**原则**：已知集合大小时使用 `with_capacity` 预分配内存。

```rust
// 推荐：预分配
let mut buffer = Vec::with_capacity(1024);
let mut text = String::with_capacity(256);

// 不推荐：动态增长
let mut buffer = Vec::new();  // ❌ 可能多次重分配
for i in 0..1024 {
    buffer.push(i);
}
```

### 2.5 性能优化流程

参考 [Rust Performance Guide](https://nnethercote.github.io/perf-book/)：

1. **测量** - 建立基准并识别瓶颈
2. **分析** - 理解瓶颈存在的原因
3. **改进** - 进行针对性修改
4. **验证** - 确认优化效果

**优化检查清单**：
- ✅ 使用迭代器而非中间集合
- ✅ 考虑数据布局 (SoA vs AoS)
- ✅ Release 构建启用 LTO
- ✅ 使用 `sort_unstable` 当稳定性不需要时
- ✅ 考虑使用 Rayon 进行并行处理
- ✅ 优化后验证正确性
- ✅ 测量改进效果
- ✅ 记录优化原因

### 2.6 QmClient 性能等级

根据调用频率划分性能等级：

| 等级 | 调用频率 | 代表函数 | 要求 |
|------|----------|----------|------|
| **Hot** | 每帧调用 | `voice_process_frame`, `voice_mix_audio` | 零堆分配，< 0.1ms |
| **Warm** | 每秒数次 | `voice_set_config`, `voice_update_players` | 允许轻量分配 |
| **Cold** | 初始化 | `voice_system_create`, `voice_system_init` | 无严格限制 |

## 3. 错误处理 (Error Handling)

### 3.1 错误类型选择

参考 [Rust Error Handling Best Practices](https://github.com/apollographql/skills/blob/main/skills/rust-best-practices/references/errors.md)：

| 场景 | 推荐方案 | 说明 |
|------|----------|------|
| 库代码 | `thiserror` | 类型安全、可模式匹配、错误信息清晰 |
| 应用代码 | `anyhow` | 快速聚合、携带上下文、减少样板代码 |

### 3.2 thiserror 使用（库代码）

```rust
use thiserror::Error;

#[derive(Debug, Error)]
pub enum VoiceError {
    #[error("Failed to initialize audio device")]
    AudioInitFailed,
    
    #[error("Invalid sample rate: {0}")]
    InvalidSampleRate(u32),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("Codec error: {0}")]
    Codec(#[from] CodecError),
}
```

**优点**：
- 类型安全
- 可模式匹配
- 错误信息清晰
- 支持错误链

### 3.3 anyhow 使用（应用代码）

```rust
use anyhow::{Context, Result};

fn process_file(path: &str) -> Result<()> {
    let content = std::fs::read_to_string(path)
        .with_context(|| format!("Failed to read file: {}", path))?;
    
    let config: Config = toml::from_str(&content)
        .context("Failed to parse config")?;
    
    Ok(())
}
```

**优点**：
- 快速聚合不同错误类型
- 携带上下文信息
- 减少样板代码
- 支持错误链和回溯

### 3.4 错误传播

**原则**：使用 `?` 操作符进行错误传播，避免深层嵌套的 `match` 或 `if let`。

```rust
// 推荐：使用 ? 操作符
fn process_data(path: &str) -> Result<Data, MyError> {
    let content = std::fs::read_to_string(path)?;  // 自动传播错误
    let data = parse_data(&content)?;
    Ok(data)
}

// 不推荐：深层嵌套
fn process_data(path: &str) -> Result<Data, MyError> {
    match std::fs::read_to_string(path) {
        Ok(content) => match parse_data(&content) {
            Ok(data) => Ok(data),
            Err(e) => Err(e),
        },
        Err(e) => Err(MyError::from(e)),
    }
}
```

### 3.5 unwrap 与 expect 使用规则

**禁止使用场景**：
- **库代码**: 严禁使用 `unwrap()` 或 `expect()`
- **生产代码**: 避免可能导致 panic 的操作

**允许使用场景**：
- **测试代码**: 可以使用 `unwrap()` 简化测试
- **确定逻辑永不失败**: 且必须添加注释说明原因
- **原型开发**: 快速验证概念

```rust
// 禁止：库代码中使用 unwrap
pub fn get_value(&self) -> i32 {
    self.value.unwrap()  // ❌ 可能 panic
}

// 推荐：返回 Result
pub fn get_value(&self) -> Result<i32, MyError> {
    self.value.ok_or(MyError::NotFound)
}

// 允许：测试代码
#[test]
fn test_value() {
    let result = parse("123").unwrap();  // ✅ 测试中允许
    assert_eq!(result, 123);
}

// 允许：确定永不失败且有注释
pub fn new() -> Self {
    // SAFETY: "default" 是硬编码字符串，parse 永不失败
    Self {
        name: "default".parse().unwrap(),
    }
}
```

### 3.6 Option 与 Result 组合子

**原则**：利用组合子（`.map()`, `.and_then()`, `.ok_or()`）简化处理。

```rust
// 推荐：使用组合子
let result = option
    .ok_or(MyError::NotFound)?
    .parse::<i32>()
    .map_err(|e| MyError::ParseError(e))?;

// 不推荐：嵌套 match
let result = match option {
    Some(s) => match s.parse::<i32>() {
        Ok(n) => Ok(n),
        Err(e) => Err(MyError::ParseError(e)),
    },
    None => Err(MyError::NotFound),
};
```

### 3.7 FFI 边界错误处理

**QmClient 特有规范**：

- 所有 FFI 函数使用 `ffi_wrap!` 宏包裹
- 自动添加 `catch_unwind` 保护
- 错误映射为错误码或零值句柄
- 禁止 panic 跨越 FFI 边界

**错误码约定**：
- 负数 `i32` 表示错误
- 零值句柄表示创建失败
- 正数表示成功或有效句柄

```rust
ffi_wrap!(fn voice_system_create() -> usize {
    match VoiceSystem::new() {
        Ok(system) => {
            let handle = Box::new(VoiceSystemHandle { system });
            Box::into_raw(handle) as usize
        }
        Err(e) => {
            log::error!("Failed to create voice system: {}", e);
            0  // 零值句柄表示失败
        }
    }
});
```

## 4. 类型设计与 Trait (Type Design)

### 4.1 类型定义与 impl 块

**原则**：类型定义与对应的 `impl` 块应在同一文件中物理相邻。

```rust
// 推荐：定义与实现相邻
pub struct VoiceSystem {
    sample_rate: u32,
    channels: u8,
}

impl VoiceSystem {
    pub fn new() -> Self {
        Self {
            sample_rate: 48000,
            channels: 2,
        }
    }
}

impl Drop for VoiceSystem {
    fn drop(&mut self) {
        // 清理资源
    }
}
```

### 4.2 常用 Trait 派生

**原则**：优先为结构体派生常用 Trait：`Debug`, `Default`, `PartialEq`。

```rust
#[derive(Debug, Default, Clone, PartialEq)]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub channels: u8,
    pub buffer_size: usize,
}
```

**派生优先级**：
1. `Debug` - 调试必需
2. `Default` - 无参构造函数
3. `Clone` - 需要复制时
4. `PartialEq` - 需要比较时
5. `Copy` - 小型值类型
6. `Serialize/Deserialize` - 需要序列化时

### 4.3 构造函数约定

**原则**：
- 主构造函数命名为 `pub fn new(...) -> Self`
- 无参数构造函数应同时实现 `Default` Trait

```rust
#[derive(Debug, Default)]
pub struct VoiceConfig {
    pub volume: f32,
    pub noise_suppress: bool,
}

impl VoiceConfig {
    // 无参构造函数
    pub fn new() -> Self {
        Self::default()
    }
    
    // 带参数构造函数
    pub fn with_volume(volume: f32) -> Self {
        Self {
            volume,
            ..Self::default()
        }
    }
}

// 实现 Default trait
impl Default for VoiceConfig {
    fn default() -> Self {
        Self {
            volume: 1.0,
            noise_suppress: true,
        }
    }
}
```

### 4.4 字段可见性

**原则**：字段可见性遵循最小化原则，模块内部使用的字段优先使用 `pub(crate)`。

```rust
pub struct VoiceSystem {
    // 公开字段
    pub config: VoiceConfig,
    
    // 模块内部可见
    pub(crate) audio_engine: AudioEngine,
    
    // 私有字段
    buffer: Vec<f32>,
    is_running: bool,
}
```

### 4.5 Builder 模式

**原则**：函数参数超过 4 个时，考虑封装为 `Config` 结构体或使用 Builder 模式。

```rust
pub struct VoiceSystemBuilder {
    sample_rate: u32,
    channels: u8,
    buffer_size: usize,
    enable_noise_suppress: bool,
}

impl VoiceSystemBuilder {
    pub fn new() -> Self {
        Self {
            sample_rate: 48000,
            channels: 2,
            buffer_size: 1024,
            enable_noise_suppress: true,
        }
    }
    
    pub fn sample_rate(mut self, rate: u32) -> Self {
        self.sample_rate = rate;
        self
    }
    
    pub fn channels(mut self, channels: u8) -> Self {
        self.channels = channels;
        self
    }
    
    pub fn build(self) -> Result<VoiceSystem, VoiceError> {
        VoiceSystem::new(self)
    }
}

// 使用
let system = VoiceSystemBuilder::new()
    .sample_rate(44100)
    .channels(1)
    .build()?;
```

## 5. 模式匹配 (Pattern Matching)

### 5.1 显式处理所有分支

**原则**：显式处理所有枚举分支，避免过度依赖 `_ => ...`。

```rust
// 推荐：显式处理所有分支
match status {
    Status::Connected => handle_connected(),
    Status::Disconnected => handle_disconnected(),
    Status::Connecting => handle_connecting(),
    Status::Error => handle_error(),
}

// 不推荐：过度使用通配符
match status {
    Status::Connected => handle_connected(),
    _ => handle_other(),  // ❌ 隐藏了其他分支的处理逻辑
}
```

**例外**：当确实需要忽略某些分支时，添加注释说明：

```rust
match event {
    Event::KeyPress(key) => handle_key(key),
    Event::MouseMove(x, y) => handle_mouse(x, y),
    // 忽略其他事件类型
    _ => {}
}
```

### 5.2 Option 与 Result 组合子

**原则**：利用组合子简化 `Option` 和 `Result` 的链式处理。

```rust
// 推荐：使用组合子
let result = data
    .get(index)
    .ok_or(MyError::IndexOutOfBounds)?
    .parse::<i32>()
    .map_err(|e| MyError::ParseError(e))?;

// 不推荐：嵌套 match
let result = match data.get(index) {
    Some(s) => match s.parse::<i32>() {
        Ok(n) => Ok(n),
        Err(e) => Err(MyError::ParseError(e)),
    },
    None => Err(MyError::IndexOutOfBounds),
};
```

### 5.3 if let 与 let else

**原则**：简单分支判断使用 `if let` 或 `let else`（Rust 1.65+）。

```rust
// 推荐：使用 let else
let Some(value) = option else {
    return Err(MyError::NotFound);
};

// 推荐：使用 if let
if let Some(value) = option {
    process(value);
}

// 不推荐：单分支 match
match option {
    Some(value) => process(value),
    None => {}
}
```

## 6. 模块组织与路径引用 (Module & Pathing)

### 6.1 路径简化原则

**原则**：禁止在逻辑代码中频繁出现长路径引用（如 `a::b::c::Type`）。

```rust
// 推荐：导入后直接使用
use crate::voice::audio::AudioEngine;

fn process(engine: &AudioEngine) {
    engine.process();
}

// 不推荐：长路径引用
fn process(engine: &crate::voice::audio::AudioEngine) {
    engine.process();  // ❌ 路径过长
}
```

### 6.2 导入冲突处理

**原则**：若有同名类型，使用 `use ... as ...` 别名，或仅导入至上一级。

```rust
// 推荐：使用别名
use std::io::Result as IoResult;
use crate::error::Result as AppResult;

// 推荐：导入至上一级
use std::fmt;
fn format_value(value: &impl fmt::Display) -> fmt::Result {
    write!(f, "{}", value)
}
```

### 6.3 文件组织方式

**原则**：采用 `name.rs` + `name/` 子目录的文件组织方式，弃用 `mod.rs`。

```
voice/
├── lib.rs           # 模块入口
├── audio.rs         # audio 模块
├── audio/           # audio 子模块
│   ├── engine.rs
│   ├── capture.rs
│   └── mixer.rs
├── codec.rs
├── dsp.rs
└── network.rs
```

**lib.rs 内容**：
```rust
pub mod audio;
pub mod codec;
pub mod dsp;
pub mod network;
```

### 6.4 去 utils 化

**原则**：按功能语义归类（如 `time.rs`），而非堆叠在通用的 `utils` 模块中。

```rust
// 不推荐：utils 模块
mod utils {
    pub fn format_time() { }
    pub fn parse_config() { }
    pub fn validate_input() { }
}

// 推荐：按功能分类
mod time {
    pub fn format() { }
}

mod config {
    pub fn parse() { }
}

mod validation {
    pub fn validate() { }
}
```

## 7. 函数与逻辑 (Logic & Functions)

### 7.1 单一职责原则

**原则**：一个函数只做一件事。

```rust
// 推荐：单一职责
fn validate_config(config: &Config) -> Result<(), ConfigError> {
    if config.sample_rate == 0 {
        return Err(ConfigError::InvalidSampleRate);
    }
    if config.channels == 0 {
        return Err(ConfigError::InvalidChannels);
    }
    Ok(())
}

fn apply_config(system: &mut VoiceSystem, config: &Config) -> Result<(), VoiceError> {
    validate_config(config)?;
    system.set_sample_rate(config.sample_rate);
    system.set_channels(config.channels);
    Ok(())
}

// 不推荐：一个函数做多件事
fn validate_and_apply_config(system: &mut VoiceSystem, config: &Config) -> Result<(), Error> {
    // 验证逻辑
    if config.sample_rate == 0 { return Err(Error::InvalidSampleRate); }
    // 应用逻辑
    system.set_sample_rate(config.sample_rate);
    // ...
}
```

### 7.2 提取辅助函数

**原则**：当函数嵌套过深或逻辑分支过多时，应提取私有辅助函数。

```rust
// 推荐：提取辅助函数
pub fn process_audio_frame(&mut self, frame: &[f32]) -> Vec<f32> {
    let normalized = self.normalize(frame);
    let filtered = self.apply_filters(&normalized);
    let mixed = self.mix_channels(&filtered);
    mixed
}

fn normalize(&self, frame: &[f32]) -> Vec<f32> {
    // 归一化逻辑
}

fn apply_filters(&self, frame: &[f32]) -> Vec<f32> {
    // 滤波逻辑
}

fn mix_channels(&self, frame: &[f32]) -> Vec<f32> {
    // 混音逻辑
}
```

### 7.3 参数封装

**原则**：函数参数超过 4 个时，考虑封装为 `Config` 结构体或使用 Builder 模式。

```rust
// 不推荐：参数过多
fn create_voice_system(
    sample_rate: u32,
    channels: u8,
    buffer_size: usize,
    noise_suppress: bool,
    echo_cancel: bool,
    gain: f32,
) -> VoiceSystem { }

// 推荐：使用 Config 结构体
#[derive(Debug, Clone)]
pub struct VoiceSystemConfig {
    pub sample_rate: u32,
    pub channels: u8,
    pub buffer_size: usize,
    pub noise_suppress: bool,
    pub echo_cancel: bool,
    pub gain: f32,
}

fn create_voice_system(config: &VoiceSystemConfig) -> VoiceSystem { }
```

### 7.4 魔法值提取

**原则**：魔法值必须提取为关联常量（`impl` 块内）或模块级 `const`。

```rust
// 不推荐：魔法值
fn process(&mut self, samples: &[f32]) {
    if samples.len() > 1024 {  // ❌ 魔法值
        // ...
    }
}

// 推荐：使用常量
const MAX_BUFFER_SIZE: usize = 1024;

fn process(&mut self, samples: &[f32]) {
    if samples.len() > MAX_BUFFER_SIZE {
        // ...
    }
}

// 或使用关联常量
impl VoiceSystem {
    const MAX_BUFFER_SIZE: usize = 1024;
    const DEFAULT_SAMPLE_RATE: u32 = 48000;
    
    fn process(&mut self, samples: &[f32]) {
        if samples.len() > Self::MAX_BUFFER_SIZE {
            // ...
        }
    }
}
```

## 8. 文档与安全 (Docs & Safety)

### 8.1 文档注释规范

**原则**：所有 `pub` 成员必须编写 `///` 文档注释。

```rust
/// 音频处理系统，负责音频采集、处理和播放。
///
/// # Examples
///
/// ```
/// use voice::VoiceSystem;
///
/// let mut system = VoiceSystem::new()?;
/// system.start_capture();
/// ```
///
/// # Features
///
/// - 实时音频采集
/// - 噪声抑制
/// - 回声消除
/// - 3D 空间音频
pub struct VoiceSystem {
    // ...
}
```

### 8.2 文档结构

```rust
/// 简短描述（一行）
///
/// 详细描述（多行）
///
/// # Examples
/// ```
/// use mylib::MyType;
/// let instance = MyType::new();
/// ```
///
/// # Panics
/// 描述可能 panic 的情况
///
/// # Errors
/// 描述可能返回的错误
///
/// # Safety
/// 描述 unsafe 代码的安全性要求
pub fn my_function() {}
```

### 8.3 文档测试

**原则**：复杂逻辑需提供 `# Examples` 文档测试，确保示例代码可编译且正确。

```rust
/// 解析音频配置字符串。
///
/// # Examples
///
/// ```
/// use voice::parse_config;
///
/// let config = parse_config("48000,2,1024")?;
/// assert_eq!(config.sample_rate, 48000);
/// assert_eq!(config.channels, 2);
/// # Ok::<(), voice::VoiceError>(())
/// ```
pub fn parse_config(input: &str) -> Result<AudioConfig, VoiceError> {
    // ...
}
```

### 8.4 unsafe 代码安全规范

**原则**：使用 `unsafe` 块时，必须在上方标注 `// SAFETY:` 注释，解释其安全性。

```rust
// SAFETY: ptr 来自 Box::into_raw，保证非空且对齐正确。
// 调用者必须确保 ptr 有效且未被其他代码使用。
unsafe fn from_raw(ptr: *mut VoiceSystemHandle) -> Box<VoiceSystemHandle> {
    Box::from_raw(ptr)
}

// SAFETY: FFI 函数调用，C 侧保证返回有效指针。
unsafe fn get_native_buffer(&mut self) -> *mut f32 {
    self.native_engine.get_buffer()
}
```

### 8.5 unsafe 使用原则

- 仅在必要时使用 `unsafe`
- 将 `unsafe` 代码封装在安全 API 之下
- 最小化 `unsafe` 块的范围
- 文档化所有安全假设

```rust
// 推荐：封装 unsafe 代码
pub fn process_frame(&mut self, frame: &[f32]) -> Vec<f32> {
    // 安全封装
    self.process_frame_internal(frame)
}

fn process_frame_internal(&mut self, frame: &[f32]) -> Vec<f32> {
    // SAFETY: buffer 已正确初始化，长度与 frame 匹配
    unsafe {
        self.process_with_native(frame.as_ptr(), frame.len())
    }
}
```

## 9. 质量红线

### 9.1 必须通过的检查

```bash
# 格式化检查
cargo fmt --check

# Clippy 检查
cargo clippy -- -D warnings

# 单元测试
cargo test --workspace

# 文档生成
cargo doc --no-deps
```

### 9.2 禁止事项

- ❌ 库代码中使用 `unwrap()` 或 `expect()`
- ❌ panic 跨越 FFI 边界
- ❌ 阻塞渲染线程
- ❌ 使用 Shell 命令修改代码文件
- ❌ 使用 `cargo fix --allow-dirty`
- ❌ 在库代码中使用 `anyhow`（应使用 `thiserror`）

### 9.3 代码修改安全规则

**禁止**：
- 使用 Shell 命令（sed/awk/pwsh -replace 等）修改代码文件
- 使用 `cargo fix --allow-dirty`
- 使用 `RunCommand` 写入代码文件（Set-Content, Out-File, echo > 等）

**正确流程**：
1. 使用 `Read` 工具读取目标文件
2. 使用 `SearchReplace` 工具精确替换（old_str → new_str）
3. 使用 `Read` 工具验证修改结果
4. 使用 `cargo check` / `cargo clippy` 验证编译

## 10. 参考资源

### 官方文档
- [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/)
- [The Rust Programming Language](https://doc.rust-lang.org/book/)
- [Rust Performance Book](https://nnethercote.github.io/perf-book/)
- [Rust Reference](https://doc.rust-lang.org/reference/)

### 最佳实践
- [Rust Error Handling Patterns](https://github.com/apollographql/skills/blob/main/skills/rust-best-practices/references/errors.md)
- [Microsoft Rust Guidelines](https://microsoft.github.io/rust-guidelines/)
- [Rust Design Patterns](https://rust-unofficial.github.io/patterns/)

### 工具与库
- [thiserror](https://docs.rs/thiserror) - 库错误处理
- [anyhow](https://docs.rs/anyhow) - 应用错误处理
- [clippy](https://github.com/rust-lang/rust-clippy) - 代码检查
- [rustfmt](https://github.com/rust-lang/rustfmt) - 代码格式化

---

**文档版本**: 1.0  
**最后更新**: 2026-04-20  
**适用范围**: QmClient 所有 Rust 模块（ddnet-voice, ddnet-base, ddnet-engine 等）
