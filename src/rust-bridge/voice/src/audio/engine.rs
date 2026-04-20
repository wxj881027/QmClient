//! 音频引擎核心模块
//!
//! 统一管理音频捕获和播放，提供完整的音频 I/O 能力
//! 参考 C++ voice_core.cpp 实现

use std::sync::atomic::{AtomicI32, AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;

use crossbeam::channel::{bounded, Receiver, Sender};
use parking_lot::{Mutex, RwLock};

use super::backend::{AudioBackendType, AudioError};
use super::mixer::{AudioMixer, AudioSource, MixConfig};
use super::AudioConfig;

/// 音频帧大小 (20ms @ 48kHz)
pub const FRAME_SAMPLES: usize = 960;

/// 采样率
pub const SAMPLE_RATE: u32 = 48000;

/// 音频帧类型
pub type AudioFrame = [i16; FRAME_SAMPLES];

/// 音频引擎配置
#[derive(Debug, Clone)]
pub struct EngineConfig {
    /// 音频配置
    pub audio: AudioConfig,
    /// 输出声道数 (1 = 单声道, 2 = 立体声)
    pub output_channels: usize,
    /// 输入设备名称
    pub input_device: Option<String>,
    /// 输出设备名称
    pub output_device: Option<String>,
    /// 缓冲区帧数
    pub buffer_frames: usize,
}

impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            audio: AudioConfig::default(),
            output_channels: 2,
            input_device: None,
            output_device: None,
            buffer_frames: 8,
        }
    }
}

/// 音频引擎状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineState {
    /// 未初始化
    Uninitialized,
    /// 已初始化
    Initialized,
    /// 运行中
    Running,
    /// 已暂停
    Paused,
    /// 错误状态
    Error,
}

/// 音频引擎
///
/// 统一管理音频捕获和播放
pub struct AudioEngine {
    /// 配置
    config: RwLock<EngineConfig>,
    /// 状态
    state: AtomicI32,
    /// 输出声道数
    output_channels: AtomicUsize,

    // 音频流
    #[cfg(feature = "cpal-backend")]
    input_stream: Mutex<Option<cpal::Stream>>,
    #[cfg(feature = "cpal-backend")]
    output_stream: Mutex<Option<cpal::Stream>>,

    // 音频通道
    /// 捕获音频发送端
    capture_tx: Mutex<Option<Sender<AudioFrame>>>,
    /// 捕获音频接收端
    capture_rx: Mutex<Option<Receiver<AudioFrame>>>,

    /// 播放音频发送端
    playback_tx: Mutex<Option<Sender<AudioFrame>>>,
    /// 播放音频接收端
    playback_rx: Mutex<Option<Receiver<AudioFrame>>>,

    // 混音器
    mixer: RwLock<AudioMixer>,

    // 电平监控
    /// 麦克风电平 (0-100)
    mic_level: AtomicI32,
    /// 输出电平 (0-100)
    output_level: AtomicI32,

    // 丢包统计
    /// 捕获丢帧计数（通道满时丢弃）
    dropped_capture_frames: Arc<AtomicU64>,
    /// 播放丢帧计数（通道满时丢弃）
    dropped_playback_frames: Arc<AtomicU64>,

    // 设备信息
    /// 输入设备列表
    input_devices: RwLock<Vec<String>>,
    /// 输出设备列表
    output_devices: RwLock<Vec<String>>,

    /// 后端类型
    #[allow(dead_code)]
    backend_type: AudioBackendType,
}

impl AudioEngine {
    /// 创建新的音频引擎
    pub fn new(config: EngineConfig) -> Self {
        // 通道容量从 16 增大到 64，减少高负载时的丢帧
        // 64 帧 * 20ms/帧 = 1.28 秒缓冲，足以应对偶发的处理延迟
        let (capture_tx, capture_rx) = bounded(64);
        let (playback_tx, playback_rx) = bounded(64);

        let dropped_capture = Arc::new(AtomicU64::new(0));
        let dropped_playback = Arc::new(AtomicU64::new(0));

        Self {
            config: RwLock::new(config.clone()),
            state: AtomicI32::new(EngineState::Uninitialized as i32),
            output_channels: AtomicUsize::new(config.output_channels),

            #[cfg(feature = "cpal-backend")]
            input_stream: Mutex::new(None),
            #[cfg(feature = "cpal-backend")]
            output_stream: Mutex::new(None),

            capture_tx: Mutex::new(Some(capture_tx)),
            capture_rx: Mutex::new(Some(capture_rx)),
            playback_tx: Mutex::new(Some(playback_tx)),
            playback_rx: Mutex::new(Some(playback_rx)),

            mixer: RwLock::new(AudioMixer::new()),
            mic_level: AtomicI32::new(0),
            output_level: AtomicI32::new(0),
            dropped_capture_frames: dropped_capture,
            dropped_playback_frames: dropped_playback,
            input_devices: RwLock::new(Vec::new()),
            output_devices: RwLock::new(Vec::new()),
            backend_type: AudioBackendType::Cpal,
        }
    }

    /// 使用默认配置创建引擎
    pub fn default_engine() -> Self {
        Self::new(EngineConfig::default())
    }

    /// 初始化音频引擎
    pub fn init(&self) -> Result<(), AudioError> {
        let current_state = self.get_state();
        if current_state == EngineState::Running {
            return Ok(());
        }

        // 枚举设备
        self.enumerate_devices()?;

        // 设置状态
        self.set_state(EngineState::Initialized);

        log::info!("Audio engine initialized");
        Ok(())
    }

    /// 启动音频引擎
    pub fn start(&self) -> Result<(), AudioError> {
        let config = self.config.read().clone();

        // 初始化 cpal 后端
        #[cfg(feature = "cpal-backend")]
        {
            self.start_cpal(&config)?;
        }

        #[cfg(not(feature = "cpal-backend"))]
        {
            return Err(AudioError::BackendNotAvailable(
                "cpal backend not enabled".to_string(),
            ));
        }

        self.set_state(EngineState::Running);
        log::info!("Audio engine started");
        Ok(())
    }

    #[cfg(feature = "cpal-backend")]
    fn start_cpal(&self, config: &EngineConfig) -> Result<(), AudioError> {
        use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

        let host = cpal::default_host();

        // 打开输入流
        let input_device = if let Some(ref name) = config.input_device {
            host.input_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?
                .find(|d| d.name().map(|n| n == *name).unwrap_or(false))
        } else {
            host.default_input_device()
        };

        if let Some(device) = input_device {
            let supported_config = device
                .default_input_config()
                .map_err(|e| AudioError::FormatError(e.to_string()))?;

            let sample_format = supported_config.sample_format();
            let config: cpal::StreamConfig = supported_config.into();

            let capture_tx = self.capture_tx.lock().clone();
            let capture_tx = capture_tx.ok_or_else(|| {
                AudioError::StreamError("Capture channel not available".to_string())
            })?;

            let err_fn = |err| log::error!("Audio input error: {}", err);

            // 根据采样格式创建流
            let stream = match sample_format {
                cpal::SampleFormat::I16 => {
                    let tx = capture_tx.clone();
                    let drop_counter = self.dropped_capture_frames.clone();
                    device.build_input_stream(
                        &config,
                        move |data: &[i16], _: &cpal::InputCallbackInfo| {
                            Self::capture_callback_i16(data, &tx, &drop_counter);
                        },
                        err_fn,
                        None,
                    )
                }
                cpal::SampleFormat::F32 => {
                    let tx = capture_tx.clone();
                    let drop_counter = self.dropped_capture_frames.clone();
                    device.build_input_stream(
                        &config,
                        move |data: &[f32], _: &cpal::InputCallbackInfo| {
                            Self::capture_callback_f32(data, &tx, &drop_counter);
                        },
                        err_fn,
                        None,
                    )
                }
                _ => {
                    return Err(AudioError::FormatError(format!(
                        "Unsupported sample format: {:?}",
                        sample_format
                    )));
                }
            }
            .map_err(|e| AudioError::StreamError(e.to_string()))?;

            stream
                .play()
                .map_err(|e| AudioError::StreamError(e.to_string()))?;
            *self.input_stream.lock() = Some(stream);

            log::info!("Input stream started on device: {:?}", device.name());
        } else {
            log::warn!("No input device available");
        }

        // 打开输出流
        let output_device = if let Some(ref name) = config.output_device {
            host.output_devices()
                .map_err(|e| AudioError::DeviceNotAvailable(e.to_string()))?
                .find(|d| d.name().map(|n| n == *name).unwrap_or(false))
        } else {
            host.default_output_device()
        };

        if let Some(device) = output_device {
            let supported_config = device
                .default_output_config()
                .map_err(|e| AudioError::FormatError(e.to_string()))?;

            let sample_format = supported_config.sample_format();
            let output_config: cpal::StreamConfig = supported_config.into();

            // 更新输出声道数
            let channels = output_config.channels as usize;
            self.output_channels.store(channels, Ordering::SeqCst);

            let playback_rx = self.playback_rx.lock().clone();
            let playback_rx = playback_rx.ok_or_else(|| {
                AudioError::StreamError("Playback channel not available".to_string())
            })?;

            let err_fn = |err| log::error!("Audio output error: {}", err);

            // 根据采样格式创建流
            let stream = match sample_format {
                cpal::SampleFormat::I16 => {
                    let rx = playback_rx.clone();
                    device.build_output_stream(
                        &output_config,
                        move |data: &mut [i16], _: &cpal::OutputCallbackInfo| {
                            Self::playback_callback_i16(data, &rx, channels);
                        },
                        err_fn,
                        None,
                    )
                }
                cpal::SampleFormat::F32 => {
                    let rx = playback_rx.clone();
                    device.build_output_stream(
                        &output_config,
                        move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
                            Self::playback_callback_f32(data, &rx, channels);
                        },
                        err_fn,
                        None,
                    )
                }
                _ => {
                    return Err(AudioError::FormatError(format!(
                        "Unsupported sample format: {:?}",
                        sample_format
                    )));
                }
            }
            .map_err(|e| AudioError::StreamError(e.to_string()))?;

            stream
                .play()
                .map_err(|e| AudioError::StreamError(e.to_string()))?;
            *self.output_stream.lock() = Some(stream);

            log::info!("Output stream started on device: {:?}", device.name());
        } else {
            log::warn!("No output device available");
        }

        Ok(())
    }

    /// i16 格式捕获回调
    #[cfg(feature = "cpal-backend")]
    fn capture_callback_i16(data: &[i16], tx: &Sender<AudioFrame>, drop_counter: &AtomicU64) {
        // 简单实现：收集样本并发送帧
        // 注意：实际实现需要处理采样率转换和声道混合
        let mut frame = [0i16; FRAME_SAMPLES];
        let copy_len = data.len().min(FRAME_SAMPLES);
        frame[..copy_len].copy_from_slice(&data[..copy_len]);

        // 非阻塞发送，通道满时记录丢帧
        if tx.try_send(frame).is_err() {
            drop_counter.fetch_add(1, Ordering::Relaxed);
            log::debug!("Capture frame dropped: channel full (capacity=64)");
        }
    }

    /// f32 格式捕获回调
    #[cfg(feature = "cpal-backend")]
    fn capture_callback_f32(data: &[f32], tx: &Sender<AudioFrame>, drop_counter: &AtomicU64) {
        let mut frame = [0i16; FRAME_SAMPLES];
        let copy_len = data.len().min(FRAME_SAMPLES);

        for i in 0..copy_len {
            // f32 转 i16
            let sample = (data[i] * 32767.0).clamp(-32768.0, 32767.0) as i16;
            frame[i] = sample;
        }

        if tx.try_send(frame).is_err() {
            drop_counter.fetch_add(1, Ordering::Relaxed);
            log::debug!("Capture frame dropped: channel full (capacity=64)");
        }
    }

    /// i16 格式播放回调
    #[cfg(feature = "cpal-backend")]
    fn playback_callback_i16(data: &mut [i16], rx: &Receiver<AudioFrame>, channels: usize) {
        // 尝试获取音频帧
        if let Ok(frame) = rx.try_recv() {
            // 单声道转立体声
            if channels == 2 {
                for (i, &sample) in frame.iter().enumerate() {
                    if i * 2 + 1 < data.len() {
                        data[i * 2] = sample;
                        data[i * 2 + 1] = sample;
                    }
                }
            } else {
                // 单声道输出
                let copy_len = frame.len().min(data.len());
                data[..copy_len].copy_from_slice(&frame[..copy_len]);
            }
        } else {
            // 无数据时输出静音
            data.fill(0);
        }
    }

    /// f32 格式播放回调
    #[cfg(feature = "cpal-backend")]
    fn playback_callback_f32(data: &mut [f32], rx: &Receiver<AudioFrame>, channels: usize) {
        if let Ok(frame) = rx.try_recv() {
            if channels == 2 {
                for (i, &sample) in frame.iter().enumerate() {
                    if i * 2 + 1 < data.len() {
                        data[i * 2] = sample as f32 / 32767.0;
                        data[i * 2 + 1] = sample as f32 / 32767.0;
                    }
                }
            } else {
                for (i, &sample) in frame.iter().enumerate() {
                    if i < data.len() {
                        data[i] = sample as f32 / 32767.0;
                    }
                }
            }
        } else {
            data.fill(0.0);
        }
    }

    /// 停止音频引擎
    pub fn stop(&self) {
        #[cfg(feature = "cpal-backend")]
        {
            if let Some(stream) = self.input_stream.lock().take() {
                drop(stream);
            }
            if let Some(stream) = self.output_stream.lock().take() {
                drop(stream);
            }
        }

        self.set_state(EngineState::Initialized);
        log::info!("Audio engine stopped");
    }

    /// 暂停音频引擎
    pub fn pause(&self) {
        #[cfg(feature = "cpal-backend")]
        {
            use cpal::traits::StreamTrait;
            if let Some(ref stream) = *self.input_stream.lock() {
                let _ = stream.pause();
            }
            if let Some(ref stream) = *self.output_stream.lock() {
                let _ = stream.pause();
            }
        }

        self.set_state(EngineState::Paused);
    }

    /// 恢复音频引擎
    pub fn resume(&self) {
        #[cfg(feature = "cpal-backend")]
        {
            use cpal::traits::StreamTrait;
            if let Some(ref stream) = *self.input_stream.lock() {
                let _ = stream.play();
            }
            if let Some(ref stream) = *self.output_stream.lock() {
                let _ = stream.play();
            }
        }

        self.set_state(EngineState::Running);
    }

    /// 枚举音频设备
    pub fn enumerate_devices(&self) -> Result<(), AudioError> {
        #[cfg(feature = "cpal-backend")]
        {
            use cpal::traits::{DeviceTrait, HostTrait};

            let host = cpal::default_host();

            // 枚举输入设备
            let mut input_devices = Vec::new();
            if let Ok(devices) = host.input_devices() {
                for device in devices {
                    if let Ok(name) = device.name() {
                        input_devices.push(name);
                    }
                }
            }
            *self.input_devices.write() = input_devices;

            // 枚举输出设备
            let mut output_devices = Vec::new();
            if let Ok(devices) = host.output_devices() {
                for device in devices {
                    if let Ok(name) = device.name() {
                        output_devices.push(name);
                    }
                }
            }
            *self.output_devices.write() = output_devices;
        }

        Ok(())
    }

    /// 获取输入设备列表
    pub fn get_input_devices(&self) -> Vec<String> {
        self.input_devices.read().clone()
    }

    /// 获取输出设备列表
    pub fn get_output_devices(&self) -> Vec<String> {
        self.output_devices.read().clone()
    }

    /// 接收捕获的音频帧
    pub fn recv_capture(&self) -> Option<AudioFrame> {
        if let Some(rx) = self.capture_rx.lock().as_ref() {
            rx.try_recv().ok()
        } else {
            None
        }
    }

    /// 发送音频帧到播放
    pub fn send_playback(&self, frame: &AudioFrame) -> bool {
        if let Some(tx) = self.playback_tx.lock().as_ref() {
            tx.try_send(*frame).is_ok()
        } else {
            false
        }
    }

    /// 添加音频源到混音器
    pub fn add_audio_source(&self, source: AudioSource) {
        self.mixer.write().add_source(source);
    }

    /// 移除音频源
    pub fn remove_audio_source(&self, id: u16) {
        self.mixer.write().remove_source(id);
    }

    /// 清空所有音频源
    pub fn clear_audio_sources(&self) {
        self.mixer.write().clear();
    }

    /// 设置混音配置
    pub fn set_mix_config(&self, config: MixConfig) {
        self.mixer.write().set_config(config);
    }

    /// 执行混音并发送
    pub fn mix_and_send(&self) {
        let channels = self.output_channels.load(Ordering::SeqCst);
        let mixed = self.mixer.write().mix(FRAME_SAMPLES, channels);

        // 转换为帧格式
        let mut frame = [0i16; FRAME_SAMPLES];
        let copy_len = mixed.len().min(FRAME_SAMPLES);
        frame[..copy_len].copy_from_slice(&mixed[..copy_len]);

        self.send_playback(&frame);
    }

    /// 更新麦克风电平
    pub fn update_mic_level(&self, samples: &[i16]) {
        let rms = calculate_rms(samples);
        let level = (rms * 100.0) as i32;
        self.mic_level.store(level.clamp(0, 100), Ordering::SeqCst);
    }

    /// 获取麦克风电平 (0-100)
    pub fn get_mic_level(&self) -> i32 {
        self.mic_level.load(Ordering::SeqCst)
    }

    /// 更新输出电平
    pub fn update_output_level(&self, samples: &[i16]) {
        let rms = calculate_rms(samples);
        let level = (rms * 100.0) as i32;
        self.output_level
            .store(level.clamp(0, 100), Ordering::SeqCst);
    }

    /// 获取输出电平 (0-100)
    pub fn get_output_level(&self) -> i32 {
        self.output_level.load(Ordering::SeqCst)
    }

    /// 获取输出声道数
    pub fn get_output_channels(&self) -> usize {
        self.output_channels.load(Ordering::SeqCst)
    }

    /// 获取捕获丢帧计数
    pub fn get_dropped_capture_frames(&self) -> u64 {
        self.dropped_capture_frames.load(Ordering::Relaxed)
    }

    /// 获取播放丢帧计数
    pub fn get_dropped_playback_frames(&self) -> u64 {
        self.dropped_playback_frames.load(Ordering::Relaxed)
    }

    /// 重置丢帧计数
    pub fn reset_drop_counters(&self) {
        self.dropped_capture_frames.store(0, Ordering::Relaxed);
        self.dropped_playback_frames.store(0, Ordering::Relaxed);
    }

    /// 获取引擎状态
    pub fn get_state(&self) -> EngineState {
        match self.state.load(Ordering::SeqCst) {
            0 => EngineState::Uninitialized,
            1 => EngineState::Initialized,
            2 => EngineState::Running,
            3 => EngineState::Paused,
            _ => EngineState::Error,
        }
    }

    /// 设置引擎状态
    fn set_state(&self, state: EngineState) {
        self.state.store(state as i32, Ordering::SeqCst);
    }

    /// 设置配置
    pub fn set_config(&self, config: EngineConfig) {
        *self.config.write() = config;
    }

    /// 获取配置
    pub fn get_config(&self) -> EngineConfig {
        self.config.read().clone()
    }

    /// 设置输入设备
    pub fn set_input_device(&self, name: Option<String>) {
        self.config.write().input_device = name;
    }

    /// 设置输出设备
    pub fn set_output_device(&self, name: Option<String>) {
        self.config.write().output_device = name;
    }

    /// 设置输出声道数
    pub fn set_output_channels(&self, channels: usize) {
        self.config.write().output_channels = channels;
        self.output_channels.store(channels, Ordering::SeqCst);
    }
}

impl Default for AudioEngine {
    fn default() -> Self {
        Self::default_engine()
    }
}

impl Drop for AudioEngine {
    fn drop(&mut self) {
        self.stop();
    }
}

/// 计算 RMS (均方根) 电平
///
/// 用于计算音频信号的响度
pub fn calculate_rms(samples: &[i16]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }

    let sum: f64 = samples
        .iter()
        .map(|&s| {
            let normalized = s as f64 / 32768.0;
            normalized * normalized
        })
        .sum();

    (sum / samples.len() as f64).sqrt() as f32
}

/// 计算峰值电平
///
/// 返回样本中最大的绝对值
pub fn calculate_peak(samples: &[i16]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }

    let max_abs = samples.iter().map(|&s| s.abs()).max().unwrap_or(0);

    max_abs as f32 / 32768.0
}

/// 应用增益
///
/// 调整音频样本的音量
pub fn apply_gain(samples: &mut [i16], gain: f32) {
    let gain = gain.clamp(0.0, 4.0);

    for sample in samples.iter_mut() {
        let out = *sample as f32 * gain;
        *sample = out.clamp(-32768.0, 32767.0) as i16;
    }
}

/// 硬削波
///
/// 将样本限制在 i16 范围内
pub fn hard_clip(samples: &mut [i16]) {
    for sample in samples.iter_mut() {
        *sample = (*sample).clamp(-32768, 32767);
    }
}

/// 混合两个音频缓冲区
///
/// result[i] = a[i] + b[i]，并进行削波
pub fn mix_buffers(a: &[i16], b: &[i16], result: &mut [i16]) {
    let len = a.len().min(b.len()).min(result.len());

    for i in 0..len {
        let mixed = a[i] as i32 + b[i] as i32;
        result[i] = mixed.clamp(-32768, 32767) as i16;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_calculate_rms() {
        // 静音
        let silence = [0i16; 960];
        assert_eq!(calculate_rms(&silence), 0.0);

        // 最大音量
        let max_volume: [i16; 960] = [32767; 960];
        let rms = calculate_rms(&max_volume);
        assert!((rms - 1.0).abs() < 0.01);
    }

    #[test]
    fn test_calculate_peak() {
        let samples: [i16; 10] = [100, -200, 300, -400, 500, -600, 700, -800, 900, -1000];
        let peak = calculate_peak(&samples);
        assert_eq!(peak, 1000.0 / 32768.0);
    }

    #[test]
    fn test_apply_gain() {
        let mut samples = [1000i16; 10];
        apply_gain(&mut samples, 2.0);
        assert_eq!(samples[0], 2000);
    }

    #[test]
    fn test_apply_gain_clipping() {
        let mut samples = [20000i16; 10];
        apply_gain(&mut samples, 2.0);
        // 20000 * 2 = 40000, 应该被削波到 32767
        assert_eq!(samples[0], 32767);
    }

    #[test]
    fn test_mix_buffers() {
        let a = [1000i16; 10];
        let b = [2000i16; 10];
        let mut result = [0i16; 10];

        mix_buffers(&a, &b, &mut result);
        assert_eq!(result[0], 3000);
    }

    #[test]
    fn test_engine_creation() {
        let engine = AudioEngine::default_engine();
        assert_eq!(engine.get_state(), EngineState::Uninitialized);
    }

    #[test]
    fn test_engine_config() {
        let config = EngineConfig {
            output_channels: 2,
            input_device: Some("Test Device".to_string()),
            ..Default::default()
        };

        let engine = AudioEngine::new(config.clone());
        let retrieved = engine.get_config();

        assert_eq!(retrieved.output_channels, 2);
        assert_eq!(retrieved.input_device, Some("Test Device".to_string()));
    }
}
