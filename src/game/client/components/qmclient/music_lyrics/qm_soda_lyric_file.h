#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SODA_LYRIC_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SODA_LYRIC_FILE_H

#include "music_lyrics_model.h"

#include <cstddef>
#include <string>
#include <string_view>

// 客户端侧解析汽水 Helper 写入的歌词 JSON 数据文件。
// 文件结构: {"mediaId","title","artist","album","coverUrl","durationMs","positionMs",
//            "isPlaying","lyricType","lyricContent","translationLrc"}
// lyricType=krc 时 lyricContent 是明文 KRC(酷狗格式,行 [start,dur] + 词 <off,dur,0>);
// lyricType=lrc 时是普通 LRC。translationLrc 是独立 LRC 翻译轨。
namespace QmSodaLyricFile
{
	// 正式来源最多 16MiB，JSON 逐字节最坏展开六倍，再为字段与数值留空间。
	// 这是文件字节预算，不代表解析树、词表或进程内存的上限。
	inline constexpr size_t MAX_FILE_BYTES = 128 * 1024 * 1024;

	// 只读取普通文件的完整限长内容；失败保留原输出，已知资源异常返回 false。
	bool LoadLyricFile(const char *pPath, QmMusicLyrics::SLyricsData *pOut);

	// 解析 JSON 文本为统一歌词数据。成功返回 true。
	// 非空独立翻译轨 LRC 按时间戳覆盖；否则 KRC 使用与主轨共同过滤、排序的内嵌译文。
	bool ParseLyricFileJson(std::string_view Json, QmMusicLyrics::SLyricsData *pOut, std::string *pError = nullptr);
} // namespace QmSodaLyricFile

#endif
