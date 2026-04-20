//! UDP 语音服务器
//!
//! 接收客户端音频包并广播给其他客户端
//!
//! 参考 C++ 实现: voice_core.cpp

use super::protocol::{
    should_hear, token_group, PacketType, ParseError, VoicePacket, VOICE_MAX_PACKET,
};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::time::{interval, Duration, Instant};

/// 默认监听端口
pub const DEFAULT_PORT: u16 = 9987;
/// 最大客户端数
pub const MAX_CLIENTS: usize = 64;
/// 客户端超时时间（秒）
const CLIENT_TIMEOUT_SECS: u64 = 30;
/// 清理间隔（秒）
const CLEANUP_INTERVAL_SECS: u64 = 10;

/// 服务器错误
#[derive(Debug, thiserror::Error)]
pub enum ServerError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Parse error: {0}")]
    Parse(#[from] ParseError),
    #[error("Invalid bind address: {0}")]
    InvalidAddress(String),
    #[error("Server already running")]
    AlreadyRunning,
    #[error("Server not running")]
    NotRunning,
}

/// 服务器配置
#[derive(Debug, Clone)]
pub struct ServerConfig {
    /// 监听端口
    pub port: u16,
    /// 最大客户端数
    pub max_clients: usize,
    /// 客户端超时时间
    pub client_timeout: Duration,
    /// 清理间隔
    pub cleanup_interval: Duration,
    /// 是否启用回环测试
    pub enable_loopback: bool,
}

impl Default for ServerConfig {
    fn default() -> Self {
        Self {
            port: DEFAULT_PORT,
            max_clients: MAX_CLIENTS,
            client_timeout: Duration::from_secs(CLIENT_TIMEOUT_SECS),
            cleanup_interval: Duration::from_secs(CLEANUP_INTERVAL_SECS),
            enable_loopback: false,
        }
    }
}

/// 客户端信息
#[derive(Debug, Clone)]
pub struct ClientInfo {
    /// 客户端地址
    pub addr: SocketAddr,
    /// 发送者 ID
    pub sender_id: u16,
    /// 上下文哈希
    pub context_hash: u32,
    /// Token 哈希
    pub token_hash: u32,
    /// 最后活跃时间
    pub last_active: Instant,
    /// 位置 X
    pub pos_x: f32,
    /// 位置 Y
    pub pos_y: f32,
    /// 标志位
    pub flags: u8,
}

impl ClientInfo {
    /// 创建新的客户端信息
    pub fn new(addr: SocketAddr, packet: &VoicePacket) -> Self {
        Self {
            addr,
            sender_id: packet.sender_id,
            context_hash: packet.context_hash,
            token_hash: packet.token_hash,
            last_active: Instant::now(),
            pos_x: packet.pos_x,
            pos_y: packet.pos_y,
            flags: packet.flags,
        }
    }

    /// 更新客户端信息
    pub fn update(&mut self, packet: &VoicePacket) {
        self.sender_id = packet.sender_id;
        self.context_hash = packet.context_hash;
        self.token_hash = packet.token_hash;
        self.last_active = Instant::now();
        self.pos_x = packet.pos_x;
        self.pos_y = packet.pos_y;
        self.flags = packet.flags;
    }

    /// 检查是否超时
    pub fn is_timeout(&self, timeout: Duration) -> bool {
        self.last_active.elapsed() > timeout
    }
}

/// 服务器统计信息
#[derive(Debug, Clone, Default)]
pub struct ServerStats {
    /// 总接收包数
    pub packets_recv: u64,
    /// 总发送包数
    pub packets_sent: u64,
    /// 总接收字节数
    pub bytes_recv: u64,
    /// 总发送字节数
    pub bytes_sent: u64,
    /// 当前客户端数
    pub clients_connected: usize,
    /// 峰值客户端数
    pub peak_clients: usize,
    /// 服务器启动时间
    pub start_time: Option<Instant>,
}

impl ServerStats {
    /// 计算运行时间
    pub fn uptime_secs(&self) -> u64 {
        self.start_time.map(|t| t.elapsed().as_secs()).unwrap_or(0)
    }
}

/// 服务器状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServerState {
    /// 未运行
    Stopped,
    /// 正在运行
    Running,
    /// 错误状态
    Error,
}

/// 共享服务器状态
struct SharedState {
    /// 客户端列表 (sender_id -> ClientInfo)
    clients: HashMap<u16, ClientInfo>,
    /// 统计信息
    stats: ServerStats,
    /// 服务器状态
    state: ServerState,
    /// 配置
    config: ServerConfig,
}

impl SharedState {
    fn new(config: ServerConfig) -> Self {
        Self {
            clients: HashMap::new(),
            stats: ServerStats::default(),
            state: ServerState::Stopped,
            config,
        }
    }
}

/// 语音服务器
pub struct VoiceServer {
    /// 共享状态
    shared: Arc<Mutex<SharedState>>,
    /// 是否正在运行
    running: Arc<Mutex<bool>>,
}

impl VoiceServer {
    /// 创建新的语音服务器
    pub fn new(config: Option<ServerConfig>) -> Self {
        let config = config.unwrap_or_default();
        Self {
            shared: Arc::new(Mutex::new(SharedState::new(config))),
            running: Arc::new(Mutex::new(false)),
        }
    }

    /// 启动服务器
    pub async fn start(&self) -> Result<(), ServerError> {
        // 检查是否已运行
        {
            let mut running = self.running.lock();
            if *running {
                return Err(ServerError::AlreadyRunning);
            }
            *running = true;
        }

        let port = self.shared.lock().config.port;
        let bind_addr = format!("0.0.0.0:{}", port);

        // 创建 UDP socket
        let socket = UdpSocket::bind(&bind_addr).await?;
        let socket = Arc::new(socket);

        // 更新状态
        {
            let mut shared = self.shared.lock();
            shared.state = ServerState::Running;
            shared.stats.start_time = Some(Instant::now());
        }

        log::info!("Voice server started on {}", bind_addr);

        // 启动接收循环
        {
            let socket_clone = socket.clone();
            let shared_clone = self.shared.clone();
            let running_clone = self.running.clone();
            tokio::spawn(async move {
                Self::recv_loop(socket_clone, shared_clone, running_clone).await;
            });
        }

        // 启动清理循环
        {
            let shared_clone = self.shared.clone();
            let running_clone = self.running.clone();
            let cleanup_interval = self.shared.lock().config.cleanup_interval;
            tokio::spawn(async move {
                Self::cleanup_loop(shared_clone, running_clone, cleanup_interval).await;
            });
        }

        Ok(())
    }

    /// 停止服务器
    pub fn stop(&self) {
        *self.running.lock() = false;

        let mut shared = self.shared.lock();
        shared.state = ServerState::Stopped;
        shared.clients.clear();
        shared.stats.clients_connected = 0;

        log::info!("Voice server stopped");
    }

    /// 获取服务器状态
    pub fn state(&self) -> ServerState {
        self.shared.lock().state
    }

    /// 获取统计信息
    pub fn stats(&self) -> ServerStats {
        self.shared.lock().stats.clone()
    }

    /// 获取客户端数量
    pub fn client_count(&self) -> usize {
        self.shared.lock().clients.len()
    }

    /// 获取客户端列表
    pub fn clients(&self) -> Vec<ClientInfo> {
        self.shared.lock().clients.values().cloned().collect()
    }

    /// 接收循环
    async fn recv_loop(
        socket: Arc<UdpSocket>,
        shared: Arc<Mutex<SharedState>>,
        running: Arc<Mutex<bool>>,
    ) {
        let mut buf = [0u8; VOICE_MAX_PACKET];

        while *running.lock() {
            tokio::select! {
                result = socket.recv_from(&mut buf) => {
                    match result {
                        Ok((len, addr)) => {
                            Self::handle_packet(&socket, &shared, &buf[..len], addr).await;
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

        log::info!("Server receive loop ended");
    }

    /// 处理接收到的包
    async fn handle_packet(
        socket: &Arc<UdpSocket>,
        shared: &Arc<Mutex<SharedState>>,
        data: &[u8],
        addr: SocketAddr,
    ) {
        // 解析包
        let packet = match VoicePacket::parse(data) {
            Ok(p) => p,
            Err(e) => {
                log::debug!("Failed to parse packet from {}: {}", addr, e);
                return;
            }
        };

        // 更新统计
        {
            let mut shared = shared.lock();
            shared.stats.packets_recv += 1;
            shared.stats.bytes_recv += data.len() as u64;
        }

        match packet.packet_type {
            PacketType::Audio => {
                Self::handle_audio_packet(socket, shared, &packet, addr, data).await;
            }
            PacketType::Ping => {
                Self::handle_ping_packet(socket, shared, &packet, addr).await;
            }
            PacketType::Pong => {
                // 服务器通常不处理 Pong
            }
        }
    }

    /// 处理音频包
    async fn handle_audio_packet(
        socket: &Arc<UdpSocket>,
        shared: &Arc<Mutex<SharedState>>,
        packet: &VoicePacket,
        addr: SocketAddr,
        raw_data: &[u8],
    ) {
        // 检查回环标志
        let is_loopback = packet.is_loopback();
        let enable_loopback = shared.lock().config.enable_loopback;

        // 更新或添加客户端
        let sender_group = token_group(packet.token_hash);
        let clients_to_broadcast: Vec<(SocketAddr, u32, u32)>;

        {
            let mut shared = shared.lock();

            // 更新发送者信息
            if let Some(client) = shared.clients.get_mut(&packet.sender_id) {
                client.update(packet);
            } else {
                // 检查客户端数量限制
                if shared.clients.len() < shared.config.max_clients {
                    shared
                        .clients
                        .insert(packet.sender_id, ClientInfo::new(addr, packet));
                    shared.stats.clients_connected = shared.clients.len();
                    shared.stats.peak_clients = shared.stats.peak_clients.max(shared.clients.len());
                } else {
                    log::warn!("Max clients reached, rejecting client {}", packet.sender_id);
                    return;
                }
            }

            // 收集需要广播的客户端
            clients_to_broadcast = shared
                .clients
                .iter()
                .filter(|(&id, _)| id != packet.sender_id || is_loopback || enable_loopback)
                .filter_map(|(&_id, client)| {
                    // 检查上下文
                    if client.context_hash != packet.context_hash {
                        return None;
                    }

                    // 检查 token 组
                    let receiver_group = token_group(client.token_hash);
                    if !should_hear(sender_group, receiver_group) {
                        return None;
                    }

                    Some((client.addr, client.token_hash, client.context_hash))
                })
                .collect();
        }

        // 广播给其他客户端
        for (client_addr, _, _) in clients_to_broadcast {
            if let Err(e) = socket.send_to(raw_data, client_addr).await {
                log::debug!("Failed to broadcast to {}: {}", client_addr, e);
            } else {
                let mut shared = shared.lock();
                shared.stats.packets_sent += 1;
                shared.stats.bytes_sent += raw_data.len() as u64;
            }
        }
    }

    /// 处理 Ping 包
    async fn handle_ping_packet(
        socket: &Arc<UdpSocket>,
        shared: &Arc<Mutex<SharedState>>,
        packet: &VoicePacket,
        addr: SocketAddr,
    ) {
        // 更新客户端信息
        {
            let mut shared = shared.lock();
            if let Some(client) = shared.clients.get_mut(&packet.sender_id) {
                client.update(packet);
            }
        }

        // 发送 Pong
        let pong = VoicePacket::new_pong(
            packet.sender_id,
            packet.sequence,
            packet.context_hash,
            packet.token_hash,
        );

        let mut buf = [0u8; VOICE_MAX_PACKET];
        let len = pong.serialize(&mut buf);

        if len > 0 {
            if let Err(e) = socket.send_to(&buf[..len], addr).await {
                log::debug!("Failed to send pong to {}: {}", addr, e);
            }
        }
    }

    /// 清理超时客户端
    async fn cleanup_loop(
        shared: Arc<Mutex<SharedState>>,
        running: Arc<Mutex<bool>>,
        cleanup_interval: Duration,
    ) {
        let mut interval = interval(cleanup_interval);

        while *running.lock() {
            interval.tick().await;

            let timeout = {
                let shared = shared.lock();
                shared.config.client_timeout
            };

            let removed_count = {
                let mut shared = shared.lock();
                let before = shared.clients.len();

                shared
                    .clients
                    .retain(|_, client| !client.is_timeout(timeout));

                shared.stats.clients_connected = shared.clients.len();
                before - shared.clients.len()
            };

            if removed_count > 0 {
                log::info!("Cleaned up {} timeout clients", removed_count);
            }
        }

        log::info!("Cleanup loop ended");
    }
}

/// 简单的 UDP 服务器（同步版本，用于测试）
pub struct SimpleUdpServer {
    socket: std::net::UdpSocket,
    clients: HashMap<u16, ClientInfo>,
    config: ServerConfig,
}

impl SimpleUdpServer {
    /// 创建新的简单服务器
    pub fn new(port: u16) -> Result<Self, ServerError> {
        let config = ServerConfig {
            port,
            ..Default::default()
        };

        let socket = std::net::UdpSocket::bind(format!("0.0.0.0:{}", port))?;
        socket.set_nonblocking(true)?;

        Ok(Self {
            socket,
            clients: HashMap::new(),
            config,
        })
    }

    /// 处理一个包（非阻塞）
    pub fn tick(&mut self) -> Result<Option<(VoicePacket, SocketAddr)>, ServerError> {
        let mut buf = [0u8; VOICE_MAX_PACKET];

        match self.socket.recv_from(&mut buf) {
            Ok((len, addr)) => {
                let packet = VoicePacket::parse(&buf[..len])?;

                // 更新客户端
                if let Some(client) = self.clients.get_mut(&packet.sender_id) {
                    client.update(&packet);
                } else if self.clients.len() < self.config.max_clients {
                    self.clients
                        .insert(packet.sender_id, ClientInfo::new(addr, &packet));
                }

                // 广播
                if packet.packet_type == PacketType::Audio {
                    let sender_group = token_group(packet.token_hash);

                    for (_, client) in self.clients.iter() {
                        if client.sender_id == packet.sender_id && !packet.is_loopback() {
                            continue;
                        }

                        if client.context_hash != packet.context_hash {
                            continue;
                        }

                        let receiver_group = token_group(client.token_hash);
                        if !should_hear(sender_group, receiver_group) {
                            continue;
                        }

                        let _ = self.socket.send_to(&buf[..len], client.addr);
                    }
                }

                Ok(Some((packet, addr)))
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    /// 获取客户端数量
    pub fn client_count(&self) -> usize {
        self.clients.len()
    }

    /// 清理超时客户端
    pub fn cleanup(&mut self) {
        let timeout = self.config.client_timeout;
        self.clients.retain(|_, client| !client.is_timeout(timeout));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_server_config_default() {
        let config = ServerConfig::default();
        assert_eq!(config.port, DEFAULT_PORT);
        assert_eq!(config.max_clients, MAX_CLIENTS);
        assert_eq!(
            config.client_timeout,
            Duration::from_secs(CLIENT_TIMEOUT_SECS)
        );
    }

    #[test]
    fn test_client_info() {
        let packet = VoicePacket::new_audio(
            42,
            100,
            0x12345678,
            0xABCDEF00,
            100.0,
            200.0,
            vec![0xAA; 50],
        );

        let addr: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let client = ClientInfo::new(addr, &packet);

        assert_eq!(client.sender_id, 42);
        assert_eq!(client.context_hash, 0x12345678);
        assert_eq!(client.token_hash, 0xABCDEF00);
        assert_eq!(client.pos_x, 100.0);
        assert_eq!(client.pos_y, 200.0);
    }

    #[test]
    fn test_client_info_timeout() {
        let packet = VoicePacket::new_audio(1, 1, 0, 0, 0.0, 0.0, vec![]);
        let addr: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let client = ClientInfo::new(addr, &packet);

        // 刚创建的客户端不应该超时
        assert!(!client.is_timeout(Duration::from_secs(30)));

        // 0 秒超时应该总是触发
        assert!(client.is_timeout(Duration::from_secs(0)));
    }

    #[test]
    fn test_server_stats() {
        let mut stats = ServerStats::default();
        stats.start_time = Some(Instant::now());

        let uptime = stats.uptime_secs();
        assert!(uptime < 1); // 刚启动
    }

    #[tokio::test]
    async fn test_server_creation() {
        let server = VoiceServer::new(None);
        assert_eq!(server.state(), ServerState::Stopped);
    }

    #[tokio::test]
    async fn test_server_start_stop() {
        let server = VoiceServer::new(Some(ServerConfig {
            port: 19987, // 使用非常用端口
            ..Default::default()
        }));

        let result = server.start().await;
        assert!(result.is_ok());
        assert_eq!(server.state(), ServerState::Running);

        server.stop();
        assert_eq!(server.state(), ServerState::Stopped);
    }

    #[test]
    fn test_simple_server_creation() {
        let result = SimpleUdpServer::new(19988);
        assert!(result.is_ok());
    }
}
