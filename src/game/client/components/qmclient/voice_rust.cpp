/**
 * @file voice_rust.cpp
 * @brief Rust 语音模块 C++ 实现
 *
 * 封装 Rust 语音模块的 C++ 接口
 * 使用不透明句柄模式避免 cxx 依赖问题
 */

#include "voice_rust.h"

#include <algorithm>

// 声明外部 Rust 函数 (从 ddnet_voice 库导出)
extern "C" {
    size_t voice_system_create();
    void voice_system_destroy(size_t handle);
    void voice_set_config(size_t handle, const int* config);
    void voice_set_ptt(size_t handle, int active);
    void voice_set_vad(size_t handle, int active);
    void voice_update_players(size_t handle, const float* players, size_t count);
    int voice_get_mic_level(size_t handle);
    int voice_get_ping(size_t handle);
    int voice_is_speaking(size_t handle);
}

namespace voice {

VoiceSystem::VoiceSystem()
{
    m_Handle = voice_system_create();
}

VoiceSystem::~VoiceSystem()
{
    if(m_Handle) {
        voice_system_destroy(m_Handle);
        m_Handle = 0;
    }
}

void VoiceSystem::setConfig(const Config &config)
{
    if(!m_Handle) return;
    
    // 将配置转换为整数数组 (15 个参数)
    int config_array[15] = {
        config.mic_volume,
        config.noise_suppress ? 1 : 0,
        config.noise_suppress_strength,
        config.comp_threshold,
        config.comp_ratio,
        config.comp_attack_ms,
        config.comp_release_ms,
        config.comp_makeup,
        config.vad_enable ? 1 : 0,
        config.vad_threshold,
        config.vad_release_delay_ms,
        config.stereo ? 1 : 0,
        config.stereo_width,
        config.volume,
        config.radius
    };
    
    voice_set_config(m_Handle, config_array);
}

void VoiceSystem::setPTT(bool active)
{
    if(m_Handle) {
        voice_set_ptt(m_Handle, active ? 1 : 0);
    }
}

void VoiceSystem::setVAD(bool active)
{
    if(m_Handle) {
        voice_set_vad(m_Handle, active ? 1 : 0);
    }
}

void VoiceSystem::updatePlayers(const std::vector<PlayerSnapshot> &players)
{
    if(!m_Handle || players.empty()) return;
    
    // 转换玩家数据为浮点数组
    std::vector<float> player_data(players.size() * 6);
    for(size_t i = 0; i < players.size(); i++) {
        player_data[i * 6 + 0] = static_cast<float>(players[i].client_id);
        player_data[i * 6 + 1] = players[i].x;
        player_data[i * 6 + 2] = players[i].y;
        player_data[i * 6 + 3] = static_cast<float>(players[i].team);
        player_data[i * 6 + 4] = players[i].is_spectator ? 1.0f : 0.0f;
        player_data[i * 6 + 5] = players[i].is_active ? 1.0f : 0.0f;
    }
    
    voice_update_players(m_Handle, player_data.data(), players.size());
}

int VoiceSystem::getMicLevel() const
{
    return m_Handle ? voice_get_mic_level(m_Handle) : 0;
}

int VoiceSystem::getPing() const
{
    return m_Handle ? voice_get_ping(m_Handle) : 0;
}

bool VoiceSystem::isSpeaking() const
{
    return m_Handle ? voice_is_speaking(m_Handle) != 0 : false;
}

} // namespace voice
