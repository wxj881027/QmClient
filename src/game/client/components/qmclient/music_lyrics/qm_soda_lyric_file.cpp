#include "qm_soda_lyric_file.h"

#include "music_lyrics_krc.h"
#include "music_lyrics_qrc.h"

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace QmSodaLyricFile
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
				return static_cast<int64_t>(pValue->u.integer);
			// INT64_MAX转double会舍入到2^63，转换前须排除这个上界。
			if(pValue->type == json_double && std::isfinite(pValue->u.dbl) &&
				pValue->u.dbl >= -0x1p63 && pValue->u.dbl < 0x1p63)
				return static_cast<int64_t>(pValue->u.dbl);
			return 0;
		}
	}

	bool ParseLyricFileJson(std::string_view Json, QmMusicLyrics::SLyricsData *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		*pOut = {};
		std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(json_parse(Json.data(), Json.size()), json_value_free);
		if(!pRoot || pRoot->type != json_object)
		{
			pRoot.reset();
			if(pError)
				*pError = "invalid json";
			return false;
		}
		pOut->m_Song.m_Title = StringValue(Field(pRoot.get(), "title"));
		pOut->m_Song.m_Artist = StringValue(Field(pRoot.get(), "artist"));
		pOut->m_Song.m_Album = StringValue(Field(pRoot.get(), "album"));
		pOut->m_Song.m_CoverPath = StringValue(Field(pRoot.get(), "coverUrl"));
		pOut->m_Song.m_DurationMs = Int64Value(Field(pRoot.get(), "durationMs"));
		const std::string LyricType = StringValue(Field(pRoot.get(), "lyricType"));
		const std::string LyricContent = StringValue(Field(pRoot.get(), "lyricContent"));
		const std::string TranslationLrc = StringValue(Field(pRoot.get(), "translationLrc"));
		pRoot.reset();

		bool Parsed = false;
		std::string Error;
		if(LyricType == "krc")
		{
			if(!TranslationLrc.empty())
				Parsed = QmMusicLyrics::ParseKrcText(LyricContent, &pOut->m_Timeline, &Error);
			else
			{
				QmMusicLyrics::SLyricsData Lyrics;
				Parsed = QmMusicLyrics::ParseKrcTextData(LyricContent, &Lyrics, &Error);
				if(Parsed)
				{
					pOut->m_Timeline = std::move(Lyrics.m_Timeline);
					pOut->m_vTranslations = std::move(Lyrics.m_vTranslations);
				}
			}
		}
		else if(LyricType == "qrc")
		{
			std::string Content;
			if(LyricContent.find("LyricContent=") != std::string::npos || LyricContent.find("<QrcInfos") != std::string::npos)
			{
				if(QmMusicLyrics::ExtractQrcLyricContent(LyricContent, &Content, &Error))
					Parsed = QmMusicLyrics::ParseQrcRlrc(Content, &pOut->m_Timeline, &Error);
			}
			else
				Parsed = QmMusicLyrics::ParseQrcRlrc(LyricContent, &pOut->m_Timeline, &Error);
		}
		else
			Parsed = NeteaseLyrics::ParseLrc(LyricContent, &pOut->m_Timeline, &Error);
		if(!Parsed)
		{
			if(pError)
				*pError = Error.empty() ? "lyric parse failed" : Error;
			return false;
		}
		if(!TranslationLrc.empty())
			QmMusicLyrics::AlignTranslations(pOut->m_Timeline, TranslationLrc, &pOut->m_vTranslations);
		return true;
	}
} // namespace QmSodaLyricFile
