//! 3D 空间音频模块
//!
//! 基于玩家位置计算距离衰减和立体声声像

use crate::PlayerSnapshot;

/// 空间音频配置
#[derive(Debug, Clone, Copy)]
pub struct SpatialConfig {
    /// 语音半径 (像素)
    pub radius: f32,
    /// 立体声宽度 (0.0 - 1.0)
    pub stereo_width: f32,
    /// 主音量 (0.0 - 1.0)
    pub volume: f32,
}

impl Default for SpatialConfig {
    fn default() -> Self {
        Self {
            radius: 50.0 * 32.0, // 50 tiles * 32 pixels
            stereo_width: 1.0,
            volume: 1.0,
        }
    }
}

/// 空间音频计算结果
#[derive(Debug, Clone, Copy)]
pub struct SpatialResult {
    /// 距离因子 (0.0 - 1.0)
    pub distance_factor: f32,
    /// 左声道增益 (0.0 - 1.0)
    pub left_gain: f32,
    /// 右声道增益 (0.0 - 1.0)
    pub right_gain: f32,
    /// 是否在范围内
    pub in_range: bool,
}

/// 计算空间音频参数
///
/// # 参数
/// * `local_pos` - 本地玩家位置
/// * `sender_pos` - 发送者位置
/// * `config` - 空间音频配置
pub fn calculate_spatial(
    local_pos: (f32, f32),
    sender_pos: (f32, f32),
    config: &SpatialConfig,
) -> SpatialResult {
    let dx = sender_pos.0 - local_pos.0;
    let dy = sender_pos.1 - local_pos.1;
    let distance = (dx * dx + dy * dy).sqrt();

    // 检查是否在范围内
    if distance > config.radius {
        return SpatialResult {
            distance_factor: 0.0,
            left_gain: 0.0,
            right_gain: 0.0,
            in_range: false,
        };
    }

    // 距离衰减 (线性)
    let distance_factor = 1.0 - (distance / config.radius);
    let base_volume = distance_factor * config.volume;

    // 立体声声像 (基于 X 轴偏移)
    let pan = (dx / config.radius).clamp(-1.0, 1.0) * config.stereo_width;

    let left_gain = if pan <= 0.0 {
        base_volume
    } else {
        base_volume * (1.0 - pan)
    };

    let right_gain = if pan >= 0.0 {
        base_volume
    } else {
        base_volume * (1.0 + pan)
    };

    SpatialResult {
        distance_factor,
        left_gain,
        right_gain,
        in_range: true,
    }
}

/// 计算两个玩家之间的距离
pub fn calculate_distance(pos1: (f32, f32), pos2: (f32, f32)) -> f32 {
    let dx = pos2.0 - pos1.0;
    let dy = pos2.1 - pos1.1;
    (dx * dx + dy * dy).sqrt()
}

/// 检查玩家是否在语音范围内
pub fn is_in_range(local_pos: (f32, f32), sender_pos: (f32, f32), radius: f32) -> bool {
    calculate_distance(local_pos, sender_pos) <= radius
}

/// 批量计算多个玩家的空间音频参数
pub fn calculate_spatial_batch(
    local_pos: (f32, f32),
    players: &[PlayerSnapshot],
    config: &SpatialConfig,
) -> Vec<(u16, SpatialResult)> {
    players
        .iter()
        .filter(|p| p.is_active)
        .map(|p| {
            let result = calculate_spatial(local_pos, (p.x, p.y), config);
            (p.client_id, result)
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_spatial_center() {
        let config = SpatialConfig::default();
        let result = calculate_spatial((0.0, 0.0), (0.0, 0.0), &config);

        assert!(result.in_range);
        assert_eq!(result.distance_factor, 1.0);
        assert_eq!(result.left_gain, 1.0);
        assert_eq!(result.right_gain, 1.0);
    }

    #[test]
    fn test_spatial_left() {
        let config = SpatialConfig::default();
        let result = calculate_spatial((0.0, 0.0), (-800.0, 0.0), &config);

        assert!(result.in_range);
        // 在左边，左声道应该更响
        assert!(result.left_gain > result.right_gain);
    }

    #[test]
    fn test_spatial_right() {
        let config = SpatialConfig::default();
        let result = calculate_spatial((0.0, 0.0), (800.0, 0.0), &config);

        assert!(result.in_range);
        // 在右边，右声道应该更响
        assert!(result.right_gain > result.left_gain);
    }

    #[test]
    fn test_spatial_out_of_range() {
        let config = SpatialConfig::default();
        let result = calculate_spatial((0.0, 0.0), (5000.0, 0.0), &config);

        assert!(!result.in_range);
        assert_eq!(result.left_gain, 0.0);
        assert_eq!(result.right_gain, 0.0);
    }

    #[test]
    fn test_spatial_distance_attenuation() {
        let config = SpatialConfig::default();

        // 近距离
        let close = calculate_spatial((0.0, 0.0), (100.0, 0.0), &config);
        // 远距离
        let far = calculate_spatial((0.0, 0.0), (1000.0, 0.0), &config);

        assert!(close.distance_factor > far.distance_factor);
        assert!(close.left_gain > far.left_gain);
    }

    #[test]
    fn test_calculate_distance() {
        let d1 = calculate_distance((0.0, 0.0), (300.0, 400.0));
        assert_eq!(d1, 500.0);

        let d2 = calculate_distance((0.0, 0.0), (0.0, 0.0));
        assert_eq!(d2, 0.0);
    }

    #[test]
    fn test_is_in_range() {
        assert!(is_in_range((0.0, 0.0), (100.0, 0.0), 200.0));
        assert!(!is_in_range((0.0, 0.0), (300.0, 0.0), 200.0));
    }
}
