#include "music_lyrics_community_format.h"

#include <algorithm>
#include <charconv>
#include <utility>

namespace QmMusicLyrics
{
	namespace
	{
		bool IsSpace(char Character)
		{
			return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n' || Character == '\f' || Character == '\v';
		}

		bool IsWhitespace(std::string_view Text)
		{
			return std::all_of(Text.begin(), Text.end(), IsSpace);
		}

		std::string_view Trim(std::string_view Text)
		{
			while(!Text.empty() && IsSpace(Text.front()))
				Text.remove_prefix(1);
			while(!Text.empty() && IsSpace(Text.back()))
				Text.remove_suffix(1);
			return Text;
		}

		bool IsAlphaNumericId(std::string_view Id, size_t Length)
		{
			return Id.size() == Length && std::all_of(Id.begin(), Id.end(), [](char Character) {
				return (Character >= '0' && Character <= '9') || (Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z');
			});
		}

		bool IsNumericId(std::string_view Id)
		{
			if(Id.empty() || Id.size() > 20 || Id.front() == '0')
				return false;
			uint64_t Value = 0;
			const auto Result = std::from_chars(Id.data(), Id.data() + Id.size(), Value);
			return Result.ec == std::errc{} && Result.ptr == Id.data() + Id.size() && Value != 0;
		}

		bool IsValidSong(const SCommunitySong &Song)
		{
			switch(Song.m_Source)
			{
			case ECommunitySource::NETEASE:
				return IsNumericId(Song.m_Id);
			case ECommunitySource::QQMUSIC:
				return IsNumericId(Song.m_Id) || IsAlphaNumericId(Song.m_Id, 14);
			case ECommunitySource::SPOTIFY:
				return IsAlphaNumericId(Song.m_Id, 22);
			default:
				return false;
			}
		}

		bool HasLineWordTiming(const NeteaseLyrics::SLine &Line)
		{
			if(Line.m_StartMs < 0 || Line.m_vWords.empty())
				return false;
			size_t TextOffset = 0;
			int64_t PreviousStart = Line.m_StartMs;
			bool HasTimedText = false;
			for(const auto &Word : Line.m_vWords)
			{
				if(Word.m_Text.empty())
					continue;
				if(Word.m_StartMs < PreviousStart || Word.m_EndMs < Word.m_StartMs ||
					Word.m_Text.size() > Line.m_Text.size() - TextOffset ||
					std::string_view(Line.m_Text).substr(TextOffset, Word.m_Text.size()) != Word.m_Text)
					return false;
				PreviousStart = Word.m_StartMs;
				TextOffset += Word.m_Text.size();
				HasTimedText |= Word.m_EndMs > Word.m_StartMs && !IsWhitespace(Word.m_Text);
			}
			return HasTimedText && TextOffset == Line.m_Text.size();
		}

		bool IsUntimedSpace(const NeteaseLyrics::SWord &Word)
		{
			return Word.m_StartMs == 0 && Word.m_EndMs == 0 && IsWhitespace(Word.m_Text);
		}

		bool NormalizeLine(NeteaseLyrics::SLine &Line)
		{
			// AMLL 导出的分词空格使用 (0,0,0)，其显示时刻附着于相邻词。
			int64_t PreviousEnd = -1;
			for(size_t Index = 0; Index < Line.m_vWords.size(); ++Index)
			{
				auto &Word = Line.m_vWords[Index];
				if(Word.m_Text.empty())
					continue;
				if(IsUntimedSpace(Word))
				{
					int64_t Anchor = PreviousEnd;
					if(Anchor < 0)
					{
						const auto Next = std::find_if(Line.m_vWords.begin() + Index + 1, Line.m_vWords.end(), [](const auto &Candidate) {
							return !Candidate.m_Text.empty() && !IsUntimedSpace(Candidate);
						});
						if(Next == Line.m_vWords.end())
							return false;
						Anchor = Next->m_StartMs;
					}
					Word.m_StartMs = Anchor;
					Word.m_EndMs = Anchor;
				}
				PreviousEnd = Word.m_EndMs;
				Line.m_EndMs = std::max(Line.m_EndMs, Word.m_EndMs);
			}
			return HasLineWordTiming(Line);
		}
	}

	SCommunitySong CommunitySong(int SourceId, uint64_t SongId, std::string_view CatalogId)
	{
		if(SourceId == 1 && SongId != 0)
			return {ECommunitySource::NETEASE, std::to_string(SongId)};
		if(SourceId == 4)
		{
			constexpr std::string_view ID_PREFIX = "qqmusic:id:";
			constexpr std::string_view MID_PREFIX = "qqmusic:mid:";
			if(CatalogId.starts_with(ID_PREFIX) && IsNumericId(CatalogId.substr(ID_PREFIX.size())))
				return {ECommunitySource::QQMUSIC, std::string(CatalogId.substr(ID_PREFIX.size()))};
			if(CatalogId.starts_with(MID_PREFIX) && IsAlphaNumericId(CatalogId.substr(MID_PREFIX.size()), 14))
				return {ECommunitySource::QQMUSIC, std::string(CatalogId.substr(MID_PREFIX.size()))};
		}
		if(SourceId == 5 && IsAlphaNumericId(CatalogId, 22))
			return {ECommunitySource::SPOTIFY, std::string(CatalogId)};
		return {};
	}

	std::string CommunityCachePath(const SCommunitySong &Song)
	{
		if(!IsValidSong(Song))
			return {};
		const char *pDirectory = Song.m_Source == ECommunitySource::NETEASE ? "netease" : Song.m_Source == ECommunitySource::QQMUSIC ? "qqmusic" :
																	       "spotify";
		return std::string("qmclient/lyrics/") + pDirectory + "/" + Song.m_Id + ".yrc";
	}

	std::string CommunityUrl(const SCommunitySong &Song)
	{
		if(!IsValidSong(Song))
			return {};
		const char *pDirectory = Song.m_Source == ECommunitySource::NETEASE ? "ncm-lyrics" : Song.m_Source == ECommunitySource::QQMUSIC ? "qq-lyrics" :
																		  "spotify-lyrics";
		return std::string("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/") + pDirectory + "/" + Song.m_Id + ".yrc";
	}

	bool ParseCommunityLyrics(std::string_view Text, SLyricsData *pOut)
	{
		if(pOut == nullptr)
			return false;
		*pOut = {};
		if(Text.empty() || Text.size() > COMMUNITY_LYRICS_MAX_BYTES || Text.find('\0') != std::string_view::npos || !NeteaseLyrics::IsValidUtf8(Text))
			return false;
		if(Text.starts_with("\xEF\xBB\xBF"))
			Text.remove_prefix(3);
		SLyricsData Parsed;
		while(!Text.empty())
		{
			const size_t Newline = Text.find('\n');
			const std::string_view LineText = Trim(Text.substr(0, Newline));
			Text = Newline == std::string_view::npos ? std::string_view() : Text.substr(Newline + 1);
			if(LineText.empty())
				continue;
			NeteaseLyrics::STimeline LineTimeline;
			// 逐行验证，避免宽松的来源解析器跳过损坏行后把残缺响应写进缓存。
			if(!NeteaseLyrics::ParseYrc(LineText, &LineTimeline) || LineTimeline.m_vLines.size() != 1)
				return false;
			auto &Line = LineTimeline.m_vLines.front();
			if(!NormalizeLine(Line))
				return false;
			Parsed.m_Timeline.m_vLines.push_back(std::move(Line));
		}
		if(Parsed.m_Timeline.m_vLines.empty())
			return false;
		auto &Lines = Parsed.m_Timeline.m_vLines;
		std::stable_sort(Lines.begin(), Lines.end(), [](const auto &Left, const auto &Right) { return Left.m_StartMs < Right.m_StartMs; });
		Parsed.m_Timeline.m_HasTiming = true;
		*pOut = std::move(Parsed);
		return true;
	}

	bool HasWordTiming(const SLyricsData &Data)
	{
		return Data.HasLyrics() && std::any_of(Data.m_Timeline.m_vLines.begin(), Data.m_Timeline.m_vLines.end(), HasLineWordTiming);
	}
}
