#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_LYRIC_PARSER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_LYRIC_PARSER_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace NeteaseLyrics
{
	struct SWord
	{
		int64_t m_StartMs = 0;
		int64_t m_EndMs = 0;
		std::string m_Text;
	};

	struct SLine
	{
		int64_t m_StartMs = 0;
		// -1 means that no reliable end time is available.
		int64_t m_EndMs = -1;
		std::string m_Text;
		std::vector<SWord> m_vWords;
	};

	struct STimeline
	{
		std::vector<SLine> m_vLines;
		bool m_HasTiming = false;
		void Clear()
		{
			m_vLines.clear();
			m_HasTiming = false;
		}
	};

	struct SRawLyrics
	{
		std::string m_Lrc;
		std::string m_Yrc;
		std::string m_TranslationLrc;
	};

	// 解析网易云 LRC。无有效时间戳时返回 false，不构造虚假时间轴。
	bool ParseLrc(std::string_view Text, STimeline *pOut, std::string *pError = nullptr);
	// 解析网易云 YRC（行级 [start,duration]，可选词级 (offset,duration)）。
	bool ParseYrc(std::string_view Text, STimeline *pOut, std::string *pError = nullptr);
	// 优先 YRC，YRC 不可用时回退 LRC。
	bool ParseRawLyrics(const SRawLyrics &Raw, STimeline *pOut, std::string *pError = nullptr);

	bool IsValidUtf8(std::string_view Text);
	// 返回不超过 MaxBytes 且不截断 UTF-8 codepoint 的字符串。
	std::string TruncateUtf8(std::string_view Text, size_t MaxBytes);

} // namespace NeteaseLyrics

// 便于客户端模块和测试使用的稳定别名；不引入通用 provider 抽象。
namespace QmNetease
{
	using NeteaseLyrics::IsValidUtf8;
	using NeteaseLyrics::ParseLrc;
	using NeteaseLyrics::ParseRawLyrics;
	using NeteaseLyrics::ParseYrc;
	using NeteaseLyrics::SLine;
	using NeteaseLyrics::SRawLyrics;
	using NeteaseLyrics::STimeline;
	using NeteaseLyrics::SWord;
	using NeteaseLyrics::TruncateUtf8;
}

#endif
