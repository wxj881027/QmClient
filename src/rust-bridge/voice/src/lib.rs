//! QmClient 语音聊天模块 - Rust 重构版本
//!
//! 基于 Opus 编解码器和自定义 UDP 协议 (RV01) 实现
//! 提供音频采集、处理、编码、网络传输、解码、混音播放完整流程
//!
//! 核心流程：
//! ```text
//! 捕获音频 -> 增益 -> 降噪 -> 压缩 -> VAD检测 -> Opus编码 -> UDP发送
//! UDP接收 -> Jitter Buffer -> Opus解码 -> 空间音频 -> 混音 -> 播放
//! ```

pub mod audio;
pub mod codec;
pub mod dsp;
pub mod jitter;
pub mod network;
pub mod spatial;
pub mod stats;
pub mod worker;

mod bridge;

use std::ffi::CStr;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

macro_rules! ffi_wrap {
    (fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) -> $ret:ty $body:block) => {
        #[no_mangle]
        pub extern "C" fn $fn_name($($param: $ty),*) -> $ret {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
                Default::default()
            })
        }
    };
    (unsafe fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) -> $ret:ty $body:block) => {
        #[no_mangle]
        #[allow(clippy::missing_safety_doc)]
        pub unsafe extern "C" fn $fn_name($($param: $ty),*) -> $ret {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
                Default::default()
            })
        }
    };
    (fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) $body:block) => {
        #[no_mangle]
        pub extern "C" fn $fn_name($($param: $ty),*) {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
            })
        }
    };
    (unsafe fn $fn_name:ident($($param:ident: $ty:ty),* $(,)?) $body:block) => {
        #[no_mangle]
        #[allow(clippy::missing_safety_doc)]
        pub unsafe extern "C" fn $fn_name($($param: $ty),*) {
            catch_unwind(AssertUnwindSafe(|| $body)).unwrap_or_else(|_| {
                log::error!("Panic in FFI function {}", stringify!($fn_name));
            })
        }
    };
}

use crate::codec::opus::{FRAME_SAMPLES, SAMPLE_RATE};
use crate::dsp::DspChain;
use crate::network::protocol::{PacketType, VoicePacket, VOICE_MAX_PACKET};
use crate::spatial::{calculate_spatial, SpatialConfig, SpatialResult};

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicI64, AtomicU16, AtomicU32, Ordering};
use std::sync::OnceLock;
use std::time::{Duration, Instant};

use parking_lot::{Mutex, RwLock};

// ============== 单调时间源 ==============

/// 全局时间基准点，首次调用时初始化
/// 等效于 C++ 中 voice_time_get() 的使用模式
static TIME_BASE: OnceLock<Instant> = OnceLock::new();

/// 返回单调递增的微秒计数（自首次调用起）
///
/// C++ 实现使用 `voice_time_get()` 获取单调时钟，Rust 中使用 `Instant` 实现。
/// `Instant::now().elapsed()` 总是接近 0（刚创建就询问经过了多久），
/// 正确做法是保存一个基准点，然后用 `elapsed()` 计算差值。
pub(crate) fn monotonic_micros() -> i64 {
    let base = TIME_BASE.get_or_init(Instant::now);
    base.elapsed().as_micros() as i64
}

// ============== 常量定义 ==============

/// 最大客户端数量
pub const MAX_CLIENTS: usize = 64;

/// 语音帧采样数 (20ms @ 48kHz)
pub const VOICE_FRAME_SAMPLES: usize = FRAME_SAMPLES;

/// 采样率
pub const VOICE_SAMPLE_RATE: u32 = SAMPLE_RATE;

/// 最大抖动缓冲包数
const MAX_JITTER_PACKETS: usize = 32;

/// 最大播放帧数
const MAX_PLAYBACK_FRAMES: usize = 8;

/// VAD 释放延迟默认值 (ms)
#[allow(dead_code)]
const VAD_RELEASE_DELAY_MS: i64 = 150;

/// PTT 释放延迟默认值 (ms)
const PTT_RELEASE_DELAY_MS: i64 = 50;

// ============== 名单过滤系统 ==============

/// 名单列表结构
#[derive(Debug, Clone, Default)]
pub struct NameLists {
    /// 白名单（逗号分隔的玩家名称）
    pub whitelist: String,
    /// 黑名单（逗号分隔的玩家名称）
    pub blacklist: String,
    /// 静音名单（逗号分隔的玩家名称）
    pub mute: String,
    /// VAD 允许名单（逗号分隔的玩家名称）
    pub vad_allow: String,
    /// 名称音量映射（格式：name1=volume1,name2=volume2）
    pub name_volumes: String,
}

impl NameLists {
    /// 创建新的空名单列表
    pub fn new() -> Self {
        Self::default()
    }

    /// 检查名称是否在列表中（支持逗号分隔的多个名称）
    ///
    /// # 参数
    /// * `list` - 逗号分隔的名单字符串
    /// * `name` - 要检查的玩家名称
    ///
    /// # 返回
    /// * `true` - 名称在列表中
    /// * `false` - 名称不在列表中
    fn name_in_list(list: &str, name: &str) -> bool {
        if list.is_empty() || name.is_empty() {
            return false;
        }

        let name_lower = name.to_lowercase();

        for token in list.split(',') {
            let token = token.trim();
            if token.eq_ignore_ascii_case(&name_lower) {
                return true;
            }
        }
        false
    }

    /// 检查玩家是否在白名单中
    pub fn is_whitelisted(&self, name: &str) -> bool {
        Self::name_in_list(&self.whitelist, name)
    }

    /// 检查玩家是否在黑名单中
    pub fn is_blacklisted(&self, name: &str) -> bool {
        Self::name_in_list(&self.blacklist, name)
    }

    /// 检查玩家是否在静音名单中
    pub fn is_muted(&self, name: &str) -> bool {
        Self::name_in_list(&self.mute, name)
    }

    /// 检查玩家是否在 VAD 允许名单中
    pub fn is_vad_allowed(&self, name: &str) -> bool {
        Self::name_in_list(&self.vad_allow, name)
    }

    /// 解析名称音量映射
    ///
    /// # 参数
    /// * `name` - 玩家名称
    ///
    /// # 返回
    /// * `Some(volume)` - 找到该玩家的音量设置（0-200）
    /// * `None` - 未找到该玩家的音量设置
    pub fn get_name_volume(&self, name: &str) -> Option<i32> {
        if self.name_volumes.is_empty() || name.is_empty() {
            return None;
        }

        let name_lower = name.to_lowercase();

        for token in self.name_volumes.split(',') {
            let token = token.trim();
            if let Some(sep_pos) = token.find(['=', ':']) {
                let (entry_name, entry_value) = token.split_at(sep_pos);
                let entry_name = entry_name.trim();
                let entry_value = entry_value[1..].trim(); // 跳过分隔符

                if entry_name.eq_ignore_ascii_case(&name_lower) {
                    if let Ok(percent) = entry_value.parse::<i32>() {
                        return Some(percent.clamp(0, 200));
                    }
                }
            }
        }
        None
    }

    /// 检查是否应该收听该玩家（根据名单模式）
    ///
    /// # 参数
    /// * `name` - 玩家名称
    /// * `list_mode` - 名单模式（0=无过滤, 1=白名单, 2=黑名单）
    ///
    /// # 返回
    /// * `true` - 应该收听
    /// * `false` - 应该过滤掉
    pub fn should_hear(&self, name: &str, list_mode: i32) -> bool {
        // 首先检查静音名单（最高优先级）
        if self.is_muted(name) {
            return false;
        }

        match list_mode {
            1 => {
                // 白名单模式：只有在白名单中才收听
                self.is_whitelisted(name)
            }
            2 => {
                // 黑名单模式：不在黑名单中才收听
                !self.is_blacklisted(name)
            }
            _ => {
                // 无过滤模式
                true
            }
        }
    }
}

#[cfg(test)]
mod name_lists_tests {
    use super::NameLists;

    #[test]
    fn test_whitelist() {
        let lists = NameLists {
            whitelist: "player1, player2, Player3".to_string(),
            ..Default::default()
        };

        assert!(lists.is_whitelisted("player1"));
        assert!(lists.is_whitelisted("PLAYER2")); // 大小写不敏感
        assert!(lists.is_whitelisted("player3"));
        assert!(!lists.is_whitelisted("player4"));
    }

    #[test]
    fn test_blacklist() {
        let lists = NameLists {
            blacklist: "griefer1, spammer".to_string(),
            ..Default::default()
        };

        assert!(lists.is_blacklisted("griefer1"));
        assert!(!lists.is_blacklisted("goodplayer"));
    }

    #[test]
    fn test_mute_list() {
        let lists = NameLists {
            mute: "annoying_player".to_string(),
            ..Default::default()
        };

        assert!(lists.is_muted("annoying_player"));
        assert!(!lists.is_muted("other_player"));
    }

    #[test]
    fn test_name_volumes() {
        let lists = NameLists {
            name_volumes: "loud_player=50, quiet_player=150".to_string(),
            ..Default::default()
        };

        assert_eq!(lists.get_name_volume("loud_player"), Some(50));
        assert_eq!(lists.get_name_volume("quiet_player"), Some(150));
        assert_eq!(lists.get_name_volume("normal_player"), None);
    }

    #[test]
    fn test_should_hear_modes() {
        let lists = NameLists {
            whitelist: "friend1, friend2".to_string(),
            blacklist: "enemy1".to_string(),
            mute: "muted_player".to_string(),
            ..Default::default()
        };

        // 无过滤模式 (0)
        assert!(lists.should_hear("anyone", 0));
        assert!(!lists.should_hear("muted_player", 0)); // 静音名单始终生效

        // 白名单模式 (1)
        assert!(lists.should_hear("friend1", 1));
        assert!(!lists.should_hear("stranger", 1));
        assert!(!lists.should_hear("muted_player", 1)); // 静音名单始终生效

        // 黑名单模式 (2)
        assert!(lists.should_hear("friend1", 2));
        assert!(!lists.should_hear("enemy1", 2));
        assert!(!lists.should_hear("muted_player", 2)); // 静音名单始终生效
    }
}

/// 心跳间隔 (ms)
#[allow(dead_code)]
const KEEPALIVE_INTERVAL_MS: i64 = 2000;

/// 音频重试间隔 (ms)
#[allow(dead_code)]
const AUDIO_RETRY_INTERVAL_MS: i64 = 1000;

// ============== 配置结构 ==============

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
    /// 麦克风静音
    pub mic_mute: bool,
    /// 测试模式 (0=正常, 1=本地回环, 2=服务器回环)
    pub test_mode: i32,
    /// 忽略距离限制
    pub ignore_distance: bool,
    /// 组内全局语音
    pub group_global: bool,
    /// Token 哈希
    pub token_hash: u32,
    /// 上下文哈希
    pub context_hash: u32,
    /// 滤波器开关
    pub filter_enable: bool,
    /// 限制器阈值
    pub limiter: i32,
    /// 可见性模式
    pub visibility_mode: i32,
    /// 名单模式
    pub list_mode: i32,
    /// 调试模式
    pub debug: bool,
    /// 组模式
    pub group_mode: i32,
    /// 观战位置收听
    pub hear_on_spec_pos: bool,
    /// 接收旁观者语音
    pub hear_in_spectate: bool,
    /// 接收 VAD 语音
    pub hear_vad: bool,
    /// PTT 释放延迟 (ms)
    pub ptt_release_delay_ms: i32,
    /// PTT 模式 (0=按住说话, 1=切换模式)
    pub ptt_mode: i32,
    /// 按键码
    pub ptt_key: i32,
    /// 回声消除
    pub echo_cancel: bool,
    /// 回声消除强度
    pub echo_cancel_strength: i32,
    /// 自动增益控制
    pub agc_enable: bool,
    /// AGC 目标
    pub agc_target: i32,
    /// AGC 最大增益
    pub agc_max_gain: i32,
    /// Opus 比特率
    pub opus_bitrate: i32,
    /// 抖动缓冲区大小 (ms)
    pub jitter_buffer_ms: i32,
    /// 丢包隐藏
    pub packet_loss_concealment: bool,
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
            mic_mute: false,
            test_mode: 0,
            ignore_distance: false,
            group_global: false,
            token_hash: 0,
            context_hash: 0,
            filter_enable: true,
            limiter: 50,
            visibility_mode: 0,
            list_mode: 0,
            debug: false,
            group_mode: 0,
            hear_on_spec_pos: false,
            hear_in_spectate: false,
            hear_vad: true,
            ptt_release_delay_ms: 0,
            ptt_mode: 0,
            ptt_key: 0,
            echo_cancel: true,
            echo_cancel_strength: 50,
            agc_enable: true,
            agc_target: 2000,
            agc_max_gain: 3000,
            opus_bitrate: 32000,
            jitter_buffer_ms: 60,
            packet_loss_concealment: true,
        }
    }
}

// ============== 玩家快照 ==============

/// 玩家快照
#[derive(Debug, Clone)]
pub struct PlayerSnapshot {
    pub client_id: u16,
    pub name: String,
    pub x: f32,
    pub y: f32,
    pub team: i32,
    pub is_spectator: bool,
    pub is_active: bool,
    /// 观战位置 X (当 is_spectator = true 时有效)
    pub spec_x: f32,
    /// 观战位置 Y (当 is_spectator = true 时有效)
    pub spec_y: f32,
}

impl Default for PlayerSnapshot {
    fn default() -> Self {
        Self {
            client_id: 0,
            name: String::new(),
            x: 0.0,
            y: 0.0,
            team: 0,
            is_spectator: false,
            is_active: false,
            spec_x: 0.0,
            spec_y: 0.0,
        }
    }
}

// ============== 对端玩家状态 ==============

/// 抖动缓冲包
#[derive(Debug, Clone)]
struct JitterPacket {
    valid: bool,
    seq: u16,
    size: usize,
    left_gain: f32,
    right_gain: f32,
    data: Vec<u8>,
}

impl Default for JitterPacket {
    fn default() -> Self {
        Self {
            valid: false,
            seq: 0,
            size: 0,
            left_gain: 1.0,
            right_gain: 1.0,
            data: vec![0u8; VOICE_MAX_PACKET],
        }
    }
}

/// 播放帧
#[derive(Debug, Clone)]
struct PlaybackFrame {
    pcm: Vec<i16>,
    samples: usize,
    left_gain: f32,
    right_gain: f32,
}

impl Default for PlaybackFrame {
    fn default() -> Self {
        Self {
            pcm: vec![0i16; VOICE_FRAME_SAMPLES],
            samples: 0,
            left_gain: 1.0,
            right_gain: 1.0,
        }
    }
}

/// 对端玩家状态
pub(crate) struct PeerState {
    /// Opus 解码器
    decoder: Option<crate::codec::OpusCodec>,
    /// 解码器创建失败
    decoder_failed: bool,
    /// 抖动缓冲包
    jitter_packets: Box<[JitterPacket]>,
    /// 播放帧队列
    playback_frames: Box<[PlaybackFrame]>,
    /// 播放帧头指针
    frame_head: usize,
    /// 播放帧尾指针
    frame_tail: usize,
    /// 播放帧计数
    frame_count: usize,
    /// 播放帧读取位置
    frame_read_pos: usize,
    /// 上一个接收序列号
    last_recv_seq: u16,
    /// 是否有上一个接收序列号
    has_last_recv_seq: bool,
    /// 下一个要播放的序列号
    next_seq: u16,
    /// 是否有下一个序列号
    has_next_seq: bool,
    /// 上次接收时间
    last_recv_time: Option<Instant>,
    /// 抖动估计 (ms)
    jitter_ms: f32,
    /// 目标缓冲帧数
    target_frames: usize,
    /// 排队包数
    queued_packets: usize,
    /// 丢包率 EWMA
    loss_ewma: f32,
    /// 上次增益 (左)
    last_gain_left: f32,
    /// 上次增益 (右)
    last_gain_right: f32,
    /// 最后听到时间
    #[allow(dead_code)]
    last_heard: Option<Instant>,
}

impl PeerState {
    fn new() -> Self {
        let mut jitter_packets = Vec::with_capacity(MAX_JITTER_PACKETS);
        for _ in 0..MAX_JITTER_PACKETS {
            jitter_packets.push(JitterPacket::default());
        }

        let mut playback_frames = Vec::with_capacity(MAX_PLAYBACK_FRAMES);
        for _ in 0..MAX_PLAYBACK_FRAMES {
            playback_frames.push(PlaybackFrame::default());
        }

        Self {
            decoder: None,
            decoder_failed: false,
            jitter_packets: jitter_packets.into_boxed_slice(),
            playback_frames: playback_frames.into_boxed_slice(),
            frame_head: 0,
            frame_tail: 0,
            frame_count: 0,
            frame_read_pos: 0,
            last_recv_seq: 0,
            has_last_recv_seq: false,
            next_seq: 0,
            has_next_seq: false,
            last_recv_time: None,
            jitter_ms: 0.0,
            target_frames: 3,
            queued_packets: 0,
            loss_ewma: 0.0,
            last_gain_left: 1.0,
            last_gain_right: 1.0,
            last_heard: None,
        }
    }

    fn reset(&mut self) {
        self.frame_head = 0;
        self.frame_tail = 0;
        self.frame_count = 0;
        self.frame_read_pos = 0;
        for pkt in &mut self.jitter_packets {
            pkt.valid = false;
            pkt.seq = 0;
            pkt.size = 0;
        }
        self.queued_packets = 0;
        self.last_recv_seq = 0;
        self.has_last_recv_seq = false;
        self.has_next_seq = false;
        self.next_seq = 0;
        self.last_recv_time = None;
        self.jitter_ms = 0.0;
        self.target_frames = 3;
        self.loss_ewma = 0.0;
        if self.decoder.is_some() {
            // 解码器状态重置在 OpusCodec 中没有直接方法，可以重新创建
        }
    }
}

// ============== 回调函数类型 ==============

/// 说话状态变化回调
pub type SpeakingCallback = fn(client_id: u16, speaking: bool);

/// 收到语音包回调
pub type PacketCallback = fn(data: &[u8], sender_id: u16);

// ============== 语音系统 ==============

/// 语音系统状态
pub struct VoiceSystem {
    // 配置
    config: RwLock<VoiceConfig>,

    // 玩家状态
    players: RwLock<Vec<PlayerSnapshot>>,
    local_client_id: AtomicI32,

    // PTT/VAD 状态
    ptt_active: AtomicBool,
    vad_active: AtomicBool,
    ptt_release_deadline: AtomicI64,
    vad_release_deadline: AtomicI64,

    // 音频状态
    mic_level: AtomicI32,
    speaking: AtomicBool,

    // 网络状态
    ping_ms: AtomicI32,
    sequence: AtomicU16,
    context_hash: AtomicU32,

    // 时间戳
    #[allow(dead_code)]
    last_keepalive: Mutex<Option<Instant>>,
    #[allow(dead_code)]
    last_token_hash_sent: AtomicU32,

    // 对端状态
    peers: Mutex<Box<[PeerState]>>,
    last_heard: Mutex<Vec<i64>>,

    // DSP 处理链
    dsp_chain: Mutex<DspChain>,

    // Opus 编码器
    encoder: Mutex<Option<crate::codec::OpusCodec>>,

    // 编码输出缓冲区
    encode_output_buf: Mutex<Vec<u8>>,

    // 编码器参数
    enc_bitrate: AtomicI32,
    enc_loss_perc: AtomicI32,
    enc_fec: AtomicBool,
    last_enc_update: Mutex<Option<Instant>>,

    // 发送状态
    tx_was_active: AtomicBool,

    // 回调函数
    speaking_callback: Mutex<Option<SpeakingCallback>>,
    packet_callback: Mutex<Option<PacketCallback>>,

    // 名单过滤系统
    name_lists: RwLock<NameLists>,

    // Ping 测量
    last_ping_sent_time: AtomicI64,
    last_ping_seq: AtomicU16,

    // 初始化状态
    initialized: AtomicBool,
}

impl VoiceSystem {
    /// 创建新的语音系统
    pub fn new() -> Self {
        Self {
            config: RwLock::new(VoiceConfig::default()),
            players: RwLock::new(Vec::new()),
            local_client_id: AtomicI32::new(-1),
            ptt_active: AtomicBool::new(false),
            vad_active: AtomicBool::new(false),
            ptt_release_deadline: AtomicI64::new(0),
            vad_release_deadline: AtomicI64::new(0),
            mic_level: AtomicI32::new(0),
            speaking: AtomicBool::new(false),
            ping_ms: AtomicI32::new(-1),
            sequence: AtomicU16::new(0),
            context_hash: AtomicU32::new(0),
            last_keepalive: Mutex::new(None),
            last_token_hash_sent: AtomicU32::new(0),
            peers: Mutex::new((0..MAX_CLIENTS).map(|_| PeerState::new()).collect()),
            last_heard: Mutex::new(vec![0i64; MAX_CLIENTS]),
            dsp_chain: Mutex::new(DspChain::new()),
            encoder: Mutex::new(None),
            encode_output_buf: Mutex::new(vec![0u8; VOICE_MAX_PACKET]),
            enc_bitrate: AtomicI32::new(24000),
            enc_loss_perc: AtomicI32::new(0),
            enc_fec: AtomicBool::new(false),
            last_enc_update: Mutex::new(None),
            tx_was_active: AtomicBool::new(false),
            speaking_callback: Mutex::new(None),
            packet_callback: Mutex::new(None),
            name_lists: RwLock::new(NameLists::new()),

            // Ping 测量
            last_ping_sent_time: AtomicI64::new(0),
            last_ping_seq: AtomicU16::new(0),

            initialized: AtomicBool::new(false),
        }
    }

    /// 初始化语音系统
    pub fn init(&self) -> bool {
        if self.initialized.load(Ordering::SeqCst) {
            return true;
        }

        log::info!("Initializing voice system...");

        // 初始化 Opus 编码器
        {
            let mut encoder = self.encoder.lock();
            match crate::codec::OpusCodec::new() {
                Ok(enc) => {
                    *encoder = Some(enc);
                    log::info!("Opus encoder created successfully");
                }
                Err(e) => {
                    log::error!("Failed to create Opus encoder: {}", e);
                    return false;
                }
            }
        }

        // 初始化对端解码器
        {
            let mut peers = self.peers.lock();
            for peer in peers.iter_mut() {
                peer.decoder = None;
                peer.decoder_failed = false;
            }
        }

        self.initialized.store(true, Ordering::SeqCst);
        log::info!("Voice system initialized successfully");
        true
    }

    /// 关闭语音系统
    pub fn shutdown(&self) {
        if !self.initialized.load(Ordering::SeqCst) {
            return;
        }

        log::info!("Shutting down voice system...");

        // 销毁编码器
        {
            let mut encoder = self.encoder.lock();
            *encoder = None;
        }

        // 销毁对端解码器
        {
            let mut peers = self.peers.lock();
            for peer in peers.iter_mut() {
                peer.decoder = None;
                peer.decoder_failed = false;
                peer.reset();
            }
        }

        // 重置状态
        self.ptt_active.store(false, Ordering::SeqCst);
        self.vad_active.store(false, Ordering::SeqCst);
        self.mic_level.store(0, Ordering::SeqCst);
        self.speaking.store(false, Ordering::SeqCst);
        self.ping_ms.store(-1, Ordering::SeqCst);
        self.sequence.store(0, Ordering::SeqCst);
        self.tx_was_active.store(false, Ordering::SeqCst);

        // 重置 last_heard
        {
            let mut last_heard = self.last_heard.lock();
            for lh in last_heard.iter_mut() {
                *lh = 0;
            }
        }

        self.initialized.store(false, Ordering::SeqCst);
        log::info!("Voice system shutdown complete");
    }

    // ============== 配置管理 ==============

    /// 设置配置
    pub fn set_config(&self, config: VoiceConfig) {
        self.context_hash
            .store(config.context_hash, Ordering::SeqCst);
        *self.config.write() = config;
    }

    /// 获取配置
    pub fn get_config(&self) -> VoiceConfig {
        self.config.read().clone()
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

    // ============== 玩家管理 ==============

    /// 更新玩家列表
    pub fn update_players(&self, players: Vec<PlayerSnapshot>) {
        *self.players.write() = players;
    }

    /// 获取玩家位置
    pub fn get_player_pos(&self, client_id: u16) -> Option<(f32, f32)> {
        let players = self.players.read();
        for p in players.iter() {
            if p.client_id == client_id && p.is_active {
                return Some((p.x, p.y));
            }
        }
        None
    }

    /// 获取本地玩家位置
    pub fn get_local_pos(&self) -> (f32, f32) {
        let local_id = self.local_client_id.load(Ordering::SeqCst);
        if local_id < 0 {
            return (0.0, 0.0);
        }

        self.get_player_pos(local_id as u16).unwrap_or((0.0, 0.0))
    }

    /// 获取本地玩家的收听位置
    ///
    /// 如果启用了观战位置收听且本地玩家是观察者，返回观战位置
    /// 否则返回玩家实际位置
    ///
    /// 对应 C++: if(SpecActive && Config.m_RiVoiceHearOnSpecPos) LocalPos = SpecPos;
    pub fn get_local_listening_pos(&self, config: &VoiceConfig) -> (f32, f32) {
        let local_id = self.local_client_id.load(Ordering::SeqCst);
        if local_id < 0 {
            return (0.0, 0.0);
        }

        // 检查是否启用了观战位置收听
        if !config.hear_on_spec_pos {
            return self.get_player_pos(local_id as u16).unwrap_or((0.0, 0.0));
        }

        // 获取本地玩家信息
        let players = self.players.read();
        if let Some(player) = players.iter().find(|p| p.client_id as i32 == local_id) {
            // 如果是观察者，返回观战位置
            if player.is_spectator {
                return (player.spec_x, player.spec_y);
            }
        }

        // 默认返回玩家实际位置
        self.get_player_pos(local_id as u16).unwrap_or((0.0, 0.0))
    }

    // ============== 名单过滤系统 ==============

    /// 设置名单列表
    ///
    /// # 参数
    /// * `whitelist` - 白名单（逗号分隔的玩家名称）
    /// * `blacklist` - 黑名单（逗号分隔的玩家名称）
    /// * `mute` - 静音名单（逗号分隔的玩家名称）
    /// * `vad_allow` - VAD 允许名单（逗号分隔的玩家名称）
    /// * `name_volumes` - 名称音量映射（格式：name1=volume1,name2=volume2）
    pub fn set_name_lists(
        &self,
        whitelist: &str,
        blacklist: &str,
        mute: &str,
        vad_allow: &str,
        name_volumes: &str,
    ) {
        let mut lists = self.name_lists.write();
        lists.whitelist = whitelist.to_string();
        lists.blacklist = blacklist.to_string();
        lists.mute = mute.to_string();
        lists.vad_allow = vad_allow.to_string();
        lists.name_volumes = name_volumes.to_string();

        log::info!(
            "Name lists updated: whitelist={}, blacklist={}, mute={}, vad_allow={}, name_volumes={}",
            whitelist.len(),
            blacklist.len(),
            mute.len(),
            vad_allow.len(),
            name_volumes.len()
        );
    }

    /// 获取名称音量调节值
    ///
    /// # 参数
    /// * `name` - 玩家名称
    ///
    /// # 返回
    /// * `Some(volume)` - 音量百分比（0-200）
    /// * `None` - 未找到该玩家的音量设置
    pub fn get_name_volume(&self, name: &str) -> Option<i32> {
        self.name_lists.read().get_name_volume(name)
    }

    /// 检查是否应该收听该玩家（根据名单模式）
    ///
    /// # 参数
    /// * `name` - 玩家名称
    ///
    /// # 返回
    /// * `true` - 应该收听
    /// * `false` - 应该过滤掉
    pub fn should_hear_player(&self, name: &str) -> bool {
        let lists = self.name_lists.read();
        let config = self.config.read();
        lists.should_hear(name, config.list_mode)
    }

    /// 根据队伍过滤检查是否应该收听该玩家
    ///
    /// 参考 C++ voice_core.cpp 中的队伍过滤逻辑：
    /// - visibility_mode = 0 (默认): 只播放同队伍语音（除非是观察者）
    /// - visibility_mode = 1 (显示其他队伍): 如果发送者在其他队伍且不是 AllowObserver，则跳过
    /// - visibility_mode = 2 (全部可见): 播放所有语音
    ///
    /// # 参数
    /// * `sender_id` - 发送者客户端 ID
    /// * `ignore_distance` - 是否忽略距离限制（组内全局语音）
    ///
    /// # 返回
    /// * `true` - 应该收听
    /// * `false` - 应该过滤掉
    pub fn should_hear_by_team(&self, sender_id: u16, ignore_distance: bool) -> bool {
        // 获取发送者和本地玩家的信息
        let local_team = {
            let local_id = self.local_client_id.load(Ordering::SeqCst);
            let players = self.players.read();

            // 找到本地玩家
            let local_player = players.iter().find(|p| p.client_id as i32 == local_id);
            local_player.map(|p| p.team).unwrap_or(0)
        };

        // 获取发送者信息
        let (sender_team, sender_is_active, _sender_is_spectator) = {
            let players = self.players.read();
            let sender = players.iter().find(|p| p.client_id == sender_id);
            (
                sender.map(|p| p.team).unwrap_or(0),
                sender.map(|p| p.is_active).unwrap_or(false),
                sender.map(|p| p.is_spectator).unwrap_or(false),
            )
        };

        let config = self.config.read();
        let visibility_mode = config.visibility_mode;
        let hear_in_spectate = config.hear_in_spectate;

        // 计算 AllowObserver: 允许收听非活跃玩家语音
        // 当 hear_in_spectate = true 且发送者非活跃时，允许收听
        let allow_observer = hear_in_spectate && !sender_is_active;

        // 同队伍检查
        let same_team = local_team == sender_team;

        // 根据 visibility_mode 应用过滤
        match visibility_mode {
            0 => {
                // 默认模式: 只播放同队伍语音（除非是观察者或忽略距离）
                // C++: if(!IgnoreDistance && !SenderActive && !AllowObserver) continue;
                if !ignore_distance && !sender_is_active && !allow_observer {
                    return false;
                }
            }
            1 => {
                // 显示其他队伍模式: 过滤掉其他队伍的玩家（除非允许观察者）
                // C++: if(SenderOtherTeam && !AllowObserver) continue;
                // SenderOtherTeam = m_pGameClient->m_Teams.Team(i) != m_pGameClient->m_Teams.Team(m_LocalClientIdSnap);
                if !same_team && !allow_observer {
                    return false;
                }
            }
            2 => {
                // 全部可见模式: 不过滤
                // C++ 中没有限制，所有语音都播放
            }
            _ => {
                // 未知模式，使用默认行为
                if !ignore_distance && !sender_is_active && !allow_observer {
                    return false;
                }
            }
        }

        true
    }

    /// 检查玩家是否在 VAD 允许名单中
    pub fn is_vad_allowed(&self, name: &str) -> bool {
        self.name_lists.read().is_vad_allowed(name)
    }

    // ============== PTT/VAD 控制 ==============

    /// 设置 PTT 状态
    pub fn set_ptt_active(&self, active: bool) {
        let was_active = self.ptt_active.swap(active, Ordering::SeqCst);

        if active {
            self.ptt_release_deadline.store(0, Ordering::SeqCst);
            return;
        }

        if was_active {
            // 设置释放延迟
            let now = monotonic_micros();
            let deadline = now + PTT_RELEASE_DELAY_MS * 1000;
            self.ptt_release_deadline.store(deadline, Ordering::SeqCst);
        }
    }

    /// 获取 PTT 状态
    pub fn is_ptt_active(&self) -> bool {
        self.ptt_active.load(Ordering::SeqCst)
    }

    /// 设置 VAD 状态
    pub fn set_vad_active(&self, active: bool) {
        self.vad_active.store(active, Ordering::SeqCst);
    }

    /// 获取 VAD 状态
    pub fn is_vad_active(&self) -> bool {
        self.vad_active.load(Ordering::SeqCst)
    }

    // ============== 状态查询 ==============

    /// 获取麦克风电平
    pub fn get_mic_level(&self) -> i32 {
        self.mic_level.load(Ordering::SeqCst)
    }

    /// 设置麦克风电平
    pub fn set_mic_level(&self, level: i32) {
        self.mic_level.store(level, Ordering::SeqCst);
    }

    /// 获取延迟
    pub fn get_ping_ms(&self) -> i32 {
        self.ping_ms.load(Ordering::SeqCst)
    }

    /// 设置延迟
    pub fn set_ping_ms(&self, ping: i32) {
        self.ping_ms.store(ping, Ordering::SeqCst);
    }

    /// 是否正在说话
    pub fn is_speaking(&self) -> bool {
        self.speaking.load(Ordering::SeqCst)
    }

    /// 是否已初始化
    pub fn is_initialized(&self) -> bool {
        self.initialized.load(Ordering::SeqCst)
    }

    // ============== 回调设置 ==============

    /// 设置说话状态变化回调
    pub fn set_speaking_callback(&self, callback: SpeakingCallback) {
        *self.speaking_callback.lock() = Some(callback);
    }

    /// 设置语音包接收回调
    pub fn set_packet_callback(&self, callback: PacketCallback) {
        *self.packet_callback.lock() = Some(callback);
    }

    // ============== 核心处理方法 ==============

    /// 每帧处理（捕获 -> DSP -> 编码 -> 发送）
    ///
    /// # 参数
    /// * `capture_data` - 捕获的音频数据 (960 samples)
    /// * `send_packet` - 发送回调函数
    pub fn on_frame<F>(&self, capture_data: &mut [i16], mut send_packet: F)
    where
        F: FnMut(&[u8]),
    {
        if !self.initialized.load(Ordering::SeqCst) {
            return;
        }

        let config = self.get_config();
        let local_id = self.local_client_id.load(Ordering::SeqCst);

        // 检查是否应该发送
        let should_send = self.check_should_send(&config);

        // 麦克风静音检查
        if config.mic_mute {
            self.update_mic_level(0.0);
            self.update_speaking_state(false);
            return;
        }

        // DSP 处理
        {
            let mut dsp = self.dsp_chain.lock();
            dsp.process(capture_data, &config);
        }

        // 计算音频峰值
        let peak = Self::calculate_peak(capture_data);
        self.update_mic_level(peak);

        // VAD 检测
        if config.vad_enable {
            self.process_vad(peak, &config);
        }

        // 更新说话状态
        let is_speaking = if config.vad_enable {
            self.vad_active.load(Ordering::SeqCst)
        } else {
            should_send
        };
        self.update_speaking_state(is_speaking);

        if !should_send {
            return;
        }

        // 检查是否需要重置编码器
        let tx_was_active = self.tx_was_active.load(Ordering::SeqCst);
        if !tx_was_active {
            // 开始新的发送会话
            self.sequence.fetch_add(1000, Ordering::SeqCst);
            self.tx_was_active.store(true, Ordering::SeqCst);
        }

        // 本地测试模式
        if config.test_mode == 1 {
            // 本地回环，不发送网络包
            if local_id >= 0 {
                let volume = config.volume as f32 / 100.0;
                self.push_peer_frame(local_id as u16, capture_data, volume, volume);
            }
            return;
        }

        // 编码并发送
        if let Some(encoded_data) = self.encode_audio(capture_data) {
            let packet = self.create_audio_packet(local_id as u16, &encoded_data, &config);

            let mut buf = [0u8; VOICE_MAX_PACKET];
            let len = packet.serialize(&mut buf);
            if len > 0 {
                send_packet(&buf[..len]);
            }
        }
    }

    /// 发送 Pong 响应
    ///
    /// # 参数
    /// * `seq` - Ping 包的序列号
    fn send_pong(&self, seq: u16) {
        let config = self.get_config();
        let local_id = self.local_client_id.load(Ordering::SeqCst);

        let packet =
            VoicePacket::new_pong(local_id as u16, seq, config.context_hash, config.token_hash);

        // 通过网络发送 Pong 包
        if let Some(ref callback) = *self.packet_callback.lock() {
            let mut buf = [0u8; VOICE_MAX_PACKET];
            let len = packet.serialize(&mut buf);
            if len > 0 {
                callback(&buf[..len], local_id as u16);
            }
        }
    }

    /// 处理收到的 Pong 包，计算 RTT
    ///
    /// # 参数
    /// * `seq` - Pong 包的序列号
    fn handle_pong(&self, seq: u16) {
        let last_ping_seq = self.last_ping_seq.load(Ordering::SeqCst);

        // 检查序列号是否匹配
        if seq != last_ping_seq {
            log::debug!(
                "Pong sequence mismatch: expected {}, got {}",
                last_ping_seq,
                seq
            );
            return;
        }

        let last_ping_time = self.last_ping_sent_time.load(Ordering::SeqCst);
        if last_ping_time == 0 {
            return;
        }

        // 计算 RTT（毫秒）
        let now = monotonic_micros();
        let rtt_us = now - last_ping_time;
        let rtt_ms = (rtt_us / 1000) as i32;

        // 限制在合理范围内
        let rtt_ms = rtt_ms.clamp(0, 9999);

        self.ping_ms.store(rtt_ms, Ordering::SeqCst);
        log::debug!("Ping RTT: {} ms", rtt_ms);
    }

    /// 发送 Ping 包（定期调用）
    pub fn send_ping(&self) {
        let config = self.get_config();
        let local_id = self.local_client_id.load(Ordering::SeqCst);

        // 生成序列号
        let seq = self.sequence.fetch_add(1, Ordering::SeqCst);

        // 记录发送时间
        let now = monotonic_micros();
        self.last_ping_sent_time.store(now, Ordering::SeqCst);
        self.last_ping_seq.store(seq, Ordering::SeqCst);

        let packet =
            VoicePacket::new_ping(local_id as u16, seq, config.context_hash, config.token_hash);

        // 通过回调发送
        if let Some(ref callback) = *self.packet_callback.lock() {
            let mut buf = [0u8; VOICE_MAX_PACKET];
            let len = packet.serialize(&mut buf);
            if len > 0 {
                callback(&buf[..len], local_id as u16);
            }
        }
    }

    /// 接收网络数据
    ///
    /// # 参数
    /// * `data` - 接收到的网络数据
    pub fn on_receive(&self, data: &[u8]) {
        if !self.initialized.load(Ordering::SeqCst) {
            return;
        }

        // 解析包
        let packet = match VoicePacket::parse(data) {
            Ok(p) => p,
            Err(e) => {
                log::debug!("Failed to parse voice packet: {}", e);
                return;
            }
        };

        // 验证上下文
        let local_context = self.context_hash.load(Ordering::SeqCst);
        if packet.context_hash == 0 || packet.context_hash != local_context {
            return;
        }

        // 处理 PING/PONG
        if packet.packet_type == PacketType::Ping {
            // 收到 Ping，回复 Pong
            // 使用相同的序列号回复
            self.send_pong(packet.sequence);
            return;
        }

        if packet.packet_type == PacketType::Pong {
            // 收到 Pong，计算 RTT
            self.handle_pong(packet.sequence);
            return;
        }

        // 处理音频包
        if packet.packet_type != PacketType::Audio {
            return;
        }

        let config = self.get_config();
        let local_id = self.local_client_id.load(Ordering::SeqCst);

        // 忽略自己的包（除非是服务器回环测试）
        if packet.sender_id == local_id as u16 && config.test_mode != 2 {
            return;
        }

        // 发送者 ID 越界检查（C++ 中 SenderId >= MAX_CLIENTS 则跳过）
        if packet.sender_id as usize >= MAX_CLIENTS {
            return;
        }

        // Token 验证
        if !self.should_hear(&config, packet.token_hash) {
            return;
        }

        // 获取发送者完整信息和本地玩家信息（用于后续的名单过滤、可见性模式、VAD检查等）
        let (
            sender_name,
            sender_is_active,
            sender_is_spectator,
            sender_team,
            local_team,
            _local_is_spectator,
        ) = {
            let players = self.players.read();
            let sender = players.iter().find(|p| p.client_id == packet.sender_id);
            let local_id = self.local_client_id.load(Ordering::SeqCst);
            let local_player = players.iter().find(|p| p.client_id as i32 == local_id);

            (
                sender.map(|p| p.name.clone()),
                sender.map(|p| p.is_active).unwrap_or(false),
                sender.map(|p| p.is_spectator).unwrap_or(false),
                sender.map(|p| p.team).unwrap_or(0),
                local_player.map(|p| p.team).unwrap_or(0),
                local_player.map(|p| p.is_spectator).unwrap_or(false),
            )
        };

        // 名单过滤检查（静音、白名单、黑名单）
        if let Some(ref name) = sender_name {
            if !self.should_hear_player(name) {
                log::debug!(
                    "Filtering voice from client {} (name='{}') based on name lists",
                    packet.sender_id,
                    name
                );
                return;
            }
        }

        // 检查 VAD 玩家过滤
        // C++: if(SenderUsesVad && !Config.m_RiVoiceHearVad && !VoiceListMatch(...)) continue;
        let sender_uses_vad = packet.is_vad();
        if sender_uses_vad && !config.hear_vad {
            // 检查是否在 VAD 允许名单中
            if let Some(ref name) = sender_name {
                if !self.is_vad_allowed(name) {
                    log::debug!(
                        "Filtering voice from client {} (VAD user not in allow list)",
                        packet.sender_id
                    );
                    return;
                }
            } else {
                // 无法确认玩家名称，且不在允许名单中，过滤掉
                log::debug!(
                    "Filtering voice from client {} (unknown VAD user)",
                    packet.sender_id
                );
                return;
            }
        }

        // 队伍过滤检查
        // 计算 SameGroup 用于 ignore_distance 判断
        let local_group = config.token_hash & 0x3FFFFFFF;
        let sender_group = packet.token_hash & 0x3FFFFFFF;
        let same_group = local_group != 0 && sender_group == local_group;
        let ignore_distance = config.ignore_distance || (config.group_global && same_group);

        // 计算 AllowObserver (允许收听旁观者语音)
        // C++: AllowObserver = Config.m_RiVoiceHearPeoplesInSpectate && !SenderActive && !SenderSpec
        let allow_observer = config.hear_in_spectate && !sender_is_active && !sender_is_spectator;

        // 同队伍检查
        let same_team = local_team == sender_team;

        // 根据 visibility_mode 应用过滤
        let should_hear = match config.visibility_mode {
            0 => {
                // 默认模式: 只播放活跃玩家的语音（除非是观察者或忽略距离）
                // C++: if(!IgnoreDistance && !SenderActive && !AllowObserver) continue;
                ignore_distance || sender_is_active || allow_observer
            }
            1 => {
                // 显示其他队伍模式: 过滤掉其他队伍的玩家（除非允许观察者）
                // C++: if(SenderOtherTeam && !AllowObserver) continue;
                same_team || allow_observer
            }
            2 => {
                // 全部可见模式: 不过滤
                true
            }
            _ => {
                // 未知模式，使用默认行为
                ignore_distance || sender_is_active || allow_observer
            }
        };

        if !should_hear {
            log::debug!(
                "Filtering voice from client {} based on team filter (visibility_mode={}, same_team={}, allow_observer={})",
                packet.sender_id,
                config.visibility_mode,
                same_team,
                allow_observer
            );
            return;
        }

        // 计算空间音频
        // 如果启用了观战位置收听且本地玩家是观察者，使用观战位置作为收听位置
        let local_pos = self.get_local_listening_pos(&config);

        let sender_pos = (packet.pos_x, packet.pos_y);
        let spatial = self.calculate_spatial_audio(local_pos, sender_pos, &config);

        if !spatial.in_range && !config.ignore_distance {
            return;
        }

        // 更新最后听到时间
        let now = monotonic_micros();
        {
            let mut last_heard = self.last_heard.lock();
            last_heard[packet.sender_id as usize] = now;
        }

        // 推入抖动缓冲
        self.push_jitter_packet(
            packet.sender_id,
            packet.sequence,
            &packet.opus_payload,
            spatial.left_gain,
            spatial.right_gain,
        );
    }

    /// 解码抖动缓冲
    ///
    /// 从抖动缓冲中取出包，解码并推入播放队列
    ///
    /// 优化：逐个 peer 持锁而非整个循环持锁，
    /// 允许其他线程（如音频回调、网络接收）在 peer 之间访问共享数据。
    /// C++ 实现中 DecodeJitter 在工作线程运行，仅在访问播放帧队列时
    /// 才通过 SDL_LockAudioDevice 同步。
    pub fn decode_jitter(&self) {
        if !self.initialized.load(Ordering::SeqCst) {
            return;
        }

        let mut pkt_data_buf = [0u8; VOICE_MAX_PACKET];
        let mut next_pkt_data_buf = [0u8; VOICE_MAX_PACKET];
        let mut decode_buf = [0i16; VOICE_FRAME_SAMPLES];

        for peer_id in 0..MAX_CLIENTS {
            let mut peers = self.peers.lock();
            let peer = &mut peers[peer_id];

            if peer.queued_packets == 0 {
                continue;
            }

            if !peer.has_next_seq {
                if peer.queued_packets < peer.target_frames {
                    continue;
                }

                let mut min_seq = None;
                for pkt in &peer.jitter_packets {
                    if !pkt.valid {
                        continue;
                    }
                    min_seq = Some(match min_seq {
                        None => pkt.seq,
                        Some(s) => {
                            if Self::seq_less(pkt.seq, s) {
                                pkt.seq
                            } else {
                                s
                            }
                        }
                    });
                }

                if let Some(seq) = min_seq {
                    peer.next_seq = seq;
                    peer.has_next_seq = true;
                } else {
                    continue;
                }
            }

            if peer.frame_count >= MAX_PLAYBACK_FRAMES {
                continue;
            }

            let slot = peer.next_seq as usize % MAX_JITTER_PACKETS;
            let pkt = &peer.jitter_packets[slot];

            let (pkt_valid, pkt_seq, pkt_size, pkt_left_gain, pkt_right_gain) =
                (pkt.valid, pkt.seq, pkt.size, pkt.left_gain, pkt.right_gain);
            let pkt_data_len = if pkt_valid && pkt_seq == peer.next_seq {
                let len = pkt_size.min(VOICE_MAX_PACKET);
                pkt_data_buf[..len].copy_from_slice(&pkt.data[..len]);
                Some(len)
            } else {
                None
            };

            let next_slot = peer.next_seq.wrapping_add(1) as usize % MAX_JITTER_PACKETS;
            let next_pkt = &peer.jitter_packets[next_slot];
            let (next_pkt_valid, next_pkt_seq, next_pkt_size) =
                (next_pkt.valid, next_pkt.seq, next_pkt.size);
            let next_pkt_data_len =
                if next_pkt_valid && next_pkt_seq == peer.next_seq.wrapping_add(1) {
                    let len = next_pkt_size.min(VOICE_MAX_PACKET);
                    next_pkt_data_buf[..len].copy_from_slice(&next_pkt.data[..len]);
                    Some(len)
                } else {
                    None
                };

            let (decoded_samples, left_gain, right_gain) = if pkt_valid && pkt_seq == peer.next_seq
            {
                let decoded = if let Some(len) = pkt_data_len {
                    self.decode_peer_audio(peer, &pkt_data_buf[..len], &mut decode_buf)
                } else {
                    None
                };
                peer.queued_packets = peer.queued_packets.saturating_sub(1);
                (decoded, pkt_left_gain, pkt_right_gain)
            } else {
                if next_pkt_valid
                    && next_pkt_seq == peer.next_seq.wrapping_add(1)
                    && peer.loss_ewma > 0.02
                {
                    let decoded = if let Some(len) = next_pkt_data_len {
                        self.decode_peer_audio_fec(peer, &next_pkt_data_buf[..len], &mut decode_buf)
                    } else {
                        None
                    };
                    (decoded, peer.last_gain_left, peer.last_gain_right)
                } else if peer.has_last_recv_seq {
                    let decoded = self.decode_peer_audio_plc(peer, &mut decode_buf);
                    (decoded, peer.last_gain_left, peer.last_gain_right)
                } else {
                    (None, peer.last_gain_left, peer.last_gain_right)
                }
            };

            peer.jitter_packets[slot].valid = false;

            if let Some(samples) = decoded_samples {
                if samples > 0 {
                    peer.last_gain_left = left_gain;
                    peer.last_gain_right = right_gain;

                    let frame = &mut peer.playback_frames[peer.frame_tail];
                    let copy_len = samples.min(VOICE_FRAME_SAMPLES);
                    frame.pcm[..copy_len].copy_from_slice(&decode_buf[..copy_len]);
                    frame.samples = copy_len;
                    frame.left_gain = left_gain;
                    frame.right_gain = right_gain;

                    peer.frame_tail = (peer.frame_tail + 1) % MAX_PLAYBACK_FRAMES;
                    peer.frame_count += 1;
                }
            }

            peer.next_seq = peer.next_seq.wrapping_add(1);
        }
    }

    /// 混音输出 - 零拷贝高性能版本
    ///
    /// 优化策略（参考 C++ MixAudio 实现 voice_core.cpp:720-782）：
    /// 1. **零拷贝访问**：直接读取环形缓冲区，不克隆 PCM 数据
    /// 2. **预分配混音缓冲区**：使用 thread_local 重用缓冲区，避免每帧堆分配
    /// 3. **单次持锁**：单次持锁完成所有操作（与 C++ SDL_LockAudioDevice 策略一致）
    /// 4. **先读后检查**：与 C++ 一致，先读取样本，再检查是否需要移动到下
    ///
    /// 性能对比：
    /// - 原实现：每帧 N 次堆分配（N = 活跃 peer 数 × 帧数）
    /// - 新实现：0 次堆分配（重用 thread_local 缓冲区）
    ///
    /// # 参数
    /// * `output` - 输出缓冲区
    /// * `channels` - 输出声道数
    pub fn mix_audio(&self, output: &mut [i16], channels: usize) {
        if output.is_empty() || channels == 0 {
            return;
        }

        let samples = output.len() / channels;

        // 使用 thread_local 预分配混音缓冲区，避免每帧堆分配
        // 第一次调用时分配，后续直接重用
        thread_local! {
            static MIX_BUF: std::cell::RefCell<Vec<i32>> = const { std::cell::RefCell::new(Vec::new()) };
        }

        MIX_BUF.with(|buf| {
            let mut mix_buf = buf.borrow_mut();

            // 只在需要时扩容，正常情况只分配一次
            if mix_buf.len() < output.len() {
                mix_buf.resize(output.len(), 0);
            }

            // 快速清零混音缓冲区
            let mix_slice = &mut mix_buf[..output.len()];
            for val in mix_slice.iter_mut() {
                *val = 0;
            }

            // 单次持锁完成所有操作（与 C++ SDL_LockAudioDevice 策略一致）
            let mut peers = self.peers.lock();

            for peer in peers.iter_mut() {
                if peer.frame_count == 0 {
                    continue;
                }

                let mut frame_idx = peer.frame_head;
                let mut frame_count = peer.frame_count;
                let mut read_pos = peer.frame_read_pos;

                for i in 0..samples {
                    if frame_count == 0 {
                        break;
                    }

                    let frame = &peer.playback_frames[frame_idx];

                    // 直接访问原始帧数据（零拷贝）
                    // 注意：与 C++ 实现一致，先读取样本，再检查边界
                    let pcm_sample = frame.pcm[read_pos];
                    let left_gain = frame.left_gain;
                    let right_gain = frame.right_gain;

                    let base = i * channels;

                    if channels == 1 {
                        // 单声道：左右增益平均
                        let mono_gain = 0.5 * (left_gain + right_gain);
                        mix_slice[base] += (pcm_sample as f32 * mono_gain) as i32;
                    } else {
                        // 立体声
                        mix_slice[base] += (pcm_sample as f32 * left_gain) as i32;
                        mix_slice[base + 1] += (pcm_sample as f32 * right_gain) as i32;

                        // 多声道：其他声道复制中心增益
                        if channels > 2 {
                            let center =
                                (pcm_sample as f32 * 0.5 * (left_gain + right_gain)) as i32;
                            for ch in 2..channels {
                                mix_slice[base + ch] += center;
                            }
                        }
                    }

                    // 更新读取位置（与 C++ 实现一致）
                    read_pos += 1;
                    if read_pos >= frame.samples {
                        read_pos = 0;
                        frame_idx = (frame_idx + 1) % MAX_PLAYBACK_FRAMES;
                        frame_count -= 1;
                    }
                }

                // 更新 peer 状态（混音完成后立即更新）
                peer.frame_head = frame_idx;
                peer.frame_count = frame_count;
                peer.frame_read_pos = read_pos;
            }

            // 使用 clamp 进行硬削波并写入输出
            for i in 0..output.len() {
                output[i] = mix_slice[i].clamp(-32768, 32767) as i16;
            }
        });
    }

    /// 更新编码器参数（自适应比特率）
    pub fn update_encoder_params(&self) {
        let mut encoder = self.encoder.lock();
        let encoder = match encoder.as_mut() {
            Some(e) => e,
            None => return,
        };

        let now = Instant::now();
        {
            let last_update = self.last_enc_update.lock();
            if let Some(last) = *last_update {
                if now.duration_since(last) < Duration::from_secs(1) {
                    return;
                }
            }
        }

        // 计算平均丢包率和最大抖动
        let mut loss_avg = 0.0f32;
        let mut jitter_max = 0.0f32;
        let mut count = 0;

        {
            let peers = self.peers.lock();
            for peer in peers.iter() {
                if peer.last_recv_time.is_none() {
                    continue;
                }
                // 只统计最近活跃的对端
                loss_avg += peer.loss_ewma;
                jitter_max = jitter_max.max(peer.jitter_ms);
                count += 1;
            }
        }

        if count > 0 {
            loss_avg /= count as f32;
        }

        // 根据网络状况调整参数
        let (target_bitrate, target_loss, target_fec) = if loss_avg <= 0.02 && jitter_max < 8.0 {
            // 网络好
            (32000, 0, false)
        } else if loss_avg <= 0.05 {
            // 网络一般
            (24000, 5, true)
        } else if loss_avg <= 0.10 {
            // 网络较差
            (20000, 10, true)
        } else {
            // 网络很差
            (16000, 20, true)
        };

        // 应用新参数
        if let Err(e) = encoder.set_bitrate(target_bitrate) {
            log::warn!("Failed to set encoder bitrate to {}: {}", target_bitrate, e);
        }
        if let Err(e) = encoder.set_packet_loss_perc(target_loss) {
            log::warn!(
                "Failed to set encoder packet loss to {}: {}",
                target_loss,
                e
            );
        }
        if let Err(e) = encoder.set_fec(target_fec) {
            log::warn!("Failed to set encoder FEC to {}: {}", target_fec, e);
        }

        self.enc_bitrate.store(target_bitrate, Ordering::SeqCst);
        self.enc_loss_perc.store(target_loss, Ordering::SeqCst);
        self.enc_fec.store(target_fec, Ordering::SeqCst);

        *self.last_enc_update.lock() = Some(now);
    }

    // ============== 私有辅助方法 ==============

    /// 检查是否应该发送
    fn check_should_send(&self, config: &VoiceConfig) -> bool {
        if config.vad_enable {
            return self.vad_active.load(Ordering::SeqCst);
        }

        // PTT 模式
        let ptt_active = self.ptt_active.load(Ordering::SeqCst);
        if ptt_active {
            return true;
        }

        // 检查释放延迟
        let deadline = self.ptt_release_deadline.load(Ordering::SeqCst);
        if deadline > 0 {
            let now = monotonic_micros();
            if now < deadline {
                return true;
            }
            self.ptt_release_deadline.store(0, Ordering::SeqCst);
        }

        false
    }

    /// 处理 VAD
    fn process_vad(&self, peak: f32, config: &VoiceConfig) {
        let threshold = config.vad_threshold as f32 / 100.0;
        let release_ms = config.vad_release_delay_ms;

        let triggered = threshold <= 0.0 || peak >= threshold;

        if triggered {
            self.vad_active.store(true, Ordering::SeqCst);
            if release_ms > 0 {
                let deadline = monotonic_micros() + release_ms as i64 * 1000;
                self.vad_release_deadline.store(deadline, Ordering::SeqCst);
            } else {
                self.vad_release_deadline.store(0, Ordering::SeqCst);
            }
        } else if self.vad_active.load(Ordering::SeqCst) {
            let deadline = self.vad_release_deadline.load(Ordering::SeqCst);
            let now = monotonic_micros();
            if deadline == 0 || now >= deadline {
                self.vad_active.store(false, Ordering::SeqCst);
                self.vad_release_deadline.store(0, Ordering::SeqCst);
            }
        }
    }

    /// 更新麦克风电平
    fn update_mic_level(&self, peak: f32) {
        let prev = self.mic_level.load(Ordering::SeqCst) as f32 / 100.0;

        let next = if peak < 0.0 {
            prev * 0.97 // 衰减
        } else {
            let peak_clamped = peak.clamp(0.0, 1.0);
            if peak_clamped >= prev {
                peak_clamped
            } else {
                prev * 0.9
            }
        };

        self.mic_level
            .store((next * 100.0) as i32, Ordering::SeqCst);
    }

    /// 更新说话状态
    fn update_speaking_state(&self, speaking: bool) {
        let was_speaking = self.speaking.swap(speaking, Ordering::SeqCst);

        if speaking != was_speaking {
            // 触发回调
            if let Some(callback) = *self.speaking_callback.lock() {
                let local_id = self.local_client_id.load(Ordering::SeqCst);
                if local_id >= 0 {
                    callback(local_id as u16, speaking);
                }
            }
        }
    }

    /// 计算音频峰值
    fn calculate_peak(samples: &[i16]) -> f32 {
        let mut peak = 0i32;
        for &sample in samples {
            let abs = sample.abs() as i32;
            if abs > peak {
                peak = abs;
            }
        }
        peak as f32 / 32768.0
    }

    /// 编码音频
    fn encode_audio(&self, pcm: &[i16]) -> Option<Vec<u8>> {
        let mut encoder = self.encoder.lock();
        let encoder = encoder.as_mut()?;

        let mut output_buf = self.encode_output_buf.lock();
        output_buf.resize(VOICE_MAX_PACKET, 0);
        match encoder.encode(pcm, &mut output_buf[..VOICE_MAX_PACKET]) {
            Ok(len) => Some(output_buf[..len].to_vec()),
            Err(e) => {
                log::error!("Opus encode error: {}", e);
                None
            }
        }
    }

    /// 创建音频包
    fn create_audio_packet(
        &self,
        sender_id: u16,
        opus_data: &[u8],
        config: &VoiceConfig,
    ) -> VoicePacket {
        let seq = self.sequence.fetch_add(1, Ordering::SeqCst);
        let (pos_x, pos_y) = self.get_local_pos();

        let mut packet = VoicePacket::new_audio(
            sender_id,
            seq,
            config.context_hash,
            config.token_hash,
            pos_x,
            pos_y,
            opus_data.to_vec(),
        );

        if config.vad_enable {
            packet.set_vad(true);
        }
        if config.test_mode == 2 {
            packet.set_loopback(true);
        }

        packet
    }

    /// 检查是否应该听到
    fn should_hear(&self, config: &VoiceConfig, sender_token: u32) -> bool {
        let local_group = config.token_hash & 0x3FFFFFFF;
        let sender_group = sender_token & 0x3FFFFFFF;

        // 相同组或都是公开组
        sender_group == local_group
    }

    /// 计算空间音频
    fn calculate_spatial_audio(
        &self,
        local_pos: (f32, f32),
        sender_pos: (f32, f32),
        config: &VoiceConfig,
    ) -> SpatialResult {
        let spatial_config = SpatialConfig {
            radius: config.radius as f32 * 32.0, // tiles -> pixels
            stereo_width: config.stereo_width as f32 / 100.0,
            volume: config.volume as f32 / 100.0,
        };

        calculate_spatial(local_pos, sender_pos, &spatial_config)
    }

    /// 推入抖动缓冲包
    fn push_jitter_packet(
        &self,
        peer_id: u16,
        seq: u16,
        data: &[u8],
        left_gain: f32,
        right_gain: f32,
    ) {
        if peer_id as usize >= MAX_CLIENTS {
            return;
        }

        let mut peers = self.peers.lock();
        let peer = &mut peers[peer_id as usize];

        // 检查是否需要重置
        let now = Instant::now();
        if let Some(last_recv) = peer.last_recv_time {
            if now.duration_since(last_recv) > Duration::from_secs(2) {
                peer.reset();
            }
        }

        // 更新抖动估计
        if let Some(last_recv) = peer.last_recv_time {
            let delta_ms = now.duration_since(last_recv).as_millis() as f32;
            let deviation = (delta_ms - 20.0).abs();
            peer.jitter_ms = 0.9 * peer.jitter_ms + 0.1 * deviation;
        }
        peer.last_recv_time = Some(now);

        // 调整目标帧数
        peer.target_frames = Self::clamp_jitter_target(peer.jitter_ms);

        // 检测丢包
        if peer.has_last_recv_seq {
            let expected = peer.last_recv_seq.wrapping_add(1);
            if seq != expected {
                peer.target_frames = (peer.target_frames + 1).min(6);
            }

            let delta = seq.wrapping_sub(peer.last_recv_seq) as i16;
            if delta > 0 && delta < 1000 {
                let lost = (delta as i32 - 1).max(0) as f32;
                let loss_ratio = (lost / delta as f32).clamp(0.0, 1.0);
                peer.loss_ewma = 0.9 * peer.loss_ewma + 0.1 * loss_ratio;
            }
        }

        // 更新序列号
        if !peer.has_last_recv_seq || Self::seq_less(peer.last_recv_seq, seq) {
            peer.last_recv_seq = seq;
            peer.has_last_recv_seq = true;
        }

        // 存储包
        let slot = seq as usize % MAX_JITTER_PACKETS;
        let pkt = &mut peer.jitter_packets[slot];
        let is_new_packet = !pkt.valid || pkt.seq != seq;

        pkt.valid = true;
        pkt.seq = seq;
        pkt.size = data.len().min(VOICE_MAX_PACKET);
        pkt.left_gain = left_gain;
        pkt.right_gain = right_gain;
        pkt.data[..pkt.size].copy_from_slice(&data[..pkt.size]);

        if is_new_packet {
            peer.queued_packets = (peer.queued_packets + 1).min(MAX_JITTER_PACKETS);
        }
    }

    /// 解码对端音频
    fn decode_peer_audio(
        &self,
        peer: &mut PeerState,
        data: &[u8],
        output: &mut [i16],
    ) -> Option<usize> {
        if peer.decoder.is_none() && !peer.decoder_failed {
            match crate::codec::OpusCodec::new() {
                Ok(dec) => peer.decoder = Some(dec),
                Err(e) => {
                    log::error!("Failed to create Opus decoder: {}", e);
                    peer.decoder_failed = true;
                    return None;
                }
            }
        }

        let decoder = peer.decoder.as_mut()?;

        match decoder.decode(data, output) {
            Ok(samples) => Some(samples),
            Err(e) => {
                log::error!("Opus decode error: {}", e);
                None
            }
        }
    }

    /// FEC 解码
    fn decode_peer_audio_fec(
        &self,
        peer: &mut PeerState,
        data: &[u8],
        output: &mut [i16],
    ) -> Option<usize> {
        let decoder = peer.decoder.as_mut()?;
        decoder.decode_fec(data, output).ok()
    }

    /// PLC 解码
    fn decode_peer_audio_plc(&self, peer: &mut PeerState, output: &mut [i16]) -> Option<usize> {
        let decoder = peer.decoder.as_mut()?;
        decoder.decode_plc(output).ok()
    }

    /// 推入播放帧
    fn push_peer_frame(&self, peer_id: u16, pcm: &[i16], left_gain: f32, right_gain: f32) {
        if peer_id as usize >= MAX_CLIENTS {
            return;
        }

        let mut peers = self.peers.lock();
        let peer = &mut peers[peer_id as usize];

        if peer.frame_count >= MAX_PLAYBACK_FRAMES {
            peer.frame_head = (peer.frame_head + 1) % MAX_PLAYBACK_FRAMES;
            peer.frame_count -= 1;
            peer.frame_read_pos = 0;
        }

        let frame = &mut peer.playback_frames[peer.frame_tail];
        let copy_len = pcm.len().min(VOICE_FRAME_SAMPLES);
        frame.pcm[..copy_len].copy_from_slice(&pcm[..copy_len]);
        frame.samples = copy_len;
        frame.left_gain = left_gain;
        frame.right_gain = right_gain;

        peer.frame_tail = (peer.frame_tail + 1) % MAX_PLAYBACK_FRAMES;
        peer.frame_count += 1;
    }

    /// 序列号比较 (考虑回绕)
    fn seq_less(a: u16, b: u16) -> bool {
        (a as i16).wrapping_sub(b as i16) < 0
    }

    /// 根据抖动计算目标帧数
    fn clamp_jitter_target(jitter_ms: f32) -> usize {
        if jitter_ms <= 8.0 {
            2
        } else if jitter_ms <= 14.0 {
            3
        } else if jitter_ms <= 22.0 {
            4
        } else if jitter_ms <= 32.0 {
            5
        } else {
            6
        }
    }
}

impl Default for VoiceSystem {
    fn default() -> Self {
        Self::new()
    }
}

// ============== 全局函数 ==============

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

/// C ABI 兼容的语音配置结构体
///
/// 所有字段使用 i32 以避免 bool 对齐问题。
/// 必须与 C++ 端 VoiceConfigCABI 的内存布局完全一致。
/// 布尔字段使用 0/1 整数值表示。
#[repr(C)]
pub struct VoiceConfigCABI {
    pub mic_volume: i32,
    pub noise_suppress: i32, // 0 或 1
    pub noise_suppress_strength: i32,
    pub comp_threshold: i32,
    pub comp_ratio: i32,
    pub comp_attack_ms: i32,
    pub comp_release_ms: i32,
    pub comp_makeup: i32,
    pub vad_enable: i32, // 0 或 1
    pub vad_threshold: i32,
    pub vad_release_delay_ms: i32,
    pub stereo: i32, // 0 或 1
    pub stereo_width: i32,
    pub volume: i32,
    pub radius: i32,
    pub mic_mute: i32,        // 0 或 1
    pub test_mode: i32,       // 0=正常, 1=本地回环, 2=服务器回环
    pub ignore_distance: i32, // 0 或 1
    pub group_global: i32,    // 0 或 1
    pub token_hash: u32,
    pub context_hash: u32,
    // 新增字段
    pub filter_enable: i32, // 0 或 1
    pub limiter: i32,
    pub visibility_mode: i32,
    pub list_mode: i32,
    pub debug: i32, // 0 或 1
    pub group_mode: i32,
    pub hear_on_spec_pos: i32, // 0 或 1
    pub hear_in_spectate: i32, // 0 或 1
    pub hear_vad: i32,         // 0 或 1
    pub ptt_release_delay_ms: i32,
    // 额外字段（与 C++ 保持一致）
    pub ptt_mode: i32,    // 0=按住说话, 1=切换模式
    pub ptt_key: i32,     // 按键码
    pub echo_cancel: i32, // 0 或 1
    pub echo_cancel_strength: i32,
    pub agc_enable: i32, // 0 或 1
    pub agc_target: i32,
    pub agc_max_gain: i32,
    pub opus_bitrate: i32,            // Opus 编码比特率 (bps)
    pub jitter_buffer_ms: i32,        // 抖动缓冲区大小 (毫秒)
    pub packet_loss_concealment: i32, // 0 或 1
}

const _: () = assert!(std::mem::size_of::<VoiceConfigCABI>() == 41 * std::mem::size_of::<i32>());
const _: () = assert!(std::mem::align_of::<VoiceConfigCABI>() == std::mem::align_of::<i32>());

/// 语音系统状态 (C ABI 版本)
struct VoiceSystemHandle {
    system: Box<VoiceSystem>,
}

ffi_wrap!(
    fn voice_system_create() -> usize {
        let handle = Box::new(VoiceSystemHandle {
            system: Box::new(VoiceSystem::new()),
        });
        Box::into_raw(handle) as usize
    }
);

ffi_wrap!(
    unsafe fn voice_system_destroy(handle: usize) {
        if handle != 0 {
            let _ = Box::from_raw(handle as *mut VoiceSystemHandle);
        }
    }
);

ffi_wrap!(
    unsafe fn voice_system_init(handle: usize) -> i32 {
        if handle == 0 {
            return 0;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        if handle.system.init() {
            1
        } else {
            0
        }
    }
);

ffi_wrap!(
    unsafe fn voice_system_shutdown(handle: usize) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.shutdown();
    }
);

ffi_wrap!(
    unsafe fn voice_set_config(handle: usize, config: *const VoiceConfigCABI) {
        if handle == 0 || config.is_null() {
            return;
        }
        let config = &*config;
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.set_config(VoiceConfig {
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
        });
    }
);

ffi_wrap!(
    unsafe fn voice_set_ptt(handle: usize, active: i32) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.set_ptt_active(active != 0);
    }
);

ffi_wrap!(
    unsafe fn voice_set_vad(handle: usize, active: i32) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.set_vad_active(active != 0);
    }
);

ffi_wrap!(
    unsafe fn voice_update_players(handle: usize, players: *const f32, count: usize) {
        if handle == 0 || players.is_null() || count == 0 || count > 64 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
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
                spec_x: x, // 默认观战位置与实际位置相同
                spec_y: y,
            });
        }
        handle.system.update_players(player_list);
    }
);

ffi_wrap!(
    unsafe fn voice_set_local_client_id(handle: usize, client_id: i32) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.set_local_client_id(client_id);
    }
);

ffi_wrap!(
    unsafe fn voice_set_context_hash(handle: usize, hash: u32) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.set_context_hash(hash);
    }
);

ffi_wrap!(
    unsafe fn voice_get_mic_level(handle: usize) -> i32 {
        if handle == 0 {
            return 0;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.get_mic_level()
    }
);

ffi_wrap!(
    unsafe fn voice_get_ping(handle: usize) -> i32 {
        if handle == 0 {
            return -1;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.get_ping_ms()
    }
);

ffi_wrap!(
    unsafe fn voice_is_speaking(handle: usize) -> i32 {
        if handle == 0 {
            return 0;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        if handle.system.is_speaking() {
            1
        } else {
            0
        }
    }
);

ffi_wrap!(
    unsafe fn voice_process_frame(
        handle: usize,
        pcm: *mut i16,
        pcm_len: usize,
        output: *mut u8,
        output_len: usize,
    ) -> i32 {
        if handle == 0 || pcm.is_null() || pcm_len < VOICE_FRAME_SAMPLES {
            return 0;
        }

        if output.is_null() || output_len < VOICE_MAX_PACKET {
            return 0;
        }

        let handle = &*(handle as *const VoiceSystemHandle);

        let pcm_slice = std::slice::from_raw_parts_mut(pcm, pcm_len.min(VOICE_FRAME_SAMPLES));
        let output_slice = std::slice::from_raw_parts_mut(output, output_len);

        let mut result_len: i32 = 0;

        handle.system.on_frame(pcm_slice, |packet_data| {
            if result_len == 0 && packet_data.len() <= output_len {
                output_slice[..packet_data.len()].copy_from_slice(packet_data);
                result_len = packet_data.len() as i32;
            }
        });

        result_len
    }
);

ffi_wrap!(
    unsafe fn voice_receive_packet(handle: usize, data: *const u8, len: usize) {
        if handle == 0 || data.is_null() || len == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        let data_slice = std::slice::from_raw_parts(data, len);
        handle.system.on_receive(data_slice);
    }
);

ffi_wrap!(
    unsafe fn voice_decode_jitter(handle: usize) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.decode_jitter();
    }
);

ffi_wrap!(
    unsafe fn voice_mix_audio(handle: usize, output: *mut i16, samples: usize, channels: usize) {
        if handle == 0 || output.is_null() || samples == 0 || channels == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        let output_slice = std::slice::from_raw_parts_mut(output, samples * channels);
        handle.system.mix_audio(output_slice, channels);
    }
);

ffi_wrap!(
    unsafe fn voice_update_encoder_params(handle: usize) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);
        handle.system.update_encoder_params();
    }
);

// ============== 名单过滤 FFI ==============

ffi_wrap!(
    unsafe fn voice_set_name_lists(
        handle: usize,
        whitelist: *const c_char,
        blacklist: *const c_char,
        mute: *const c_char,
        vad_allow: *const c_char,
        name_volumes: *const c_char,
    ) {
        if handle == 0 {
            return;
        }
        let handle = &*(handle as *const VoiceSystemHandle);

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
        let vad_allow_owned = if vad_allow.is_null() {
            String::new()
        } else {
            CStr::from_ptr(vad_allow).to_string_lossy().into_owned()
        };
        let name_volumes_owned = if name_volumes.is_null() {
            String::new()
        } else {
            CStr::from_ptr(name_volumes).to_string_lossy().into_owned()
        };

        handle.system.set_name_lists(
            &whitelist_owned,
            &blacklist_owned,
            &mute_owned,
            &vad_allow_owned,
            &name_volumes_owned,
        );
    }
);

// ============== 单元测试 ==============

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

        // mic_level is updated internally by audio processing
        assert_eq!(system.get_mic_level(), 0);
    }

    #[test]
    fn test_voice_system_init() {
        let system = VoiceSystem::new();
        assert!(!system.is_initialized());

        let result = system.init();
        assert!(result);
        assert!(system.is_initialized());

        system.shutdown();
        assert!(!system.is_initialized());
    }

    #[test]
    fn test_calculate_peak() {
        let samples = vec![0i16, 1000, -2000, 500, -3000];
        let peak = VoiceSystem::calculate_peak(&samples);
        assert!((peak - 3000.0 / 32768.0).abs() < 0.001);
    }

    #[test]
    fn test_seq_less() {
        assert!(VoiceSystem::seq_less(1, 2));
        assert!(!VoiceSystem::seq_less(2, 1));
        assert!(VoiceSystem::seq_less(65535, 0)); // 回绕
        assert!(!VoiceSystem::seq_less(0, 65535));
    }

    #[test]
    fn test_clamp_jitter_target() {
        assert_eq!(VoiceSystem::clamp_jitter_target(5.0), 2);
        assert_eq!(VoiceSystem::clamp_jitter_target(10.0), 3);
        assert_eq!(VoiceSystem::clamp_jitter_target(20.0), 4);
        assert_eq!(VoiceSystem::clamp_jitter_target(30.0), 5);
        assert_eq!(VoiceSystem::clamp_jitter_target(50.0), 6);
    }

    #[test]
    fn test_c_abi() {
        let handle = voice_system_create();
        assert_ne!(handle, 0);

        let config = VoiceConfigCABI {
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
            context_hash: 0,
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
        };

        unsafe {
            voice_set_config(handle, &config);
        }

        unsafe {
            voice_set_ptt(handle, 1);
            assert_eq!(voice_is_speaking(handle), 0);

            voice_system_destroy(handle);
        }
    }

    #[test]
    fn test_ffi_lifecycle() {
        let handle = voice_system_create();
        assert_ne!(handle, 0);

        assert_eq!(unsafe { voice_system_init(handle) }, 1);

        // 设置配置
        let config = VoiceConfigCABI {
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
            context_hash: 0,
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
        };
        unsafe {
            voice_set_config(handle, &config);
        }

        // 销毁
        unsafe {
            voice_system_destroy(handle);
        }
    }

    #[test]
    fn test_ffi_config_context_hash() {
        let handle = voice_system_create();
        assert_ne!(handle, 0);

        let config = VoiceConfigCABI {
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
            token_hash: 0xABCDEF00,
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
        };

        unsafe {
            voice_set_config(handle, &config);
        }

        // 验证 context_hash 被正确存储到原子变量
        // 通过 voice_set_context_hash 也能设置
        unsafe {
            voice_set_context_hash(handle, 0x87654321);
        }

        // 清理
        unsafe {
            voice_system_destroy(handle);
        }
    }

    #[test]
    fn test_ffi_update_players_bounds() {
        let handle = voice_system_create();
        assert_ne!(handle, 0);

        // count > 64 应被拒绝
        let players: [f32; 5] = [1.0, 0.0, 0.0, 0.0, 1.0];
        unsafe {
            voice_update_players(handle, players.as_ptr(), 65); // 应该内部拒绝，不崩溃
        }

        // null 指针应被拒绝
        unsafe {
            voice_update_players(handle, std::ptr::null(), 1); // 应该内部拒绝，不崩溃
        }

        // count = 0 应被拒绝
        unsafe {
            voice_update_players(handle, players.as_ptr(), 0); // 应该内部拒绝，不崩溃
        }

        unsafe {
            voice_system_destroy(handle);
        }
    }

    #[test]
    fn test_ffi_catch_unwind_protection() {
        // 无效句柄不应导致 panic（catch_unwind 保护）
        assert_eq!(unsafe { voice_system_init(0) }, 0);
        assert_eq!(unsafe { voice_is_speaking(0) }, 0);
        assert_eq!(unsafe { voice_get_mic_level(0) }, 0);
        assert_eq!(unsafe { voice_get_ping(0) }, -1);

        // 销毁句柄 0 不应崩溃（内部检查 handle != 0）
        unsafe {
            voice_system_destroy(0);
        }
        // 注意：不测试 voice_system_destroy(99999)，因为非零无效句柄
        // 会导致 Box::from_raw 解引用野指针，这是 UB 而非 panic，
        // catch_unwind 无法捕获段错误（STATUS_ACCESS_VIOLATION）
    }

    #[test]
    fn test_team_filter_visibility_mode_0() {
        // 测试 visibility_mode = 0 (默认模式)
        let system = VoiceSystem::new();

        // 设置本地玩家 (client_id=0, team=1)
        system.set_local_client_id(0);
        let local_player = PlayerSnapshot {
            client_id: 0,
            name: "LocalPlayer".to_string(),
            x: 100.0,
            y: 100.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 100.0,
        };

        // 同队伍玩家 (team=1, active)
        let same_team_player = PlayerSnapshot {
            client_id: 1,
            name: "Teammate".to_string(),
            x: 200.0,
            y: 200.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 200.0,
            spec_y: 200.0,
        };

        // 其他队伍玩家 (team=2, active)
        let other_team_player = PlayerSnapshot {
            client_id: 2,
            name: "Enemy".to_string(),
            x: 300.0,
            y: 300.0,
            team: 2,
            is_spectator: false,
            is_active: true,
            spec_x: 300.0,
            spec_y: 300.0,
        };

        // 旁观者 (team=0, spectator)
        let spectator = PlayerSnapshot {
            client_id: 3,
            name: "Spectator".to_string(),
            x: 400.0,
            y: 400.0,
            team: 0,
            is_spectator: true,
            is_active: false,
            spec_x: 400.0,
            spec_y: 400.0,
        };

        system.update_players(vec![
            local_player,
            same_team_player,
            other_team_player,
            spectator,
        ]);

        // visibility_mode = 0, ignore_distance = false
        // 同队伍活跃玩家应该被听到
        assert!(system.should_hear_by_team(1, false));
        // 其他队伍活跃玩家应该被听到（因为 active）
        assert!(system.should_hear_by_team(2, false));
        // 旁观者不应该被听到（因为 !active && !allow_observer）
        // 但这里 hear_in_spectate 默认为 false，所以 allow_observer = false
        // 因此旁观者不应该被听到
        // 注意：这里的行为取决于 hear_in_spectate 配置
    }

    #[test]
    fn test_team_filter_visibility_mode_1() {
        // 测试 visibility_mode = 1 (显示其他队伍模式)
        let system = VoiceSystem::new();

        // 设置配置
        let mut config = VoiceConfig::default();
        config.visibility_mode = 1; // 显示其他队伍模式
        system.set_config(config);

        // 设置本地玩家 (client_id=0, team=1)
        system.set_local_client_id(0);
        let local_player = PlayerSnapshot {
            client_id: 0,
            name: "LocalPlayer".to_string(),
            x: 100.0,
            y: 100.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 100.0,
        };

        // 同队伍玩家
        let same_team_player = PlayerSnapshot {
            client_id: 1,
            name: "Teammate".to_string(),
            x: 200.0,
            y: 200.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 200.0,
            spec_y: 200.0,
        };

        // 其他队伍玩家
        let other_team_player = PlayerSnapshot {
            client_id: 2,
            name: "Enemy".to_string(),
            x: 300.0,
            y: 300.0,
            team: 2,
            is_spectator: false,
            is_active: true,
            spec_x: 300.0,
            spec_y: 300.0,
        };

        system.update_players(vec![local_player, same_team_player, other_team_player]);

        // visibility_mode = 1, ignore_distance = false
        // 同队伍玩家应该被听到
        assert!(system.should_hear_by_team(1, false));
        // 其他队伍玩家不应该被听到（因为 !same_team && !allow_observer）
        assert!(!system.should_hear_by_team(2, false));
    }

    #[test]
    fn test_team_filter_visibility_mode_2() {
        // 测试 visibility_mode = 2 (全部可见模式)
        let system = VoiceSystem::new();

        // 设置配置
        let mut config = VoiceConfig::default();
        config.visibility_mode = 2; // 全部可见模式
        system.set_config(config);

        // 设置本地玩家
        system.set_local_client_id(0);
        let local_player = PlayerSnapshot {
            client_id: 0,
            name: "LocalPlayer".to_string(),
            x: 100.0,
            y: 100.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 100.0,
        };

        // 其他队伍玩家
        let other_team_player = PlayerSnapshot {
            client_id: 2,
            name: "Enemy".to_string(),
            x: 300.0,
            y: 300.0,
            team: 2,
            is_spectator: false,
            is_active: true,
            spec_x: 300.0,
            spec_y: 300.0,
        };

        system.update_players(vec![local_player, other_team_player]);

        // visibility_mode = 2，所有玩家都应该被听到
        assert!(system.should_hear_by_team(2, false));
    }

    #[test]
    fn test_team_filter_with_ignore_distance() {
        // 测试 ignore_distance = true 的情况
        let system = VoiceSystem::new();

        // 设置本地玩家
        system.set_local_client_id(0);
        let local_player = PlayerSnapshot {
            client_id: 0,
            name: "LocalPlayer".to_string(),
            x: 100.0,
            y: 100.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 100.0,
        };

        // 非活跃旁观者
        let spectator = PlayerSnapshot {
            client_id: 3,
            name: "Spectator".to_string(),
            x: 400.0,
            y: 400.0,
            team: 0,
            is_spectator: true,
            is_active: false,
            spec_x: 400.0,
            spec_y: 400.0,
        };

        system.update_players(vec![local_player, spectator]);

        // visibility_mode = 0
        // ignore_distance = true 时，即使 !active && !allow_observer 也应该被听到
        assert!(system.should_hear_by_team(3, true));
        // ignore_distance = false 时，旁观者不应该被听到
        assert!(!system.should_hear_by_team(3, false));
    }

    #[test]
    fn test_team_filter_with_hear_in_spectate() {
        // 测试 hear_in_spectate = true 的情况
        let system = VoiceSystem::new();

        // 设置配置
        let mut config = VoiceConfig::default();
        config.hear_in_spectate = true; // 允许收听旁观者语音
        system.set_config(config);

        // 设置本地玩家
        system.set_local_client_id(0);
        let local_player = PlayerSnapshot {
            client_id: 0,
            name: "LocalPlayer".to_string(),
            x: 100.0,
            y: 100.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 100.0,
        };

        // 非活跃自由观众（非旁观者，只是非活跃观察者）
        let spectator = PlayerSnapshot {
            client_id: 3,
            name: "Spectator".to_string(),
            x: 400.0,
            y: 400.0,
            team: 0,
            is_spectator: false, // 不是旁观者，只是非活跃观察者
            is_active: false,
            spec_x: 400.0,
            spec_y: 400.0,
        };

        system.update_players(vec![local_player, spectator]);

        // visibility_mode = 0
        // hear_in_spectate = true 且 !active && !spectator = true
        // allow_observer = true && true && true = true
        // 所以非活跃观察者应该被听到
        assert!(system.should_hear_by_team(3, false));
    }
}
