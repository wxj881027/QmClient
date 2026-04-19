//! Opus 编解码器封装
//!
//! 使用 opus crate 进行音频编解码
//! 采样率: 48000 Hz
//! 帧大小: 960 采样 (20ms)
//! 声道数: 1 (单声道)

use opus::{Decoder, Encoder, Application, Bitrate, Channels, Error as OpusError};

/// 语音编解码器
pub struct OpusCodec {
    encoder: Encoder,
    decoder: Decoder,
    sample_rate: u32,
    channels: Channels,
    current_bitrate: i32,
    fec_enabled: bool,
}

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

impl From<OpusError> for CodecError {
    fn from(e: OpusError) -> Self {
        CodecError::Opus(e.to_string())
    }
}

/// 帧大小（采样数）
pub const FRAME_SAMPLES: usize = 960; // 20ms @ 48kHz
/// 采样率
pub const SAMPLE_RATE: u32 = 48000;

impl OpusCodec {
    /// 创建新的编解码器
    /// 
    /// 使用 VoIP 优化模式，初始比特率 24kbps
    pub fn new() -> Result<Self, CodecError> {
        let sample_rate = SAMPLE_RATE;
        let channels = Channels::Mono;
        
        let mut encoder = Encoder::new(sample_rate, channels, Application::Voip)?;
        encoder.set_bitrate(Bitrate::Bits(24000))?;
        
        let decoder = Decoder::new(sample_rate, channels)?;
        
        Ok(Self {
            encoder,
            decoder,
            sample_rate,
            channels,
            current_bitrate: 24000,
            fec_enabled: false,
        })
    }

    /// 编码一帧音频
    /// 
    /// # 参数
    /// * `pcm` - PCM 样本 (960 samples = 20ms)
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
        
        let len = self.encoder.encode(pcm, output)?;
        Ok(len)
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
        
        let samples = self.decoder.decode(opus_data, pcm, false)?;
        Ok(samples)
    }

    /// 使用 FEC 解码 (前向纠错)
    /// 
    /// 当丢包但收到下一个包时，使用 FEC 恢复丢失的帧
    pub fn decode_fec(&mut self, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, CodecError> {
        if pcm.len() < FRAME_SAMPLES {
            return Err(CodecError::BufferTooSmall {
                needed: FRAME_SAMPLES,
                have: pcm.len(),
            });
        }
        
        let samples = self.decoder.decode(opus_data, pcm, true)?;
        Ok(samples)
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
        
        // 传入空数据触发 PLC
        let samples = self.decoder.decode(&[], pcm, true)?;
        Ok(samples)
    }

    /// 设置比特率
    /// 
    /// # 参数
    /// * `bitrate` - 比特率 (bps)，范围 6000-510000
    pub fn set_bitrate(&mut self, bitrate: i32) -> Result<(), CodecError> {
        let bitrate = bitrate.clamp(6000, 510000);
        self.encoder.set_bitrate(Bitrate::Bits(bitrate))?;
        self.current_bitrate = bitrate;
        Ok(())
    }

    /// 设置 FEC (前向纠错)
    pub fn set_fec(&mut self, enabled: bool) -> Result<(), CodecError> {
        self.encoder.set_inband_fec(enabled)?;
        self.fec_enabled = enabled;
        Ok(())
    }

    /// 获取当前比特率
    pub fn get_bitrate(&self) -> Result<i32, CodecError> {
        Ok(self.current_bitrate)
    }

    /// 重置编码器状态
    pub fn reset_encoder(&mut self) -> Result<(), CodecError> {
        self.encoder.reset_state()?;
        Ok(())
    }

    /// 重置解码器状态
    pub fn reset_decoder(&mut self) -> Result<(), CodecError> {
        self.decoder.reset_state()?;
        Ok(())
    }

    /// 获取采样率
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// 获取每帧采样数
    pub fn frame_samples(&self) -> usize {
        FRAME_SAMPLES
    }

    /// 是否启用 FEC
    pub fn is_fec_enabled(&self) -> bool {
        self.fec_enabled
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
    fn test_encode_decode_roundtrip() {
        let mut codec = OpusCodec::new().unwrap();
        
        // 生成测试信号 (1kHz 正弦波)
        let original: Vec<i16> = (0..FRAME_SAMPLES)
            .map(|i| {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * 1000.0 / SAMPLE_RATE as f32;
                (phase.sin() * 16000.0) as i16
            })
            .collect();
        
        // 编码
        let mut encoded = vec![0u8; 1000];
        let encoded_len = codec.encode(&original, &mut encoded).unwrap();
        assert!(encoded_len > 0);
        
        // 解码
        let mut decoded = vec![0i16; FRAME_SAMPLES];
        codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
        
        // 计算 SNR
        let mut sum_sq_diff = 0.0f64;
        let mut sum_sq_orig = 0.0f64;
        for (orig, dec) in original.iter().zip(decoded.iter()) {
            let diff = *orig as f64 - *dec as f64;
            sum_sq_diff += diff * diff;
            sum_sq_orig += (*orig as f64) * (*orig as f64);
        }
        
        let snr_db = 10.0 * (sum_sq_orig / sum_sq_diff).log10();
        
        // Opus 在 24kbps 应该有 > 15dB SNR
        assert!(snr_db > 15.0, "SNR too low: {} dB", snr_db);
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
        codec.decode_plc(&mut concealed).unwrap();
        
        // PLC 应该产生平滑的音频，不是静音
        let energy: f32 = concealed.iter().map(|&s| (s as f32).powi(2)).sum::<f32>() / FRAME_SAMPLES as f32;
        assert!(energy > 0.0, "PLC should produce non-silent output");
    }

    #[test]
    fn test_bitrate_change() {
        let mut codec = OpusCodec::new().unwrap();
        
        // 设置不同比特率
        codec.set_bitrate(16000).unwrap();
        assert_eq!(codec.get_bitrate().unwrap(), 16000);
        
        codec.set_bitrate(32000).unwrap();
        assert_eq!(codec.get_bitrate().unwrap(), 32000);
    }

    #[test]
    fn test_fec() {
        let mut codec = OpusCodec::new().unwrap();
        
        // 默认不启用 FEC
        assert!(!codec.is_fec_enabled());
        
        // 启用 FEC
        codec.set_fec(true).unwrap();
        assert!(codec.is_fec_enabled());
        
        // 禁用 FEC
        codec.set_fec(false).unwrap();
        assert!(!codec.is_fec_enabled());
    }
}
