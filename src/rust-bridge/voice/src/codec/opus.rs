//! Opus 编解码器封装
//!
//! 使用 opus crate (libopus 官方绑定)
//! 采样率: 48000 Hz
//! 帧大小: 960 采样 (20ms)
//! 声道数: 1 (单声道)
//!
//! 配置与原有 C++ 实现 1:1 对应:
//! - 模式: VoIP (OPUS_APPLICATION_VOIP)
//! - 默认比特率: 48kbps (用户要求)
//! - 自适应比特率: 16-64kbps
//! - 支持 FEC 和丢包率设置

use opus::{Application, Bitrate, Channels, Decoder, Encoder};

/// 帧大小（采样数）
pub const FRAME_SAMPLES: usize = 960; // 20ms @ 48kHz
/// 采样率
pub const SAMPLE_RATE: u32 = 48000;
/// 声道数
pub const CHANNELS: usize = 1;

/// 默认比特率 (bps)
pub const DEFAULT_BITRATE: i32 = 48000;
/// 最小比特率 (bps)
pub const MIN_BITRATE: i32 = 6000;
/// 最大比特率 (bps)
pub const MAX_BITRATE: i32 = 510000;

/// 编解码错误
#[derive(Debug, thiserror::Error)]
pub enum CodecError {
    #[error("Opus error: {0}")]
    Opus(String),
    #[error("Invalid frame size: expected {expected}, got {actual}")]
    InvalidFrameSize { expected: usize, actual: usize },
    #[error("Buffer too small: need {needed}, have {have}")]
    BufferTooSmall { needed: usize, have: usize },
}

impl From<opus::Error> for CodecError {
    fn from(e: opus::Error) -> Self {
        CodecError::Opus(e.to_string())
    }
}

/// Opus 编解码器
///
/// 与原有 C++ 实现 CRClientVoice 中的 Opus 配置对应
pub struct OpusCodec {
    encoder: Encoder,
    decoder: Decoder,
    bitrate: i32,
    packet_loss_perc: i32,
    fec_enabled: bool,
}

impl OpusCodec {
    /// 创建新的编解码器
    ///
    /// 使用 VoIP 模式 (与原有实现一致)
    /// 默认比特率 48kbps
    pub fn new() -> Result<Self, CodecError> {
        let mut encoder = Encoder::new(SAMPLE_RATE, Channels::Mono, Application::Voip)
            .map_err(|e| CodecError::Opus(e.to_string()))?;

        // 与原有 C++ 实现对应的配置
        encoder
            .set_bitrate(Bitrate::Bits(DEFAULT_BITRATE))
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        encoder
            .set_complexity(10)
            .map_err(|e| CodecError::Opus(e.to_string()))?; // 最高复杂度
        encoder
            .set_inband_fec(false)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        encoder
            .set_packet_loss_perc(0)
            .map_err(|e| CodecError::Opus(e.to_string()))?;

        let decoder = Decoder::new(SAMPLE_RATE, Channels::Mono)
            .map_err(|e| CodecError::Opus(e.to_string()))?;

        Ok(Self {
            encoder,
            decoder,
            bitrate: DEFAULT_BITRATE,
            packet_loss_perc: 0,
            fec_enabled: false,
        })
    }

    /// 编码一帧音频
    ///
    /// # 参数
    /// * `pcm` - PCM 样本 (960 samples = 20ms), i16 格式
    /// * `output` - 输出缓冲区
    ///
    /// # 返回
    /// 编码后的字节数
    pub fn encode(&mut self, pcm: &[i16], output: &mut [u8]) -> Result<usize, CodecError> {
        if pcm.len() != FRAME_SAMPLES {
            return Err(CodecError::InvalidFrameSize {
                expected: FRAME_SAMPLES,
                actual: pcm.len(),
            });
        }

        // opus crate 的 encode 直接接受 i16 输入，无需 f32 中间转换
        self.encoder
            .encode(pcm, output)
            .map_err(|e| CodecError::Opus(e.to_string()))
    }

    /// 解码一帧音频
    ///
    /// # 参数
    /// * `opus_data` - Opus 编码数据
    /// * `pcm` - 输出 PCM 缓冲区 (至少 960 samples)
    ///
    /// # 返回
    /// 解码后的采样数
    pub fn decode(&mut self, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, CodecError> {
        if pcm.len() < FRAME_SAMPLES {
            return Err(CodecError::BufferTooSmall {
                needed: FRAME_SAMPLES,
                have: pcm.len(),
            });
        }

        // opus crate 直接输出 i16，无需 f32 中间转换
        self.decoder
            .decode(opus_data, pcm, false)
            .map_err(|e| CodecError::Opus(e.to_string()))
    }

    /// 使用 FEC 解码 (前向纠错)
    ///
    /// 当丢包但收到下一个包时，使用 FEC 恢复丢失的帧。
    /// opus crate 原生支持 decode_fec 标志。
    pub fn decode_fec(&mut self, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, CodecError> {
        if pcm.len() < FRAME_SAMPLES {
            return Err(CodecError::BufferTooSmall {
                needed: FRAME_SAMPLES,
                have: pcm.len(),
            });
        }

        // 使用 decode_fec=true 从当前包中恢复前一帧
        self.decoder
            .decode(opus_data, pcm, true)
            .map_err(|e| CodecError::Opus(e.to_string()))
    }

    /// PLC 解码 (丢包隐藏)
    ///
    /// 当完全丢包时，生成平滑的填补音频
    pub fn decode_plc(&mut self, pcm: &mut [i16]) -> Result<usize, CodecError> {
        if pcm.len() < FRAME_SAMPLES {
            return Err(CodecError::BufferTooSmall {
                needed: FRAME_SAMPLES,
                have: pcm.len(),
            });
        }

        // opus crate 原生支持 PLC: 传入空切片表示丢包
        self.decoder
            .decode(&[], pcm, false)
            .map_err(|e| CodecError::Opus(e.to_string()))
    }

    /// 设置比特率
    ///
    /// # 参数
    /// * `bitrate` - 比特率 (bps)，范围 6000-510000
    pub fn set_bitrate(&mut self, bitrate: i32) -> Result<(), CodecError> {
        let bitrate = bitrate.clamp(MIN_BITRATE, MAX_BITRATE);
        self.encoder
            .set_bitrate(Bitrate::Bits(bitrate))
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        self.bitrate = bitrate;
        Ok(())
    }

    /// 设置 FEC (前向纠错)
    pub fn set_fec(&mut self, enabled: bool) -> Result<(), CodecError> {
        self.encoder
            .set_inband_fec(enabled)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        self.fec_enabled = enabled;
        Ok(())
    }

    /// 设置预期丢包率
    ///
    /// # 参数
    /// * `loss_perc` - 预期丢包率 (0-100)
    pub fn set_packet_loss_perc(&mut self, loss_perc: i32) -> Result<(), CodecError> {
        let loss_perc = loss_perc.clamp(0, 100);
        self.encoder
            .set_packet_loss_perc(loss_perc)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        self.packet_loss_perc = loss_perc;
        Ok(())
    }

    /// 设置复杂度
    ///
    /// # 参数
    /// * `complexity` - 复杂度 (1-10)，10 为最高
    pub fn set_complexity(&mut self, complexity: i32) -> Result<(), CodecError> {
        let complexity = complexity.clamp(1, 10);
        self.encoder
            .set_complexity(complexity)
            .map_err(|e| CodecError::Opus(e.to_string()))
    }

    /// 获取当前比特率
    pub fn get_bitrate(&self) -> i32 {
        self.bitrate
    }

    /// 是否启用 FEC
    pub fn is_fec_enabled(&self) -> bool {
        self.fec_enabled
    }

    /// 获取预期丢包率
    pub fn get_packet_loss_perc(&self) -> i32 {
        self.packet_loss_perc
    }

    /// 获取采样率
    pub fn sample_rate(&self) -> u32 {
        SAMPLE_RATE
    }

    /// 获取每帧采样数
    pub fn frame_samples(&self) -> usize {
        FRAME_SAMPLES
    }

    /// 获取声道数
    pub fn channels(&self) -> usize {
        CHANNELS
    }

    /// 根据网络状况自适应调整编码参数
    ///
    /// 与原有 C++ 实现的 UpdateEncoderParams 对应
    ///
    /// # 参数
    /// * `loss_perc` - 实际丢包率 (0-100)
    /// * `jitter_ms` - 抖动 (ms)
    pub fn adapt_to_network(&mut self, loss_perc: f32, jitter_ms: f32) {
        let loss_perc_clamped = loss_perc.clamp(0.0, 30.0) as i32;

        // 与原有 C++ 实现对应的自适应逻辑
        let (target_bitrate, target_loss, target_fec) = if loss_perc_clamped <= 2 && jitter_ms < 8.0
        {
            // 网络好: 高比特率，无 FEC
            (48000, 0, false)
        } else if loss_perc_clamped <= 5 {
            // 网络一般: 中等比特率，开启 FEC
            (32000, 5, true)
        } else if loss_perc_clamped <= 10 {
            // 网络较差: 降低比特率，开启 FEC
            (24000, 10, true)
        } else {
            // 网络很差: 最低比特率，开启 FEC
            (16000, 20, true)
        };

        // 应用新的参数
        if let Err(e) = self.set_bitrate(target_bitrate) {
            log::warn!(
                "Opus adapt: failed to set bitrate to {}: {}",
                target_bitrate,
                e
            );
        }
        if let Err(e) = self.set_packet_loss_perc(target_loss) {
            log::warn!(
                "Opus adapt: failed to set packet loss to {}: {}",
                target_loss,
                e
            );
        }
        if let Err(e) = self.set_fec(target_fec) {
            log::warn!("Opus adapt: failed to set FEC to {}: {}", target_fec, e);
        }
    }
}

impl Default for OpusCodec {
    fn default() -> Self {
        Self::new().expect("Failed to create Opus codec")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_decode_roundtrip() -> Result<(), CodecError> {
        // 使用 Audio 模式测试编解码质量
        // VoIP 模式对非语音信号（如正弦波）质量较低，Audio 模式能正确反映 libopus 的质量
        let mut encoder = Encoder::new(SAMPLE_RATE, Channels::Mono, Application::Audio)?;
        encoder.set_bitrate(Bitrate::Bits(DEFAULT_BITRATE))?;
        encoder.set_complexity(10)?;
        let mut decoder = Decoder::new(SAMPLE_RATE, Channels::Mono)?;

        // 生成测试信号 (440Hz 正弦波，多帧)
        let num_frames = 5;
        let total_samples = FRAME_SAMPLES * num_frames;
        let signal: Vec<i16> = (0..total_samples)
            .map(|i| {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / SAMPLE_RATE as f32;
                (phase.sin() * 16000.0) as i16
            })
            .collect();

        // 编码/解码多帧
        let mut all_decoded = Vec::with_capacity(total_samples);
        let mut encoded = vec![0u8; 1000];
        for frame_idx in 0..num_frames {
            let start = frame_idx * FRAME_SAMPLES;
            let pcm = &signal[start..start + FRAME_SAMPLES];
            let encoded_len = encoder.encode(pcm, &mut encoded)?;

            let mut decoded = vec![0i16; FRAME_SAMPLES];
            let samples = decoder.decode(&encoded[..encoded_len], &mut decoded, false)?;
            assert_eq!(samples, FRAME_SAMPLES);
            all_decoded.extend_from_slice(&decoded[..samples]);
        }

        // 使用互相关找到最佳对齐偏移
        // opus 编解码器有算法延迟，解码信号相对于原始信号有时间偏移
        let search_range = 512;
        let mut best_offset = 0i32;
        let mut best_corr = 0.0f64;

        for offset in -(search_range as i32)..=(search_range as i32) {
            let mut corr = 0.0f64;
            let start_orig = offset.max(0) as usize;
            let start_dec = offset.min(0).abs() as usize;
            let len = total_samples - offset.abs() as usize;
            if len <= 0 {
                continue;
            }
            for i in 0..len {
                let o = signal[start_orig + i] as f64;
                let d = all_decoded[start_dec + i] as f64;
                corr += o * d;
            }
            if corr > best_corr {
                best_corr = corr;
                best_offset = offset;
            }
        }

        // 使用最佳偏移计算 SNR
        let start_orig = best_offset.max(0) as usize;
        let start_dec = best_offset.min(0).abs() as usize;
        let len = total_samples - best_offset.abs() as usize;

        let mut sum_sq_diff = 0.0f64;
        let mut sum_sq_orig = 0.0f64;
        for i in 0..len {
            let orig = signal[start_orig + i] as f64;
            let dec = all_decoded[start_dec + i] as f64;
            let diff = orig - dec;
            sum_sq_diff += diff * diff;
            sum_sq_orig += orig * orig;
        }

        let snr_db = 10.0 * (sum_sq_orig / sum_sq_diff).log10();

        // libopus 官方实现应达到 20dB 以上
        assert!(
            snr_db > 20.0,
            "SNR too low: {} dB (best_offset: {})",
            snr_db,
            best_offset
        );
        Ok(())
    }

    #[test]
    fn test_packet_loss_concealment() {
        let mut codec = OpusCodec::new().unwrap();

        // 先编码一帧
        let samples: Vec<i16> = (0..FRAME_SAMPLES).map(|i| (i % 100) as i16).collect();
        let mut encoded = vec![0u8; 1000];
        let _ = codec.encode(&samples, &mut encoded).unwrap();

        // PLC 解码 (无输入数据)
        let mut concealed = vec![0i16; FRAME_SAMPLES];
        let result = codec.decode_plc(&mut concealed);
        assert!(result.is_ok());
        assert!(result.unwrap() > 0);
    }

    #[test]
    fn test_fec_decode() {
        let mut codec = OpusCodec::new().unwrap();
        codec.set_fec(true).unwrap();

        let frame1: Vec<i16> = (0..FRAME_SAMPLES).map(|i| (i % 100) as i16).collect();
        let frame2: Vec<i16> = (0..FRAME_SAMPLES)
            .map(|i| ((i + 50) % 100) as i16)
            .collect();

        let mut enc1 = vec![0u8; 1000];
        let mut enc2 = vec![0u8; 1000];
        let _ = codec.encode(&frame1, &mut enc1).unwrap();
        let _ = codec.encode(&frame2, &mut enc2).unwrap();

        // 使用 FEC 从 frame2 恢复 frame1
        let mut recovered = vec![0i16; FRAME_SAMPLES];
        let result = codec.decode_fec(&enc2, &mut recovered);
        assert!(result.is_ok());
    }

    #[test]
    fn test_bitrate_change() {
        let mut codec = OpusCodec::new().unwrap();

        codec.set_bitrate(16000).unwrap();
        assert_eq!(codec.get_bitrate(), 16000);

        codec.set_bitrate(48000).unwrap();
        assert_eq!(codec.get_bitrate(), 48000);
    }

    #[test]
    fn test_fec() {
        let mut codec = OpusCodec::new().unwrap();

        assert!(!codec.is_fec_enabled());

        codec.set_fec(true).unwrap();
        assert!(codec.is_fec_enabled());

        codec.set_fec(false).unwrap();
        assert!(!codec.is_fec_enabled());
    }

    #[test]
    fn test_packet_loss_perc() {
        let mut codec = OpusCodec::new().unwrap();

        assert_eq!(codec.get_packet_loss_perc(), 0);

        codec.set_packet_loss_perc(10).unwrap();
        assert_eq!(codec.get_packet_loss_perc(), 10);

        codec.set_packet_loss_perc(50).unwrap();
        assert_eq!(codec.get_packet_loss_perc(), 50);
    }

    #[test]
    fn test_codec_creation() {
        let codec = OpusCodec::new().unwrap();
        assert_eq!(codec.get_bitrate(), DEFAULT_BITRATE);
        assert_eq!(codec.sample_rate(), 48000);
        assert_eq!(codec.frame_samples(), 960);
        assert_eq!(codec.channels(), 1);
    }

    #[test]
    fn test_adapt_to_network() {
        let mut codec = OpusCodec::new().unwrap();

        // 网络好
        codec.adapt_to_network(0.0, 5.0);
        assert_eq!(codec.get_bitrate(), 48000);
        assert!(!codec.is_fec_enabled());

        // 网络一般
        codec.adapt_to_network(3.0, 10.0);
        assert_eq!(codec.get_bitrate(), 32000);
        assert!(codec.is_fec_enabled());

        // 网络较差
        codec.adapt_to_network(8.0, 20.0);
        assert_eq!(codec.get_bitrate(), 24000);
        assert!(codec.is_fec_enabled());

        // 网络很差
        codec.adapt_to_network(15.0, 50.0);
        assert_eq!(codec.get_bitrate(), 16000);
        assert!(codec.is_fec_enabled());
    }

    #[test]
    fn test_decode_plc() {
        let mut codec = OpusCodec::new().unwrap();

        // 先编码一帧，让解码器有参考数据
        let input: Vec<i16> = (0..960)
            .map(|i| {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
                (phase.sin() * 16000.0) as i16
            })
            .collect();
        let mut encoded = vec![0u8; 1000];
        let encoded_len = codec.encode(&input, &mut encoded).unwrap();

        // 正常解码
        let mut decoded = vec![0i16; 960];
        codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();

        // PLC 解码（丢包隐藏）
        let mut plc_output = vec![0i16; 960];
        let plc_result = codec.decode_plc(&mut plc_output);
        assert!(plc_result.is_ok());
        assert_eq!(plc_result.unwrap(), 960);
    }
}
