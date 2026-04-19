//! DSP (数字信号处理) 模块
//!
//! 提供音频处理链：增益 -> 噪声抑制 -> 高通滤波 -> 动态压缩

pub mod gain;
pub mod hpf;
pub mod compressor;
pub mod noise_suppress;

use crate::VoiceConfig;

/// DSP 处理链状态
pub struct DspChain {
    hpf_state: hpf::HpfState,
    compressor_state: compressor::CompressorState,
}

impl DspChain {
    pub fn new() -> Self {
        Self {
            hpf_state: hpf::HpfState::default(),
            compressor_state: compressor::CompressorState::default(),
        }
    }

    /// 处理一帧音频（960 samples = 20ms @ 48kHz）
    pub fn process(&mut self, samples: &mut [i16], config: &VoiceConfig) {
        // 1. 麦克风增益
        gain::apply(config.mic_volume as f32 / 100.0, samples);

        // 2. 噪声抑制
        if config.noise_suppress {
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
}
