/**
 * @file voice_rust.cpp
 * @brief Rust 语音模块 C++ 实现
 *
 * 封装 Rust 语音模块的 C++ 接口
 * 使用不透明句柄模式避免 cxx 依赖问题
 */

#include "voice_rust.h"

#include <algorithm>
#include <cstring>

extern "C" {
	// ============== 原有 VoiceSystem FFI ==============
	size_t voice_system_create();
	void voice_system_destroy(size_t handle);
	int voice_system_init(size_t handle);
	void voice_system_shutdown(size_t handle);
	void voice_set_config(size_t handle, const voice::VoiceConfigCABI *config);
	void voice_set_ptt(size_t handle, int active);
	void voice_set_vad(size_t handle, int active);
	void voice_update_players(size_t handle, const float *players, size_t count);
	void voice_set_local_client_id(size_t handle, int client_id);
	void voice_set_context_hash(size_t handle, uint32_t hash);
	int voice_get_mic_level(size_t handle);
	int voice_get_ping(size_t handle);
	int voice_is_speaking(size_t handle);
	int voice_process_frame(size_t handle, int16_t *pcm, size_t pcm_len, uint8_t *output, size_t output_len);
	void voice_receive_packet(size_t handle, const uint8_t *data, size_t len);
	void voice_decode_jitter(size_t handle);
	void voice_mix_audio(size_t handle, int16_t *output, size_t samples, size_t channels);
	void voice_update_encoder_params(size_t handle);

	// ============== 新增 Worker FFI ==============
	size_t voice_worker_create();
	void voice_worker_destroy(size_t handle);
	void voice_worker_set_config(size_t handle, const voice::VoiceConfigCABI *config);
	void voice_worker_submit_capture(size_t handle, const int16_t *pcm, size_t samples);
	void voice_worker_mix_output(size_t handle, int16_t *output, size_t samples, size_t channels);
	void voice_worker_submit_packet(size_t handle, const uint8_t *data, size_t len);
	void voice_worker_set_name_lists(
		size_t handle,
		const char **whitelist, size_t whitelist_count,
		const char **blacklist, size_t blacklist_count,
		const char **mute, size_t mute_count,
		const char **vad_allow, size_t vad_allow_count,
		const char **name_volumes_names, const int32_t *name_volumes_values, size_t name_volumes_count
	);
}

namespace voice {

VoiceSystem::VoiceSystem()
{
	m_Handle = voice_system_create();
}

VoiceSystem::~VoiceSystem()
{
	// 先停止 Worker 线程
	if(m_WorkerRunning) {
		stopWorker();
	}
	if(m_Handle) {
		voice_system_destroy(m_Handle);
		m_Handle = 0;
	}
}

bool VoiceSystem::init()
{
	if(!m_Handle) return false;
	return voice_system_init(m_Handle) != 0;
}

void VoiceSystem::shutdown()
{
	// 先停止 Worker 线程
	if(m_WorkerRunning) {
		stopWorker();
	}
	if(m_Handle) {
		voice_system_shutdown(m_Handle);
	}
}

void VoiceSystem::setConfig(const Config &config)
{
	if(!m_Handle) return;

	// 使用结构体传递配置，避免硬编码数组索引
	VoiceConfigCABI cabi = config.toCABI();
	voice_set_config(m_Handle, &cabi);

	// Worker 配置更新
	if(m_WorkerHandle) {
		voice_worker_set_config(m_WorkerHandle, &cabi);
	}
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

	std::vector<float> player_data(players.size() * 5);
	for(size_t i = 0; i < players.size(); i++) {
		int flags = 0;
		if(players[i].is_spectator) flags |= 1;
		if(players[i].is_active) flags |= 2;

		player_data[i * 5 + 0] = static_cast<float>(players[i].client_id);
		player_data[i * 5 + 1] = players[i].x;
		player_data[i * 5 + 2] = players[i].y;
		player_data[i * 5 + 3] = static_cast<float>(players[i].team);
		player_data[i * 5 + 4] = static_cast<float>(flags);
	}

	voice_update_players(m_Handle, player_data.data(), players.size());
}

void VoiceSystem::setLocalClientId(int clientId)
{
	if(m_Handle) {
		voice_set_local_client_id(m_Handle, clientId);
	}
}

void VoiceSystem::setContextHash(uint32_t hash)
{
	if(m_Handle) {
		voice_set_context_hash(m_Handle, hash);
	}
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

int VoiceSystem::processFrame(int16_t *pcm, size_t pcmLen, uint8_t *output, size_t outputLen)
{
	if(!m_Handle) return 0;

	// 空指针检查: pcm 和 output 必须有效
	if(!pcm || pcmLen < (size_t)FRAME_SAMPLES) return 0;
	if(!output || outputLen < (size_t)MAX_PACKET_SIZE) return 0;

	return voice_process_frame(m_Handle, pcm, pcmLen, output, outputLen);
}

void VoiceSystem::receivePacket(const uint8_t *data, size_t len)
{
	if(m_Handle && data && len > 0) {
		voice_receive_packet(m_Handle, data, len);
	}
}

void VoiceSystem::decodeJitter()
{
	if(m_Handle) {
		voice_decode_jitter(m_Handle);
	}
}

void VoiceSystem::mixAudio(int16_t *output, size_t samples, size_t channels)
{
	// Worker 混音优先
	if(m_WorkerHandle && m_WorkerRunning && output && samples > 0 && channels > 0) {
		voice_worker_mix_output(m_WorkerHandle, output, samples, channels);
	} else if(m_Handle && output && samples > 0 && channels > 0) {
		voice_mix_audio(m_Handle, output, samples, channels);
	}
}

void VoiceSystem::updateEncoderParams()
{
	if(m_Handle) {
		voice_update_encoder_params(m_Handle);
	}
}

// ============== Worker 线程方法实现 ==============

bool VoiceSystem::startWorker()
{
	if(!m_Handle || m_WorkerRunning) {
		return false;
	}
	m_WorkerHandle = voice_worker_create();
	if(!m_WorkerHandle) {
		return false;
	}
	m_WorkerRunning = true;
	return true;
}

void VoiceSystem::stopWorker()
{
	if(m_WorkerHandle) {
		voice_worker_destroy(m_WorkerHandle);
		m_WorkerHandle = 0;
	}
	m_WorkerRunning = false;
}

void VoiceSystem::submitCapture(const int16_t *pcm, size_t samples)
{
	if(m_WorkerHandle && m_WorkerRunning && pcm && samples > 0) {
		voice_worker_submit_capture(m_WorkerHandle, pcm, samples);
	}
}

void VoiceSystem::mixWorkerOutput(int16_t *output, size_t samples, size_t channels)
{
	if(m_WorkerHandle && m_WorkerRunning && output && samples > 0 && channels > 0) {
		voice_worker_mix_output(m_WorkerHandle, output, samples, channels);
	}
}

void VoiceSystem::submitPacket(const uint8_t *data, size_t len)
{
	if(m_WorkerHandle && m_WorkerRunning && data && len > 0) {
		voice_worker_submit_packet(m_WorkerHandle, data, len);
	}
}

void VoiceSystem::setNameLists(const NameLists &lists)
{
	if(!m_WorkerHandle || !m_WorkerRunning) {
		return;
	}

	// 将 std::vector<std::string> 转换为 C 字符串数组
	std::vector<const char*> whitelist_ptrs;
	for(const auto &name : lists.whitelist) {
		whitelist_ptrs.push_back(name.c_str());
	}

	std::vector<const char*> blacklist_ptrs;
	for(const auto &name : lists.blacklist) {
		blacklist_ptrs.push_back(name.c_str());
	}

	std::vector<const char*> mute_ptrs;
	for(const auto &name : lists.mute) {
		mute_ptrs.push_back(name.c_str());
	}

	std::vector<const char*> vad_allow_ptrs;
	for(const auto &name : lists.vad_allow) {
		vad_allow_ptrs.push_back(name.c_str());
	}

	// 分离名称和音量值
	std::vector<const char*> name_volume_names;
	std::vector<int32_t> name_volume_values;
	for(const auto &pair : lists.name_volumes) {
		name_volume_names.push_back(pair.first.c_str());
		name_volume_values.push_back(static_cast<int32_t>(pair.second));
	}

	voice_worker_set_name_lists(
		m_WorkerHandle,
		whitelist_ptrs.empty() ? nullptr : whitelist_ptrs.data(),
		whitelist_ptrs.size(),
		blacklist_ptrs.empty() ? nullptr : blacklist_ptrs.data(),
		blacklist_ptrs.size(),
		mute_ptrs.empty() ? nullptr : mute_ptrs.data(),
		mute_ptrs.size(),
		vad_allow_ptrs.empty() ? nullptr : vad_allow_ptrs.data(),
		vad_allow_ptrs.size(),
		name_volume_names.empty() ? nullptr : name_volume_names.data(),
		name_volume_values.empty() ? nullptr : name_volume_values.data(),
		name_volume_names.size()
	);
}

bool VoiceSystem::isWorkerRunning() const
{
	return m_WorkerRunning && m_WorkerHandle != 0;
}

} // namespace voice
