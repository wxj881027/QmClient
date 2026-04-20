//! UDP 语音客户端
//!
//! 使用 Tokio 异步运行时处理网络 I/O
//!
//! 参考 C++ 实现: voice_core.cpp

use super::protocol::{
    calculate_context_hash, calculate_token_hash, flags, pack_token, sanitize_float, should_hear,
    token_group, NetworkStats, PacketType, ParseError, PingTracker, VoicePacket, VOICE_MAX_PACKET,
    VOICE_MAX_PAYLOAD,
};
use parking_lot::Mutex;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tokio::time::{interval, Duration, Instant};

/// 默认发送队列大小
const DEFAULT_SEND_QUEUE_SIZE: usize = 64;
/// 默认接收队列大小
const DEFAULT_RECV_QUEUE_SIZE: usize = 64;
/// Ping 间隔（秒）
const PING_INTERVAL_SECS: u64 = 2;
/// 最大客户端数
pub const MAX_CLIENTS: usize = 64;

/// 语音客户端错误
#[derive(Debug, thiserror::Error)]
pub enum ClientError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Parse error: {0}")]
    Parse(#[from] ParseError),
    #[error("Invalid server address: {0}")]
    InvalidAddress(String),
    #[error("Channel closed")]
    ChannelClosed,
    #[error("Client not connected")]
    NotConnected,
    #[error("Already running")]
    AlreadyRunning,
}

/// 客户端配置
#[derive(Debug, Clone)]
pub struct ClientConfig {
    /// 发送队列大小
    pub send_queue_size: usize,
    /// 接收队列大小
    pub recv_queue_size: usize,
    /// Ping 间隔
    pub ping_interval: Duration,
    /// 是否启用 FEC
    pub enable_fec: bool,
    /// 初始比特率
    pub initial_bitrate: i32,
}

impl Default for ClientConfig {
    fn default() -> Self {
        Self {
            send_queue_size: DEFAULT_SEND_QUEUE_SIZE,
            recv_queue_size: DEFAULT_RECV_QUEUE_SIZE,
            ping_interval: Duration::from_secs(PING_INTERVAL_SECS),
            enable_fec: false,
            initial_bitrate: 24000,
        }
    }
}

/// 客户端状态
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ClientState {
    /// 未连接
    #[default]
    Disconnected,
    /// 正在连接
    Connecting,
    /// 已连接
    Connected,
    /// 错误状态
    Error,
}

/// 接收到的音频数据
#[derive(Debug, Clone)]
pub struct ReceivedAudio {
    /// 发送者 ID
    pub sender_id: u16,
    /// 序列号
    pub sequence: u16,
    /// 位置 X
    pub pos_x: f32,
    /// 位置 Y
    pub pos_y: f32,
    /// 标志位
    pub flags: u8,
    /// Opus 编码数据
    pub opus_data: Vec<u8>,
    /// 接收时间
    pub recv_time: Instant,
}

/// 客户端统计信息（共享）
#[derive(Debug, Default)]
struct SharedState {
    /// 客户端状态
    state: ClientState,
    /// 网络统计
    stats: NetworkStats,
    /// Ping 追踪器
    ping_tracker: PingTracker,
    /// 当前序列号
    sequence: u16,
    /// 上下文哈希
    context_hash: u32,
    /// Token 哈希
    token_hash: u32,
    /// 发送者 ID
    sender_id: u16,
    /// 位置 X
    pos_x: f32,
    /// 位置 Y
    pos_y: f32,
    /// 标志位
    flags: u8,
}

/// 语音客户端
pub struct VoiceClient {
    /// 配置
    #[allow(dead_code)]
    config: ClientConfig,
    /// 服务器地址
    server_addr: SocketAddr,
    /// 共享状态
    shared: Arc<Mutex<SharedState>>,
    /// 发送通道发送端
    send_tx: mpsc::Sender<Vec<u8>>,
    /// 是否已启动
    running: Arc<Mutex<bool>>,
}

impl VoiceClient {
    /// 创建新的语音客户端
    ///
    /// # 参数
    /// * `server_addr` - 服务器地址 (例如 "42.194.185.210:9987")
    /// * `config` - 客户端配置（可选）
    pub async fn new(
        server_addr: &str,
        config: Option<ClientConfig>,
    ) -> Result<(Self, mpsc::Receiver<ReceivedAudio>), ClientError> {
        let config = config.unwrap_or_default();

        // 解析服务器地址
        let server_addr: SocketAddr = server_addr
            .parse()
            .map_err(|_| ClientError::InvalidAddress(server_addr.to_string()))?;

        // 创建 UDP socket
        let socket = UdpSocket::bind("0.0.0.0:0").await?;
        socket.connect(server_addr).await?;

        // 创建通道
        let (send_tx, send_rx) = mpsc::channel::<Vec<u8>>(config.send_queue_size);
        let (recv_tx, recv_rx) = mpsc::channel::<ReceivedAudio>(config.recv_queue_size);

        let socket = Arc::new(socket);
        let shared = Arc::new(Mutex::new(SharedState {
            state: ClientState::Connected,
            ..Default::default()
        }));
        let running = Arc::new(Mutex::new(true));

        // 启动发送任务
        {
            let socket_clone = socket.clone();
            let shared_clone = shared.clone();
            let running_clone = running.clone();
            tokio::spawn(async move {
                Self::send_loop(socket_clone, send_rx, shared_clone, running_clone).await;
            });
        }

        // 启动接收任务
        {
            let socket_clone = socket.clone();
            let shared_clone = shared.clone();
            let running_clone = running.clone();
            tokio::spawn(async move {
                Self::recv_loop(socket_clone, recv_tx, shared_clone, running_clone).await;
            });
        }

        // 启动 Ping 任务
        {
            let send_tx_clone = send_tx.clone();
            let shared_clone = shared.clone();
            let running_clone = running.clone();
            let ping_interval = config.ping_interval;
            tokio::spawn(async move {
                Self::ping_loop(send_tx_clone, shared_clone, running_clone, ping_interval).await;
            });
        }

        let client = Self {
            config,
            server_addr,
            shared,
            send_tx,
            running,
        };

        Ok((client, recv_rx))
    }

    /// 设置客户端信息
    pub fn set_client_info(&self, sender_id: u16, game_server_addr: &str, token: &str, mode: u32) {
        let mut shared = self.shared.lock();
        shared.sender_id = sender_id;
        shared.context_hash = calculate_context_hash(game_server_addr);

        let group_hash = calculate_token_hash(token);
        shared.token_hash = pack_token(group_hash, mode);
    }

    /// 设置位置
    pub fn set_position(&self, x: f32, y: f32) {
        let mut shared = self.shared.lock();
        shared.pos_x = sanitize_float(x);
        shared.pos_y = sanitize_float(y);
    }

    /// 设置 VAD 标志
    pub fn set_vad(&self, vad: bool) {
        let mut shared = self.shared.lock();
        if vad {
            shared.flags |= flags::VAD;
        } else {
            shared.flags &= !flags::VAD;
        }
    }

    /// 设置回环标志
    pub fn set_loopback(&self, loopback: bool) {
        let mut shared = self.shared.lock();
        if loopback {
            shared.flags |= flags::LOOPBACK;
        } else {
            shared.flags &= !flags::LOOPBACK;
        }
    }

    /// 发送语音包
    pub async fn send_packet(&self, packet: &VoicePacket) -> Result<(), ClientError> {
        let mut buf = vec![0u8; VOICE_MAX_PACKET];
        let len = packet.serialize(&mut buf);

        if len == 0 {
            return Err(ClientError::Io(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "Failed to serialize packet",
            )));
        }

        buf.truncate(len);
        self.send_tx
            .send(buf)
            .await
            .map_err(|_| ClientError::ChannelClosed)?;

        // 更新统计
        {
            let mut shared = self.shared.lock();
            shared.stats.packets_sent += 1;
            shared.stats.bytes_sent += len as u64;
        }

        Ok(())
    }

    /// 发送音频数据
    pub async fn send_audio(&self, opus_data: Vec<u8>) -> Result<(), ClientError> {
        if opus_data.len() > VOICE_MAX_PAYLOAD {
            return Err(ClientError::Io(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!(
                    "Payload too large: {} > {}",
                    opus_data.len(),
                    VOICE_MAX_PAYLOAD
                ),
            )));
        }

        let (sender_id, sequence, context_hash, token_hash, pos_x, pos_y, flags) = {
            let mut shared = self.shared.lock();
            let seq = shared.sequence;
            shared.sequence = seq.wrapping_add(1);
            (
                shared.sender_id,
                seq,
                shared.context_hash,
                shared.token_hash,
                shared.pos_x,
                shared.pos_y,
                shared.flags,
            )
        };

        let mut packet = VoicePacket::new_audio(
            sender_id,
            sequence,
            context_hash,
            token_hash,
            pos_x,
            pos_y,
            opus_data,
        );

        // 设置标志
        if flags & flags::VAD != 0 {
            packet.set_vad(true);
        }
        if flags & flags::LOOPBACK != 0 {
            packet.set_loopback(true);
        }

        self.send_packet(&packet).await
    }

    /// 发送 PING
    pub async fn send_ping(&self) -> Result<(), ClientError> {
        let (sender_id, sequence, context_hash, token_hash) = {
            let mut shared = self.shared.lock();
            let seq = shared.sequence;
            shared.sequence = seq.wrapping_add(1);
            shared.ping_tracker.send_ping(seq);
            (
                shared.sender_id,
                seq,
                shared.context_hash,
                shared.token_hash,
            )
        };

        let packet = VoicePacket::new_ping(sender_id, sequence, context_hash, token_hash);
        self.send_packet(&packet).await
    }

    /// 发送 PONG
    pub async fn send_pong(&self, sequence: u16) -> Result<(), ClientError> {
        let (sender_id, context_hash, token_hash) = {
            let shared = self.shared.lock();
            (shared.sender_id, shared.context_hash, shared.token_hash)
        };

        let packet = VoicePacket::new_pong(sender_id, sequence, context_hash, token_hash);
        self.send_packet(&packet).await
    }

    /// 获取客户端状态
    pub fn state(&self) -> ClientState {
        self.shared.lock().state
    }

    /// 获取服务器地址
    pub fn server_addr(&self) -> SocketAddr {
        self.server_addr
    }

    /// 获取网络统计
    pub fn stats(&self) -> NetworkStats {
        self.shared.lock().stats.clone()
    }

    /// 获取当前 RTT (毫秒)
    pub fn rtt_ms(&self) -> Option<u32> {
        self.shared.lock().ping_tracker.last_rtt_ms
    }

    /// 获取平均 RTT (毫秒)
    pub fn avg_rtt_ms(&self) -> Option<f32> {
        self.shared.lock().ping_tracker.avg_rtt_ms()
    }

    /// 停止客户端
    pub fn stop(&self) {
        *self.running.lock() = false;
    }

    /// 发送循环
    async fn send_loop(
        socket: Arc<UdpSocket>,
        mut rx: mpsc::Receiver<Vec<u8>>,
        shared: Arc<Mutex<SharedState>>,
        running: Arc<Mutex<bool>>,
    ) {
        while *running.lock() {
            tokio::select! {
                Some(data) = rx.recv() => {
                    if let Err(e) = socket.send(&data).await {
                        log::warn!("Failed to send packet: {}", e);
                        let mut shared = shared.lock();
                        shared.state = ClientState::Error;
                    }
                }
                _ = tokio::time::sleep(Duration::from_millis(100)) => {
                    // 检查是否仍在运行
                }
            }
        }
        log::info!("Send loop ended");
    }

    /// 接收循环
    async fn recv_loop(
        socket: Arc<UdpSocket>,
        tx: mpsc::Sender<ReceivedAudio>,
        shared: Arc<Mutex<SharedState>>,
        running: Arc<Mutex<bool>>,
    ) {
        let mut buf = [0u8; VOICE_MAX_PACKET];

        while *running.lock() {
            tokio::select! {
                result = socket.recv_from(&mut buf) => {
                    match result {
                        Ok((len, _addr)) => {
                            // 验证来源
                            {
                                let _shared = shared.lock();
                                // 这里可以添加地址验证逻辑
                            }

                            // 解析包
                            match VoicePacket::parse(&buf[..len]) {
                                Ok(packet) => {
                                    // 更新统计
                                    {
                                        let mut shared = shared.lock();
                                        shared.stats.packets_recv += 1;
                                        shared.stats.bytes_recv += len as u64;
                                    }

                                    match packet.packet_type {
                                        PacketType::Audio => {
                                            // 验证上下文
                                            let local_context = shared.lock().context_hash;
                                            if packet.context_hash == 0 || packet.context_hash != local_context {
                                                continue;
                                            }

                                            // 验证 token 组
                                            let local_token = shared.lock().token_hash;
                                            let local_group = token_group(local_token);
                                            let sender_group = token_group(packet.token_hash);

                                            if !should_hear(sender_group, local_group) {
                                                continue;
                                            }

                                            let audio = ReceivedAudio {
                                                sender_id: packet.sender_id,
                                                sequence: packet.sequence,
                                                pos_x: packet.pos_x,
                                                pos_y: packet.pos_y,
                                                flags: packet.flags,
                                                opus_data: packet.opus_payload,
                                                recv_time: Instant::now(),
                                            };

                                            if tx.send(audio).await.is_err() {
                                                log::info!("Receive channel closed");
                                                break;
                                            }
                                        }
                                        PacketType::Pong => {
                                            // 处理 Pong
                                            let mut shared = shared.lock();
                                            if let Some(rtt) = shared.ping_tracker.recv_pong(packet.sequence) {
                                                shared.stats.avg_rtt_ms = rtt as f32;
                                            }
                                        }
                                        PacketType::Ping => {
                                            // 收到 Ping，发送 Pong
                                            // 这里可以添加自动回复逻辑
                                        }
                                    }
                                }
                                Err(e) => {
                                    log::debug!("Failed to parse packet: {}", e);
                                }
                            }
                        }
                        Err(e) => {
                            log::warn!("Receive error: {}", e);
                        }
                    }
                }
                _ = tokio::time::sleep(Duration::from_millis(10)) => {
                    // 检查是否仍在运行
                }
            }
        }
        log::info!("Receive loop ended");
    }

    /// Ping 循环
    async fn ping_loop(
        send_tx: mpsc::Sender<Vec<u8>>,
        shared: Arc<Mutex<SharedState>>,
        running: Arc<Mutex<bool>>,
        ping_interval: Duration,
    ) {
        let mut interval = interval(ping_interval);

        while *running.lock() {
            interval.tick().await;

            // 检查是否需要发送 Ping
            let need_ping = {
                let shared = shared.lock();
                shared.ping_tracker.need_ping(ping_interval)
            };

            if need_ping {
                let (sender_id, sequence, context_hash, token_hash) = {
                    let mut shared = shared.lock();
                    let seq = shared.sequence;
                    shared.sequence = seq.wrapping_add(1);
                    shared.ping_tracker.send_ping(seq);
                    (
                        shared.sender_id,
                        seq,
                        shared.context_hash,
                        shared.token_hash,
                    )
                };

                let packet = VoicePacket::new_ping(sender_id, sequence, context_hash, token_hash);
                let mut buf = vec![0u8; VOICE_MAX_PACKET];
                let len = packet.serialize(&mut buf);

                if len > 0 {
                    buf.truncate(len);
                    if send_tx.send(buf).await.is_err() {
                        break;
                    }
                }
            }
        }
        log::info!("Ping loop ended");
    }
}

/// 简单的 UDP 客户端（同步版本，用于测试）
pub struct SimpleUdpClient {
    socket: std::net::UdpSocket,
    server_addr: SocketAddr,
    sequence: u16,
}

impl SimpleUdpClient {
    /// 创建新的简单客户端
    pub fn new(server_addr: &str) -> Result<Self, ClientError> {
        let server_addr: SocketAddr = server_addr
            .parse()
            .map_err(|_| ClientError::InvalidAddress(server_addr.to_string()))?;

        let socket = std::net::UdpSocket::bind("0.0.0.0:0")?;
        socket.connect(server_addr)?;

        Ok(Self {
            socket,
            server_addr,
            sequence: 0,
        })
    }

    /// 发送音频包
    pub fn send_audio(
        &mut self,
        sender_id: u16,
        context_hash: u32,
        token_hash: u32,
        pos_x: f32,
        pos_y: f32,
        opus_data: &[u8],
    ) -> Result<(), ClientError> {
        let packet = VoicePacket::new_audio(
            sender_id,
            self.sequence,
            context_hash,
            token_hash,
            pos_x,
            pos_y,
            opus_data.to_vec(),
        );

        self.sequence = self.sequence.wrapping_add(1);

        let mut buf = vec![0u8; VOICE_MAX_PACKET];
        let len = packet.serialize(&mut buf);

        if len == 0 {
            return Err(ClientError::Io(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "Failed to serialize packet",
            )));
        }

        self.socket.send(&buf[..len])?;
        Ok(())
    }

    /// 接收数据
    pub fn recv(&self, buf: &mut [u8]) -> Result<(usize, VoicePacket), ClientError> {
        let (len, _addr) = self.socket.recv_from(buf)?;
        let packet = VoicePacket::parse(&buf[..len])?;
        Ok((len, packet))
    }

    /// 获取服务器地址
    pub fn server_addr(&self) -> SocketAddr {
        self.server_addr
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_client_creation() {
        // 使用本地地址测试
        let result = VoiceClient::new("127.0.0.1:9987", None).await;
        assert!(result.is_ok());

        let (client, _rx) = result.unwrap();
        assert_eq!(client.state(), ClientState::Connected);
    }

    #[tokio::test]
    async fn test_invalid_address() {
        let result = VoiceClient::new("not-an-address", None).await;
        assert!(matches!(result, Err(ClientError::InvalidAddress(_))));
    }

    #[test]
    fn test_simple_client_creation() {
        let result = SimpleUdpClient::new("127.0.0.1:9987");
        assert!(result.is_ok());
    }

    #[test]
    fn test_client_config_default() {
        let config = ClientConfig::default();
        assert_eq!(config.send_queue_size, DEFAULT_SEND_QUEUE_SIZE);
        assert_eq!(config.recv_queue_size, DEFAULT_RECV_QUEUE_SIZE);
        assert_eq!(
            config.ping_interval,
            Duration::from_secs(PING_INTERVAL_SECS)
        );
    }

    #[tokio::test]
    async fn test_set_client_info() {
        let (client, _rx) = VoiceClient::new("127.0.0.1:9987", None).await.unwrap();

        client.set_client_info(42, "192.168.1.1:8303", "room123", 1);

        let shared = client.shared.lock();
        assert_eq!(shared.sender_id, 42);
        assert!(shared.context_hash != 0);
        assert!(shared.token_hash != 0);
    }

    #[tokio::test]
    async fn test_set_position() {
        let (client, _rx) = VoiceClient::new("127.0.0.1:9987", None).await.unwrap();

        client.set_position(100.0, 200.0);

        let shared = client.shared.lock();
        assert_eq!(shared.pos_x, 100.0);
        assert_eq!(shared.pos_y, 200.0);
    }

    #[tokio::test]
    async fn test_set_flags() {
        let (client, _rx) = VoiceClient::new("127.0.0.1:9987", None).await.unwrap();

        client.set_vad(true);
        {
            let shared = client.shared.lock();
            assert!(shared.flags & flags::VAD != 0);
        }

        client.set_loopback(true);
        {
            let shared = client.shared.lock();
            assert!(shared.flags & flags::LOOPBACK != 0);
        }

        client.set_vad(false);
        {
            let shared = client.shared.lock();
            assert!(shared.flags & flags::VAD == 0);
            assert!(shared.flags & flags::LOOPBACK != 0);
        }
    }

    #[test]
    fn test_context_hash_filter() {
        let local_context = calculate_context_hash("192.168.1.1:8303");

        // context_hash 为 0 的包应被拒绝
        let packet_zero = VoicePacket::new_audio(1, 0, 0, 0, 0.0, 0.0, vec![0xAA; 10]);
        assert!(
            packet_zero.context_hash == 0 || packet_zero.context_hash != local_context,
            "context_hash 为 0 应被拒绝"
        );

        // context_hash 不匹配的包应被拒绝
        let wrong_context = calculate_context_hash("10.0.0.1:8303");
        let packet_wrong = VoicePacket::new_audio(1, 0, wrong_context, 0, 0.0, 0.0, vec![0xAA; 10]);
        assert_ne!(
            packet_wrong.context_hash, local_context,
            "不同服务器地址的 context_hash 应不匹配"
        );
        assert!(
            packet_wrong.context_hash == 0 || packet_wrong.context_hash != local_context,
            "context_hash 不匹配应被拒绝"
        );

        // context_hash 匹配的包应被接受
        let packet_match = VoicePacket::new_audio(1, 0, local_context, 0, 0.0, 0.0, vec![0xAA; 10]);
        assert!(
            packet_match.context_hash != 0 && packet_match.context_hash == local_context,
            "context_hash 匹配且非 0 应被接受"
        );
    }

    #[test]
    fn test_token_group_filter() {
        // 同 token 不同 mode 的玩家属于同组，应该能听到彼此
        let token_a = pack_token(calculate_token_hash("room1"), 0);
        let token_b = pack_token(calculate_token_hash("room1"), 1);
        let group_a = token_group(token_a);
        let group_b = token_group(token_b);
        assert_eq!(group_a, group_b, "同 token 字符串的组哈希应相同");
        assert!(should_hear(group_a, group_b), "同组玩家应该能听到彼此");

        // 不同 token 的玩家属于不同组，不应该能听到
        let token_c = pack_token(calculate_token_hash("room2"), 0);
        let group_c = token_group(token_c);
        assert_ne!(group_a, group_c, "不同 token 字符串的组哈希应不同");
        assert!(!should_hear(group_a, group_c), "不同组玩家不应该能听到");

        // 空 token (组 0) 的玩家
        let empty_token = pack_token(calculate_token_hash(""), 0);
        let empty_group = token_group(empty_token);
        assert_eq!(empty_group, 0, "空 token 的组应为 0");
        assert!(
            should_hear(empty_group, empty_group),
            "同组 (组 0) 玩家应该能听到彼此"
        );
        assert!(
            !should_hear(empty_group, group_a),
            "组 0 和非 0 组不应该能听到"
        );

        // mode 不影响组判断
        let token_d = pack_token(calculate_token_hash("room1"), 2);
        let group_d = token_group(token_d);
        assert_eq!(group_a, group_d, "同 token 不同 mode 的组应相同");
        assert!(should_hear(group_a, group_d), "同组不同 mode 应该能听到");
    }

    #[tokio::test]
    async fn test_shared_state_config() {
        let (client, _rx) = VoiceClient::new("127.0.0.1:9987", None).await.unwrap();

        // 初始状态 context_hash 和 token_hash 为 0
        {
            let shared = client.shared.lock();
            assert_eq!(shared.context_hash, 0);
            assert_eq!(shared.token_hash, 0);
        }

        // 设置客户端信息
        client.set_client_info(42, "192.168.1.1:8303", "room123", 1);

        // 验证 context_hash 和 token_hash 被正确设置
        {
            let shared = client.shared.lock();
            let expected_context = calculate_context_hash("192.168.1.1:8303");
            let expected_group = calculate_token_hash("room123");
            let expected_token = pack_token(expected_group, 1);

            assert_eq!(shared.context_hash, expected_context);
            assert_eq!(shared.token_hash, expected_token);
            assert_ne!(shared.context_hash, 0);
            assert_ne!(shared.token_hash, 0);
        }

        // 更新为不同的配置
        client.set_client_info(99, "10.0.0.1:8303", "room456", 2);

        {
            let shared = client.shared.lock();
            let expected_context = calculate_context_hash("10.0.0.1:8303");
            let expected_group = calculate_token_hash("room456");
            let expected_token = pack_token(expected_group, 2);

            assert_eq!(shared.context_hash, expected_context);
            assert_eq!(shared.token_hash, expected_token);
            assert_eq!(shared.sender_id, 99);
        }
    }
}
