//! Opus 编解码模块
//!
//! 提供 Opus 音频编解码功能，支持：
//! - 编码/解码
//! - 自适应比特率
//! - FEC (前向纠错)
//! - PLC (丢包隐藏)

pub mod adaptive;
pub mod opus;

pub use adaptive::AdaptiveBitrate;
pub use opus::OpusCodec;
