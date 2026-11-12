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
			if(!Source.m_Active || Source.m_Original.empty())
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
			if(NewLine || m_Language != Language || m_Current.m_Translation != Source.m_Translation)
			{
				m_Language = Language;
				m_Current.m_Translation = Source.m_Translation;
				const SDisplayRows Rows = DisplayRows(Source.m_Original, Source.m_Translation, Language);
				m_Current.m_RowCount = Rows.m_Count;
				for(size_t Index = 0; Index < m_Current.m_aRows.size(); ++Index)
					m_Current.m_aRows[Index] = NeteaseLyrics::TruncateUtf8(Rows.m_aRows[Index], 512);
			}
		}
	};
}

#endif
