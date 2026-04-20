//! 带宽估计模块 - GCC (Google Congestion Control) 风格实现
//!
//! ## 概述
//!
//! 提供基于延迟梯度和丢包率的混合带宽估计算法，
//! 实现更平滑的比特率过渡，避免音频质量突变。
//!
//! ## 算法原理
//!
//! 本模块实现了类似 GCC (Google Congestion Control) 的拥塞控制算法，
//! 主要包含以下核心组件：
//!
//! ### 1. 延迟梯度计算
//!
//! 延迟梯度是网络拥塞的关键指标，计算公式为：
//!
//! ```text
//! delay_gradient = (arrival_time[i] - arrival_time[i-1]) - (send_time[i] - send_time[i-1])
//! ```
//!
//! - 正值表示网络排队增加（拥塞）
//! - 负值表示网络空闲
//! - 接近零表示网络稳定
//!
//! ### 2. 趋势滤波器
//!
//! 使用滑动窗口平均滤波器平滑延迟梯度：
//!
//! ```text
//! filtered_gradient = mean(gradient_window)
//! ```
//!
//! 窗口大小由 `trend_window_size` 参数控制，默认 10 个样本。
//!
//! ### 3. 状态机检测
//!
//! 基于累积延迟梯度判断网络状态：
//!
//! | 状态 | 条件 | 动作 |
//! |------|------|------|
//! | Overuse | accumulated_gradient > overuse_threshold | 降低比特率 |
//! | Underuse | accumulated_gradient < underuse_threshold | 增加比特率 |
//! | Normal | abs(accumulated_gradient) < normal_threshold | 轻微增加（探测） |
//!
//! ### 4. 丢包率调整
//!
//! 当检测到丢包时，额外调整比特率：
//!
//! - 丢包率 > 10%: 大幅降低比特率
//! - 丢包率 > 5%: 适度降低比特率
//! - 丢包率 > 2%: 暂停增加
//! - 丢包率 < 2%: 允许探测增加
//!
//! ### 5. 平滑过渡
//!
//! 使用指数移动平均 (EMA) 平滑比特率变化：
//!
//! ```text
//! estimated_bitrate = (1 - alpha) * current + alpha * target
//! ```
//!
//! 避免比特率突变导致的音频质量波动。
//!
//! ## 参考文献
//!
//! - [1] GCC 算法规范: <https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02>
//! - [2] WebRTC GCC 实现: <https://webrtc.googlesource.com/src/+/refs/heads/main/modules/congestion_controller/goog_cc/>
//! - [3] 延迟梯度拥塞检测: <https://tools.ietf.org/html/draft-ietf-rmcat-delay-based-congestion-control>
//!
//! ## 参数说明
//!
//! | 参数 | 默认值 | 说明 |
//! |------|--------|------|
//! | `delay_window_size` | 20 | 延迟样本窗口大小 |
//! | `trend_window_size` | 10 | 趋势滤波器窗口大小 |
//! | `overuse_threshold_ms` | 1.5 | 过度使用阈值 (ms) |
//! | `underuse_threshold_ms` | -1.5 | 欠使用阈值 (ms) |
//! | `normal_threshold_ms` | 0.5 | 正常区域阈值 (ms) |
//! | `min_bitrate` | 16000 | 最小比特率 (bps) |
//! | `max_bitrate` | 64000 | 最大比特率 (bps) |
//! | `bitrate_adjust_step` | 0.05 | 比特率调整步长比例 |
//! | `smoothing_factor` | 0.3 | 平滑系数 (alpha) |
//! | `state_duration_threshold` | 100ms | 状态持续时间阈值 |

use std::collections::VecDeque;
use std::time::{Duration, Instant};

/// 带宽估计配置
#[derive(Debug, Clone)]
pub struct BandwidthEstimatorConfig {
    /// 延迟梯度窗口大小（样本数）
    pub delay_window_size: usize,
    /// 趋势滤波器窗口大小
    pub trend_window_size: usize,
    /// 过度使用阈值（ms）
    pub overuse_threshold_ms: f32,
    /// 欠使用阈值（ms）
    pub underuse_threshold_ms: f32,
    /// 正常区域阈值（ms）
    pub normal_threshold_ms: f32,
    /// 最小比特率 (bps)
    pub min_bitrate: i32,
    /// 最大比特率 (bps)
    pub max_bitrate: i32,
    /// 比特率调整步长比例
    pub bitrate_adjust_step: f32,
    /// 平滑系数（alpha）
    pub smoothing_factor: f32,
    /// 状态持续时间阈值
    pub state_duration_threshold: Duration,
}

impl Default for BandwidthEstimatorConfig {
    fn default() -> Self {
        Self {
            delay_window_size: 20,
            trend_window_size: 10,
            overuse_threshold_ms: 1.5,
            underuse_threshold_ms: -1.5,
            normal_threshold_ms: 0.5,
            min_bitrate: 16000,
            max_bitrate: 64000,
            bitrate_adjust_step: 0.05,
            smoothing_factor: 0.3,
            state_duration_threshold: Duration::from_millis(100),
        }
    }
}

/// 带宽使用状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BandwidthUsage {
    /// 网络欠使用（可以增加比特率）
    Underuse,
    /// 网络正常使用
    Normal,
    /// 网络过度使用（需要降低比特率）
    Overuse,
}

/// 延迟样本
#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]
struct DelaySample {
    /// 到达时间
    arrival_time_ms: f32,
    /// 发送时间
    send_time_ms: f32,
    /// 包大小（保留供将来带宽计算使用）
    size_bytes: usize,
}

/// 带宽估计器
pub struct BandwidthEstimator {
    /// 配置
    config: BandwidthEstimatorConfig,
    /// 延迟样本窗口
    delay_samples: VecDeque<DelaySample>,
    /// 趋势滤波器窗口
    trend_samples: VecDeque<f32>,
    /// 当前估计带宽 (bps)
    estimated_bitrate: i32,
    /// 当前目标比特率 (bps)
    target_bitrate: i32,
    /// 上次更新时间
    last_update: Instant,
    /// 当前带宽使用状态
    usage_state: BandwidthUsage,
    /// 累积延迟梯度
    accumulated_delay_gradient: f32,
    /// 上次状态改变时间
    last_state_change: Instant,
    /// 丢包率 EWMA
    loss_ewma: f32,
    /// 丢包率平滑系数
    loss_smoothing: f32,
}

impl BandwidthEstimator {
    /// 创建新的带宽估计器
    pub fn new(initial_bitrate: i32) -> Self {
        let config = BandwidthEstimatorConfig::default();
        let now = Instant::now();

        Self {
            config,
            delay_samples: VecDeque::with_capacity(100),
            trend_samples: VecDeque::with_capacity(20),
            estimated_bitrate: initial_bitrate,
            target_bitrate: initial_bitrate,
            last_update: now,
            usage_state: BandwidthUsage::Normal,
            accumulated_delay_gradient: 0.0,
            last_state_change: now,
            loss_ewma: 0.0,
            loss_smoothing: 0.1,
        }
    }

    /// 创建带自定义配置的估计器
    pub fn with_config(initial_bitrate: i32, config: BandwidthEstimatorConfig) -> Self {
        let mut estimator = Self::new(initial_bitrate);
        estimator.config = config;
        estimator
    }

    /// 添加延迟样本
    ///
    /// # 参数
    /// * `arrival_time_ms` - 包到达时间（毫秒，单调递增）
    /// * `send_time_ms` - 包发送时间（毫秒，来自发送端）
    /// * `size_bytes` - 包大小（字节）
    pub fn add_delay_sample(&mut self, arrival_time_ms: f32, send_time_ms: f32, size_bytes: usize) {
        let sample = DelaySample {
            arrival_time_ms,
            send_time_ms,
            size_bytes,
        };

        self.delay_samples.push_back(sample);

        // 限制窗口大小
        while self.delay_samples.len() > self.config.delay_window_size * 2 {
            self.delay_samples.pop_front();
        }

        // 更新带宽估计
        self.update_estimate();
    }

    /// 报告丢包事件
    pub fn report_packet_loss(&mut self, lost: bool) {
        let loss_value = if lost { 1.0 } else { 0.0 };
        self.loss_ewma =
            self.loss_ewma * (1.0 - self.loss_smoothing) + loss_value * self.loss_smoothing;
    }

    /// 获取当前估计带宽
    pub fn estimated_bitrate(&self) -> i32 {
        self.estimated_bitrate
    }

    /// 获取当前目标比特率
    pub fn target_bitrate(&self) -> i32 {
        self.target_bitrate
    }

    /// 获取当前带宽使用状态
    pub fn usage_state(&self) -> BandwidthUsage {
        self.usage_state
    }

    /// 获取当前丢包率估计
    pub fn loss_rate(&self) -> f32 {
        self.loss_ewma
    }

    /// 更新带宽估计
    fn update_estimate(&mut self) {
        if self.delay_samples.len() < 2 {
            return;
        }

        // 计算延迟梯度
        let delay_gradient = self.calculate_delay_gradient();

        // 应用趋势滤波
        let filtered_gradient = self.apply_trend_filter(delay_gradient);

        // 检测带宽使用状态
        let new_state = self.detect_bandwidth_usage(filtered_gradient);

        // 更新状态（考虑持续时间阈值，避免抖动）
        self.update_usage_state(new_state);

        // 根据状态调整目标比特率
        self.adjust_target_bitrate();

        // 平滑过渡到目标比特率
        self.smooth_bitrate_transition();

        self.last_update = Instant::now();
    }

    /// 计算延迟梯度（包间延迟差）
    fn calculate_delay_gradient(&self) -> f32 {
        let samples: Vec<_> = self.delay_samples.iter().rev().take(2).collect();
        if samples.len() < 2 {
            return 0.0;
        }

        let current = samples[0];
        let previous = samples[1];

        // 到达时间差
        let arrival_delta = current.arrival_time_ms - previous.arrival_time_ms;
        // 发送时间差
        let send_delta = current.send_time_ms - previous.send_time_ms;

        // 延迟梯度 = 到达时间差 - 发送时间差
        // 正值表示网络排队增加（拥塞）
        // 负值表示网络空闲
        arrival_delta - send_delta
    }

    /// 应用趋势滤波器（简单移动平均）
    fn apply_trend_filter(&mut self, gradient: f32) -> f32 {
        self.trend_samples.push_back(gradient);

        // 限制窗口大小
        while self.trend_samples.len() > self.config.trend_window_size {
            self.trend_samples.pop_front();
        }

        // 计算移动平均
        let sum: f32 = self.trend_samples.iter().sum();
        sum / self.trend_samples.len() as f32
    }

    /// 检测带宽使用状态
    fn detect_bandwidth_usage(&mut self, filtered_gradient: f32) -> BandwidthUsage {
        // 累积延迟梯度
        self.accumulated_delay_gradient += filtered_gradient;

        // 根据累积梯度判断状态
        if self.accumulated_delay_gradient > self.config.overuse_threshold_ms {
            // 过度使用 - 重置累积值
            self.accumulated_delay_gradient = 0.0;
            BandwidthUsage::Overuse
        } else if self.accumulated_delay_gradient < self.config.underuse_threshold_ms {
            // 欠使用 - 重置累积值
            self.accumulated_delay_gradient = 0.0;
            BandwidthUsage::Underuse
        } else if self.accumulated_delay_gradient.abs() < self.config.normal_threshold_ms {
            // 正常区域 - 逐渐衰减累积值
            self.accumulated_delay_gradient *= 0.9;
            BandwidthUsage::Normal
        } else {
            // 保持当前状态
            self.usage_state
        }
    }

    /// 更新带宽使用状态（考虑持续时间阈值）
    fn update_usage_state(&mut self, new_state: BandwidthUsage) {
        let now = Instant::now();

        if new_state != self.usage_state {
            // 检查是否满足持续时间阈值
            if now.duration_since(self.last_state_change) >= self.config.state_duration_threshold {
                self.usage_state = new_state;
                self.last_state_change = now;
            }
        }
        // 状态相同时不更新时间，保持状态进入时间点
    }

    /// 根据状态调整目标比特率
    fn adjust_target_bitrate(&mut self) {
        match self.usage_state {
            BandwidthUsage::Overuse => {
                // 过度使用 - 降低比特率
                let reduction_factor = 1.0 - self.config.bitrate_adjust_step * 2.0;
                self.target_bitrate = (self.target_bitrate as f32 * reduction_factor) as i32;
            }
            BandwidthUsage::Underuse => {
                // 欠使用 - 增加比特率
                let increase_factor = 1.0 + self.config.bitrate_adjust_step;
                self.target_bitrate = (self.target_bitrate as f32 * increase_factor) as i32;
            }
            BandwidthUsage::Normal => {
                // 正常状态 - 轻微增加（探测可用带宽）
                if self.loss_ewma < 0.02 {
                    let increase_factor = 1.0 + self.config.bitrate_adjust_step * 0.5;
                    self.target_bitrate = (self.target_bitrate as f32 * increase_factor) as i32;
                }
            }
        }

        // 考虑丢包率的额外调整
        self.adjust_for_packet_loss();

        // 限制在有效范围内
        self.target_bitrate = self
            .target_bitrate
            .clamp(self.config.min_bitrate, self.config.max_bitrate);
    }

    /// 根据丢包率调整比特率
    fn adjust_for_packet_loss(&mut self) {
        if self.loss_ewma > 0.10 {
            // 高丢包率 - 大幅降低比特率
            let reduction_factor = 1.0 - self.config.bitrate_adjust_step * 3.0;
            self.target_bitrate = (self.target_bitrate as f32 * reduction_factor) as i32;
        } else if self.loss_ewma > 0.05 {
            // 中等丢包率 - 适度降低
            let reduction_factor = 1.0 - self.config.bitrate_adjust_step;
            self.target_bitrate = (self.target_bitrate as f32 * reduction_factor) as i32;
        } else if self.loss_ewma > 0.02 {
            // 轻微丢包 - 暂停增加
            // 不调整
        }
        // 低丢包率 (< 2%) 已在 Normal 状态中处理
    }

    /// 平滑比特率过渡（避免突变）
    fn smooth_bitrate_transition(&mut self) {
        let alpha = self.config.smoothing_factor;
        let current = self.estimated_bitrate as f32;
        let target = self.target_bitrate as f32;

        // 指数移动平均
        let smoothed = current * (1.0 - alpha) + target * alpha;

        self.estimated_bitrate = smoothed as i32;
    }

    /// 重置估计器
    pub fn reset(&mut self, initial_bitrate: i32) {
        self.delay_samples.clear();
        self.trend_samples.clear();
        self.estimated_bitrate = initial_bitrate;
        self.target_bitrate = initial_bitrate;
        self.usage_state = BandwidthUsage::Normal;
        self.accumulated_delay_gradient = 0.0;
        self.loss_ewma = 0.0;
        self.last_update = Instant::now();
        self.last_state_change = Instant::now();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bandwidth_estimator_basic() {
        let mut estimator = BandwidthEstimator::new(32000);

        // 模拟正常网络条件
        for i in 0..50 {
            let arrival = i as f32 * 20.0; // 20ms 间隔到达
            let send = i as f32 * 20.0; // 20ms 间隔发送
            estimator.add_delay_sample(arrival, send, 100);
        }

        // 正常情况下比特率应该保持稳定或轻微增加
        let bitrate = estimator.estimated_bitrate();
        assert!(bitrate >= 16000 && bitrate <= 64000);
    }

    #[test]
    fn test_overuse_detection() {
        // 使用自定义配置，降低状态持续时间阈值以便快速检测
        let mut config = BandwidthEstimatorConfig::default();
        config.state_duration_threshold = Duration::from_millis(0);
        let mut estimator = BandwidthEstimator::with_config(32000, config);

        // 模拟拥塞（到达延迟持续增加）
        let mut arrival_offset = 0.0;
        for i in 0..30 {
            let arrival = i as f32 * 20.0 + arrival_offset;
            let send = i as f32 * 20.0;
            estimator.add_delay_sample(arrival, send, 100);

            // 模拟不断增加的延迟
            arrival_offset += 2.0;
        }

        // 应该检测到过度使用
        assert_eq!(estimator.usage_state(), BandwidthUsage::Overuse);
        // 比特率应该降低
        assert!(estimator.target_bitrate() < 32000);
    }

    #[test]
    fn test_packet_loss_adjustment() {
        let mut estimator = BandwidthEstimator::new(32000);

        // 模拟高丢包率
        for _ in 0..20 {
            estimator.report_packet_loss(true);
        }

        // 添加一些样本触发更新
        for i in 0..10 {
            estimator.add_delay_sample(i as f32 * 20.0, i as f32 * 20.0, 100);
        }

        // 丢包率应该很高
        assert!(estimator.loss_rate() > 0.5);
    }

    #[test]
    fn test_bitrate_limits() {
        let config = BandwidthEstimatorConfig {
            min_bitrate: 20000,
            max_bitrate: 40000,
            ..Default::default()
        };

        let mut estimator = BandwidthEstimator::with_config(30000, config);

        // 模拟极端拥塞
        let mut arrival_offset = 0.0;
        for i in 0..100 {
            let arrival = i as f32 * 20.0 + arrival_offset;
            let send = i as f32 * 20.0;
            estimator.add_delay_sample(arrival, send, 100);
            arrival_offset += 5.0;
        }

        // 比特率应该被限制在最小值
        assert!(estimator.estimated_bitrate() >= 20000);
    }

    #[test]
    fn test_smooth_transition() {
        let mut estimator = BandwidthEstimator::new(32000);

        // 设置目标为更高值
        estimator.target_bitrate = 48000;

        // 多次调用平滑过渡
        let initial = estimator.estimated_bitrate();
        for _ in 0..10 {
            estimator.smooth_bitrate_transition();
        }
        let final_bitrate = estimator.estimated_bitrate();

        // 应该逐渐接近目标，而不是突变
        assert!(final_bitrate > initial);
        assert!(final_bitrate < 48000); // 还没有完全达到
    }
}
