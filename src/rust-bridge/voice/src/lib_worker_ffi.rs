// ============== Worker 线程 FFI ==============

use std::ffi::CStr;
use std::os::raw::c_char;
use std::sync::Arc;
use parking_lot::Mutex;

use crate::audio::CaptureFrame;
use crate::monotonic_micros;
use crate::network::protocol::{PacketType, VoicePacket};
use crate::worker::{WorkerCommand, WorkerContext, WorkerThread};
use crate::{PlayerSnapshot, VoiceConfig, VoiceConfigCABI, MAX_CLIENTS, VOICE_FRAME_SAMPLES, VOICE_MAX_PACKET};

/// Worker 句柄结构体
/// 封装 WorkerContext 和 WorkerThread，提供 C ABI 接口
pub struct VoiceWorkerHandle {
    pub context: Arc<WorkerContext>,
    pub worker: Mutex<Option<WorkerThread>>,
}

impl VoiceWorkerHandle {
    pub fn new() -> Self {
        Self {
            context: Arc::new(WorkerContext::new()),
            worker: Mutex::new(None),
        }
    }
}

// 创建 Worker，返回句柄
#[no_mangle]
pub extern "C" fn voice_worker_create() -> usize {
    let handle = Box::new(VoiceWorkerHandle::new());
    Box::into_raw(handle) as usize
}

// 销毁 Worker
#[no_mangle]
pub unsafe extern "C" fn voice_worker_destroy(handle: usize) {
    if handle == 0 {
        return;
    }
    let handle = Box::from_raw(handle as *mut VoiceWorkerHandle);
    // 停止工作线程（如果正在运行）
    {
        let mut worker_guard = handle.worker.lock();
        if let Some(worker) = worker_guard.take() {
            worker.stop();
        }
    } // 锁在这里释放
    // handle 在这里被 drop，自动释放内存
}

// 设置 Worker 配置
#[no_mangle]
pub unsafe extern "C" fn voice_worker_set_config(handle: usize, config: *const VoiceConfigCABI) {
    if handle == 0 || config.is_null() {
        return;
    }
    let config = &*config;
    let handle = &*(handle as *const VoiceWorkerHandle);

    let voice_config = VoiceConfig {
        mic_volume: config.mic_volume,
        noise_suppress: config.noise_suppress != 0,
        noise_suppress_strength: config.noise_suppress_strength,
        comp_threshold: config.comp_threshold,
        comp_ratio: config.comp_ratio,
        comp_attack_ms: config.comp_attack_ms,
        comp_release_ms: config.comp_release_ms,
        comp_makeup: config.comp_makeup,
        vad_enable: config.vad_enable != 0,
        vad_threshold: config.vad_threshold,
        vad_release_delay_ms: config.vad_release_delay_ms,
        stereo: config.stereo != 0,
        stereo_width: config.stereo_width,
        volume: config.volume,
        radius: config.radius,
        mic_mute: config.mic_mute != 0,
        test_mode: config.test_mode,
        ignore_distance: config.ignore_distance != 0,
        group_global: config.group_global != 0,
        token_hash: config.token_hash,
        context_hash: config.context_hash,
        filter_enable: config.filter_enable != 0,
        limiter: config.limiter,
        visibility_mode: config.visibility_mode,
        list_mode: config.list_mode,
        debug: config.debug != 0,
        group_mode: config.group_mode,
        hear_on_spec_pos: config.hear_on_spec_pos != 0,
        hear_in_spectate: config.hear_in_spectate != 0,
        hear_vad: config.hear_vad != 0,
        ptt_release_delay_ms: config.ptt_release_delay_ms,
        ptt_mode: config.ptt_mode,
        ptt_key: config.ptt_key,
        echo_cancel: config.echo_cancel != 0,
        echo_cancel_strength: config.echo_cancel_strength,
        agc_enable: config.agc_enable != 0,
        agc_target: config.agc_target,
        agc_max_gain: config.agc_max_gain,
        opus_bitrate: config.opus_bitrate,
        jitter_buffer_ms: config.jitter_buffer_ms,
        packet_loss_concealment: config.packet_loss_concealment != 0,
    };

    // 如果工作线程正在运行，通过命令通道发送配置更新
    if let Some(ref worker) = *handle.worker.lock() {
        let _ = worker.send(WorkerCommand::UpdateConfig(voice_config));
    } else {
        // 否则直接更新上下文配置
        handle.context.update_config(voice_config);
    }
}

// 启动 Worker 线程
// 必须在调用其他操作前启动工作线程
#[no_mangle]
pub unsafe extern "C" fn voice_worker_start(handle: usize) -> i32 {
    if handle == 0 {
        return 0;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    // 初始化编码器
    if !handle.context.init_encoder() {
        log::error!("Failed to initialize encoder");
        return 0;
    }

    // 启动工作线程
    let worker = WorkerThread::spawn(Arc::clone(&handle.context));
    *handle.worker.lock() = Some(worker);

    log::info!("Voice worker started");
    1
}

// 停止 Worker 线程
#[no_mangle]
pub unsafe extern "C" fn voice_worker_stop(handle: usize) {
    if handle == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);
    if let Some(worker) = handle.worker.lock().take() {
        worker.stop();
        log::info!("Voice worker stopped");
    }
    handle.context.shutdown_encoder();
}

// 提交捕获的音频数据
#[no_mangle]
pub unsafe extern "C" fn voice_worker_submit_capture(handle: usize, pcm: *const i16, samples: usize) {
    if handle == 0 || pcm.is_null() || samples == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    // 将 PCM 数据复制到捕获队列
    let pcm_slice = std::slice::from_raw_parts(pcm, samples.min(VOICE_FRAME_SAMPLES));
    let mut frame_pcm = [0i16; VOICE_FRAME_SAMPLES];
    frame_pcm[..pcm_slice.len()].copy_from_slice(pcm_slice);

    let frame = CaptureFrame {
        pcm: frame_pcm,
        timestamp: monotonic_micros(),
    };

    // 推入捕获队列
    handle.context.capture_queue.push(frame);
}

// 获取混音输出
#[no_mangle]
pub unsafe extern "C" fn voice_worker_mix_output(
    handle: usize,
    output: *mut i16,
    samples: usize,
    channels: usize,
) {
    if handle == 0 || output.is_null() || samples == 0 || channels == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    let output_slice = std::slice::from_raw_parts_mut(output, samples * channels);

    // 从播放缓冲区读取混音后的数据
    let playback_buffer = handle.context.playback_buffer.lock();

    // 准备左右声道缓冲区
    let mut left = vec![0i16; samples.min(VOICE_FRAME_SAMPLES)];
    let mut right = vec![0i16; samples.min(VOICE_FRAME_SAMPLES)];

    let read_samples = playback_buffer.read_stereo(&mut left, &mut right);

    // 混音到输出缓冲区
    for i in 0..read_samples {
        let base = i * channels;
        if channels == 1 {
            // 单声道：平均左右声道
            output_slice[i] = ((left[i] as i32 + right[i] as i32) / 2) as i16;
        } else {
            // 立体声或多声道
            output_slice[base] = left[i];
            if channels >= 2 {
                output_slice[base + 1] = right[i];
            }
            // 其他声道复制左声道或置零
            for ch in 2..channels {
                output_slice[base + ch] = left[i];
            }
        }
    }

    // 清零未填充的部分
    for i in (read_samples * channels)..output_slice.len() {
        output_slice[i] = 0;
    }
}

// 提交网络数据包（用于非 UDP 模式，如通过游戏网络传输）
#[no_mangle]
pub unsafe extern "C" fn voice_worker_submit_packet(handle: usize, data: *const u8, len: usize) {
    if handle == 0 || data.is_null() || len == 0 || len > VOICE_MAX_PACKET {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    let data_slice = std::slice::from_raw_parts(data, len);

    // 解析数据包并处理
    match VoicePacket::parse(data_slice) {
        Ok(packet) => {
            // 根据包类型处理
            match packet.packet_type {
                PacketType::Audio => {
                    // 音频包：放入对应 peer 的抖动缓冲区
                    let client_id = packet.sender_id as usize;
                    if client_id < MAX_CLIENTS {
                        let mut peers = handle.context.peers.lock();
                        let peer = &mut peers[client_id];

                        // 查找空闲的抖动包槽位
                        if let Some(slot) = peer.jitter_packets.iter_mut().find(|p| !p.valid) {
                            slot.valid = true;
                            slot.seq = packet.sequence;
                            slot.size = packet.opus_payload.len().min(slot.data.len());
                            slot.data[..slot.size]
                                .copy_from_slice(&packet.opus_payload[..slot.size]);
                            peer.queued_packets += 1;
                            peer.last_recv_time = Some(std::time::Instant::now());
                        }
                    }
                }
                PacketType::Ping | PacketType::Pong => {
                    // Ping/Pong 包：这里不直接处理，由工作线程处理
                    // 如果需要通过这种方式处理，可以放入接收队列
                }
            }
        }
        Err(e) => {
            log::debug!("Failed to parse submitted packet: {}", e);
        }
    }
}

// 设置玩家名称列表
#[no_mangle]
pub unsafe extern "C" fn voice_worker_set_name_lists(
    _handle: usize,
    whitelist: *const c_char,
    blacklist: *const c_char,
    mute: *const c_char,
    vad_allow: *const c_char,
    name_volumes: *const c_char,
) {
    if _handle == 0 {
        return;
    }
    let _handle = &*(_handle as *const VoiceWorkerHandle);

    // 安全地将 C 字符串转换为 Rust 字符串
    let whitelist_owned = if whitelist.is_null() {
        String::new()
    } else {
        CStr::from_ptr(whitelist).to_string_lossy().into_owned()
    };
    let blacklist_owned = if blacklist.is_null() {
        String::new()
    } else {
        CStr::from_ptr(blacklist).to_string_lossy().into_owned()
    };
    let mute_owned = if mute.is_null() {
        String::new()
    } else {
        CStr::from_ptr(mute).to_string_lossy().into_owned()
    };
    let _vad_allow_owned = if vad_allow.is_null() {
        String::new()
    } else {
        CStr::from_ptr(vad_allow).to_string_lossy().into_owned()
    };
    let _name_volumes_owned = if name_volumes.is_null() {
        String::new()
    } else {
        CStr::from_ptr(name_volumes).to_string_lossy().into_owned()
    };

    // 更新名单列表到上下文的 name_lists
    // 注意：WorkerContext 目前没有直接的 name_lists 字段
    // 这里需要添加到 WorkerContext 或通过其他方式处理
    // 暂时记录日志
    log::info!(
        "Worker name lists updated: whitelist_len={}, blacklist_len={}, mute_len={}",
        whitelist_owned.len(),
        blacklist_owned.len(),
        mute_owned.len()
    );
    // 避免未使用变量警告
    let _ = (_vad_allow_owned, _name_volumes_owned);
}

// 设置本地客户端 ID
#[no_mangle]
pub unsafe extern "C" fn voice_worker_set_local_client_id(handle: usize, client_id: i32) {
    if handle == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    if let Some(ref worker) = *handle.worker.lock() {
        let _ = worker.send(WorkerCommand::SetLocalClientId(client_id));
    } else {
        handle.context.set_local_client_id(client_id);
    }
}

// 设置上下文哈希
#[no_mangle]
pub unsafe extern "C" fn voice_worker_set_context_hash(handle: usize, hash: u32) {
    if handle == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    if let Some(ref worker) = *handle.worker.lock() {
        let _ = worker.send(WorkerCommand::SetContextHash(hash));
    } else {
        handle.context.set_context_hash(hash);
    }
}

// 设置 PTT 状态
#[no_mangle]
pub unsafe extern "C" fn voice_worker_set_ptt(handle: usize, active: i32) {
    if handle == 0 {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    let is_active = active != 0;
    if let Some(ref worker) = *handle.worker.lock() {
        let _ = worker.send(WorkerCommand::SetPttActive(is_active));
    } else {
        handle.context.set_ptt_active(is_active);
    }
}

// 获取麦克风电平
#[no_mangle]
pub unsafe extern "C" fn voice_worker_get_mic_level(handle: usize) -> i32 {
    if handle == 0 {
        return 0;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);
    handle.context.get_mic_level()
}

// 获取延迟 (ms)
#[no_mangle]
pub unsafe extern "C" fn voice_worker_get_ping(handle: usize) -> i32 {
    if handle == 0 {
        return -1;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);
    handle.context.get_ping_ms()
}

// 更新玩家列表
#[no_mangle]
pub unsafe extern "C" fn voice_worker_update_players(handle: usize, players: *const f32, count: usize) {
    if handle == 0 || players.is_null() || count == 0 || count > MAX_CLIENTS {
        return;
    }
    let handle = &*(handle as *const VoiceWorkerHandle);

    let player_data = std::slice::from_raw_parts(players, count * 5);
    let mut player_list = Vec::with_capacity(count);

    for i in 0..count {
        let base = i * 5;
        let client_id = player_data[base] as u16;
        let x = player_data[base + 1];
        let y = player_data[base + 2];
        let team = player_data[base + 3] as i32;
        let flags = player_data[base + 4] as i32;

        player_list.push(PlayerSnapshot {
            client_id,
            name: String::new(), // 名称通过其他方式设置
            x,
            y,
            team,
            is_spectator: (flags & 1) != 0,
            is_active: (flags & 2) != 0,
            spec_x: x,
            spec_y: y,
        });
    }

    if let Some(ref worker) = *handle.worker.lock() {
        let _ = worker.send(WorkerCommand::UpdatePlayers(player_list));
    } else {
        handle.context.update_players(player_list);
    }
}
