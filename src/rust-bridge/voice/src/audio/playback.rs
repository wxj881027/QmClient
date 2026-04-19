//! 音频播放模块
//!
//! 统一的音频播放接口，支持多种后端

use super::backend::{AudioBackendType, AudioError, create_backend};

/// 音频播放器
pub struct AudioPlayback {
    _backend_type: AudioBackendType,
}

impl AudioPlayback {
    /// 使用指定后端创建播放器
    pub fn new(backend_type: AudioBackendType, _device_name: Option<&str>) -> Result<Self, AudioError> {
        let _backend = create_backend(backend_type)?;
        Ok(Self { _backend_type: backend_type })
    }
    
    /// 使用默认后端 (cpal) 创建播放器
    pub fn default_cpal(device_name: Option<&str>) -> Result<Self, AudioError> {
        Self::new(AudioBackendType::Cpal, device_name)
    }
    
    /// 列出可用输出设备
    pub fn list_devices(backend_type: AudioBackendType) -> Result<Vec<String>, AudioError> {
        let backend = create_backend(backend_type)?;
        backend.list_output_devices()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list_devices() {
        let devices = AudioPlayback::list_devices(AudioBackendType::Cpal);
        println!("Output devices: {:?}", devices);
    }
}
