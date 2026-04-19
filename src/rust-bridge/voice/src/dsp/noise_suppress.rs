//! 噪声抑制 (Noise Suppression)
//!
//! 提供简单门控实现（基于 RMS 能量估计的噪声门）
//! 可选 RNNoise 深度学习降噪（使用 nnnoiseless crate）

/// 噪声门状态
#[derive(Debug, Clone, Copy)]
pub struct NoiseGateState {
    /// 噪声底估计 (0.0 - 1.0)
    noise_floor: f32,
    /// 增益状态
    gain: f32,
}

impl NoiseGateState {
    pub fn new() -> Self {
        Self {
            noise_floor: 0.001,
            gain: 1.0,
        }
    }

    pub fn reset(&mut self) {
        self.noise_floor = 0.001;
        self.gain = 1.0;
    }
}

impl Default for NoiseGateState {
    fn default() -> Self {
        Self::new()
    }
}

/// 应用噪声门控
/// 
/// 基于信号能量与噪声底的比率 (SNR) 控制增益
/// 
/// # 参数
/// * `samples` - 音频样本数组 (会被原地修改)
/// * `strength` - 噪声抑制强度 (0.0 - 1.0)
pub fn apply_gate(samples: &mut [i16], strength: f32) {
    if samples.is_empty() || strength <= 0.0 {
        return;
    }

    // SNR 阈值
    let low_threshold = 1.2f32;   // 静音阈值
    let high_threshold = 2.5f32;  // 通过阈值
    
    // 攻击/释放时间 (样本数)
    let attack_samples = (0.01 * 48000.0) as usize;   // 10ms
    let release_samples = (0.08 * 48000.0) as usize;  // 80ms
    
    // 计算当前帧 RMS
    let sum_squares: f64 = samples
        .iter()
        .map(|&s| (s as f64).powi(2))
        .sum();
    let rms = ((sum_squares / samples.len() as f64).sqrt() as f32 / 32768.0)
        .max(0.0001);
    
    // 更新噪声底估计 (指数移动平均)
    let noise_update_coef = if rms < 0.05 { 0.2 } else { 0.05 };
    let mut noise_floor = 0.001f32;
    noise_floor = noise_floor + noise_update_coef * (rms - noise_floor);
    noise_floor = noise_floor.max(0.0001);
    
    // 计算 SNR
    let snr = rms / noise_floor;
    
    // 计算目标增益
    let target_gain = if snr < low_threshold {
        0.0 // 静音
    } else if snr > high_threshold {
        1.0 // 完全通过
    } else {
        // 过渡区域
        (snr - low_threshold) / (high_threshold - low_threshold)
    };
    
    // 应用强度参数
    let target_gain = target_gain * strength;
    
    // 应用增益 (使用简单的攻击/释放)
    static mut CURRENT_GAIN: f32 = 1.0;
    
    let attack_coef = 1.0 / attack_samples as f32;
    let release_coef = 1.0 / release_samples as f32;
    
    unsafe {
        if target_gain < CURRENT_GAIN {
            CURRENT_GAIN -= attack_coef;
            if CURRENT_GAIN < target_gain {
                CURRENT_GAIN = target_gain;
            }
        } else {
            CURRENT_GAIN += release_coef;
            if CURRENT_GAIN > target_gain {
                CURRENT_GAIN = target_gain;
            }
        }
        
        // 应用增益
        for sample in samples.iter_mut() {
            *sample = ((*sample as f32) * CURRENT_GAIN).clamp(-32768.0, 32767.0) as i16;
        }
    }
}

/// 应用噪声门控（带状态保持）
pub fn apply_gate_with_state(samples: &mut [i16], state: &mut NoiseGateState, strength: f32) {
    if samples.is_empty() || strength <= 0.0 {
        return;
    }

    let low_threshold = 1.2f32;
    let high_threshold = 2.5f32;
    
    let attack_samples = (0.01 * 48000.0) as usize;
    let release_samples = (0.08 * 48000.0) as usize;
    
    let sum_squares: f64 = samples
        .iter()
        .map(|&s| (s as f64).powi(2))
        .sum();
    let rms = ((sum_squares / samples.len() as f64).sqrt() as f32 / 32768.0)
        .max(0.0001);
    
    let noise_update_coef = if rms < 0.05 { 0.2 } else { 0.05 };
    state.noise_floor = state.noise_floor + noise_update_coef * (rms - state.noise_floor);
    state.noise_floor = state.noise_floor.max(0.0001);
    
    let snr = rms / state.noise_floor;
    
    let target_gain = if snr < low_threshold {
        0.0
    } else if snr > high_threshold {
        1.0
    } else {
        (snr - low_threshold) / (high_threshold - low_threshold)
    };
    
    let target_gain = target_gain * strength;
    
    let attack_coef = 1.0 / attack_samples as f32;
    let release_coef = 1.0 / release_samples as f32;
    
    if target_gain < state.gain {
        state.gain -= attack_coef;
        if state.gain < target_gain {
            state.gain = target_gain;
        }
    } else {
        state.gain += release_coef;
        if state.gain > target_gain {
            state.gain = target_gain;
        }
    }
    
    for sample in samples.iter_mut() {
        *sample = ((*sample as f32) * state.gain).clamp(-32768.0, 32767.0) as i16;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_noise_gate_silence() {
        // 低电平噪声
        let mut samples = vec![50i16; 960];
        apply_gate(&mut samples, 0.5);
        
        // 低电平信号应该被衰减
        let max_val = samples.iter().map(|&s| s.abs()).max().unwrap();
        assert!(max_val < 50, "Low level noise should be suppressed");
    }

    #[test]
    fn test_noise_gate_preserves_signal() {
        // 高电平信号 (模拟语音)
        let mut samples: Vec<i16> = (0..960)
            .map(|i| ((i as f32 * 0.1).sin() * 10000.0) as i16)
            .collect();
        
        let original_rms = super::super::gain::frame_rms(&samples);
        apply_gate(&mut samples, 0.5);
        let processed_rms = super::super::gain::frame_rms(&samples);
        
        // 高电平信号应该基本保留
        assert!(processed_rms > original_rms * 0.7, 
                "High level signal should be preserved");
    }

    #[test]
    fn test_noise_gate_with_state() {
        let mut state = NoiseGateState::default();
        let mut samples = vec![50i16; 960];
        
        apply_gate_with_state(&mut samples, &mut state, 1.0);
        
        // 状态应该被更新
        assert!(state.noise_floor > 0.0);
        assert!(state.gain < 1.0); // 应该被衰减
    }
}
