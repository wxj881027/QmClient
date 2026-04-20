//! FFI 端到端测试
//!
//! 测试从 C++ 调用 Rust FFI 的完整流程，验证 ABI 兼容性。
//!
//! 测试覆盖：
//! - 生命周期测试（create/destroy, init/shutdown）
//! - 配置管理测试
//! - PTT/VAD 控制测试
//! - 玩家管理测试
//! - 音频处理测试
//! - 边界条件测试（无效句柄、null 指针、超出范围参数）

use ddnet_voice::VoiceConfigCABI;
use std::ffi::CString;

// 直接使用 ddnet_voice crate 导出的 FFI 函数
// 这些函数在 lib.rs 中通过 #[no_mangle] pub extern "C" 导出
use ddnet_voice::{
    voice_decode_jitter, voice_get_mic_level, voice_get_ping, voice_is_speaking,
    voice_mix_audio, voice_process_frame, voice_receive_packet, voice_set_config,
    voice_set_context_hash, voice_set_local_client_id, voice_set_name_lists, voice_set_ptt,
    voice_set_vad, voice_system_create, voice_system_destroy, voice_system_init,
    voice_system_shutdown, voice_update_encoder_params, voice_update_players,
};

// ============== 辅助函数 ==============

/// 创建默认配置
fn create_default_config() -> VoiceConfigCABI {
    VoiceConfigCABI {
        mic_volume: 100,
        noise_suppress: 1,
        noise_suppress_strength: 50,
        comp_threshold: 20,
        comp_ratio: 25,
        comp_attack_ms: 20,
        comp_release_ms: 200,
        comp_makeup: 160,
        vad_enable: 0,
        vad_threshold: 8,
        vad_release_delay_ms: 150,
        stereo: 1,
        stereo_width: 100,
        volume: 100,
        radius: 50,
        mic_mute: 0,
        test_mode: 0,
        ignore_distance: 0,
        group_global: 0,
        token_hash: 0,
        context_hash: 0x12345678,
        filter_enable: 1,
        limiter: 50,
        visibility_mode: 0,
        list_mode: 0,
        debug: 0,
        group_mode: 0,
        hear_on_spec_pos: 0,
        hear_in_spectate: 0,
        hear_vad: 1,
        ptt_release_delay_ms: 0,
        ptt_mode: 0,
        ptt_key: 0,
        echo_cancel: 1,
        echo_cancel_strength: 50,
        agc_enable: 1,
        agc_target: 2000,
        agc_max_gain: 3000,
        opus_bitrate: 32000,
        jitter_buffer_ms: 60,
        packet_loss_concealment: 1,
    }
}

// ============== 生命周期测试 ==============

/// 测试完整的 FFI 生命周期：create -> init -> shutdown -> destroy
#[test]
fn test_ffi_lifecycle() {
    // 1. 创建系统
    let handle = voice_system_create();
    assert_ne!(handle, 0, "voice_system_create should return non-zero handle");

    // 2. 初始化
    let init_result = unsafe { voice_system_init(handle) };
    assert_eq!(init_result, 1, "voice_system_init should return 1 on success");

    // 3. 再次初始化（幂等性测试）
    let init_again = unsafe { voice_system_init(handle) };
    assert_eq!(init_again, 1, "voice_system_init should be idempotent");

    // 4. 关闭
    unsafe { voice_system_shutdown(handle) };

    // 5. 销毁
    unsafe { voice_system_destroy(handle) };
}

/// 测试多次创建和销毁
#[test]
fn test_ffi_multiple_create_destroy() {
    for i in 0..10 {
        let handle = voice_system_create();
        assert_ne!(handle, 0, "Iteration {}: create failed", i);

        let init_result = unsafe { voice_system_init(handle) };
        assert_eq!(init_result, 1, "Iteration {}: init failed", i);

        unsafe { voice_system_shutdown(handle) };
        unsafe { voice_system_destroy(handle) };
    }
}

/// 测试仅创建和销毁（不初始化）
#[test]
fn test_ffi_create_destroy_without_init() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);

    // 直接销毁，不初始化
    unsafe { voice_system_destroy(handle) };
}

// ============== 配置管理测试 ==============

/// 测试配置设置
#[test]
fn test_ffi_set_config() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);

    unsafe { voice_system_init(handle) };

    let config = create_default_config();
    unsafe { voice_set_config(handle, &config) };

    // 验证配置不会导致崩溃
    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试配置边界值
#[test]
fn test_ffi_config_boundary_values() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 最小值配置
    let min_config = VoiceConfigCABI {
        mic_volume: 0,
        noise_suppress: 0,
        noise_suppress_strength: 0,
        comp_threshold: 0,
        comp_ratio: 0,
        comp_attack_ms: 0,
        comp_release_ms: 0,
        comp_makeup: 0,
        vad_enable: 0,
        vad_threshold: 0,
        vad_release_delay_ms: 0,
        stereo: 0,
        stereo_width: 0,
        volume: 0,
        radius: 0,
        mic_mute: 0,
        test_mode: 0,
        ignore_distance: 0,
        group_global: 0,
        token_hash: 0,
        context_hash: 0,
        filter_enable: 0,
        limiter: 0,
        visibility_mode: 0,
        list_mode: 0,
        debug: 0,
        group_mode: 0,
        hear_on_spec_pos: 0,
        hear_in_spectate: 0,
        hear_vad: 0,
        ptt_release_delay_ms: 0,
        ptt_mode: 0,
        ptt_key: 0,
        echo_cancel: 0,
        echo_cancel_strength: 0,
        agc_enable: 0,
        agc_target: 0,
        agc_max_gain: 0,
        opus_bitrate: 0,
        jitter_buffer_ms: 0,
        packet_loss_concealment: 0,
    };
    unsafe { voice_set_config(handle, &min_config) };

    // 最大值配置
    let max_config = VoiceConfigCABI {
        mic_volume: 200,
        noise_suppress: 1,
        noise_suppress_strength: 100,
        comp_threshold: 100,
        comp_ratio: 100,
        comp_attack_ms: 1000,
        comp_release_ms: 1000,
        comp_makeup: 300,
        vad_enable: 1,
        vad_threshold: 100,
        vad_release_delay_ms: 1000,
        stereo: 1,
        stereo_width: 200,
        volume: 200,
        radius: 1000,
        mic_mute: 1,
        test_mode: 2,
        ignore_distance: 1,
        group_global: 1,
        token_hash: u32::MAX,
        context_hash: u32::MAX,
        filter_enable: 1,
        limiter: 100,
        visibility_mode: 2,
        list_mode: 2,
        debug: 1,
        group_mode: 2,
        hear_on_spec_pos: 1,
        hear_in_spectate: 1,
        hear_vad: 1,
        ptt_release_delay_ms: 1000,
        ptt_mode: 1,
        ptt_key: 255,
        echo_cancel: 1,
        echo_cancel_strength: 100,
        agc_enable: 1,
        agc_target: 10000,
        agc_max_gain: 10000,
        opus_bitrate: 128000,
        jitter_buffer_ms: 500,
        packet_loss_concealment: 1,
    };
    unsafe { voice_set_config(handle, &max_config) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

// ============== PTT/VAD 控制测试 ==============

/// 测试 PTT 控制
#[test]
fn test_ffi_ptt_control() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let config = create_default_config();
    unsafe { voice_set_config(handle, &config) };

    // 设置本地客户端 ID
    unsafe { voice_set_local_client_id(handle, 0) };

    // 初始状态：不说话
    let speaking = unsafe { voice_is_speaking(handle) };
    assert_eq!(speaking, 0, "Should not be speaking initially");

    // 激活 PTT
    unsafe { voice_set_ptt(handle, 1) };

    // PTT 激活后，需要音频帧才能触发 speaking
    // 这里只验证不会崩溃
    let speaking = unsafe { voice_is_speaking(handle) };
    assert_eq!(speaking, 0, "PTT active but no audio, should not be speaking");

    // 停止 PTT
    unsafe { voice_set_ptt(handle, 0) };

    let speaking = unsafe { voice_is_speaking(handle) };
    assert_eq!(speaking, 0, "Should not be speaking after PTT release");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试 VAD 控制
#[test]
fn test_ffi_vad_control() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 启用 VAD 的配置
    let config = VoiceConfigCABI {
        vad_enable: 1,
        vad_threshold: 10,
        ..create_default_config()
    };
    unsafe { voice_set_config(handle, &config) };

    // 设置 VAD 状态
    unsafe { voice_set_vad(handle, 1) };
    unsafe { voice_set_vad(handle, 0) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

// ============== 玩家管理测试 ==============

/// 测试玩家更新
#[test]
fn test_ffi_update_players() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 设置本地客户端 ID
    unsafe { voice_set_local_client_id(handle, 0) };

    // 玩家数据格式：[client_id, x, y, team, flags] * count
    let players: [f32; 15] = [
        0.0, 100.0, 100.0, 1.0, 3.0,  // 玩家 0 (本地): active, spectator
        1.0, 200.0, 200.0, 1.0, 2.0,  // 玩家 1: active
        2.0, 300.0, 300.0, 2.0, 2.0,  // 玩家 2: active
    ];

    unsafe { voice_update_players(handle, players.as_ptr(), 3) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试设置本地客户端 ID
#[test]
fn test_ffi_set_local_client_id() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 设置不同的客户端 ID
    unsafe { voice_set_local_client_id(handle, 0) };
    unsafe { voice_set_local_client_id(handle, 15) };
    unsafe { voice_set_local_client_id(handle, 63) }; // 最大有效 ID

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试设置上下文哈希
#[test]
fn test_ffi_set_context_hash() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    unsafe { voice_set_context_hash(handle, 0) };
    unsafe { voice_set_context_hash(handle, 0x12345678) };
    unsafe { voice_set_context_hash(handle, u32::MAX) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

// ============== 音频处理测试 ==============

/// 测试音频帧处理
#[test]
fn test_ffi_process_frame() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let config = create_default_config();
    unsafe { voice_set_config(handle, &config) };
    unsafe { voice_set_local_client_id(handle, 0) };

    // 创建测试音频帧 (960 samples @ 48kHz = 20ms)
    let mut pcm: Vec<i16> = (0..960).map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16).collect();
    let mut output: Vec<u8> = vec![0u8; 1200];

    // 激活 PTT
    unsafe { voice_set_ptt(handle, 1) };

    // 处理帧
    let result = unsafe {
        voice_process_frame(
            handle,
            pcm.as_mut_ptr(),
            pcm.len(),
            output.as_mut_ptr(),
            output.len(),
        )
    };

    // 结果应该是编码后的数据长度（可能为 0，因为需要初始化编码器）
    assert!(result >= 0, "voice_process_frame should return non-negative");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试混音输出
#[test]
fn test_ffi_mix_audio() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let config = create_default_config();
    unsafe { voice_set_config(handle, &config) };

    // 创建输出缓冲区
    let mut output: Vec<i16> = vec![0i16; 960 * 2]; // 立体声

    // 混音（没有音频数据时应该输出静音）
    unsafe { voice_mix_audio(handle, output.as_mut_ptr(), 960, 2) };

    // 验证输出为静音
    let is_silent = output.iter().all(|&s| s == 0);
    assert!(is_silent, "Output should be silent when no audio data");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试抖动缓冲解码
#[test]
fn test_ffi_decode_jitter() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 没有数据时调用应该安全
    unsafe { voice_decode_jitter(handle) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试编码器参数更新
#[test]
fn test_ffi_update_encoder_params() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 更新编码器参数
    unsafe { voice_update_encoder_params(handle) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

// ============== 名单过滤测试 ==============

/// 测试名单过滤设置
#[test]
fn test_ffi_set_name_lists() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let whitelist = CString::new("player1,player2").unwrap();
    let blacklist = CString::new("griefer").unwrap();
    let mute = CString::new("annoying").unwrap();
    let vad_allow = CString::new("trusted").unwrap();
    let name_volumes = CString::new("loud=50,quiet=150").unwrap();

    unsafe {
        voice_set_name_lists(
            handle,
            whitelist.as_ptr(),
            blacklist.as_ptr(),
            mute.as_ptr(),
            vad_allow.as_ptr(),
            name_volumes.as_ptr(),
        );
    }

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试空名单
#[test]
fn test_ffi_set_empty_name_lists() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let empty = CString::new("").unwrap();

    unsafe {
        voice_set_name_lists(
            handle,
            empty.as_ptr(),
            empty.as_ptr(),
            empty.as_ptr(),
            empty.as_ptr(),
            empty.as_ptr(),
        );
    }

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

// ============== 边界条件测试 ==============

/// 测试无效句柄处理
#[test]
fn test_ffi_invalid_handle() {
    // 句柄 0 应该被安全处理
    assert_eq!(unsafe { voice_system_init(0) }, 0);
    assert_eq!(unsafe { voice_is_speaking(0) }, 0);
    assert_eq!(unsafe { voice_get_mic_level(0) }, 0);
    assert_eq!(unsafe { voice_get_ping(0) }, -1);

    // 销毁句柄 0 不应崩溃
    unsafe { voice_system_destroy(0) };
    unsafe { voice_system_shutdown(0) };
}

/// 测试 null 指针处理
#[test]
fn test_ffi_null_pointer() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // null 配置指针
    unsafe { voice_set_config(handle, std::ptr::null()) };

    // null 玩家数据指针
    unsafe { voice_update_players(handle, std::ptr::null(), 1) };

    // null PCM 指针
    let mut output: Vec<u8> = vec![0u8; 1200];
    let result = unsafe {
        voice_process_frame(
            handle,
            std::ptr::null_mut(),
            960,
            output.as_mut_ptr(),
            output.len(),
        )
    };
    assert_eq!(result, 0, "Should return 0 for null PCM pointer");

    // null 输出指针
    let mut pcm: Vec<i16> = vec![0i16; 960];
    let result = unsafe {
        voice_process_frame(
            handle,
            pcm.as_mut_ptr(),
            pcm.len(),
            std::ptr::null_mut(),
            1200,
        )
    };
    assert_eq!(result, 0, "Should return 0 for null output pointer");

    // null 接收数据指针
    unsafe { voice_receive_packet(handle, std::ptr::null(), 100) };

    // null 混音输出指针
    unsafe { voice_mix_audio(handle, std::ptr::null_mut(), 960, 2) };

    // null 名单指针
    unsafe {
        voice_set_name_lists(
            handle,
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        );
    }

    unsafe { voice_system_destroy(handle) };
}

/// 测试超出范围的参数
#[test]
fn test_ffi_out_of_range_params() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 玩家数量超出限制 (> 64)
    let players: [f32; 5] = [0.0, 0.0, 0.0, 0.0, 0.0];
    unsafe { voice_update_players(handle, players.as_ptr(), 65) }; // 应该被拒绝

    // 玩家数量为 0
    unsafe { voice_update_players(handle, players.as_ptr(), 0) }; // 应该被拒绝

    // PCM 长度不足
    let mut pcm: Vec<i16> = vec![0i16; 100]; // 少于 960
    let mut output: Vec<u8> = vec![0u8; 1200];
    let result = unsafe {
        voice_process_frame(
            handle,
            pcm.as_mut_ptr(),
            pcm.len(),
            output.as_mut_ptr(),
            output.len(),
        )
    };
    assert_eq!(result, 0, "Should return 0 for insufficient PCM length");

    // 输出缓冲区不足
    let mut pcm2: Vec<i16> = vec![0i16; 960];
    let mut output2: Vec<u8> = vec![0u8; 10]; // 太小
    let result = unsafe {
        voice_process_frame(
            handle,
            pcm2.as_mut_ptr(),
            pcm2.len(),
            output2.as_mut_ptr(),
            output2.len(),
        )
    };
    assert_eq!(result, 0, "Should return 0 for insufficient output buffer");

    // 混音参数为 0
    unsafe { voice_mix_audio(handle, std::ptr::null_mut(), 0, 2) };
    unsafe { voice_mix_audio(handle, std::ptr::null_mut(), 960, 0) };

    // 接收包长度为 0
    let data: [u8; 10] = [0; 10];
    unsafe { voice_receive_packet(handle, data.as_ptr(), 0) };

    unsafe { voice_system_destroy(handle) };
}

/// 测试本地回环模式
#[test]
fn test_ffi_local_loopback_mode() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 本地回环模式 (test_mode = 1)
    let config = VoiceConfigCABI {
        test_mode: 1,
        ..create_default_config()
    };
    unsafe { voice_set_config(handle, &config) };
    unsafe { voice_set_local_client_id(handle, 0) };

    // 激活 PTT
    unsafe { voice_set_ptt(handle, 1) };

    // 处理帧
    let mut pcm: Vec<i16> = (0..960).map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16).collect();
    let mut output: Vec<u8> = vec![0u8; 1200];

    let result = unsafe {
        voice_process_frame(
            handle,
            pcm.as_mut_ptr(),
            pcm.len(),
            output.as_mut_ptr(),
            output.len(),
        )
    };

    // 本地回环模式不发送网络包，返回 0
    assert_eq!(result, 0, "Local loopback should not send network packets");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试麦克风静音
#[test]
fn test_ffi_mic_mute() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 麦克风静音配置
    let config = VoiceConfigCABI {
        mic_mute: 1,
        ..create_default_config()
    };
    unsafe { voice_set_config(handle, &config) };
    unsafe { voice_set_local_client_id(handle, 0) };

    // 激活 PTT
    unsafe { voice_set_ptt(handle, 1) };

    // 处理帧
    let mut pcm: Vec<i16> = vec![10000i16; 960]; // 高电平信号
    let mut output: Vec<u8> = vec![0u8; 1200];

    let result = unsafe {
        voice_process_frame(
            handle,
            pcm.as_mut_ptr(),
            pcm.len(),
            output.as_mut_ptr(),
            output.len(),
        )
    };

    // 静音时不应该发送数据
    assert_eq!(result, 0, "Should not send when mic is muted");

    // 麦克风电平应该为 0
    let level = unsafe { voice_get_mic_level(handle) };
    assert_eq!(level, 0, "Mic level should be 0 when muted");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试接收网络数据包
#[test]
fn test_ffi_receive_packet() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 设置上下文哈希
    unsafe { voice_set_context_hash(handle, 0x12345678) };

    // 构造有效的语音包
    // 包格式：MAGIC(4) + VERSION(1) + TYPE(1) + SENDER_ID(2) + SEQ(2) + CONTEXT(4) + TOKEN(4) + POS_X(4) + POS_Y(4) + PAYLOAD_SIZE(2) + PAYLOAD
    let mut packet: Vec<u8> = Vec::new();

    // MAGIC: "RV01"
    packet.extend_from_slice(b"RV01");
    // VERSION: 1
    packet.push(1);
    // TYPE: 1 (Audio)
    packet.push(1);
    // SENDER_ID: 1
    packet.extend_from_slice(&1u16.to_le_bytes());
    // SEQ: 1
    packet.extend_from_slice(&1u16.to_le_bytes());
    // CONTEXT: 0x12345678
    packet.extend_from_slice(&0x12345678u32.to_le_bytes());
    // TOKEN: 0
    packet.extend_from_slice(&0u32.to_le_bytes());
    // POS_X: 100.0
    packet.extend_from_slice(&100.0f32.to_le_bytes());
    // POS_Y: 200.0
    packet.extend_from_slice(&200.0f32.to_le_bytes());
    // PAYLOAD_SIZE: 0
    packet.extend_from_slice(&0u16.to_le_bytes());

    // 接收包（应该安全处理）
    unsafe { voice_receive_packet(handle, packet.as_ptr(), packet.len()) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试状态查询函数
#[test]
fn test_ffi_status_queries() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 初始状态
    let mic_level = unsafe { voice_get_mic_level(handle) };
    assert_eq!(mic_level, 0, "Initial mic level should be 0");

    let ping = unsafe { voice_get_ping(handle) };
    assert_eq!(ping, -1, "Initial ping should be -1 (unknown)");

    let speaking = unsafe { voice_is_speaking(handle) };
    assert_eq!(speaking, 0, "Should not be speaking initially");

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试重复初始化和关闭
#[test]
fn test_ffi_repeated_init_shutdown() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);

    // 第一次初始化
    assert_eq!(unsafe { voice_system_init(handle) }, 1);

    // 第一次关闭
    unsafe { voice_system_shutdown(handle) };

    // 重新初始化
    assert_eq!(unsafe { voice_system_init(handle) }, 1);

    // 重新关闭
    unsafe { voice_system_shutdown(handle) };

    unsafe { voice_system_destroy(handle) };
}

/// 测试不同声道数的混音
#[test]
fn test_ffi_mix_audio_different_channels() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    // 单声道混音
    let mut mono_output: Vec<i16> = vec![0i16; 960];
    unsafe { voice_mix_audio(handle, mono_output.as_mut_ptr(), 960, 1) };

    // 立体声混音
    let mut stereo_output: Vec<i16> = vec![0i16; 960 * 2];
    unsafe { voice_mix_audio(handle, stereo_output.as_mut_ptr(), 960, 2) };

    // 5.1 声道混音
    let mut surround_output: Vec<i16> = vec![0i16; 960 * 6];
    unsafe { voice_mix_audio(handle, surround_output.as_mut_ptr(), 960, 6) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}

/// 测试完整的音频处理流程
#[test]
fn test_ffi_full_audio_flow() {
    let handle = voice_system_create();
    assert_ne!(handle, 0);
    unsafe { voice_system_init(handle) };

    let config = create_default_config();
    unsafe { voice_set_config(handle, &config) };
    unsafe { voice_set_local_client_id(handle, 0) };

    // 激活 PTT
    unsafe { voice_set_ptt(handle, 1) };

    // 处理多个音频帧
    for frame_idx in 0..5 {
        let mut pcm: Vec<i16> = (0..960)
            .map(|i| {
                let phase = (i + frame_idx * 960) as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
                (phase.sin() * 16000.0) as i16
            })
            .collect();
        let mut output: Vec<u8> = vec![0u8; 1200];

        let result = unsafe {
            voice_process_frame(
                handle,
                pcm.as_mut_ptr(),
                pcm.len(),
                output.as_mut_ptr(),
                output.len(),
            )
        };

        // 第一帧可能返回 0（编码器初始化），后续帧应该有数据
        if frame_idx > 0 {
            assert!(result >= 0, "Frame {}: process should succeed", frame_idx);
        }
    }

    // 停止 PTT
    unsafe { voice_set_ptt(handle, 0) };

    // 混音输出
    let mut mix_output: Vec<i16> = vec![0i16; 960 * 2];
    unsafe { voice_mix_audio(handle, mix_output.as_mut_ptr(), 960, 2) };

    unsafe { voice_system_shutdown(handle) };
    unsafe { voice_system_destroy(handle) };
}
