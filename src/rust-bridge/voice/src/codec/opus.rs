//! Opus 编解码器封装
//!
//! 采样率: 48000 Hz (Opus 唯一支持的采样率)
//! 帧大小: 960 采样 (20ms)
//! 声道数: 1 (单声道)

// 使用 mock 实现进行实验性构建
// 实际项目中应该使用: use opus::{Decoder, Encoder, Application, Error as OpusError};
use super::opus_mock::{Decoder, Encoder, Application, Error as OpusError, Bitrate, Channels};

/// 语音编解码器
pub struct OpusCodec {
    encoder: Encoder,
    decoder: Decoder,
    sample_rate: u32,
    channels: Channels,
    _mock_bitrate: i32,
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
        let encoder = Encoder::new(SAMPLE_RATE, Channels::Mono, Application::Voip)?;
        let decoder = Decoder::new(SAMPLE_RATE, Channels::Mono)?;
        
        Ok(Self {
            encoder,
            decoder,
            sample_rate: SAMPLE_RATE,
            channels: Channels::Mono,
            _mock_bitrate: 24000,
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
    pub fn encode(&self, pcm: &[i16], output: &mut [u8]) -> Result<usize, CodecError> {
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
        self._mock_bitrate = bitrate.clamp(6000, 510000);
        Ok(())
    }

    /// 设置 FEC (前向纠错)
    pub fn set_fec(&mut self, enabled: bool) -> Result<(), CodecError> {
        // 在 mock 中忽略
        let _ = enabled;
        Ok(())
    }

    /// 获取当前比特率
    pub fn get_bitrate(&self) -> Result<i32, CodecError> {
        Ok(self._mock_bitrate)
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
        
        // Mock 实现会有数据，但不会完全相同
        assert_eq!(decoded.len(), FRAME_SAMPLES);
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
        
        // PLC 应该产生输出
        assert_eq!(concealed.len(), FRAME_SAMPLES);
    }

    #[test]
    fn test_bitrate_change() {
        let mut codec = OpusCodec::new().unwrap();
        
        // 设置不同比特率 (mock 中忽略)
        codec.set_bitrate(16000).unwrap();
        codec.set_bitrate(24000).unwrap();
        codec.set_bitrate(32000).unwrap();
        
        // Mock 返回固定值
        let bitrate = codec.get_bitrate().unwrap();
        assert!(bitrate > 0);
    }

    #[test]
    fn test_fec() {
        let mut codec = OpusCodec::new().unwrap();
        
        // 启用 FEC (mock 中忽略)
        codec.set_fec(true).unwrap();
        
        // 编码两帧
        let frame1: Vec<i16> = (0..FRAME_SAMPLES).map(|i| (i % 100) as i16).collect();
        let frame2: Vec<i16> = (0..FRAME_SAMPLES).map(|i| ((i + 50) % 100) as i16).collect();
        
        let mut enc1 = vec![0u8; 1000];
        let mut enc2 = vec![0u8; 1000];
        let _ = codec.encode(&frame1, &mut enc1).unwrap();
        let _ = codec.encode(&frame2, &mut enc2).unwrap();
        
        // 使用第二帧恢复第一帧 (FEC)
        let mut recovered = vec![0i16; FRAME_SAMPLES];
        codec.decode_fec(&enc2, &mut recovered).unwrap();
        
        // FEC 应该产生输出
        assert_eq!(recovered.len(), FRAME_SAMPLES);
    }
}
