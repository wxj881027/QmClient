//! 限制器 (Limiter)
//!
//! 提供硬削波限制功能，防止多人同时说话时音频削波失真。
//! 与 C++ 实现中的限制器逻辑对应。

/// 限制器状态
#[derive(Debug, Clone, Copy, Default)]
pub struct LimiterState {
    /// 是否启用
    enabled: bool,
    /// 限制阈值 (0.0 - 1.0)
    threshold: f32,
}

impl LimiterState {
    /// 创建新的限制器状态
    pub fn new() -> Self {
        Self {
            enabled: false,
            threshold: 0.5, // 默认 50%
        }
    }

    /// 设置限制器参数
    ///
    /// # 参数
    /// * `enabled` - 是否启用
    /// * `threshold_percent` - 限制阈值（百分比，5-100）
    pub fn set_params(&mut self, enabled: bool, threshold_percent: i32) {
        self.enabled = enabled;
        // 限制阈值在 5% 到 100% 之间
        self.threshold = (threshold_percent as f32 / 100.0).clamp(0.05, 1.0);
    }

    /// 重置状态
    pub fn reset(&mut self) {
        self.enabled = false;
        self.threshold = 0.5;
    }
}

/// 应用限制器到音频样本
///
/// # 参数
/// * `samples` - 音频样本数组（i16 格式）
/// * `state` - 限制器状态
/// * `threshold_percent` - 限制阈值（百分比，5-100），0 表示禁用
///
/// # 说明
/// 限制器对音频进行硬削波，确保输出不超过指定阈值。
/// 这是防止多人同时说话时削波失真的最后一道防线。
pub fn apply(samples: &mut [i16], state: &mut LimiterState, threshold_percent: i32) {
    // 如果阈值为 0，禁用限制器
    if threshold_percent <= 0 {
        state.enabled = false;
        return;
    }

    // 更新状态
    state.set_params(true, threshold_percent);

    let threshold = state.threshold;
    let limit = threshold * 32768.0;

    for sample in samples.iter_mut() {
        // 转换为 f32 进行处理
        let sample_f = *sample as f32;

        // 应用硬限制
        let limited = sample_f.clamp(-limit, limit);

        // 转换回 i16
        *sample = limited as i16;
    }
}

/// 快速应用限制器（无状态版本）
///
/// # 参数
/// * `samples` - 音频样本数组
/// * `threshold_percent` - 限制阈值（百分比，5-100）
///
/// # 说明
/// 这是一个无状态的快速限制器函数，适用于简单的使用场景。
pub fn apply_simple(samples: &mut [i16], threshold_percent: i32) {
    if threshold_percent <= 0 {
        return;
    }

    let threshold = (threshold_percent as f32 / 100.0).clamp(0.05, 1.0);
    let limit = threshold * 32768.0;

    for sample in samples.iter_mut() {
        let sample_f = *sample as f32;
        let limited = sample_f.clamp(-limit, limit);
        *sample = limited as i16;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_limiter_basic() {
        let mut state = LimiterState::new();
        let mut samples = vec![1000i16, 20000i16, -20000i16, 32767i16, -32768i16];

        // 使用 50% 阈值（限制到 -16384 到 16384）
        apply(&mut samples, &mut state, 50);

        assert_eq!(samples[0], 1000, "samples[0] should remain unchanged");
        assert_eq!(samples[1], 16384, "samples[1] should be limited to 16384");
        assert_eq!(samples[2], -16384, "samples[2] should be limited to -16384");
        assert_eq!(samples[3], 16384, "samples[3] should be limited to 16384");
        assert_eq!(samples[4], -16384, "samples[4] should be limited to -16384");
    }

    #[test]
    fn test_limiter_disabled() {
        let mut state = LimiterState::new();
        let original = vec![1000i16, 30000i16, -30000i16];
        let mut samples = original.clone();

        // 阈值为 0，禁用限制器
        apply(&mut samples, &mut state, 0);

        // 样本应该保持不变
        assert_eq!(samples, original);
    }

    #[test]
    fn test_limiter_simple() {
        let mut samples = vec![1000i16, 20000i16, -20000i16];

        // 使用 100% 阈值（实际上不限制）
        apply_simple(&mut samples, 100);

        // 100% 阈值意味着限制到 32767，所以这些样本不会被限制
        assert_eq!(samples[0], 1000);
        assert_eq!(samples[1], 20000);
        assert_eq!(samples[2], -20000);
    }

    #[test]
    fn test_limiter_threshold_clamping() {
        let mut state = LimiterState::new();
        let mut samples = vec![32767i16];

        // 测试阈值被限制在 5% 到 100% 之间
        apply(&mut samples, &mut state, 1); // 小于 5%，应该被限制到 5%
        assert_eq!(samples[0], 1638); // 约 5% of 32767

        let mut samples = vec![32767i16];
        apply(&mut samples, &mut state, 150); // 大于 100%，应该被限制到 100%
        assert_eq!(samples[0], 32767); // 100% 不限制
    }
}
