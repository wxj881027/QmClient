//! 音频 I/O 模块
//!
//! 使用 cpal 实现跨平台音频采集和播放

pub mod capture;
pub mod playback;
pub mod mixer;

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

/// 音频后端类型
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioBackend {
    Auto,
    Cpal,
}

impl Default for AudioBackend {
    fn default() -> Self {
        AudioBackend::Auto
    }
}
