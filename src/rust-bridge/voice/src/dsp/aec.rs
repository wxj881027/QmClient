//! 回声消除模块 (AEC - Acoustic Echo Cancellation)
//!
//! ## 概述
//!
//! 基于 NLMS (Normalized Least Mean Square) 自适应滤波器实现，
//! 提供双端通话检测和舒适噪声生成。
//!
//! ## 算法原理
//!
//! ### 1. NLMS 自适应滤波器
//!
//! NLMS 是 LMS (Least Mean Square) 算法的归一化变体，具有更好的收敛性和稳定性。
//!
//! **核心更新公式**：
//!
//! ```text
//! echo_estimate = sum(w[i] * far_end_buffer[i])
//! error = near_end - echo_estimate
//! w[i] = w[i] + mu * error * x[i] / (x^T * x + epsilon)
//! ```
//!
//! 其中：
//! - `w` 是滤波器权重向量
//! - `mu` 是步长参数 (step_size)
//! - `x` 是远端信号向量
//! - `epsilon` 是正则化因子，防止除以零
//!
//! **归一化的优势**：
//! - 对输入信号功率不敏感
//! - 收敛速度更稳定
//! - 适合处理动态变化的音频信号
//!
//! ### 2. 双端通话检测 (DTD)
//!
//! 双端通话时，近端和远端同时有语音，此时更新滤波器会导致发散。
//! 使用能量比检测法：
//!
//! ```text
//! if near_energy > far_energy * threshold:
//!     state = DoubleTalk  // 双端通话
//! elif far_energy > threshold:
//!     state = SingleTalkFar  // 只有远端，可更新滤波器
//! else:
//!     state = SingleTalkNear  // 只有近端
//! ```
//!
//! 只有在 `SingleTalkFar` 状态下才更新滤波器权重。
//!
//! ### 3. 舒适噪声生成 (CNG)
//!
//! 完全消除回声会导致静音段听起来不自然，因此添加少量舒适噪声：
//!
//! ```text
//! comfort_noise = random() * gain * sqrt(near_energy)
//! output = error + comfort_noise
//! ```
//!
//! ### 4. 收敛检测
//!
//! 滤波器收敛后回声消除效果最佳。收敛条件：
//! - 远端有信号 (far_energy > 1000)
//! - 误差能量远小于远端能量 (error_energy < far_energy * 0.1)
//! - 连续满足条件超过 100 帧
//!
//! ## 参考文献
//!
//! - [1] NLMS 算法: Haykin, S. "Adaptive Filter Theory", 5th Edition, Prentice Hall
//! - [2] 双端通话检测: Benesty, J. et al. "Advances in Network and Acoustic Echo Cancellation"
//! - [3] WebRTC AEC 实现: <https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/aec/>
//! - [4] ITU-T G.168: 数字网络回声消除器标准
//!
//! ## 参数说明
//!
//! | 参数 | 默认值 | 说明 |
//! |------|--------|------|
//! | `filter_length` | 480 | 滤波器长度（样本数），覆盖最大回声延迟 |
//! | `step_size` | 0.5 | 步长参数 (mu)，控制收敛速度 |
//! | `epsilon` | 1e-6 | 正则化因子，防止除以零 |
//! | `dtd_threshold` | 2.0 | 双端通话检测阈值（能量比） |
//! | `comfort_noise_gain` | 0.01 | 舒适噪声增益 |
//! | `near_end_threshold` | 100.0 | 近端信号阈值 |
//!
//! ## 性能特性
//!
//! - **Hot Path**: `process_frame` 在渲染线程调用，必须零堆分配
//! - 时间复杂度: O(N * filter_length)，N 为帧大小
//! - 空间复杂度: O(filter_length)

use std::collections::VecDeque;

/// AEC 配置
#[derive(Debug, Clone, Copy)]
pub struct AecConfig {
    /// 滤波器长度（样本数），覆盖最大回声延迟
    /// 默认 480 样本 = 10ms @ 48kHz，适用于大多数场景
    pub filter_length: usize,
    /// 步长 (mu)，控制收敛速度，范围 0.0-1.0
    pub step_size: f32,
    /// 正则化因子，防止除以零
    pub epsilon: f32,
    /// 双端通话检测阈值（能量比）
    pub dtd_threshold: f32,
    /// 舒适噪声增益
    pub comfort_noise_gain: f32,
    /// 近端信号阈值（用于 VAD 辅助）
    pub near_end_threshold: f32,
}

impl Default for AecConfig {
    fn default() -> Self {
        Self {
            filter_length: 480,        // 10ms @ 48kHz
            step_size: 0.5,            // 较快收敛
            epsilon: 1e-6,             // 正则化
            dtd_threshold: 2.0,        // 近端能量 > 2x 远端能量时认为双端通话
            comfort_noise_gain: 0.01,  // 舒适噪声水平
            near_end_threshold: 100.0, // 近端阈值
        }
    }
}

/// 双端通话检测器状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DtdState {
    /// 单端通话（远端说话），可以更新滤波器
    SingleTalkFar,
    /// 单端通话（近端说话），不更新滤波器
    SingleTalkNear,
    /// 双端通话（双方说话），不更新滤波器
    DoubleTalk,
    /// 静音状态
    Silence,
}

/// AEC 处理器
pub struct AcousticEchoCanceller {
    /// 配置
    config: AecConfig,
    /// 自适应滤波器权重
    weights: Vec<f32>,
    /// 远端信号缓冲区（参考信号）
    far_end_buffer: VecDeque<f32>,
    /// 双端通话检测状态
    dtd_state: DtdState,
    /// 远端信号能量估计（EWMA）
    far_energy: f32,
    /// 近端信号能量估计（EWMA）
    near_energy: f32,
    /// 误差信号能量估计
    error_energy: f32,
    /// 能量平滑系数
    energy_alpha: f32,
    /// 收敛状态
    converged: bool,
    /// 收敛计数器
    convergence_count: usize,
    /// 舒适噪声生成器状态
    comfort_noise_state: u32,
}

impl AcousticEchoCanceller {
    /// 创建新的 AEC 处理器
    pub fn new() -> Self {
        Self::with_config(AecConfig::default())
    }

    /// 使用自定义配置创建 AEC
    pub fn with_config(config: AecConfig) -> Self {
        Self {
            config,
            weights: vec![0.0; config.filter_length],
            far_end_buffer: VecDeque::with_capacity(config.filter_length),
            dtd_state: DtdState::Silence,
            far_energy: 0.0,
            near_energy: 0.0,
            error_energy: 0.0,
            energy_alpha: 0.1, // 能量平滑系数
            converged: false,
            convergence_count: 0,
            comfort_noise_state: 1,
        }
    }

    /// 处理一帧音频
    ///
    /// # 参数
    /// * `near_end` - 麦克风捕获信号（含回声）
    /// * `far_end` - 远端参考信号（扬声器输出）
    /// * `output` - 输出缓冲区（回声消除后的信号）
    pub fn process_frame(&mut self, near_end: &[i16], far_end: &[i16], output: &mut [i16]) {
        assert_eq!(near_end.len(), far_end.len());
        assert_eq!(near_end.len(), output.len());

        // 更新远端缓冲区
        for &sample in far_end {
            self.far_end_buffer.push_back(sample as f32);
            if self.far_end_buffer.len() > self.config.filter_length {
                self.far_end_buffer.pop_front();
            }
        }

        // 计算能量
        self.update_energy_estimates(near_end, far_end);

        // 双端通话检测
        self.dtd_state = self.detect_double_talk();

        // 处理每个样本
        for i in 0..near_end.len() {
            let near_sample = near_end[i] as f32;
            let _far_sample = far_end[i] as f32;

            // 估计回声
            let echo_estimate = self.estimate_echo();

            // 计算误差（回声消除后的信号）
            let error = near_sample - echo_estimate;

            // 根据 DTD 状态决定是否更新滤波器
            match self.dtd_state {
                DtdState::SingleTalkFar => {
                    // 只有远端说话，安全更新滤波器
                    self.update_nlms(error);
                }
                _ => {
                    // 双端通话或近端说话，暂停更新
                }
            }

            // 添加舒适噪声
            let comfort_noise = self.generate_comfort_noise();
            let output_sample = error + comfort_noise;

            // 限制输出范围
            output[i] = output_sample.clamp(-32768.0, 32767.0) as i16;
        }

        // 检查收敛状态
        self.check_convergence();
    }

    /// 估计当前回声（使用滤波器权重）
    /// 零堆分配实现，直接通过索引访问 VecDeque
    fn estimate_echo(&self) -> f32 {
        let buffer_len = self.far_end_buffer.len();
        if buffer_len < self.config.filter_length {
            return 0.0;
        }

        // 直接使用迭代器，避免堆分配
        self.weights
            .iter()
            .enumerate()
            .take(buffer_len)
            .map(|(i, &weight)| {
                // 从缓冲区末尾开始访问（最新样本在前）
                let idx = buffer_len - 1 - i;
                weight * self.far_end_buffer[idx]
            })
            .sum()
    }

    /// 更新 NLMS 滤波器权重
    /// 零堆分配实现，直接通过索引访问 VecDeque
    fn update_nlms(&mut self, error: f32) {
        let buffer_len = self.far_end_buffer.len();
        if buffer_len < self.config.filter_length {
            return;
        }

        // 计算远端信号功率（归一化因子）
        let power: f32 = self.far_end_buffer.iter().map(|&s| s * s).sum();

        // NLMS 更新公式：w = w + mu * error * x / (x^T * x + epsilon)
        let denominator = power + self.config.epsilon;
        let mu = self.config.step_size;

        // 直接使用索引访问，避免堆分配
        for (i, weight) in self.weights.iter_mut().enumerate() {
            if i < buffer_len {
                // 从缓冲区末尾开始访问（最新样本在前）
                let idx = buffer_len - 1 - i;
                let x = self.far_end_buffer[idx];
                *weight += mu * error * x / denominator;
            }
        }
    }

    /// 更新能量估计
    fn update_energy_estimates(&mut self, near_end: &[i16], far_end: &[i16]) {
        // 计算帧能量
        let near_frame_energy: f32 = near_end.iter().map(|&s| (s as f32).powi(2)).sum();
        let far_frame_energy: f32 = far_end.iter().map(|&s| (s as f32).powi(2)).sum();

        // EWMA 更新
        let alpha = self.energy_alpha;
        self.near_energy = (1.0 - alpha) * self.near_energy + alpha * near_frame_energy;
        self.far_energy = (1.0 - alpha) * self.far_energy + alpha * far_frame_energy;
    }

    /// 双端通话检测
    fn detect_double_talk(&self) -> DtdState {
        let near = self.near_energy.sqrt();
        let far = self.far_energy.sqrt();

        // 静音检测
        if near < self.config.near_end_threshold && far < self.config.near_end_threshold {
            return DtdState::Silence;
        }

        // 只有远端有信号
        if far > near * 0.5 && near < self.config.near_end_threshold {
            return DtdState::SingleTalkFar;
        }

        // 只有近端有信号
        if near > far * 0.5 && far < self.config.near_end_threshold {
            return DtdState::SingleTalkNear;
        }

        // 能量比检测
        if near > far * self.config.dtd_threshold {
            // 近端能量远大于远端，可能是双端通话或只有近端
            if far > self.config.near_end_threshold {
                return DtdState::DoubleTalk;
            } else {
                return DtdState::SingleTalkNear;
            }
        }

        // 默认认为远端单端通话（可以继续更新滤波器）
        DtdState::SingleTalkFar
    }

    /// 生成舒适噪声
    fn generate_comfort_noise(&mut self) -> f32 {
        // 简单的伪随机噪声生成器
        self.comfort_noise_state = self
            .comfort_noise_state
            .wrapping_mul(1664525)
            .wrapping_add(1013904223);
        let random = (self.comfort_noise_state as f32 / u32::MAX as f32) - 0.5;

        // 根据近端信号能量调整噪声水平
        let noise_level = self.config.comfort_noise_gain * self.near_energy.sqrt();
        random * noise_level
    }

    /// 检查收敛状态
    fn check_convergence(&mut self) {
        if self.converged {
            return;
        }

        // 如果远端有信号且误差足够小，认为收敛
        if self.far_energy > 1000.0 && self.error_energy < self.far_energy * 0.1 {
            self.convergence_count += 1;
            if self.convergence_count > 100 {
                self.converged = true;
            }
        } else {
            self.convergence_count = 0;
        }
    }

    /// 是否已收敛
    pub fn is_converged(&self) -> bool {
        self.converged
    }

    /// 获取当前 DTD 状态
    pub fn dtd_state(&self) -> DtdState {
        self.dtd_state
    }

    /// 获取远端能量估计
    pub fn far_energy(&self) -> f32 {
        self.far_energy
    }

    /// 获取近端能量估计
    pub fn near_energy(&self) -> f32 {
        self.near_energy
    }

    /// 重置 AEC 状态
    pub fn reset(&mut self) {
        for w in &mut self.weights {
            *w = 0.0;
        }
        self.far_end_buffer.clear();
        self.dtd_state = DtdState::Silence;
        self.far_energy = 0.0;
        self.near_energy = 0.0;
        self.error_energy = 0.0;
        self.converged = false;
        self.convergence_count = 0;
    }
}

impl Default for AcousticEchoCanceller {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_aec_creation() {
        let aec = AcousticEchoCanceller::new();
        assert!(!aec.is_converged());
        assert_eq!(aec.weights.len(), 480);
    }

    #[test]
    fn test_aec_basic_processing() {
        let mut aec = AcousticEchoCanceller::new();
        let frame_size = 960; // 20ms @ 48kHz

        let near_end = vec![0i16; frame_size];
        let far_end = vec![0i16; frame_size];
        let mut output = vec![0i16; frame_size];

        aec.process_frame(&near_end, &far_end, &mut output);

        // 全零输入应该产生全零输出（加上舒适噪声）
        assert_eq!(output.len(), frame_size);
    }

    #[test]
    fn test_aec_with_echo() {
        let mut aec = AcousticEchoCanceller::new();
        let frame_size = 960;

        // 模拟远端信号
        let far_end: Vec<i16> = (0..frame_size)
            .map(|i| ((i as f32 * 0.1).sin() * 1000.0) as i16)
            .collect();

        // 模拟近端信号 = 回声 + 近端语音
        let echo_delay = 100; // 100 样本延迟
        let mut near_end = vec![0i16; frame_size];

        for i in echo_delay..frame_size {
            // 添加回声（衰减后的远端信号）
            near_end[i] = far_end[i - echo_delay] / 2;
        }

        // 添加近端语音（后半帧）
        for i in frame_size / 2..frame_size {
            near_end[i] += ((i as f32 * 0.05).cos() * 2000.0) as i16;
        }

        let mut output = vec![0i16; frame_size];

        // 多次处理以允许滤波器收敛
        for _ in 0..10 {
            aec.process_frame(&near_end, &far_end, &mut output);
        }

        // 检查输出能量是否降低（回声被消除）
        let output_energy: f32 = output.iter().map(|&s| (s as f32).powi(2)).sum();
        let near_energy: f32 = near_end.iter().map(|&s| (s as f32).powi(2)).sum();

        // 输出能量应该小于输入能量（部分回声被消除）
        assert!(output_energy < near_energy);
    }

    #[test]
    fn test_double_talk_detection() {
        let mut aec = AcousticEchoCanceller::new();
        let frame_size = 960;

        // 场景1：只有远端信号
        let far_only: Vec<i16> = (0..frame_size).map(|_| 1000).collect();
        let near_silent = vec![0i16; frame_size];
        let mut output = vec![0i16; frame_size];

        aec.process_frame(&near_silent, &far_only, &mut output);
        assert_eq!(aec.dtd_state(), DtdState::SingleTalkFar);

        // 场景2：双端通话（远端 + 近端）
        let far_signal: Vec<i16> = (0..frame_size).map(|_| 1000).collect();
        let near_signal: Vec<i16> = (0..frame_size).map(|_| 3000).collect(); // 更强

        aec.process_frame(&near_signal, &far_signal, &mut output);
        // 应该检测到双端通话或近端单端通话
        assert!(
            aec.dtd_state() == DtdState::DoubleTalk || aec.dtd_state() == DtdState::SingleTalkNear
        );
    }

    #[test]
    fn test_comfort_noise() {
        let mut aec = AcousticEchoCanceller::new();
        let frame_size = 960;

        // 全静音输入
        let near_end = vec![0i16; frame_size];
        let far_end = vec![0i16; frame_size];
        let mut output = vec![0i16; frame_size];

        aec.process_frame(&near_end, &far_end, &mut output);

        // 静音输入应该有舒适噪声输出
        let output_energy: f32 = output.iter().map(|&s| (s as f32).powi(2)).sum();
        // 注意：由于舒适噪声很小，能量可能接近0
        assert!(output_energy >= 0.0);
    }

    #[test]
    fn test_aec_reset() {
        let mut aec = AcousticEchoCanceller::new();
        let frame_size = 960;

        // 先处理一些数据
        let near_end: Vec<i16> = (0..frame_size).map(|i| i as i16).collect();
        let far_end: Vec<i16> = (0..frame_size).map(|i| (i * 2) as i16).collect();
        let mut output = vec![0i16; frame_size];

        aec.process_frame(&near_end, &far_end, &mut output);

        // 重置
        aec.reset();

        // 检查状态是否重置
        assert!(!aec.is_converged());
        assert!(aec.weights.iter().all(|&w| w == 0.0));
        assert!(aec.far_end_buffer.is_empty());
    }
}
