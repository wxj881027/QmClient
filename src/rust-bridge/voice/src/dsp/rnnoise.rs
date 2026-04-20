//! RNNoise 高级降噪模块
//!
//! 使用 nnnoiseless (RNNoise 纯 Rust 移植)
//! 帧大小: 480 采样 (10ms @ 48kHz)
//!
//! 与原有 C++ 实现对应:
//! - CONF_RNNOISE 宏 -> rnnoise feature
//! - DenoiseState -> RnNoiseState
//! - rnnoise_process_frame -> process_frame

#[cfg(feature = "rnnoise")]
use nnnoiseless::DenoiseState;

/// RNNoise 帧大小（采样数）
/// 480 采样 = 10ms @ 48kHz
pub const RNNOISE_FRAME_SAMPLES: usize = 480;

/// RNNoise 降噪错误
#[derive(Debug, thiserror::Error)]
pub enum RnNoiseError {
    #[error("Invalid frame size: expected {expected}, got {actual}")]
    InvalidFrameSize { expected: usize, actual: usize },

    #[error("RNNoise not available (feature not enabled)")]
    NotAvailable,
}

/// RNNoise 降噪状态
#[cfg(feature = "rnnoise")]
pub struct RnNoiseState {
    state: Box<DenoiseState<'static>>,
    enabled: bool,
    strength: f32,
    first_frame: bool,
}

/// RNNoise 降噪状态（无功能版本）
#[cfg(not(feature = "rnnoise"))]
pub struct RnNoiseState {
    enabled: bool,
    strength: f32,
}

#[cfg(feature = "rnnoise")]
impl RnNoiseState {
    /// 创建新的降噪状态
    pub fn new() -> Self {
        Self {
            state: DenoiseState::new(),
            enabled: false,
            strength: 1.0,
            first_frame: true,
        }
    }

    /// 处理一帧音频
    ///
    /// # 参数
    /// * `input` - 输入 PCM 样本 (480 samples), i16 格式
    /// * `output` - 输出 PCM 样本 (480 samples), i16 格式
    ///
    /// # 说明
    /// 根据 nnnoiseless 文档，第一帧输出包含淡入伪影，建议丢弃
    pub fn process_frame(&mut self, input: &[i16], output: &mut [i16]) -> Result<(), RnNoiseError> {
        if input.len() != RNNOISE_FRAME_SAMPLES {
            return Err(RnNoiseError::InvalidFrameSize {
                expected: RNNOISE_FRAME_SAMPLES,
                actual: input.len(),
            });
        }
        if output.len() != RNNOISE_FRAME_SAMPLES {
            return Err(RnNoiseError::InvalidFrameSize {
                expected: RNNOISE_FRAME_SAMPLES,
                actual: output.len(),
            });
        }

        if !self.enabled || self.strength <= 0.0 {
            output.copy_from_slice(input);
            return Ok(());
        }

        // 转换 i16 -> f32 (nnnoiseless 期望 i16 范围的 f32)
        let mut input_f32 = [0.0f32; RNNOISE_FRAME_SAMPLES];
        for (i, &sample) in input.iter().enumerate() {
            input_f32[i] = sample as f32;
        }

        // 处理帧
        let mut output_f32 = [0.0f32; RNNOISE_FRAME_SAMPLES];
        self.state.process_frame(&mut output_f32, &input_f32);

        // 第一帧包含淡入伪影，使用输入代替
        if self.first_frame {
            self.first_frame = false;
            output.copy_from_slice(input);
            return Ok(());
        }

        // 转换 f32 -> i16
        for (i, &sample) in output_f32.iter().enumerate() {
            output[i] = sample.clamp(-32768.0, 32767.0) as i16;
        }

        Ok(())
    }

    /// 处理多帧音频
    ///
    /// # 参数
    /// * `samples` - PCM 样本，长度必须是 480 的倍数
    pub fn process(&mut self, samples: &mut [i16]) -> Result<(), RnNoiseError> {
        if !samples.len().is_multiple_of(RNNOISE_FRAME_SAMPLES) {
            return Err(RnNoiseError::InvalidFrameSize {
                expected: RNNOISE_FRAME_SAMPLES,
                actual: samples.len() % RNNOISE_FRAME_SAMPLES,
            });
        }

        if !self.enabled || self.strength <= 0.0 {
            return Ok(());
        }

        let frames = samples.len() / RNNOISE_FRAME_SAMPLES;

        for f in 0..frames {
            let base = f * RNNOISE_FRAME_SAMPLES;
            let input: Vec<i16> = samples[base..base + RNNOISE_FRAME_SAMPLES].to_vec();
            let mut output = [0i16; RNNOISE_FRAME_SAMPLES];

            self.process_frame(&input, &mut output)?;
            samples[base..base + RNNOISE_FRAME_SAMPLES].copy_from_slice(&output);
        }

        Ok(())
    }

    /// 启用/禁用降噪
    pub fn set_enabled(&mut self, enabled: bool) {
        if enabled && !self.enabled {
            // 重新启用时重置第一帧标志
            self.first_frame = true;
        }
        self.enabled = enabled;
    }

    /// 设置降噪强度 (0.0-1.0)
    pub fn set_strength(&mut self, strength: f32) {
        self.strength = strength.clamp(0.0, 1.0);
    }

    /// 是否启用
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    /// 获取降噪强度
    pub fn strength(&self) -> f32 {
        self.strength
    }

    /// 获取帧大小
    pub fn frame_size(&self) -> usize {
        RNNOISE_FRAME_SAMPLES
    }
}

#[cfg(feature = "rnnoise")]
impl Default for RnNoiseState {
    fn default() -> Self {
        Self::new()
    }
}

// 无功能版本的实现
#[cfg(not(feature = "rnnoise"))]
impl RnNoiseState {
    pub fn new() -> Self {
        Self {
            enabled: false,
            strength: 1.0,
        }
    }

    pub fn process_frame(&mut self, input: &[i16], output: &mut [i16]) -> Result<(), RnNoiseError> {
        if input.len() != RNNOISE_FRAME_SAMPLES || output.len() != RNNOISE_FRAME_SAMPLES {
            return Err(RnNoiseError::InvalidFrameSize {
                expected: RNNOISE_FRAME_SAMPLES,
                actual: input.len().min(output.len()),
            });
        }
        output.copy_from_slice(input);
        Ok(())
    }

    pub fn process(&mut self, samples: &mut [i16]) -> Result<(), RnNoiseError> {
        Ok(())
    }

    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }

    pub fn set_strength(&mut self, strength: f32) {
        self.strength = strength.clamp(0.0, 1.0);
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn strength(&self) -> f32 {
        self.strength
    }

    pub fn frame_size(&self) -> usize {
        RNNOISE_FRAME_SAMPLES
    }
}

#[cfg(not(feature = "rnnoise"))]
impl Default for RnNoiseState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rnnoise_creation() {
        let state = RnNoiseState::new();
        assert!(!state.is_enabled());
        assert_eq!(state.strength(), 1.0);
        assert_eq!(state.frame_size(), RNNOISE_FRAME_SAMPLES);
    }

    #[test]
    fn test_rnnoise_enable_disable() {
        let mut state = RnNoiseState::new();

        state.set_enabled(true);
        assert!(state.is_enabled());

        state.set_enabled(false);
        assert!(!state.is_enabled());
    }

    #[test]
    fn test_rnnoise_strength() {
        let mut state = RnNoiseState::new();

        state.set_strength(0.5);
        assert!((state.strength() - 0.5).abs() < 0.001);

        state.set_strength(2.0);
        assert!((state.strength() - 1.0).abs() < 0.001);

        state.set_strength(-1.0);
        assert!((state.strength() - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_rnnoise_process_frame_disabled() {
        let mut state = RnNoiseState::new();
        state.set_enabled(false);

        let input = [1000i16; RNNOISE_FRAME_SAMPLES];
        let mut output = [0i16; RNNOISE_FRAME_SAMPLES];

        state.process_frame(&input, &mut output).unwrap();
        assert_eq!(output, input);
    }

    #[test]
    fn test_rnnoise_process_frame_invalid_size() {
        let mut state = RnNoiseState::new();
        state.set_enabled(true);

        let input = [0i16; 100];
        let mut output = [0i16; 100];

        let result = state.process_frame(&input, &mut output);
        assert!(result.is_err());
    }

    #[cfg(feature = "rnnoise")]
    #[test]
    fn test_rnnoise_process_frame_enabled() {
        let mut state = RnNoiseState::new();
        state.set_enabled(true);

        // 生成静音信号
        let input = [0i16; RNNOISE_FRAME_SAMPLES];
        let mut output = [0i16; RNNOISE_FRAME_SAMPLES];

        // 第一帧会被丢弃（淡入伪影）
        state.process_frame(&input, &mut output).unwrap();

        // 第二帧才是真正的降噪输出
        state.process_frame(&input, &mut output).unwrap();
        println!("Output for silence: {:?}", &output[..10]);
    }

    #[cfg(feature = "rnnoise")]
    #[test]
    fn test_rnnoise_process_sine_wave() {
        let mut state = RnNoiseState::new();
        state.set_enabled(true);

        // 生成 440Hz 正弦波 (48kHz 采样率)
        let input: Vec<i16> = (0..RNNOISE_FRAME_SAMPLES)
            .map(|i| {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
                (phase.sin() * 16000.0) as i16
            })
            .collect();

        let mut output = [0i16; RNNOISE_FRAME_SAMPLES];

        // 处理两帧（第一帧会被丢弃）
        state.process_frame(&input, &mut output).unwrap();
        state.process_frame(&input, &mut output).unwrap();

        println!("Input first 10: {:?}", &input[..10]);
        println!("Output first 10: {:?}", &output[..10]);
    }
}
