//! 可靠传输层
//!
//! 提供序列号管理、丢包检测、FEC 支持等功能
//!
//! 参考 C++ 实现: voice_core.cpp

use std::time::{Duration, Instant};

use super::protocol::{seq_delta, seq_less, VOICE_MAX_PAYLOAD};

/// 最大抖动包数
pub const MAX_JITTER_PACKETS: usize = 16;
/// 最大帧数
pub const MAX_FRAMES: usize = 32;

/// 序列号管理器
#[derive(Debug, Clone)]
pub struct SequenceManager {
    /// 当前序列号
    current: u16,
    /// 初始序列号（用于检测流重置）
    initial: u16,
    /// 是否已发送
    has_sent: bool,
}

impl SequenceManager {
    /// 创建新的序列号管理器
    pub fn new() -> Self {
        Self {
            current: 0,
            initial: 0,
            has_sent: false,
        }
    }

    /// 获取下一个序列号
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> u16 {
        let seq = self.current;
        self.current = self.current.wrapping_add(1);
        self.has_sent = true;
        seq
    }

    /// 获取当前序列号（不递增）
    pub fn current(&self) -> u16 {
        self.current
    }

    /// 重置序列号（跳过一定数量以区分新旧流）
    pub fn reset(&mut self, jump: u16) {
        self.current = self.current.wrapping_add(jump);
        self.initial = self.current;
    }

    /// 检查是否已发送
    pub fn has_sent(&self) -> bool {
        self.has_sent
    }
}

impl Default for SequenceManager {
    fn default() -> Self {
        Self::new()
    }
}

/// 丢包检测器
#[derive(Debug, Clone)]
pub struct LossDetector {
    /// 最后收到的序列号
    last_seq: Option<u16>,
    /// 是否有最后序列号
    has_last_seq: bool,
    /// 丢包计数
    packets_lost: u64,
    /// 总包计数
    packets_total: u64,
    /// EWMA 丢包率
    loss_ewma: f32,
    /// EWMA 衰减系数
    ewma_alpha: f32,
}

impl LossDetector {
    /// 创建新的丢包检测器
    pub fn new() -> Self {
        Self {
            last_seq: None,
            has_last_seq: false,
            packets_lost: 0,
            packets_total: 0,
            loss_ewma: 0.0,
            ewma_alpha: 0.1,
        }
    }

    /// 记录收到的包
    pub fn record_packet(&mut self, seq: u16) -> u32 {
        self.packets_total += 1;

        let lost = if let Some(last) = self.last_seq {
            let expected = last.wrapping_add(1);
            if seq != expected {
                let delta = seq_delta(seq, expected);
                if delta > 0 && delta < 1000 {
                    // delta 表示从 expected 到 seq 的距离
                    // 丢失的包是 expected, expected+1, ..., seq-1，共 delta 个
                    let lost_count = delta as u32;
                    self.packets_lost += lost_count as u64;

                    // 更新 EWMA
                    let loss_ratio = lost_count as f32 / (lost_count + 1) as f32;
                    self.loss_ewma =
                        (1.0 - self.ewma_alpha) * self.loss_ewma + self.ewma_alpha * loss_ratio;

                    lost_count
                } else {
                    0
                }
            } else {
                // 无丢包，降低 EWMA
                self.loss_ewma *= 1.0 - self.ewma_alpha;
                0
            }
        } else {
            0
        };

        // 更新最后序列号
        if !self.has_last_seq || seq_less(self.last_seq.unwrap(), seq) {
            self.last_seq = Some(seq);
            self.has_last_seq = true;
        }

        lost
    }

    /// 获取丢包率 (0.0 - 1.0)
    pub fn loss_rate(&self) -> f32 {
        if self.packets_total == 0 {
            0.0
        } else {
            self.packets_lost as f32 / (self.packets_total + self.packets_lost) as f32
        }
    }

    /// 获取 EWMA 丢包率
    pub fn loss_ewma(&self) -> f32 {
        self.loss_ewma
    }

    /// 重置检测器
    pub fn reset(&mut self) {
        self.last_seq = None;
        self.has_last_seq = false;
        self.packets_lost = 0;
        self.packets_total = 0;
        self.loss_ewma = 0.0;
    }

    /// 获取最后序列号
    pub fn last_seq(&self) -> Option<u16> {
        self.last_seq
    }
}

impl Default for LossDetector {
    fn default() -> Self {
        Self::new()
    }
}

/// 抖动包
#[derive(Debug, Clone)]
pub struct JitterPacket {
    /// 是否有效
    pub valid: bool,
    /// 序列号
    pub seq: u16,
    /// 数据大小
    pub size: usize,
    /// 左声道增益
    pub left_gain: f32,
    /// 右声道增益
    pub right_gain: f32,
    /// 数据
    pub data: [u8; VOICE_MAX_PAYLOAD],
}

impl Default for JitterPacket {
    fn default() -> Self {
        Self {
            valid: false,
            seq: 0,
            size: 0,
            left_gain: 1.0,
            right_gain: 1.0,
            data: [0u8; VOICE_MAX_PAYLOAD],
        }
    }
}

/// 抖动缓冲区（参考 C++ SVoicePeer）
#[derive(Debug)]
pub struct JitterBuffer {
    /// 抖动包数组
    packets: Vec<JitterPacket>,
    /// 队列中的包数
    queued_packets: usize,
    /// 最后序列号
    last_seq: u16,
    /// 是否有序列号
    has_seq: bool,
    /// 下一个期望序列号
    next_seq: u16,
    /// 是否有下一个序列号
    has_next_seq: bool,
    /// 最后接收序列号
    last_recv_seq: u16,
    /// 是否有最后接收序列号
    has_last_recv_seq: bool,
    /// 最后接收时间
    last_recv_time: Option<Instant>,
    /// 抖动估计 (ms)
    jitter_ms: f32,
    /// 目标帧数
    target_frames: usize,
    /// 最后左增益
    last_left_gain: f32,
    /// 最后右增益
    last_right_gain: f32,
    /// 丢包 EWMA
    loss_ewma: f32,
}

impl JitterBuffer {
    /// 创建新的抖动缓冲区
    pub fn new() -> Self {
        Self {
            packets: vec![JitterPacket::default(); MAX_JITTER_PACKETS],
            queued_packets: 0,
            last_seq: 0,
            has_seq: false,
            next_seq: 0,
            has_next_seq: false,
            last_recv_seq: 0,
            has_last_recv_seq: false,
            last_recv_time: None,
            jitter_ms: 0.0,
            target_frames: 3,
            last_left_gain: 1.0,
            last_right_gain: 1.0,
            loss_ewma: 0.0,
        }
    }

    /// 推入包
    pub fn push(&mut self, seq: u16, data: &[u8], left_gain: f32, right_gain: f32) {
        let now = Instant::now();

        // 检查是否需要重置
        let should_reset = if let Some(last_time) = self.last_recv_time {
            let gap = now.duration_since(last_time);
            gap > Duration::from_secs(2)
        } else {
            false
        };

        if should_reset {
            self.reset();
        }

        // 更新抖动估计
        if let Some(last_time) = self.last_recv_time {
            let delta_ms = now.duration_since(last_time).as_millis() as f32;
            let deviation = (delta_ms - 20.0).abs();
            self.jitter_ms = 0.9 * self.jitter_ms + 0.1 * deviation;
        }
        self.last_recv_time = Some(now);

        // 调整目标帧数
        self.adjust_target();

        // 更新丢包估计
        if self.has_last_recv_seq {
            let expected = self.last_recv_seq.wrapping_add(1);
            if seq != expected {
                self.target_frames = (self.target_frames + 1).min(6);
            }

            let delta = seq_delta(seq, self.last_recv_seq);
            if delta > 0 && delta < 1000 {
                let lost = (delta - 1).max(0) as u32;
                let loss_ratio = lost as f32 / delta as f32;
                self.loss_ewma = 0.9 * self.loss_ewma + 0.1 * loss_ratio;
            }
        }

        // 更新最后接收序列号
        if !self.has_last_recv_seq || seq_less(self.last_recv_seq, seq) {
            self.last_recv_seq = seq;
        }
        self.has_last_recv_seq = true;

        // 保存增益
        self.last_left_gain = left_gain;
        self.last_right_gain = right_gain;

        // 存入包
        let slot = (seq as usize) % MAX_JITTER_PACKETS;
        let pkt = &mut self.packets[slot];
        let is_new_packet = !pkt.valid || pkt.seq != seq;

        pkt.valid = true;
        pkt.seq = seq;
        pkt.size = data.len().min(VOICE_MAX_PAYLOAD);
        pkt.left_gain = left_gain;
        pkt.right_gain = right_gain;
        pkt.data[..pkt.size].copy_from_slice(&data[..pkt.size]);

        if is_new_packet {
            self.queued_packets = (self.queued_packets + 1).min(MAX_JITTER_PACKETS);
        }
    }

    /// 弹出包
    pub fn pop(&mut self) -> Option<(u16, Vec<u8>, f32, f32)> {
        // 等待达到目标帧数
        if !self.has_next_seq {
            if self.queued_packets < self.target_frames {
                return None;
            }

            // 找到起始序列号
            let mut found = false;
            let mut start_seq = 0u16;

            for pkt in &self.packets {
                if pkt.valid {
                    if !found {
                        start_seq = pkt.seq;
                        found = true;
                    } else if seq_less(pkt.seq, start_seq) {
                        start_seq = pkt.seq;
                    }
                }
            }

            if !found {
                return None;
            }

            self.next_seq = start_seq;
            self.has_next_seq = true;
        }

        // 获取包
        let slot = (self.next_seq as usize) % MAX_JITTER_PACKETS;
        let pkt = &self.packets[slot];

        if pkt.valid && pkt.seq == self.next_seq {
            let data = pkt.data[..pkt.size].to_vec();
            let left_gain = pkt.left_gain;
            let right_gain = pkt.right_gain;
            let seq = pkt.seq;

            // 标记为无效
            let pkt = &mut self.packets[slot];
            pkt.valid = false;
            self.queued_packets = self.queued_packets.saturating_sub(1);

            // 更新状态
            self.last_seq = self.next_seq;
            self.has_seq = true;
            self.next_seq = self.next_seq.wrapping_add(1);

            Some((seq, data, left_gain, right_gain))
        } else {
            // 丢包，尝试 FEC 恢复
            if self.loss_ewma > 0.02 {
                let next_slot = (self.next_seq.wrapping_add(1) as usize) % MAX_JITTER_PACKETS;
                let next_pkt = &self.packets[next_slot];

                if next_pkt.valid && next_pkt.seq == self.next_seq.wrapping_add(1) {
                    // 可以使用 FEC（需要解码器支持）
                    // 这里只返回标记，实际 FEC 由解码器处理
                }
            }

            // 跳过这个序列号
            self.last_seq = self.next_seq;
            self.has_seq = true;
            self.next_seq = self.next_seq.wrapping_add(1);

            None
        }
    }

    /// 调整目标帧数
    fn adjust_target(&mut self) {
        self.target_frames = if self.jitter_ms <= 8.0 {
            2
        } else if self.jitter_ms <= 14.0 {
            3
        } else if self.jitter_ms <= 22.0 {
            4
        } else if self.jitter_ms <= 32.0 {
            5
        } else {
            6
        };
    }

    /// 获取队列中的包数
    pub fn len(&self) -> usize {
        self.queued_packets
    }

    /// 是否为空
    pub fn is_empty(&self) -> bool {
        self.queued_packets == 0
    }

    /// 获取抖动 (ms)
    pub fn jitter_ms(&self) -> f32 {
        self.jitter_ms
    }

    /// 获取丢包 EWMA
    pub fn loss_ewma(&self) -> f32 {
        self.loss_ewma
    }

    /// 获取目标帧数
    pub fn target_frames(&self) -> usize {
        self.target_frames
    }

    /// 重置缓冲区
    pub fn reset(&mut self) {
        for pkt in &mut self.packets {
            *pkt = JitterPacket::default();
        }
        self.queued_packets = 0;
        self.last_seq = 0;
        self.has_seq = false;
        self.next_seq = 0;
        self.has_next_seq = false;
        self.last_recv_seq = 0;
        self.has_last_recv_seq = false;
        self.last_recv_time = None;
        self.jitter_ms = 0.0;
        self.target_frames = 3;
        self.last_left_gain = 1.0;
        self.last_right_gain = 1.0;
        self.loss_ewma = 0.0;
    }
}

impl Default for JitterBuffer {
    fn default() -> Self {
        Self::new()
    }
}

/// FEC 配置
#[derive(Debug, Clone, Copy)]
pub struct FecConfig {
    /// 是否启用 FEC
    pub enabled: bool,
    /// FEC 开销比例 (0.0 - 1.0)
    pub overhead: f32,
    /// 最大 FEC 块大小
    pub max_block_size: usize,
}

impl Default for FecConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            overhead: 0.2,
            max_block_size: 10,
        }
    }
}

/// 自适应比特率控制器
#[derive(Debug, Clone)]
pub struct AdaptiveBitrateController {
    /// 当前比特率
    current_bitrate: i32,
    /// 目标比特率
    target_bitrate: i32,
    /// 最小比特率
    min_bitrate: i32,
    /// 最大比特率
    max_bitrate: i32,
    /// 当前丢包率
    current_loss: f32,
    /// 当前抖动 (ms)
    current_jitter: f32,
    /// 是否启用 FEC
    fec_enabled: bool,
    /// 当前 FEC 丢包百分比
    fec_loss_perc: i32,
    /// 最后更新时间
    last_update: Option<Instant>,
    /// 更新间隔
    update_interval: Duration,
}

impl AdaptiveBitrateController {
    /// 创建新的自适应比特率控制器
    pub fn new() -> Self {
        Self {
            current_bitrate: 24000,
            target_bitrate: 24000,
            min_bitrate: 6000,
            max_bitrate: 510000,
            current_loss: 0.0,
            current_jitter: 0.0,
            fec_enabled: false,
            fec_loss_perc: 0,
            last_update: None,
            update_interval: Duration::from_secs(1),
        }
    }

    /// 更新网络状况
    pub fn update(&mut self, loss_rate: f32, jitter_ms: f32) {
        self.update_internal(loss_rate, jitter_ms, false);
    }

    /// 强制更新网络状况（跳过间隔检查）
    pub fn force_update(&mut self, loss_rate: f32, jitter_ms: f32) {
        self.update_internal(loss_rate, jitter_ms, true);
    }

    /// 内部更新逻辑
    fn update_internal(&mut self, loss_rate: f32, jitter_ms: f32, force: bool) {
        let now = Instant::now();

        // 检查更新间隔（强制更新时跳过）
        if !force {
            if let Some(last) = self.last_update {
                if now.duration_since(last) < self.update_interval {
                    return;
                }
            }
        }

        self.current_loss = loss_rate;
        self.current_jitter = jitter_ms;
        self.last_update = Some(now);

        // 根据网络状况调整
        let loss_perc = (loss_rate * 100.0) as i32;

        if loss_perc <= 2 && jitter_ms < 8.0 {
            // 良好网络
            self.target_bitrate = 32000;
            self.fec_enabled = false;
            self.fec_loss_perc = 0;
        } else if loss_perc <= 5 {
            // 轻微丢包
            self.target_bitrate = 24000;
            self.fec_enabled = true;
            self.fec_loss_perc = 5;
        } else if loss_perc <= 10 {
            // 中等丢包
            self.target_bitrate = 20000;
            self.fec_enabled = true;
            self.fec_loss_perc = 10;
        } else {
            // 严重丢包
            self.target_bitrate = 16000;
            self.fec_enabled = true;
            self.fec_loss_perc = 20;
        }

        // 限制范围
        self.target_bitrate = self
            .target_bitrate
            .clamp(self.min_bitrate, self.max_bitrate);
    }

    /// 获取当前比特率
    pub fn bitrate(&self) -> i32 {
        self.current_bitrate
    }

    /// 获取目标比特率
    pub fn target_bitrate(&self) -> i32 {
        self.target_bitrate
    }

    /// 是否启用 FEC
    pub fn fec_enabled(&self) -> bool {
        self.fec_enabled
    }

    /// 获取 FEC 丢包百分比
    pub fn fec_loss_perc(&self) -> i32 {
        self.fec_loss_perc
    }

    /// 应用目标比特率
    pub fn apply(&mut self) -> bool {
        if self.current_bitrate != self.target_bitrate {
            self.current_bitrate = self.target_bitrate;
            true
        } else {
            false
        }
    }

    /// 重置控制器
    pub fn reset(&mut self) {
        self.current_bitrate = 24000;
        self.target_bitrate = 24000;
        self.current_loss = 0.0;
        self.current_jitter = 0.0;
        self.fec_enabled = false;
        self.fec_loss_perc = 0;
        self.last_update = None;
    }
}

impl Default for AdaptiveBitrateController {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sequence_manager() {
        let mut seq = SequenceManager::new();

        assert_eq!(seq.next(), 0);
        assert_eq!(seq.next(), 1);
        assert_eq!(seq.next(), 2);

        seq.reset(1000);
        assert!(seq.current() >= 1000);
    }

    #[test]
    fn test_sequence_manager_wrap() {
        let mut seq = SequenceManager::new();
        seq.current = 65534;

        assert_eq!(seq.next(), 65534);
        assert_eq!(seq.next(), 65535);
        assert_eq!(seq.next(), 0); // 回绕
    }

    #[test]
    fn test_loss_detector() {
        let mut detector = LossDetector::new();

        // 正常序列
        assert_eq!(detector.record_packet(0), 0);
        assert_eq!(detector.record_packet(1), 0);
        assert_eq!(detector.record_packet(2), 0);

        // 丢包
        assert_eq!(detector.record_packet(5), 2); // 丢失 3, 4

        let loss = detector.loss_rate();
        assert!(loss > 0.0);
    }

    #[test]
    fn test_loss_detector_ewma() {
        let mut detector = LossDetector::new();

        // 连续丢包
        detector.record_packet(0);
        detector.record_packet(2); // 丢失 1
        assert!(detector.loss_ewma() > 0.0);

        // 正常包应该降低 EWMA
        detector.record_packet(3);
        let ewma_before = detector.loss_ewma();
        detector.record_packet(4);
        assert!(detector.loss_ewma() < ewma_before);
    }

    #[test]
    fn test_jitter_buffer_push_pop() {
        let mut buffer = JitterBuffer::new();

        // 推入包
        buffer.push(0, &[1, 2, 3], 1.0, 1.0);
        buffer.push(1, &[4, 5, 6], 1.0, 1.0);
        buffer.push(2, &[7, 8, 9], 1.0, 1.0);

        // 等待达到目标
        assert!(buffer.len() >= buffer.target_frames());

        // 弹出
        let result = buffer.pop();
        assert!(result.is_some());
        let (seq, data, _, _) = result.unwrap();
        assert_eq!(seq, 0);
        assert_eq!(data, vec![1, 2, 3]);
    }

    #[test]
    fn test_jitter_buffer_out_of_order() {
        let mut buffer = JitterBuffer::new();

        // 乱序推入
        buffer.push(2, &[7, 8, 9], 1.0, 1.0);
        buffer.push(0, &[1, 2, 3], 1.0, 1.0);
        buffer.push(1, &[4, 5, 6], 1.0, 1.0);

        // 应该按顺序弹出
        let result1 = buffer.pop();
        assert!(result1.is_some());
        assert_eq!(result1.unwrap().0, 0);

        let result2 = buffer.pop();
        assert!(result2.is_some());
        assert_eq!(result2.unwrap().0, 1);

        let result3 = buffer.pop();
        assert!(result3.is_some());
        assert_eq!(result3.unwrap().0, 2);
    }

    #[test]
    fn test_jitter_buffer_reset() {
        let mut buffer = JitterBuffer::new();

        buffer.push(0, &[1, 2, 3], 1.0, 1.0);
        buffer.push(1, &[4, 5, 6], 1.0, 1.0);

        buffer.reset();

        assert!(buffer.is_empty());
        assert_eq!(buffer.jitter_ms(), 0.0);
    }

    #[test]
    fn test_adaptive_bitrate() {
        let mut controller = AdaptiveBitrateController::new();

        // 良好网络
        controller.force_update(0.01, 5.0);
        assert_eq!(controller.target_bitrate(), 32000);
        assert!(!controller.fec_enabled());

        // 中等丢包
        controller.force_update(0.08, 15.0);
        assert_eq!(controller.target_bitrate(), 20000);
        assert!(controller.fec_enabled());

        // 严重丢包
        controller.force_update(0.15, 30.0);
        assert_eq!(controller.target_bitrate(), 16000);
        assert!(controller.fec_enabled());
    }

    #[test]
    fn test_adaptive_bitrate_apply() {
        let mut controller = AdaptiveBitrateController::new();

        controller.force_update(0.08, 15.0);
        let changed = controller.apply();

        assert!(changed);
        assert_eq!(controller.bitrate(), controller.target_bitrate());
    }

    #[test]
    fn test_jitter_buffer_queued_packets_overwrite() {
        let mut jb = JitterBuffer::new();

        // 推入第一个包
        jb.push(1, &[0u8; 100], 1.0, 1.0);
        assert_eq!(jb.len(), 1);

        // 推入相同序列号的包到同一槽位
        // push 同一序列号，计数不应增加
        jb.push(1, &[1u8; 100], 1.0, 1.0);
        assert_eq!(
            jb.len(),
            1,
            "Overwriting same seq should not increase count"
        );

        // 推入新序列号的包
        jb.push(2, &[2u8; 100], 1.0, 1.0);
        assert_eq!(jb.len(), 2, "New seq should increase count");
    }
}
