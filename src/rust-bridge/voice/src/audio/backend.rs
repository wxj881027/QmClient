//! 音频后端抽象层
//!
//! 提供统一的音频接口，支持多种后端实现

use thiserror::Error;

/// 音频后端类型
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum AudioBackendType {
    #[default]
    Auto,
    Cpal,
    Sdl,
}

/// 音频错误
#[derive(Debug, Error)]
pub enum AudioError {
    #[error("Device not available: {0}")]
    DeviceNotAvailable(String),
    #[error("Backend not available: {0}")]
    BackendNotAvailable(String),
    #[error("Stream error: {0}")]
    StreamError(String),
    #[error("Format error: {0}")]
    FormatError(String),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

/// 音频样本帧
pub type AudioFrame = Vec<i16>;

/// 音频后端 trait
pub trait AudioBackend: Send + Sync {
    /// 获取后端类型
    fn backend_type(&self) -> AudioBackendType;
    
    /// 列出可用输入设备
    fn list_input_devices(&self) -> Result<Vec<String>, AudioError>;
    
    /// 列出可用输出设备
    fn list_output_devices(&self) -> Result<Vec<String>, AudioError>;
}

/// 创建音频后端
pub fn create_backend(backend_type: AudioBackendType) -> Result<Box<dyn AudioBackend>, AudioError> {
    match backend_type {
        AudioBackendType::Auto | AudioBackendType::Cpal => {
            Ok(Box::new(crate::audio::backend::cpal_backend::CpalBackend::new()?))
        }
        AudioBackendType::Sdl => {
            Err(AudioError::BackendNotAvailable("SDL backend not yet implemented".to_string()))
        }
    }
}

// cpal 后端实现
pub mod cpal_backend {
    use super::*;
    use cpal::traits::{DeviceTrait, HostTrait};
    
    /// cpal 后端
    pub struct CpalBackend {
        host: cpal::Host,
    }
    
    impl CpalBackend {
        pub fn new() -> Result<Self, AudioError> {
            let host = cpal::default_host();
            Ok(Self { host })
        }
    }
    
    impl AudioBackend for CpalBackend {
        fn backend_type(&self) -> AudioBackendType {
            AudioBackendType::Cpal
        }
        
        fn list_input_devices(&self) -> Result<Vec<String>, AudioError> {
            let devices = self.host.input_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?;
            let mut names = Vec::new();
            for device in devices {
                if let Ok(name) = device.name() {
                    names.push(name);
                }
            }
            Ok(names)
        }
        
        fn list_output_devices(&self) -> Result<Vec<String>, AudioError> {
            let devices = self.host.output_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?;
            let mut names = Vec::new();
            for device in devices {
                if let Ok(name) = device.name() {
                    names.push(name);
                }
            }
            Ok(names)
        }
    }
}
