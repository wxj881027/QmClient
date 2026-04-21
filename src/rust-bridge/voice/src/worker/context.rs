//! Worker 线程共享上下文
//!
//! 封装语音工作线程所需的所有共享状态，使用细粒度锁策略优化并发性能。
//! 每个字段的锁类型根据访问模式选择：
//! - RwLock: 读多写少（配置、玩家列表）
//! - Mutex: 读写频繁且需要独占访问（网络套接字、编解码器）
//! - Atomic: 简单状态标志（PTT/VAD 状态）

use std::net::{SocketAddr, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicI64, AtomicU16, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::Instant;

use parking_lot::{Mutex, RwLock};

use crate::audio::CaptureQueue;
use crate::codec::OpusCodec;
use crate::dsp::AcousticEchoCanceller;
use crate::{PeerState, PlayerSnapshot, VoiceConfig, MAX_CLIENTS};

/// 播放缓冲区
///
/// 混音后的音频数据缓冲区，由工作线程写入，播放线程读取。
/// 使用 2 的幂次容量以支持位运算优化取模。
#[derive(Debug)]
pub struct PlaybackBuffer {
    /// 立体声 PCM 数据（左右声道交替）
    /// 每帧 20ms @ 48kHz = 960 samples/声道
    buffer: Mutex<Vec<i16>>,
    /// 写入位置（采样数）
    write_pos: AtomicU32,
    /// 最大容量（采样数，始终为 2 的幂次）
    capacity: usize,
    /// 容量掩码 (capacity - 1)，用于位运算替代取模
    capacity_mask: usize,
}

impl PlaybackBuffer {
    /// 创建指定容量的播放缓冲区（以帧为单位）
    ///
    /// 容量会向上取整到最近的 2 的幂次，以支持位运算优化。
    pub fn new(frames: usize) -> Self {
        let min_capacity = frames * 960 * 2; // 立体声
                                             // 向上取整到最近的 2 的幂次
        let capacity = min_capacity.next_power_of_two();
        Self {
            buffer: Mutex::new(vec![0i16; capacity]),
            write_pos: AtomicU32::new(0),
            capacity,
            capacity_mask: capacity - 1,
        }
    }

    /// 写入立体声数据
    ///
    /// 使用位运算替代取模，优化热路径性能。
    pub fn write_stereo(&self, left: &[i16], right: &[i16]) -> usize {
        if left.len() != right.len() || left.len() > 960 {
            return 0;
        }

        let samples = left.len();
        let write_len = samples * 2; // 立体声

        {
            let mut buffer = self.buffer.lock();
            let write_pos = self.write_pos.load(Ordering::SeqCst) as usize;

            // 使用位掩码替代取模运算
            for i in 0..samples {
                let pos = (write_pos + i * 2) & self.capacity_mask;
                buffer[pos] = left[i];
                buffer[pos + 1] = right[i];
            }
        }

        self.write_pos.fetch_add(write_len as u32, Ordering::SeqCst);
        samples
    }

    /// 读取立体声数据
    ///
    /// 使用位运算替代取模，优化热路径性能。
    pub fn read_stereo(&self, left: &mut [i16], right: &mut [i16]) -> usize {
        if left.len() != right.len() || left.len() > 960 {
            return 0;
        }

        let samples = left.len();
        let read_len = samples * 2;

        let buffer = self.buffer.lock();
        let write_pos = self.write_pos.load(Ordering::SeqCst) as usize;
        let read_pos = write_pos.saturating_sub(read_len);

        let available = (write_pos - read_pos) / 2;
        let to_read = samples.min(available);

        // 使用位掩码替代取模运算
        for i in 0..to_read {
            let pos = (read_pos + i * 2) & self.capacity_mask;
            left[i] = buffer[pos];
            right[i] = buffer[pos + 1];
        }

        to_read
    }

    /// 获取可读取的采样数
    pub fn available_samples(&self) -> usize {
        let buffer = self.buffer.lock();
        let write_pos = self.write_pos.load(Ordering::SeqCst) as usize;
        let filled = buffer.iter().take(write_pos).filter(|&&s| s != 0).count();
        filled / 2
    }

    /// 清空缓冲区
    pub fn clear(&self) {
        self.write_pos.store(0, Ordering::SeqCst);
        let mut buffer = self.buffer.lock();
        buffer.fill(0);
    }

    /// 获取实际容量（2 的幂次）
    #[inline]
    pub fn capacity(&self) -> usize {
        self.capacity
    }
}

impl Default for PlaybackBuffer {
    fn default() -> Self {
        Self::new(8)
    }
}

// ============== 编解码器参数 ==============

/// 编码器参数
///
/// 记录 Opus 编码器的配置参数和最后更新时间，用于自适应码率控制。
#[derive(Debug, Clone, Copy)]
pub struct EncoderParams {
    /// 比特率 (bps)
    pub bitrate: i32,
    /// 预期丢包率 (0-100)
    pub packet_loss_perc: i32,
    /// 是否启用 FEC
    pub fec_enabled: bool,
    /// 最后更新时间
    pub last_update: Option<Instant>,
}

impl Default for EncoderParams {
    fn default() -> Self {
        Self {
            bitrate: 24000,
            packet_loss_perc: 0,
            fec_enabled: false,
            last_update: None,
        }
    }
}

impl EncoderParams {
    /// 创建默认参数
    pub fn new() -> Self {
        Self::default()
    }

    /// 更新参数并刷新时间戳
    pub fn update(&mut self, bitrate: i32, packet_loss_perc: i32, fec_enabled: bool) {
        self.bitrate = bitrate;
        self.packet_loss_perc = packet_loss_perc;
        self.fec_enabled = fec_enabled;
        self.last_update = Some(Instant::now());
    }

    /// 检查是否需要更新（超过指定间隔）
    pub fn needs_update(&self, interval: std::time::Duration) -> bool {
        match self.last_update {
            None => true,
            Some(last) => Instant::now().duration_since(last) >= interval,
        }
    }
}

// ============== 统计信息 ==============

/// 工作线程统计信息
///
/// 记录语音系统的运行统计，用于性能监控和调试。
#[derive(Debug, Clone)]
pub struct WorkerStats {
    /// 发送的音频包数
    pub packets_sent: u64,
    /// 接收的音频包数
    pub packets_received: u64,
    /// 丢包数
    pub packets_lost: u64,
    /// 发送的字节数
    pub bytes_sent: u64,
    /// 接收的字节数
    pub bytes_received: u64,
    /// 编码的帧数
    pub frames_encoded: u64,
    /// 解码的帧数
    pub frames_decoded: u64,
    /// 混音的帧数
    pub frames_mixed: u64,
    /// 捕获的帧数
    pub frames_captured: u64,
    /// 播放的帧数
    pub frames_played: u64,
    /// 抖动缓冲调整次数
    pub jitter_adjustments: u64,
    /// 当前抖动估计 (ms)
    pub current_jitter_ms: f32,
    /// 平均延迟 (ms)
    pub avg_latency_ms: f32,
    /// 启动时间
    pub start_time: Instant,
}

impl Default for WorkerStats {
    fn default() -> Self {
        Self {
            packets_sent: 0,
            packets_received: 0,
            packets_lost: 0,
            bytes_sent: 0,
            bytes_received: 0,
            frames_encoded: 0,
            frames_decoded: 0,
            frames_mixed: 0,
            frames_captured: 0,
            frames_played: 0,
            jitter_adjustments: 0,
            current_jitter_ms: 0.0,
            avg_latency_ms: 0.0,
            start_time: Instant::now(),
        }
    }
}

impl WorkerStats {
    /// 创建新的统计实例
    pub fn new() -> Self {
        Self::default()
    }

    /// 重置统计
    pub fn reset(&mut self) {
        *self = Self {
            start_time: Instant::now(),
            ..Default::default()
        };
    }

    /// 获取运行时间
    pub fn uptime(&self) -> std::time::Duration {
        Instant::now().duration_since(self.start_time)
    }

    /// 计算发送丢包率
    pub fn send_loss_rate(&self) -> f32 {
        let total = self.packets_sent + self.packets_lost;
        if total == 0 {
            0.0
        } else {
            self.packets_lost as f32 / total as f32
        }
    }

    /// 计算平均包大小（字节）
    pub fn avg_packet_size(&self) -> f32 {
        if self.packets_sent == 0 {
            0.0
        } else {
            self.bytes_sent as f32 / self.packets_sent as f32
        }
    }
}

// ============== Worker 上下文 ==============

/// Worker 线程共享上下文
///
/// 封装语音工作线程所需的所有共享状态。这是 VoiceSystem 的精简版本，
/// 专门为工作线程设计，使用更细粒度的锁策略。
pub struct WorkerContext {
    // ========== 配置状态 ==========
    /// 语音配置（读多写少，使用 RwLock）
    pub config: RwLock<VoiceConfig>,

    // ========== 网络状态 ==========
    /// UDP 套接字（需要独占访问发送/接收，使用 Mutex）
    pub socket: Mutex<Option<UdpSocket>>,
    /// 服务器地址（读多写少）
    pub server_addr: RwLock<Option<SocketAddr>>,
    /// 服务器地址是否有效
    pub server_addr_valid: AtomicBool,

    // ========== 音频设备状态 ==========
    /// 音频捕获队列（跨线程共享，使用 Arc）
    pub capture_queue: Arc<CaptureQueue>,
    /// 播放缓冲区（需要独占访问混音，使用 Mutex）
    pub playback_buffer: Mutex<PlaybackBuffer>,

    // ========== 编解码器 ==========
    /// Opus 编码器（需要可变访问编码，使用 Mutex）
    pub encoder: Mutex<Option<OpusCodec>>,
    /// 编码器参数（读多写少）
    pub encoder_params: RwLock<EncoderParams>,

    // ========== 回声消除 ==========
    /// AEC 处理器（需要可变访问，使用 Mutex）
    pub aec: Mutex<AcousticEchoCanceller>,
    /// AEC 是否启用
    pub aec_enabled: AtomicBool,

    // ========== 对端状态 ==========
    /// 对端玩家状态数组（需要可变访问，使用 Mutex）
    #[allow(private_interfaces)]
    pub peers: Mutex<Box<[PeerState]>>,

    // ========== 游戏状态快照 ==========
    /// 本地客户端 ID（原子访问）
    pub local_client_id: AtomicI32,
    /// 上下文哈希（原子访问）
    pub context_hash: AtomicU32,
    /// 玩家快照列表（读多写少）
    pub players: RwLock<Vec<PlayerSnapshot>>,

    // ========== PTT/VAD 状态 ==========
    /// PTT 是否激活
    pub ptt_active: AtomicBool,
    /// VAD 是否激活
    pub vad_active: AtomicBool,
    /// PTT 释放截止时间（微秒）
    pub ptt_release_deadline: AtomicI64,
    /// VAD 释放截止时间（微秒）
    pub vad_release_deadline: AtomicI64,

    // ========== 统计信息 ==========
    /// 运行统计（读多写少）
    pub stats: RwLock<WorkerStats>,
    /// 延迟 (ms)
    pub ping_ms: AtomicI32,
    /// 麦克风电平 (0-100)
    pub mic_level: AtomicI32,
    /// 序列号（发送包递增）
    pub sequence: AtomicU16,

    // ========== Ping 测量 ==========
    /// 上次发送 Ping 的时间（微秒）
    pub last_ping_sent_time: AtomicI64,
    /// 上次发送 Ping 的序列号
    pub last_ping_seq: AtomicU16,
}

impl WorkerContext {
    /// 创建新的 WorkerContext
    pub fn new() -> Self {
        Self {
            // 配置
            config: RwLock::new(VoiceConfig::default()),

            // 网络
            socket: Mutex::new(None),
            server_addr: RwLock::new(None),
            server_addr_valid: AtomicBool::new(false),

            // 音频设备
            capture_queue: Arc::new(CaptureQueue::new(32)),
            playback_buffer: Mutex::new(PlaybackBuffer::new(8)),

            // 编解码器
            encoder: Mutex::new(None),
            encoder_params: RwLock::new(EncoderParams::new()),

            // 回声消除
            aec: Mutex::new(AcousticEchoCanceller::new()),
            aec_enabled: AtomicBool::new(false),

            // 对端状态
            peers: Mutex::new((0..MAX_CLIENTS).map(|_| PeerState::new()).collect()),

            // 游戏状态
            local_client_id: AtomicI32::new(-1),
            context_hash: AtomicU32::new(0),
            players: RwLock::new(Vec::new()),

            // PTT/VAD
            ptt_active: AtomicBool::new(false),
            vad_active: AtomicBool::new(false),
            ptt_release_deadline: AtomicI64::new(0),
            vad_release_deadline: AtomicI64::new(0),

            // 统计
            stats: RwLock::new(WorkerStats::new()),
            ping_ms: AtomicI32::new(-1),
            mic_level: AtomicI32::new(0),
            sequence: AtomicU16::new(0),

            // Ping 测量
            last_ping_sent_time: AtomicI64::new(0),
            last_ping_seq: AtomicU16::new(0),
        }
    }

    /// 初始化编码器
    pub fn init_encoder(&self) -> bool {
        let mut encoder = self.encoder.lock();
        if encoder.is_some() {
            return true;
        }

        match OpusCodec::new() {
            Ok(enc) => {
                *encoder = Some(enc);
                log::info!("WorkerContext: Opus encoder initialized");
                true
            }
            Err(e) => {
                log::error!("WorkerContext: Failed to initialize Opus encoder: {}", e);
                false
            }
        }
    }

    /// 关闭编码器
    pub fn shutdown_encoder(&self) {
        let mut encoder = self.encoder.lock();
        *encoder = None;
    }

    /// 设置服务器地址
    pub fn set_server_addr(&self, addr: SocketAddr) {
        *self.server_addr.write() = Some(addr);
        self.server_addr_valid.store(true, Ordering::SeqCst);
    }

    /// 清除服务器地址
    pub fn clear_server_addr(&self) {
        *self.server_addr.write() = None;
        self.server_addr_valid.store(false, Ordering::SeqCst);
    }

    /// 获取服务器地址（如果有效）
    pub fn get_server_addr(&self) -> Option<SocketAddr> {
        if self.server_addr_valid.load(Ordering::SeqCst) {
            *self.server_addr.read()
        } else {
            None
        }
    }

    /// 设置本地客户端 ID
    pub fn set_local_client_id(&self, id: i32) {
        self.local_client_id.store(id, Ordering::SeqCst);
    }

    /// 获取本地客户端 ID
    pub fn get_local_client_id(&self) -> i32 {
        self.local_client_id.load(Ordering::SeqCst)
    }

    /// 设置上下文哈希
    pub fn set_context_hash(&self, hash: u32) {
        self.context_hash.store(hash, Ordering::SeqCst);
    }

    /// 获取上下文哈希
    pub fn get_context_hash(&self) -> u32 {
        self.context_hash.load(Ordering::SeqCst)
    }

    /// 设置 PTT 状态
    pub fn set_ptt_active(&self, active: bool) {
        self.ptt_active.store(active, Ordering::SeqCst);
        if active {
            self.ptt_release_deadline.store(0, Ordering::SeqCst);
        }
    }

    /// 获取 PTT 状态
    pub fn is_ptt_active(&self) -> bool {
        self.ptt_active.load(Ordering::SeqCst)
    }

    /// 设置 PTT 释放截止时间
    pub fn set_ptt_release_deadline(&self, deadline_micros: i64) {
        self.ptt_release_deadline
            .store(deadline_micros, Ordering::SeqCst);
    }

    /// 获取 PTT 释放截止时间
    pub fn get_ptt_release_deadline(&self) -> i64 {
        self.ptt_release_deadline.load(Ordering::SeqCst)
    }

    /// 设置 VAD 状态
    pub fn set_vad_active(&self, active: bool) {
        self.vad_active.store(active, Ordering::SeqCst);
    }

    /// 获取 VAD 状态
    pub fn is_vad_active(&self) -> bool {
        self.vad_active.load(Ordering::SeqCst)
    }

    /// 设置 VAD 释放截止时间
    pub fn set_vad_release_deadline(&self, deadline_micros: i64) {
        self.vad_release_deadline
            .store(deadline_micros, Ordering::SeqCst);
    }

    /// 获取 VAD 释放截止时间
    pub fn get_vad_release_deadline(&self) -> i64 {
        self.vad_release_deadline.load(Ordering::SeqCst)
    }

    /// 更新玩家列表
    pub fn update_players(&self, players: Vec<PlayerSnapshot>) {
        *self.players.write() = players;
    }

    /// 获取玩家位置
    pub fn get_player_pos(&self, client_id: u16) -> Option<(f32, f32)> {
        let players = self.players.read();
        players
            .iter()
            .find(|p| p.client_id == client_id && p.is_active)
            .map(|p| (p.x, p.y))
    }

    /// 获取本地玩家位置
    pub fn get_local_pos(&self) -> (f32, f32) {
        let local_id = self.get_local_client_id();
        if local_id < 0 {
            return (0.0, 0.0);
        }
        self.get_player_pos(local_id as u16).unwrap_or((0.0, 0.0))
    }

    /// 更新配置
    pub fn update_config(&self, config: VoiceConfig) {
        self.context_hash
            .store(config.context_hash, Ordering::SeqCst);
        *self.config.write() = config;
    }

    /// 获取配置副本
    pub fn get_config(&self) -> VoiceConfig {
        self.config.read().clone()
    }

    /// 获取序列号并递增
    pub fn next_sequence(&self) -> u16 {
        self.sequence.fetch_add(1, Ordering::SeqCst)
    }

    /// 设置延迟
    pub fn set_ping_ms(&self, ping: i32) {
        self.ping_ms.store(ping, Ordering::SeqCst);
    }

    /// 获取延迟
    pub fn get_ping_ms(&self) -> i32 {
        self.ping_ms.load(Ordering::SeqCst)
    }

    /// 设置麦克风电平
    pub fn set_mic_level(&self, level: i32) {
        self.mic_level.store(level.clamp(0, 100), Ordering::SeqCst);
    }

    /// 获取麦克风电平
    pub fn get_mic_level(&self) -> i32 {
        self.mic_level.load(Ordering::SeqCst)
    }

    /// 设置 AEC 启用状态
    pub fn set_aec_enabled(&self, enabled: bool) {
        self.aec_enabled.store(enabled, Ordering::SeqCst);
    }

    /// 获取 AEC 启用状态
    pub fn is_aec_enabled(&self) -> bool {
        self.aec_enabled.load(Ordering::SeqCst)
    }

    /// 重置 AEC 状态
    pub fn reset_aec(&self) {
        let mut aec = self.aec.lock();
        aec.reset();
    }

    /// 获取 AEC 收敛状态
    pub fn is_aec_converged(&self) -> bool {
        let aec = self.aec.lock();
        aec.is_converged()
    }

    /// 获取 AEC DTD 状态
    pub fn get_aec_dtd_state(&self) -> crate::dsp::DtdState {
        let aec = self.aec.lock();
        aec.dtd_state()
    }

    /// 发送数据包到服务器
    ///
    /// # 参数
    /// * `data` - 要发送的数据
    ///
    /// # 返回
    /// * `Ok(usize)` - 发送的字节数
    /// * `Err(std::io::Error)` - 发送失败
    pub fn send_packet(&self, data: &[u8]) -> Result<usize, std::io::Error> {
        let socket = self.socket.lock();
        let server_addr = self.server_addr.read();

        if let (Some(sock), Some(addr)) = (socket.as_ref(), server_addr.as_ref()) {
            sock.send_to(data, addr)
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::NotConnected,
                "Socket or server address not available",
            ))
        }
    }

    /// 接收数据包（非阻塞）
    ///
    /// # 返回
    /// * `Ok((Vec<u8>, SocketAddr))` - 接收到的数据和发送者地址
    /// * `Err(std::io::Error)` - 接收失败或无数据
    pub fn receive_packet(&self) -> Result<(Vec<u8>, SocketAddr), std::io::Error> {
        let socket = self.socket.lock();

        if let Some(sock) = socket.as_ref() {
            let mut buf = vec![0u8; 1500]; // MTU 大小
            match sock.recv_from(&mut buf) {
                Ok((len, addr)) => {
                    buf.truncate(len);
                    Ok((buf, addr))
                }
                Err(e) => Err(e),
            }
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::NotConnected,
                "Socket not available",
            ))
        }
    }

    /// 解码抖动缓冲区中的音频
    ///
    /// 遍历所有对端，从抖动缓冲区解码音频到播放帧。
    /// 这是 worker 线程的核心功能之一。
    pub fn decode_jitter(&self) {
        let mut peers = self.peers.lock();

        for peer in peers.iter_mut() {
            // 检查是否有数据包等待解码
            if peer.jitter_packets.is_empty() {
                continue;
            }

            // 解码逻辑：遍历抖动缓冲区中的包
            let packets_to_decode: Vec<_> = peer
                .jitter_packets
                .iter()
                .filter(|p| p.valid && p.seq >= peer.next_seq)
                .cloned()
                .collect();

            for packet in packets_to_decode {
                // 确保解码器已初始化
                if peer.decoder.is_none() && !peer.decoder_failed {
                    match OpusCodec::new() {
                        Ok(decoder) => {
                            peer.decoder = Some(decoder);
                        }
                        Err(e) => {
                            log::error!("Failed to create decoder: {}", e);
                            peer.decoder_failed = true;
                            continue;
                        }
                    }
                }

                // 解码音频数据
                if let Some(ref mut decoder) = peer.decoder {
                    let mut decoded = [0i16; 960]; // 20ms @ 48kHz
                    match decoder.decode(&packet.data[..packet.size], &mut decoded) {
                        Ok(samples) => {
                            // 写入播放帧队列（环形缓冲区）
                            if peer.frame_count < peer.playback_frames.len() {
                                let frame_idx = peer.frame_tail;
                                let frame = &mut peer.playback_frames[frame_idx];
                                frame.samples = samples.min(frame.pcm.len());
                                frame.pcm[..frame.samples]
                                    .copy_from_slice(&decoded[..frame.samples]);
                                frame.left_gain = packet.left_gain;
                                frame.right_gain = packet.right_gain;

                                peer.frame_tail =
                                    (peer.frame_tail + 1) % peer.playback_frames.len();
                                peer.frame_count += 1;
                                peer.next_seq = packet.seq.wrapping_add(1);
                            } else {
                                log::debug!("Playback buffer full, dropping decoded frame");
                            }
                        }
                        Err(e) => {
                            log::debug!("Decode error: {:?}", e);
                        }
                    }
                }

                // 标记包为已处理
                if let Some(p) = peer.jitter_packets.iter_mut().find(|p| p.seq == packet.seq) {
                    p.valid = false;
                }
            }
        }
    }

    /// 重置所有状态
    pub fn reset(&self) {
        self.shutdown_encoder();
        self.clear_server_addr();

        self.local_client_id.store(-1, Ordering::SeqCst);
        self.context_hash.store(0, Ordering::SeqCst);
        self.players.write().clear();

        self.ptt_active.store(false, Ordering::SeqCst);
        self.vad_active.store(false, Ordering::SeqCst);
        self.ptt_release_deadline.store(0, Ordering::SeqCst);
        self.vad_release_deadline.store(0, Ordering::SeqCst);

        self.ping_ms.store(-1, Ordering::SeqCst);
        self.mic_level.store(0, Ordering::SeqCst);
        self.sequence.store(0, Ordering::SeqCst);

        self.capture_queue.clear();
        self.playback_buffer.lock().clear();

        // 重置 AEC
        {
            let mut aec = self.aec.lock();
            aec.reset();
        }
        self.aec_enabled.store(false, Ordering::SeqCst);

        {
            let mut peers = self.peers.lock();
            for peer in peers.iter_mut() {
                peer.reset();
            }
        }

        self.stats.write().reset();

        log::info!("WorkerContext: All state reset");
    }
}

impl Default for WorkerContext {
    fn default() -> Self {
        Self::new()
    }
}

// ============== 单调时间辅助函数 ==============

/// 单调时间源（微秒）
/// 复用 lib.rs 中的 TIME_BASE
pub fn monotonic_micros() -> i64 {
    crate::monotonic_micros()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::audio::CaptureFrame;
    use crate::VOICE_FRAME_SAMPLES;

    #[test]
    fn test_capture_queue_basic() {
        let queue = CaptureQueue::new(4);
        assert!(queue.is_empty());
        assert_eq!(queue.len(), 0);

        // 推入一帧
        let frame = CaptureFrame {
            pcm: [1000i16; VOICE_FRAME_SAMPLES],
            timestamp: 12345,
        };
        assert!(queue.push(frame));
        assert_eq!(queue.len(), 1);

        // 弹出
        let popped = queue.pop();
        assert!(popped.is_some());
        let popped_frame = popped.unwrap();
        assert_eq!(popped_frame.pcm[0], 1000);
        assert_eq!(popped_frame.timestamp, 12345);
        assert!(queue.is_empty());
    }

    #[test]
    fn test_capture_queue_capacity() {
        let queue = CaptureQueue::new(16);

        // 填充队列
        for i in 0..16u16 {
            let frame = CaptureFrame {
                pcm: [i as i16; VOICE_FRAME_SAMPLES],
                timestamp: i as i64,
            };
            assert!(queue.push(frame), "Failed at {}", i);
        }

        // 队列已满，推入会触发溢出
        let frame = CaptureFrame {
            pcm: [99i16; VOICE_FRAME_SAMPLES],
            timestamp: 99,
        };
        assert!(queue.push(frame)); // 新的 API 会丢弃旧帧并成功推入
        assert!(queue.overflow_count() > 0);
    }

    #[test]
    fn test_playback_buffer_basic() {
        let buffer = PlaybackBuffer::new(4);

        let left = [1000i16; 960];
        let right = [2000i16; 960];

        assert_eq!(buffer.write_stereo(&left, &right), 960);

        let mut left_out = [0i16; 960];
        let mut right_out = [0i16; 960];

        let read = buffer.read_stereo(&mut left_out, &mut right_out);
        assert_eq!(read, 960);
        assert_eq!(left_out[0], 1000);
        assert_eq!(right_out[0], 2000);
    }

    #[test]
    fn test_encoder_params() {
        let mut params = EncoderParams::new();
        assert_eq!(params.bitrate, 24000);
        assert!(params.needs_update(std::time::Duration::from_secs(1)));

        params.update(32000, 5, true);
        assert_eq!(params.bitrate, 32000);
        assert_eq!(params.packet_loss_perc, 5);
        assert!(params.fec_enabled);
        assert!(!params.needs_update(std::time::Duration::from_secs(1)));
    }

    #[test]
    fn test_worker_stats() {
        let mut stats = WorkerStats::new();
        assert_eq!(stats.packets_sent, 0);

        stats.packets_sent = 100;
        stats.packets_lost = 10;

        assert!((stats.send_loss_rate() - 0.0909).abs() < 0.01);
    }

    #[test]
    fn test_worker_context_creation() {
        let ctx = WorkerContext::new();
        assert_eq!(ctx.get_local_client_id(), -1);
        assert_eq!(ctx.get_context_hash(), 0);
        assert_eq!(ctx.get_ping_ms(), -1);
        assert!(!ctx.is_ptt_active());
        assert!(!ctx.is_vad_active());
    }

    #[test]
    fn test_worker_context_config() {
        let ctx = WorkerContext::new();

        let config = VoiceConfig {
            mic_volume: 80,
            vad_enable: true,
            ..Default::default()
        };

        ctx.update_config(config.clone());
        let retrieved = ctx.get_config();

        assert_eq!(retrieved.mic_volume, 80);
        assert_eq!(retrieved.vad_enable, true);
    }

    #[test]
    fn test_worker_context_players() {
        let ctx = WorkerContext::new();
        ctx.set_local_client_id(5);

        let players = vec![
            PlayerSnapshot {
                client_id: 5,
                name: "Player5".to_string(),
                x: 100.0,
                y: 200.0,
                team: 0,
                is_spectator: false,
                is_active: true,
                spec_x: 100.0,
                spec_y: 200.0,
            },
            PlayerSnapshot {
                client_id: 10,
                name: "Player10".to_string(),
                x: 300.0,
                y: 400.0,
                team: 0,
                is_spectator: false,
                is_active: true,
                spec_x: 300.0,
                spec_y: 400.0,
            },
        ];

        ctx.update_players(players);

        assert_eq!(ctx.get_local_pos(), (100.0, 200.0));
        assert_eq!(ctx.get_player_pos(10), Some((300.0, 400.0)));
        assert_eq!(ctx.get_player_pos(99), None);
    }

    #[test]
    fn test_worker_context_ptt_vad() {
        let ctx = WorkerContext::new();

        ctx.set_ptt_active(true);
        assert!(ctx.is_ptt_active());

        ctx.set_ptt_release_deadline(1000000);
        assert_eq!(ctx.get_ptt_release_deadline(), 1000000);

        ctx.set_ptt_active(false);
        assert!(!ctx.is_ptt_active());

        ctx.set_vad_active(true);
        assert!(ctx.is_vad_active());

        ctx.set_vad_release_deadline(2000000);
        assert_eq!(ctx.get_vad_release_deadline(), 2000000);
    }

    #[test]
    fn test_worker_context_sequence() {
        let ctx = WorkerContext::new();

        assert_eq!(ctx.next_sequence(), 0);
        assert_eq!(ctx.next_sequence(), 1);
        assert_eq!(ctx.next_sequence(), 2);
    }

    #[test]
    fn test_worker_context_ping_mic() {
        let ctx = WorkerContext::new();

        ctx.set_ping_ms(50);
        assert_eq!(ctx.get_ping_ms(), 50);

        ctx.set_mic_level(75);
        assert_eq!(ctx.get_mic_level(), 75);

        // 测试边界
        ctx.set_mic_level(150);
        assert_eq!(ctx.get_mic_level(), 100);
    }

    #[test]
    fn test_worker_context_server_addr() {
        let ctx = WorkerContext::new();

        assert!(ctx.get_server_addr().is_none());
        assert!(!ctx.server_addr_valid.load(Ordering::SeqCst));

        let addr = SocketAddr::from(([127, 0, 0, 1], 8303));
        ctx.set_server_addr(addr);

        assert!(ctx.server_addr_valid.load(Ordering::SeqCst));
        assert_eq!(ctx.get_server_addr(), Some(addr));

        ctx.clear_server_addr();
        assert!(ctx.get_server_addr().is_none());
    }
}
