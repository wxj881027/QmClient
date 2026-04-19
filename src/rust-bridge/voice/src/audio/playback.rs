//! 音频播放模块
//!
//! 使用 cpal 实现音频输出

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use crossbeam::channel::{Sender, Receiver, bounded};
use std::sync::Arc;
use parking_lot::Mutex;

/// 音频播放错误
#[derive(Debug, thiserror::Error)]
pub enum PlaybackError {
    #[error("Device not available")]
    DeviceNotAvailable,
    #[error("Build stream error: {0}")]
    BuildStream(String),
    #[error("Default format error: {0}")]
    DefaultFormat(String),
}

/// 音频播放器
pub struct AudioPlayback {
    stream: Option<cpal::Stream>,
    sample_tx: Sender<Vec<i16>>,
    _device_name: Option<String>,
}

impl AudioPlayback {
    /// 创建音频播放器
    pub fn new(device_name: Option<&str>) -> Result<Self, PlaybackError> {
        let host = cpal::default_host();

        let device = if let Some(name) = device_name {
            host.output_devices()
                .map_err(|e| PlaybackError::BuildStream(e.to_string()))?
                .find(|d| d.name().map(|n| n == name).unwrap_or(false))
                .ok_or(PlaybackError::DeviceNotAvailable)?
        } else {
            host.default_output_device()
                .ok_or(PlaybackError::DeviceNotAvailable)?
        };

        let config = device
            .default_output_config()
            .map_err(|e| PlaybackError::DefaultFormat(e.to_string()))?;

        let (tx, rx) = bounded::<Vec<i16>>(16);
        let rx = Arc::new(Mutex::new(rx));

        // 配置：48kHz, 单声道或立体声, 16-bit
        let channels = config.channels() as usize;
        let rx_clone = rx.clone();

        let stream = match config.sample_format() {
            cpal::SampleFormat::I16 => device.build_output_stream(
                &config.into(),
                move |data: &mut [i16], _: &_| {
                    Self::fill_buffer_i16(data, &rx_clone, channels);
                },
                |err| log::error!("Audio playback error: {}", err),
                None,
            ),
            cpal::SampleFormat::F32 => device.build_output_stream(
                &config.into(),
                move |data: &mut [f32], _: &_| {
                    Self::fill_buffer_f32(data, &rx_clone, channels);
                },
                |err| log::error!("Audio playback error: {}", err),
                None,
            ),
            _ => {
                return Err(PlaybackError::BuildStream(
                    "Unsupported sample format".to_string(),
                ))
            }
        }
        .map_err(|e| PlaybackError::BuildStream(e.to_string()))?;

        Ok(Self {
            stream: Some(stream),
            sample_tx: tx,
            _device_name: device_name.map(|s| s.to_string()),
        })
    }

    /// 开始播放
    pub fn start(&self) -> Result<(), PlaybackError> {
        if let Some(stream) = &self.stream {
            stream
                .play()
                .map_err(|e| PlaybackError::BuildStream(e.to_string()))?;
        }
        Ok(())
    }

    /// 停止播放
    pub fn stop(&self) {
        if let Some(stream) = &self.stream {
            let _ = stream.pause();
        }
    }

    /// 获取样本发送器
    pub fn sample_sender(&self) -> Sender<Vec<i16>> {
        self.sample_tx.clone()
    }

    /// 发送样本播放
    pub fn play_samples(&self, samples: Vec<i16>) -> Result<(), crossbeam::channel::TrySendError<Vec<i16>>> {
        self.sample_tx.try_send(samples)
    }

    /// 列出可用输出设备
    pub fn list_devices() -> Result<Vec<String>, PlaybackError> {
        let host = cpal::default_host();
        let devices = host
            .output_devices()
            .map_err(|e| PlaybackError::BuildStream(e.to_string()))?;

        let mut names = Vec::new();
        for device in devices {
            if let Ok(name) = device.name() {
                names.push(name);
            }
        }
        Ok(names)
    }

    fn fill_buffer_i16(data: &mut [i16], rx: &Arc<Mutex<Receiver<Vec<i16>>>>, channels: usize) {
        let rx = rx.lock();
        let mut sample_idx = 0;

        while sample_idx < data.len() {
            match rx.try_recv() {
                Ok(samples) => {
                    for sample in samples {
                        if sample_idx >= data.len() {
                            break;
                        }
                        // 写入所有声道
                        for ch in 0..channels {
                            if sample_idx + ch < data.len() {
                                data[sample_idx + ch] = sample;
                            }
                        }
                        sample_idx += channels;
                    }
                }
                Err(_) => {
                    // 没有更多数据，填充静音
                    for i in sample_idx..data.len() {
                        data[i] = 0;
                    }
                    break;
                }
            }
        }
    }

    fn fill_buffer_f32(data: &mut [f32], rx: &Arc<Mutex<Receiver<Vec<i16>>>>, channels: usize) {
        let rx = rx.lock();
        let mut sample_idx = 0;

        while sample_idx < data.len() {
            match rx.try_recv() {
                Ok(samples) => {
                    for sample in samples {
                        if sample_idx >= data.len() {
                            break;
                        }
                        let f32_sample = sample as f32 / 32768.0;
                        for ch in 0..channels {
                            if sample_idx + ch < data.len() {
                                data[sample_idx + ch] = f32_sample;
                            }
                        }
                        sample_idx += channels;
                    }
                }
                Err(_) => {
                    for i in sample_idx..data.len() {
                        data[i] = 0.0;
                    }
                    break;
                }
            }
        }
    }
}

impl Drop for AudioPlayback {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list_devices() {
        let devices = AudioPlayback::list_devices();
        println!("Output devices: {:?}", devices);
    }
}
