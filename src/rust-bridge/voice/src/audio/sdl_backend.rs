//! SDL 音频后端
//!
//! 使用 SDL2 进行音频输入/输出
//! 与原有 C++ 实现的 SDL 音频回调对应

#[cfg(feature = "sdl-backend")]
use sdl2::audio::{AudioCallback, AudioDevice, AudioSpecDesired};
#[cfg(feature = "sdl-backend")]
use sdl2::AudioSubsystem;

use crate::codec::opus::FRAME_SAMPLES;

/// 音频设备错误
#[derive(Debug, thiserror::Error)]
pub enum AudioError {
    #[error("SDL error: {0}")]
    Sdl(String),

    #[error("Device not available: {0}")]
    DeviceNotAvailable(String),

    #[error("SDL backend not enabled")]
    NotEnabled,
}

/// 音频设备信息
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub name: String,
    pub is_capture: bool,
    pub is_default: bool,
}

/// SDL 音频输出回调
#[cfg(feature = "sdl-backend")]
pub struct SdlOutputCallback {
    /// 混合后的音频数据接收器
    audio_rx: crossbeam::channel::Receiver<[i16; FRAME_SAMPLES]>,
    /// 输出声道数
    channels: usize,
}

#[cfg(feature = "sdl-backend")]
impl AudioCallback for SdlOutputCallback {
    type Channel = i16;

    fn callback(&mut self, out: &mut [i16]) {
        // 尝试获取音频数据
        if let Ok(frame) = self.audio_rx.try_recv() {
            if self.channels == 1 {
                // 单声道输出
                let len = out.len().min(FRAME_SAMPLES);
                out[..len].copy_from_slice(&frame[..len]);
            } else if self.channels == 2 {
                // 立体声输出（复制到两个声道）
                for (i, &sample) in frame.iter().enumerate() {
                    if i * 2 + 1 < out.len() {
                        out[i * 2] = sample;
                        out[i * 2 + 1] = sample;
                    }
                }
            }
        } else {
            // 无数据时输出静音
            out.fill(0);
        }
    }
}

/// SDL 音频输入回调
#[cfg(feature = "sdl-backend")]
pub struct SdlCaptureCallback {
    /// 音频数据发送器
    audio_tx: crossbeam::channel::Sender<[i16; FRAME_SAMPLES]>,
    /// 输入声道数
    channels: usize,
    /// 缓冲区
    buffer: Vec<i16>,
}

#[cfg(feature = "sdl-backend")]
impl AudioCallback for SdlCaptureCallback {
    type Channel = i16;

    fn callback(&mut self, input: &mut [i16]) {
        // 如果是立体声，转换为单声道
        if self.channels == 2 {
            for chunk in input.chunks(2) {
                let mono = if chunk.len() == 2 {
                    ((chunk[0] as i32 + chunk[1] as i32) / 2) as i16
                } else {
                    chunk[0]
                };
                self.buffer.push(mono);
            }
        } else {
            self.buffer.extend_from_slice(input);
        }

        // 发送完整的帧
        while self.buffer.len() >= FRAME_SAMPLES {
            let mut frame = [0i16; FRAME_SAMPLES];
            frame.copy_from_slice(&self.buffer[..FRAME_SAMPLES]);
            self.buffer.drain(..FRAME_SAMPLES);

            // 如果发送失败（接收端已关闭），忽略
            let _ = self.audio_tx.try_send(frame);
        }
    }
}

/// SDL 音频后端
#[cfg(feature = "sdl-backend")]
pub struct SdlAudioBackend {
    audio_subsystem: AudioSubsystem,
    output_device: Option<AudioDevice<SdlOutputCallback>>,
    capture_device: Option<AudioDevice<SdlCaptureCallback>>,
    output_tx: Option<crossbeam::channel::Sender<[i16; FRAME_SAMPLES]>>,
    capture_rx: Option<crossbeam::channel::Receiver<[i16; FRAME_SAMPLES]>>,
    output_channels: usize,
    capture_channels: usize,
}

#[cfg(feature = "sdl-backend")]
impl SdlAudioBackend {
    /// 创建新的 SDL 音频后端
    pub fn new(sdl_context: &sdl2::Sdl) -> Result<Self, AudioError> {
        let audio_subsystem = sdl_context
            .audio()
            .map_err(|e| AudioError::Sdl(e.to_string()))?;

        Ok(Self {
            audio_subsystem,
            output_device: None,
            capture_device: None,
            output_tx: None,
            capture_rx: None,
            output_channels: 1,
            capture_channels: 1,
        })
    }

    /// 枚举输出设备
    pub fn enumerate_output_devices(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        let mut devices = Vec::new();

        let num_devices = self
            .audio_subsystem
            .num_audio_playback_devices()
            .unwrap_or(0);

        for i in 0..num_devices {
            match self.audio_subsystem.audio_playback_device_name(i) {
                Ok(name) => {
                    devices.push(DeviceInfo {
                        name,
                        is_capture: false,
                        is_default: i == 0,
                    });
                }
                Err(_) => continue,
            }
        }

        Ok(devices)
    }

    /// 枚举捕获设备
    pub fn enumerate_capture_devices(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        let mut devices = Vec::new();

        let num_devices = self
            .audio_subsystem
            .num_audio_capture_devices()
            .unwrap_or(0);

        for i in 0..num_devices {
            match self.audio_subsystem.audio_capture_device_name(i) {
                Ok(name) => {
                    devices.push(DeviceInfo {
                        name,
                        is_capture: true,
                        is_default: i == 0,
                    });
                }
                Err(_) => continue,
            }
        }

        Ok(devices)
    }

    /// 打开输出设备
    pub fn open_output(
        &mut self,
        device_name: Option<&str>,
        channels: usize,
    ) -> Result<(), AudioError> {
        let desired_spec = AudioSpecDesired {
            freq: Some(SAMPLE_RATE as i32),
            channels: Some(channels as u8),
            samples: Some(FRAME_SAMPLES as u16),
        };

        let (output_tx, output_rx) = crossbeam::channel::bounded(16);

        let device = self
            .audio_subsystem
            .open_playback(device_name, &desired_spec, |spec| {
                self.output_channels = spec.channels as usize;
                SdlOutputCallback {
                    audio_rx: output_rx.clone(),
                    channels: spec.channels as usize,
                }
            })
            .map_err(|e| AudioError::Sdl(e.to_string()))?;

        device.resume();
        self.output_device = Some(device);
        self.output_tx = Some(output_tx);

        Ok(())
    }

    /// 打开捕获设备
    pub fn open_capture(
        &mut self,
        device_name: Option<&str>,
        channels: usize,
    ) -> Result<(), AudioError> {
        let desired_spec = AudioSpecDesired {
            freq: Some(SAMPLE_RATE as i32),
            channels: Some(channels as u8),
            samples: Some(FRAME_SAMPLES as u16),
        };

        let (capture_tx, capture_rx) = crossbeam::channel::bounded(16);

        let device = self
            .audio_subsystem
            .open_capture(device_name, &desired_spec, |spec| {
                self.capture_channels = spec.channels as usize;
                SdlCaptureCallback {
                    audio_tx: capture_tx.clone(),
                    channels: spec.channels as usize,
                    buffer: Vec::new(),
                }
            })
            .map_err(|e| AudioError::Sdl(e.to_string()))?;

        device.resume();
        self.capture_device = Some(device);
        self.capture_rx = Some(capture_rx);

        Ok(())
    }

    /// 发送音频帧到输出设备
    pub fn send_frame(&self, frame: &[i16; FRAME_SAMPLES]) -> Result<(), AudioError> {
        if let Some(tx) = &self.output_tx {
            tx.try_send(*frame)
                .map_err(|_| AudioError::DeviceNotAvailable("Output buffer full".into()))?;
        }
        Ok(())
    }

    /// 从捕获设备接收音频帧
    pub fn recv_frame(&self) -> Option<[i16; FRAME_SAMPLES]> {
        if let Some(rx) = &self.capture_rx {
            rx.try_recv().ok()
        } else {
            None
        }
    }

    /// 关闭输出设备
    pub fn close_output(&mut self) {
        self.output_device = None;
        self.output_tx = None;
    }

    /// 关闭捕获设备
    pub fn close_capture(&mut self) {
        self.capture_device = None;
        self.capture_rx = None;
    }

    /// 获取输出声道数
    pub fn output_channels(&self) -> usize {
        self.output_channels
    }

    /// 获取捕获声道数
    pub fn capture_channels(&self) -> usize {
        self.capture_channels
    }
}

// 无 SDL 功能时的存根实现
#[cfg(not(feature = "sdl-backend"))]
pub struct SdlAudioBackend;

#[cfg(not(feature = "sdl-backend"))]
impl SdlAudioBackend {
    /// 创建新的 SDL 音频后端（需要启用 sdl-backend feature）
    pub fn new() -> Result<Self, AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn enumerate_output_devices(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn enumerate_capture_devices(&self) -> Result<Vec<DeviceInfo>, AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn open_output(
        &mut self,
        _device_name: Option<&str>,
        _channels: usize,
    ) -> Result<(), AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn open_capture(
        &mut self,
        _device_name: Option<&str>,
        _channels: usize,
    ) -> Result<(), AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn send_frame(&self, _frame: &[i16; FRAME_SAMPLES]) -> Result<(), AudioError> {
        Err(AudioError::NotEnabled)
    }

    pub fn recv_frame(&self) -> Option<[i16; FRAME_SAMPLES]> {
        None
    }

    pub fn close_output(&mut self) {}

    pub fn close_capture(&mut self) {}

    pub fn output_channels(&self) -> usize {
        1
    }

    pub fn capture_channels(&self) -> usize {
        1
    }
}

#[cfg(not(feature = "sdl-backend"))]
impl Default for SdlAudioBackend {
    fn default() -> Self {
        Self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_device_info() {
        let info = DeviceInfo {
            name: "Default Device".to_string(),
            is_capture: false,
            is_default: true,
        };

        assert_eq!(info.name, "Default Device");
        assert!(!info.is_capture);
        assert!(info.is_default);
    }

    #[cfg(feature = "sdl-backend")]
    #[test]
    fn test_enumerate_devices() {
        let sdl = sdl2::init().unwrap();
        let backend = SdlAudioBackend::new(&sdl).unwrap();

        let output_devices = backend.enumerate_output_devices().unwrap();
        println!("Output devices: {:?}", output_devices);

        let capture_devices = backend.enumerate_capture_devices().unwrap();
        println!("Capture devices: {:?}", capture_devices);
    }
}
