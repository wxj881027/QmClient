//! 网络传输模块
//!
//! 实现 RV01 语音协议，基于 UDP 传输

pub mod protocol;
pub mod client;

pub use protocol::{VoicePacket, PacketType, VOICE_MAGIC, VOICE_VERSION, VOICE_HEADER_SIZE};
pub use client::VoiceClient;
