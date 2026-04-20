//! 抖动缓冲区模块
//!
//! 用于平滑网络抖动，确保音频播放的连续性

use std::collections::VecDeque;

// ============================================================================
// RFC 1982 序列号比较算法
// ============================================================================

/// 序列号比较的半范围阈值 (2^15 = 32768)
const SEQ_HALF_RANGE: u32 = 0x8000;

/// 比较两个序列号，考虑 u16 回绕 (RFC 1982)
/// 返回 true 如果 a < b（考虑回绕）
#[inline]
fn seq_less(a: u16, b: u16) -> bool {
    // RFC 1982: 使用 wrapping_sub 计算差值
    // 如果差值在 (0, 2^15) 范围内，说明 b > a
    // 如果差值在 [2^15, 2^16) 范围内，说明 a > b（回绕情况）
    let diff = b.wrapping_sub(a) as u32;
    diff > 0 && diff < SEQ_HALF_RANGE
}

/// 比较两个序列号，考虑 u16 回绕 (RFC 1982)
/// 返回 true 如果 a <= b（考虑回绕）
#[inline]
#[allow(dead_code)]
fn seq_less_eq(a: u16, b: u16) -> bool {
    a == b || seq_less(a, b)
}

/// 比较两个序列号，考虑 u16 回绕 (RFC 1982)
/// 返回 true 如果 a > b（考虑回绕）
#[inline]
fn seq_greater(a: u16, b: u16) -> bool {
    seq_less(b, a)
}

/// 计算两个序列号之间的距离（考虑回绕）
/// 返回从 a 到 b 的正向距离（假设 b >= a 考虑回绕）
#[inline]
fn seq_distance(a: u16, b: u16) -> u16 {
    b.wrapping_sub(a)
}

/// 计算两个序列号之间的差值（绝对值，考虑回绕）
/// 返回最小的距离（正向或反向）
#[inline]
fn seq_diff(a: u16, b: u16) -> u16 {
    let diff1 = a.wrapping_sub(b);
    let diff2 = b.wrapping_sub(a);
    diff1.min(diff2)
}

// ============================================================================
// 抖动缓冲区配置
// ============================================================================

/// 抖动缓冲区配置
#[derive(Debug, Clone, Copy)]
pub struct JitterConfig {
    /// 目标缓冲帧数
    pub target_frames: usize,
    /// 最大缓冲帧数
    pub max_frames: usize,
    /// 最小缓冲帧数
    pub min_frames: usize,
    /// 自适应调整的平滑因子 (0.0 - 1.0)
    pub adapt_factor: f32,
    /// 抖动估计的平滑因子 (0.0 - 1.0)
    pub jitter_smooth_factor: f32,
}

impl Default for JitterConfig {
    fn default() -> Self {
        Self {
            target_frames: 3,
            max_frames: 16, // 增大最大值以适应高抖动网络
            min_frames: 1,
            adapt_factor: 0.1,
            jitter_smooth_factor: 0.1,
        }
    }
}

// ============================================================================
// 自适应抖动估计器
// ============================================================================

/// 自适应抖动估计器
/// 使用指数加权移动平均 (EWMA) 估计网络抖动
#[derive(Debug)]
struct JitterEstimator {
    /// 平滑后的抖动估计 (帧数)
    smoothed_jitter: f32,
    /// 平滑因子
    smooth_factor: f32,
    /// 上一次接收时间
    last_recv_time: Option<std::time::Instant>,
    /// 上一次序列号
    last_sequence: Option<u16>,
    /// 抖动方差估计
    jitter_variance: f32,
}

impl JitterEstimator {
    fn new(smooth_factor: f32) -> Self {
        Self {
            smoothed_jitter: 0.0,
            smooth_factor,
            last_recv_time: None,
            last_sequence: None,
            jitter_variance: 0.0,
        }
    }

    /// 更新抖动估计
    fn update(&mut self, sequence: u16, recv_time: std::time::Instant, frame_duration_ms: f32) {
        if let (Some(last_time), Some(last_seq)) = (self.last_recv_time, self.last_sequence) {
            // 计算实际到达间隔
            let actual_interval_ms = recv_time.duration_since(last_time).as_secs_f32() * 1000.0;

            // 计算期望间隔（基于序列号差）
            let seq_diff = seq_distance(last_seq, sequence) as f32;
            let expected_interval_ms = seq_diff * frame_duration_ms;

            // 计算抖动（实际与期望的偏差）
            let jitter_ms = (actual_interval_ms - expected_interval_ms).abs();

            // 转换为帧数
            let jitter_frames = jitter_ms / frame_duration_ms;

            // 使用 EWMA 更新抖动估计
            let old_smoothed = self.smoothed_jitter;
            self.smoothed_jitter = self.smoothed_jitter * (1.0 - self.smooth_factor)
                + jitter_frames * self.smooth_factor;

            // 更新方差估计
            let delta = jitter_frames - old_smoothed;
            self.jitter_variance = self.jitter_variance * (1.0 - self.smooth_factor)
                + delta * delta * self.smooth_factor;
        }

        self.last_recv_time = Some(recv_time);
        self.last_sequence = Some(sequence);
    }

    /// 获取抖动估计（帧数）
    fn jitter_frames(&self) -> f32 {
        self.smoothed_jitter
    }

    /// 获取抖动标准差（帧数）
    fn jitter_stddev(&self) -> f32 {
        self.jitter_variance.sqrt()
    }

    /// 重置估计器
    fn reset(&mut self) {
        self.smoothed_jitter = 0.0;
        self.jitter_variance = 0.0;
        self.last_recv_time = None;
        self.last_sequence = None;
    }
}

// ============================================================================
// 音频帧
// ============================================================================

/// 音频帧
#[derive(Debug, Clone)]
pub struct AudioFrame {
    /// 序列号
    pub sequence: u16,
    /// PCM 样本
    pub samples: Vec<i16>,
    /// 接收时间戳
    pub recv_time: std::time::Instant,
}

// ============================================================================
// 抖动缓冲区
// ============================================================================

/// 抖动缓冲区
pub struct JitterBuffer {
    frames: VecDeque<AudioFrame>,
    config: JitterConfig,
    last_sequence: Option<u16>,
    /// 检测到的丢包数
    packets_lost: u32,
    /// 接收的包数
    packets_received: u32,
    /// 自适应抖动估计器
    jitter_estimator: JitterEstimator,
    /// 每帧时长 (ms)，用于抖动估计
    frame_duration_ms: f32,
    /// 目标帧数的平滑值
    smoothed_target: f32,
}

impl JitterBuffer {
    /// 创建新的抖动缓冲区
    ///
    /// # Arguments
    /// * `config` - 缓冲区配置
    /// * `sample_rate` - 采样率 (Hz)，用于计算帧时长
    /// * `frame_size` - 每帧样本数
    pub fn new(config: JitterConfig) -> Self {
        Self {
            frames: VecDeque::with_capacity(config.max_frames),
            config,
            last_sequence: None,
            packets_lost: 0,
            packets_received: 0,
            jitter_estimator: JitterEstimator::new(config.jitter_smooth_factor),
            frame_duration_ms: 20.0, // 默认 20ms (48000 Hz / 960 samples)
            smoothed_target: config.target_frames as f32,
        }
    }

    /// 设置音频参数
    pub fn set_audio_params(&mut self, sample_rate: u32, frame_size: usize) {
        // 计算每帧时长 (ms)
        self.frame_duration_ms = (frame_size as f32 / sample_rate as f32) * 1000.0;
    }

    /// 推入帧
    pub fn push(&mut self, sequence: u16, samples: Vec<i16>) {
        self.packets_received += 1;
        let recv_time = std::time::Instant::now();

        // 更新抖动估计
        self.jitter_estimator
            .update(sequence, recv_time, self.frame_duration_ms);

        // 检测丢包（使用 RFC 1982 序列号比较）
        if let Some(last) = self.last_sequence {
            let expected = last.wrapping_add(1);
            if sequence != expected {
                // 使用 seq_greater 判断是否有丢包
                if seq_greater(sequence, expected) {
                    // 计算丢失的包数
                    let lost = seq_distance(expected, sequence) as u32;
                    self.packets_lost += lost;
                }
                // 如果 sequence < expected，可能是乱序包或重传包
            }
        }
        self.last_sequence = Some(sequence);

        // 检查是否已存在（使用 seq_diff 判断是否为同一序列号）
        if self.frames.iter().any(|f| seq_diff(f.sequence, sequence) == 0) {
            return; // 重复包，忽略
        }

        let frame = AudioFrame {
            sequence,
            samples,
            recv_time,
        };

        // 按序列号插入到正确位置（使用 RFC 1982 比较）
        let insert_pos = self
            .frames
            .iter()
            .position(|f| seq_greater(f.sequence, sequence))
            .unwrap_or(self.frames.len());

        self.frames.insert(insert_pos, frame);

        // 限制缓冲区大小
        while self.frames.len() > self.config.max_frames {
            self.frames.pop_front();
        }

        // 自适应调整目标帧数
        self.adapt_target();
    }

    /// 弹出帧
    pub fn pop(&mut self) -> Option<AudioFrame> {
        // 等待达到目标帧数
        if self.frames.len() < self.config.target_frames {
            return None;
        }

        self.frames.pop_front()
    }

    /// 查看下一帧（不弹出）
    pub fn peek(&self) -> Option<&AudioFrame> {
        self.frames.front()
    }

    /// 获取当前缓冲帧数
    pub fn len(&self) -> usize {
        self.frames.len()
    }

    /// 是否为空
    pub fn is_empty(&self) -> bool {
        self.frames.is_empty()
    }

    /// 获取丢包率 (0.0 - 1.0)
    /// 计算方式：丢失包数 / 接收包数
    pub fn loss_rate(&self) -> f32 {
        if self.packets_received == 0 {
            0.0
        } else {
            self.packets_lost as f32 / self.packets_received as f32
        }
    }

    /// 自适应调整目标帧数
    /// 基于抖动估计和丢包率动态调整
    fn adapt_target(&mut self) {
        let jitter_frames = self.jitter_estimator.jitter_frames();
        let jitter_stddev = self.jitter_estimator.jitter_stddev();

        // 目标帧数 = 抖动估计 + 2 * 标准差（覆盖 95% 的抖动）
        // 加上安全边际以应对突发抖动
        let target = jitter_frames + 2.0 * jitter_stddev + 1.0;

        // 使用平滑因子渐进调整
        self.smoothed_target = self.smoothed_target * (1.0 - self.config.adapt_factor)
            + target * self.config.adapt_factor;

        // 转换为整数并限制范围
        let new_target = (self.smoothed_target.ceil() as usize)
            .clamp(self.config.min_frames, self.config.max_frames);

        self.config.target_frames = new_target;
    }

    /// 手动调整目标帧数（用于外部控制）
    pub fn adjust_target(&mut self, jitter_ms: f32) {
        // 将 ms 转换为帧数
        let jitter_frames = jitter_ms / self.frame_duration_ms;

        // 更新平滑目标
        self.smoothed_target = self.smoothed_target * (1.0 - self.config.adapt_factor)
            + jitter_frames * self.config.adapt_factor;

        // 限制范围
        self.config.target_frames = (self.smoothed_target.ceil() as usize)
            .clamp(self.config.min_frames, self.config.max_frames);
    }

    /// 获取当前抖动估计 (帧数)
    pub fn jitter_estimate(&self) -> f32 {
        self.jitter_estimator.jitter_frames()
    }

    /// 获取当前抖动估计 (ms)
    pub fn jitter_estimate_ms(&self) -> f32 {
        self.jitter_estimator.jitter_frames() * self.frame_duration_ms
    }

    /// 获取当前目标帧数
    pub fn target_frames(&self) -> usize {
        self.config.target_frames
    }

    /// 清空缓冲区
    pub fn clear(&mut self) {
        self.frames.clear();
        self.last_sequence = None;
        self.jitter_estimator.reset();
        self.smoothed_target = self.config.target_frames as f32;
    }

    /// 重置统计
    pub fn reset_stats(&mut self) {
        self.packets_lost = 0;
        self.packets_received = 0;
        self.jitter_estimator.reset();
    }

    /// 获取下一帧的序列号
    pub fn next_sequence(&self) -> Option<u16> {
        self.frames.front().map(|f| f.sequence)
    }
}

impl Default for JitterBuffer {
    fn default() -> Self {
        Self::new(JitterConfig::default())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ========================================================================
    // RFC 1982 序列号比较测试
    // ========================================================================

    #[test]
    fn test_seq_less_normal() {
        // 正常情况：a < b
        assert!(seq_less(1, 2));
        assert!(seq_less(100, 200));
        assert!(seq_less(0, 1));
        assert!(!seq_less(2, 1));
        assert!(!seq_less(200, 100));
    }

    #[test]
    fn test_seq_less_wraparound() {
        // 回绕情况：65535 -> 0
        // 65535 < 0（考虑回绕）
        assert!(seq_less(65535, 0));
        assert!(seq_less(65535, 1));
        assert!(seq_less(65534, 0));

        // 反过来不成立
        assert!(!seq_less(0, 65535));
        assert!(!seq_less(1, 65535));

        // 跨越回绕边界的比较
        assert!(seq_less(65530, 10)); // 65530 < 10（回绕后）
        assert!(!seq_less(10, 65530)); // 10 > 65530（回绕后）
    }

    #[test]
    fn test_seq_less_equal() {
        // 相等的情况
        assert!(!seq_less(0, 0));
        assert!(!seq_less(65535, 65535));
        assert!(!seq_less(100, 100));

        // seq_less_eq 包含相等
        assert!(seq_less_eq(0, 0));
        assert!(seq_less_eq(65535, 65535));
        assert!(seq_less_eq(100, 100));
    }

    #[test]
    fn test_seq_greater() {
        // 正常情况
        assert!(seq_greater(2, 1));
        assert!(seq_greater(200, 100));

        // 回绕情况
        assert!(seq_greater(0, 65535));
        assert!(seq_greater(1, 65535));
        assert!(seq_greater(10, 65530));
    }

    #[test]
    fn test_seq_distance() {
        // 正常距离
        assert_eq!(seq_distance(1, 5), 4);
        assert_eq!(seq_distance(100, 200), 100);

        // 回绕距离
        assert_eq!(seq_distance(65535, 0), 1);
        assert_eq!(seq_distance(65535, 2), 3);
        assert_eq!(seq_distance(65530, 10), 16);
    }

    #[test]
    fn test_seq_diff() {
        // 绝对差值
        assert_eq!(seq_diff(1, 5), 4);
        assert_eq!(seq_diff(5, 1), 4);

        // 回绕情况：取较小的距离
        assert_eq!(seq_diff(65535, 0), 1);
        assert_eq!(seq_diff(0, 65535), 1);
        assert_eq!(seq_diff(65530, 10), 16);
        assert_eq!(seq_diff(10, 65530), 16);
    }

    // ========================================================================
    // 抖动缓冲区基本功能测试
    // ========================================================================

    #[test]
    fn test_jitter_buffer_order() {
        let mut buffer = JitterBuffer::default();

        // 乱序插入
        buffer.push(3, vec![3i16; 960]);
        buffer.push(1, vec![1i16; 960]);
        buffer.push(2, vec![2i16; 960]);

        // 等待达到目标
        buffer.config.target_frames = 1;

        // 应该按顺序输出
        let frame1 = buffer.pop().unwrap();
        assert_eq!(frame1.sequence, 1);

        let frame2 = buffer.pop().unwrap();
        assert_eq!(frame2.sequence, 2);

        let frame3 = buffer.pop().unwrap();
        assert_eq!(frame3.sequence, 3);
    }

    #[test]
    fn test_jitter_buffer_underflow() {
        let mut buffer = JitterBuffer::default();

        // 推入 2 帧，目标 3 帧
        buffer.push(1, vec![1i16; 960]);
        buffer.push(2, vec![2i16; 960]);

        // 应该返回 None
        assert!(buffer.pop().is_none());

        // 再推入一帧
        buffer.push(3, vec![3i16; 960]);

        // 现在可以弹出了
        assert!(buffer.pop().is_some());
    }

    #[test]
    fn test_jitter_buffer_duplicate() {
        let mut buffer = JitterBuffer::default();

        buffer.push(1, vec![1i16; 960]);
        buffer.push(1, vec![100i16; 960]); // 重复

        buffer.config.target_frames = 1;

        let frame = buffer.pop().unwrap();
        assert_eq!(frame.samples[0], 1); // 应该是第一个

        assert!(buffer.pop().is_none()); // 没有第二个
    }

    #[test]
    fn test_loss_detection() {
        let mut buffer = JitterBuffer::default();

        buffer.push(1, vec![1i16; 960]);
        buffer.push(3, vec![3i16; 960]); // 丢失 2

        // 丢包率应该是 50%
        let loss = buffer.loss_rate();
        assert!((loss - 0.5).abs() < 0.01);
    }

    #[test]
    fn test_max_size_limit() {
        let mut config = JitterConfig::default();
        config.max_frames = 3;
        let mut buffer = JitterBuffer::new(config);

        // 推入超过最大限制的帧
        buffer.push(1, vec![1i16; 960]);
        buffer.push(2, vec![2i16; 960]);
        buffer.push(3, vec![3i16; 960]);
        buffer.push(4, vec![4i16; 960]);

        // 应该只保留后 3 个
        assert_eq!(buffer.len(), 3);
    }

    // ========================================================================
    // 序列号回绕场景测试
    // ========================================================================

    #[test]
    fn test_sequence_wraparound_order() {
        let mut buffer = JitterBuffer::default();

        // 模拟序列号回绕：65534 -> 65535 -> 0 -> 1
        buffer.push(65535, vec![-1i16; 960]); // 65535 作为序列号
        buffer.push(0, vec![0i16; 960]); // 回绕
        buffer.push(65534, vec![-2i16; 960]); // 乱序
        buffer.push(1, vec![1i16; 960]);

        buffer.config.target_frames = 1;

        // 应该按正确顺序输出
        let frame1 = buffer.pop().unwrap();
        assert_eq!(frame1.sequence, 65534);

        let frame2 = buffer.pop().unwrap();
        assert_eq!(frame2.sequence, 65535);

        let frame3 = buffer.pop().unwrap();
        assert_eq!(frame3.sequence, 0);

        let frame4 = buffer.pop().unwrap();
        assert_eq!(frame4.sequence, 1);
    }

    #[test]
    fn test_sequence_wraparound_loss_detection() {
        let mut buffer = JitterBuffer::default();

        // 序列号从 65535 回绕到 0，中间丢失 0
        buffer.push(65535, vec![1i16; 960]);
        buffer.push(1, vec![1i16; 960]); // 丢失 0

        // 应该检测到 1 个丢包
        assert_eq!(buffer.packets_lost, 1);
    }

    #[test]
    fn test_sequence_wraparound_duplicate() {
        let mut buffer = JitterBuffer::default();

        // 推入回绕附近的序列号
        buffer.push(65535, vec![1i16; 960]);
        buffer.push(0, vec![2i16; 960]);
        buffer.push(65535, vec![100i16; 960]); // 重复

        buffer.config.target_frames = 1;

        // 应该只有 2 帧
        let frame1 = buffer.pop().unwrap();
        assert_eq!(frame1.sequence, 65535);
        assert_eq!(frame1.samples[0], 1); // 第一个

        let frame2 = buffer.pop().unwrap();
        assert_eq!(frame2.sequence, 0);

        assert!(buffer.pop().is_none());
    }

    #[test]
    fn test_sequence_wraparound_large_gap() {
        let mut buffer = JitterBuffer::default();

        // 大跨度回绕
        buffer.push(65530, vec![1i16; 960]);
        buffer.push(10, vec![1i16; 960]); // 跨越回绕

        // 应该检测到丢包：65531-65535 (5个) + 0-9 (10个) = 15个
        // 但由于序列号差值计算，实际是 seq_distance(65531, 10) = 15
        // 预期序列号是 65531，收到的是 10
        // seq_greater(10, 65531) 应该为 true
        assert!(seq_greater(10, 65531));
        assert_eq!(seq_distance(65531, 10), 15);
    }

    // ========================================================================
    // 自适应抖动缓冲区测试
    // ========================================================================

    #[test]
    fn test_adaptive_target_adjustment() {
        let mut config = JitterConfig::default();
        config.min_frames = 1;
        config.max_frames = 10;
        config.adapt_factor = 0.5; // 较快调整便于测试
        let mut buffer = JitterBuffer::new(config);

        // 初始目标帧数
        assert_eq!(buffer.target_frames(), 3);

        // 手动设置高抖动
        buffer.adjust_target(100.0); // 100ms 抖动 = 5 帧（假设 20ms/帧）

        // 目标帧数应该增加
        let target = buffer.target_frames();
        assert!(target >= 3, "Target should increase with high jitter, got {}", target);
    }

    #[test]
    fn test_jitter_estimator() {
        let mut estimator = JitterEstimator::new(0.5);

        // 初始抖动为 0
        assert_eq!(estimator.jitter_frames(), 0.0);

        // 更新一些数据点
        let now = std::time::Instant::now();
        estimator.update(1, now, 20.0);
        estimator.update(2, now + std::time::Duration::from_millis(25), 20.0); // 5ms 抖动

        // 抖动估计应该更新
        let jitter = estimator.jitter_frames();
        assert!(jitter > 0.0, "Jitter should be positive after update");
    }

    #[test]
    fn test_set_audio_params() {
        let mut buffer = JitterBuffer::default();

        // 设置 48kHz 采样率，960 样本/帧
        buffer.set_audio_params(48000, 960);

        // 每帧时长应该是 20ms
        assert!((buffer.frame_duration_ms - 20.0).abs() < 0.01);

        // 设置 44.1kHz 采样率，441 样本/帧
        buffer.set_audio_params(44100, 441);

        // 每帧时长应该是 10ms
        assert!((buffer.frame_duration_ms - 10.0).abs() < 0.01);
    }

    #[test]
    fn test_clear_resets_estimator() {
        let mut buffer = JitterBuffer::default();

        // 推入一些帧
        buffer.push(1, vec![1i16; 960]);
        buffer.push(2, vec![2i16; 960]);

        // 清空
        buffer.clear();

        // 抖动估计应该重置
        assert_eq!(buffer.jitter_estimate(), 0.0);
        assert!(buffer.is_empty());
    }

    #[test]
    fn test_target_frames_clamping() {
        let mut config = JitterConfig::default();
        config.min_frames = 2;
        config.max_frames = 5;
        config.adapt_factor = 1.0; // 立即调整
        let mut buffer = JitterBuffer::new(config);

        // 尝试设置低于最小值
        buffer.adjust_target(10.0); // 0.5 帧
        assert_eq!(buffer.target_frames(), 2); // 应该被限制到最小值

        // 尝试设置高于最大值
        buffer.adjust_target(200.0); // 10 帧
        assert_eq!(buffer.target_frames(), 5); // 应该被限制到最大值
    }

    #[test]
    fn test_out_of_order_packet_not_counted_as_loss() {
        let mut buffer = JitterBuffer::default();

        // 正常顺序
        buffer.push(1, vec![1i16; 960]);
        buffer.push(2, vec![2i16; 960]);

        // 乱序到达（晚到的包）
        buffer.push(1, vec![1i16; 960]); // 重复，不应该增加丢包

        // 只有 1 个接收（重复包被忽略）
        assert_eq!(buffer.packets_received, 3);
        // 丢包应该是 0（因为乱序包不算丢包）
        assert_eq!(buffer.packets_lost, 0);
    }
}
