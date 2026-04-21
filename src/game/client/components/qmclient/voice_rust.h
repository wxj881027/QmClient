/**
 * @file voice_rust.h
 * @brief Rust 语音模块 C++ 接口
 *
 * 封装 Rust 语音模块的 C++ 接口
 * 使用不透明句柄模式避免 cxx 依赖问题
 *
 * 线程安全说明:
 * - 以下方法必须在主线程调用: init, shutdown, setConfig, setPTT, setVAD,
 *   updatePlayers, setLocalClientId, setContextHash, processFrame, receivePacket,
 *   decodeJitter, mixAudio, updateEncoderParams, startWorker, stopWorker, setNameLists
 * - 以下方法可从任意线程调用: getMicLevel, getPing, isSpeaking (内部使用原子操作)
 */

#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace voice {

// ============== 共享常量 ==============

/// 每帧采样数 (20ms @ 48kHz)，与 C++ VOICE_FRAME_SAMPLES 和 Rust FRAME_SAMPLES 一致
static constexpr int FRAME_SAMPLES = 960;

/// 最大网络包大小 (字节)，与 C++ VOICE_MAX_PACKET 和 Rust VOICE_MAX_PACKET 一致
static constexpr int MAX_PACKET_SIZE = 1200;

/// 配置字段数量 (21 + 10 + 10 = 41)
static constexpr int CONFIG_COUNT = 41;

// ============== FFI 配置结构体 ==============

/**
 * @brief C ABI 兼容的语音配置结构体
 *
 * 所有字段使用 int32_t 以避免 bool 对齐问题。
 * 必须与 Rust 端 VoiceConfigCABI 的内存布局完全一致。
 * 布尔字段使用 0/1 整数值表示。
 */
struct VoiceConfigCABI {
	// 原有 21 个字段
	int32_t mic_volume;
	int32_t noise_suppress;          // 0 或 1
	int32_t noise_suppress_strength;
	int32_t comp_threshold;
	int32_t comp_ratio;
	int32_t comp_attack_ms;
	int32_t comp_release_ms;
	int32_t comp_makeup;
	int32_t vad_enable;              // 0 或 1
	int32_t vad_threshold;
	int32_t vad_release_delay_ms;
	int32_t stereo;                  // 0 或 1
	int32_t stereo_width;
	int32_t volume;
	int32_t radius;
	int32_t mic_mute;                // 0 或 1
	int32_t test_mode;               // 0=正常, 1=本地回环, 2=服务器回环
	int32_t ignore_distance;         // 0 或 1
	int32_t group_global;            // 0 或 1
	uint32_t token_hash;
	uint32_t context_hash;

	// 新增 10 个字段 (与 Rust VoiceConfig 保持一致)
	int32_t filter_enable;          // 0 或 1 - 滤波器开关 (HPF/压缩/限制器)
	int32_t limiter;                // 限制器阈值 (5-100)
	int32_t visibility_mode;        // 0=默认, 1=显示其他队伍, 2=全部可见
	int32_t list_mode;              // 0=无过滤, 1=白名单, 2=黑名单
	int32_t debug;                  // 0 或 1 - 调试模式
	int32_t group_mode;             // 组模式 (0-3)
	int32_t hear_on_spec_pos;       // 0 或 1 - 观战位置收听
	int32_t hear_in_spectate;       // 0 或 1 - 接收旁观者语音
	int32_t hear_vad;               // 0 或 1 - 接收 VAD 语音
	int32_t ptt_release_delay_ms;   // PTT 释放延迟 (0-1000 ms)

	// 原有新增字段
	int32_t ptt_mode;                // 0=按住说话, 1=切换模式
	int32_t ptt_key;                 // 按键码
	int32_t echo_cancel;             // 0 或 1
	int32_t echo_cancel_strength;
	int32_t agc_enable;              // 0 或 1
	int32_t agc_target;
	int32_t agc_max_gain;
	int32_t opus_bitrate;            // Opus 编码比特率 (bps)
	int32_t jitter_buffer_ms;        // 抖动缓冲区大小 (毫秒)
	int32_t packet_loss_concealment; // 0 或 1
};

static_assert(sizeof(VoiceConfigCABI) == CONFIG_COUNT * sizeof(int32_t),
	"VoiceConfigCABI size must equal CONFIG_COUNT * sizeof(int32_t)");
static_assert(alignof(VoiceConfigCABI) == alignof(int32_t),
	"VoiceConfigCABI alignment must equal alignof(int32_t)");

// ============== 数据结构 ==============

/**
 * @brief 语音配置结构 (C++ 侧高级封装)
 */
struct Config {
	// 原有配置项
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
	bool mic_mute = false;
	int test_mode = 0;
	bool ignore_distance = false;
	bool group_global = false;
	uint32_t token_hash = 0;
	uint32_t context_hash = 0;

	// 新增配置项 (与 Rust VoiceConfig 保持一致)
	bool filter_enable = true;       // 滤波器开关 (HPF/压缩/限制器)
	int limiter = 50;                // 限制器阈值 (5-100)
	int visibility_mode = 0;         // 0=默认, 1=显示其他队伍, 2=全部可见
	int list_mode = 0;               // 0=无过滤, 1=白名单, 2=黑名单
	bool debug = false;              // 调试模式
	int group_mode = 0;              // 组模式 (0-3)
	bool hear_on_spec_pos = false;   // 观战位置收听
	bool hear_in_spectate = false;   // 接收旁观者语音
	bool hear_vad = true;            // 接收 VAD 语音
	int ptt_release_delay_ms = 0;    // PTT 释放延迟 (0-1000 ms)

	// 原有新增配置项
	int ptt_mode = 0;                // 0=按住说话, 1=切换模式
	int ptt_key = 0;                 // 按键码
	bool echo_cancel = true;
	int echo_cancel_strength = 50;
	bool agc_enable = true;
	int agc_target = 2000;
	int agc_max_gain = 3000;
	int opus_bitrate = 32000;        // Opus 编码比特率 (bps)
	int jitter_buffer_ms = 60;       // 抖动缓冲区大小 (毫秒)
	bool packet_loss_concealment = true;

	/**
	 * @brief 转换为 FFI 兼容结构体
	 * 布尔字段转换为 0/1 整数值
	 */
	VoiceConfigCABI toCABI() const {
		return VoiceConfigCABI{
			// 原有字段
			mic_volume,
			noise_suppress ? 1 : 0,
			noise_suppress_strength,
			comp_threshold,
			comp_ratio,
			comp_attack_ms,
			comp_release_ms,
			comp_makeup,
			vad_enable ? 1 : 0,
			vad_threshold,
			vad_release_delay_ms,
			stereo ? 1 : 0,
			stereo_width,
			volume,
			radius,
			mic_mute ? 1 : 0,
			test_mode,
			ignore_distance ? 1 : 0,
			group_global ? 1 : 0,
			token_hash,
			context_hash,
			// 新增字段
			filter_enable ? 1 : 0,
			limiter,
			visibility_mode,
			list_mode,
			debug ? 1 : 0,
			group_mode,
			hear_on_spec_pos ? 1 : 0,
			hear_in_spectate ? 1 : 0,
			hear_vad ? 1 : 0,
			ptt_release_delay_ms,
			// 原有新增字段
			ptt_mode,
			ptt_key,
			echo_cancel ? 1 : 0,
			echo_cancel_strength,
			agc_enable ? 1 : 0,
			agc_target,
			agc_max_gain,
			opus_bitrate,
			jitter_buffer_ms,
			packet_loss_concealment ? 1 : 0,
		};
	}
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
 * @brief 名称列表结构 (用于 setNameLists)
 */
struct NameLists {
	std::vector<std::string> whitelist;     // 白名单玩家名称
	std::vector<std::string> blacklist;     // 黑名单玩家名称
	std::vector<std::string> mute;          // 静音玩家名称
	std::vector<std::string> vad_allow;     // 允许 VAD 的玩家名称
	std::vector<std::pair<std::string, int>> name_volumes; // 玩家名称-音量映射
};

/**
 * @brief 语音系统封装类
 *
 * 使用不透明句柄模式，避免 C++ 直接依赖 cxx 生成的头文件
 *
 * 线程安全: 所有非 const 方法必须在主线程调用。
 * const 查询方法 (getMicLevel, getPing, isSpeaking) 可从任意线程调用。
 */
class VoiceSystem {
public:
	VoiceSystem();
	~VoiceSystem();

	/// 初始化语音系统 (主线程)
	bool init();

	/// 关闭语音系统 (主线程)
	void shutdown();

	/// 设置配置 (主线程)
	void setConfig(const Config &config);

	/// 设置 PTT 状态 (主线程)
	void setPTT(bool active);

	/// 设置 VAD 状态 (主线程)
	void setVAD(bool active);

	/// 更新玩家快照 (主线程)
	void updatePlayers(const std::vector<PlayerSnapshot> &players);

	/// 设置本地客户端 ID (主线程)
	void setLocalClientId(int clientId);

	/// 设置上下文哈希 (主线程)
	void setContextHash(uint32_t hash);

	/// 获取麦克风电平 (0-100) (线程安全)
	int getMicLevel() const;

	/// 获取延迟 ms (线程安全)
	int getPing() const;

	/// 是否正在说话 (线程安全)
	bool isSpeaking() const;

	/// 处理音频帧 (捕获 -> DSP -> 编码) (主线程)
	/// @param pcm 输入/输出 PCM 数据 (FRAME_SAMPLES samples)
	/// @param output 输出缓冲区 (用于存储编码后的网络包，至少 MAX_PACKET_SIZE 字节)
	/// @return 编码后的数据长度，如果不需要发送则返回 0
	int processFrame(int16_t *pcm, size_t pcmLen, uint8_t *output, size_t outputLen);

	/// 接收网络数据 (主线程)
	void receivePacket(const uint8_t *data, size_t len);

	/// 解码抖动缓冲 (主线程)
	void decodeJitter();

	/// 混音输出 (主线程)
	void mixAudio(int16_t *output, size_t samples, size_t channels);

	/// 更新编码器参数 (主线程)
	void updateEncoderParams();

	// ============== Worker 线程方法 ==============

	/// 启动 Worker 线程 (主线程)
	/// Worker 线程用于异步处理音频捕获、编码和网络传输
	bool startWorker();

	/// 停止 Worker 线程 (主线程)
	void stopWorker();

	/// 向 Worker 提交捕获的音频数据 (主线程)
	/// @param pcm PCM 数据指针
	/// @param samples 采样数
	void submitCapture(const int16_t *pcm, size_t samples);

	/// 从 Worker 获取混音后的输出数据 (主线程)
	/// @param output 输出缓冲区
	/// @param samples 采样数
	/// @param channels 通道数
	void mixWorkerOutput(int16_t *output, size_t samples, size_t channels);

	/// 向 Worker 提交网络数据包 (主线程)
	/// @param data 数据指针
	/// @param len 数据长度
	void submitPacket(const uint8_t *data, size_t len);

	/// 设置玩家名称列表 (主线程)
	/// 用于基于玩家名称的过滤和音量控制
	void setNameLists(const NameLists &lists);

	/// 检查 Worker 是否运行中
	bool isWorkerRunning() const;

private:
	size_t m_Handle = 0;
	size_t m_WorkerHandle = 0;
	bool m_WorkerRunning = false;
};

} // namespace voice

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_RUST_H
