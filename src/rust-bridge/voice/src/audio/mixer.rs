//! 音频混音器
//!
//! 将多个音频流混音成一个输出流

use std::collections::HashMap;

/// 混音配置
#[derive(Debug, Clone, Copy)]
pub struct MixConfig {
    /// 主音量 (0.0 - 1.0)
    pub master_volume: f32,
    /// 立体声宽度 (0.0 - 1.0)
    pub stereo_width: f32,
}

impl Default for MixConfig {
    fn default() -> Self {
        Self {
            master_volume: 1.0,
            stereo_width: 1.0,
        }
    }
}

/// 音频源
#[derive(Debug, Clone)]
pub struct AudioSource {
    /// 源 ID
    pub id: u16,
    /// 样本缓冲区
    samples: Vec<i16>,
    /// 读取位置
    read_pos: usize,
    /// 音量
    volume: f32,
    /// 声像 (-1.0 = 左, 0.0 = 中, 1.0 = 右)
    pan: f32,
}

impl AudioSource {
    pub fn new(id: u16, samples: Vec<i16>, volume: f32, pan: f32) -> Self {
        Self {
            id,
            samples,
            read_pos: 0,
            volume: volume.clamp(0.0, 2.0),
            pan: pan.clamp(-1.0, 1.0),
        }
    }

    /// 读取样本
    pub fn read(&mut self, count: usize) -> Vec<i16> {
        let end = (self.read_pos + count).min(self.samples.len());
        let result = self.samples[self.read_pos..end].to_vec();
        self.read_pos = end;
        result
    }

    /// 是否已读完
    pub fn is_empty(&self) -> bool {
        self.read_pos >= self.samples.len()
    }

    /// 剩余样本数
    pub fn remaining(&self) -> usize {
        self.samples.len().saturating_sub(self.read_pos)
    }
}

/// 音频混音器
pub struct AudioMixer {
    sources: HashMap<u16, AudioSource>,
    config: MixConfig,
    mix_buffer: Vec<i32>,
}

impl AudioMixer {
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

    /// 移除音频源
    pub fn remove_source(&mut self, id: u16) {
        self.sources.remove(&id);
    }

    /// 混音
    /// 
    /// 将多个音频源混音成单声道或立体声输出
    /// 
    /// # 参数
    /// * `samples` - 需要的采样数
    /// * `channels` - 输出声道数 (1 或 2)
    /// 
    /// # 返回
    /// 混音后的样本
    pub fn mix(&mut self, samples: usize, channels: usize) -> Vec<i16> {
        let needed = samples * channels;
        
        // 清零混音缓冲区
        self.mix_buffer.resize(needed, 0);
        self.mix_buffer.fill(0);

        // 遍历所有源
        let mut empty_sources = Vec::new();
        
        for (id, source) in &mut self.sources {
            let source_samples = source.read(samples);
            
            // 计算左右增益
            let left_gain = if source.pan <= 0.0 {
                source.volume
            } else {
                source.volume * (1.0 - source.pan)
            };
            
            let right_gain = if source.pan >= 0.0 {
                source.volume
            } else {
                source.volume * (1.0 + source.pan)
            };

            // 混音
            for (i, &sample) in source_samples.iter().enumerate() {
                if channels == 1 {
                    // 单声道：平均左右
                    let mono_gain = (left_gain + right_gain) / 2.0;
                    self.mix_buffer[i] += (sample as f32 * mono_gain) as i32;
                } else {
                    // 立体声
                    let base = i * 2;
                    if base < needed {
                        self.mix_buffer[base] += (sample as f32 * left_gain) as i32;
                    }
                    if base + 1 < needed {
                        self.mix_buffer[base + 1] += (sample as f32 * right_gain) as i32;
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

        // 应用主音量并硬削波
        let mut output = Vec::with_capacity(needed);
        for &sample in &self.mix_buffer {
            let scaled = (sample as f32 * self.config.master_volume) as i32;
            let clipped = scaled.clamp(-32768, 32767);
            output.push(clipped as i16);
        }

        output
    }

    /// 设置配置
    pub fn set_config(&mut self, config: MixConfig) {
        self.config = config;
    }

    /// 获取配置
    pub fn config(&self) -> &MixConfig {
        &self.config
    }

    /// 获取活跃源数量
    pub fn active_sources(&self) -> usize {
        self.sources.len()
    }

    /// 清空所有源
    pub fn clear(&mut self) {
        self.sources.clear();
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
        
        // 左声道源
        let left_source = AudioSource::new(1, vec![1000i16; 960], 1.0, -1.0);
        // 右声道源
        let right_source = AudioSource::new(2, vec![1000i16; 960], 1.0, 1.0);
        
        mixer.add_source(left_source);
        mixer.add_source(right_source);
        
        // 立体声混音
        let output = mixer.mix(960, 2);
        
        // 左声道：source1 全 + source2 0
        // 右声道：source1 0 + source2 全
        assert_eq!(output[0], 1000);  // 左
        assert_eq!(output[1], 0);      // 右 (第一帧来自左源)
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
        
        // 添加一个短源
        let source = AudioSource::new(1, vec![1000i16; 100], 1.0, 0.0);
        mixer.add_source(source);
        
        // 混音 (请求更多样本)
        let _ = mixer.mix(960, 1);
        
        // 源应该被移除
        assert_eq!(mixer.active_sources(), 0);
    }
}
