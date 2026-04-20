//! 音频 I/O 模块
//!
//! 双后端策略：
//! - cpal: 纯 Rust 跨平台音频（首选）
//! - sdl: SDL2 音频后端（兼容性）

pub mod backend;
pub mod capture;
pub mod capture_queue;
pub mod engine;
pub mod mixer;
pub mod playback;
pub mod sdl_backend;

pub use backend::{AudioBackend, AudioBackendType};
pub use capture::AudioCapture;
pub use capture_queue::{CaptureFrame, CaptureQueue};
pub use engine::{AudioEngine, AudioFrame, EngineConfig, EngineState, FRAME_SAMPLES, SAMPLE_RATE};
pub use mixer::AudioMixer;
pub use playback::AudioPlayback;

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
