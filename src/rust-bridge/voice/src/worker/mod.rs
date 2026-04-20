//! Worker 线程模块
//!
//! 提供独立的语音工作线程，实现 5ms 固定轮询周期。
//! 负责音频采集处理、网络收发、抖动缓冲解码、编码器参数更新。

pub mod context;

pub use context::{EncoderParams, PlaybackBuffer, WorkerContext, WorkerStats};
pub use crate::audio::{CaptureFrame, CaptureQueue};

use crossbeam::channel::{bounded, Receiver, Sender, TryRecvError};
use parking_lot::Mutex;
use std::cell::RefCell;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::network::protocol::{PacketType, VoicePacket, VOICE_MAX_PACKET};
use crate::network::BandwidthEstimator;
use crate::{monotonic_micros, PlayerSnapshot, VoiceConfig, VOICE_FRAME_SAMPLES, MAX_CLIENTS};

// 线程本地编码缓冲区，用于重用内存分配
thread_local! {
    static ENCODED_BUFFER: RefCell<Vec<u8>> = RefCell::new(Vec::with_capacity(1500));
}

/// 工作线程命令
#[derive(Debug, Clone)]
pub enum WorkerCommand {
    /// 更新配置
    UpdateConfig(VoiceConfig),
    /// 更新玩家列表
    UpdatePlayers(Vec<PlayerSnapshot>),
    /// 设置本地客户端 ID
    SetLocalClientId(i32),
    /// 设置上下文哈希
    SetContextHash(u32),
    /// 设置 PTT 状态
    SetPttActive(bool),
    /// 设置 VAD 状态
    SetVadActive(bool),
    /// 关闭工作线程
    Shutdown,
}

/// 工作线程状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WorkerState {
    /// 空闲（未启动）
    Idle,
    /// 运行中
    Running,
    /// 正在停止
    Stopping,
    /// 已停止
    Stopped,
}

/// 工作线程
///
/// 独立的语音处理线程，提供 5ms 固定轮周期的实时音频处理。
/// 通过命令队列与主线程通信，所有共享状态通过 WorkerContext 访问。
pub struct WorkerThread {
    /// 命令发送通道（主线程 -> 工作线程）
    cmd_tx: Sender<WorkerCommand>,
    /// 线程句柄
    handle: Mutex<Option<JoinHandle<()>>>,
    /// 运行状态标志
    running: Arc<AtomicBool>,
    /// 共享上下文
    context: Arc<WorkerContext>,
}

impl WorkerThread {
    /// 创建并启动新的工作线程
    ///
    /// # 参数
    /// * `context` - 共享的 WorkerContext
    ///
    /// # 返回
    /// 新创建的 WorkerThread 实例
    pub fn spawn(context: Arc<WorkerContext>) -> Self {
        let (cmd_tx, cmd_rx) = bounded::<WorkerCommand>(100);
        let running = Arc::new(AtomicBool::new(true));

        let running_clone = Arc::clone(&running);
        let context_clone = Arc::clone(&context);

        let handle = thread::spawn(move || {
            worker_loop(running_clone, context_clone, cmd_rx);
        });

        log::info!("WorkerThread: Spawned new worker thread");

        Self {
            cmd_tx,
            handle: Mutex::new(Some(handle)),
            running,
            context,
        }
    }

    /// 发送命令到工作线程（非阻塞）
    ///
    /// # 参数
    /// * `cmd` - 要发送的命令
    ///
    /// # 返回
    /// * `Ok(())` - 命令已入队
    /// * `Err(())` - 通道已满或已关闭
    #[allow(clippy::result_unit_err)]
    pub fn send(&self, cmd: WorkerCommand) -> Result<(), ()> {
        self.cmd_tx.try_send(cmd).map_err(|_| ())
    }

    /// 停止工作线程
    ///
    /// 发送 Shutdown 命令并等待线程结束。
    pub fn stop(&self) {
        log::info!("WorkerThread: Stopping worker thread...");

        // 设置停止标志
        self.running.store(false, Ordering::SeqCst);

        // 发送关闭命令
        let _ = self.cmd_tx.send(WorkerCommand::Shutdown);

        // 等待线程结束
        if let Some(handle) = self.handle.lock().take() {
            if handle.join().is_err() {
                log::error!("WorkerThread: Failed to join worker thread");
            } else {
                log::info!("WorkerThread: Worker thread stopped");
            }
        }
    }

    /// 检查工作线程是否正在运行
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    /// 获取共享上下文
    pub fn context(&self) -> &Arc<WorkerContext> {
        &self.context
    }

    /// 获取运行统计
    pub fn stats(&self) -> WorkerStats {
        self.context.stats.read().clone()
    }
}

impl Drop for WorkerThread {
    fn drop(&mut self) {
        if self.is_running() {
            self.stop();
        }
    }
}

/// 工作线程主循环
///
/// 实现 5ms 固定轮询周期的实时音频处理。
/// 循环内执行：命令处理、网络接收、抖动解码、音频采集、编码器更新 。
fn worker_loop(running: Arc<AtomicBool>, ctx: Arc<WorkerContext>, cmd_rx: Receiver<WorkerCommand>) {
    log::info!("Worker loop started");

    let mut last_encoder_update = Instant::now();
    let mut loop_timer = Instant::now();

    // 音频处理缓冲区（重用分配）
    let mut capture_buffer = [0i16; VOICE_FRAME_SAMPLES];

    // 创建带宽估计器（初始比特率 32kbps）
    let mut bandwidth_estimator = BandwidthEstimator::new(32000);

    while running.load(Ordering::SeqCst) {
        let _loop_start = Instant::now();

        // ========== 1. 处理命令队列 ==========
        process_commands(&cmd_rx, &ctx);

        // ========== 2. 网络接收（非阻塞） ==========
        process_incoming(&ctx);

        // ========== 3. 抖动缓冲解码 ==========
        ctx.decode_jitter();

        // ========== 4. 音频采集处理 ==========
        process_capture(&ctx, &mut capture_buffer);

        // ========== 5. 编码器参数更新（每秒一次） ==========
        if last_encoder_update.elapsed() >= Duration::from_secs(1) {
            update_encoder_params_with_estimator(&ctx, &mut bandwidth_estimator);
            last_encoder_update = Instant::now();
        }

        // ========== 6. 更新统计 ==========
        {
            let mut stats = ctx.stats.write();
            stats.frames_mixed += 1;
        }

        // ========== 7. 固定 5ms 轮询间隔 ==========
        let elapsed = loop_timer.elapsed();
        let target_interval = Duration::from_millis(5);

        if elapsed < target_interval {
            let sleep_duration = target_interval - elapsed;
            thread::sleep(sleep_duration);
        } else if elapsed > Duration::from_millis(10) {
            // 处理超时警告
            log::warn!("Worker loop overtime: {:?}", elapsed);
        }

        loop_timer = Instant::now();
    }

    log::info!("Worker loop stopped");
}

/// 处理命令队列
fn process_commands(cmd_rx: &Receiver<WorkerCommand>, ctx: &WorkerContext) {
    loop {
        match cmd_rx.try_recv() {
            Ok(cmd) => match cmd {
                WorkerCommand::UpdateConfig(config) => {
                    ctx.update_config(config);
                    log::debug!("Worker: Config updated");
                }
                WorkerCommand::UpdatePlayers(players) => {
                    let count = players.len();
                    ctx.update_players(players);
                    log::debug!("Worker: Players updated (count: {})", count);
                }
                WorkerCommand::SetLocalClientId(id) => {
                    ctx.set_local_client_id(id);
                    log::debug!("Worker: Local client ID set to {}", id);
                }
                WorkerCommand::SetContextHash(hash) => {
                    ctx.set_context_hash(hash);
                    log::debug!("Worker: Context hash set to {}", hash);
                }
                WorkerCommand::SetPttActive(active) => {
                    ctx.set_ptt_active(active);
                    log::debug!("Worker: PTT set to {}", active);
                }
                WorkerCommand::SetVadActive(active) => {
                    ctx.set_vad_active(active);
                    log::debug!("Worker: VAD set to {}", active);
                }
                WorkerCommand::Shutdown => {
                    log::info!("Worker: Received shutdown command");
                    break;
                }
            },
            Err(TryRecvError::Empty) => break,
            Err(TryRecvError::Disconnected) => {
                log::error!("Worker: Command channel disconnected");
                break;
            }
        }
    }
}

/// 处理接收到的网络数据
///
/// 非阻塞接收 UDP 数据包并处理
fn process_incoming(ctx: &WorkerContext) {
    // 获取配置（用于 Ping/Pong 处理）
    let config = ctx.get_config();
    
    // 非阻塞接收所有可用数据包
    loop {
        match ctx.receive_packet() {
            Ok((data, _addr)) => {
                // 解析数据包
                match VoicePacket::parse(&data) {
                    Ok(packet) => {
                        // 处理不同类型的数据包
                        match packet.packet_type {
                            PacketType::Audio => {
                                // 音频数据包 - 放入对应对端的抖动缓冲区
                                let client_id = packet.sender_id as usize;
                                if client_id < MAX_CLIENTS {
                                    let mut peers = ctx.peers.lock();
                                    let peer = &mut peers[client_id];

                                    // 查找空闲的抖动包槽位
                                    if let Some(slot) = peer.jitter_packets.iter_mut().find(|p| !p.valid) {
                                        slot.valid = true;
                                        slot.seq = packet.sequence;
                                        slot.size = packet.opus_payload.len().min(slot.data.len());
                                        slot.data[..slot.size].copy_from_slice(&packet.opus_payload[..slot.size]);
                                        peer.queued_packets += 1;
                                        peer.last_recv_time = Some(Instant::now());
                                    }
                                }
                            }
                            PacketType::Ping => {
                                // Ping 包 - 发送 Pong 回复
                                log::debug!("Received Ping from {}, sending Pong", packet.sender_id);
                                
                                let pong_packet = VoicePacket::new_pong(
                                    ctx.local_client_id.load(Ordering::SeqCst) as u16,
                                    packet.sequence,
                                    config.context_hash,
                                    config.token_hash,
                                );
                                
                                let mut buf = [0u8; VOICE_MAX_PACKET];
                                let len = pong_packet.serialize(&mut buf);
                                if len > 0 {
                                    if let Err(e) = ctx.send_packet(&buf[..len]) {
                                        log::debug!("Failed to send Pong: {}", e);
                                    } else {
                                        log::debug!("Pong sent successfully");
                                    }
                                }
                            }
                            PacketType::Pong => {
                                // Pong 包 - 计算 RTT
                                log::debug!("Received Pong from {}", packet.sender_id);
                                
                                // 计算 RTT
                                let last_ping_time = ctx.last_ping_sent_time.load(Ordering::SeqCst);
                                if last_ping_time > 0 {
                                    let now = monotonic_micros();
                                    let rtt_ms = ((now - last_ping_time) / 1000) as i32;
                                    ctx.set_ping_ms(rtt_ms);
                                    
                                    log::debug!("RTT calculated: {} ms", rtt_ms);
                                }
                            }
                        }
                    }
                    Err(e) => {
                        log::debug!("Failed to parse voice packet: {}", e);
                    }
                }

                // 更新统计
                {
                    let mut stats = ctx.stats.write();
                    stats.packets_received += 1;
                    stats.bytes_received += data.len() as u64;
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                // 没有更多数据
                break;
            }
            Err(e) => {
                log::debug!("Receive error: {}", e);
                break;
            }
        }
    }
}

/// 处理音频采集
fn process_capture(ctx: &WorkerContext, buffer: &mut [i16; VOICE_FRAME_SAMPLES]) {
    // 从捕获队列读取音频帧
    let capture_frame = match ctx.capture_queue.pop() {
        Some(frame) => frame,
        None => return, // 没有新数据
    };

    // 复制 PCM 数据到缓冲区
    buffer.copy_from_slice(&capture_frame.pcm);

    let config = ctx.get_config();

    // 检查是否应该处理
    let should_process = check_should_process(ctx, &config);

    // 麦克风静音检查
    if config.mic_mute {
        ctx.set_mic_level(0);
        return;
    }

    // 计算峰值
    let peak = calculate_peak(buffer);
    update_mic_level(ctx, peak);

    // VAD 处理
    if config.vad_enable {
        process_vad(ctx, peak, &config);
    }

    // 检查发送条件
    let is_speaking = if config.vad_enable {
        ctx.is_vad_active()
    } else {
        should_process
    };

    if !is_speaking {
        return;
    }

    // 本地回环测试
    if config.test_mode == 1 {
        // 本地回环：直接将捕获的音频放入播放缓冲区（立体声）
        ctx.playback_buffer.lock().write_stereo(buffer, buffer);
        return;
    }

    // 编码并发送
    encode_and_send(ctx, buffer, &config);

    {
        let mut stats = ctx.stats.write();
        stats.frames_captured += 1;
    }
}

/// 检查是否应该处理音频
fn check_should_process(ctx: &WorkerContext, _config: &VoiceConfig) -> bool {
    let now = monotonic_micros();

    // PTT 检查
    if ctx.is_ptt_active() {
        return true;
    }

    // PTT 释放延迟检查
    let ptt_deadline = ctx.get_ptt_release_deadline();
    if ptt_deadline > 0 && now < ptt_deadline {
        return true;
    }

    false
}

/// 计算音频峰值（0-32767）
fn calculate_peak(samples: &[i16]) -> i32 {
    samples.iter().map(|&s| s.abs() as i32).max().unwrap_or(0)
}

/// 更新麦克风电平（0-100）
fn update_mic_level(ctx: &WorkerContext, peak: i32) {
    let level = (peak * 100 / 32767).clamp(0, 100);
    ctx.set_mic_level(level);
}

/// VAD 处理
fn process_vad(ctx: &WorkerContext, peak: i32, config: &VoiceConfig) {
    let now = monotonic_micros();
    let threshold = (config.vad_threshold * 32767 / 100).max(1);

    if peak >= threshold {
        // 语音检测激活
        ctx.set_vad_active(true);
        ctx.set_vad_release_deadline(0);
    } else {
        // 检查释放延迟
        let deadline = ctx.get_vad_release_deadline();
        if deadline == 0 {
            // 设置释放截止时间
            let delay_us = config.vad_release_delay_ms as i64 * 1000;
            ctx.set_vad_release_deadline(now + delay_us);
        } else if now >= deadline {
            // 释放延迟已过
            ctx.set_vad_active(false);
        }
    }
}

/// 编码音频并发送
///
/// # 参数
/// * `ctx` - Worker 上下文
/// * `pcm` - PCM 音频数据
/// * `config` - 语音配置
fn encode_and_send(ctx: &WorkerContext, pcm: &[i16], config: &VoiceConfig) {
    // 获取编码器
    let mut encoder_guard = ctx.encoder.lock();
    let encoder = match encoder_guard.as_mut() {
        Some(e) => e,
        None => {
            log::debug!("Encoder not initialized");
            return;
        }
    };

    // 使用线程本地存储重用缓冲区，避免每次分配新内存
    // 在闭包内完成编码和 packet 构建，避免数据拷贝
    let packet_opt = ENCODED_BUFFER.with(|buf| {
        let mut encoded = buf.borrow_mut();
        encoded.clear();
        encoded.resize(1500, 0);
        let encoded_len = match encoder.encode(pcm, &mut encoded) {
            Ok(len) => len,
            Err(e) => {
                log::error!("Opus encode error: {}", e);
                return None;
            }
        };

        // 获取序列号
        let sequence = ctx.next_sequence();

        // 获取本地玩家位置
        let local_id = ctx.local_client_id.load(Ordering::SeqCst) as u16;
        let (pos_x, pos_y) = {
            let players = ctx.players.read();
            players
                .iter()
                .find(|p| p.client_id == local_id)
                .map(|p| (p.x, p.y))
                .unwrap_or((0.0, 0.0))
        };

        // 构建语音数据包 - 从线程本地缓冲区克隆数据
        Some(VoicePacket {
            version: 3,
            packet_type: PacketType::Audio,
            payload_size: encoded_len as u16,
            context_hash: config.context_hash,
            token_hash: config.token_hash,
            flags: if ctx.is_vad_active() { 1 } else { 0 }, // VAD 标志
            sender_id: local_id,
            sequence,
            pos_x,
            pos_y,
            opus_payload: encoded[..encoded_len].to_vec(),
        })
    });

    let packet = match packet_opt {
        Some(p) => p,
        None => return,
    };

    // 序列化并发送
    let mut buf = [0u8; VOICE_MAX_PACKET];
    let len = packet.serialize(&mut buf);

    if len > 0 {
        match ctx.send_packet(&buf[..len]) {
            Ok(n) => {
                log::debug!("Sent {} bytes voice packet", n);
                // 更新发送统计
                let mut stats = ctx.stats.write();
                stats.packets_sent += 1;
                stats.bytes_sent += n as u64;
            }
            Err(e) => {
                log::debug!("Failed to send packet: {}", e);
            }
        }
    }
}

/// 更新编码器参数（自适应比特率 - 基础版本）
#[allow(dead_code)]
fn update_encoder_params(ctx: &WorkerContext) {
    let mut encoder_guard = ctx.encoder.lock();
    let encoder = match encoder_guard.as_mut() {
        Some(e) => e,
        None => return,
    };

    let _config = ctx.get_config();

    // 计算平均丢包率
    let mut loss_avg = 0.0f32;
    let mut count = 0;

    {
        let peers = ctx.peers.lock();
        for peer in peers.iter() {
            if peer.last_recv_time.is_some() {
                loss_avg += peer.loss_ewma;
                count += 1;
            }
        }
    }

    if count > 0 {
        loss_avg /= count as f32;
    }

    // 自适应比特率
    let (bitrate, fec) = if loss_avg <= 0.02 {
        (48000, false)
    } else if loss_avg <= 0.05 {
        (32000, true)
    } else if loss_avg <= 0.10 {
        (24000, true)
    } else {
        (16000, true)
    };

    // 更新编码器
    let _ = encoder.set_bitrate(bitrate);
    let _ = encoder.set_fec(fec);

    // 更新参数记录
    {
        let mut params = ctx.encoder_params.write();
        params.update(bitrate, (loss_avg * 100.0) as i32, fec);
    }

    log::debug!(
        "Encoder updated: bitrate={}, fec={}, loss={:.2}%",
        bitrate,
        fec,
        loss_avg * 100.0
    );
}

/// 更新编码器参数（使用 GCC 风格带宽估计器）
fn update_encoder_params_with_estimator(
    ctx: &WorkerContext,
    estimator: &mut BandwidthEstimator,
) {
    let mut encoder_guard = ctx.encoder.lock();
    let encoder = match encoder_guard.as_mut() {
        Some(e) => e,
        None => return,
    };

    // 计算平均丢包率并报告给估计器
    let mut loss_avg = 0.0f32;
    let mut count = 0;

    {
        let peers = ctx.peers.lock();
        for peer in peers.iter() {
            if peer.last_recv_time.is_some() {
                loss_avg += peer.loss_ewma;
                count += 1;
                // 报告丢包事件给估计器
                estimator.report_packet_loss(peer.loss_ewma > 0.05);
            }
        }
    }

    if count > 0 {
        loss_avg /= count as f32;
    }

    // 获取估计器推荐的比特率（平滑后的值）
    let estimated_bitrate = estimator.estimated_bitrate();

    // 根据比特率决定是否启用 FEC
    // 低比特率时启用 FEC 以提高鲁棒性
    let fec = estimated_bitrate < 32000 || loss_avg > 0.03;

    // 获取带宽使用状态用于调试
    let usage_state = estimator.usage_state();

    // 更新编码器
    let _ = encoder.set_bitrate(estimated_bitrate);
    let _ = encoder.set_fec(fec);

    // 更新参数记录
    {
        let mut params = ctx.encoder_params.write();
        params.update(estimated_bitrate, (loss_avg * 100.0) as i32, fec);
    }

    log::debug!(
        "Encoder updated (GCC): bitrate={}, fec={}, loss={:.2}%, state={:?}",
        estimated_bitrate,
        fec,
        loss_avg * 100.0,
        usage_state
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_worker_command_send() {
        let ctx = Arc::new(WorkerContext::new());
        let worker = WorkerThread::spawn(ctx);

        // 发送配置更新
        let config = VoiceConfig::default();
        assert!(worker.send(WorkerCommand::UpdateConfig(config)).is_ok());

        // 发送玩家更新
        let players = vec![PlayerSnapshot {
            client_id: 0,
            name: "Player0".to_string(),
            x: 100.0,
            y: 200.0,
            team: 0,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 200.0,
        }];
        assert!(worker.send(WorkerCommand::UpdatePlayers(players)).is_ok());

        // 停止工作线程
        worker.stop();
        assert!(!worker.is_running());
    }

    #[test]
    fn test_worker_state_transitions() {
        let ctx = Arc::new(WorkerContext::new());
        let worker = WorkerThread::spawn(ctx);

        assert!(worker.is_running());

        worker.stop();

        assert!(!worker.is_running());
    }

    #[test]
    fn test_calculate_peak() {
        let samples = [1000i16, -2000, 500, -3000, 100];
        assert_eq!(calculate_peak(&samples), 3000);

        let silence = [0i16; 960];
        assert_eq!(calculate_peak(&silence), 0);
    }

    #[test]
    fn test_check_should_process_ptt() {
        let ctx = Arc::new(WorkerContext::new());
        let config = VoiceConfig::default();

        // 初始状态：不应该处理
        assert!(!check_should_process(&ctx, &config));

        // 激活 PTT
        ctx.set_ptt_active(true);
        assert!(check_should_process(&ctx, &config));

        // 释放 PTT
        ctx.set_ptt_active(false);
        assert!(!check_should_process(&ctx, &config));
    }

    #[test]
    fn test_process_vad_activation() {
        let ctx = Arc::new(WorkerContext::new());
        let mut config = VoiceConfig::default();
        config.vad_enable = true;
        config.vad_threshold = 10; // 10%

        // 静音输入
        let silence = [0i16; 960];
        process_vad(&ctx, calculate_peak(&silence), &config);
        assert!(!ctx.is_vad_active());

        // 语音输入（超过阈值）
        let speech = [5000i16; 960]; // 约 15% 峰值
        process_vad(&ctx, calculate_peak(&speech), &config);
        assert!(ctx.is_vad_active());
    }
}
