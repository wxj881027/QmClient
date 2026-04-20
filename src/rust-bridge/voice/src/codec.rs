//! Opus 编解码模块
//!
//! 提供 Opus 音频编解码功能，支持：
//! - 编码/解码
//! - 自适应比特率
//! - FEC (前向纠错)
//! - PLC (丢包隐藏)
//!
//! # Examples
//!
//! ```
//! use ddnet_voice::codec::OpusCodec;
//!
//! // 创建 Opus 编解码器
//! let mut codec = OpusCodec::new()?;
//!
//! // 生成示例音频帧 (960 采样 = 20ms @ 48kHz)
//! let pcm_samples: Vec<i16> = (0..960)
//!     .map(|i| {
//!         let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
//!         (phase.sin() * 16000.0) as i16  // 440Hz 正弦波
//!     })
//!     .collect();
//!
//! // 编码一帧音频
//! let mut encoded = vec![0u8; 1000];
//! let encoded_len = codec.encode(&pcm_samples, &mut encoded)?;
//!
//! // 解码音频帧
//! let mut decoded = vec![0i16; 960];
//! let decoded_samples = codec.decode(&encoded[..encoded_len], &mut decoded)?;
//!
//! assert_eq!(decoded_samples, 960);
//! # Ok::<(), ddnet_voice::codec::opus::CodecError>(())
//! ```

pub mod adaptive;
pub mod opus;

pub use adaptive::AdaptiveBitrate;
pub use opus::OpusCodec;
