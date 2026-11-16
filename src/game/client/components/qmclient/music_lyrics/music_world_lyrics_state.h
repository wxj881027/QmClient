#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_WORLD_LYRICS_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_WORLD_LYRICS_STATE_H

#include "music_lyrics_display.h"

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace QmMusicLyrics
{
	struct SWorldLyric
	{
		bool m_Active = false;
		vec2 m_Anchor = vec2(0.0f, 0.0f);
		double m_CreatedAt = 0.0;
		double m_FadeAt = -1.0;
		float m_FadeAlpha = 1.0f;
		int64_t m_LineStartMs = -1;
		std::string m_Original;
		std::string m_Translation;
		std::array<std::string, 2> m_aRows;
		std::array<int, 2> m_aCharacterCounts{};
		std::array<int, 2> m_aVisibleCharacters{};
		int m_RowCount = 0;
	};

	struct SWorldLyricVisual
	{
		vec2 m_Position = vec2(0.0f, 0.0f);
		float m_Alpha = 0.0f;
	};

	inline constexpr double WORLD_LYRIC_FADE_SECONDS = 1.2;

	inline SWorldLyricVisual WorldLyricVisual(const SWorldLyric &Line, double Now)
	{
		if(!Line.m_Active)
			return {};
		const double Age = std::max(0.0, Now - Line.m_CreatedAt);
		const float Fade = Line.m_FadeAt >= 0.0 ? static_cast<float>(std::clamp((Now - Line.m_FadeAt) / WORLD_LYRIC_FADE_SECONDS, 0.0, 1.0)) : 0.0f;
		const float Alpha = Line.m_FadeAt >= 0.0 ? Line.m_FadeAlpha * (1.0f - Fade) : static_cast<float>(std::clamp(Age / 0.25, 0.0, 1.0));
		// 仅在固定世界锚点附近浮动，镜头与角色的后续移动不改变锚点。
		return {Line.m_Anchor + vec2(0.0f, -static_cast<float>(std::sin(Age * 1.8)) * 3.0f - Fade * 32.0f), Alpha};
	}

	inline int WorldLyricVisibleCharacters(const SWorldLyric &Line, size_t Row)
	{
		if(!Line.m_Active || Row >= Line.m_aVisibleCharacters.size())
			return 0;
		return Line.m_aVisibleCharacters[Row];
	}

	inline int WorldLyricTimedCharacters(const SSourceSnapshot &Source, std::string_view Text, int CharacterCount)
	{
		if(Source.m_pData == nullptr || Source.m_PositionMs < 0 || Source.m_LineStartMs < 0 || Text.empty())
			return CharacterCount;
		const auto &vLines = Source.m_pData->m_Timeline.m_vLines;
		auto It = std::lower_bound(vLines.begin(), vLines.end(), Source.m_LineStartMs,
			[](const NeteaseLyrics::SLine &Line, int64_t Start) { return Line.m_StartMs < Start; });
		// 来源快照和显示文字都可能在 UTF-8 边界截短，只匹配同一时间戳的原文前缀。
		for(; It != vLines.end() && It->m_StartMs == Source.m_LineStartMs; ++It)
			if(std::string_view(It->m_Text).starts_with(Source.m_Original))
				break;
		if(It == vLines.end() || It->m_StartMs != Source.m_LineStartMs || It->m_vWords.empty())
			return CharacterCount;

		size_t Offset = 0;
		int Visible = 0;
		int64_t PreviousStart = -1;
		bool Waiting = false;
		// 只遍历实际显示的前缀，不复制词表；每帧不分配，也不扫描整首歌词。
		for(const auto &Word : It->m_vWords)
		{
			if(Offset == Text.size())
				break;
			if(Word.m_Text.empty())
				continue;
			const size_t Length = std::min(Word.m_Text.size(), Text.size() - Offset);
			if(Word.m_StartMs < 0 || Word.m_EndMs < Word.m_StartMs || Word.m_StartMs < PreviousStart ||
				Text.substr(Offset, Length) != std::string_view(Word.m_Text).substr(0, Length))
				return CharacterCount;
			PreviousStart = Word.m_StartMs;
			const size_t Characters = str_utf8_offset_bytes_to_chars(Word.m_Text.c_str(), Length);
			// 末词可能只显示前缀，展开比例仍使用完整词的字符数。
			const size_t FullCharacters = Length == Word.m_Text.size() ? Characters :
										     str_utf8_offset_bytes_to_chars(Word.m_Text.c_str(), Word.m_Text.size());
			int WordVisible = 0;
			if(Source.m_PositionMs >= Word.m_EndMs)
				WordVisible = static_cast<int>(Characters);
			else if(Source.m_PositionMs > Word.m_StartMs)
				WordVisible = static_cast<int>(std::min<double>(Characters, std::ceil(FullCharacters *
												      (static_cast<double>(Source.m_PositionMs) - Word.m_StartMs) /
												      (static_cast<double>(Word.m_EndMs) - Word.m_StartMs))));
			if(!Waiting)
				Visible += WordVisible;
			Waiting = Waiting || WordVisible < static_cast<int>(Characters);
			Offset += Length;
		}
		// 缺失或不匹配的词时间不能给整句编造匀速动画。
		return Offset == Text.size() ? std::min(Visible, CharacterCount) : CharacterCount;
	}

	class CWorldLyricsState
	{
		struct SIdentity
		{
			int m_Source;
			uint64_t m_SongId;
			uint64_t m_Generation;
			uint32_t m_ProducerPid;
			bool operator==(const SIdentity &) const = default;
		};
		SIdentity m_Identity{};
		bool m_HasIdentity = false;
		double m_LastNow = 0.0;
		int m_Language = -1;
		SWorldLyric m_Current;
		SWorldLyric m_Previous;

		void FadeCurrent(double Now)
		{
			if(!m_Current.m_Active)
				return;
			const float Alpha = WorldLyricVisual(m_Current, Now).m_Alpha;
			m_Previous = std::move(m_Current);
			m_Previous.m_FadeAt = Now;
			m_Previous.m_FadeAlpha = Alpha;
			m_Current = {};
		}

	public:
		void Reset() { *this = {}; }
		const SWorldLyric &Current() const { return m_Current; }
		const SWorldLyric &Previous() const { return m_Previous; }

		void Update(bool Available, int SourceId, const SSourceSnapshot &Source, int Language, vec2 Anchor, double Now)
		{
			if(!Available || Source.m_SongId == 0)
			{
				Reset();
				return;
			}
			const SIdentity Identity{SourceId, Source.m_SongId, Source.m_Generation, Source.m_ProducerPid};
			if(!m_HasIdentity || !(m_Identity == Identity) || Now < m_LastNow)
			{
				Reset();
				m_HasIdentity = true;
				m_Identity = Identity;
			}
			m_LastNow = Now;
			if(m_Previous.m_Active && Now - m_Previous.m_FadeAt >= WORLD_LYRIC_FADE_SECONDS)
				m_Previous = {};
			if(!Source.m_Active || Source.m_Original.empty() || IsLyricCredit(Source.m_Original))
			{
				FadeCurrent(Now);
				return;
			}
			const bool NewLine = !m_Current.m_Active || m_Current.m_LineStartMs != Source.m_LineStartMs || m_Current.m_Original != Source.m_Original;
			if(NewLine)
			{
				FadeCurrent(Now);
				m_Current.m_Active = true;
				m_Current.m_Anchor = Anchor;
				m_Current.m_CreatedAt = Now;
				m_Current.m_LineStartMs = Source.m_LineStartMs;
				m_Current.m_Original = Source.m_Original;
			}
			const SDisplayRows Rows = DisplayRows(Source.m_Original, Source.m_Translation, Language);
			if(NewLine || m_Language != Language || m_Current.m_Translation != Source.m_Translation)
			{
				m_Language = Language;
				m_Current.m_Translation = Source.m_Translation;
				m_Current.m_RowCount = Rows.m_Count;
				for(size_t Index = 0; Index < m_Current.m_aRows.size(); ++Index)
				{
					m_Current.m_aRows[Index] = NeteaseLyrics::TruncateUtf8(Rows.m_aRows[Index], 1023);
					const auto &Text = m_Current.m_aRows[Index];
					m_Current.m_aCharacterCounts[Index] = static_cast<int>(str_utf8_offset_bytes_to_chars(Text.c_str(), Text.size()));
				}
			}
			const bool TranslationOnly = Language == static_cast<int>(EDisplayLanguage::TRANSLATION) &&
						     !Source.m_Translation.empty() && !IsLyricCredit(Source.m_Translation);
			for(size_t Index = 0; Index < m_Current.m_aRows.size(); ++Index)
			{
				const auto &Text = m_Current.m_aRows[Index];
				// 译文没有与原文逐字对应的时间轴，保留整句显示。
				m_Current.m_aVisibleCharacters[Index] = Index == 0 && !TranslationOnly ?
										WorldLyricTimedCharacters(Source, Text, m_Current.m_aCharacterCounts[Index]) :
										m_Current.m_aCharacterCounts[Index];
			}
		}
	};
}

#endif
