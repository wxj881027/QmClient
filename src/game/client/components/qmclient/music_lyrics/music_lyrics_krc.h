#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_KRC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_KRC_H

#include "music_lyrics_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// 酷狗 KRC 歌词解密与解析(纯函数,无平台依赖,可单测)。
// KRC 文件结构:
//   [0..3]  魔数 "krc1"
//   [4..7]  小端 uint32 加密载荷长度(可选,多数实现忽略)
//   [8]     密钥标志:0=全部使用 64 字节密钥表;1=前 1024 字节用 1024 字节密钥表
//   [9..]   加密载荷
// 解密:载荷逐字节 XOR 密钥表 -> zlib inflate -> UTF-8 LRC 风格文本。
// 文本行格式: [mm:ss.xx]<start,dur>词<start,dur>词... (start 为相对行起始的毫秒偏移)
// 旧版变体:行格式 [start,dur](毫秒对) + <offset,dur,0>词,解析器同时兼容。
namespace QmMusicLyrics
{
	constexpr size_t KRC_MAX_TEXT_BYTES = 2 * 1024 * 1024;

	// 标准 64 字节 XOR 密钥表(公开逆向资料,多个开源实现一致)。
	constexpr uint8_t KRC_KEY64[64] = {
		0x40, 0x57, 0x7D, 0x24, 0x30, 0x16, 0x0B, 0x35, 0x13, 0x11, 0x09, 0x00, 0x1C, 0x30, 0x3B, 0x3A,
		0x18, 0x27, 0x03, 0x1D, 0x2D, 0x2B, 0x37, 0x05, 0x11, 0x04, 0x2E, 0x37, 0x2D, 0x1A, 0x0B, 0x11,
		0x2A, 0x01, 0x0B, 0x35, 0x08, 0x32, 0x0E, 0x0A, 0x2D, 0x36, 0x05, 0x07, 0x06, 0x0A, 0x1A, 0x25,
		0x02, 0x31, 0x39, 0x3B, 0x04, 0x08, 0x19, 0x37, 0x3A, 0x08, 0x2D, 0x32, 0x03, 0x22, 0x20, 0x06};

	// 旧版 16 字节密钥表(仓库历史 qm_lyrics_source_kugou.cpp 使用,兼容旧文件)。
	constexpr uint8_t KRC_KEY16[16] = {
		0x40, 0x47, 0x61, 0x77, 0x5E, 0x32, 0x74, 0x47,
		0x51, 0x36, 0x31, 0x2D, 0xCE, 0xD2, 0x6E, 0x69};

	// 解密 KRC 文件字节。成功时 pOutText 为解密后的 UTF-8 文本。
	// 依次尝试标准 64 字节表与旧版 16 字节表,以 zlib 解压成功为准。
	bool DecryptKrc(std::string_view Data, std::string *pOutText, std::string *pError = nullptr);

	// 解析解密后的 KRC 文本为统一时间轴。兼容两种行头:
	//   [mm:ss.xx]            (LRC 风格,标准)
	//   [startMs,durationMs]  (毫秒对,旧版)
	// 词标记:<start,dur>text 或 <start,dur,0>text。
	bool ParseKrcText(std::string_view Text, NeteaseLyrics::STimeline *pOut, std::string *pError = nullptr);

	// 从 KRC 文本提取内嵌翻译轨 [language:base64json]。
	// base64 解码后为 JSON: {"content":[{"lyricContent":[["译文片段"...],...],"type":1}]}
	// type=1 为中文翻译、type=0 为罗马音;返回与主歌词行序号对齐的译文数组。
	// 无翻译轨或解码失败时返回空数组。
	std::vector<std::string> ExtractKrcTranslation(std::string_view Text);

	// 组合入口:解密 + 解析(含内嵌翻译轨),输出统一歌词数据。
	bool ParseKrcData(std::string_view Data, SLyricsData *pOut, std::string *pError = nullptr);
} // namespace QmMusicLyrics

#endif
