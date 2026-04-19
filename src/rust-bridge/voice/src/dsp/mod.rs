//! DSP (数字信号处理) 模块
//!
//! 提供音频处理链：增益 -> RNNoise降噪 -> 高通滤波 -> 动态压缩

pub mod gain;
pub mod hpf;
pub mod compressor;
pub mod noise_suppress;
pub mod rnnoise;

use crate::VoiceConfig;

/// DSP 处理链状态
pub struct DspChain {
    hpf_state: hpf::HpfState,
    compressor_state: compressor::CompressorState,
    rnnoise_state: rnnoise::RnNoiseState,
}

impl DspChain {
    pub fn new() -> Self {
        Self {
            hpf_state: hpf::HpfState::default(),
            compressor_state: compressor::CompressorState::default(),
            rnnoise_state: rnnoise::RnNoiseState::new(),
        }
    }

    /// 处理一帧音频（960 samples = 20ms @ 48kHz）
    pub fn process(&mut self, samples: &mut [i16], config: &VoiceConfig) {
        // 1. 麦克风增益
        gain::apply(config.mic_volume as f32 / 100.0, samples);

        // 2. RNNoise 高级降噪（如果启用且 feature 可用）
        if config.noise_suppress && config.noise_suppress_strength > 0 {
            self.rnnoise_state.set_enabled(true);
            self.rnnoise_state.set_strength(config.noise_suppress_strength as f32 / 100.0);
            
            // RNNoise 帧大小是 480，需要处理两帧
            if samples.len() >= rnnoise::RNNOISE_FRAME_SAMPLES * 2 {
                let _ = self.rnnoise_state.process(samples);
            }
        } else {
            // 回退到简单噪声抑制
            noise_suppress::apply_gate(samples, config.noise_suppress_strength as f32 / 100.0);
        }

        // 3. 高通滤波 + 压缩
        hpf::apply(samples, &mut self.hpf_state);
        
        let comp_config = compressor::CompressorConfig {
            threshold: config.comp_threshold as f32 / 100.0,
            ratio: config.comp_ratio as f32 / 10.0,
            attack_ms: config.comp_attack_ms,
            release_ms: config.comp_release_ms,
            makeup_gain: config.comp_makeup as f32 / 100.0,
        };
        compressor::apply(samples, &mut self.compressor_state, &comp_config);
    }

    /// 仅使用 RNNoise 降噪（不经过其他处理）
    /// 
    /// 与原有 C++ 实现的 ApplyNoiseSuppressor 对应
    pub fn apply_rnnoise(&mut self, samples: &mut [i16], strength: f32) {
        if strength <= 0.0 {
            return;
        }
        
        self.rnnoise_state.set_enabled(true);
        self.rnnoise_state.set_strength(strength);
        
        let _ = self.rnnoise_state.process(samples);
    }
}

impl Default for DspChain {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dsp_chain() {
        let mut chain = DspChain::new();
        let mut samples = vec![1000i16; 960];
        let config = VoiceConfig::default();
        
        chain.process(&mut samples, &config);
        
        // 处理后的样本应该在有效范围内
        for &sample in &samples {
            assert!(sample >= -32768);
            assert!(sample <= 32767);
        }
    }

    #[test]
    fn test_dsp_chain_with_rnnoise() {
        let mut chain = DspChain::new();
        let mut samples = vec![1000i16; 960];
        let mut config = VoiceConfig::default();
        config.noise_suppress = true;
        config.noise_suppress_strength = 50;
        
        chain.process(&mut samples, &config);
        
        // 处理后的样本应该在有效范围内
        for &sample in &samples {
            assert!(sample >= -32768);
            assert!(sample <= 32767);
        }
    }
}
