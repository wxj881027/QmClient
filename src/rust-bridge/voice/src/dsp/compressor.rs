//! 动态范围压缩器 (Dynamic Range Compressor)
//!
//! 参数：
//! - Threshold: 压缩起始阈值 (0.0 - 1.0)
//! - Ratio: 压缩比率 (例如 2.5:1)
//! - Attack: 压缩启动时间 (ms)
//! - Release: 压缩释放时间 (ms)
//! - Makeup Gain: 压缩后补偿增益

const SAMPLE_RATE: f32 = 48000.0;

/// 压缩器配置
#[derive(Debug, Clone, Copy)]
pub struct CompressorConfig {
    /// 压缩起始阈值 (0.0 - 1.0)
    pub threshold: f32,
    /// 压缩比率
    pub ratio: f32,
    /// 攻击时间 (ms)
    pub attack_ms: i32,
    /// 释放时间 (ms)
    pub release_ms: i32,
    /// 补偿增益
    pub makeup_gain: f32,
}

impl Default for CompressorConfig {
    fn default() -> Self {
        Self {
            threshold: 0.2,
            ratio: 2.5,
            attack_ms: 20,
            release_ms: 200,
            makeup_gain: 1.6,
        }
    }
}

/// 压缩器状态
#[derive(Debug, Clone, Copy)]
pub struct CompressorState {
    envelope: f32,
}

impl CompressorState {
    pub fn new() -> Self {
        Self { envelope: 0.0 }
    }

    pub fn reset(&mut self) {
        self.envelope = 0.0;
    }
}

impl Default for CompressorState {
    fn default() -> Self {
        Self::new()
    }
}

/// 应用动态压缩
///
/// 压缩公式: if envelope > threshold, gain = (threshold + (envelope - threshold) / ratio) / envelope
///
/// # 参数
/// * `samples` - 音频样本数组 (会被原地修改)
/// * `state` - 压缩器状态
/// * `config` - 压缩器配置
pub fn apply(samples: &mut [i16], state: &mut CompressorState, config: &CompressorConfig) {
    if samples.is_empty() {
        return;
    }

    // 计算攻击/释放系数
    let attack_coef = (-1.0 / (config.attack_ms as f32 / 1000.0 * SAMPLE_RATE)).exp();
    let release_coef = (-1.0 / (config.release_ms as f32 / 1000.0 * SAMPLE_RATE)).exp();

    for sample in samples.iter_mut() {
        let input = sample.abs() as f32 / 32768.0;

        // 包络跟随 (peak detector)
        let coef = if input > state.envelope {
            attack_coef
        } else {
            release_coef
        };
        state.envelope = state.envelope + (1.0 - coef) * (input - state.envelope);

        // 计算压缩增益
        let gain = if state.envelope > config.threshold {
            let compressed_level =
                config.threshold + (state.envelope - config.threshold) / config.ratio;
            compressed_level / state.envelope
        } else {
            1.0
        };

        // 应用增益和补偿
        let output = (*sample as f32) * gain * config.makeup_gain;
        *sample = output.clamp(-32768.0, 32767.0) as i16;
    }
}

/// 应用限制器 (硬削波到指定阈值)
pub fn apply_limiter(samples: &mut [i16], limit_threshold: f32) {
    let threshold = (limit_threshold.clamp(0.0, 1.0) * 32767.0) as i16;

    for sample in samples.iter_mut() {
        *sample = (*sample).clamp(-threshold, threshold);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compressor_limits_peak() {
        // 高电平信号
        let mut samples = vec![30000i16; 960];
        let mut state = CompressorState::default();
        let config = CompressorConfig {
            threshold: 0.5, // 50%
            ratio: 4.0,
            attack_ms: 1,     // 使用快速攻击时间 (1ms)，以便在单帧内建立压缩
            makeup_gain: 1.0, // 不使用补偿增益，以便测试压缩效果
            ..Default::default()
        };

        apply(&mut samples, &mut state, &config);

        // 压缩后，后半部分样本的峰值应该降低（包络已建立）
        // 检查最后 100 个样本的平均值
        let tail_avg: f32 = samples
            .iter()
            .rev()
            .take(100)
            .map(|&s| s.abs() as f32)
            .sum::<f32>()
            / 100.0;
        assert!(
            tail_avg < 30000.0,
            "Tail average should be reduced after compression"
        );
    }

    #[test]
    fn test_compressor_preserves_low_level() {
        // 低电平信号
        let mut samples = vec![1000i16; 960];
        let mut state = CompressorState::default();
        let config = CompressorConfig::default();

        apply(&mut samples, &mut state, &config);

        // 低电平信号应该被补偿增益放大
        let avg = samples.iter().map(|&s| s.abs() as f32).sum::<f32>() / samples.len() as f32;
        assert!(
            avg > 1000.0,
            "Low level signals should be amplified by makeup gain"
        );
    }

    #[test]
    fn test_limiter() {
        let mut samples = vec![30000i16, -32000, 1000, -500];

        apply_limiter(&mut samples, 0.5); // 50% 限制

        assert!(samples[0].abs() <= 16384);
        assert!(samples[1].abs() <= 16384);
        assert_eq!(samples[2], 1000); // 低于阈值，不变
        assert_eq!(samples[3], -500);
    }
}
