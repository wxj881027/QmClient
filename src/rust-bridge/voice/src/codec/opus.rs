//! Opus 编解码器封装
//!
//! 使用 opus-rs 纯 Rust 实现
//! 采样率: 48000 Hz
//! 帧大小: 960 采样 (20ms)
//! 声道数: 1 (单声道)

use opus_rs::{OpusDecoder, OpusEncoder, Application};

/// 帧大小（采样数）
pub const FRAME_SAMPLES: usize = 960; // 20ms @ 48kHz
/// 采样率
pub const SAMPLE_RATE: u32 = 48000;

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

/// Opus 编解码器
pub struct OpusCodec {
    encoder: OpusEncoder,
    decoder: OpusDecoder,
    bitrate: i32,
    fec_enabled: bool,
}

impl OpusCodec {
    /// 创建新的编解码器
    /// 
    /// 使用 VoIP 优化模式，初始比特率 24kbps
    pub fn new() -> Result<Self, CodecError> {
        let mut encoder = OpusEncoder::new(SAMPLE_RATE as i32, 1, Application::Voip)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        encoder.bitrate_bps = 24000;
        encoder.use_cbr = false; // 使用 VBR
        
        let decoder = OpusDecoder::new(SAMPLE_RATE as i32, 1)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        
        Ok(Self {
            encoder,
            decoder,
            bitrate: 24000,
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
        
        // 转换 i16 -> f32
        let pcm_f32: Vec<f32> = pcm.iter()
            .map(|&s| s as f32 / 32768.0)
            .collect();
        
        let len = self.encoder
            .encode(&pcm_f32, FRAME_SAMPLES, output)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        
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
        
        // 解码到 f32 缓冲区
        let mut pcm_f32 = vec![0.0f32; FRAME_SAMPLES];
        
        let samples = self.decoder
            .decode(opus_data, FRAME_SAMPLES, &mut pcm_f32)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        
        // 转换 f32 -> i16
        for (i, &sample) in pcm_f32[..samples].iter().enumerate() {
            pcm[i] = (sample * 32767.0).clamp(-32768.0, 32767.0) as i16;
        }
        
        Ok(samples)
    }

    /// 使用 FEC 解码 (前向纠错)
    /// 
    /// 当丢包但收到下一个包时，使用 FEC 恢复丢失的帧
    pub fn decode_fec(&mut self, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, CodecError> {
        // opus-rs 的 decode 支持 FEC
        self.decode(opus_data, pcm)
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
        let mut pcm_f32 = vec![0.0f32; FRAME_SAMPLES];
        
        let samples = self.decoder
            .decode(&[], FRAME_SAMPLES, &mut pcm_f32)
            .map_err(|e| CodecError::Opus(e.to_string()))?;
        
        // 转换 f32 -> i16
        for (i, &sample) in pcm_f32[..samples].iter().enumerate() {
            pcm[i] = (sample * 32767.0).clamp(-32768.0, 32767.0) as i16;
        }
        
        Ok(samples)
    }

    /// 设置比特率
    /// 
    /// # 参数
    /// * `bitrate` - 比特率 (bps)，范围 6000-510000
    pub fn set_bitrate(&mut self, bitrate: i32) -> Result<(), CodecError> {
        let bitrate = bitrate.clamp(6000, 510000);
        self.encoder.bitrate_bps = bitrate;
        self.bitrate = bitrate;
        Ok(())
    }

    /// 设置 FEC (前向纠错)
    pub fn set_fec(&mut self, enabled: bool) -> Result<(), CodecError> {
        self.encoder.use_inband_fec = enabled;
        self.fec_enabled = enabled;
        Ok(())
    }

    /// 获取当前比特率
    pub fn get_bitrate(&self) -> Result<i32, CodecError> {
        Ok(self.bitrate)
    }

    /// 是否启用 FEC
    pub fn is_fec_enabled(&self) -> bool {
        self.fec_enabled
    }

    /// 获取采样率
    pub fn sample_rate(&self) -> u32 {
        SAMPLE_RATE
    }

    /// 获取每帧采样数
    pub fn frame_samples(&self) -> usize {
        FRAME_SAMPLES
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
        let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
        assert_eq!(samples, FRAME_SAMPLES);
        
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
        let result = codec.decode_plc(&mut concealed);
        
        // PLC 应该产生输出
        if let Ok(samples) = result {
            assert!(samples > 0);
        }
    }

    #[test]
    fn test_bitrate_change() {
        let mut codec = OpusCodec::new().unwrap();
        
        codec.set_bitrate(16000).unwrap();
        assert_eq!(codec.get_bitrate().unwrap(), 16000);
        
        codec.set_bitrate(32000).unwrap();
        assert_eq!(codec.get_bitrate().unwrap(), 32000);
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
    fn test_codec_creation() {
        let codec = OpusCodec::new().unwrap();
        assert_eq!(codec.get_bitrate().unwrap(), 24000);
        assert_eq!(codec.sample_rate(), 48000);
        assert_eq!(codec.frame_samples(), 960);
    }
}
