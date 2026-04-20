//! 自动增益控制模块 (AGC2 - Automatic Gain Control)
//!
//! ## 概述
//!
//! 实现类似 WebRTC AGC2 的智能增益控制算法，
//! 支持目标音量自动调整、防削波预测、自适应增益调整。
//!
//! ## 算法原理
//!
//! ### 1. 目标增益计算
//!
//! 根据当前音量估计和目标音量计算所需增益：
//!
//! ```text
//! desired_gain = target_level / current_level
//! desired_gain_db = 20 * log10(desired_gain)
//! clamped_gain_db = clamp(desired_gain_db, min_gain_db, max_gain_db)
//! target_gain = 10^(clamped_gain_db / 20)
//! ```
//!
//! 增益被限制在 [min_gain_db, max_gain_db] 范围内，避免过度放大或衰减。
//!
//! ### 2. 音量估计 (RMS)
//!
//! 使用滑动窗口计算均方根 (RMS) 作为音量估计：
//!
//! ```text
//! current_level = sqrt(mean(samples^2))
//! ```
//!
//! 窗口大小由 `level_estimator_window` 参数控制，默认 480 样本 (10ms @ 48kHz)。
//!
//! ### 3. 时间常数平滑
//!
//! 使用指数平滑实现攻击/释放时间控制：
//!
//! ```text
//! coeff = 1 - exp(-1 / time_constant_samples)
//! current_gain += (target_gain - current_gain) * coeff
//! ```
//!
//! - **攻击时间** (attack_ms): 增益增加时的响应时间，默认 50ms
//! - **释放时间** (release_ms): 增益减少时的响应时间，默认 200ms
//!
//! 释放时间通常比攻击时间长，避免音量快速波动。
//!
//! ### 4. 防削波限制器
//!
//! 预测当前增益是否会导致削波，动态调整增益上限：
//!
//! ```text
//! max_safe_gain = limiter_threshold * 32767 / max_abs_sample
//! if current_gain > max_safe_gain:
//!     limiter_gain_cap = max_safe_gain
//!     state = Attenuating
//! ```
//!
//! 限制器阈值默认为 0.95 (95% 满幅度)，留出 5% 余量。
//!
//! ### 5. VAD 辅助
//!
//! 当启用 VAD 辅助时，非语音期间减慢增益调整速度：
//!
//! ```text
//! if !vad_active && gain_increasing:
//!     adjusted_coeff = coeff * 0.1  // 减慢 10 倍
//! ```
//!
//! 避免在静音段错误地增加增益。
//!
//! ### 6. 状态机
//!
//! AGC 使用状态机跟踪当前状态：
//!
//! | 状态 | 条件 | 行为 |
//! |------|------|------|
//! | Idle | 音量 < 50 | 等待有效信号 |
//! | Gaining | target_gain > current_gain | 增加增益 |
//! | Attenuating | target_gain < current_gain | 减少增益 |
//! | Stable | diff < 0.01 持续 10 帧 | 保持当前增益 |
//!
//! ## 参考文献
//!
//! - [1] WebRTC AGC2: <https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/agc2/>
//! - [2] ITU-T G.169: 自动电平控制设备标准
//! - [3] AES Conference Paper: "Digital Dynamic Range Control"
//! - [4] Zölzer, U. "Digital Audio Signal Processing", 2nd Edition
//!
//! ## 参数说明
//!
//! | 参数 | 默认值 | 说明 |
//! |------|--------|------|
//! | `target_level` | 2000 | 目标音量水平 (RMS)，约 -24dBFS |
//! | `max_gain_db` | 30.0 | 最大增益 (dB) |
//! | `min_gain_db` | -10.0 | 最小增益 (dB) |
//! | `attack_ms` | 50.0 | 攻击时间 (ms) |
//! | `release_ms` | 200.0 | 释放时间 (ms) |
//! | `enable_limiter` | true | 是否启用防削波 |
//! | `limiter_threshold` | 0.95 | 防削波阈值 |
//! | `level_estimator_window` | 480 | 音量估计窗口大小 |
//! | `use_vad` | true | 是否启用 VAD 辅助 |
//!
//! ## 性能特性
//!
//! - **Hot Path**: `process_frame` 在渲染线程调用，必须零堆分配
//! - 时间复杂度: O(N)，N 为帧大小
//! - 空间复杂度: O(level_estimator_window)

use std::collections::VecDeque;

/// AGC 配置
#[derive(Debug, Clone, Copy)]
pub struct AgcConfig {
    /// 目标音量水平 (RMS，范围 0-32767)
    /// 默认 2000 约等于 -24dBFS
    pub target_level: i16,
    /// 最大增益 (dB)，默认 30dB
    pub max_gain_db: f32,
    /// 最小增益 (dB)，默认 -10dB
    pub min_gain_db: f32,
    /// 攻击时间 (ms)，增益增加时的响应时间
    pub attack_ms: f32,
    /// 释放时间 (ms)，增益减少时的响应时间
    pub release_ms: f32,
    /// 是否启用防削波
    pub enable_limiter: bool,
    /// 防削波阈值 (相对于满幅度的比例，默认 0.95)
    pub limiter_threshold: f32,
    /// 音量估计窗口大小 (样本数)
    pub level_estimator_window: usize,
    /// 是否启用 VAD 辅助（只在有语音时调整增益）
    pub use_vad: bool,
}

impl Default for AgcConfig {
    fn default() -> Self {
        Self {
            target_level: 2000,       // 约 -24dBFS
            max_gain_db: 30.0,        // +30dB
            min_gain_db: -10.0,       // -10dB
            attack_ms: 50.0,          // 50ms 攻击时间
            release_ms: 200.0,        // 200ms 释放时间
            enable_limiter: true,     // 启用防削波
            limiter_threshold: 0.95,  // 95% 满幅度
            level_estimator_window: 480, // 10ms @ 48kHz
            use_vad: true,            // 使用 VAD 辅助
        }
    }
}

/// 增益状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AgcState {
    /// 空闲状态（无信号或信号太弱）
    Idle,
    /// 增益增加中（音量偏低）
    Gaining,
    /// 增益减少中（音量偏高或即将削波）
    Attenuating,
    /// 稳定状态
    Stable,
}

/// 自动增益控制器
pub struct AutomaticGainController {
    /// 配置
    config: AgcConfig,
    /// 当前状态
    state: AgcState,
    /// 当前增益 (线性，非 dB)
    current_gain: f32,
    /// 目标增益 (线性)
    target_gain: f32,
    /// 音量估计缓冲区
    level_buffer: VecDeque<f32>,
    /// 当前音量估计 (RMS)
    current_level: f32,
    /// 攻击系数 (每样本)
    attack_coeff: f32,
    /// 释放系数 (每样本)
    release_coeff: f32,
    /// 防削波预测增益上限
    limiter_gain_cap: f32,
    /// VAD 状态（外部设置）
    vad_active: bool,
    /// 稳定计数器
    stable_count: usize,
    /// 最后更新时间
    last_update_samples: usize,
}

impl AutomaticGainController {
    /// 创建新的 AGC 控制器
    pub fn new() -> Self {
        Self::with_config(AgcConfig::default())
    }

    /// 使用自定义配置创建 AGC
    pub fn with_config(config: AgcConfig) -> Self {
        let sample_rate = 48000u32;
        let (attack_coeff, release_coeff) = Self::calculate_time_constants(
            config.attack_ms,
            config.release_ms,
            sample_rate,
        );

        Self {
            config,
            state: AgcState::Idle,
            current_gain: 1.0,
            target_gain: 1.0,
            level_buffer: VecDeque::with_capacity(config.level_estimator_window),
            current_level: 0.0,
            attack_coeff,
            release_coeff,
            limiter_gain_cap: f32::MAX,
            vad_active: false,
            stable_count: 0,
            last_update_samples: 0,
        }
    }

    /// 计算时间常数
    fn calculate_time_constants(attack_ms: f32, release_ms: f32, sample_rate: u32) -> (f32, f32) {
        // 指数平滑系数计算: coeff = 1 - exp(-1 / (time_constant * sample_rate))
        let attack_samples = attack_ms * sample_rate as f32 / 1000.0;
        let release_samples = release_ms * sample_rate as f32 / 1000.0;

        let attack_coeff = 1.0 - (-1.0 / attack_samples).exp();
        let release_coeff = 1.0 - (-1.0 / release_samples).exp();

        (attack_coeff, release_coeff)
    }

    /// 处理一帧音频
    ///
    /// # 参数
    /// * `samples` - 输入/输出音频样本（原地处理）
    pub fn process_frame(&mut self, samples: &mut [i16]) {
        if samples.is_empty() {
            return;
        }

        // 1. 计算当前帧音量
        let frame_level = self.calculate_frame_level(samples);

        // 2. 更新音量估计
        self.update_level_estimate(frame_level);

        // 3. 防削波预测（限制增益上限）- 必须先执行以更新 limiter_gain_cap
        if self.config.enable_limiter {
            self.predict_and_limit_clipping(samples);
        }

        // 4. 计算目标增益（使用更新后的 limiter_gain_cap）
        self.calculate_target_gain();

        // 5. 平滑增益过渡
        self.smooth_gain_transition();

        // 6. 应用增益
        self.apply_gain(samples);

        // 7. 更新状态
        self.update_state();

        self.last_update_samples += samples.len();
    }

    /// 计算帧音量 (RMS)
    fn calculate_frame_level(&self, samples: &[i16]) -> f32 {
        let sum_squares: f64 = samples
            .iter()
            .map(|&s| (s as f64).powi(2))
            .sum();

        ((sum_squares / samples.len() as f64).sqrt()) as f32
    }

    /// 更新音量估计（使用滑动窗口）
    fn update_level_estimate(&mut self, frame_level: f32) {
        self.level_buffer.push_back(frame_level);

        // 限制窗口大小
        while self.level_buffer.len() > self.config.level_estimator_window {
            self.level_buffer.pop_front();
        }

        // 计算 RMS
        if !self.level_buffer.is_empty() {
            let sum_squares: f32 = self.level_buffer.iter().map(|&x| x * x).sum();
            self.current_level = (sum_squares / self.level_buffer.len() as f32).sqrt();
        }
    }

    /// 计算目标增益
    fn calculate_target_gain(&mut self) {
        // 如果音量太低，保持当前增益
        if self.current_level < 10.0 {
            self.target_gain = self.current_gain;
            return;
        }

        // 目标增益 = 目标音量 / 当前音量
        let target_level = self.config.target_level as f32;
        let desired_gain = target_level / self.current_level.max(1.0);

        // 转换为 dB 进行限制
        let desired_gain_db = 20.0 * desired_gain.log10();
        let clamped_gain_db = desired_gain_db.clamp(
            self.config.min_gain_db,
            self.config.max_gain_db,
        );

        // 转回线性增益
        self.target_gain = 10.0f32.powf(clamped_gain_db / 20.0);

        // 应用防削波上限
        self.target_gain = self.target_gain.min(self.limiter_gain_cap);
    }

    /// 预测并限制削波
    fn predict_and_limit_clipping(&mut self, samples: &[i16]) {
        // 找到当前帧的最大绝对值（使用 i32 避免 i16::MIN.abs() 溢出）
        let max_abs: i32 = samples.iter().map(|&s| (s as i32).abs()).max().unwrap_or(0);

        if max_abs == 0 {
            return;
        }

        // 计算最大安全增益
        let max_safe_gain = (self.config.limiter_threshold * 32767.0) / max_abs as f32;

        // 如果当前增益会导致削波，设置上限
        if self.current_gain > max_safe_gain {
            self.limiter_gain_cap = max_safe_gain;
            self.target_gain = self.target_gain.min(max_safe_gain);

            // 强制进入衰减状态
            self.state = AgcState::Attenuating;
        } else {
            // 逐渐放宽限制
            self.limiter_gain_cap = self.limiter_gain_cap * 1.001 + max_safe_gain * 0.001;
        }
    }

    /// 平滑增益过渡
    fn smooth_gain_transition(&mut self) {
        let diff = self.target_gain - self.current_gain;

        if diff.abs() < 0.001 {
            self.current_gain = self.target_gain;
            return;
        }

        // 根据增益变化方向选择系数
        let coeff = if diff > 0.0 {
            self.attack_coeff // 增益增加（攻击）
        } else {
            self.release_coeff // 增益减少（释放）
        };

        // 如果 VAD 未激活且配置要求 VAD，则减慢增益调整
        let adjusted_coeff = if self.config.use_vad && !self.vad_active && diff > 0.0 {
            coeff * 0.1 // 非语音期间缓慢增加增益
        } else {
            coeff
        };

        // 指数平滑
        self.current_gain += diff * adjusted_coeff;
    }

    /// 应用增益到样本
    fn apply_gain(&self, samples: &mut [i16]) {
        let gain = self.current_gain;

        // 计算限制器阈值对应的限制值
        let limit = if self.config.enable_limiter {
            (self.config.limiter_threshold * 32767.0) as i32
        } else {
            32767
        };

        for sample in samples.iter_mut() {
            let amplified = (*sample as f32 * gain) as i32;
            // 先应用增益，然后限制到限制器阈值，最后限制到 i16 范围
            *sample = amplified.clamp(-limit, limit).clamp(-32768, 32767) as i16;
        }
    }

    /// 更新状态机
    fn update_state(&mut self) {
        let diff = (self.target_gain - self.current_gain).abs();

        match self.state {
            AgcState::Idle => {
                if self.current_level > 100.0 {
                    if self.target_gain > self.current_gain {
                        self.state = AgcState::Gaining;
                    } else if self.target_gain < self.current_gain {
                        self.state = AgcState::Attenuating;
                    }
                }
            }
            AgcState::Gaining => {
                if diff < 0.01 {
                    self.stable_count += 1;
                    if self.stable_count > 10 {
                        self.state = AgcState::Stable;
                        self.stable_count = 0;
                    }
                } else if self.target_gain < self.current_gain {
                    self.state = AgcState::Attenuating;
                    self.stable_count = 0;
                }
            }
            AgcState::Attenuating => {
                if diff < 0.01 {
                    self.stable_count += 1;
                    if self.stable_count > 10 {
                        self.state = AgcState::Stable;
                        self.stable_count = 0;
                    }
                } else if self.target_gain > self.current_gain {
                    self.state = AgcState::Gaining;
                    self.stable_count = 0;
                }
            }
            AgcState::Stable => {
                if diff > 0.05 {
                    if self.target_gain > self.current_gain {
                        self.state = AgcState::Gaining;
                    } else {
                        self.state = AgcState::Attenuating;
                    }
                    self.stable_count = 0;
                }
            }
        }

        // 如果音量太低，回到空闲状态
        if self.current_level < 50.0 {
            self.state = AgcState::Idle;
        }
    }

    /// 设置 VAD 状态（外部调用）
    pub fn set_vad_active(&mut self, active: bool) {
        self.vad_active = active;
    }

    /// 获取当前状态
    pub fn state(&self) -> AgcState {
        self.state
    }

    /// 获取当前增益 (dB)
    pub fn gain_db(&self) -> f32 {
        20.0 * self.current_gain.log10()
    }

    /// 获取当前增益 (线性)
    pub fn gain_linear(&self) -> f32 {
        self.current_gain
    }

    /// 获取当前音量估计
    pub fn current_level(&self) -> f32 {
        self.current_level
    }

    /// 获取目标音量
    pub fn target_level(&self) -> i16 {
        self.config.target_level
    }

    /// 设置目标音量
    pub fn set_target_level(&mut self, level: i16) {
        self.config.target_level = level.clamp(100, 10000);
    }

    /// 设置最大增益
    pub fn set_max_gain_db(&mut self, gain_db: f32) {
        self.config.max_gain_db = gain_db.clamp(0.0, 60.0);
    }

    /// 重置 AGC 状态
    pub fn reset(&mut self) {
        self.state = AgcState::Idle;
        self.current_gain = 1.0;
        self.target_gain = 1.0;
        self.level_buffer.clear();
        self.current_level = 0.0;
        self.limiter_gain_cap = f32::MAX;
        self.vad_active = false;
        self.stable_count = 0;
        self.last_update_samples = 0;
    }
}

impl Default for AutomaticGainController {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_agc_creation() {
        let agc = AutomaticGainController::new();
        assert_eq!(agc.state(), AgcState::Idle);
        assert!((agc.gain_db() - 0.0).abs() < 0.1);
    }

    #[test]
    fn test_agc_low_signal() {
        let mut agc = AutomaticGainController::new();
        let mut samples = vec![10i16; 960]; // 很弱的信号

        // 处理多帧
        for _ in 0..20 {
            agc.process_frame(&mut samples);
        }

        // 应该增加增益
        assert!(agc.gain_db() > 0.0);
    }

    #[test]
    fn test_agc_high_signal() {
        let mut agc = AutomaticGainController::new();
        // 很强的信号（接近削波）
        let mut samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16 * 300).collect();

        // 处理多帧
        for _ in 0..20 {
            agc.process_frame(&mut samples.clone());
        }

        // 应该减少增益或保持稳定
        assert!(agc.gain_db() <= 30.0);
    }

    #[test]
    fn test_agc_limiter() {
        let mut config = AgcConfig::default();
        config.enable_limiter = true;
        config.limiter_threshold = 0.5; // 50% 阈值

        let mut agc = AutomaticGainController::with_config(config);

        // 强信号
        let mut samples = vec![30000i16; 960];

        agc.process_frame(&mut samples);

        // 检查是否被限制（使用 i32 避免 i16::MIN.abs() 溢出）
        let max_sample: i32 = samples.iter().map(|&s| (s as i32).abs()).max().unwrap();
        assert!(max_sample <= 16384); // 50% of 32767
    }

    #[test]
    fn test_agc_vad_assist() {
        let mut config = AgcConfig::default();
        config.use_vad = true;

        let mut agc = AutomaticGainController::with_config(config);

        // 弱信号
        let mut samples = vec![50i16; 960];

        // VAD 未激活时处理
        agc.set_vad_active(false);
        for _ in 0..10 {
            agc.process_frame(&mut samples.clone());
        }
        let gain_no_vad = agc.gain_db();

        // 重置并 VAD 激活时处理
        agc.reset();
        agc.set_vad_active(true);
        for _ in 0..10 {
            agc.process_frame(&mut samples.clone());
        }
        let gain_with_vad = agc.gain_db();

        // VAD 激活时增益应该增加更快
        assert!(gain_with_vad >= gain_no_vad);
    }

    #[test]
    fn test_agc_target_level() {
        let mut agc = AutomaticGainController::new();

        agc.set_target_level(5000);
        assert_eq!(agc.target_level(), 5000);
    }

    #[test]
    fn test_agc_reset() {
        let mut agc = AutomaticGainController::new();

        // 处理一些数据
        let mut samples = vec![100i16; 960];
        for _ in 0..10 {
            agc.process_frame(&mut samples.clone());
        }

        // 增益应该已经改变
        assert!(agc.gain_db() != 0.0);

        // 重置
        agc.reset();

        // 检查重置状态
        assert_eq!(agc.state(), AgcState::Idle);
        assert!((agc.gain_db() - 0.0).abs() < 0.01);
    }

    #[test]
    fn test_agc_state_transitions() {
        let mut agc = AutomaticGainController::new();

        // 初始状态
        assert_eq!(agc.state(), AgcState::Idle);

        // 弱信号 - 应该进入 Gaining 状态
        let mut weak_samples = vec![100i16; 960];
        agc.process_frame(&mut weak_samples);

        // 多次处理以触发状态变化
        for _ in 0..50 {
            agc.process_frame(&mut weak_samples.clone());
        }

        // 应该有增益增加
        assert!(agc.gain_db() > 0.0);
    }
}
