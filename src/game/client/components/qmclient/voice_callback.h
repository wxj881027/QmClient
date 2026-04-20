/**
 * @file voice_callback.h
 * @brief Rust 语音模块 C++ 回调接口
 *
 * 提供 C++ 端的回调函数，供 Rust 调用
 */

#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_CALLBACK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_CALLBACK_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 回调：收到语音数据包
 * @param data Opus 编码数据
 * @param len 数据长度
 * @param sender_id 发送者客户端 ID
 */
void on_voice_packet(const uint8_t *data, size_t len, uint16_t sender_id);

/**
 * @brief 回调：玩家说话状态变化
 * @param client_id 客户端 ID
 * @param speaking 是否正在说话
 */
void on_voice_state_change(uint16_t client_id, bool speaking);

/**
 * @brief 获取当前时间 (microseconds)
 * @return 当前时间戳
 */
int64_t voice_time_get();

/**
 * @brief 获取时间频率 (ticks per second)
 * @return 时间频率
 */
int64_t voice_time_freq();

#ifdef __cplusplus
}
#endif

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_CALLBACK_H
