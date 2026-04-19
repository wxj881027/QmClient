//! C++ 桥接层
//!
//! 提供 Rust 与 C++ 之间的 FFI 接口
//! 
//! 注意：这是实验性实现，实际项目中需要配置 cxx 桥接

use crate::{VoiceSystem, VoiceConfig, PlayerSnapshot};
use std::sync::Arc;

/// 语音系统句柄 (包装 VoiceSystem)
pub struct VoiceSystemHandle {
    system: Arc<VoiceSystem>,
}

impl VoiceSystemHandle {
    pub fn new() -> Self {
        Self {
            system: Arc::new(VoiceSystem::new()),
        }
    }
}

/// C++ 配置结构
#[repr(C)]
pub struct VoiceConfigFFI {
    pub mic_volume: i32,
    pub noise_suppress: bool,
    pub noise_suppress_strength: i32,
    pub comp_threshold: i32,
    pub comp_ratio: i32,
    pub comp_attack_ms: i32,
    pub comp_release_ms: i32,
    pub comp_makeup: i32,
    pub vad_enable: bool,
    pub vad_threshold: i32,
    pub vad_release_delay_ms: i32,
    pub stereo: bool,
    pub stereo_width: i32,
    pub volume: i32,
    pub radius: i32,
}

/// C++ 玩家快照结构
#[repr(C)]
pub struct PlayerSnapshotFFI {
    pub client_id: u16,
    pub x: f32,
    pub y: f32,
    pub team: i32,
    pub is_spectator: bool,
    pub is_active: bool,
}

/// 初始化语音系统
pub fn voice_init() -> bool {
    crate::init()
}

/// 关闭语音系统
pub fn voice_shutdown() {
    crate::shutdown()
}

/// 创建语音系统实例
pub fn voice_create() -> Box<VoiceSystemHandle> {
    Box::new(VoiceSystemHandle::new())
}

/// 销毁语音系统实例
pub fn voice_destroy(_handle: Box<VoiceSystemHandle>) {
    // Box 会在函数结束时自动 drop
}

/// 设置配置
pub fn voice_set_config(handle: &mut VoiceSystemHandle, config_ffi: &VoiceConfigFFI) {
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
    handle.system.set_config(config);
}

/// 设置 PTT 状态
pub fn voice_set_ptt(handle: &mut VoiceSystemHandle, active: bool) {
    handle.system.set_ptt_active(active);
}

/// 更新玩家快照
pub fn voice_update_players(handle: &mut VoiceSystemHandle, players_ffi: &[PlayerSnapshotFFI]) {
    let players: Vec<PlayerSnapshot> = players_ffi
        .iter()
        .map(|p| PlayerSnapshot {
            client_id: p.client_id,
            x: p.x,
            y: p.y,
            team: p.team,
            is_spectator: p.is_spectator,
            is_active: p.is_active,
        })
        .collect();
    handle.system.update_players(players);
}

/// 获取麦克风电平
pub fn voice_get_mic_level(handle: &VoiceSystemHandle) -> i32 {
    handle.system.get_mic_level()
}

/// 获取延迟
pub fn voice_get_ping(handle: &VoiceSystemHandle) -> i32 {
    handle.system.get_ping_ms()
}

/// 是否正在说话
pub fn voice_is_speaking(handle: &VoiceSystemHandle) -> bool {
    handle.system.is_ptt_active() || handle.system.is_vad_active()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_voice_system_handle() {
        let handle = VoiceSystemHandle::new();
        assert_eq!(handle.system.get_mic_level(), 0);
    }
}
