#include "qm_qqmusic_protocol.h"

#include <engine/external/json-parser/json.h>
#include <engine/external/zlib/zlib.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_qrc.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace QmMusicHook::QQMusic
{
	namespace
	{
		// 数据来自 PlayerCap 的 CE/qqprobe 实测表；仅支持对应 x86 版本，不猜测邻近版本。
		constexpr SOffsets OFFSETS[] = {
			{20, 5, 0xB63ED0, 0x70, 0x74, 0x78, 0x40, 0x44, 0x68, 0, 0, 0, 0xB61E78, true, true},
			{21, 81, 0xB75840, 0, 0x18, 0x30, 0x60, 0x68, 0x6C, 0, 0, 0, 0, false, false},
			{22, 16, 0xC87C80, 0, 0x18, 0x30, 0x60, 0x68, 0x6C, 0, 0xC157D8, 0x618, 0, false, false},
			{22, 22, 0xC95EA0, 0x80, 0x98, 0xB0, 0xE0, 0xE8, 0xEC, 0, 0xC23994, 0x798, 0, false, false},
			{22, 31, 0xC332D8, 0, 4, 8, 0x71FD8, 0x10, 0x0C, 0x71FE0, 0, 0, 0, true, false},
			{22, 41, 0xC40548, 0, 4, 8, 0x722E8, 0x10, 0x0C, 0x722F0, 0, 0, 0, true, false},
			{22, 52, 0xC58BD0, 0, 4, 8, 0x726C0, 0x10, 0x0C, 0x726C8, 0, 0, 0, true, false},
			{22, 60, 0xC6DBA0, 0, 4, 8, 0x726F0, 0x10, 0x0C, 0x726F8, 0, 0, 0, true, false}};

		bool ValidMid(std::string_view Mid)
		{
			return Mid.size() == 14 && std::all_of(Mid.begin(), Mid.end(), [](unsigned char C) {
				return (C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
			});
		}

		const json_value *Field(const json_value *pObject, std::string_view Name)
		{
			if(!pObject || pObject->type != json_object)
				return nullptr;
			for(unsigned i = 0; i < pObject->u.object.length; ++i)
			{
				const auto &Entry = pObject->u.object.values[i];
				if(std::string_view(Entry.name, Entry.name_length) == Name)
					return Entry.value;
			}
			return nullptr;
		}

		bool IsInteger(const json_value *pValue, int64_t Value)
		{
			return pValue && pValue->type == json_integer && pValue->u.integer == Value;
		}

		std::string String(const json_value *pValue)
		{
			return pValue && pValue->type == json_string ? std::string(pValue->u.string.ptr, pValue->u.string.length) : std::string();
		}

		int HexDigit(char C)
		{
			if(C >= '0' && C <= '9')
				return C - '0';
			if(C >= 'a' && C <= 'f')
				return C - 'a' + 10;
			if(C >= 'A' && C <= 'F')
				return C - 'A' + 10;
			return -1;
		}

		bool DecodeLyric(std::string_view Raw, std::string &Out)
		{
			Out.clear();
			const bool Hex = !Raw.empty() && std::all_of(Raw.begin(), Raw.end(), [](char C) { return HexDigit(C) >= 0; });
			if(Hex)
			{
				if(Raw.size() % 16 != 0)
					return false;
				std::vector<uint8_t> Cipher(Raw.size() / 2);
				for(size_t i = 0; i < Cipher.size(); ++i)
					Cipher[i] = static_cast<uint8_t>((HexDigit(Raw[i * 2]) << 4) | HexDigit(Raw[i * 2 + 1]));
				std::vector<uint8_t> Plain;
				constexpr char KEY[] = "!@#)(*$%123ZXC!@!@#)(NHL";
				static_assert(sizeof(KEY) == 25);
				if(!QmMusicLyrics::QrcTripleDesDecrypt(Cipher.data(), Cipher.size(), reinterpret_cast<const uint8_t *>(KEY), &Plain))
					return false;
				Out.resize(2 * 1024 * 1024);
				uLongf Size = static_cast<uLongf>(Out.size());
				if(uncompress(reinterpret_cast<Bytef *>(Out.data()), &Size, Plain.data(), static_cast<uLong>(Plain.size())) != Z_OK)
				{
					Out.clear();
					return false;
				}
				Out.resize(Size);
			}
			else
				Out.assign(Raw);
			if(Out.find("LyricContent=\"") != std::string::npos)
			{
				std::string Content;
				if(!QmMusicLyrics::ExtractQrcLyricContent(Out, &Content))
					return false;
				Out = std::move(Content);
			}
			else
			{
				// 复用 XML 属性解码处理官方接口的实体，先保护原有引号。
				std::string Attribute;
				for(char C : Out)
					Attribute += C == '"' ? "&quot;" : std::string(1, C);
				QmMusicLyrics::ExtractQrcLyricContent("LyricContent=\"" + Attribute + "\"", &Out);
			}
			return true;
		}
	}

	const SOffsets *FindOffsets(int Major, int Minor)
	{
		for(const auto &Offsets : OFFSETS)
			if(Offsets.m_Major == Major && Offsets.m_Minor == Minor)
				return &Offsets;
		return nullptr;
	}

	std::string ExtractSongMid(std::string_view StreamUrl, std::string_view Parameters)
	{
		const size_t Scheme = StreamUrl.find("://");
		if(Scheme != std::string_view::npos && (StreamUrl.substr(0, Scheme) == "https" || StreamUrl.substr(0, Scheme) == "http"))
		{
			const size_t Path = StreamUrl.find('/', Scheme + 3);
			const auto Host = StreamUrl.substr(Scheme + 3, Path - (Scheme + 3));
			const auto Suffix = std::string_view(".qqmusic.qq.com");
			if(Path != std::string_view::npos && Host.size() > Suffix.size() && Host.substr(Host.size() - Suffix.size()) == Suffix)
			{
				const auto File = StreamUrl.substr(StreamUrl.rfind('/') + 1);
				const auto Mid = File.substr(0, File.find('.'));
				if(ValidMid(Mid))
					return std::string(Mid);
			}
		}
		while(!Parameters.empty())
		{
			const size_t End = Parameters.find('&');
			const auto Param = Parameters.substr(0, End);
			if(Param.substr(0, 2) == "0=" && ValidMid(Param.substr(2)))
				return std::string(Param.substr(2));
			if(End == std::string_view::npos)
				break;
			Parameters.remove_prefix(End + 1);
		}
		return {};
	}

	bool DecodeSsoLayout(const unsigned char *pData, size_t Size, SSsoLayout &Out)
	{
		Out = {};
		if(!pData || Size < 24)
			return false;
		uint32_t Capacity;
		std::memcpy(&Out.m_Length, pData + 16, 4);
		std::memcpy(&Capacity, pData + 20, 4);
		if(Out.m_Length == 0 || Out.m_Length >= 1024 || Out.m_Length > Capacity)
			return false;
		if(Capacity > 15)
		{
			std::memcpy(&Out.m_Pointer, pData, 4);
			return Out.m_Pointer >= 0x10000;
		}
		return Out.m_Length <= 15;
	}

	bool ParseLyricsResponse(std::string_view Json, uint32_t ExpectedSongId, bool MidRequest, SLyrics &Out)
	{
		Out = {};
		if(Json.size() > 4 * 1024 * 1024)
			return false;
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
		const json_value *pData = Root.get();
		if(MidRequest)
		{
			if(!IsInteger(Field(pData, "retcode"), 0))
				return false;
		}
		else
		{
			const auto *pRequest = Field(pData, "req_0");
			if(!IsInteger(Field(pRequest, "code"), 0))
				return false;
			pData = Field(pRequest, "data");
			if(!pData || pData->type != json_object)
				return false;
			// 同一接口有 songID/songId 两种字段拼写；有字段时必须精确匹配。
			const auto *pSongId = Field(pData, "songID");
			if(!pSongId)
				pSongId = Field(pData, "songId");
			if(ExpectedSongId == 0 || (pSongId && !IsInteger(pSongId, ExpectedSongId)))
				return false;
		}
		const auto *pLyric = Field(pData, "lyric");
		if(!pLyric || pLyric->type != json_string)
			return false;
		SLyrics Result;
		if(!DecodeLyric(String(pLyric), Result.m_Content))
			return false;
		if(!Result.m_Content.empty())
		{
			NeteaseLyrics::STimeline Timeline;
			if(QmMusicLyrics::ParseQrcRlrc(Result.m_Content, &Timeline))
				Result.m_Type = "qrc";
			else if(NeteaseLyrics::ParseLrc(Result.m_Content, &Timeline))
				Result.m_Type = "lrc";
			else
				return false;
		}
		DecodeLyric(String(Field(pData, "trans")), Result.m_Translation);
		Out = std::move(Result);
		return true;
	}

	bool CPlaybackClock::Update(std::string_view Identity, int64_t PositionMs, int64_t NowMs)
	{
		if(m_Identity != Identity)
		{
			m_Identity = Identity;
			m_Playing = false;
			m_LastAdvanceAt = NowMs;
		}
		else if(PositionMs != m_LastPosition)
		{
			const int64_t Advance = PositionMs - m_LastPosition;
			// 暂停拖动进度只更新位置；后续自然推进后才判恢复。
			m_Playing = Advance > 0 && Advance <= NowMs - m_LastObservedAt + 1500;
			m_LastAdvanceAt = NowMs;
		}
		else if(NowMs - m_LastAdvanceAt >= 1200)
			m_Playing = false;
		m_LastPosition = PositionMs;
		m_LastObservedAt = NowMs;
		return m_Playing;
	}

	std::string CSongIdentity::Update(std::string_view Title, std::string_view Artist, uint32_t DurationMs, std::string_view Key, int64_t NowMs)
	{
		const bool DifferentSong = m_Title != Title || m_DurationMs != DurationMs || (!m_Artist.empty() && !Artist.empty() && m_Artist != Artist);
		if(DifferentSong)
		{
			// 切歌可能经历多帧临时标题/时长；只有确认新 ID 才能替换最后确认的旧 ID。
			m_RejectedKey = m_LastConfirmedKey;
			m_Title = Title;
			m_Artist = Artist;
			m_DurationMs = DurationMs;
			Suspend();
		}
		else if(!Artist.empty())
			m_Artist = Artist;
		if(Key.empty() || Key == m_RejectedKey)
		{
			Suspend();
			return {};
		}
		if(m_Candidate != Key)
		{
			m_Candidate = Key;
			m_CandidateSince = NowMs;
			m_Accepted.clear();
		}
		if(NowMs - m_CandidateSince >= 250)
		{
			m_Accepted = Key;
			m_LastConfirmedKey = Key;
		}
		return m_Accepted;
	}

	void CSongIdentity::Suspend()
	{
		m_Accepted.clear();
		m_Candidate.clear();
	}
}
