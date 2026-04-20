//! 网络传输模块
//!
//! 实现 RV01 语音协议，基于 UDP 传输
//!
//! ## 模块结构
//!
//! - `protocol`: RV01 协议实现，包含包格式、序列化/反序列化
//! - `client`: UDP 客户端，连接服务器、发送/接收音频包
//! - `server`: UDP 服务器，接收客户端音频并广播
//! - `reliable`: 可靠传输层，序列号管理、丢包检测、FEC 支持

pub mod bandwidth_estimator;
pub mod client;
pub mod protocol;
pub mod reliable;
pub mod server;

// 重导出常用类型
pub use protocol::{
    calculate_context_hash, calculate_token_hash, flags, pack_token, sanitize_float, seq_delta,
    seq_less, should_hear, token_group, token_mode, NetworkStats, PacketType, ParseError,
    PingTracker, VoicePacket, VOICE_FRAME_BYTES, VOICE_FRAME_SAMPLES, VOICE_GROUP_MASK,
    VOICE_HEADER_SIZE, VOICE_MAGIC, VOICE_MAX_PACKET, VOICE_MAX_PAYLOAD, VOICE_MODE_MASK,
    VOICE_MODE_SHIFT, VOICE_SAMPLE_RATE, VOICE_VERSION,
};

pub use client::{
    ClientConfig, ClientError, ClientState, ReceivedAudio, SimpleUdpClient, VoiceClient,
    MAX_CLIENTS,
};

pub use server::{
    ClientInfo, ServerConfig, ServerError, ServerState, ServerStats, SimpleUdpServer, VoiceServer,
    DEFAULT_PORT,
};

pub use bandwidth_estimator::{
    BandwidthEstimator, BandwidthEstimatorConfig, BandwidthUsage,
};

pub use reliable::{
    AdaptiveBitrateController, FecConfig, JitterBuffer, JitterPacket, LossDetector,
    SequenceManager, MAX_FRAMES, MAX_JITTER_PACKETS,
};
