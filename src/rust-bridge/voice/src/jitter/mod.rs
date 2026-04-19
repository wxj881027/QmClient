//! 抖动缓冲区模块
//!
//! 用于平滑网络抖动，确保音频播放的连续性

use std::collections::VecDeque;

/// 抖动缓冲区配置
#[derive(Debug, Clone, Copy)]
pub struct JitterConfig {
    /// 目标缓冲帧数
    pub target_frames: usize,
    /// 最大缓冲帧数
    pub max_frames: usize,
    /// 最小缓冲帧数
    pub min_frames: usize,
}

impl Default for JitterConfig {
    fn default() -> Self {
        Self {
            target_frames: 3,
            max_frames: 8,
            min_frames: 1,
        }
    }
}

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

/// 抖动缓冲区
pub struct JitterBuffer {
    frames: VecDeque<AudioFrame>,
    config: JitterConfig,
    last_sequence: Option<u16>,
    /// 检测到的丢包数
    packets_lost: u32,
    /// 接收的包数
    packets_received: u32,
}

impl JitterBuffer {
    pub fn new(config: JitterConfig) -> Self {
        Self {
            frames: VecDeque::with_capacity(config.max_frames),
            config,
            last_sequence: None,
            packets_lost: 0,
            packets_received: 0,
        }
    }

    /// 推入帧
    pub fn push(&mut self, sequence: u16, samples: Vec<i16>) {
        self.packets_received += 1;

        // 检测丢包
        if let Some(last) = self.last_sequence {
            let expected = last.wrapping_add(1);
            if sequence != expected {
                let lost = sequence.wrapping_sub(expected);
                self.packets_lost += lost as u32;
            }
        }
        self.last_sequence = Some(sequence);

        // 检查是否已存在
        if self.frames.iter().any(|f| f.sequence == sequence) {
            return; // 重复包，忽略
        }

        let frame = AudioFrame {
            sequence,
            samples,
            recv_time: std::time::Instant::now(),
        };

        // 按序列号插入到正确位置
        let insert_pos = self
            .frames
            .iter()
            .position(|f| f.sequence.wrapping_sub(sequence) as i16 > 0)
            .unwrap_or(self.frames.len());

        self.frames.insert(insert_pos, frame);

        // 限制缓冲区大小
        while self.frames.len() > self.config.max_frames {
            self.frames.pop_front();
        }
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
    pub fn loss_rate(&self) -> f32 {
        let total = self.packets_received + self.packets_lost;
        if total == 0 {
            0.0
        } else {
            self.packets_lost as f32 / total as f32
        }
    }

    /// 调整目标帧数（根据网络状况）
    pub fn adjust_target(&mut self, jitter_ms: f32) {
        self.config.target_frames = if jitter_ms <= 8.0 {
            2
        } else if jitter_ms <= 14.0 {
            3
        } else if jitter_ms <= 22.0 {
            4
        } else if jitter_ms <= 32.0 {
            5
        } else {
            6
        };

        // 限制范围
        self.config.target_frames = self
            .config
            .target_frames
            .clamp(self.config.min_frames, self.config.max_frames);
    }

    /// 清空缓冲区
    pub fn clear(&mut self) {
        self.frames.clear();
        self.last_sequence = None;
    }

    /// 重置统计
    pub fn reset_stats(&mut self) {
        self.packets_lost = 0;
        self.packets_received = 0;
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
    fn test_adjust_target() {
        let mut buffer = JitterBuffer::default();

        buffer.adjust_target(5.0);
        assert_eq!(buffer.config.target_frames, 2);

        buffer.adjust_target(15.0);
        assert_eq!(buffer.config.target_frames, 3);

        buffer.adjust_target(50.0);
        assert_eq!(buffer.config.target_frames, 6);
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
}
