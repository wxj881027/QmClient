//! C++ 桥接层
//!
//! 使用 cxx 提供 Rust 与 C++ 之间的 FFI 接口
//! 函数名与 voice_rust.cpp 中的调用匹配

use crate::VoiceConfig;
use crate::dsp::DspChain;
use crate::codec::OpusCodec;
use crate::network::protocol::{calculate_context_hash, calculate_token_hash};
use crate::spatial::{calculate_spatial, SpatialConfig};

#[cxx::bridge]
mod ffi {
    // Rust 暴露给 C++ 的接口
    extern "Rust" {
        // 语音系统
        fn voice_system_create() -> usize;
        fn voice_system_destroy(handle: usize);
        fn voice_set_config(handle: usize, config: &VoiceConfigFFI);
        fn voice_set_ptt(handle: usize, active: bool);
        fn voice_set_vad(handle: usize, active: bool);
        fn voice_update_players(handle: usize, players: &[PlayerSnapshotFFI]);
        fn voice_get_mic_level(handle: usize) -> i32;
        fn voice_get_ping(handle: usize) -> i32;
        fn voice_is_speaking(handle: usize) -> bool;
        
        // DSP 处理器
        fn dsp_create() -> usize;
        fn dsp_destroy(dsp: usize);
        fn dsp_process(dsp: usize, samples: &mut [i16], config: &VoiceConfigFFI);
        
        // Opus 编解码器
        fn opus_create() -> Result<usize>;
        fn opus_destroy(codec: usize);
        fn opus_encode(codec: usize, pcm: &[i16], output: &mut [u8]) -> Result<usize>;
        fn opus_decode(codec: usize, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize>;
        fn opus_set_bitrate(codec: usize, bitrate: i32) -> Result<()>;
        
        // 空间音频
        fn voice_calculate_spatial(
            local_x: f32, local_y: f32,
            sender_x: f32, sender_y: f32,
            radius: f32, stereo_width: f32, volume: f32
        ) -> SpatialResultFFI;
        
        // 工具函数
        fn voice_context_hash(server_addr: &str) -> u32;
        fn voice_token_hash(token: &str) -> u32;
    }
    
    // C++ 暴露给 Rust 的接口
    unsafe extern "C++" {
        include!("game/client/components/qmclient/voice_callback.h");
        
        fn on_voice_packet(data: &[u8], sender_id: u16);
        fn on_voice_state_change(client_id: u16, speaking: bool);
        fn time_get() -> i64;
        fn time_freq() -> i64;
    }
    
    /// 语音配置 FFI 结构
    struct VoiceConfigFFI {
        mic_volume: i32,
        noise_suppress: bool,
        noise_suppress_strength: i32,
        comp_threshold: i32,
        comp_ratio: i32,
        comp_attack_ms: i32,
        comp_release_ms: i32,
        comp_makeup: i32,
        vad_enable: bool,
        vad_threshold: i32,
        vad_release_delay_ms: i32,
        stereo: bool,
        stereo_width: i32,
        volume: i32,
        radius: i32,
    }
    
    /// 玩家快照 FFI 结构
    struct PlayerSnapshotFFI {
        client_id: u16,
        x: f32,
        y: f32,
        team: i32,
        is_spectator: bool,
        is_active: bool,
    }
    
    /// 空间音频结果 FFI 结构
    struct SpatialResultFFI {
        distance_factor: f32,
        left_gain: f32,
        right_gain: f32,
        in_range: bool,
    }
}

// ============== 语音系统 ==============

/// 语音系统状态
struct VoiceSystemState {
    config: VoiceConfig,
    ptt_active: bool,
    vad_active: bool,
    mic_level: i32,
    ping: i32,
    speaking: bool,
}

fn voice_system_create() -> usize {
    let state = Box::new(VoiceSystemState {
        config: VoiceConfig::default(),
        ptt_active: false,
        vad_active: false,
        mic_level: 0,
        ping: 0,
        speaking: false,
    });
    Box::into_raw(state) as usize
}

fn voice_system_destroy(handle: usize) {
    if handle != 0 {
        unsafe {
            let _ = Box::from_raw(handle as *mut VoiceSystemState);
        }
    }
}

fn voice_set_config(handle: usize, config_ffi: &ffi::VoiceConfigFFI) {
    if handle == 0 {
        return;
    }
    
    let state = unsafe { &mut *(handle as *mut VoiceSystemState) };
    state.config = VoiceConfig {
        mic_volume: config_ffi.mic_volume,
        noise_suppress: config_ffi.noise_suppress,
        noise_suppress_strength: config_ffi.noise_suppress_strength,
        comp_threshold: config_ffi.comp_threshold,
        comp_ratio: config_ffi.comp_ratio,
        comp_attack_ms: config_ffi.comp_attack_ms,
        comp_release_ms: config_ffi.comp_release_ms,
        comp_makeup: config_ffi.comp_makeup,
        vad_enable: config_ffi.vad_enable,
        vad_threshold: config_ffi.vad_threshold,
        vad_release_delay_ms: config_ffi.vad_release_delay_ms,
        stereo: config_ffi.stereo,
        stereo_width: config_ffi.stereo_width,
        volume: config_ffi.volume,
        radius: config_ffi.radius,
    };
}

fn voice_set_ptt(handle: usize, active: bool) {
    if handle == 0 {
        return;
    }
    
    let state = unsafe { &mut *(handle as *mut VoiceSystemState) };
    state.ptt_active = active;
}

fn voice_set_vad(handle: usize, active: bool) {
    if handle == 0 {
        return;
    }
    
    let state = unsafe { &mut *(handle as *mut VoiceSystemState) };
    state.vad_active = active;
}

fn voice_update_players(handle: usize, _players: &[ffi::PlayerSnapshotFFI]) {
    if handle == 0 {
        return;
    }
    
    // TODO: 实现玩家位置更新
}

fn voice_get_mic_level(handle: usize) -> i32 {
    if handle == 0 {
        return 0;
    }
    
    let state = unsafe { &*(handle as *const VoiceSystemState) };
    state.mic_level
}

fn voice_get_ping(handle: usize) -> i32 {
    if handle == 0 {
        return 0;
    }
    
    let state = unsafe { &*(handle as *const VoiceSystemState) };
    state.ping
}

fn voice_is_speaking(handle: usize) -> bool {
    if handle == 0 {
        return false;
    }
    
    let state = unsafe { &*(handle as *const VoiceSystemState) };
    state.speaking
}

// ============== DSP 处理器 ==============

fn dsp_create() -> usize {
    let dsp = Box::new(DspChain::new());
    Box::into_raw(dsp) as usize
}

fn dsp_destroy(dsp: usize) {
    if dsp != 0 {
        unsafe {
            let _ = Box::from_raw(dsp as *mut DspChain);
        }
    }
}

fn dsp_process(dsp: usize, samples: &mut [i16], config_ffi: &ffi::VoiceConfigFFI) {
    if dsp == 0 {
        return;
    }
    
    let dsp = unsafe { &mut *(dsp as *mut DspChain) };
    
    let config = VoiceConfig {
        mic_volume: config_ffi.mic_volume,
        noise_suppress: config_ffi.noise_suppress,
        noise_suppress_strength: config_ffi.noise_suppress_strength,
        comp_threshold: config_ffi.comp_threshold,
        comp_ratio: config_ffi.comp_ratio,
        comp_attack_ms: config_ffi.comp_attack_ms,
        comp_release_ms: config_ffi.comp_release_ms,
        comp_makeup: config_ffi.comp_makeup,
        vad_enable: config_ffi.vad_enable,
        vad_threshold: config_ffi.vad_threshold,
        vad_release_delay_ms: config_ffi.vad_release_delay_ms,
        stereo: config_ffi.stereo,
        stereo_width: config_ffi.stereo_width,
        volume: config_ffi.volume,
        radius: config_ffi.radius,
    };
    dsp.process(samples, &config);
}

// ============== Opus 编解码器 ==============

fn opus_create() -> Result<usize, String> {
    match OpusCodec::new() {
        Ok(codec) => {
            let boxed = Box::new(codec);
            Ok(Box::into_raw(boxed) as usize)
        }
        Err(e) => Err(e.to_string()),
    }
}

fn opus_destroy(codec: usize) {
    if codec != 0 {
        unsafe {
            let _ = Box::from_raw(codec as *mut OpusCodec);
        }
    }
}

fn opus_encode(codec: usize, pcm: &[i16], output: &mut [u8]) -> Result<usize, String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.encode(pcm, output).map_err(|e| e.to_string())
}

fn opus_decode(codec: usize, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.decode(opus_data, pcm).map_err(|e| e.to_string())
}

fn opus_set_bitrate(codec: usize, bitrate: i32) -> Result<(), String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.set_bitrate(bitrate).map_err(|e| e.to_string())
}

// ============== 空间音频 ==============

fn voice_calculate_spatial(
    local_x: f32, local_y: f32,
    sender_x: f32, sender_y: f32,
    radius: f32, stereo_width: f32, volume: f32
) -> ffi::SpatialResultFFI {
    let config = SpatialConfig {
        radius,
        stereo_width,
        volume,
    };
    let result = calculate_spatial((local_x, local_y), (sender_x, sender_y), &config);
    
    ffi::SpatialResultFFI {
        distance_factor: result.distance_factor,
        left_gain: result.left_gain,
        right_gain: result.right_gain,
        in_range: result.in_range,
    }
}

// ============== 工具函数 ==============

fn voice_context_hash(server_addr: &str) -> u32 {
    calculate_context_hash(server_addr)
}

fn voice_token_hash(token: &str) -> u32 {
    calculate_token_hash(token)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_voice_system() {
        let handle = voice_system_create();
        assert_ne!(handle, 0);
        
        let config = ffi::VoiceConfigFFI {
            mic_volume: 100,
            noise_suppress: true,
            noise_suppress_strength: 50,
            comp_threshold: 20,
            comp_ratio: 25,
            comp_attack_ms: 20,
            comp_release_ms: 200,
            comp_makeup: 160,
            vad_enable: false,
            vad_threshold: 8,
            vad_release_delay_ms: 150,
            stereo: true,
            stereo_width: 100,
            volume: 100,
            radius: 50,
        };
        
        voice_set_config(handle, &config);
        voice_set_ptt(handle, true);
        
        assert!(!voice_is_speaking(handle));
        
        voice_system_destroy(handle);
    }

    #[test]
    fn test_dsp_processor() {
        let dsp = dsp_create();
        assert_ne!(dsp, 0);
        
        let mut samples = vec![1000i16; 960];
        let config = ffi::VoiceConfigFFI {
            mic_volume: 100,
            noise_suppress: true,
            noise_suppress_strength: 50,
            comp_threshold: 20,
            comp_ratio: 25,
            comp_attack_ms: 20,
            comp_release_ms: 200,
            comp_makeup: 160,
            vad_enable: false,
            vad_threshold: 8,
            vad_release_delay_ms: 150,
            stereo: true,
            stereo_width: 100,
            volume: 100,
            radius: 50,
        };
        dsp_process(dsp, &mut samples, &config);
        
        dsp_destroy(dsp);
    }

    #[test]
    fn test_opus_codec() {
        let codec = opus_create().unwrap();
        
        let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
        let mut encoded = vec![0u8; 1000];
        let len = opus_encode(codec, &samples, &mut encoded).unwrap();
        assert!(len > 0);
        
        let mut decoded = vec![0i16; 960];
        let samples_decoded = opus_decode(codec, &encoded[..len], &mut decoded).unwrap();
        assert!(samples_decoded > 0);
        
        opus_destroy(codec);
    }

    #[test]
    fn test_spatial_calculation() {
        let result = voice_calculate_spatial(0.0, 0.0, 100.0, 0.0, 1600.0, 1.0, 1.0);
        assert!(result.in_range);
        assert!(result.distance_factor > 0.0);
    }
}
