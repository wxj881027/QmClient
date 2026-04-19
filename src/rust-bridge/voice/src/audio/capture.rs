//! 音频采集模块
//!
//! 统一的音频采集接口，支持多种后端

use super::backend::{AudioBackendType, AudioError, create_backend};

/// 音频采集器
pub struct AudioCapture {
    _backend_type: AudioBackendType,
}

impl AudioCapture {
    /// 使用指定后端创建采集器
    pub fn new(backend_type: AudioBackendType, _device_name: Option<&str>) -> Result<Self, AudioError> {
        let _backend = create_backend(backend_type)?;
        Ok(Self { _backend_type: backend_type })
    }
    
    /// 使用默认后端 (cpal) 创建采集器
    pub fn default_cpal(device_name: Option<&str>) -> Result<Self, AudioError> {
        Self::new(AudioBackendType::Cpal, device_name)
    }
    
    /// 列出可用输入设备
    pub fn list_devices(backend_type: AudioBackendType) -> Result<Vec<String>, AudioError> {
        let backend = create_backend(backend_type)?;
        backend.list_input_devices()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list_devices() {
        let devices = AudioCapture::list_devices(AudioBackendType::Cpal);
        println!("Input devices: {:?}", devices);
    }
}
