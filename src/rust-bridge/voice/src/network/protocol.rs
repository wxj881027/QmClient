//! RV01 语音协议实现
//!
//! 协议格式：
//!
//! | 字段 | 大小 | 说明 |
//! |------|------|------|
//! | Magic | 4 | 魔数 "RV01" |
//! | Version | 1 | 协议版本 |
//! | Type | 1 | 包类型 |
//! | PayloadSize | 2 | 负载大小 |
//! | ContextHash | 4 | 上下文哈希 |
//! | TokenHash | 4 | Token 哈希 |
//! | Flags | 1 | 标志位 |
//! | SenderId | 2 | 发送者 ID |
//! | Sequence | 2 | 序列号 |
//! | PosX | 4 | X 坐标 |
//! | PosY | 4 | Y 坐标 |
//! | Payload | 变长 | Opus 数据 |
//!
//! 参考 C++ 实现: voice_core.cpp

use std::time::{Duration, Instant};

/// 协议魔数
pub const VOICE_MAGIC: &[u8; 4] = b"RV01";
/// 协议版本
pub const VOICE_VERSION: u8 = 3;
/// 最大包大小
pub const VOICE_MAX_PACKET: usize = 1200;

// 头部字段偏移量常量（用于边界检查）
/// Magic 字段偏移量
pub const OFFSET_MAGIC: usize = 0;
/// Version 字段偏移量
pub const OFFSET_VERSION: usize = 4;
/// Type 字段偏移量
pub const OFFSET_TYPE: usize = 5;
/// PayloadSize 字段偏移量
pub const OFFSET_PAYLOAD_SIZE: usize = 6;
/// ContextHash 字段偏移量
pub const OFFSET_CONTEXT_HASH: usize = 8;
/// TokenHash 字段偏移量
pub const OFFSET_TOKEN_HASH: usize = 12;
/// Flags 字段偏移量
pub const OFFSET_FLAGS: usize = 16;
/// SenderId 字段偏移量
pub const OFFSET_SENDER_ID: usize = 17;
/// Sequence 字段偏移量
pub const OFFSET_SEQUENCE: usize = 19;
/// PosX 字段偏移量
pub const OFFSET_POS_X: usize = 21;
/// PosY 字段偏移量
pub const OFFSET_POS_Y: usize = 25;

// 字段大小常量
/// Magic 字段大小
pub const SIZE_MAGIC: usize = 4;
/// Version 字段大小
pub const SIZE_VERSION: usize = 1;
/// Type 字段大小
pub const SIZE_TYPE: usize = 1;
/// PayloadSize 字段大小
pub const SIZE_PAYLOAD_SIZE: usize = 2;
/// ContextHash 字段大小
pub const SIZE_CONTEXT_HASH: usize = 4;
/// TokenHash 字段大小
pub const SIZE_TOKEN_HASH: usize = 4;
/// Flags 字段大小
pub const SIZE_FLAGS: usize = 1;
/// SenderId 字段大小
pub const SIZE_SENDER_ID: usize = 2;
/// Sequence 字段大小
pub const SIZE_SEQUENCE: usize = 2;
/// PosX 字段大小
pub const SIZE_POS_X: usize = 4;
/// PosY 字段大小
pub const SIZE_POS_Y: usize = 4;

/// 头部大小 (Magic + Version + Type + PayloadSize + ContextHash + TokenHash + Flags + SenderId + Sequence + PosX + PosY)
pub const VOICE_HEADER_SIZE: usize = SIZE_MAGIC
    + SIZE_VERSION
    + SIZE_TYPE
    + SIZE_PAYLOAD_SIZE
    + SIZE_CONTEXT_HASH
    + SIZE_TOKEN_HASH
    + SIZE_FLAGS
    + SIZE_SENDER_ID
    + SIZE_SEQUENCE
    + SIZE_POS_X
    + SIZE_POS_Y;

// 编译期验证头部大小
const _: () = assert!(
    VOICE_HEADER_SIZE == 29,
    "VOICE_HEADER_SIZE must be 29 bytes (4+1+1+2+4+4+1+2+2+4+4)"
);

/// 最大负载大小
pub const VOICE_MAX_PAYLOAD: usize = VOICE_MAX_PACKET - VOICE_HEADER_SIZE;
/// 音频采样率
pub const VOICE_SAMPLE_RATE: u32 = 48000;
/// 音频帧样本数
pub const VOICE_FRAME_SAMPLES: usize = 960;
/// 音频帧字节数
pub const VOICE_FRAME_BYTES: usize = VOICE_FRAME_SAMPLES * 2;
/// Token 组掩码
pub const VOICE_GROUP_MASK: u32 = 0x3FFFFFFF;
/// 模式位移
pub const VOICE_MODE_SHIFT: u32 = 30;
/// 模式掩码
pub const VOICE_MODE_MASK: u32 = 0x3;

/// 包类型
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketType {
    /// 音频数据包
    Audio = 1,
    /// 心跳/Keepalive 包
    Ping = 2,
    /// 心跳回复
    Pong = 3,
}

/// 标志位
pub mod flags {
    /// VAD (语音激活检测) 标志
    pub const VAD: u8 = 1 << 0;
    /// 服务器回环测试模式
    pub const LOOPBACK: u8 = 1 << 1;
}

/// 语音包
#[derive(Debug, Clone)]
pub struct VoicePacket {
    pub version: u8,
    pub packet_type: PacketType,
    pub payload_size: u16,
    pub context_hash: u32,
    pub token_hash: u32,
    pub flags: u8,
    pub sender_id: u16,
    pub sequence: u16,
    pub pos_x: f32,
    pub pos_y: f32,
    pub opus_payload: Vec<u8>,
}

/// 解析错误
#[derive(Debug, thiserror::Error)]
pub enum ParseError {
    #[error("Data too short: {0} bytes, need at least {1}")]
    TooShort(usize, usize),
    #[error("Invalid magic: expected RV01, got {0:?}")]
    InvalidMagic([u8; 4]),
    #[error("Invalid packet type: {0}")]
    InvalidType(u8),
    #[error("Invalid version: {0}, expected {1}")]
    InvalidVersion(u8, u8),
    #[error("Payload too large: {0} bytes, max {1}")]
    PayloadTooLarge(u16, usize),
    #[error("Payload size mismatch: header says {0}, actual {1}")]
    PayloadSizeMismatch(u16, usize),
}

impl VoicePacket {
    /// 创建新的音频包
    pub fn new_audio(
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
        pos_x: f32,
        pos_y: f32,
        opus_payload: Vec<u8>,
    ) -> Self {
        Self {
            version: VOICE_VERSION,
            packet_type: PacketType::Audio,
            payload_size: opus_payload.len() as u16,
            context_hash,
            token_hash,
            flags: 0,
            sender_id,
            sequence,
            pos_x,
            pos_y,
            opus_payload,
        }
    }

    /// 创建新的 PING 包
    pub fn new_ping(sender_id: u16, sequence: u16, context_hash: u32, token_hash: u32) -> Self {
        Self {
            version: VOICE_VERSION,
            packet_type: PacketType::Ping,
            payload_size: 0,
            context_hash,
            token_hash,
            flags: 0,
            sender_id,
            sequence,
            pos_x: 0.0,
            pos_y: 0.0,
            opus_payload: Vec::new(),
        }
    }

    /// 创建新的 PONG 包
    pub fn new_pong(sender_id: u16, sequence: u16, context_hash: u32, token_hash: u32) -> Self {
        Self {
            version: VOICE_VERSION,
            packet_type: PacketType::Pong,
            payload_size: 0,
            context_hash,
            token_hash,
            flags: 0,
            sender_id,
            sequence,
            pos_x: 0.0,
            pos_y: 0.0,
            opus_payload: Vec::new(),
        }
    }

    /// 解析网络包
    ///
    /// 安全性：每个字段读取前都验证剩余缓冲区大小，防止越界访问
    pub fn parse(data: &[u8]) -> Result<Self, ParseError> {
        // 辅助函数：安全读取指定偏移量的字节
        let read_byte = |offset: usize| -> Result<u8, ParseError> {
            data.get(offset)
                .copied()
                .ok_or(ParseError::TooShort(data.len(), offset + 1))
        };

        // 辅助函数：安全读取指定偏移量的 u16
        let read_u16 = |offset: usize| -> Result<u16, ParseError> {
            if data.len() < offset + 2 {
                return Err(ParseError::TooShort(data.len(), offset + 2));
            }
            Ok(u16::from_le_bytes([data[offset], data[offset + 1]]))
        };

        // 辅助函数：安全读取指定偏移量的 u32
        let read_u32 = |offset: usize| -> Result<u32, ParseError> {
            if data.len() < offset + 4 {
                return Err(ParseError::TooShort(data.len(), offset + 4));
            }
            Ok(u32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]))
        };

        // 辅助函数：安全读取指定偏移量的 f32
        let read_f32 = |offset: usize| -> Result<f32, ParseError> {
            if data.len() < offset + 4 {
                return Err(ParseError::TooShort(data.len(), offset + 4));
            }
            Ok(f32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]))
        };

        // 1. 验证最小头部大小
        if data.len() < VOICE_HEADER_SIZE {
            return Err(ParseError::TooShort(data.len(), VOICE_HEADER_SIZE));
        }

        // 2. 检查魔数 (4 字节)
        let magic = [
            read_byte(OFFSET_MAGIC)?,
            read_byte(OFFSET_MAGIC + 1)?,
            read_byte(OFFSET_MAGIC + 2)?,
            read_byte(OFFSET_MAGIC + 3)?,
        ];
        if &magic != VOICE_MAGIC {
            return Err(ParseError::InvalidMagic(magic));
        }

        // 3. 验证版本 (1 字节)
        let version = read_byte(OFFSET_VERSION)?;
        if version != VOICE_VERSION {
            return Err(ParseError::InvalidVersion(version, VOICE_VERSION));
        }

        // 4. 解析包类型 (1 字节)
        let packet_type = match read_byte(OFFSET_TYPE)? {
            1 => PacketType::Audio,
            2 => PacketType::Ping,
            3 => PacketType::Pong,
            t => return Err(ParseError::InvalidType(t)),
        };

        // 5. 读取 payload 大小 (2 字节)
        let payload_size = read_u16(OFFSET_PAYLOAD_SIZE)?;

        // 6. 验证 payload 大小不超过最大值（防止恶意构造的包）
        if payload_size as usize > VOICE_MAX_PAYLOAD {
            return Err(ParseError::PayloadTooLarge(payload_size, VOICE_MAX_PAYLOAD));
        }

        // 7. 验证数据长度足够包含 payload
        let expected_len = VOICE_HEADER_SIZE.saturating_add(payload_size as usize);
        if data.len() < expected_len {
            return Err(ParseError::PayloadSizeMismatch(
                payload_size,
                data.len().saturating_sub(VOICE_HEADER_SIZE),
            ));
        }

        // 8. 读取其他头部字段
        let context_hash = read_u32(OFFSET_CONTEXT_HASH)?;
        let token_hash = read_u32(OFFSET_TOKEN_HASH)?;
        let flags = read_byte(OFFSET_FLAGS)?;
        let sender_id = read_u16(OFFSET_SENDER_ID)?;
        let sequence = read_u16(OFFSET_SEQUENCE)?;
        let pos_x = read_f32(OFFSET_POS_X)?;
        let pos_y = read_f32(OFFSET_POS_Y)?;

        // 9. 安全读取 payload
        let opus_payload = if payload_size > 0 {
            // 已经验证了 data.len() >= VOICE_HEADER_SIZE + payload_size
            // 所以这里是安全的
            data[VOICE_HEADER_SIZE..VOICE_HEADER_SIZE + payload_size as usize].to_vec()
        } else {
            Vec::new()
        };

        Ok(Self {
            version,
            packet_type,
            payload_size,
            context_hash,
            token_hash,
            flags,
            sender_id,
            sequence,
            pos_x,
            pos_y,
            opus_payload,
        })
    }

    /// 序列化为字节
    ///
    /// 返回写入的字节数，如果缓冲区太小则返回 0
    ///
    /// 安全性：验证 payload 大小不超过 VOICE_MAX_PAYLOAD
    pub fn serialize(&self, buf: &mut [u8]) -> usize {
        // 验证 payload 大小
        if self.opus_payload.len() > VOICE_MAX_PAYLOAD {
            return 0;
        }

        let total_len = VOICE_HEADER_SIZE + self.opus_payload.len();

        if buf.len() < total_len {
            return 0; // 缓冲区太小
        }

        // 写入头部字段（使用偏移量常量）
        buf[OFFSET_MAGIC..OFFSET_MAGIC + SIZE_MAGIC].copy_from_slice(VOICE_MAGIC);
        buf[OFFSET_VERSION] = self.version;
        buf[OFFSET_TYPE] = self.packet_type as u8;
        buf[OFFSET_PAYLOAD_SIZE..OFFSET_PAYLOAD_SIZE + SIZE_PAYLOAD_SIZE]
            .copy_from_slice(&self.payload_size.to_le_bytes());
        buf[OFFSET_CONTEXT_HASH..OFFSET_CONTEXT_HASH + SIZE_CONTEXT_HASH]
            .copy_from_slice(&self.context_hash.to_le_bytes());
        buf[OFFSET_TOKEN_HASH..OFFSET_TOKEN_HASH + SIZE_TOKEN_HASH]
            .copy_from_slice(&self.token_hash.to_le_bytes());
        buf[OFFSET_FLAGS] = self.flags;
        buf[OFFSET_SENDER_ID..OFFSET_SENDER_ID + SIZE_SENDER_ID]
            .copy_from_slice(&self.sender_id.to_le_bytes());
        buf[OFFSET_SEQUENCE..OFFSET_SEQUENCE + SIZE_SEQUENCE]
            .copy_from_slice(&self.sequence.to_le_bytes());
        buf[OFFSET_POS_X..OFFSET_POS_X + SIZE_POS_X].copy_from_slice(&self.pos_x.to_le_bytes());
        buf[OFFSET_POS_Y..OFFSET_POS_Y + SIZE_POS_Y].copy_from_slice(&self.pos_y.to_le_bytes());

        // 写入 payload
        if !self.opus_payload.is_empty() {
            buf[VOICE_HEADER_SIZE..VOICE_HEADER_SIZE + self.opus_payload.len()]
                .copy_from_slice(&self.opus_payload);
        }

        total_len
    }

    /// 检查 VAD 标志
    pub fn is_vad(&self) -> bool {
        self.flags & flags::VAD != 0
    }

    /// 设置 VAD 标志
    pub fn set_vad(&mut self, vad: bool) {
        if vad {
            self.flags |= flags::VAD;
        } else {
            self.flags &= !flags::VAD;
        }
    }

    /// 检查回环标志
    pub fn is_loopback(&self) -> bool {
        self.flags & flags::LOOPBACK != 0
    }

    /// 设置回环标志
    pub fn set_loopback(&mut self, loopback: bool) {
        if loopback {
            self.flags |= flags::LOOPBACK;
        } else {
            self.flags &= !flags::LOOPBACK;
        }
    }

    /// 计算包总大小
    pub fn total_size(&self) -> usize {
        VOICE_HEADER_SIZE + self.opus_payload.len()
    }
}

/// 计算上下文哈希 (使用游戏服务器地址)
pub fn calculate_context_hash(server_addr: &str) -> u32 {
    // 简单的 djb2 哈希
    let mut hash: u32 = 5381;
    for byte in server_addr.bytes() {
        hash = hash.wrapping_mul(33).wrapping_add(byte as u32);
    }
    hash
}

/// 计算 Token 哈希
pub fn calculate_token_hash(token: &str) -> u32 {
    if token.is_empty() {
        return 0;
    }

    // djb2 哈希 + 模式位
    let mut hash: u32 = 5381;
    for byte in token.bytes() {
        hash = hash.wrapping_mul(33).wrapping_add(byte as u32);
    }

    // 保留低 30 位
    hash & VOICE_GROUP_MASK
}

/// 打包 Token (哈希 + 模式)
pub fn pack_token(group_hash: u32, mode: u32) -> u32 {
    (group_hash & VOICE_GROUP_MASK) | ((mode & VOICE_MODE_MASK) << VOICE_MODE_SHIFT)
}

/// 从 Token 提取组哈希
pub fn token_group(token_hash: u32) -> u32 {
    token_hash & VOICE_GROUP_MASK
}

/// 从 Token 提取模式
pub fn token_mode(token_hash: u32) -> u32 {
    (token_hash >> VOICE_MODE_SHIFT) & VOICE_MODE_MASK
}

/// 判断是否应该听到对方
///
/// 规则：
/// - 双方都是空 token (组 0): 同一个公共语音池
/// - 任何非空 token: 只有相同的 token 组才能互相听到
pub fn should_hear(sender_group: u32, receiver_group: u32) -> bool {
    sender_group == receiver_group
}

/// 序列号差值计算（处理回绕）
pub fn seq_delta(new_seq: u16, old_seq: u16) -> i32 {
    (new_seq as i16 - old_seq as i16) as i32
}

/// 序列号比较（处理回绕）
pub fn seq_less(a: u16, b: u16) -> bool {
    (a as i16).wrapping_sub(b as i16) < 0
}

/// 浮点数安全化（处理 NaN 和无穷大）
pub fn sanitize_float(value: f32) -> f32 {
    if value.is_nan() {
        return 0.0;
    }
    if value.is_infinite() {
        return if value.is_sign_positive() {
            1000000.0
        } else {
            -1000000.0
        };
    }
    value.clamp(-1000000.0, 1000000.0)
}

/// 网络统计信息
#[derive(Debug, Clone, Default)]
pub struct NetworkStats {
    /// 发送的包数
    pub packets_sent: u64,
    /// 接收的包数
    pub packets_recv: u64,
    /// 丢包数
    pub packets_lost: u64,
    /// 发送的字节数
    pub bytes_sent: u64,
    /// 接收的字节数
    pub bytes_recv: u64,
    /// 平均延迟 (毫秒)
    pub avg_rtt_ms: f32,
    /// 抖动 (毫秒)
    pub jitter_ms: f32,
    /// 最后更新时间
    pub last_update: Option<Instant>,
}

impl NetworkStats {
    /// 创建新的统计信息
    pub fn new() -> Self {
        Self::default()
    }

    /// 计算丢包率
    pub fn loss_rate(&self) -> f32 {
        let total = self.packets_sent + self.packets_lost;
        if total == 0 {
            0.0
        } else {
            self.packets_lost as f32 / total as f32
        }
    }

    /// 重置统计
    pub fn reset(&mut self) {
        *self = Self::default();
    }
}

/// Ping/Pong 状态追踪
#[derive(Debug, Clone)]
pub struct PingTracker {
    /// 最后发送的 Ping 序列号
    pub last_ping_seq: u16,
    /// 最后发送 Ping 的时间
    pub last_ping_time: Option<Instant>,
    /// 最后收到的 RTT (毫秒)
    pub last_rtt_ms: Option<u32>,
    /// RTT 历史（用于计算平均值）
    pub rtt_history: Vec<u32>,
    /// 最大历史记录数
    pub max_history: usize,
}

impl PingTracker {
    /// 创建新的 Ping 追踪器
    pub fn new() -> Self {
        Self {
            last_ping_seq: 0,
            last_ping_time: None,
            last_rtt_ms: None,
            rtt_history: Vec::with_capacity(10),
            max_history: 10,
        }
    }

    /// 记录发送 Ping
    pub fn send_ping(&mut self, seq: u16) {
        self.last_ping_seq = seq;
        self.last_ping_time = Some(Instant::now());
    }

    /// 记录收到 Pong
    pub fn recv_pong(&mut self, seq: u16) -> Option<u32> {
        if seq != self.last_ping_seq {
            return None;
        }

        if let Some(sent_time) = self.last_ping_time {
            let rtt = sent_time.elapsed().as_millis() as u32;
            self.last_rtt_ms = Some(rtt);

            // 更新历史
            self.rtt_history.push(rtt);
            if self.rtt_history.len() > self.max_history {
                self.rtt_history.remove(0);
            }

            return Some(rtt);
        }
        None
    }

    /// 计算平均 RTT
    pub fn avg_rtt_ms(&self) -> Option<f32> {
        if self.rtt_history.is_empty() {
            return None;
        }
        let sum: u32 = self.rtt_history.iter().sum();
        Some(sum as f32 / self.rtt_history.len() as f32)
    }

    /// 是否需要发送 Ping（距离上次发送超过指定时间）
    pub fn need_ping(&self, interval: Duration) -> bool {
        match self.last_ping_time {
            None => true,
            Some(time) => time.elapsed() >= interval,
        }
    }
}

impl Default for PingTracker {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_voice_packet_audio() {
        let packet = VoicePacket::new_audio(
            42,             // sender_id
            100,            // sequence
            0x12345678,     // context_hash
            0xABCDEF00,     // token_hash
            100.0,          // pos_x
            200.0,          // pos_y
            vec![0xAA; 50], // opus_payload
        );

        let mut buf = vec![0u8; VOICE_MAX_PACKET];
        let len = packet.serialize(&mut buf);

        assert_eq!(len, VOICE_HEADER_SIZE + 50);

        // 验证魔数
        assert_eq!(&buf[0..4], VOICE_MAGIC);

        // 解析回包
        let parsed = VoicePacket::parse(&buf[..len]).unwrap();
        assert_eq!(parsed.version, VOICE_VERSION);
        assert_eq!(parsed.packet_type, PacketType::Audio);
        assert_eq!(parsed.sender_id, 42);
        assert_eq!(parsed.sequence, 100);
        assert_eq!(parsed.context_hash, 0x12345678);
        assert_eq!(parsed.token_hash, 0xABCDEF00);
        assert_eq!(parsed.pos_x, 100.0);
        assert_eq!(parsed.pos_y, 200.0);
        assert_eq!(parsed.opus_payload, vec![0xAA; 50]);
    }

    #[test]
    fn test_voice_packet_ping() {
        let packet = VoicePacket::new_ping(42, 1, 0x12345678, 0);

        let mut buf = vec![0u8; VOICE_MAX_PACKET];
        let len = packet.serialize(&mut buf);

        assert_eq!(len, VOICE_HEADER_SIZE);

        let parsed = VoicePacket::parse(&buf[..len]).unwrap();
        assert_eq!(parsed.packet_type, PacketType::Ping);
        assert_eq!(parsed.opus_payload.len(), 0);
    }

    #[test]
    fn test_invalid_magic() {
        // 创建足够长的数据包（至少 VOICE_HEADER_SIZE 字节）才能触发魔数检查
        let mut data = vec![0u8; VOICE_HEADER_SIZE];
        data[0..4].copy_from_slice(b"XXXX"); // 无效魔数
        let result = VoicePacket::parse(&data);
        assert!(matches!(result, Err(ParseError::InvalidMagic(_))));
    }

    #[test]
    fn test_invalid_version() {
        let mut data = vec![0u8; VOICE_HEADER_SIZE];
        data[0..4].copy_from_slice(VOICE_MAGIC);
        data[4] = 99; // 错误版本

        let result = VoicePacket::parse(&data);
        assert!(matches!(result, Err(ParseError::InvalidVersion(99, 3))));
    }

    #[test]
    fn test_flags() {
        let mut packet = VoicePacket::new_audio(1, 1, 0, 0, 0.0, 0.0, vec![]);

        assert!(!packet.is_vad());
        assert!(!packet.is_loopback());

        packet.set_vad(true);
        assert!(packet.is_vad());

        packet.set_loopback(true);
        assert!(packet.is_loopback());

        packet.set_vad(false);
        assert!(!packet.is_vad());
        assert!(packet.is_loopback());
    }

    #[test]
    fn test_context_hash() {
        let hash1 = calculate_context_hash("42.194.185.210:8303");
        let hash2 = calculate_context_hash("42.194.185.210:8303");
        let hash3 = calculate_context_hash("42.194.185.210:8304");

        assert_eq!(hash1, hash2);
        assert_ne!(hash1, hash3);
    }

    #[test]
    fn test_token_hash() {
        let hash1 = calculate_token_hash("room123");
        let hash2 = calculate_token_hash("room123");
        let hash3 = calculate_token_hash("room456");
        let empty = calculate_token_hash("");

        assert_eq!(hash1, hash2);
        assert_ne!(hash1, hash3);
        assert_eq!(empty, 0);
    }

    #[test]
    fn test_pack_token() {
        let hash = 0x12345678u32;
        let mode = 2u32;
        let packed = pack_token(hash, mode);

        // 低 30 位是哈希
        assert_eq!(token_group(packed), hash & VOICE_GROUP_MASK);
        // 高 2 位是模式
        assert_eq!(token_mode(packed), mode);
    }

    #[test]
    fn test_should_hear() {
        // 同组应该能听到
        assert!(should_hear(0, 0));
        assert!(should_hear(123, 123));

        // 不同组不能听到
        assert!(!should_hear(0, 123));
        assert!(!should_hear(123, 456));
    }

    #[test]
    fn test_seq_operations() {
        // 正常序列
        assert_eq!(seq_delta(10, 5), 5);
        assert!(seq_less(5, 10));
        assert!(!seq_less(10, 5));

        // 回绕处理
        assert_eq!(seq_delta(1, 65535), 2);
        assert!(seq_less(65535, 1));
        assert!(!seq_less(1, 65535));
    }

    #[test]
    fn test_sanitize_float() {
        assert_eq!(sanitize_float(100.0), 100.0);
        assert_eq!(sanitize_float(0.0), 0.0);
        assert_eq!(sanitize_float(-100.0), -100.0);

        // NaN 和无穷大
        assert_eq!(sanitize_float(f32::NAN), 0.0);
        assert_eq!(sanitize_float(f32::INFINITY), 1000000.0);
        assert_eq!(sanitize_float(f32::NEG_INFINITY), -1000000.0);

        // 超大值
        assert_eq!(sanitize_float(2000000.0), 1000000.0);
        assert_eq!(sanitize_float(-2000000.0), -1000000.0);
    }

    #[test]
    fn test_network_stats() {
        let mut stats = NetworkStats::new();

        stats.packets_sent = 100;
        stats.packets_lost = 5;

        assert!((stats.loss_rate() - 0.0476).abs() < 0.001);

        stats.reset();
        assert_eq!(stats.packets_sent, 0);
        assert_eq!(stats.packets_lost, 0);
    }

    #[test]
    fn test_ping_tracker() {
        let mut tracker = PingTracker::new();

        // 初始状态需要发送 ping
        assert!(tracker.need_ping(Duration::from_secs(1)));

        // 发送 ping
        tracker.send_ping(1);
        assert!(!tracker.need_ping(Duration::from_secs(1)));

        // 收到 pong
        let rtt = tracker.recv_pong(1);
        assert!(rtt.is_some());

        // 错误的序列号
        let rtt2 = tracker.recv_pong(2);
        assert!(rtt2.is_none());

        // 平均 RTT
        let avg = tracker.avg_rtt_ms();
        assert!(avg.is_some());
    }
}
