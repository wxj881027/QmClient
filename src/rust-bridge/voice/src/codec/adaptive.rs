//! 自适应比特率控制
//!
//! 根据网络质量动态调整编码参数：
//! - 丢包率 ≤2%: 32000 bps, FEC 关闭
//! - 丢包率 ≤5%: 24000 bps, FEC 开启
//! - 丢包率 ≤10%: 20000 bps, FEC 开启
//! - 丢包率 >10%: 16000 bps, FEC 开启

use std::collections::VecDeque;

/// 网络质量等级
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NetworkQuality {
    Excellent, // ≤2% 丢包
    Good,      // ≤5% 丢包
    Fair,      // ≤10% 丢包
    Poor,      // >10% 丢包
}

/// 自适应比特率控制器
pub struct AdaptiveBitrate {
    /// 丢包率 EWMA (指数加权移动平均)
    loss_ewma: f32,
    /// 抖动 EWMA (ms)
    jitter_ewma: f32,
    /// 当前比特率
    current_bitrate: i32,
    /// 是否启用 FEC
    fec_enabled: bool,
    /// 丢包历史
    loss_history: VecDeque<f32>,
    /// 最后评估时间
    last_eval_time: std::time::Instant,
    /// 评估间隔
    eval_interval_ms: u64,
}

impl AdaptiveBitrate {
    pub fn new() -> Self {
        Self {
            loss_ewma: 0.0,
            jitter_ewma: 0.0,
            current_bitrate: 24000,
            fec_enabled: true,
            loss_history: VecDeque::with_capacity(100),
            last_eval_time: std::time::Instant::now(),
            eval_interval_ms: 1000, // 每秒评估一次
        }
    }

    /// 记录丢包
    ///
    /// # 参数
    /// * `expected` - 期望接收的包数
    /// * `received` - 实际接收的包数
    pub fn record_packet_loss(&mut self, expected: u32, received: u32) {
        let lost = expected.saturating_sub(received);
        let loss_ratio = if expected > 0 {
            lost as f32 / expected as f32
        } else {
            0.0
        };

        // 更新 EWMA
        self.loss_ewma = 0.9 * self.loss_ewma + 0.1 * loss_ratio;

        // 记录历史
        self.loss_history.push_back(loss_ratio);
        if self.loss_history.len() > 100 {
            self.loss_history.pop_front();
        }
    }

    /// 记录抖动
    pub fn record_jitter(&mut self, jitter_ms: f32) {
        self.jitter_ewma = 0.9 * self.jitter_ewma + 0.1 * jitter_ms;
    }

    /// 获取当前网络质量等级
    pub fn get_quality(&self) -> NetworkQuality {
        let loss_percent = self.loss_ewma * 100.0;

        if loss_percent <= 2.0 {
            NetworkQuality::Excellent
        } else if loss_percent <= 5.0 {
            NetworkQuality::Good
        } else if loss_percent <= 10.0 {
            NetworkQuality::Fair
        } else {
            NetworkQuality::Poor
        }
    }

    /// 评估并更新编码参数
    ///
    /// 返回 (新比特率, FEC 是否启用)
    pub fn evaluate(&mut self) -> (i32, bool) {
        let now = std::time::Instant::now();
        let elapsed = now.duration_since(self.last_eval_time).as_millis() as u64;

        if elapsed < self.eval_interval_ms {
            return (self.current_bitrate, self.fec_enabled);
        }

        self.last_eval_time = now;

        let quality = self.get_quality();

        let (target_bitrate, fec) = match quality {
            NetworkQuality::Excellent => {
                // 优秀网络，提高比特率，关闭 FEC
                let new_bitrate = (self.current_bitrate + 2000).min(32000);
                (new_bitrate, false)
            }
            NetworkQuality::Good => {
                // 良好网络，标准比特率，开启 FEC
                (24000, true)
            }
            NetworkQuality::Fair => {
                // 一般网络，降低比特率，开启 FEC
                let new_bitrate = (self.current_bitrate - 2000).max(20000);
                (new_bitrate, true)
            }
            NetworkQuality::Poor => {
                // 差网络，最低比特率，开启 FEC
                (16000, true)
            }
        };

        // 平滑过渡
        let diff = target_bitrate - self.current_bitrate;
        if diff.abs() > 1000 {
            self.current_bitrate += diff.signum() * 2000;
        } else {
            self.current_bitrate = target_bitrate;
        }

        self.fec_enabled = fec;

        (self.current_bitrate, self.fec_enabled)
    }

    /// 获取当前丢包率 (0.0 - 1.0)
    pub fn get_loss_rate(&self) -> f32 {
        self.loss_ewma
    }

    /// 获取当前比特率
    pub fn get_bitrate(&self) -> i32 {
        self.current_bitrate
    }

    /// 是否启用 FEC
    pub fn is_fec_enabled(&self) -> bool {
        self.fec_enabled
    }

    /// 重置状态
    pub fn reset(&mut self) {
        self.loss_ewma = 0.0;
        self.jitter_ewma = 0.0;
        self.current_bitrate = 24000;
        self.fec_enabled = true;
        self.loss_history.clear();
        self.last_eval_time = std::time::Instant::now();
    }

    /// 设置评估间隔
    pub fn set_eval_interval(&mut self, ms: u64) {
        self.eval_interval_ms = ms;
    }
}

impl Default for AdaptiveBitrate {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initial_state() {
        let adaptive = AdaptiveBitrate::new();
        assert_eq!(adaptive.get_bitrate(), 24000);
        assert_eq!(adaptive.is_fec_enabled(), true);
        assert_eq!(adaptive.get_loss_rate(), 0.0);
    }

    #[test]
    fn test_packet_loss_recording() {
        let mut adaptive = AdaptiveBitrate::new();

        // 记录 10% 丢包
        for _ in 0..10 {
            adaptive.record_packet_loss(100, 90);
        }

        assert!(adaptive.get_loss_rate() > 0.05);
        assert_eq!(adaptive.get_quality(), NetworkQuality::Fair);
    }

    #[test]
    fn test_adaptation() {
        let mut adaptive = AdaptiveBitrate::new();
        // 设置评估间隔为 0，确保每次调用都执行评估逻辑
        adaptive.set_eval_interval(0);

        // 模拟优秀网络
        for _ in 0..10 {
            adaptive.record_packet_loss(100, 99);
        }

        let (bitrate, fec) = adaptive.evaluate();

        // 应该提高比特率并关闭 FEC
        assert!(bitrate >= 24000);
        assert_eq!(fec, false);
    }

    #[test]
    fn test_poor_network() {
        let mut adaptive = AdaptiveBitrate::new();
        // 设置评估间隔为 0，确保每次调用都执行评估逻辑
        adaptive.set_eval_interval(0);

        // 模拟差网络 (20% 丢包)
        for _ in 0..10 {
            adaptive.record_packet_loss(100, 80);
        }

        // 比特率调整有平滑过渡机制，需要多次评估才能达到目标值
        // 24000 -> 22000 -> 20000 -> 18000 -> 16000
        let mut last_bitrate = 24000;
        for _ in 0..5 {
            let (bitrate, fec) = adaptive.evaluate();
            // 每次应该降低或保持
            assert!(bitrate <= last_bitrate);
            assert_eq!(fec, true);
            last_bitrate = bitrate;
        }

        // 最终应该稳定在最低比特率
        let (bitrate, fec) = adaptive.evaluate();
        assert_eq!(bitrate, 16000);
        assert_eq!(fec, true);
    }

    #[test]
    fn test_reset() {
        let mut adaptive = AdaptiveBitrate::new();

        adaptive.record_packet_loss(100, 80);
        adaptive.record_jitter(50.0);

        adaptive.reset();

        assert_eq!(adaptive.get_loss_rate(), 0.0);
        assert_eq!(adaptive.get_bitrate(), 24000);
    }
}
