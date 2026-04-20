//! 音频播放模块
//!
//! 统一的音频播放接口，支持多种后端
//! 参考 C++ voice_core.cpp 的 SDL 音频回调实现

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicUsize, Ordering};
use std::sync::Arc;

use crossbeam::channel::{bounded, Receiver, Sender};
use parking_lot::Mutex;

use super::backend::{create_backend, AudioBackendType, AudioError};
use super::engine::{AudioFrame, FRAME_SAMPLES, SAMPLE_RATE};

/// 播放配置
#[derive(Debug, Clone)]
pub struct PlaybackConfig {
    /// 采样率
    pub sample_rate: u32,
    /// 声道数 (1 = 单声道, 2 = 立体声)
    pub channels: usize,
    /// 帧大小
    pub frame_size: usize,
    /// 设备名称
    pub device_name: Option<String>,
    /// 音量 (0.0 - 2.0)
    pub volume: f32,
}

impl Default for PlaybackConfig {
    fn default() -> Self {
        Self {
            sample_rate: SAMPLE_RATE,
            channels: 2,
            frame_size: FRAME_SAMPLES,
            device_name: None,
            volume: 1.0,
        }
    }
}

/// 音频播放器
///
/// 使用 cpal 进行跨平台音频播放
pub struct AudioPlayback {
    /// 配置
    config: PlaybackConfig,
    /// 后端类型
    #[allow(dead_code)]
    backend_type: AudioBackendType,
    /// 音频流
    #[cfg(feature = "cpal-backend")]
    stream: Option<cpal::Stream>,
    /// 音频帧发送端
    frame_tx: Sender<AudioFrame>,
    /// 音频帧接收端
    frame_rx: Receiver<AudioFrame>,
    /// 是否正在运行
    running: AtomicBool,
    /// 输出声道数
    output_channels: AtomicUsize,
    /// 输出电平 (0-100)
    output_level: Arc<AtomicI32>,
    /// 混音缓冲区
    mix_buffer: Arc<Mutex<Vec<i32>>>,
}

impl AudioPlayback {
    /// 使用指定后端创建播放器
    pub fn new(backend_type: AudioBackendType, config: PlaybackConfig) -> Result<Self, AudioError> {
        let (frame_tx, frame_rx) = bounded(32);

        Ok(Self {
            config,
            backend_type,
            #[cfg(feature = "cpal-backend")]
            stream: None,
            frame_tx,
            frame_rx,
            running: AtomicBool::new(false),
            output_channels: AtomicUsize::new(2),
            output_level: Arc::new(AtomicI32::new(0)),
            mix_buffer: Arc::new(Mutex::new(Vec::new())),
        })
    }

    /// 使用默认后端 (cpal) 创建播放器
    pub fn default_cpal(device_name: Option<&str>) -> Result<Self, AudioError> {
        let config = PlaybackConfig {
            device_name: device_name.map(|s| s.to_string()),
            ..Default::default()
        };
        Self::new(AudioBackendType::Cpal, config)
    }

    /// 启动播放
    pub fn start(&mut self) -> Result<(), AudioError> {
        if self.running.load(Ordering::SeqCst) {
            return Ok(());
        }

        #[cfg(feature = "cpal-backend")]
        {
            self.start_cpal()?;
        }

        #[cfg(not(feature = "cpal-backend"))]
        {
            return Err(AudioError::BackendNotAvailable(
                "cpal backend not enabled".to_string(),
            ));
        }

        self.running.store(true, Ordering::SeqCst);
        log::info!("Audio playback started");
        Ok(())
    }

    #[cfg(feature = "cpal-backend")]
    fn start_cpal(&mut self) -> Result<(), AudioError> {
        use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

        let host = cpal::default_host();

        // 选择设备
        let device = if let Some(ref name) = self.config.device_name {
            host.output_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?
                .find(|d| d.name().map(|n| n == *name).unwrap_or(false))
                .ok_or_else(|| {
                    AudioError::DeviceNotAvailable(format!("Device not found: {}", name))
                })?
        } else {
            host.default_output_device().ok_or_else(|| {
                AudioError::DeviceNotAvailable("No default output device".to_string())
            })?
        };

        // 获取支持的配置
        let supported_config = device
            .default_output_config()
            .map_err(|e| AudioError::FormatError(e.to_string()))?;

        log::info!(
            "Opening playback device '{}' with config: {:?}",
            device.name().unwrap_or_default(),
            supported_config
        );

        let sample_format = supported_config.sample_format();
        let stream_config: cpal::StreamConfig = supported_config.into();

        // 更新输出声道数
        let channels = stream_config.channels as usize;
        self.output_channels.store(channels, Ordering::SeqCst);

        // 创建回调
        let frame_rx = self.frame_rx.clone();
        let volume = self.config.volume;
        let output_level_clone = self.output_level.clone();
        let mix_buffer_clone = self.mix_buffer.clone();

        let err_fn = |err| log::error!("Playback stream error: {}", err);

        // 根据采样格式创建流
        let stream = match sample_format {
            cpal::SampleFormat::I16 => device.build_output_stream(
                &stream_config,
                move |data: &mut [i16], _: &cpal::OutputCallbackInfo| {
                    Self::playback_callback_i16(
                        data,
                        &frame_rx,
                        channels,
                        volume,
                        &output_level_clone,
                        &mix_buffer_clone,
                    );
                },
                err_fn,
                None,
            ),
            cpal::SampleFormat::F32 => device.build_output_stream(
                &stream_config,
                move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
                    Self::playback_callback_f32(
                        data,
                        &frame_rx,
                        channels,
                        volume,
                        &output_level_clone,
                        &mix_buffer_clone,
                    );
                },
                err_fn,
                None,
            ),
            format => {
                return Err(AudioError::FormatError(format!(
                    "Unsupported sample format: {:?}",
                    format
                )));
            }
        }
        .map_err(|e| AudioError::StreamError(e.to_string()))?;

        stream
            .play()
            .map_err(|e| AudioError::StreamError(e.to_string()))?;
        self.stream = Some(stream);

        Ok(())
    }

    /// i16 格式播放回调
    #[cfg(feature = "cpal-backend")]
    fn playback_callback_i16(
        data: &mut [i16],
        frame_rx: &Receiver<AudioFrame>,
        channels: usize,
        volume: f32,
        output_level: &Arc<AtomicI32>,
        _mix_buffer: &Arc<Mutex<Vec<i32>>>,
    ) {
        // 尝试获取音频帧
        if let Ok(frame) = frame_rx.try_recv() {
            // 计算输出电平
            let rms = calculate_frame_rms(&frame);
            let level = (rms * 100.0) as i32;
            output_level.store(level.clamp(0, 100), Ordering::SeqCst);

            // 应用音量并输出
            if channels == 2 {
                // 单声道转立体声
                for (i, &sample) in frame.iter().enumerate() {
                    if i * 2 + 1 < data.len() {
                        let out = (sample as f32 * volume).clamp(-32768.0, 32767.0) as i16;
                        data[i * 2] = out;
                        data[i * 2 + 1] = out;
                    }
                }
            } else if channels == 1 {
                // 单声道输出
                let copy_len = frame.len().min(data.len());
                for i in 0..copy_len {
                    data[i] = (frame[i] as f32 * volume).clamp(-32768.0, 32767.0) as i16;
                }
            } else {
                // 多声道：复制到所有声道
                let samples_per_frame = data.len() / channels;
                for i in 0..samples_per_frame.min(FRAME_SAMPLES) {
                    let sample = frame[i];
                    let out = (sample as f32 * volume).clamp(-32768.0, 32767.0) as i16;
                    for ch in 0..channels {
                        data[i * channels + ch] = out;
                    }
                }
            }
        } else {
            // 无数据时输出静音
            data.fill(0);
            output_level.store(0, Ordering::SeqCst);
        }
    }

    /// f32 格式播放回调
    #[cfg(feature = "cpal-backend")]
    fn playback_callback_f32(
        data: &mut [f32],
        frame_rx: &Receiver<AudioFrame>,
        channels: usize,
        volume: f32,
        output_level: &Arc<AtomicI32>,
        _mix_buffer: &Arc<Mutex<Vec<i32>>>,
    ) {
        if let Ok(frame) = frame_rx.try_recv() {
            let rms = calculate_frame_rms(&frame);
            let level = (rms * 100.0) as i32;
            output_level.store(level.clamp(0, 100), Ordering::SeqCst);

            if channels == 2 {
                for (i, &sample) in frame.iter().enumerate() {
                    if i * 2 + 1 < data.len() {
                        let out = (sample as f32 / 32767.0) * volume;
                        data[i * 2] = out;
                        data[i * 2 + 1] = out;
                    }
                }
            } else if channels == 1 {
                let copy_len = frame.len().min(data.len());
                for i in 0..copy_len {
                    data[i] = (frame[i] as f32 / 32767.0) * volume;
                }
            } else {
                let samples_per_frame = data.len() / channels;
                for i in 0..samples_per_frame.min(FRAME_SAMPLES) {
                    let out = (frame[i] as f32 / 32767.0) * volume;
                    for ch in 0..channels {
                        data[i * channels + ch] = out;
                    }
                }
            }
        } else {
            data.fill(0.0);
            output_level.store(0, Ordering::SeqCst);
        }
    }

    /// 停止播放
    pub fn stop(&mut self) {
        #[cfg(feature = "cpal-backend")]
        {
            self.stream = None;
        }

        self.running.store(false, Ordering::SeqCst);
        self.mix_buffer.lock().clear();
        log::info!("Audio playback stopped");
    }

    /// 暂停播放
    pub fn pause(&self) {
        #[cfg(feature = "cpal-backend")]
        {
            use cpal::traits::StreamTrait;
            if let Some(ref stream) = self.stream {
                let _ = stream.pause();
            }
        }
    }

    /// 恢复播放
    pub fn resume(&self) {
        #[cfg(feature = "cpal-backend")]
        {
            use cpal::traits::StreamTrait;
            if let Some(ref stream) = self.stream {
                let _ = stream.play();
            }
        }
    }

    /// 发送音频帧
    pub fn send_frame(&self, frame: &AudioFrame) -> bool {
        self.frame_tx.try_send(*frame).is_ok()
    }

    /// 发送音频帧（阻塞）
    pub fn send_frame_blocking(&self, frame: &AudioFrame) -> bool {
        self.frame_tx.send(*frame).is_ok()
    }

    /// 发送样本数据
    pub fn send_samples(&self, samples: &[i16]) -> bool {
        if samples.len() < FRAME_SAMPLES {
            return false;
        }

        let mut frame = [0i16; FRAME_SAMPLES];
        frame.copy_from_slice(&samples[..FRAME_SAMPLES]);
        self.send_frame(&frame)
    }

    /// 发送立体声混音数据
    ///
    /// left_gain 和 right_gain 用于空间音频
    pub fn send_spatial_frame(&self, frame: &AudioFrame, left_gain: f32, right_gain: f32) -> bool {
        let channels = self.output_channels.load(Ordering::SeqCst);

        if channels == 1 {
            // 单声道：平均左右
            let mono_gain = (left_gain + right_gain) / 2.0;
            let mut mono_frame = *frame;
            for sample in mono_frame.iter_mut() {
                *sample = (*sample as f32 * mono_gain).clamp(-32768.0, 32767.0) as i16;
            }
            self.send_frame(&mono_frame)
        } else {
            // 立体声或多声道：直接发送，让回调处理
            // 注意：这里简化处理，实际应该在回调中应用增益
            self.send_frame(frame)
        }
    }

    /// 获取输出电平 (0-100)
    pub fn output_level(&self) -> i32 {
        self.output_level.load(Ordering::SeqCst)
    }

    /// 获取输出声道数
    pub fn output_channels(&self) -> usize {
        self.output_channels.load(Ordering::SeqCst)
    }

    /// 设置音量
    pub fn set_volume(&mut self, volume: f32) {
        self.config.volume = volume.clamp(0.0, 2.0);
    }

    /// 获取音量
    pub fn volume(&self) -> f32 {
        self.config.volume
    }

    /// 是否正在运行
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    /// 列出可用输出设备
    pub fn list_devices(backend_type: AudioBackendType) -> Result<Vec<String>, AudioError> {
        let backend = create_backend(backend_type)?;
        backend.list_output_devices()
    }

    /// 列出 cpal 输出设备
    pub fn list_cpal_devices() -> Result<Vec<String>, AudioError> {
        Self::list_devices(AudioBackendType::Cpal)
    }

    /// 清空播放队列
    pub fn clear_queue(&self) {
        while self.frame_rx.try_recv().is_ok() {}
    }
}

impl Drop for AudioPlayback {
    fn drop(&mut self) {
        self.stop();
    }
}

/// 计算帧的 RMS 电平
fn calculate_frame_rms(frame: &AudioFrame) -> f32 {
    let sum: f64 = frame
        .iter()
        .map(|&s| {
            let normalized = s as f64 / 32768.0;
            normalized * normalized
        })
        .sum();

    (sum / frame.len() as f64).sqrt() as f32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_playback_config_default() {
        let config = PlaybackConfig::default();
        assert_eq!(config.sample_rate, SAMPLE_RATE);
        assert_eq!(config.channels, 2);
        assert_eq!(config.frame_size, FRAME_SAMPLES);
        assert_eq!(config.volume, 1.0);
    }

    #[test]
    fn test_playback_creation() {
        let playback = AudioPlayback::default_cpal(None);
        assert!(playback.is_ok());
    }

    #[test]
    fn test_list_devices() {
        let devices = AudioPlayback::list_cpal_devices();
        println!("Output devices: {:?}", devices);
    }

    #[test]
    fn test_send_frame() {
        let playback = AudioPlayback::default_cpal(None).unwrap();
        let frame = [1000i16; FRAME_SAMPLES];

        // 未启动时发送应该成功（只是放入队列）
        assert!(playback.send_frame(&frame));
    }

    #[test]
    fn test_volume_clamp() {
        let mut playback = AudioPlayback::default_cpal(None).unwrap();

        playback.set_volume(3.0);
        assert_eq!(playback.volume(), 2.0);

        playback.set_volume(-1.0);
        assert_eq!(playback.volume(), 0.0);
    }
}
