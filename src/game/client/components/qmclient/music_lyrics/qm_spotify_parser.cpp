#include "qm_spotify_parser.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

namespace QmSpotify
{
	namespace
	{
		using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

		const json_value *Field(const json_value *pObject, const char *pName)
		{
			if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		}

		std::string StringValue(const json_value *pValue)
		{
			if(pValue == nullptr || pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return {};
			return std::string(pValue->u.string.ptr, pValue->u.string.length);
		}

		bool TryInt64Value(const json_value *pValue, int64_t &Value)
		{
			if(pValue == nullptr)
				return false;
			if(pValue->type == json_integer)
			{
				Value = pValue->u.integer;
				return true;
			}
			// INT64_MAX转double会舍入到2^63，故上界必须使用严格小于。
			if(pValue->type != json_double || !std::isfinite(pValue->u.dbl) ||
				pValue->u.dbl < -0x1p63 || pValue->u.dbl >= 0x1p63)
				return false;
			Value = static_cast<int64_t>(pValue->u.dbl);
			return true;
		}

		int64_t Int64Value(const json_value *pValue, int64_t Fallback = 0)
		{
			int64_t Value;
			return TryInt64Value(pValue, Value) ? Value : Fallback;
		}

		int64_t LrclibDurationMs(const json_value *pValue)
		{
			if(pValue == nullptr)
				return 0;
			if(pValue->type == json_integer)
			{
				const int64_t Seconds = pValue->u.integer;
				if(Seconds < 0 || Seconds > std::numeric_limits<int64_t>::max() / 1000)
					return 0;
				return Seconds * 1000;
			}
			if(pValue->type != json_double)
				return 0;
			// LRCLIB使用秒，小数部分须先换算为毫秒再截断。
			const double Milliseconds = pValue->u.dbl * 1000.0;
			if(!std::isfinite(Milliseconds) || Milliseconds < 0.0 || Milliseconds >= 0x1p63)
				return 0;
			return static_cast<int64_t>(Milliseconds);
		}

		// color-lyrics 的时间戳字段是字符串,兼容数字。
		int64_t ParseTimeMs(const json_value *pValue, int64_t Fallback)
		{
			if(pValue == nullptr)
				return Fallback;
			if(pValue->type == json_integer || pValue->type == json_double)
				return Int64Value(pValue, Fallback);
			if(pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return Fallback;
			const std::string_view Text(pValue->u.string.ptr, pValue->u.string.length);
			if(Text.empty())
				return Fallback;
			int64_t Result = 0;
			bool Any = false;
			for(char C : Text)
			{
				if(C < '0' || C > '9' || Result > (std::numeric_limits<int64_t>::max() - (C - '0')) / 10)
					return Fallback;
				Result = Result * 10 + (C - '0');
				Any = true;
			}
			return Any ? Result : Fallback;
		}

		// 从 ByteOffset 起前进 Count 个 UTF-8 码点,返回新字节偏移;不足则到字符串尾。
		size_t AdvanceUtf8(std::string_view Text, size_t ByteOffset, size_t Count)
		{
			size_t Offset = ByteOffset;
			for(size_t i = 0; i < Count && Offset < Text.size(); ++i)
			{
				const unsigned char Lead = (unsigned char)Text[Offset];
				size_t Extra = 0;
				if((Lead & 0x80) == 0)
					Extra = 0;
				else if((Lead & 0xE0) == 0xC0)
					Extra = 1;
				else if((Lead & 0xF0) == 0xE0)
					Extra = 2;
				else if((Lead & 0xF8) == 0xF0)
					Extra = 3;
				Offset += 1 + Extra;
				if(Offset > Text.size())
					Offset = Text.size();
			}
			return Offset;
		}

		TJson ParseJson(std::string_view Json)
		{
			if(Json.empty())
				return {nullptr, json_value_free};
			const std::string Copy(Json);
			return {json_parse(Copy.c_str(), Copy.size()), json_value_free};
		}

		// pathfinder 与 /v1/search 共用的行解析(兼容两种节点形状)。
		bool ParseSearchItem(const json_value *pItem, STrackCandidate *pOut)
		{
			if(pItem == nullptr || pOut == nullptr)
				return false;
			const json_value *pData = Field(pItem, "data");
			const json_value *pNode = pData != nullptr ? pData : pItem;
			const std::string Id = StringValue(Field(pNode, "id"));
			std::string Uri = StringValue(Field(pNode, "uri"));
			const std::string_view PREFIX = "spotify:track:";
			std::string TrackId = Id;
			if(TrackId.empty() && Uri.size() > PREFIX.size() && Uri.substr(0, PREFIX.size()) == PREFIX)
				TrackId = Uri.substr(PREFIX.size());
			if(TrackId.empty())
				return false;
			const std::string Title = StringValue(Field(pNode, "name"));
			if(Title.empty())
				return false;

			pOut->m_Id = std::move(TrackId);
			pOut->m_Title = Title;

			std::string Artist;
			// pathfinder 形状是 {"artists":{"items":[...]}},Web API 形状是 artists 数组。
			const json_value *pArtists = Field(pNode, "artists");
			const json_value *pArtistItems = nullptr;
			if(pArtists != nullptr && pArtists->type == json_array)
				pArtistItems = pArtists;
			else if(pArtists != nullptr && pArtists->type == json_object)
				pArtistItems = Field(pArtists, "items");
			if(pArtistItems != nullptr && pArtistItems->type == json_array)
			{
				for(unsigned int i = 0; i < pArtistItems->u.array.length; ++i)
				{
					const json_value *pArtist = pArtistItems->u.array.values[i];
					if(pArtist == nullptr)
						continue;
					std::string Name;
					const json_value *pProfile = Field(pArtist, "profile");
					if(pProfile != nullptr)
						Name = StringValue(Field(pProfile, "name"));
					if(Name.empty())
						Name = StringValue(Field(pArtist, "name"));
					if(!Name.empty())
					{
						if(!Artist.empty())
							Artist += ", ";
						Artist += Name;
					}
				}
			}
			const json_value *pTrack = Field(pNode, "track");
			if(Artist.empty() && pTrack != nullptr)
			{
				const json_value *pTrackArtists = Field(pTrack, "artists");
				const json_value *pTrackArtistItems = nullptr;
				if(pTrackArtists != nullptr && pTrackArtists->type == json_array)
					pTrackArtistItems = pTrackArtists;
				else if(pTrackArtists != nullptr && pTrackArtists->type == json_object)
					pTrackArtistItems = Field(pTrackArtists, "items");
				if(pTrackArtistItems != nullptr && pTrackArtistItems->type == json_array)
				{
					for(unsigned int i = 0; i < pTrackArtistItems->u.array.length; ++i)
					{
						const json_value *pArtist = pTrackArtistItems->u.array.values[i];
						if(pArtist == nullptr)
							continue;
						const std::string Name = StringValue(Field(pArtist, "name"));
						if(!Name.empty())
						{
							if(!Artist.empty())
								Artist += ", ";
							Artist += Name;
						}
					}
				}
			}
			pOut->m_Artist = std::move(Artist);

			std::string Album;
			const json_value *pAlbumOfTrack = Field(pNode, "albumOfTrack");
			if(pAlbumOfTrack != nullptr)
				Album = StringValue(Field(pAlbumOfTrack, "name"));
			if(Album.empty())
			{
				const json_value *pAlbum = Field(pNode, "album");
				if(pAlbum != nullptr)
					Album = StringValue(Field(pAlbum, "name"));
			}
			pOut->m_Album = std::move(Album);

			const json_value *pExternalIds = Field(pNode, "external_ids");
			if(pExternalIds != nullptr)
				pOut->m_Isrc = StringValue(Field(pExternalIds, "isrc"));
			return true;
		}

		// 首个翻译轨的下标对应过滤前的主行。
		const json_value *FirstTranslationLines(const json_value *pLyricsObject)
		{
			const json_value *pAlternatives = Field(pLyricsObject, "alternatives");
			if(pAlternatives == nullptr || pAlternatives->type != json_array || pAlternatives->u.array.length == 0)
				return nullptr;
			const json_value *pFirst = pAlternatives->u.array.values[0];
			if(pFirst == nullptr)
				return nullptr;
			const json_value *pLines = Field(pFirst, "lines");
			if(pLines == nullptr || pLines->type != json_array)
				return nullptr;
			return pLines;
		}
	} // namespace

	bool ParseColorLyrics(std::string_view Json, QmMusicLyrics::SLyricsData *pOut)
	{
		if(pOut == nullptr)
			return false;
		const auto pRoot = ParseJson(Json);
		if(!pRoot)
			return false;
		const json_value *pLyrics = Field(pRoot.get(), "lyrics");
		const bool HasLyrics = pLyrics != nullptr && pLyrics->type == json_object;
		const std::string SyncType = HasLyrics ? StringValue(Field(pLyrics, "syncType")) : "";
		const json_value *pLines = HasLyrics ? Field(pLyrics, "lines") : nullptr;
		const bool HasLines = pLines != nullptr && pLines->type == json_array;
		const bool Timed = HasLines && SyncType != "UNSYNCED";
		const json_value *pTranslationLines = FirstTranslationLines(pLyrics);
		struct SLineCandidate
		{
			NeteaseLyrics::SLine m_Line;
			std::string m_Translation;
		};

		QmMusicLyrics::SLyricsData Result;
		std::vector<SLineCandidate> vCandidates;
		if(HasLines)
		{
			for(unsigned int Index = 0; Index < pLines->u.array.length; ++Index)
			{
				const json_value *pLine = pLines->u.array.values[Index];
				if(pLine == nullptr)
					continue;
				const std::string Words = StringValue(Field(pLine, "words"));
				if(Words.empty())
					continue;
				const int64_t StartMs = ParseTimeMs(Field(pLine, "startTimeMs"), -1);
				const int64_t EndMs = ParseTimeMs(Field(pLine, "endTimeMs"), -1);
				if(Timed && StartMs < 0)
					continue;
				SLineCandidate Candidate;
				NeteaseLyrics::SLine &Line = Candidate.m_Line;
				Line.m_StartMs = StartMs;
				Line.m_EndMs = EndMs;
				Line.m_Text = Words;
				const json_value *pSyllables = Field(pLine, "syllables");
				if(pSyllables != nullptr && pSyllables->type == json_array && pSyllables->u.array.length > 0)
				{
					// 音节级:按 numChars 从行文本切词,生成词级时间轴。
					size_t ByteOffset = 0;
					for(unsigned int s = 0; s < pSyllables->u.array.length; ++s)
					{
						const json_value *pSyllable = pSyllables->u.array.values[s];
						if(pSyllable == nullptr)
							continue;
						const int64_t WordStart = ParseTimeMs(Field(pSyllable, "startTimeMs"), -1);
						const int64_t WordEnd = ParseTimeMs(Field(pSyllable, "endTimeMs"), -1);
						const int64_t CharCount = ParseTimeMs(Field(pSyllable, "numChars"), 0);
						if(WordStart < 0 || CharCount <= 0)
							continue;
						const size_t NextOffset = AdvanceUtf8(Line.m_Text, ByteOffset, (size_t)CharCount);
						if(NextOffset <= ByteOffset)
							continue;
						NeteaseLyrics::SWord Word;
						Word.m_StartMs = WordStart;
						Word.m_EndMs = WordEnd >= 0 ? WordEnd : WordStart;
						Word.m_Text = Line.m_Text.substr(ByteOffset, NextOffset - ByteOffset);
						Line.m_vWords.push_back(std::move(Word));
						ByteOffset = NextOffset;
						if(ByteOffset >= Line.m_Text.size())
							break;
					}
				}
				if(pTranslationLines != nullptr && Index < pTranslationLines->u.array.length)
					Candidate.m_Translation = StringValue(pTranslationLines->u.array.values[Index]);
				vCandidates.push_back(std::move(Candidate));
			}
		}
		Result.m_Timeline.m_HasTiming = Timed && !vCandidates.empty();
		// 主行与译文一起稳定排序，过滤和同刻行都保留原始对应关系。
		if(Result.m_Timeline.m_HasTiming)
		{
			std::stable_sort(vCandidates.begin(), vCandidates.end(),
				[](const SLineCandidate &Left, const SLineCandidate &Right) {
					return Left.m_Line.m_StartMs < Right.m_Line.m_StartMs;
				});
		}
		Result.m_Timeline.m_vLines.reserve(vCandidates.size());
		if(pTranslationLines != nullptr)
			Result.m_vTranslations.reserve(vCandidates.size());
		for(auto &Candidate : vCandidates)
		{
			Result.m_Timeline.m_vLines.push_back(std::move(Candidate.m_Line));
			if(pTranslationLines != nullptr)
				Result.m_vTranslations.push_back(std::move(Candidate.m_Translation));
		}
		*pOut = std::move(Result);
		return true;
	}

	bool ParseSearchResponse(std::string_view Json, std::vector<STrackCandidate> *pOut)
	{
		if(pOut == nullptr)
			return false;
		pOut->clear();
		const auto pRoot = ParseJson(Json);
		if(!pRoot)
			return false;
		const json_value *pData = Field(pRoot.get(), "data");
		const json_value *pTracks = nullptr;
		if(pData != nullptr)
		{
			const json_value *pSearchV2 = Field(pData, "searchV2");
			if(pSearchV2 != nullptr)
				pTracks = Field(Field(pSearchV2, "tracks"), "items");
			if(pTracks == nullptr)
			{
				const json_value *pSearch = Field(pData, "search");
				if(pSearch != nullptr)
					pTracks = Field(Field(pSearch, "tracks"), "items");
			}
		}
		if(pTracks == nullptr)
		{
			const json_value *pTracksV1 = Field(pRoot.get(), "tracks");
			if(pTracksV1 != nullptr)
				pTracks = Field(pTracksV1, "items");
		}
		if(pTracks != nullptr && pTracks->type == json_array)
		{
			for(unsigned int Index = 0; Index < pTracks->u.array.length; ++Index)
			{
				STrackCandidate Candidate;
				if(ParseSearchItem(pTracks->u.array.values[Index], &Candidate))
					pOut->push_back(std::move(Candidate));
			}
		}
		return !pOut->empty();
	}

	bool ParseServerTime(std::string_view Json, int64_t *pOut)
	{
		if(pOut == nullptr)
			return false;
		const auto pRoot = ParseJson(Json);
		if(!pRoot)
			return false;
		return TryInt64Value(Field(pRoot.get(), "serverTime"), *pOut);
	}

	bool ParseTokenResponse(std::string_view Json, std::string *pAccessToken, int64_t *pExpirationMs)
	{
		if(pAccessToken == nullptr || pExpirationMs == nullptr)
			return false;
		const auto pRoot = ParseJson(Json);
		if(!pRoot)
			return false;
		const std::string Token = StringValue(Field(pRoot.get(), "accessToken"));
		const int64_t Expiration = Int64Value(Field(pRoot.get(), "accessTokenExpirationTimestampMs"));
		const json_value *pAnonymous = Field(pRoot.get(), "isAnonymous");
		const bool Anonymous = pAnonymous != nullptr && pAnonymous->type == json_boolean && pAnonymous->u.boolean != 0;
		if(Token.empty() || Expiration <= 0 || Anonymous)
			return false;
		*pAccessToken = Token;
		*pExpirationMs = Expiration;
		return true;
	}

	bool ParseLrclib(std::string_view Json, QmMusicLyrics::SLyricsData *pOut)
	{
		if(pOut == nullptr)
			return false;
		const auto pRoot = ParseJson(Json);
		if(!pRoot)
			return false;
		QmMusicLyrics::SLyricsData Result;
		Result.m_Song.m_Title = StringValue(Field(pRoot.get(), "trackName"));
		Result.m_Song.m_Artist = StringValue(Field(pRoot.get(), "artistName"));
		Result.m_Song.m_Album = StringValue(Field(pRoot.get(), "albumName"));
		Result.m_Song.m_DurationMs = LrclibDurationMs(Field(pRoot.get(), "duration"));
		const std::string Synced = StringValue(Field(pRoot.get(), "syncedLyrics"));
		bool Ok = false;
		if(!Synced.empty())
		{
			Ok = NeteaseLyrics::ParseLrc(Synced, &Result.m_Timeline);
			if(Ok)
				Ok = Result.m_Timeline.m_HasTiming && !Result.m_Timeline.m_vLines.empty();
		}
		if(!Ok)
		{
			// 无时间轴时退回纯文本(整首作为无时间轴行,由 HasLyrics 判定不可用)。
			const std::string Plain = StringValue(Field(pRoot.get(), "plainLyrics"));
			if(!Plain.empty())
			{
				NeteaseLyrics::SLine Line;
				Line.m_Text = Plain;
				Result.m_Timeline.m_vLines.push_back(std::move(Line));
				Ok = true;
			}
		}
		if(!Ok)
			return false;
		*pOut = std::move(Result);
		return true;
	}
} // namespace QmSpotify
