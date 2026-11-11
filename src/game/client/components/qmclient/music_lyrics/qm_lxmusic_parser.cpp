#include "qm_lxmusic_parser.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace QmLxMusic
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

		// LX 的 progress/duration 是浮点秒;缺字段或类型不符按 0 处理。
		double DoubleValue(const json_value *pValue)
		{
			switch(pValue != nullptr ? pValue->type : json_none)
			{
			case json_double:
				return pValue->u.dbl;
			case json_integer:
				return (double)pValue->u.integer;
			default:
				return 0.0;
			}
		}

		json_value *ParseJson(std::string_view Json)
		{
			if(Json.empty())
				return nullptr;
			const std::string Copy(Json);
			return json_parse(Copy.c_str(), Copy.size());
		}

		int64_t SecondsToMs(double Seconds)
		{
			if(!std::isfinite(Seconds) || Seconds <= 0.0)
				return 0;
			// 防止异常大值溢出;一小时以上的单曲时长已无实际意义。
			return (int64_t)std::llround(std::min(Seconds, 24.0 * 60.0 * 60.0) * 1000.0);
		}
	} // namespace

	bool ParseStatus(std::string_view Json, SStatus *pOut)
	{
		if(pOut == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		if(pRoot->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		SStatus Status;
		Status.m_Valid = true;
		Status.m_PlaybackStatus = StringValue(Field(pRoot, "status"));
		Status.m_Title = StringValue(Field(pRoot, "name"));
		Status.m_Artist = StringValue(Field(pRoot, "singer"));
		Status.m_Album = StringValue(Field(pRoot, "albumName"));
		Status.m_CoverUrl = StringValue(Field(pRoot, "picUrl"));
		Status.m_PositionMs = SecondsToMs(DoubleValue(Field(pRoot, "progress")));
		Status.m_DurationMs = SecondsToMs(DoubleValue(Field(pRoot, "duration")));
		const double Rate = DoubleValue(Field(pRoot, "playbackRate"));
		Status.m_PlaybackRate = Rate > 0.0 && std::isfinite(Rate) ? std::min(Rate, 8.0) : 1.0;
		json_value_free(pRoot);

		*pOut = std::move(Status);
		return true;
	}

	bool ParseLyricAll(std::string_view Json, SLyricPayload *pOut)
	{
		if(pOut == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;
		if(pRoot->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		SLyricPayload Payload;
		Payload.m_Valid = true;
		Payload.m_Lyric = StringValue(Field(pRoot, "lyric"));
		Payload.m_TranslationLrc = StringValue(Field(pRoot, "tlyric"));
		json_value_free(pRoot);

		*pOut = std::move(Payload);
		return true;
	}

	bool BuildLyrics(const SStatus &Status, const SLyricPayload &Payload, QmMusicLyrics::SLyricsData *pOut)
	{
		if(pOut == nullptr || Payload.m_Lyric.empty())
			return false;

		QmMusicLyrics::SLyricsData Data;
		if(!NeteaseLyrics::ParseLrc(Payload.m_Lyric, &Data.m_Timeline))
			return false;
		// 主轨无有效时间戳时不构造虚假时间轴,交由调用方继续重试。
		if(Data.m_Timeline.m_vLines.empty())
			return false;

		QmMusicLyrics::AlignTranslations(Data.m_Timeline, Payload.m_TranslationLrc, &Data.m_vTranslations);
		Data.m_Song.m_Title = Status.m_Title;
		Data.m_Song.m_Artist = Status.m_Artist;
		Data.m_Song.m_Album = Status.m_Album;
		Data.m_Song.m_DurationMs = Status.m_DurationMs;

		*pOut = std::move(Data);
		return true;
	}

	uint64_t IdentityHash(std::string_view Title, std::string_view Artist)
	{
		uint64_t Hash = 1469598103934665603ull;
		auto Mix = [&Hash](char C) {
			Hash ^= (unsigned char)C;
			Hash *= 1099511628211ull;
		};
		for(char C : Title)
			Mix(C);
		Mix('\x1f');
		for(char C : Artist)
			Mix(C);
		return Hash;
	}
}
