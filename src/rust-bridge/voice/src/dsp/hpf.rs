//! 高通滤波器 (High Pass Filter)
//!
//! 使用二阶 IIR 滤波器，截止频率 120Hz，消除低频噪声和直流偏移

const SAMPLE_RATE: f32 = 48000.0;
const CUTOFF_HZ: f32 = 120.0;

/// 高通滤波器状态
#[derive(Debug, Clone, Copy)]
pub struct HpfState {
    prev_in: f32,
    prev_out: f32,
    alpha: f32,
}

impl HpfState {
    pub fn new() -> Self {
        let rc = 1.0 / (2.0 * std::f32::consts::PI * CUTOFF_HZ);
        let dt = 1.0 / SAMPLE_RATE;
        let alpha = rc / (rc + dt);

        Self {
            prev_in: 0.0,
            prev_out: 0.0,
            alpha,
        }
    }

    /// 重置状态
    pub fn reset(&mut self) {
        self.prev_in = 0.0;
        self.prev_out = 0.0;
    }
}

impl Default for HpfState {
    fn default() -> Self {
        Self::new()
    }
}

/// 应用高通滤波器
///
/// 使用一阶差分滤波器: y[n] = α * (y[n-1] + x[n] - x[n-1])
///
/// # 参数
/// * `samples` - 音频样本数组 (会被原地修改)
/// * `state` - 滤波器状态
pub fn apply(samples: &mut [i16], state: &mut HpfState) {
    for sample in samples.iter_mut() {
        let x = *sample as f32;
        let y = state.alpha * (state.prev_out + x - state.prev_in);

        state.prev_in = x;
        state.prev_out = y;

        *sample = y.clamp(-32768.0, 32767.0) as i16;
    }
}

/// 应用高通滤波器（带可调节截止频率）
pub fn apply_with_cutoff(samples: &mut [i16], state: &mut HpfState, cutoff_hz: f32) {
    let rc = 1.0 / (2.0 * std::f32::consts::PI * cutoff_hz.max(10.0));
    let dt = 1.0 / SAMPLE_RATE;
    let alpha = rc / (rc + dt);

    for sample in samples.iter_mut() {
        let x = *sample as f32;
        let y = alpha * (state.prev_out + x - state.prev_in);

        state.prev_in = x;
        state.prev_out = y;

        *sample = y.clamp(-32768.0, 32767.0) as i16;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hpf_removes_dc() {
        // 直流信号
        let mut samples = vec![1000i16; 960];
        let mut state = HpfState::default();

        // 处理多帧
        for _ in 0..10 {
            apply(&mut samples, &mut state);
        }

        // 直流分量应该被滤除
        let avg: f32 = samples.iter().map(|&s| s as f32).sum::<f32>() / samples.len() as f32;
        assert!(
            avg.abs() < 50.0,
            "DC component should be filtered out, got avg={}",
            avg
        );
    }

    #[test]
    fn test_hpf_preserves_high_freq() {
        // 1kHz 正弦波
        let mut samples: Vec<i16> = (0..960)
            .map(|i| {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * 1000.0 / SAMPLE_RATE;
                (phase.sin() * 16000.0) as i16
            })
            .collect();

        let original_rms = super::super::gain::frame_rms(&samples);

        let mut state = HpfState::default();
        apply(&mut samples, &mut state);

        let filtered_rms = super::super::gain::frame_rms(&samples);

        // 高频应该基本保留 (损失 < 10%)
        assert!(
            filtered_rms > original_rms * 0.9,
            "High frequency content should be preserved"
        );
    }
}
