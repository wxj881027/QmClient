//! 音频 I/O 模块
//!
//! 双后端策略：
//! - cpal: 纯 Rust 跨平台音频（首选）
//! - sdl: 通过 C++ 桥接的 SDL 音频（备选）

pub mod backend;
pub mod capture;
pub mod playback;
pub mod mixer;

pub use backend::{AudioBackend, AudioBackendType};
pub use capture::AudioCapture;
pub use playback::AudioPlayback;
pub use mixer::AudioMixer;

/// 音频配置
#[derive(Debug, Clone)]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub channels: u16,
    pub frame_size: usize,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: 48000,
            channels: 1,
            frame_size: 960, // 20ms @ 48kHz
        }
    }
}
