/**
 * @file voice_rust.cpp
 * @brief Rust 语音模块 C++ 实现
 *
 * 封装 Rust 语音模块的 C++ 接口
 */

#include "voice_rust.h"
#include "voice_callback.h"

#include <algorithm>

namespace voice {

// VoiceSystem 实现

VoiceSystem::VoiceSystem()
    : m_pHandle(voice_system_create())
{
}

VoiceSystem::~VoiceSystem() = default;

void VoiceSystem::setConfig(const Config &config)
{
    if (!m_pHandle) return;
    
    VoiceConfigFFI ffi_config;
    ffi_config.mic_volume = config.mic_volume;
    ffi_config.noise_suppress = config.noise_suppress;
    ffi_config.noise_suppress_strength = config.noise_suppress_strength;
    ffi_config.comp_threshold = config.comp_threshold;
    ffi_config.comp_ratio = config.comp_ratio;
    ffi_config.comp_attack_ms = config.comp_attack_ms;
    ffi_config.comp_release_ms = config.comp_release_ms;
    ffi_config.comp_makeup = config.comp_makeup;
    ffi_config.vad_enable = config.vad_enable;
    ffi_config.vad_threshold = config.vad_threshold;
    ffi_config.vad_release_delay_ms = config.vad_release_delay_ms;
    ffi_config.stereo = config.stereo;
    ffi_config.stereo_width = config.stereo_width;
    ffi_config.volume = config.volume;
    ffi_config.radius = config.radius;
    
    voice_set_config(*m_pHandle, ffi_config);
}

void VoiceSystem::setPTT(bool active)
{
    if (m_pHandle) {
        voice_set_ptt(*m_pHandle, active);
    }
}

void VoiceSystem::setVAD(bool active)
{
    if (m_pHandle) {
        voice_set_vad(*m_pHandle, active);
    }
}

void VoiceSystem::updatePlayers(const std::vector<PlayerSnapshot> &players)
{
    if (!m_pHandle) return;
    
    std::vector<PlayerSnapshotFFI> ffi_players;
    ffi_players.reserve(players.size());
    
    for (const auto &p : players) {
        PlayerSnapshotFFI ffi;
        ffi.client_id = p.client_id;
        ffi.x = p.x;
        ffi.y = p.y;
        ffi.team = p.team;
        ffi.is_spectator = p.is_spectator;
        ffi.is_active = p.is_active;
        ffi_players.push_back(ffi);
    }
    
    voice_update_players(*m_pHandle, rust::Slice<const PlayerSnapshotFFI>(ffi_players.data(), ffi_players.size()));
}

int VoiceSystem::getMicLevel() const
{
    return m_pHandle ? voice_get_mic_level(*m_pHandle) : 0;
}

int VoiceSystem::getPing() const
{
    return m_pHandle ? voice_get_ping(*m_pHandle) : 0;
}

bool VoiceSystem::isSpeaking() const
{
    return m_pHandle ? voice_is_speaking(*m_pHandle) : false;
}

// DspProcessor 实现

DspProcessor::DspProcessor()
    : m_pHandle(dsp_create())
{
}

DspProcessor::~DspProcessor() = default;

void DspProcessor::process(int16_t *samples, size_t count, const Config &config)
{
    if (!m_pHandle || !samples) return;
    
    VoiceConfigFFI ffi_config;
    ffi_config.mic_volume = config.mic_volume;
    ffi_config.noise_suppress = config.noise_suppress;
    ffi_config.noise_suppress_strength = config.noise_suppress_strength;
    ffi_config.comp_threshold = config.comp_threshold;
    ffi_config.comp_ratio = config.comp_ratio;
    ffi_config.comp_attack_ms = config.comp_attack_ms;
    ffi_config.comp_release_ms = config.comp_release_ms;
    ffi_config.comp_makeup = config.comp_makeup;
    ffi_config.vad_enable = config.vad_enable;
    ffi_config.vad_threshold = config.vad_threshold;
    ffi_config.vad_release_delay_ms = config.vad_release_delay_ms;
    ffi_config.stereo = config.stereo;
    ffi_config.stereo_width = config.stereo_width;
    ffi_config.volume = config.volume;
    ffi_config.radius = config.radius;
    
    rust::Slice<int16_t> slice(samples, count);
    dsp_process(*m_pHandle, slice, ffi_config);
}

// OpusCodec 实现

OpusCodec::OpusCodec()
    : m_pHandle(nullptr)
{
    auto result = opus_create();
    if (result) {
        m_pHandle = std::move(*result);
    }
}

OpusCodec::~OpusCodec() = default;

int OpusCodec::encode(const int16_t *pcm, size_t pcm_count, uint8_t *output, size_t output_size)
{
    if (!m_pHandle || !pcm || !output) return -1;
    
    rust::Slice<const int16_t> pcm_slice(pcm, pcm_count);
    rust::Slice<uint8_t> output_slice(output, output_size);
    
    auto result = opus_encode(*m_pHandle, pcm_slice, output_slice);
    return result ? static_cast<int>(*result) : -1;
}

int OpusCodec::decode(const uint8_t *opus_data, size_t opus_len, int16_t *pcm, size_t pcm_size)
{
    if (!m_pHandle || !opus_data || !pcm) return -1;
    
    rust::Slice<const uint8_t> opus_slice(opus_data, opus_len);
    rust::Slice<int16_t> pcm_slice(pcm, pcm_size);
    
    auto result = opus_decode(*m_pHandle, opus_slice, pcm_slice);
    return result ? static_cast<int>(*result) : -1;
}

void OpusCodec::setBitrate(int bitrate)
{
    if (m_pHandle) {
        opus_set_bitrate(*m_pHandle, bitrate);
    }
}

// 工具函数

SpatialResult calculateSpatial(
    float local_x, float local_y,
    float sender_x, float sender_y,
    float radius, float stereo_width, float volume)
{
    auto ffi = voice_calculate_spatial(local_x, local_y, sender_x, sender_y, radius, stereo_width, volume);
    
    SpatialResult result;
    result.distance_factor = ffi.distance_factor;
    result.left_gain = ffi.left_gain;
    result.right_gain = ffi.right_gain;
    result.in_range = ffi.in_range;
    return result;
}

uint32_t contextHash(const char *server_addr)
{
    return voice_context_hash(rust::Str(server_addr));
}

uint32_t tokenHash(const char *token)
{
    return voice_token_hash(rust::Str(token));
}

} // namespace voice
