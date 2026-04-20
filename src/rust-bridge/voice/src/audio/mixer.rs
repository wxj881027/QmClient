//! 音频混音器
//!
//! 将多个音频流混音成一个输出流
//! 支持空间音频处理（距离衰减、立体声声像）
//! 参考 C++ voice_core.cpp 的 MixAudio 实现

use std::collections::HashMap;

use super::engine::FRAME_SAMPLES;

/// 混音配置
#[derive(Debug, Clone, Copy)]
pub struct MixConfig {
    /// 主音量 (0.0 - 2.0)
    pub master_volume: f32,
    /// 立体声宽度 (0.0 - 2.0)
    pub stereo_width: f32,
    /// 是否启用软削波
    pub soft_clip: bool,
}

impl Default for MixConfig {
    fn default() -> Self {
        Self {
            master_volume: 1.0,
            stereo_width: 1.0,
            soft_clip: true,
        }
    }
}

/// 音频源状态
#[derive(Debug, Clone)]
pub struct AudioSource {
    /// 源 ID
    pub id: u16,
    /// 样本缓冲区
    samples: Vec<i16>,
    /// 读取位置
    read_pos: usize,
    /// 音量 (0.0 - 4.0)
    volume: f32,
    /// 声像 (-1.0 = 左, 0.0 = 中, 1.0 = 右)
    #[allow(dead_code)]
    pan: f32,
    /// 左声道增益
    left_gain: f32,
    /// 右声道增益
    right_gain: f32,
    /// 是否循环
    looping: bool,
}

impl AudioSource {
    /// 创建新的音频源
    pub fn new(id: u16, samples: Vec<i16>, volume: f32, pan: f32) -> Self {
        Self {
            id,
            samples,
            read_pos: 0,
            volume: volume.clamp(0.0, 4.0),
            pan: pan.clamp(-1.0, 1.0),
            left_gain: 1.0,
            right_gain: 1.0,
            looping: false,
        }
    }

    /// 创建带空间音频增益的音频源
    pub fn with_spatial(id: u16, samples: Vec<i16>, left_gain: f32, right_gain: f32) -> Self {
        Self {
            id,
            samples,
            read_pos: 0,
            volume: 1.0,
            pan: 0.0,
            left_gain: left_gain.clamp(0.0, 4.0),
            right_gain: right_gain.clamp(0.0, 4.0),
            looping: false,
        }
    }

    /// 创建单帧音频源
    pub fn from_frame(
        id: u16,
        frame: &[i16; FRAME_SAMPLES],
        left_gain: f32,
        right_gain: f32,
    ) -> Self {
        Self::with_spatial(id, frame.to_vec(), left_gain, right_gain)
    }

    /// 读取样本
    pub fn read(&mut self, count: usize) -> Vec<i16> {
        let end = (self.read_pos + count).min(self.samples.len());
        let result = self.samples[self.read_pos..end].to_vec();
        self.read_pos = end;

        // 如果循环且读完，重置
        if self.looping && self.read_pos >= self.samples.len() {
            self.read_pos = 0;
        }

        result
    }

    /// 是否已读完
    pub fn is_empty(&self) -> bool {
        !self.looping && self.read_pos >= self.samples.len()
    }

    /// 剩余样本数
    pub fn remaining(&self) -> usize {
        if self.looping {
            return self.samples.len();
        }
        self.samples.len().saturating_sub(self.read_pos)
    }

    /// 设置循环
    pub fn set_looping(&mut self, looping: bool) {
        self.looping = looping;
    }

    /// 重置读取位置
    pub fn reset(&mut self) {
        self.read_pos = 0;
    }
}

/// 空间音频参数
#[derive(Debug, Clone, Copy)]
pub struct SpatialParams {
    /// 距离因子 (0.0 - 1.0)
    pub distance_factor: f32,
    /// 左声道增益
    pub left_gain: f32,
    /// 右声道增益
    pub right_gain: f32,
}

impl Default for SpatialParams {
    fn default() -> Self {
        Self {
            distance_factor: 1.0,
            left_gain: 1.0,
            right_gain: 1.0,
        }
    }
}

/// 计算空间音频参数
///
/// 参考 C++ voice_core.cpp 的空间音频计算
///
/// # 参数
/// * `local_pos` - 本地玩家位置 (x, y)
/// * `sender_pos` - 发送者位置 (x, y)
/// * `radius` - 语音半径
/// * `stereo_width` - 立体声宽度
/// * `volume` - 基础音量
pub fn calculate_spatial_params(
    local_pos: (f32, f32),
    sender_pos: (f32, f32),
    radius: f32,
    stereo_width: f32,
    volume: f32,
) -> SpatialParams {
    let dx = sender_pos.0 - local_pos.0;
    let dy = sender_pos.1 - local_pos.1;
    let distance = (dx * dx + dy * dy).sqrt();

    // 检查是否在范围内
    if distance > radius {
        return SpatialParams {
            distance_factor: 0.0,
            left_gain: 0.0,
            right_gain: 0.0,
        };
    }

    // 距离衰减 (线性)
    let distance_factor = 1.0 - (distance / radius);
    let base_volume = distance_factor * volume;

    // 立体声声像 (基于 X 轴偏移)
    let pan = (dx / radius).clamp(-1.0, 1.0) * stereo_width;

    // 计算左右增益
    // 参考 C++ 实现:
    // LeftGain = Volume * (Pan <= 0 ? 1.0 : (1.0 - Pan))
    // RightGain = Volume * (Pan >= 0 ? 1.0 : (1.0 + Pan))
    let left_gain = base_volume * if pan <= 0.0 { 1.0 } else { 1.0 - pan };
    let right_gain = base_volume * if pan >= 0.0 { 1.0 } else { 1.0 + pan };

    SpatialParams {
        distance_factor,
        left_gain,
        right_gain,
    }
}

/// 音频混音器
///
/// 参考 C++ voice_core.cpp 的 MixAudio 实现
pub struct AudioMixer {
    /// 音频源
    sources: HashMap<u16, AudioSource>,
    /// 配置
    config: MixConfig,
    /// 混音缓冲区 (使用 i32 避免溢出)
    mix_buffer: Vec<i32>,
}

impl AudioMixer {
    /// 创建新的混音器
    pub fn new() -> Self {
        Self {
            sources: HashMap::new(),
            config: MixConfig::default(),
            mix_buffer: Vec::new(),
        }
    }

    /// 添加音频源
    pub fn add_source(&mut self, source: AudioSource) {
        self.sources.insert(source.id, source);
    }

    /// 添加单帧音频源（便捷方法）
    pub fn add_frame(
        &mut self,
        id: u16,
        frame: &[i16; FRAME_SAMPLES],
        left_gain: f32,
        right_gain: f32,
    ) {
        let source = AudioSource::from_frame(id, frame, left_gain, right_gain);
        self.add_source(source);
    }

    /// 移除音频源
    pub fn remove_source(&mut self, id: u16) {
        self.sources.remove(&id);
    }

    /// 更新音频源的空间音频参数
    pub fn update_spatial(&mut self, id: u16, left_gain: f32, right_gain: f32) {
        if let Some(source) = self.sources.get_mut(&id) {
            source.left_gain = left_gain.clamp(0.0, 4.0);
            source.right_gain = right_gain.clamp(0.0, 4.0);
        }
    }

    /// 混音到预分配缓冲区（零分配版本）
    ///
    /// 参考 C++ voice_core.cpp 的 MixAudio 实现
    ///
    /// # 参数
    /// * `output` - 预分配的输出缓冲区，大小应为 `samples * channels`
    /// * `samples` - 需要的采样数
    /// * `channels` - 输出声道数 (1 或 2)
    ///
    /// # 返回
    /// 实际混音的采样数
    pub fn mix_into(&mut self, output: &mut [i16], samples: usize, channels: usize) -> usize {
        let needed = samples * channels;

        // 检查输出缓冲区大小
        if output.len() < needed {
            return 0;
        }

        // 清零混音缓冲区
        self.mix_buffer.resize(needed, 0);
        self.mix_buffer.fill(0);

        // 遍历所有源
        let mut empty_sources = Vec::new();

        for (id, source) in &mut self.sources {
            let source_samples = source.read(samples);

            if source_samples.is_empty() {
                if source.is_empty() {
                    empty_sources.push(*id);
                }
                continue;
            }

            // 获取增益
            let left_gain = source.left_gain * source.volume;
            let right_gain = source.right_gain * source.volume;

            // 混音
            // 参考 C++ 实现:
            // if (OutputChannels == 1) {
            //     const float MonoGain = 0.5f * (LeftGain + RightGain);
            //     m_MixBuffer[Base] += (int32_t)(Pcm * MonoGain);
            // } else {
            //     m_MixBuffer[Base] += (int32_t)(Pcm * LeftGain);
            //     m_MixBuffer[Base + 1] += (int32_t)(Pcm * RightGain);
            // }

            for (i, &sample) in source_samples.iter().enumerate() {
                if channels == 1 {
                    // 单声道：平均左右
                    let mono_gain = 0.5 * (left_gain + right_gain);
                    self.mix_buffer[i] += (sample as f32 * mono_gain) as i32;
                } else {
                    // 立体声
                    let base = i * channels;
                    if base < needed {
                        self.mix_buffer[base] += (sample as f32 * left_gain) as i32;
                    }
                    if base + 1 < needed {
                        self.mix_buffer[base + 1] += (sample as f32 * right_gain) as i32;
                    }
                    // 多声道支持
                    if channels > 2 {
                        let center = (sample as f32 * 0.5 * (left_gain + right_gain)) as i32;
                        for ch in 2..channels {
                            if base + ch < needed {
                                self.mix_buffer[base + ch] += center;
                            }
                        }
                    }
                }
            }

            if source.is_empty() {
                empty_sources.push(*id);
            }
        }

        // 移除已空的源
        for id in empty_sources {
            self.sources.remove(&id);
        }

        // 应用主音量并输出
        if self.config.soft_clip {
            // 软削波 (使用 tanh 曲线)
            for (i, &sample) in self.mix_buffer.iter().enumerate() {
                let scaled = sample as f32 * self.config.master_volume;
                // 软削波: tanh(x / 32767) * 32767
                let soft = (scaled / 32767.0).tanh() * 32767.0;
                output[i] = soft.clamp(-32768.0, 32767.0) as i16;
            }
        } else {
            // 硬削波
            for (i, &sample) in self.mix_buffer.iter().enumerate() {
                let scaled = (sample as f32 * self.config.master_volume) as i32;
                output[i] = scaled.clamp(-32768, 32767) as i16;
            }
        }

        samples
    }

    /// 混音
    ///
    /// 参考 C++ voice_core.cpp 的 MixAudio 实现
    ///
    /// # 参数
    /// * `samples` - 需要的采样数
    /// * `channels` - 输出声道数 (1 或 2)
    ///
    /// # 返回
    /// 混音后的样本
    pub fn mix(&mut self, samples: usize, channels: usize) -> Vec<i16> {
        let needed = samples * channels;
        let mut output = vec![0i16; needed];

        // 使用 mix_into 进行实际混音
        self.mix_into(&mut output, samples, channels);

        output
    }

    /// 混音并返回帧
    pub fn mix_frame(&mut self, channels: usize) -> [i16; FRAME_SAMPLES] {
        let mixed = self.mix(FRAME_SAMPLES, channels);
        let mut frame = [0i16; FRAME_SAMPLES];

        if channels == 1 {
            let copy_len = mixed.len().min(FRAME_SAMPLES);
            frame[..copy_len].copy_from_slice(&mixed[..copy_len]);
        } else {
            // 立体声：取左声道或平均
            for (i, frame_slot) in frame.iter_mut().enumerate().take(FRAME_SAMPLES) {
                let base = i * channels;
                if base < mixed.len() {
                    *frame_slot = mixed[base];
                }
            }
        }

        frame
    }

    /// 设置配置
    pub fn set_config(&mut self, config: MixConfig) {
        self.config = config;
    }

    /// 获取配置
    pub fn config(&self) -> &MixConfig {
        &self.config
    }

    /// 设置主音量
    pub fn set_master_volume(&mut self, volume: f32) {
        self.config.master_volume = volume.clamp(0.0, 2.0);
    }

    /// 设置立体声宽度
    pub fn set_stereo_width(&mut self, width: f32) {
        self.config.stereo_width = width.clamp(0.0, 2.0);
    }

    /// 获取活跃源数量
    pub fn active_sources(&self) -> usize {
        self.sources.len()
    }

    /// 检查是否有活跃源
    pub fn has_sources(&self) -> bool {
        !self.sources.is_empty()
    }

    /// 清空所有源
    pub fn clear(&mut self) {
        self.sources.clear();
    }

    /// 获取源 ID 列表
    pub fn source_ids(&self) -> Vec<u16> {
        self.sources.keys().copied().collect()
    }
}

impl Default for AudioMixer {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mixer_single_source() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false; // 禁用软削波以进行精确测试

        // 添加一个源
        let source = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        mixer.add_source(source);

        // 混音
        let output = mixer.mix(960, 1);

        assert_eq!(output.len(), 960);
        assert_eq!(output[0], 1000);
    }

    #[test]
    fn test_mixer_multiple_sources() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false; // 禁用软削波以进行精确测试

        // 添加两个源
        let source1 = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        let source2 = AudioSource::new(2, vec![2000i16; 960], 0.5, 0.0);
        mixer.add_source(source1);
        mixer.add_source(source2);

        // 混音
        let output = mixer.mix(960, 1);

        // 1000 + 2000 * 0.5 = 2000
        assert_eq!(output[0], 2000);
    }

    #[test]
    fn test_mixer_stereo() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false; // 禁用软削波以进行精确测试

        // 左声道源
        let left_source = AudioSource::with_spatial(1, vec![1000i16; 960], 1.0, 0.0);
        // 右声道源
        let right_source = AudioSource::with_spatial(2, vec![1000i16; 960], 0.0, 1.0);

        mixer.add_source(left_source);
        mixer.add_source(right_source);

        // 立体声混音
        let output = mixer.mix(960, 2);

        assert_eq!(output.len(), 1920);
        // 左声道：source1 全 + source2 0
        assert_eq!(output[0], 1000);
        // 右声道：source1 0 + source2 全
        assert_eq!(output[1], 1000);
    }

    #[test]
    fn test_mixer_clipping() {
        let mut mixer = AudioMixer::new();

        // 添加一个高电平源
        let source = AudioSource::new(1, vec![30000i16; 960], 2.0, 0.0);
        mixer.add_source(source);

        // 混音
        let output = mixer.mix(960, 1);

        // 应该被削波到 i16 范围
        assert!(output.iter().all(|&s| s <= 32767 && s >= -32768));
    }

    #[test]
    fn test_mixer_remove_empty() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false;

        // 添加一个短源
        let source = AudioSource::new(1, vec![1000i16; 100], 1.0, 0.0);
        mixer.add_source(source);

        // 混音 (请求更多样本)
        let _ = mixer.mix(960, 1);

        // 源应该被移除
        assert_eq!(mixer.active_sources(), 0);
    }

    #[test]
    fn test_calculate_spatial_params() {
        // 中心位置
        let params = calculate_spatial_params((0.0, 0.0), (0.0, 0.0), 1600.0, 1.0, 1.0);
        assert!((params.distance_factor - 1.0).abs() < 0.01);
        assert!((params.left_gain - 1.0).abs() < 0.01);
        assert!((params.right_gain - 1.0).abs() < 0.01);

        // 左侧
        let params = calculate_spatial_params((0.0, 0.0), (-800.0, 0.0), 1600.0, 1.0, 1.0);
        assert!(params.left_gain > params.right_gain);

        // 右侧
        let params = calculate_spatial_params((0.0, 0.0), (800.0, 0.0), 1600.0, 1.0, 1.0);
        assert!(params.right_gain > params.left_gain);

        // 超出范围
        let params = calculate_spatial_params((0.0, 0.0), (2000.0, 0.0), 1600.0, 1.0, 1.0);
        assert_eq!(params.distance_factor, 0.0);
        assert_eq!(params.left_gain, 0.0);
        assert_eq!(params.right_gain, 0.0);
    }

    #[test]
    fn test_soft_clip() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = true;

        // 添加高电平源
        let source = AudioSource::new(1, vec![32767i16; 960], 2.0, 0.0);
        mixer.add_source(source);

        let output = mixer.mix(960, 1);

        // 软削波应该限制输出
        assert!(output.iter().all(|&s| s <= 32767 && s >= -32768));
    }

    #[test]
    fn test_add_frame() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false; // 禁用软削波以进行精确测试
        let frame = [1000i16; FRAME_SAMPLES];

        mixer.add_frame(1, &frame, 1.0, 1.0);

        assert_eq!(mixer.active_sources(), 1);

        let output = mixer.mix(960, 1);
        assert_eq!(output[0], 1000);
    }

    #[test]
    fn test_mix_into_basic() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false;

        // 添加一个源
        let source = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        mixer.add_source(source);

        // 使用预分配缓冲区
        let mut output = [0i16; 960];
        let mixed_samples = mixer.mix_into(&mut output, 960, 1);

        assert_eq!(mixed_samples, 960);
        assert_eq!(output[0], 1000);
    }

    #[test]
    fn test_mix_into_stereo() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false;

        // 左声道源
        let left_source = AudioSource::with_spatial(1, vec![1000i16; 960], 1.0, 0.0);
        mixer.add_source(left_source);

        // 使用预分配缓冲区（立体声）
        let mut output = [0i16; 1920];
        let mixed_samples = mixer.mix_into(&mut output, 960, 2);

        assert_eq!(mixed_samples, 960);
        // 左声道有声音
        assert_eq!(output[0], 1000);
        // 右声道静音
        assert_eq!(output[1], 0);
    }

    #[test]
    fn test_mix_into_insufficient_buffer() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = false;

        let source = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        mixer.add_source(source);

        // 缓冲区太小
        let mut output = [0i16; 100];
        let mixed_samples = mixer.mix_into(&mut output, 960, 1);

        // 应该返回 0 表示失败
        assert_eq!(mixed_samples, 0);
    }

    #[test]
    fn test_mix_into_soft_clip() {
        let mut mixer = AudioMixer::new();
        mixer.config.soft_clip = true;

        // 添加高电平源
        let source = AudioSource::new(1, vec![32767i16; 960], 2.0, 0.0);
        mixer.add_source(source);

        let mut output = [0i16; 960];
        mixer.mix_into(&mut output, 960, 1);

        // 软削波应该限制输出
        assert!(output.iter().all(|&s| s <= 32767 && s >= -32768));
    }

    #[test]
    fn test_mix_into_matches_mix() {
        let mut mixer1 = AudioMixer::new();
        let mut mixer2 = AudioMixer::new();
        mixer1.config.soft_clip = false;
        mixer2.config.soft_clip = false;

        // 添加相同的源
        let source1 = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        let source2 = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
        mixer1.add_source(source1);
        mixer2.add_source(source2);

        // 使用 mix()
        let output1 = mixer1.mix(960, 1);

        // 使用 mix_into()
        let mut output2 = [0i16; 960];
        mixer2.mix_into(&mut output2, 960, 1);

        // 结果应该相同
        assert_eq!(output1.as_slice(), &output2[..]);
    }
}
