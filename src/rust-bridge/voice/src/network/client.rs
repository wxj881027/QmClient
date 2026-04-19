//! UDP 语音客户端
//!
//! 使用 Tokio 异步运行时处理网络 I/O

use super::protocol::{VoicePacket, ParseError};
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use std::net::SocketAddr;
use std::sync::Arc;

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
}

/// 语音客户端
pub struct VoiceClient {
    socket: Arc<UdpSocket>,
    server_addr: SocketAddr,
    send_tx: mpsc::Sender<Vec<u8>>,
    recv_tx: mpsc::Sender<VoicePacket>,
}

impl VoiceClient {
    /// 创建新的语音客户端
    /// 
    /// # 参数
    /// * `server_addr` - 服务器地址 (例如 "42.194.185.210:9987")
    pub async fn new(server_addr: &str) -> Result<(Self, mpsc::Receiver<VoicePacket>), ClientError> {
        let socket = UdpSocket::bind("0.0.0.0:0").await?;
        let server_addr: SocketAddr = server_addr
            .parse()
            .map_err(|_| ClientError::InvalidAddress(server_addr.to_string()))?;

        // 连接到服务器 (设置默认发送目标)
        socket.connect(server_addr).await?;

        let (send_tx, send_rx) = mpsc::channel::<Vec<u8>>(32);
        let (recv_tx, recv_rx) = mpsc::channel::<VoicePacket>(32);

        let socket = Arc::new(socket);

        // 启动发送任务
        let socket_clone = socket.clone();
        tokio::spawn(async move {
            Self::send_loop(socket_clone, send_rx).await;
        });

        // 启动接收任务
        let recv_tx_clone = recv_tx.clone();
        tokio::spawn(async move {
            Self::recv_loop(socket, recv_tx_clone).await;
        });

        // 创建一个新的 socket 用于客户端结构体（占位）
        let placeholder_socket = UdpSocket::bind("0.0.0.0:0").await?;
        let client = Self {
            socket: Arc::new(placeholder_socket),
            server_addr,
            send_tx,
            recv_tx,
        };

        Ok((client, recv_rx))
    }

    /// 发送语音包
    pub async fn send_packet(&self, packet: &VoicePacket) -> Result<(), ClientError> {
        let mut buf = vec![0u8; 1200];
        let len = packet.serialize(&mut buf);
        
        if len == 0 {
            return Err(ClientError::Io(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "Failed to serialize packet",
            )));
        }
        
        buf.truncate(len);
        self.send_tx.send(buf).await.map_err(|_| ClientError::ChannelClosed)?;
        
        Ok(())
    }

    /// 发送音频数据
    pub async fn send_audio(
        &self,
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
        pos_x: f32,
        pos_y: f32,
        opus_data: Vec<u8>,
    ) -> Result<(), ClientError> {
        let packet = VoicePacket::new_audio(
            sender_id,
            sequence,
            context_hash,
            token_hash,
            pos_x,
            pos_y,
            opus_data,
        );
        self.send_packet(&packet).await
    }

    /// 发送 PING
    pub async fn send_ping(
        &self,
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
    ) -> Result<(), ClientError> {
        let packet = VoicePacket::new_ping(sender_id, sequence, context_hash, token_hash);
        self.send_packet(&packet).await
    }

    /// 发送 PONG
    pub async fn send_pong(
        &self,
        sender_id: u16,
        sequence: u16,
        context_hash: u32,
        token_hash: u32,
    ) -> Result<(), ClientError> {
        let packet = VoicePacket::new_pong(sender_id, sequence, context_hash, token_hash);
        self.send_packet(&packet).await
    }

    /// 发送循环
    async fn send_loop(socket: Arc<UdpSocket>, mut rx: mpsc::Receiver<Vec<u8>>) {
        while let Some(data) = rx.recv().await {
            if let Err(e) = socket.send(&data).await {
                log::warn!("Failed to send packet: {}", e);
            }
        }
        log::info!("Send loop ended");
    }

    /// 接收循环
    async fn recv_loop(socket: Arc<UdpSocket>, tx: mpsc::Sender<VoicePacket>) {
        let mut buf = [0u8; 1200];
        
        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, _)) => {
                    match VoicePacket::parse(&buf[..len]) {
                        Ok(packet) => {
                            if tx.send(packet).await.is_err() {
                                log::info!("Receive channel closed, stopping recv loop");
                                break;
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
    }

    /// 获取服务器地址
    pub fn server_addr(&self) -> SocketAddr {
        self.server_addr
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::time::{sleep, Duration};

    #[tokio::test]
    async fn test_client_creation() {
        // 使用本地地址测试
        let result = VoiceClient::new("127.0.0.1:9987").await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_invalid_address() {
        let result = VoiceClient::new("not-an-address").await;
        assert!(matches!(result, Err(ClientError::InvalidAddress(_))));
    }

    // 注意：下面的测试需要实际的服务器，通常在集成测试中运行
    // 
    // #[tokio::test]
    // async fn test_send_receive() {
    //     let (client, mut rx) = VoiceClient::new("127.0.0.1:9987").await.unwrap();
    //     
    //     // 发送 PING
    //     client.send_ping(1, 1, 0, 0).await.unwrap();
    //     
    //     // 等待回复
    //     let timeout = sleep(Duration::from_secs(1));
    //     tokio::pin!(timeout);
    //     
    //     tokio::select! {
    //         Some(packet) = rx.recv() => {
    //             assert_eq!(packet.packet_type, PacketType::Pong);
    //         }
    //         _ = &mut timeout => {
    //             // 超时，可能服务器未运行
    //         }
    //     }
    // }
}
