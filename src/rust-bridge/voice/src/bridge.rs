//! C++ 桥接辅助模块
//!
//! 提供空间音频计算和 DSP 处理的辅助函数
//! FFI 接口统一使用 lib.rs 底部的 C ABI 导出函数

// 允许 dead_code: 本模块函数供 C++ FFI 调用，Rust 内部可能未直接引用
// 这些函数由 lib.rs 中的 FFI 导出函数间接使用，编译器无法识别其为有效调用
#![allow(dead_code)]

use crate::dsp::DspChain;
use crate::network::protocol::{calculate_context_hash, calculate_token_hash};
use crate::spatial::{calculate_spatial, SpatialConfig};
use crate::VoiceConfig;

pub fn spatial_calculation(
    local_x: f32,
    local_y: f32,
    sender_x: f32,
    sender_y: f32,
    radius: f32,
    stereo_width: f32,
    volume: f32,
) -> (f32, f32, f32, bool) {
    let config = SpatialConfig {
        radius,
        stereo_width,
        volume,
    };
    let result = calculate_spatial((local_x, local_y), (sender_x, sender_y), &config);
    (
        result.distance_factor,
        result.left_gain,
        result.right_gain,
        result.in_range,
    )
}

pub fn dsp_processor(dsp: &mut DspChain, samples: &mut [i16], config: &VoiceConfig) {
    dsp.process(samples, config);
}

pub fn context_hash(server_addr: &str) -> u32 {
    calculate_context_hash(server_addr)
}

pub fn token_hash(token: &str) -> u32 {
    calculate_token_hash(token)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_spatial_calculation() {
        let (dist, left, right, in_range) =
            spatial_calculation(0.0, 0.0, 100.0, 0.0, 1600.0, 1.0, 1.0);
        assert!(in_range);
        assert!(dist > 0.0);
        assert!(left > 0.0);
        assert!(right > 0.0);
    }

    #[test]
    fn test_context_hash() {
        let hash1 = context_hash("127.0.0.1:8303");
        let hash2 = context_hash("127.0.0.1:8303");
        let hash3 = context_hash("192.168.1.1:8303");
        assert_eq!(hash1, hash2);
        assert_ne!(hash1, hash3);
    }

    #[test]
    fn test_token_hash() {
        let hash1 = token_hash("test_token");
        let hash2 = token_hash("test_token");
        let hash3 = token_hash("other_token");
        assert_eq!(hash1, hash2);
        assert_ne!(hash1, hash3);
    }

    #[test]
    fn test_dsp_processor() {
        let mut dsp = DspChain::new();
        let mut samples = vec![1000i16; 960];
        let config = VoiceConfig::default();
        dsp_processor(&mut dsp, &mut samples, &config);
    }
}
