//! QmClient 语音聊天模块 - Rust 重构版本
//!
//! 基于 Opus 编解码器和自定义 UDP 协议 (RV01) 实现
//! 提供音频采集、处理、编码、网络传输、解码、混音播放完整流程

pub mod audio;
pub mod codec;
pub mod dsp;
pub mod jitter;
pub mod network;
pub mod spatial;

mod bridge;

use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Arc;

/// 语音配置
#[derive(Debug, Clone)]
pub struct VoiceConfig {
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

impl Default for VoiceConfig {
    fn default() -> Self {
        Self {
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
        }
    }
}

/// 玩家快照
#[derive(Debug, Clone, Copy)]
pub struct PlayerSnapshot {
    pub client_id: u16,
    pub x: f32,
    pub y: f32,
    pub team: i32,
    pub is_spectator: bool,
    pub is_active: bool,
}

impl Default for PlayerSnapshot {
    fn default() -> Self {
        Self {
            client_id: 0,
            x: 0.0,
            y: 0.0,
            team: 0,
            is_spectator: false,
            is_active: false,
        }
    }
}

/// 语音系统状态
pub struct VoiceSystem {
    config: parking_lot::RwLock<VoiceConfig>,
    players: parking_lot::RwLock<Vec<PlayerSnapshot>>,
    ptt_active: AtomicBool,
    vad_active: AtomicBool,
    mic_level: AtomicI32,
    ping_ms: AtomicI32,
}

impl VoiceSystem {
    pub fn new() -> Self {
        Self {
            config: parking_lot::RwLock::new(VoiceConfig::default()),
            players: parking_lot::RwLock::new(Vec::new()),
            ptt_active: AtomicBool::new(false),
            vad_active: AtomicBool::new(false),
            mic_level: AtomicI32::new(0),
            ping_ms: AtomicI32::new(0),
        }
    }

    pub fn set_config(&self, config: VoiceConfig) {
        *self.config.write() = config;
    }

    pub fn get_config(&self) -> VoiceConfig {
        self.config.read().clone()
    }

    pub fn update_players(&self, players: Vec<PlayerSnapshot>) {
        *self.players.write() = players;
    }

    pub fn set_ptt_active(&self, active: bool) {
        self.ptt_active.store(active, Ordering::SeqCst);
    }

    pub fn is_ptt_active(&self) -> bool {
        self.ptt_active.load(Ordering::SeqCst)
    }

    pub fn set_vad_active(&self, active: bool) {
        self.vad_active.store(active, Ordering::SeqCst);
    }

    pub fn is_vad_active(&self) -> bool {
        self.vad_active.load(Ordering::SeqCst)
    }

    pub fn set_mic_level(&self, level: i32) {
        self.mic_level.store(level, Ordering::SeqCst);
    }

    pub fn get_mic_level(&self) -> i32 {
        self.mic_level.load(Ordering::SeqCst)
    }

    pub fn set_ping_ms(&self, ping: i32) {
        self.ping_ms.store(ping, Ordering::SeqCst);
    }

    pub fn get_ping_ms(&self) -> i32 {
        self.ping_ms.load(Ordering::SeqCst)
    }
}

impl Default for VoiceSystem {
    fn default() -> Self {
        Self::new()
    }
}

/// 初始化语音系统
pub fn init() -> bool {
    log::info!("Initializing voice system...");
    true
}

/// 关闭语音系统
pub fn shutdown() {
    log::info!("Shutting down voice system...");
}

// ============== C ABI 导出 (供 C++ 直接调用) ==============

/// 语音系统状态 (C ABI 版本)
struct VoiceSystemHandle {
    system: VoiceSystem,
}

/// 创建语音系统
#[no_mangle]
pub extern "C" fn voice_system_create() -> usize {
    let handle = Box::new(VoiceSystemHandle {
        system: VoiceSystem::new(),
    });
    Box::into_raw(handle) as usize
}

/// 销毁语音系统
#[no_mangle]
pub extern "C" fn voice_system_destroy(handle: usize) {
    if handle != 0 {
        unsafe {
            let _ = Box::from_raw(handle as *mut VoiceSystemHandle);
        }
    }
}

/// 设置配置 (使用整数数组: 15 个参数)
#[no_mangle]
pub unsafe extern "C" fn voice_set_config(handle: usize, config: *const i32) {
    if handle == 0 || config.is_null() { return; }
    
    let handle = &*(handle as *const VoiceSystemHandle);
    handle.system.set_config(VoiceConfig {
        mic_volume: *config.offset(0),
        noise_suppress: *config.offset(1) != 0,
        noise_suppress_strength: *config.offset(2),
        comp_threshold: *config.offset(3),
        comp_ratio: *config.offset(4),
        comp_attack_ms: *config.offset(5),
        comp_release_ms: *config.offset(6),
        comp_makeup: *config.offset(7),
        vad_enable: *config.offset(8) != 0,
        vad_threshold: *config.offset(9),
        vad_release_delay_ms: *config.offset(10),
        stereo: *config.offset(11) != 0,
        stereo_width: *config.offset(12),
        volume: *config.offset(13),
        radius: *config.offset(14),
    });
}

/// 设置 PTT
#[no_mangle]
pub extern "C" fn voice_set_ptt(handle: usize, active: i32) {
    if handle == 0 { return; }
    
    let handle = unsafe { &*(handle as *const VoiceSystemHandle) };
    handle.system.set_ptt_active(active != 0);
}

/// 设置 VAD
#[no_mangle]
pub extern "C" fn voice_set_vad(handle: usize, active: i32) {
    if handle == 0 { return; }
    
    let handle = unsafe { &*(handle as *const VoiceSystemHandle) };
    handle.system.set_vad_active(active != 0);
}

/// 更新玩家
#[no_mangle]
pub unsafe extern "C" fn voice_update_players(handle: usize, players: *const f32, count: usize) {
    if handle == 0 || players.is_null() || count == 0 { return; }
    
    let handle = &*(handle as *const VoiceSystemHandle);
    // TODO: 实现玩家位置更新
}

/// 获取麦克风电平
#[no_mangle]
pub extern "C" fn voice_get_mic_level(handle: usize) -> i32 {
    if handle == 0 { return 0; }
    
    let handle = unsafe { &*(handle as *const VoiceSystemHandle) };
    handle.system.get_mic_level()
}

/// 获取延迟
#[no_mangle]
pub extern "C" fn voice_get_ping(handle: usize) -> i32 {
    if handle == 0 { return 0; }
    
    let handle = unsafe { &*(handle as *const VoiceSystemHandle) };
    handle.system.get_ping_ms()
}

/// 是否正在说话
#[no_mangle]
pub extern "C" fn voice_is_speaking(handle: usize) -> i32 {
    if handle == 0 { return 0; }
    
    let handle = unsafe { &*(handle as *const VoiceSystemHandle) };
    // 基于麦克风级别判断是否在说话
    if handle.system.get_mic_level() > 10 { 1 } else { 0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_voice_config_default() {
        let config = VoiceConfig::default();
        assert_eq!(config.mic_volume, 100);
        assert_eq!(config.noise_suppress, true);
        assert_eq!(config.vad_enable, false);
    }

    #[test]
    fn test_voice_system() {
        let system = VoiceSystem::new();
        assert_eq!(system.get_mic_level(), 0);
        assert_eq!(system.is_ptt_active(), false);
        
        system.set_ptt_active(true);
        assert_eq!(system.is_ptt_active(), true);
        
        system.set_mic_level(50);
        assert_eq!(system.get_mic_level(), 50);
    }
}
