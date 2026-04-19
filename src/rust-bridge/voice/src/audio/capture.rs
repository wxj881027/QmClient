//! 音频采集模块
//!
//! 使用 cpal 实现音频输入

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use crossbeam::channel::{Sender, Receiver, bounded};
use std::sync::Arc;

/// 音频采集错误
#[derive(Debug, thiserror::Error)]
pub enum CaptureError {
    #[error("Device not available")]
    DeviceNotAvailable,
    #[error("Build stream error: {0}")]
    BuildStream(String),
    #[error("Default format error: {0}")]
    DefaultFormat(String),
}

/// 音频采集器
pub struct AudioCapture {
    stream: Option<cpal::Stream>,
    sample_tx: Sender<Vec<i16>>,
    sample_rx: Receiver<Vec<i16>>,
    device_name: Option<String>,
}

impl AudioCapture {
    /// 创建音频采集器
    pub fn new(device_name: Option<&str>) -> Result<Self, CaptureError> {
        let host = cpal::default_host();
        
        let device = if let Some(name) = device_name {
            host.input_devices()
                .map_err(|e| CaptureError::BuildStream(e.to_string()))?
                .find(|d| d.name().map(|n| n == name).unwrap_or(false))
                .ok_or(CaptureError::DeviceNotAvailable)?
        } else {
            host.default_input_device()
                .ok_or(CaptureError::DeviceNotAvailable)?
        };

        let config = device
            .default_input_config()
            .map_err(|e| CaptureError::DefaultFormat(e.to_string()))?;

        let (tx, rx) = bounded::<Vec<i16>>(8);
        let sample_tx_clone = tx.clone();

        // 配置：48kHz, 单声道, 16-bit
        let stream = match config.sample_format() {
            cpal::SampleFormat::I16 => device.build_input_stream(
                &config.into(),
                move |data: &[i16], _: &_| {
                    let _ = sample_tx_clone.try_send(data.to_vec());
                },
                |err| log::error!("Audio capture error: {}", err),
                None,
            ),
            cpal::SampleFormat::F32 => device.build_input_stream(
                &config.into(),
                move |data: &[f32], _: &_| {
                    let samples: Vec<i16> = data.iter().map(|&s| (s * 32767.0) as i16).collect();
                    let _ = sample_tx_clone.try_send(samples);
                },
                |err| log::error!("Audio capture error: {}", err),
                None,
            ),
            _ => {
                return Err(CaptureError::BuildStream(
                    "Unsupported sample format".to_string(),
                ))
            }
        }
        .map_err(|e| CaptureError::BuildStream(e.to_string()))?;

        Ok(Self {
            stream: Some(stream),
            sample_tx: tx,
            sample_rx: rx,
            device_name: device_name.map(|s| s.to_string()),
        })
    }

    /// 开始采集
    pub fn start(&self) -> Result<(), CaptureError> {
        if let Some(stream) = &self.stream {
            stream
                .play()
                .map_err(|e| CaptureError::BuildStream(e.to_string()))?;
        }
        Ok(())
    }

    /// 停止采集
    pub fn stop(&self) {
        if let Some(stream) = &self.stream {
            let _ = stream.pause();
        }
    }

    /// 获取样本接收器
    pub fn samples(&self) -> Receiver<Vec<i16>> {
        self.sample_rx.clone()
    }

    /// 获取设备名称
    pub fn device_name(&self) -> Option<&str> {
        self.device_name.as_deref()
    }

    /// 列出可用输入设备
    pub fn list_devices() -> Result<Vec<String>, CaptureError> {
        let host = cpal::default_host();
        let devices = host
            .input_devices()
            .map_err(|e| CaptureError::BuildStream(e.to_string()))?;

        let mut names = Vec::new();
        for device in devices {
            if let Ok(name) = device.name() {
                names.push(name);
            }
        }
        Ok(names)
    }
}

impl Drop for AudioCapture {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list_devices() {
        let devices = AudioCapture::list_devices();
        // 可能成功或失败，取决于系统
        println!("Input devices: {:?}", devices);
    }

    // 注意：下面的测试需要音频设备
    // #[test]
    // fn test_capture() {
    //     let capture = AudioCapture::new(None).unwrap();
    //     capture.start().unwrap();
    //     
    //     let rx = capture.samples();
    //     let samples = rx.recv().unwrap();
    //     
    //     assert!(!samples.is_empty());
    // }
}
