//! 无锁音频捕获队列
//!
//! 使用 crossbeam::queue::ArrayQueue 实现生产者-消费者无锁队列
//! 用于音频回调线程（生产者）和工作线程（消费者）之间的数据传递
//!
//! 设计特点：
//! - 无锁操作：避免音频回调线程被阻塞
//! - 溢出处理：队列满时丢弃最旧帧，保证实时性
//! - 时间戳：每个帧附带微秒级时间戳，用于延迟统计

use crossbeam::queue::ArrayQueue;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

use crate::VOICE_FRAME_SAMPLES;

/// 捕获帧结构
///
/// 包含 PCM 音频数据和时间戳
#[derive(Debug, Clone, Copy)]
pub struct CaptureFrame {
    /// PCM 音频数据 (48kHz, 20ms = 960 samples)
    pub pcm: [i16; VOICE_FRAME_SAMPLES],
    /// 微秒时间戳 (单调递增)
    pub timestamp: i64,
}

impl Default for CaptureFrame {
    fn default() -> Self {
        Self {
            pcm: [0i16; VOICE_FRAME_SAMPLES],
            timestamp: 0,
        }
    }
}

/// 无锁捕获队列
///
/// 基于 crossbeam::queue::ArrayQueue 的无锁 MPMC 队列
/// 默认容量 16 帧，约 320ms 缓冲 (20ms * 16)
pub struct CaptureQueue {
    /// 无锁队列存储帧
    queue: ArrayQueue<CaptureFrame>,
    /// 当前队列长度（近似值，仅用于统计）
    len: AtomicUsize,
    /// 溢出次数统计
    overflow_count: AtomicU64,
    /// 总推送次数统计
    push_count: AtomicU64,
    /// 总弹出次数统计
    pop_count: AtomicU64,
}

impl CaptureQueue {
    /// 创建新的捕获队列
    ///
    /// # 参数
    /// * `capacity` - 队列容量（建议 16，约 320ms 缓冲）
    ///
    /// # 示例
    /// ```
    /// use ddnet_voice::audio::capture_queue::CaptureQueue;
    ///
    /// let queue = CaptureQueue::new(16);
    /// ```
    pub fn new(capacity: usize) -> Self {
        Self {
            queue: ArrayQueue::new(capacity),
            len: AtomicUsize::new(0),
            overflow_count: AtomicU64::new(0),
            push_count: AtomicU64::new(0),
            pop_count: AtomicU64::new(0),
        }
    }

    /// 推入一帧数据
    ///
    /// 生产者方法，由音频回调线程调用。
    /// 如果队列已满，会丢弃最旧的帧并增加溢出计数。
    ///
    /// # 参数
    /// * `frame` - 要推入的捕获帧
    ///
    /// # 返回
    /// * `true` - 成功推入（或发生溢出后成功推入）
    /// * `false` - 推入失败（理论上不会发生）
    ///
    /// # 线程安全
    /// 此方法线程安全，可被多个生产者线程同时调用
    pub fn push(&self, frame: CaptureFrame) -> bool {
        self.push_count.fetch_add(1, Ordering::Relaxed);

        // 尝试直接推入
        if self.queue.push(frame).is_ok() {
            self.len.fetch_add(1, Ordering::Relaxed);
            return true;
        }

        // 队列已满，需要丢弃最旧的帧
        self.handle_overflow();

        // 再次尝试推入（这次应该成功，因为我们已经腾出空间）
        match self.queue.push(frame) {
            Ok(()) => {
                // handle_overflow 减少了 len，现在需要增加回来
                self.len.fetch_add(1, Ordering::Relaxed);
                true
            }
            Err(_) => {
                // 极端情况下可能仍然失败（如多个线程同时操作）
                // 这种概率极低，直接返回 false
                log::warn!("CaptureQueue push failed after overflow handling");
                false
            }
        }
    }

    /// 处理队列溢出
    ///
    /// 丢弃最旧的帧，为新帧腾出空间
    fn handle_overflow(&self) {
        self.overflow_count.fetch_add(1, Ordering::Relaxed);

        // 尝试弹出最旧的帧
        if self.queue.pop().is_some() {
            self.len.fetch_sub(1, Ordering::Relaxed);
        }
    }

    /// 弹出一帧数据
    ///
    /// 消费者方法，由工作线程调用。
    ///
    /// # 返回
    /// * `Some(CaptureFrame)` - 成功弹出一帧
    /// * `None` - 队列为空
    ///
    /// # 线程安全
    /// 此方法线程安全，可被多个消费者线程同时调用
    pub fn pop(&self) -> Option<CaptureFrame> {
        match self.queue.pop() {
            Some(frame) => {
                self.len.fetch_sub(1, Ordering::Relaxed);
                self.pop_count.fetch_add(1, Ordering::Relaxed);
                Some(frame)
            }
            None => None,
        }
    }

    /// 获取当前队列长度
    ///
    /// 注意：这是一个近似值，因为无锁操作的并发特性
    pub fn len(&self) -> usize {
        self.len.load(Ordering::Relaxed)
    }

    /// 检查队列是否为空
    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }

    /// 获取溢出次数统计
    ///
    /// 可用于监控音频采集的实时性
    pub fn overflow_count(&self) -> usize {
        self.overflow_count.load(Ordering::Relaxed) as usize
    }

    /// 获取总推送次数
    pub fn push_count(&self) -> u64 {
        self.push_count.load(Ordering::Relaxed)
    }

    /// 获取总弹出次数
    pub fn pop_count(&self) -> u64 {
        self.pop_count.load(Ordering::Relaxed)
    }

    /// 获取队列容量
    pub fn capacity(&self) -> usize {
        self.queue.capacity()
    }

    /// 清空队列
    pub fn clear(&self) {
        while self.queue.pop().is_some() {
            self.len.fetch_sub(1, Ordering::Relaxed);
        }
    }

    /// 重置所有统计计数器
    pub fn reset_stats(&self) {
        self.overflow_count.store(0, Ordering::Relaxed);
        self.push_count.store(0, Ordering::Relaxed);
        self.pop_count.store(0, Ordering::Relaxed);
    }
}

impl Default for CaptureQueue {
    fn default() -> Self {
        Self::new(16)
    }
}

// 确保 Send + Sync 安全
unsafe impl Send for CaptureQueue {}
unsafe impl Sync for CaptureQueue {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::thread;

    fn create_test_frame(timestamp: i64) -> CaptureFrame {
        CaptureFrame {
            pcm: [100i16; VOICE_FRAME_SAMPLES],
            timestamp,
        }
    }

    #[test]
    fn test_new_queue() {
        let queue = CaptureQueue::new(16);
        assert_eq!(queue.capacity(), 16);
        assert!(queue.is_empty());
        assert_eq!(queue.len(), 0);
        assert_eq!(queue.overflow_count(), 0);
    }

    #[test]
    fn test_default_queue() {
        let queue: CaptureQueue = Default::default();
        assert_eq!(queue.capacity(), 16);
        assert!(queue.is_empty());
    }

    #[test]
    fn test_push_and_pop() {
        let queue = CaptureQueue::new(4);

        // 推入 4 帧
        for i in 0..4 {
            let frame = create_test_frame(i);
            assert!(queue.push(frame));
        }

        assert_eq!(queue.len(), 4);
        assert_eq!(queue.push_count(), 4);

        // 弹出 4 帧
        for i in 0..4 {
            let frame = queue.pop().unwrap();
            assert_eq!(frame.timestamp, i);
        }

        assert!(queue.is_empty());
        assert_eq!(queue.pop_count(), 4);
    }

    #[test]
    fn test_overflow_handling() {
        let queue = CaptureQueue::new(4);

        // 推入 8 帧（超过容量）
        for i in 0..8 {
            let frame = create_test_frame(i);
            assert!(queue.push(frame));
        }

        // 队列长度应该保持为 4
        assert_eq!(queue.len(), 4);
        // 溢出次数应该为 4
        assert_eq!(queue.overflow_count(), 4);
        assert_eq!(queue.push_count(), 8);

        // 弹出的应该是最新的 4 帧 (4, 5, 6, 7)
        for i in 4..8 {
            let frame = queue.pop().unwrap();
            assert_eq!(frame.timestamp, i);
        }
    }

    #[test]
    fn test_pop_empty() {
        let queue = CaptureQueue::new(4);
        assert!(queue.pop().is_none());
    }

    #[test]
    fn test_clear() {
        let queue = CaptureQueue::new(4);

        for i in 0..4 {
            queue.push(create_test_frame(i));
        }

        assert_eq!(queue.len(), 4);

        queue.clear();

        assert!(queue.is_empty());
        assert_eq!(queue.len(), 0);
        assert!(queue.pop().is_none());
    }

    #[test]
    fn test_reset_stats() {
        let queue = CaptureQueue::new(2);

        // 制造一些统计
        for i in 0..4 {
            queue.push(create_test_frame(i));
        }
        queue.pop();

        assert_eq!(queue.overflow_count(), 2);
        assert_eq!(queue.push_count(), 4);
        assert_eq!(queue.pop_count(), 1);

        // 重置统计
        queue.reset_stats();

        assert_eq!(queue.overflow_count(), 0);
        assert_eq!(queue.push_count(), 0);
        assert_eq!(queue.pop_count(), 0);
    }

    #[test]
    fn test_concurrent_push_pop() {
        use std::sync::atomic::{AtomicBool, AtomicU64};
        use std::time::{Duration, Instant};

        let queue = Arc::new(CaptureQueue::new(32));
        let queue_push = queue.clone();
        let queue_pop = queue.clone();

        // 用于跟踪生产者是否完成
        let producer_done = Arc::new(AtomicBool::new(false));
        let producer_done_consumer = producer_done.clone();

        // 生产者推送的总数
        let total_pushed = Arc::new(AtomicU64::new(0));
        let total_pushed_clone = total_pushed.clone();

        // 生产者线程 - 推送100帧（不是1000，避免过多溢出）
        let producer = thread::spawn(move || {
            for i in 0..100 {
                let frame = create_test_frame(i);
                queue_push.push(frame);
                total_pushed_clone.fetch_add(1, Ordering::Relaxed);
                // 短暂延迟，给消费者处理时间
                thread::sleep(Duration::from_micros(10));
            }
            producer_done.store(true, Ordering::SeqCst);
        });

        // 消费者线程
        let consumer = thread::spawn(move || {
            let mut count = 0u64;
            let start = Instant::now();
            let timeout = Duration::from_secs(5);

            loop {
                if start.elapsed() > timeout {
                    panic!("Consumer timed out after 5 seconds");
                }

                // 消费所有可用数据
                while let Some(_frame) = queue_pop.pop() {
                    count += 1;
                }

                // 检查是否应该退出
                let done = producer_done_consumer.load(Ordering::SeqCst);
                if done {
                    // 生产者完成，再尝试消费剩余数据
                    thread::sleep(Duration::from_millis(10));
                    while let Some(_frame) = queue_pop.pop() {
                        count += 1;
                    }
                    break;
                }

                thread::yield_now();
            }
            count
        });

        producer.join().unwrap();
        let consumed = consumer.join().unwrap();

        // 验证：消费数应该不超过推送数
        let total = total_pushed.load(Ordering::Relaxed);
        assert!(
            consumed <= total,
            "Consumed ({}) should not exceed pushed ({})",
            consumed,
            total
        );

        // 验证：消费数 + 队列长度 + 溢出数 = 推送总数
        let remaining = queue.len();
        let overflow = queue.overflow_count() as u64;
        assert_eq!(
            consumed + remaining as u64 + overflow,
            total,
            "consumed({}) + remaining({}) + overflow({}) should equal total({})",
            consumed,
            remaining,
            overflow,
            total
        );

        // 验证推送总数
        assert_eq!(queue.push_count(), 100, "Total push attempts should be 100");

        // 验证统计一致性（push_count = pop_count + overflow_count + 当前队列中的元素）
        let current_len = queue.len();
        let overflow = queue.overflow_count();
        assert_eq!(
            queue.push_count(),
            queue.pop_count() + overflow as u64 + current_len as u64,
            "Push count should equal pop count + overflow count + current queue length"
        );
    }

    #[test]
    fn test_concurrent_multi_producer() {
        use std::sync::atomic::AtomicU64;
        use std::time::{Duration, Instant};

        let queue = Arc::new(CaptureQueue::new(16));
        let producer_done = Arc::new(AtomicU64::new(0));
        let total_pushed = Arc::new(AtomicU64::new(0));
        let timeout = Duration::from_secs(5);

        let mut handles = vec![];

        // 3 个生产者线程
        for thread_id in 0..3 {
            let q = queue.clone();
            let done = producer_done.clone();
            let pushed = total_pushed.clone();
            let handle = thread::spawn(move || {
                for i in 0..100 {
                    let frame = CaptureFrame {
                        pcm: [thread_id as i16; VOICE_FRAME_SAMPLES],
                        timestamp: i,
                    };
                    q.push(frame);
                    pushed.fetch_add(1, Ordering::Relaxed);
                }
                done.fetch_add(1, Ordering::SeqCst);
            });
            handles.push(handle);
        }

        // 主线程监控超时
        let start = Instant::now();
        loop {
            if start.elapsed() > timeout {
                panic!(
                    "Test timed out after {:?}. Producers completed: {}/3, Total pushed: {}",
                    timeout,
                    producer_done.load(Ordering::SeqCst),
                    total_pushed.load(Ordering::Relaxed)
                );
            }

            // 检查所有生产者是否完成
            if producer_done.load(Ordering::SeqCst) >= 3 {
                break;
            }

            thread::yield_now();
        }

        // 等待所有生产者线程完成（此时应该已经完成）
        for (idx, h) in handles.into_iter().enumerate() {
            let remaining_timeout = timeout.saturating_sub(start.elapsed());
            if remaining_timeout.is_zero() {
                panic!("Timeout waiting for producer thread {} to join", idx);
            }
            // 使用 join_timeout 模式：在循环中检查
            let join_start = Instant::now();
            loop {
                if h.is_finished() {
                    h.join().unwrap();
                    break;
                }
                if join_start.elapsed() > Duration::from_millis(100) {
                    // 给每个线程最多 100ms 来 join
                    panic!("Timeout joining producer thread {}", idx);
                }
                thread::yield_now();
            }
        }

        // 验证总数
        assert_eq!(queue.push_count(), 300);

        // 消费所有帧
        let mut count = 0;
        while queue.pop().is_some() {
            count += 1;
        }

        // 由于队列容量限制，应该有一些溢出
        assert!(count <= 16);
        assert!(queue.overflow_count() > 0);
    }

    #[test]
    fn test_capture_frame_default() {
        let frame: CaptureFrame = Default::default();
        assert_eq!(frame.pcm.len(), VOICE_FRAME_SAMPLES);
        assert_eq!(frame.timestamp, 0);
        assert!(frame.pcm.iter().all(|&s| s == 0));
    }

    #[test]
    fn test_capture_frame_clone() {
        let frame1 = CaptureFrame {
            pcm: [42i16; VOICE_FRAME_SAMPLES],
            timestamp: 12345,
        };
        let frame2 = frame1.clone();

        assert_eq!(frame2.timestamp, 12345);
        assert_eq!(frame2.pcm[0], 42);
        assert_eq!(frame2.pcm[VOICE_FRAME_SAMPLES - 1], 42);
    }
}
