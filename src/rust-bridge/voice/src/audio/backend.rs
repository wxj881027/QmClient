//! 音频后端抽象层
//!
//! 提供统一的音频接口，支持多种后端实现
//! 参考 C++ voice_core.cpp 的 SDL 音频实现

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

    #[error("Initialization error: {0}")]
    Initialization(String),

    #[error("Unsupported operation: {0}")]
    Unsupported(String),
}

/// 音频设备信息
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    /// 设备名称
    pub name: String,
    /// 是否为输入设备
    pub is_input: bool,
    /// 是否为默认设备
    pub is_default: bool,
    /// 采样率
    pub sample_rate: Option<u32>,
    /// 声道数
    pub channels: Option<u16>,
}

/// 音频后端 trait
pub trait AudioBackend: Send + Sync {
    /// 获取后端类型
    fn backend_type(&self) -> AudioBackendType;

    /// 列出可用输入设备
    fn list_input_devices(&self) -> Result<Vec<String>, AudioError>;

    /// 列出可用输出设备
    fn list_output_devices(&self) -> Result<Vec<String>, AudioError>;

    /// 列出输入设备详细信息
    fn list_input_devices_info(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        let names = self.list_input_devices()?;
        Ok(names
            .into_iter()
            .enumerate()
            .map(|(i, name)| DeviceInfo {
                name,
                is_input: true,
                is_default: i == 0,
                sample_rate: None,
                channels: None,
            })
            .collect())
    }

    /// 列出输出设备详细信息
    fn list_output_devices_info(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        let names = self.list_output_devices()?;
        Ok(names
            .into_iter()
            .enumerate()
            .map(|(i, name)| DeviceInfo {
                name,
                is_input: false,
                is_default: i == 0,
                sample_rate: None,
                channels: None,
            })
            .collect())
    }
}

/// 创建音频后端
pub fn create_backend(backend_type: AudioBackendType) -> Result<Box<dyn AudioBackend>, AudioError> {
    match backend_type {
        AudioBackendType::Auto | AudioBackendType::Cpal => {
            #[cfg(feature = "cpal-backend")]
            {
                Ok(Box::new(cpal_backend::CpalBackend::new()?))
            }
            #[cfg(not(feature = "cpal-backend"))]
            {
                Err(AudioError::BackendNotAvailable(
                    "cpal backend not enabled".to_string(),
                ))
            }
        }
        AudioBackendType::Sdl => {
            #[cfg(feature = "sdl-backend")]
            {
                Err(AudioError::BackendNotAvailable(
                    "SDL backend not yet implemented as trait".to_string(),
                ))
            }
            #[cfg(not(feature = "sdl-backend"))]
            {
                Err(AudioError::BackendNotAvailable(
                    "SDL backend not enabled".to_string(),
                ))
            }
        }
    }
}

// cpal 后端实现
#[cfg(feature = "cpal-backend")]
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

        /// 获取默认输入设备
        pub fn default_input_device(&self) -> Option<cpal::Device> {
            self.host.default_input_device()
        }

        /// 获取默认输出设备
        pub fn default_output_device(&self) -> Option<cpal::Device> {
            self.host.default_output_device()
        }

        /// 根据名称查找输入设备
        pub fn find_input_device(&self, name: &str) -> Option<cpal::Device> {
            self.host
                .input_devices()
                .ok()?
                .find(|d| d.name().map(|n| n == name).unwrap_or(false))
        }

        /// 根据名称查找输出设备
        pub fn find_output_device(&self, name: &str) -> Option<cpal::Device> {
            self.host
                .output_devices()
                .ok()?
                .find(|d| d.name().map(|n| n == name).unwrap_or(false))
        }
    }

    impl AudioBackend for CpalBackend {
        fn backend_type(&self) -> AudioBackendType {
            AudioBackendType::Cpal
        }

        fn list_input_devices(&self) -> Result<Vec<String>, AudioError> {
            let devices = self
                .host
                .input_devices()
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
            let devices = self
                .host
                .output_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?;
            let mut names = Vec::new();
            for device in devices {
                if let Ok(name) = device.name() {
                    names.push(name);
                }
            }
            Ok(names)
        }

        fn list_input_devices_info(&self) -> Result<Vec<DeviceInfo>, AudioError> {
            let devices = self
                .host
                .input_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?;
            let mut infos = Vec::new();
            let default_name = self.host.default_input_device().and_then(|d| d.name().ok());

            for device in devices {
                let name = device.name().unwrap_or_default();
                let config = device.default_input_config().ok();

                infos.push(DeviceInfo {
                    name: name.clone(),
                    is_input: true,
                    is_default: default_name.as_ref().map(|n| n == &name).unwrap_or(false),
                    sample_rate: config.as_ref().map(|c| c.sample_rate().0),
                    channels: config.as_ref().map(|c| c.channels()),
                });
            }
            Ok(infos)
        }

        fn list_output_devices_info(&self) -> Result<Vec<DeviceInfo>, AudioError> {
            let devices = self
                .host
                .output_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?;
            let mut infos = Vec::new();
            let default_name = self
                .host
                .default_output_device()
                .and_then(|d| d.name().ok());

            for device in devices {
                let name = device.name().unwrap_or_default();
                let config = device.default_output_config().ok();

                infos.push(DeviceInfo {
                    name: name.clone(),
                    is_input: false,
                    is_default: default_name.as_ref().map(|n| n == &name).unwrap_or(false),
                    sample_rate: config.as_ref().map(|c| c.sample_rate().0),
                    channels: config.as_ref().map(|c| c.channels()),
                });
            }
            Ok(infos)
        }
    }
}

// 无 cpal 功能时的存根实现
#[cfg(not(feature = "cpal-backend"))]
pub mod cpal_backend {
    use super::*;

    pub struct CpalBackend;

    impl CpalBackend {
        pub fn new() -> Result<Self, AudioError> {
            Err(AudioError::BackendNotAvailable(
                "cpal backend not enabled".to_string(),
            ))
        }
    }
}

/// 检查后端是否可用
pub fn is_backend_available(backend_type: AudioBackendType) -> bool {
    match backend_type {
        AudioBackendType::Auto | AudioBackendType::Cpal => {
            #[cfg(feature = "cpal-backend")]
            {
                true
            }
            #[cfg(not(feature = "cpal-backend"))]
            {
                false
            }
        }
        AudioBackendType::Sdl => {
            #[cfg(feature = "sdl-backend")]
            {
                true
            }
            #[cfg(not(feature = "sdl-backend"))]
            {
                false
            }
        }
    }
}

/// 获取可用的后端列表
pub fn available_backends() -> Vec<AudioBackendType> {
    let mut backends = Vec::new();

    if is_backend_available(AudioBackendType::Cpal) {
        backends.push(AudioBackendType::Cpal);
    }
    if is_backend_available(AudioBackendType::Sdl) {
        backends.push(AudioBackendType::Sdl);
    }

    backends
}

/// 获取默认后端
pub fn default_backend() -> AudioBackendType {
    if is_backend_available(AudioBackendType::Cpal) {
        AudioBackendType::Cpal
    } else if is_backend_available(AudioBackendType::Sdl) {
        AudioBackendType::Sdl
    } else {
        AudioBackendType::Auto
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_backend_type_default() {
        let backend_type = AudioBackendType::default();
        assert_eq!(backend_type, AudioBackendType::Auto);
    }

    #[test]
    fn test_available_backends() {
        let backends = available_backends();
        println!("Available backends: {:?}", backends);

        #[cfg(feature = "cpal-backend")]
        assert!(backends.contains(&AudioBackendType::Cpal));
    }

    #[test]
    fn test_default_backend() {
        let backend = default_backend();
        println!("Default backend: {:?}", backend);
    }

    #[cfg(feature = "cpal-backend")]
    #[test]
    fn test_cpal_backend_creation() {
        let backend = cpal_backend::CpalBackend::new();
        assert!(backend.is_ok());
    }

    #[cfg(feature = "cpal-backend")]
    #[test]
    fn test_list_devices() {
        let backend = create_backend(AudioBackendType::Cpal).unwrap();

        let input_devices = backend.list_input_devices().unwrap();
        println!("Input devices: {:?}", input_devices);

        let output_devices = backend.list_output_devices().unwrap();
        println!("Output devices: {:?}", output_devices);
    }

    #[cfg(feature = "cpal-backend")]
    #[test]
    fn test_list_devices_info() {
        let backend = create_backend(AudioBackendType::Cpal).unwrap();

        let input_infos = backend.list_input_devices_info().unwrap();
        for info in &input_infos {
            println!(
                "Input: {} (default: {}, rate: {:?}, channels: {:?})",
                info.name, info.is_default, info.sample_rate, info.channels
            );
        }

        let output_infos = backend.list_output_devices_info().unwrap();
        for info in &output_infos {
            println!(
                "Output: {} (default: {}, rate: {:?}, channels: {:?})",
                info.name, info.is_default, info.sample_rate, info.channels
            );
        }
    }
}
