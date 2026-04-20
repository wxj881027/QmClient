//! 麦克风增益处理
//!
//! 支持 0-300% 增益，超过 100% 时进行硬削波

/// 应用增益到音频样本
///
/// # 参数
/// * `gain` - 增益倍数 (0.0 - 3.0)
/// * `samples` - 音频样本数组 (会被原地修改)
///
/// # 示例
/// ```
/// use ddnet_voice::dsp::gain;
/// let mut samples = vec![1000i16; 960];
/// gain::apply(2.0, &mut samples); // 200% 增益
/// ```
pub fn apply(gain: f32, samples: &mut [i16]) {
    let gain = gain.clamp(0.0, 3.0);

    for sample in samples.iter_mut() {
        let amplified = *sample as f32 * gain;
        *sample = amplified.clamp(-32768.0, 32767.0) as i16;
    }
}

/// 计算音频帧的峰值电平 (0.0 - 1.0)
pub fn frame_peak(samples: &[i16]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }

    // 使用 i32 避免 i16::MIN.abs() 溢出问题
    let max_sample = samples.iter().map(|&s| (s as i32).abs()).max().unwrap_or(0) as f32;

    max_sample / 32768.0
}

/// 计算音频帧的 RMS 能量 (0.0 - 1.0)
pub fn frame_rms(samples: &[i16]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }

    let sum_squares: f64 = samples.iter().map(|&s| (s as f64).powi(2)).sum();

    let rms = (sum_squares / samples.len() as f64).sqrt() as f32;
    rms / 32768.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gain_apply() {
        let mut samples = vec![1000i16; 960];
        apply(2.0, &mut samples);

        // 增益 200% 应该使值翻倍
        for sample in samples {
            assert_eq!(sample, 2000);
        }
    }

    #[test]
    fn test_gain_clamp() {
        let mut samples = vec![20000i16; 960];
        apply(3.0, &mut samples); // 300% 增益

        // 应该被限制在 i16 范围内
        for sample in samples {
            assert!(sample <= 32767);
            assert!(sample >= -32768);
        }
    }

    #[test]
    fn test_gain_zero() {
        let mut samples = vec![1000i16; 960];
        apply(0.0, &mut samples);

        // 0% 增益应该全部归零
        for sample in samples {
            assert_eq!(sample, 0);
        }
    }

    #[test]
    fn test_frame_peak() {
        let samples = vec![0i16, 16384, -32768, 1000];
        let peak = frame_peak(&samples);
        assert_eq!(peak, 1.0); // 最大绝对值是 32768
    }

    #[test]
    fn test_frame_rms() {
        let samples = vec![32767i16; 100];
        let rms = frame_rms(&samples);
        assert!(rms > 0.99 && rms <= 1.0);
    }
}
