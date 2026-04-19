//! C++ 桥接层
//!
//! 使用 cxx 提供 Rust 与 C++ 之间的 FFI 接口

use crate::VoiceConfig;
use crate::dsp::DspChain;
use crate::codec::OpusCodec;
use crate::network::protocol::{calculate_context_hash, calculate_token_hash};
use crate::spatial::{calculate_spatial, SpatialConfig};

#[cxx::bridge]
mod ffi {
    // Rust 暴露给 C++ 的接口
    extern "Rust" {
        /// 初始化语音系统
        fn voice_init() -> bool;
        
        /// 关闭语音系统
        fn voice_shutdown();
        
        /// 创建 DSP 处理器，返回不透明句柄
        fn voice_dsp_create() -> usize;
        
        /// 销毁 DSP 处理器
        fn voice_dsp_destroy(dsp: usize);
        
        /// 处理音频帧 (DSP)
        fn voice_dsp_process(dsp: usize, samples: &mut [i16], config: &VoiceConfigFFI);
        
        /// 创建 Opus 编解码器，返回不透明句柄
        fn voice_opus_create() -> usize;
        
        /// 销毁 Opus 编解码器
        fn voice_opus_destroy(codec: usize);
        
        /// 编码音频帧
        fn voice_opus_encode(codec: usize, pcm: &[i16], output: &mut [u8]) -> Result<usize>;
        
        /// 解码音频帧
        fn voice_opus_decode(codec: usize, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize>;
        
        /// 设置比特率
        fn voice_opus_set_bitrate(codec: usize, bitrate: i32) -> Result<()>;
        
        /// 计算 3D 空间音频
        fn voice_calculate_spatial(
            local_x: f32, local_y: f32,
            sender_x: f32, sender_y: f32,
            radius: f32, stereo_width: f32, volume: f32
        ) -> SpatialResultFFI;
        
        /// 计算上下文哈希
        fn voice_context_hash(server_addr: &str) -> u32;
        
        /// 计算 Token 哈希
        fn voice_token_hash(token: &str) -> u32;
    }
    
    // C++ 暴露给 Rust 的接口
    unsafe extern "C++" {
        include!("game/client/components/qmclient/voice_callback.h");
        
        /// 回调：收到音频数据
        fn on_voice_packet(data: &[u8], sender_id: u16);
        
        /// 回调：玩家开始/停止说话
        fn on_voice_state_change(client_id: u16, speaking: bool);
        
        /// 回调：获取当前时间 (microseconds)
        fn time_get() -> i64;
        
        /// 回调：获取时间频率 (ticks per second)
        fn time_freq() -> i64;
    }
    
    /// 语音配置 FFI 结构
    struct VoiceConfigFFI {
        mic_volume: i32,
        noise_suppress: bool,
        noise_suppress_strength: i32,
        comp_threshold: i32,
        comp_ratio: i32,
        comp_attack_ms: i32,
        comp_release_ms: i32,
        comp_makeup: i32,
        vad_enable: bool,
        vad_threshold: i32,
        vad_release_delay_ms: i32,
        stereo: bool,
        stereo_width: i32,
        volume: i32,
        radius: i32,
    }
    
    /// 空间音频结果 FFI 结构
    struct SpatialResultFFI {
        distance_factor: f32,
        left_gain: f32,
        right_gain: f32,
        in_range: bool,
    }
}

// FFI 函数实现

fn voice_init() -> bool {
    crate::init()
}

fn voice_shutdown() {
    crate::shutdown()
}

fn voice_dsp_create() -> usize {
    let dsp = Box::new(DspChain::new());
    Box::into_raw(dsp) as usize
}

fn voice_dsp_destroy(dsp: usize) {
    if dsp != 0 {
        unsafe {
            let _ = Box::from_raw(dsp as *mut DspChain);
        }
    }
}

fn voice_dsp_process(dsp: usize, samples: &mut [i16], config_ffi: &ffi::VoiceConfigFFI) {
    if dsp == 0 {
        return;
    }
    
    let dsp = unsafe { &mut *(dsp as *mut DspChain) };
    
    let config = VoiceConfig {
        mic_volume: config_ffi.mic_volume,
        noise_suppress: config_ffi.noise_suppress,
        noise_suppress_strength: config_ffi.noise_suppress_strength,
        comp_threshold: config_ffi.comp_threshold,
        comp_ratio: config_ffi.comp_ratio,
        comp_attack_ms: config_ffi.comp_attack_ms,
        comp_release_ms: config_ffi.comp_release_ms,
        comp_makeup: config_ffi.comp_makeup,
        vad_enable: config_ffi.vad_enable,
        vad_threshold: config_ffi.vad_threshold,
        vad_release_delay_ms: config_ffi.vad_release_delay_ms,
        stereo: config_ffi.stereo,
        stereo_width: config_ffi.stereo_width,
        volume: config_ffi.volume,
        radius: config_ffi.radius,
    };
    dsp.process(samples, &config);
}

fn voice_opus_create() -> usize {
    match OpusCodec::new() {
        Ok(codec) => {
            let boxed = Box::new(codec);
            Box::into_raw(boxed) as usize
        }
        Err(_) => 0,
    }
}

fn voice_opus_destroy(codec: usize) {
    if codec != 0 {
        unsafe {
            let _ = Box::from_raw(codec as *mut OpusCodec);
        }
    }
}

fn voice_opus_encode(codec: usize, pcm: &[i16], output: &mut [u8]) -> Result<usize, String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.encode(pcm, output).map_err(|e| e.to_string())
}

fn voice_opus_decode(codec: usize, opus_data: &[u8], pcm: &mut [i16]) -> Result<usize, String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.decode(opus_data, pcm).map_err(|e| e.to_string())
}

fn voice_opus_set_bitrate(codec: usize, bitrate: i32) -> Result<(), String> {
    if codec == 0 {
        return Err("Null codec pointer".to_string());
    }
    
    let codec = unsafe { &mut *(codec as *mut OpusCodec) };
    codec.set_bitrate(bitrate).map_err(|e| e.to_string())
}

fn voice_calculate_spatial(
    local_x: f32, local_y: f32,
    sender_x: f32, sender_y: f32,
    radius: f32, stereo_width: f32, volume: f32
) -> ffi::SpatialResultFFI {
    let config = SpatialConfig {
        radius,
        stereo_width,
        volume,
    };
    let result = calculate_spatial((local_x, local_y), (sender_x, sender_y), &config);
    
    ffi::SpatialResultFFI {
        distance_factor: result.distance_factor,
        left_gain: result.left_gain,
        right_gain: result.right_gain,
        in_range: result.in_range,
    }
}

fn voice_context_hash(server_addr: &str) -> u32 {
    calculate_context_hash(server_addr)
}

fn voice_token_hash(token: &str) -> u32 {
    calculate_token_hash(token)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dsp_processor() {
        let dsp = voice_dsp_create();
        assert_ne!(dsp, 0);
        
        let mut samples = vec![1000i16; 960];
        let config = ffi::VoiceConfigFFI {
            mic_volume: 100,
            noise_suppress: true,
            noise_suppress_strength: 50,
            comp_threshold: 20,
            comp_ratio: 25,
            comp_attack_ms: 20,
            comp_release_ms: 200,
            comp_makeup: 160,
            vad_enable: false,
            vad_threshold: 8,
            vad_release_delay_ms: 150,
            stereo: true,
            stereo_width: 100,
            volume: 100,
            radius: 50,
        };
        voice_dsp_process(dsp, &mut samples, &config);
        
        voice_dsp_destroy(dsp);
    }

    #[test]
    fn test_opus_codec() {
        let codec = voice_opus_create();
        assert_ne!(codec, 0);
        
        let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
        let mut encoded = vec![0u8; 1000];
        let len = voice_opus_encode(codec, &samples, &mut encoded).unwrap();
        assert!(len > 0);
        
        let mut decoded = vec![0i16; 960];
        let samples_decoded = voice_opus_decode(codec, &encoded[..len], &mut decoded).unwrap();
        assert!(samples_decoded > 0);
        
        voice_opus_destroy(codec);
    }

    #[test]
    fn test_spatial_calculation() {
        let result = voice_calculate_spatial(0.0, 0.0, 100.0, 0.0, 1600.0, 1.0, 1.0);
        assert!(result.in_range);
        assert!(result.distance_factor > 0.0);
    }
}
