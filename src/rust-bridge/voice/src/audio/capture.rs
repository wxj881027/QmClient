//! 音频采集模块
//!
//! 统一的音频采集接口，支持多种后端
//! 参考 C++ voice_core.cpp 的 SDL 音频回调实现

use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Arc;

use crossbeam::channel::{bounded, Receiver, Sender};
use parking_lot::Mutex;

use super::backend::{create_backend, AudioBackendType, AudioError};
use super::engine::{AudioFrame, FRAME_SAMPLES, SAMPLE_RATE};

/// 捕获配置
#[derive(Debug, Clone)]
pub struct CaptureConfig {
    /// 采样率
    pub sample_rate: u32,
    /// 声道数
    pub channels: u16,
    /// 帧大小
    pub frame_size: usize,
    /// 设备名称
    pub device_name: Option<String>,
    /// 增益 (0.0 - 4.0)
    pub gain: f32,
}

impl Default for CaptureConfig {
    fn default() -> Self {
        Self {
            sample_rate: SAMPLE_RATE,
            channels: 1,
            frame_size: FRAME_SAMPLES,
            device_name: None,
            gain: 1.0,
        }
    }
}

/// 音频采集器
///
/// 使用 cpal 进行跨平台音频采集
pub struct AudioCapture {
    /// 配置
    config: CaptureConfig,
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
    /// 麦克风电平 (0-100)
    mic_level: AtomicI32,
    /// 帧计数
    #[allow(dead_code)]
    frame_count: AtomicI32,
    /// 缓冲区
    buffer: Mutex<Vec<i16>>,
}

impl AudioCapture {
    /// 使用指定后端创建采集器
    pub fn new(backend_type: AudioBackendType, config: CaptureConfig) -> Result<Self, AudioError> {
        let (frame_tx, frame_rx) = bounded(32);

        Ok(Self {
            config,
            backend_type,
            #[cfg(feature = "cpal-backend")]
            stream: None,
            frame_tx,
            frame_rx,
            running: AtomicBool::new(false),
            mic_level: AtomicI32::new(0),
            frame_count: AtomicI32::new(0),
            buffer: Mutex::new(Vec::new()),
        })
    }

    /// 使用默认后端 (cpal) 创建采集器
    pub fn default_cpal(device_name: Option<&str>) -> Result<Self, AudioError> {
        let config = CaptureConfig {
            device_name: device_name.map(|s| s.to_string()),
            ..Default::default()
        };
        Self::new(AudioBackendType::Cpal, config)
    }

    /// 启动采集
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
        log::info!("Audio capture started");
        Ok(())
    }

    #[cfg(feature = "cpal-backend")]
    fn start_cpal(&mut self) -> Result<(), AudioError> {
        use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

        let host = cpal::default_host();

        // 选择设备
        let device = if let Some(ref name) = self.config.device_name {
            host.input_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?
                .find(|d| d.name().map(|n| n == *name).unwrap_or(false))
                .ok_or_else(|| {
                    AudioError::DeviceNotAvailable(format!("Device not found: {}", name))
                })?
        } else {
            host.default_input_device().ok_or_else(|| {
                AudioError::DeviceNotAvailable("No default input device".to_string())
            })?
        };

        // 获取支持的配置
        let supported_config = device
            .default_input_config()
            .map_err(|e| AudioError::FormatError(e.to_string()))?;

        log::info!(
            "Opening capture device '{}' with config: {:?}",
            device.name().unwrap_or_default(),
            supported_config
        );

        let sample_format = supported_config.sample_format();
        let stream_config: cpal::StreamConfig = supported_config.into();

        // 创建回调
        let frame_tx = self.frame_tx.clone();
        let gain = self.config.gain;
        let mic_level = Arc::new(AtomicI32::new(0));
        let mic_level_clone = mic_level.clone();
        let buffer = Arc::new(Mutex::new(Vec::new()));
        let buffer_clone = buffer.clone();

        let err_fn = |err| log::error!("Capture stream error: {}", err);

        // 根据采样格式创建流
        let stream = match sample_format {
            cpal::SampleFormat::I16 => device.build_input_stream(
                &stream_config,
                move |data: &[i16], _: &cpal::InputCallbackInfo| {
                    Self::capture_callback_i16(
                        data,
                        &frame_tx,
                        gain,
                        &mic_level_clone,
                        &buffer_clone,
                    );
                },
                err_fn,
                None,
            ),
            cpal::SampleFormat::F32 => device.build_input_stream(
                &stream_config,
                move |data: &[f32], _: &cpal::InputCallbackInfo| {
                    Self::capture_callback_f32(
                        data,
                        &frame_tx,
                        gain,
                        &mic_level_clone,
                        &buffer_clone,
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

    /// i16 格式捕获回调
    #[cfg(feature = "cpal-backend")]
    fn capture_callback_i16(
        data: &[i16],
        frame_tx: &Sender<AudioFrame>,
        gain: f32,
        mic_level: &AtomicI32,
        buffer: &Mutex<Vec<i16>>,
    ) {
        let mut buf = buffer.lock();

        // 应用增益并添加到缓冲区
        for &sample in data {
            let amplified = (sample as f32 * gain).clamp(-32768.0, 32767.0) as i16;
            buf.push(amplified);
        }

        // 发送完整的帧
        while buf.len() >= FRAME_SAMPLES {
            let mut frame = [0i16; FRAME_SAMPLES];
            frame.copy_from_slice(&buf[..FRAME_SAMPLES]);
            buf.drain(..FRAME_SAMPLES);

            // 计算并更新麦克风电平
            let rms = calculate_frame_rms(&frame);
            let level = (rms * 100.0) as i32;
            mic_level.store(level.clamp(0, 100), Ordering::SeqCst);

            // 非阻塞发送
            let _ = frame_tx.try_send(frame);
        }
    }

    /// f32 格式捕获回调
    #[cfg(feature = "cpal-backend")]
    fn capture_callback_f32(
        data: &[f32],
        frame_tx: &Sender<AudioFrame>,
        gain: f32,
        mic_level: &AtomicI32,
        buffer: &Mutex<Vec<i16>>,
    ) {
        let mut buf = buffer.lock();

        // 转换为 i16 并应用增益
        for &sample in data {
            let amplified = (sample * gain * 32767.0).clamp(-32768.0, 32767.0) as i16;
            buf.push(amplified);
        }

        // 发送完整的帧
        while buf.len() >= FRAME_SAMPLES {
            let mut frame = [0i16; FRAME_SAMPLES];
            frame.copy_from_slice(&buf[..FRAME_SAMPLES]);
            buf.drain(..FRAME_SAMPLES);

            // 计算并更新麦克风电平
            let rms = calculate_frame_rms(&frame);
            let level = (rms * 100.0) as i32;
            mic_level.store(level.clamp(0, 100), Ordering::SeqCst);

            let _ = frame_tx.try_send(frame);
        }
    }

    /// 停止采集
    pub fn stop(&mut self) {
        #[cfg(feature = "cpal-backend")]
        {
            self.stream = None;
        }

        self.running.store(false, Ordering::SeqCst);
        self.buffer.lock().clear();
        log::info!("Audio capture stopped");
    }

    /// 接收音频帧
    pub fn recv_frame(&self) -> Option<AudioFrame> {
        self.frame_rx.try_recv().ok()
    }

    /// 接收音频帧（阻塞）
    pub fn recv_frame_blocking(&self) -> Option<AudioFrame> {
        self.frame_rx.recv().ok()
    }

    /// 尝试接收所有可用帧
    pub fn recv_all_frames(&self) -> Vec<AudioFrame> {
        let mut frames = Vec::new();
        while let Ok(frame) = self.frame_rx.try_recv() {
            frames.push(frame);
        }
        frames
    }

    /// 获取麦克风电平 (0-100)
    pub fn mic_level(&self) -> i32 {
        self.mic_level.load(Ordering::SeqCst)
    }

    /// 设置增益
    pub fn set_gain(&mut self, gain: f32) {
        self.config.gain = gain.clamp(0.0, 4.0);
    }

    /// 获取增益
    pub fn gain(&self) -> f32 {
        self.config.gain
    }

    /// 是否正在运行
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    /// 列出可用输入设备
    pub fn list_devices(backend_type: AudioBackendType) -> Result<Vec<String>, AudioError> {
        let backend = create_backend(backend_type)?;
        backend.list_input_devices()
    }

    /// 列出 cpal 输入设备
    pub fn list_cpal_devices() -> Result<Vec<String>, AudioError> {
        Self::list_devices(AudioBackendType::Cpal)
    }
}

impl Drop for AudioCapture {
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

/// 计算帧的峰值电平
pub fn calculate_frame_peak(frame: &AudioFrame) -> f32 {
    let max_abs = frame.iter().map(|&s| s.abs()).max().unwrap_or(0);

    max_abs as f32 / 32768.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_capture_config_default() {
        let config = CaptureConfig::default();
        assert_eq!(config.sample_rate, SAMPLE_RATE);
        assert_eq!(config.channels, 1);
        assert_eq!(config.frame_size, FRAME_SAMPLES);
        assert_eq!(config.gain, 1.0);
    }

    #[test]
    fn test_capture_creation() {
        let capture = AudioCapture::default_cpal(None);
        assert!(capture.is_ok());
    }

    #[test]
    fn test_list_devices() {
        let devices = AudioCapture::list_cpal_devices();
        println!("Input devices: {:?}", devices);
    }

    #[test]
    fn test_calculate_frame_rms() {
        let silence = [0i16; FRAME_SAMPLES];
        assert_eq!(calculate_frame_rms(&silence), 0.0);

        let max_frame = [32767i16; FRAME_SAMPLES];
        let rms = calculate_frame_rms(&max_frame);
        assert!((rms - 1.0).abs() < 0.01);
    }

    #[test]
    fn test_calculate_frame_peak() {
        let frame: [i16; FRAME_SAMPLES] = [10000; FRAME_SAMPLES];
        let peak = calculate_frame_peak(&frame);
        assert!((peak - (10000.0 / 32768.0)).abs() < 0.001);
    }
}
