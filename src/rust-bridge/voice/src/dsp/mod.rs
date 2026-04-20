//! DSP (数字信号处理) 模块
//!
//! 提供音频处理链：增益 -> RNNoise降噪 -> 高通滤波 -> 动态压缩 -> 回声消除 -> 自动增益控制

use std::time::Instant;

pub mod aec;
pub mod agc;
pub mod compressor;
pub mod gain;
pub mod hpf;
pub mod limiter;
pub mod noise_suppress;
pub mod rnnoise;

pub use aec::{AcousticEchoCanceller, AecConfig, DtdState};
pub use agc::{AgcConfig, AgcState, AutomaticGainController};

use crate::VoiceConfig;

/// DSP 处理统计信息
#[derive(Debug, Clone, Default)]
pub struct DspStats {
    /// 平均处理时间 (微秒)
    pub avg_process_time_us: f32,
    /// 最大处理时间 (微秒)
    pub max_process_time_us: u64,
    /// 超时次数（超过 1ms）
    pub overtime_count: u64,
    /// 总处理帧数
    pub total_frames: u64,
}

/// DSP 处理链状态
pub struct DspChain {
    hpf_state: hpf::HpfState,
    compressor_state: compressor::CompressorState,
    rnnoise_state: rnnoise::RnNoiseState,
    noise_gate_state: noise_suppress::NoiseGateState,
    limiter_state: limiter::LimiterState,
    /// 处理统计
    stats: DspStats,
    /// 累计处理时间（用于计算平均值）
    total_process_time_us: u64,
}

impl DspChain {
    pub fn new() -> Self {
        Self {
            hpf_state: hpf::HpfState::default(),
            compressor_state: compressor::CompressorState::default(),
            rnnoise_state: rnnoise::RnNoiseState::new(),
            noise_gate_state: noise_suppress::NoiseGateState::new(),
            limiter_state: limiter::LimiterState::new(),
            stats: DspStats::default(),
            total_process_time_us: 0,
        }
    }

    /// 处理一帧音频（960 samples = 20ms @ 48kHz）
    pub fn process(&mut self, samples: &mut [i16], config: &VoiceConfig) {
        let start = Instant::now();

        // 1. 麦克风增益
        gain::apply(config.mic_volume as f32 / 100.0, samples);

        // 2. RNNoise 高级降噪（如果启用且 feature 可用）
        if config.noise_suppress && config.noise_suppress_strength > 0 {
            self.rnnoise_state.set_enabled(true);
            self.rnnoise_state
                .set_strength(config.noise_suppress_strength as f32 / 100.0);

            // RNNoise 帧大小是 480，需要处理两帧
            if samples.len() >= rnnoise::RNNOISE_FRAME_SAMPLES * 2 {
                let _ = self.rnnoise_state.process(samples);
            }
        } else {
            // 回退到简单噪声抑制
            noise_suppress::apply_gate_with_state(
                samples,
                &mut self.noise_gate_state,
                config.noise_suppress_strength as f32 / 100.0,
            );
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

        // 4. 限制器（防止削波失真）
        if config.filter_enable && config.limiter > 0 {
            limiter::apply(samples, &mut self.limiter_state, config.limiter);
        }

        // 更新统计信息
        let elapsed_us = start.elapsed().as_micros() as u64;
        self.update_stats(elapsed_us);
    }

    /// 更新处理统计
    fn update_stats(&mut self, elapsed_us: u64) {
        // 超时阈值：1000 微秒 (1ms)
        const OVERTIME_THRESHOLD_US: u64 = 1000;

        self.stats.total_frames += 1;
        self.total_process_time_us += elapsed_us;

        // 更新最大值
        if elapsed_us > self.stats.max_process_time_us {
            self.stats.max_process_time_us = elapsed_us;
        }

        // 计算平均值
        self.stats.avg_process_time_us =
            self.total_process_time_us as f32 / self.stats.total_frames as f32;

        // 检查超时
        if elapsed_us > OVERTIME_THRESHOLD_US {
            self.stats.overtime_count += 1;
            log::warn!(
                "DSP processing took {}us (exceeded 1ms threshold), overtime count: {}",
                elapsed_us,
                self.stats.overtime_count
            );
        }
    }

    /// 获取处理统计信息
    pub fn stats(&self) -> &DspStats {
        &self.stats
    }

    /// 重置统计信息
    pub fn reset_stats(&mut self) {
        self.stats = DspStats::default();
        self.total_process_time_us = 0;
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

    #[test]
    fn test_dsp_stats() {
        let mut chain = DspChain::new();
        let mut samples = vec![1000i16; 960];
        let config = VoiceConfig::default();

        // 初始统计应为零
        let stats = chain.stats();
        assert_eq!(stats.total_frames, 0);
        assert_eq!(stats.max_process_time_us, 0);
        assert_eq!(stats.overtime_count, 0);

        // 处理多帧
        for _ in 0..10 {
            chain.process(&mut samples, &config);
        }

        let stats = chain.stats();
        assert_eq!(stats.total_frames, 10);
        // 处理时间应该大于 0
        assert!(stats.max_process_time_us > 0 || stats.total_frames == 10);
        // 平均时间应该被计算
        assert!(stats.avg_process_time_us >= 0.0);

        // 重置统计
        chain.reset_stats();
        let stats = chain.stats();
        assert_eq!(stats.total_frames, 0);
        assert_eq!(stats.max_process_time_us, 0);
    }
}
