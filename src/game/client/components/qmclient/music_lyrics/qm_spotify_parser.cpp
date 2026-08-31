#include "qm_spotify_parser.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace QmSpotify
{
	namespace
	{
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

		int64_t Int64Value(const json_value *pValue)
		{
			if(pValue == nullptr)
				return 0;
			if(pValue->type == json_integer)
				return (int64_t)pValue->u.integer;
			if(pValue->type == json_double)
				return (int64_t)pValue->u.dbl;
			return 0;
		}

		// color-lyrics 的时间戳字段是字符串,兼容数字。
		int64_t ParseTimeMs(const json_value *pValue, int64_t Fallback)
		{
			if(pValue == nullptr)
				return Fallback;
			if(pValue->type == json_integer || pValue->type == json_double)
				return Int64Value(pValue);
			if(pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return Fallback;
			const std::string_view Text(pValue->u.string.ptr, pValue->u.string.length);
			if(Text.empty())
				return Fallback;
			int64_t Result = 0;
			bool Any = false;
			for(char C : Text)
			{
				if(C < '0' || C > '9')
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

		json_value *ParseJson(std::string_view Json)
		{
			if(Json.empty())
				return nullptr;
			const std::string Copy(Json);
			return json_parse(Copy.c_str(), Copy.size());
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

		// 把 alternatives[0] 的翻译行按下标对齐到主歌词。
		void ApplyTranslations(const json_value *pLyricsObject, QmMusicLyrics::SLyricsData *pOut)
		{
			pOut->m_vTranslations.clear();
			const json_value *pAlternatives = Field(pLyricsObject, "alternatives");
			if(pAlternatives == nullptr || pAlternatives->type != json_array || pAlternatives->u.array.length == 0)
				return;
			const json_value *pFirst = pAlternatives->u.array.values[0];
			if(pFirst == nullptr)
				return;
			const json_value *pLines = Field(pFirst, "lines");
			if(pLines == nullptr || pLines->type != json_array)
				return;
			const size_t MainCount = pOut->m_Timeline.m_vLines.size();
			const size_t Count = std::min<size_t>(MainCount, pLines->u.array.length);
			pOut->m_vTranslations.resize(MainCount);
			for(size_t i = 0; i < Count; ++i)
				pOut->m_vTranslations[i] = StringValue(pLines->u.array.values[i]);
		}
	} // namespace

	bool ParseColorLyrics(std::string_view Json, QmMusicLyrics::SLyricsData *pOut)
	{
		if(pOut == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		const json_value *pLyrics = Field(pRoot, "lyrics");
		const bool HasLyrics = pLyrics != nullptr && pLyrics->type == json_object;
		const std::string SyncType = HasLyrics ? StringValue(Field(pLyrics, "syncType")) : "";
		const json_value *pLines = HasLyrics ? Field(pLyrics, "lines") : nullptr;
		const bool HasLines = pLines != nullptr && pLines->type == json_array;
		const bool Timed = HasLines && SyncType != "UNSYNCED";

		QmMusicLyrics::SLyricsData Result;
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
				NeteaseLyrics::SLine Line;
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
				Result.m_Timeline.m_vLines.push_back(std::move(Line));
			}
		}
		Result.m_Timeline.m_HasTiming = Timed && !Result.m_Timeline.m_vLines.empty();
		// 行级歌词可能有跨行覆盖(合唱),保持 Spotify 顺序并做稳定排序,保证二分选择正确。
		if(Result.m_Timeline.m_HasTiming)
		{
			std::stable_sort(Result.m_Timeline.m_vLines.begin(), Result.m_Timeline.m_vLines.end(),
				[](const NeteaseLyrics::SLine &Left, const NeteaseLyrics::SLine &Right) {
					return Left.m_StartMs < Right.m_StartMs;
				});
		}
		if(HasLyrics)
			ApplyTranslations(pLyrics, &Result);
		json_value_free(pRoot);
		*pOut = std::move(Result);
		return true;
	}

	bool ParseSearchResponse(std::string_view Json, std::vector<STrackCandidate> *pOut)
	{
		if(pOut == nullptr)
			return false;
		pOut->clear();
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		const json_value *pData = Field(pRoot, "data");
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
			const json_value *pTracksV1 = Field(pRoot, "tracks");
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
		json_value_free(pRoot);
		return !pOut->empty();
	}

	bool ParseServerTime(std::string_view Json, int64_t *pOut)
	{
		if(pOut == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		const json_value *pServerTime = Field(pRoot, "serverTime");
		const bool Ok = pServerTime != nullptr && (pServerTime->type == json_integer || pServerTime->type == json_double);
		if(Ok)
			*pOut = Int64Value(pServerTime);
		json_value_free(pRoot);
		return Ok;
	}

	bool ParseTokenResponse(std::string_view Json, std::string *pAccessToken, int64_t *pExpirationMs)
	{
		if(pAccessToken == nullptr || pExpirationMs == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		const std::string Token = StringValue(Field(pRoot, "accessToken"));
		const int64_t Expiration = Int64Value(Field(pRoot, "accessTokenExpirationTimestampMs"));
		const json_value *pAnonymous = Field(pRoot, "isAnonymous");
		const bool Anonymous = pAnonymous != nullptr && pAnonymous->type == json_boolean && pAnonymous->u.boolean != 0;
		json_value_free(pRoot);
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
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		QmMusicLyrics::SLyricsData Result;
		Result.m_Song.m_Title = StringValue(Field(pRoot, "trackName"));
		Result.m_Song.m_Artist = StringValue(Field(pRoot, "artistName"));
		Result.m_Song.m_Album = StringValue(Field(pRoot, "albumName"));
		Result.m_Song.m_DurationMs = Int64Value(Field(pRoot, "duration"));
		const std::string Synced = StringValue(Field(pRoot, "syncedLyrics"));
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
			const std::string Plain = StringValue(Field(pRoot, "plainLyrics"));
			if(!Plain.empty())
			{
				NeteaseLyrics::SLine Line;
				Line.m_Text = Plain;
				Result.m_Timeline.m_vLines.push_back(std::move(Line));
				Ok = true;
			}
		}
		json_value_free(pRoot);
		if(!Ok)
			return false;
		*pOut = std::move(Result);
		return true;
	}
} // namespace QmSpotify
