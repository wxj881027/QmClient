//! Opus 编解码器的 Mock 实现
//!
//! 用于实验性构建，实际使用时应替换为真正的 opus crate

use thiserror::Error;

/// 编解码错误
#[derive(Debug, Error)]
pub enum Error {
    #[error("Invalid sample rate")]
    InvalidSampleRate,
    #[error("Invalid channels")]
    InvalidChannels,
    #[error("Buffer too small")]
    BufferTooSmall,
    #[error("Internal error: {0}")]
    InternalError(i32),
}

/// Opus 应用模式
#[derive(Debug, Clone, Copy)]
pub enum Application {
    Voip = 2048,
    Audio = 2049,
    LowDelay = 2051,
}

/// 比特率设置
#[derive(Debug, Clone, Copy)]
pub enum Bitrate {
    /// 默认比特率
    Auto,
    /// 最大比特率
    Max,
    /// 指定比特率 (bps)
    Bits(i32),
}

/// 声道设置
#[derive(Debug, Clone, Copy)]
pub enum Channels {
    Mono = 1,
    Stereo = 2,
}

/// Opus 编码器
pub struct Encoder {
    sample_rate: u32,
    channels: Channels,
    bitrate: i32,
    fec_enabled: bool,
}

impl Encoder {
    /// 创建新的编码器
    pub fn new(sample_rate: u32, channels: Channels, application: Application) -> Result<Self, Error> {
        if sample_rate != 8000 && sample_rate != 12000 && sample_rate != 16000 
            && sample_rate != 24000 && sample_rate != 48000 {
            return Err(Error::InvalidSampleRate);
        }

        Ok(Self {
            sample_rate,
            channels,
            bitrate: 24000,
            fec_enabled: false,
        })
    }

    /// 编码一帧
    pub fn encode(&self, pcm: &[i16], output: &mut [u8]) -> Result<usize, Error> {
        // Mock 实现：简单复制 PCM 数据到输出
        // 实际应该进行 Opus 编码
        let max_encoded = output.len().min(pcm.len() * 2);
        
        // 简单的 PCM -> 字节转换 (无压缩)
        for (i, &sample) in pcm.iter().enumerate() {
            if i * 2 + 1 >= max_encoded {
                break;
            }
            output[i * 2] = (sample & 0xFF) as u8;
            output[i * 2 + 1] = ((sample >> 8) & 0xFF) as u8;
        }

        Ok(max_encoded)
    }

    /// 设置比特率
    pub fn set_bitrate(&mut self, bitrate: Bitrate) -> Result<(), Error> {
        self.bitrate = match bitrate {
            Bitrate::Auto => 24000,
            Bitrate::Max => 510000,
            Bitrate::Bits(b) => b.clamp(6000, 510000),
        };
        Ok(())
    }

    /// 获取比特率
    pub fn get_bitrate(&self) -> Result<Bitrate, Error> {
        Ok(Bitrate::Bits(self.bitrate))
    }

    /// 设置 FEC
    pub fn set_inband_fec(&mut self, enabled: bool) -> Result<(), Error> {
        self.fec_enabled = enabled;
        Ok(())
    }

    /// 重置状态
    pub fn reset_state(&mut self) -> Result<(), Error> {
        Ok(())
    }
}

/// Opus 解码器
pub struct Decoder {
    sample_rate: u32,
    channels: Channels,
}

impl Decoder {
    /// 创建新的解码器
    pub fn new(sample_rate: u32, channels: Channels) -> Result<Self, Error> {
        if sample_rate != 8000 && sample_rate != 12000 && sample_rate != 16000 
            && sample_rate != 24000 && sample_rate != 48000 {
            return Err(Error::InvalidSampleRate);
        }

        Ok(Self {
            sample_rate,
            channels,
        })
    }

    /// 解码一帧
    pub fn decode(&self, opus_data: &[u8], pcm: &mut [i16], fec: bool) -> Result<usize, Error> {
        // Mock 实现：简单复制字节到 PCM
        // 实际应该进行 Opus 解码
        let samples = opus_data.len() / 2;
        
        for i in 0..samples.min(pcm.len()) {
            if i * 2 + 1 < opus_data.len() {
                pcm[i] = (opus_data[i * 2] as i16) | ((opus_data[i * 2 + 1] as i16) << 8);
            }
        }

        Ok(samples.min(pcm.len()))
    }

    /// 重置状态
    pub fn reset_state(&mut self) -> Result<(), Error> {
        Ok(())
    }
}
