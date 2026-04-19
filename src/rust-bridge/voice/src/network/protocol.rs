//! RV01 语音协议实现
//!
//! 协议格式：
//! ```
//! | Magic (4) | Version (1) | Type (1) | PayloadSize (2) | ContextHash (4) |
//! | TokenHash (4) | Flags (1) | SenderId (2) | Sequence (2) | PosX (4) | PosY (4) | Payload |
//! ```

/// 协议魔数
pub const VOICE_MAGIC: &[u8; 4] = b"RV01";
/// 协议版本
pub const VOICE_VERSION: u8 = 3;
/// 最大包大小
pub const VOICE_MAX_PACKET: usize = 1200;
/// 头部大小
pub const VOICE_HEADER_SIZE: usize = 33;
/// 最大负载大小
pub const VOICE_MAX_PAYLOAD: usize = VOICE_MAX_PACKET - VOICE_HEADER_SIZE;

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
    pub fn new_ping(
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
    ) -> Self {
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
    pub fn new_pong(
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
    ) -> Self {
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
    pub fn parse(data: &[u8]) -> Result<Self, ParseError> {
        if data.len() < VOICE_HEADER_SIZE {
            return Err(ParseError::TooShort(data.len(), VOICE_HEADER_SIZE));
        }

        // 检查魔数
        let magic = [data[0], data[1], data[2], data[3]];
        if &magic != VOICE_MAGIC {
            return Err(ParseError::InvalidMagic(magic));
        }

        let version = data[4];
        if version != VOICE_VERSION {
            return Err(ParseError::InvalidVersion(version, VOICE_VERSION));
        }

        let packet_type = match data[5] {
            1 => PacketType::Audio,
            2 => PacketType::Ping,
            3 => PacketType::Pong,
            t => return Err(ParseError::InvalidType(t)),
        };

        let payload_size = u16::from_be_bytes([data[6], data[7]]);
        
        // 验证 payload 大小
        let expected_len = VOICE_HEADER_SIZE + payload_size as usize;
        if data.len() < expected_len {
            return Err(ParseError::PayloadSizeMismatch(payload_size, data.len() - VOICE_HEADER_SIZE));
        }

        let context_hash = u32::from_be_bytes([data[8], data[9], data[10], data[11]]);
        let token_hash = u32::from_be_bytes([data[12], data[13], data[14], data[15]]);
        let flags = data[16];
        let sender_id = u16::from_be_bytes([data[17], data[18]]);
        let sequence = u16::from_be_bytes([data[19], data[20]]);
        let pos_x = f32::from_be_bytes([data[21], data[22], data[23], data[24]]);
        let pos_y = f32::from_be_bytes([data[25], data[26], data[27], data[28]]);
        
        // 剩余字节是 payload
        let opus_payload = if payload_size > 0 {
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
    /// 返回写入的字节数
    pub fn serialize(&self, buf: &mut [u8]) -> usize {
        let total_len = VOICE_HEADER_SIZE + self.opus_payload.len();
        
        if buf.len() < total_len {
            return 0; // 缓冲区太小
        }

        buf[0..4].copy_from_slice(VOICE_MAGIC);
        buf[4] = self.version;
        buf[5] = self.packet_type as u8;
        buf[6..8].copy_from_slice(&self.payload_size.to_be_bytes());
        buf[8..12].copy_from_slice(&self.context_hash.to_be_bytes());
        buf[12..16].copy_from_slice(&self.token_hash.to_be_bytes());
        buf[16] = self.flags;
        buf[17..19].copy_from_slice(&self.sender_id.to_be_bytes());
        buf[19..21].copy_from_slice(&self.sequence.to_be_bytes());
        buf[21..25].copy_from_slice(&self.pos_x.to_be_bytes());
        buf[25..29].copy_from_slice(&self.pos_y.to_be_bytes());
        
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
    hash & 0x3FFFFFFF
}

/// 打包 Token (哈希 + 模式)
pub fn pack_token(group_hash: u32, mode: u32) -> u32 {
    (group_hash & 0x3FFFFFFF) | ((mode & 0x3) << 30)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_voice_packet_audio() {
        let packet = VoicePacket::new_audio(
            42,           // sender_id
            100,          // sequence
            0x12345678,   // context_hash
            0xABCDEF00,   // token_hash
            100.0,        // pos_x
            200.0,        // pos_y
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
        let data = b"XXXX\x03\x01\x00\x00";
        let result = VoicePacket::parse(data);
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
        assert_eq!(packed & 0x3FFFFFFF, hash & 0x3FFFFFFF);
        // 高 2 位是模式
        assert_eq!(packed >> 30, mode);
    }
}
