/**
 * @file voice_rust.h
 * @brief Rust 语音模块 C++ 接口
 *
 * 这是 cxx 自动生成的头文件的包装器
 * 实际的头文件会在构建时生成到 build 目录
 */

#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H

// cxx 生成的头文件路径
// 构建时会生成到: ${CMAKE_BINARY_DIR}/cxxbridge/
#include "ddnet-voice/src/bridge/mod.rs.h"

#include <memory>
#include <vector>

namespace voice {

/**
 * @brief 语音配置结构
 */
struct Config {
    int mic_volume = 100;
    bool noise_suppress = true;
    int noise_suppress_strength = 50;
    int comp_threshold = 20;
    int comp_ratio = 25;
    int comp_attack_ms = 20;
    int comp_release_ms = 200;
    int comp_makeup = 160;
    bool vad_enable = false;
    int vad_threshold = 8;
    int vad_release_delay_ms = 150;
    bool stereo = true;
    int stereo_width = 100;
    int volume = 100;
    int radius = 50;
};

/**
 * @brief 玩家快照结构
 */
struct PlayerSnapshot {
    uint16_t client_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    int team = 0;
    bool is_spectator = false;
    bool is_active = false;
};

/**
 * @brief 空间音频结果
 */
struct SpatialResult {
    float distance_factor = 0.0f;
    float left_gain = 0.0f;
    float right_gain = 0.0f;
    bool in_range = false;
};

/**
 * @brief 语音系统封装类
 */
class VoiceSystem {
public:
    VoiceSystem();
    ~VoiceSystem();

    /// 设置配置
    void setConfig(const Config &config);
    
    /// 设置 PTT 状态
    void setPTT(bool active);
    
    /// 设置 VAD 状态
    void setVAD(bool active);
    
    /// 更新玩家快照
    void updatePlayers(const std::vector<PlayerSnapshot> &players);
    
    /// 获取麦克风电平 (0-100)
    int getMicLevel() const;
    
    /// 获取延迟 (ms)
    int getPing() const;
    
    /// 是否正在说话
    bool isSpeaking() const;

private:
    std::unique_ptr<VoiceSystemHandle> m_pHandle;
};

/**
 * @brief DSP 处理器封装类
 */
class DspProcessor {
public:
    DspProcessor();
    ~DspProcessor();
    
    /// 处理音频帧
    void process(int16_t *samples, size_t count, const Config &config);

private:
    std::unique_ptr<DspProcessor> m_pHandle;
};

/**
 * @brief Opus 编解码器封装类
 */
class OpusCodec {
public:
    OpusCodec();
    ~OpusCodec();
    
    /// 编码音频帧
    /// @return 编码后的字节数，-1 表示错误
    int encode(const int16_t *pcm, size_t pcm_count, uint8_t *output, size_t output_size);
    
    /// 解码音频帧
    /// @return 解码后的采样数，-1 表示错误
    int decode(const uint8_t *opus_data, size_t opus_len, int16_t *pcm, size_t pcm_size);
    
    /// 设置比特率
    void setBitrate(int bitrate);

private:
    std::unique_ptr<OpusCodecHandle> m_pHandle;
};

/**
 * @brief 计算 3D 空间音频参数
 */
SpatialResult calculateSpatial(
    float local_x, float local_y,
    float sender_x, float sender_y,
    float radius, float stereo_width, float volume
);

/**
 * @brief 计算上下文哈希
 */
uint32_t contextHash(const char *server_addr);

/**
 * @brief 计算 Token 哈希
 */
uint32_t tokenHash(const char *token);

} // namespace voice

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H
