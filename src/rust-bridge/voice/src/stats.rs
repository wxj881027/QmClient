//! 高级统计与遥测模块
//!
//! 提供详细的网络质量指标、MOS 评分、实时音频质量监控
//! 支持结构化日志导出和性能分析

use std::collections::VecDeque;
use std::fmt;
use std::time::Instant;

/// 音频质量指标
#[derive(Debug, Clone, Copy, Default)]
pub struct AudioQualityMetrics {
    /// 当前 MOS 评分 (1.0-5.0)
    pub mos_score: f32,
    /// R 因子 (0-100，用于计算 MOS)
    pub r_factor: f32,
    /// 信号强度 (dB)
    pub signal_db: f32,
    /// 噪声水平 (dB)
    pub noise_db: f32,
    /// 信噪比 (dB)
    pub snr_db: f32,
    /// 音频失真率 (%)
    pub distortion_percent: f32,
    /// 活跃语音比例 (%)
    pub speech_activity_percent: f32,
}

/// 网络质量指标
#[derive(Debug, Clone, Copy, Default)]
pub struct NetworkQualityMetrics {
    /// 平均延迟 (ms)
    pub latency_ms: f32,
    /// 抖动 (ms，标准差)
    pub jitter_ms: f32,
    /// 丢包率 (%)
    pub packet_loss_percent: f32,
    /// 突发丢包率 (%)
    pub burst_loss_percent: f32,
    /// 乱序包比例 (%)
    pub out_of_order_percent: f32,
    /// 平均包大小 (bytes)
    pub avg_packet_size: f32,
    /// 带宽使用 (kbps)
    pub bandwidth_kbps: f32,
}

/// 编解码器统计
#[derive(Debug, Clone, Copy, Default)]
pub struct CodecMetrics {
    /// 当前比特率 (bps)
    pub bitrate: i32,
    /// 编码器类型 (0=Opus)
    pub codec_type: u8,
    /// 是否启用 FEC
    pub fec_enabled: bool,
    /// 每帧平均编码字节数
    pub avg_encoded_bytes: f32,
    /// 编码时间 (μs)
    pub encode_time_us: f32,
    /// 解码时间 (μs)
    pub decode_time_us: f32,
}

/// 抖动缓冲区统计
#[derive(Debug, Clone, Copy, Default)]
pub struct JitterBufferMetrics {
    /// 当前缓冲区大小 (ms)
    pub buffer_size_ms: f32,
    /// 目标缓冲区大小 (ms)
    pub target_size_ms: f32,
    /// 欠载事件次数
    pub underrun_count: u32,
    /// 过载事件次数
    pub overrun_count: u32,
    /// 丢包隐藏次数
    pub plc_count: u32,
    /// 最大连续丢包数
    pub max_consecutive_loss: u32,
}

/// AEC/AGC 统计
#[derive(Debug, Clone, Copy, Default)]
pub struct DspMetrics {
    /// AEC 回波返回损失 (ERL, dB)
    pub aec_erl_db: f32,
    /// AEC 收敛状态
    pub aec_converged: bool,
    /// AEC 双端通话状态
    pub aec_dtd_state: u8,
    /// AGC 当前增益 (dB)
    pub agc_gain_db: f32,
    /// AGC 状态
    pub agc_state: u8,
}

/// 综合语音统计报告
#[derive(Debug, Clone)]
pub struct VoiceStatsReport {
    /// 报告时间戳
    pub timestamp: Instant,
    /// 报告周期 (s)
    pub period_seconds: f32,
    /// 音频质量
    pub audio: AudioQualityMetrics,
    /// 网络质量
    pub network: NetworkQualityMetrics,
    /// 编解码器统计
    pub codec: CodecMetrics,
    /// 抖动缓冲区
    pub jitter: JitterBufferMetrics,
    /// DSP 处理
    pub dsp: DspMetrics,
}

impl VoiceStatsReport {
    /// 创建新的空报告
    pub fn new() -> Self {
        Self {
            timestamp: Instant::now(),
            period_seconds: 0.0,
            audio: AudioQualityMetrics::default(),
            network: NetworkQualityMetrics::default(),
            codec: CodecMetrics::default(),
            jitter: JitterBufferMetrics::default(),
            dsp: DspMetrics::default(),
        }
    }

    /// 导出为 JSON 格式
    pub fn to_json(&self) -> String {
        format!(
            r#"{{
  "timestamp": {:?},
  "period_seconds": {:.2},
  "audio": {{
    "mos_score": {:.2},
    "r_factor": {:.1},
    "signal_db": {:.1},
    "noise_db": {:.1},
    "snr_db": {:.1},
    "distortion_percent": {:.2},
    "speech_activity_percent": {:.1}
  }},
  "network": {{
    "latency_ms": {:.1},
    "jitter_ms": {:.1},
    "packet_loss_percent": {:.2},
    "burst_loss_percent": {:.2},
    "out_of_order_percent": {:.2},
    "avg_packet_size": {:.1},
    "bandwidth_kbps": {:.1}
  }},
  "codec": {{
    "bitrate": {},
    "codec_type": {},
    "fec_enabled": {},
    "avg_encoded_bytes": {:.1},
    "encode_time_us": {:.1},
    "decode_time_us": {:.1}
  }},
  "jitter": {{
    "buffer_size_ms": {:.1},
    "target_size_ms": {:.1},
    "underrun_count": {},
    "overrun_count": {},
    "plc_count": {},
    "max_consecutive_loss": {}
  }},
  "dsp": {{
    "aec_erl_db": {:.1},
    "aec_converged": {},
    "aec_dtd_state": {},
    "agc_gain_db": {:.1},
    "agc_state": {}
  }}
}}"#,
            self.timestamp.elapsed().as_secs(),
            self.period_seconds,
            self.audio.mos_score,
            self.audio.r_factor,
            self.audio.signal_db,
            self.audio.noise_db,
            self.audio.snr_db,
            self.audio.distortion_percent,
            self.audio.speech_activity_percent,
            self.network.latency_ms,
            self.network.jitter_ms,
            self.network.packet_loss_percent,
            self.network.burst_loss_percent,
            self.network.out_of_order_percent,
            self.network.avg_packet_size,
            self.network.bandwidth_kbps,
            self.codec.bitrate,
            self.codec.codec_type,
            self.codec.fec_enabled,
            self.codec.avg_encoded_bytes,
            self.codec.encode_time_us,
            self.codec.decode_time_us,
            self.jitter.buffer_size_ms,
            self.jitter.target_size_ms,
            self.jitter.underrun_count,
            self.jitter.overrun_count,
            self.jitter.plc_count,
            self.jitter.max_consecutive_loss,
            self.dsp.aec_erl_db,
            self.dsp.aec_converged,
            self.dsp.aec_dtd_state,
            self.dsp.agc_gain_db,
            self.dsp.agc_state,
        )
    }

    /// 获取质量评级
    pub fn quality_rating(&self) -> QualityRating {
        if self.audio.mos_score >= 4.0 {
            QualityRating::Excellent
        } else if self.audio.mos_score >= 3.5 {
            QualityRating::Good
        } else if self.audio.mos_score >= 3.0 {
            QualityRating::Fair
        } else if self.audio.mos_score >= 2.0 {
            QualityRating::Poor
        } else {
            QualityRating::Bad
        }
    }
}

impl Default for VoiceStatsReport {
    fn default() -> Self {
        Self::new()
    }
}

/// 质量评级
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QualityRating {
    Excellent,
    Good,
    Fair,
    Poor,
    Bad,
}

impl fmt::Display for QualityRating {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            QualityRating::Excellent => write!(f, "Excellent"),
            QualityRating::Good => write!(f, "Good"),
            QualityRating::Fair => write!(f, "Fair"),
            QualityRating::Poor => write!(f, "Poor"),
            QualityRating::Bad => write!(f, "Bad"),
        }
    }
}

/// 延迟样本历史（用于抖动计算）
#[derive(Debug)]
#[allow(dead_code)]
struct LatencySample {
    latency_ms: f32,
    /// 时间戳（保留供未来使用）
    timestamp: Instant,
}

/// 统计收集器
pub struct StatsCollector {
    /// 配置
    window_size: usize,
    /// 延迟样本历史
    latency_history: VecDeque<LatencySample>,
    /// 丢包历史 (1=丢失, 0=成功)
    loss_history: VecDeque<u8>,
    /// 包大小历史
    size_history: VecDeque<usize>,
    /// 最后报告时间
    last_report: Instant,
    /// 总发送包数
    total_packets_sent: u64,
    /// 总接收包数
    total_packets_received: u64,
    /// 总丢包数
    total_packets_lost: u64,
    /// 字节统计
    total_bytes_sent: u64,
    total_bytes_received: u64,
    /// 信号能量累加
    signal_energy_sum: f64,
    /// 噪声能量累加
    noise_energy_sum: f64,
    /// 样本计数
    sample_count: u64,
    /// 活跃语音样本数
    active_speech_samples: u64,
}

impl StatsCollector {
    /// 创建新的统计收集器
    pub fn new() -> Self {
        Self::with_window_size(1000)
    }

    /// 使用指定窗口大小创建
    pub fn with_window_size(window_size: usize) -> Self {
        Self {
            window_size,
            latency_history: VecDeque::with_capacity(window_size),
            loss_history: VecDeque::with_capacity(window_size),
            size_history: VecDeque::with_capacity(window_size),
            last_report: Instant::now(),
            total_packets_sent: 0,
            total_packets_received: 0,
            total_packets_lost: 0,
            total_bytes_sent: 0,
            total_bytes_received: 0,
            signal_energy_sum: 0.0,
            noise_energy_sum: 0.0,
            sample_count: 0,
            active_speech_samples: 0,
        }
    }

    /// 记录延迟样本
    pub fn record_latency(&mut self, latency_ms: f32) {
        self.latency_history.push_back(LatencySample {
            latency_ms,
            timestamp: Instant::now(),
        });

        while self.latency_history.len() > self.window_size {
            self.latency_history.pop_front();
        }
    }

    /// 记录丢包事件
    pub fn record_packet_loss(&mut self, lost: bool) {
        self.loss_history.push_back(if lost { 1 } else { 0 });

        if lost {
            self.total_packets_lost += 1;
        }

        while self.loss_history.len() > self.window_size {
            self.loss_history.pop_front();
        }
    }

    /// 记录接收到的包
    pub fn record_packet_received(&mut self, size_bytes: usize) {
        self.total_packets_received += 1;
        self.total_bytes_received += size_bytes as u64;

        self.size_history.push_back(size_bytes);
        while self.size_history.len() > self.window_size {
            self.size_history.pop_front();
        }
    }

    /// 记录发送的包
    pub fn record_packet_sent(&mut self, size_bytes: usize) {
        self.total_packets_sent += 1;
        self.total_bytes_sent += size_bytes as u64;
    }

    /// 记录音频能量
    pub fn record_audio_energy(
        &mut self,
        signal_energy: f64,
        noise_energy: f64,
        is_active_speech: bool,
    ) {
        self.signal_energy_sum += signal_energy;
        self.noise_energy_sum += noise_energy;
        self.sample_count += 1;

        if is_active_speech {
            self.active_speech_samples += 1;
        }
    }

    /// 计算 MOS 评分 (基于 E-Model)
    ///
    /// E-Model 公式: R = Ro - Is - Id - Ie + A
    /// MOS = 1 + 0.035R + 7e-6 * R * (R-60) * (100-R)  (R < 100)
    /// MOS = 4.5  (R >= 100)
    pub fn calculate_mos(&self, codec_impairment: f32, equipment_impairment: f32) -> f32 {
        let r_factor = self.calculate_r_factor(codec_impairment, equipment_impairment);
        Self::r_factor_to_mos(r_factor)
    }

    /// 计算 R 因子
    fn calculate_r_factor(&self, codec_impairment: f32, equipment_impairment: f32) -> f32 {
        // Ro: 基本信噪比 (假设良好环境)
        let ro = 93.2;

        // Is: 同时损伤因子 (简化计算)
        let is_factor = 0.0;

        // Id: 延迟损伤因子
        let avg_latency = self.average_latency();
        let id = Self::delay_impairment(avg_latency);

        // Ie: 设备/编解码器损伤
        let ie = codec_impairment + equipment_impairment;

        // A: 优势因子 (假设为 0)
        let a = 0.0;

        let r = ro - is_factor - id - ie + a;

        // 考虑丢包影响
        let loss_rate = self.packet_loss_rate();
        let r_with_loss = r * (1.0 - loss_rate * 2.5); // 丢包对质量影响较大

        r_with_loss.clamp(0.0, 100.0)
    }

    /// 延迟损伤因子
    fn delay_impairment(delay_ms: f32) -> f32 {
        if delay_ms < 100.0 {
            // 低于 100ms 延迟影响很小
            delay_ms * 0.01
        } else if delay_ms < 300.0 {
            // 100-300ms 中等影响
            1.0 + (delay_ms - 100.0) * 0.05
        } else {
            // 超过 300ms 严重影响
            11.0 + (delay_ms - 300.0) * 0.1
        }
    }

    /// R 因子转 MOS
    fn r_factor_to_mos(r: f32) -> f32 {
        if r < 0.0 {
            1.0
        } else if r > 100.0 {
            4.5
        } else {
            let mos = 1.0 + 0.035 * r + 7e-6 * r * (r - 60.0) * (100.0 - r);
            mos.clamp(1.0, 4.5)
        }
    }

    /// 计算平均延迟
    pub fn average_latency(&self) -> f32 {
        if self.latency_history.is_empty() {
            return 0.0;
        }

        let sum: f32 = self.latency_history.iter().map(|s| s.latency_ms).sum();
        sum / self.latency_history.len() as f32
    }

    /// 计算抖动 (标准差)
    pub fn jitter(&self) -> f32 {
        if self.latency_history.len() < 2 {
            return 0.0;
        }

        let avg = self.average_latency();
        let variance: f32 = self
            .latency_history
            .iter()
            .map(|s| (s.latency_ms - avg).powi(2))
            .sum::<f32>()
            / self.latency_history.len() as f32;

        variance.sqrt()
    }

    /// 计算丢包率
    pub fn packet_loss_rate(&self) -> f32 {
        if self.loss_history.is_empty() {
            return 0.0;
        }

        let lost: u32 = self.loss_history.iter().map(|&x| x as u32).sum();
        lost as f32 / self.loss_history.len() as f32
    }

    /// 计算突发丢包率
    pub fn burst_loss_rate(&self) -> f32 {
        if self.loss_history.len() < 3 {
            return 0.0;
        }

        let history: Vec<_> = self.loss_history.iter().copied().collect();
        let mut burst_losses = 0;
        let mut in_burst = false;

        for i in 1..history.len() {
            if history[i] == 1 && history[i - 1] == 1 {
                if !in_burst {
                    burst_losses += 2;
                    in_burst = true;
                } else {
                    burst_losses += 1;
                }
            } else {
                in_burst = false;
            }
        }

        burst_losses as f32 / self.loss_history.len() as f32
    }

    /// 生成统计报告
    pub fn generate_report(&self) -> VoiceStatsReport {
        let now = Instant::now();
        let period = now.duration_since(self.last_report).as_secs_f32();

        // 计算音频指标
        let avg_signal_energy = if self.sample_count > 0 {
            self.signal_energy_sum / self.sample_count as f64
        } else {
            0.0
        };

        let avg_noise_energy = if self.sample_count > 0 {
            self.noise_energy_sum / self.sample_count as f64
        } else {
            1.0 // 避免除零
        };

        let signal_db = 10.0 * (avg_signal_energy.max(1.0) as f32).log10() - 90.0; // 转换为 dB
        let noise_db = 10.0 * (avg_noise_energy.max(1.0) as f32).log10() - 90.0;
        let snr_db = signal_db - noise_db;

        let speech_activity = if self.sample_count > 0 {
            self.active_speech_samples as f32 / self.sample_count as f32 * 100.0
        } else {
            0.0
        };

        // 计算 MOS (使用典型值)
        let codec_ie = 5.0; // Opus VoIP 典型值
        let equipment_ie = 2.0;
        let mos = self.calculate_mos(codec_ie, equipment_ie);
        let r_factor = self.calculate_r_factor(codec_ie, equipment_ie);

        // 计算平均包大小
        let avg_packet_size = if !self.size_history.is_empty() {
            self.size_history.iter().sum::<usize>() as f32 / self.size_history.len() as f32
        } else {
            0.0
        };

        // 计算带宽 (kbps)
        let bandwidth_kbps = if period > 0.0 {
            (self.total_bytes_received as f32 * 8.0 / 1000.0) / period
        } else {
            0.0
        };

        VoiceStatsReport {
            timestamp: now,
            period_seconds: period,
            audio: AudioQualityMetrics {
                mos_score: mos,
                r_factor,
                signal_db,
                noise_db,
                snr_db: snr_db.max(0.0),
                distortion_percent: 0.0, // 需要额外测量
                speech_activity_percent: speech_activity,
            },
            network: NetworkQualityMetrics {
                latency_ms: self.average_latency(),
                jitter_ms: self.jitter(),
                packet_loss_percent: self.packet_loss_rate() * 100.0,
                burst_loss_percent: self.burst_loss_rate() * 100.0,
                out_of_order_percent: 0.0, // 需要序列号信息
                avg_packet_size,
                bandwidth_kbps,
            },
            codec: CodecMetrics::default(),         // 需要外部填充
            jitter: JitterBufferMetrics::default(), // 需要外部填充
            dsp: DspMetrics::default(),             // 需要外部填充
        }
    }

    /// 重置统计
    pub fn reset(&mut self) {
        self.latency_history.clear();
        self.loss_history.clear();
        self.size_history.clear();
        self.last_report = Instant::now();
        self.total_packets_sent = 0;
        self.total_packets_received = 0;
        self.total_packets_lost = 0;
        self.total_bytes_sent = 0;
        self.total_bytes_received = 0;
        self.signal_energy_sum = 0.0;
        self.noise_energy_sum = 0.0;
        self.sample_count = 0;
        self.active_speech_samples = 0;
    }

    /// 获取总计包数
    pub fn total_packets(&self) -> (u64, u64, u64) {
        (
            self.total_packets_sent,
            self.total_packets_received,
            self.total_packets_lost,
        )
    }
}

impl Default for StatsCollector {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mos_calculation() {
        let collector = StatsCollector::new();

        // 理想条件下的 MOS
        let ideal_mos = collector.calculate_mos(0.0, 0.0);
        assert!(ideal_mos > 4.0, "Ideal MOS should be > 4.0");

        // 典型 Opus 条件下的 MOS
        let typical_mos = collector.calculate_mos(5.0, 2.0);
        assert!(typical_mos > 3.0 && typical_mos < 4.5);

        // 差条件下的 MOS
        let poor_mos = collector.calculate_mos(30.0, 10.0);
        assert!(poor_mos < 3.0);
    }

    #[test]
    fn test_r_factor_to_mos() {
        // R=100 -> MOS=4.5
        assert!((StatsCollector::r_factor_to_mos(100.0) - 4.5).abs() < 0.01);

        // R=0 -> MOS=1.0
        assert!((StatsCollector::r_factor_to_mos(0.0) - 1.0).abs() < 0.01);

        // R=50 -> MOS 约 2.5
        let mos = StatsCollector::r_factor_to_mos(50.0);
        assert!(mos > 2.0 && mos < 3.0);
    }

    #[test]
    fn test_latency_stats() {
        let mut collector = StatsCollector::new();

        collector.record_latency(50.0);
        collector.record_latency(60.0);
        collector.record_latency(55.0);

        assert!((collector.average_latency() - 55.0).abs() < 0.1);
        assert!(collector.jitter() > 0.0);
    }

    #[test]
    fn test_packet_loss_stats() {
        let mut collector = StatsCollector::new();

        // 10% 丢包
        for i in 0..100 {
            collector.record_packet_loss(i < 10);
        }

        assert!((collector.packet_loss_rate() - 0.1).abs() < 0.01);
    }

    #[test]
    fn test_report_generation() {
        let mut collector = StatsCollector::new();

        // 记录一些数据
        collector.record_latency(50.0);
        collector.record_packet_received(100);
        collector.record_audio_energy(10000.0, 100.0, true);

        let report = collector.generate_report();

        assert!(report.audio.mos_score >= 1.0 && report.audio.mos_score <= 4.5);
        assert!(report.network.latency_ms > 0.0);
        assert!(!report.to_json().is_empty());
    }

    #[test]
    fn test_quality_rating() {
        let mut collector = StatsCollector::new();

        // 高 MOS
        for _ in 0..10 {
            collector.record_latency(20.0); // 低延迟
        }
        let good_report = collector.generate_report();
        assert_eq!(good_report.quality_rating(), QualityRating::Excellent);

        collector.reset();

        // 高延迟导致低 MOS
        for _ in 0..10 {
            collector.record_latency(500.0); // 高延迟
        }
        let poor_report = collector.generate_report();
        assert!(matches!(
            poor_report.quality_rating(),
            QualityRating::Poor | QualityRating::Bad
        ));
    }
}
