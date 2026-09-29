// 酷狗接口与补丁指纹参考 Metabox-Nexus-PlayerCap（MIT），授权见 license.txt。
#include "qm_kugou_protocol.h"

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_krc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <memory>

namespace QmMusicHook
{
	namespace
	{
		using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

		const json_value *Field(const json_value *pObject, std::string_view Name)
		{
			if(pObject == nullptr || pObject->type != json_object)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name && std::string_view(Entry.name, Entry.name_length) == Name)
					return Entry.value;
			}
			return nullptr;
		}

		std::string Text(const json_value *pValue)
		{
			if(pValue && pValue->type == json_string)
				return {pValue->u.string.ptr, pValue->u.string.length};
			if(pValue && pValue->type == json_integer)
				return std::to_string(pValue->u.integer);
			return {};
		}

		bool Integer(const json_value *pValue, int64_t *pNumber)
		{
			const auto String = Text(pValue);
			if(String.empty())
				return false;
			int64_t Parsed = 0;
			const auto Result = std::from_chars(String.data(), String.data() + String.size(), Parsed);
			if(Result.ec != std::errc() || Result.ptr != String.data() + String.size() || Parsed < 0)
				return false;
			*pNumber = Parsed;
			return true;
		}

		std::string Trim(std::string Value)
		{
			const auto First = Value.find_first_not_of(" \t\r\n");
			return First == std::string::npos ? std::string() : Value.substr(First, Value.find_last_not_of(" \t\r\n") - First + 1);
		}

		bool Base64Decode(std::string_view Encoded, std::string *pDecoded)
		{
			pDecoded->clear();
			if(Encoded.size() > 4 * 1024 * 1024 || Encoded.size() % 4 != 0)
				return false;
			constexpr std::string_view Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			for(size_t Index = 0; Index < Encoded.size(); Index += 4)
			{
				uint32_t Value = 0;
				int Padding = 0;
				for(size_t Byte = 0; Byte < 4; ++Byte)
				{
					Value <<= 6;
					const char Character = Encoded[Index + Byte];
					if(Character == '=')
					{
						if(Byte < 2 || Index + 4 != Encoded.size())
							return false;
						++Padding;
						continue;
					}
					const size_t Digit = Alphabet.find(Character);
					if(Digit == std::string_view::npos || Padding > 0)
						return false;
					Value |= (uint32_t)Digit;
				}
				pDecoded->push_back((char)(Value >> 16));
				if(Padding < 2)
					pDecoded->push_back((char)(Value >> 8));
				if(Padding < 1)
					pDecoded->push_back((char)Value);
			}
			return true;
		}
	}

	bool ParseKugouPlayback(std::string_view Json, SPlayback *pState)
	{
		if(!pState)
			return false;
		*pState = {};
		if(Json.size() > 1024 * 1024)
			return false;
		const TJson Root(json_parse(Json.data(), Json.size()), json_value_free);
		if(!Root || Root->type != json_object)
			return false;
		auto Hash = Text(Field(Root.get(), "hash"));
		const auto Status = Text(Field(Root.get(), "play_status"));
		if(Hash.empty() || Status == "stopped" || Status.empty())
			return true;
		if(Hash.size() != 32 || !std::all_of(Hash.begin(), Hash.end(), [](unsigned char C) { return std::isxdigit(C) != 0; }))
			return false;
		if(Status != "playing" && Status != "paused")
			return true;
		std::transform(Hash.begin(), Hash.end(), Hash.begin(), [](unsigned char C) { return (char)std::tolower(C); });
		pState->m_HasSong = true;
		pState->m_Playing = Status == "playing";
		pState->m_MediaId = std::move(Hash);
		const auto Filename = Text(Field(Root.get(), "filename"));
		const size_t Separator = Filename.find(" - ");
		pState->m_Title = Trim(Separator == std::string::npos ? Filename : Filename.substr(Separator + 3));
		if(Separator != std::string::npos)
			pState->m_Artist = Trim(Filename.substr(0, Separator));
		pState->m_CoverUrl = Text(Field(Root.get(), "cover"));
		Integer(Field(Root.get(), "duration"), &pState->m_DurationMs);
		int64_t RawPosition = 0;
		if(Integer(Field(Root.get(), "progress"), &RawPosition))
		{
			// progress 使用 100 纳秒，duration 使用毫秒。
			const int64_t PositionMs = RawPosition / 10000;
			pState->m_PositionValid = pState->m_DurationMs > 0 && PositionMs <= pState->m_DurationMs;
			if(pState->m_PositionValid)
				pState->m_PositionMs = PositionMs;
		}
		return true;
	}

	bool SelectKugouLyricCandidate(std::string_view Json, std::string *pId, std::string *pAccessKey)
	{
		if(!pId || !pAccessKey)
			return false;
		pId->clear();
		pAccessKey->clear();
		const TJson Root(json_parse(Json.data(), Json.size()), json_value_free);
		if(!Root || Root->type != json_object)
			return false;
		int64_t Status = 0;
		if(!Integer(Field(Root.get(), "status"), &Status) || Status != 200)
			return false;
		const auto *pCandidates = Field(Root.get(), "candidates");
		if(!pCandidates || pCandidates->type != json_array)
			return false;
		const auto Proposal = Text(Field(Root.get(), "proposal"));
		for(unsigned int Index = 0; Index < pCandidates->u.array.length; ++Index)
		{
			const auto *pCandidate = pCandidates->u.array.values[Index];
			const auto Id = Text(Field(pCandidate, "id"));
			const auto Key = Text(Field(pCandidate, "accesskey"));
			if(Id.empty() || Key.empty())
				continue;
			if(pId->empty() || Id == Proposal)
			{
				*pId = Id;
				*pAccessKey = Key;
			}
			if(Id == Proposal)
				break;
		}
		return true;
	}

	bool DecodeKugouLyricResponse(std::string_view Json, bool Krc, std::string *pText)
	{
		if(!pText)
			return false;
		pText->clear();
		const TJson Root(json_parse(Json.data(), Json.size()), json_value_free);
		int64_t Status = 0;
		if(!Root || !Integer(Field(Root.get(), "status"), &Status) || Status != 200)
			return false;
		std::string Decoded;
		if(!Base64Decode(Text(Field(Root.get(), "content")), &Decoded) || Decoded.empty())
			return false;
		NeteaseLyrics::STimeline Timeline;
		if(Krc)
		{
			std::string Plain;
			if(!QmMusicLyrics::DecryptKrc(Decoded, &Plain) || !QmMusicLyrics::ParseKrcText(Plain, &Timeline))
				return false;
			*pText = std::move(Plain);
			return true;
		}
		if(!NeteaseLyrics::ParseLrc(Decoded, &Timeline))
			return false;
		*pText = std::move(Decoded);
		return true;
	}

	const std::vector<SKugouPatch> &KugouPatches()
	{
		static const std::vector<SKugouPatch> Patches = {
			{0x58C63EB, std::string("\xC7\x06\xAA\xAA\xAA\xAA", 6), std::string("\xC7\x06\xC9\x2F\x00\x00", 6)},
			{0x58C6415, std::string("\xE8\x96\xA9\xE9\xFC", 5), std::string(5, '\x90')},
			{0x58C6420, std::string("\x0F\x87\x96\x01\x00\x00", 6), std::string(6, '\x90')},
			{0x58C6428, std::string("\x0F\x84\x8E\x01\x00\x00", 6), std::string(6, '\x90')},
			{0x4BC180E, std::string("\x8B\x95\x7C\x01\x00\x00", 6), std::string("\xBA\xC9\x2F\x00\x00\x90", 6)},
			{0x4BEDE41, std::string("\x0F\x84\x70\x01\x00\x00", 6), std::string(6, '\x90')},
			{0x4BEDE4C, std::string("\x41\xC7\x07\xAA\xAA\xAA\xAA", 7), std::string("\x41\xC7\x07\xC9\x2F\x00\x00", 7)},
			{0x4BEDEB5, std::string("\xE8\xF6\x2E\xB7\xFD", 5), std::string(5, '\x90')},
			{0x4BEDED0, std::string("\x0F\x44\xDA", 3), std::string(3, '\x90')},
		};
		return Patches;
	}

	EKugouPatchState ClassifyKugouPatch(const TKugouReadBytes &Read)
	{
		bool AllOriginal = true;
		bool AllPatched = true;
		std::array<char, 7> Buffer{};
		for(const auto &Patch : KugouPatches())
		{
			if(!Read(Patch.m_Offset, Buffer.data(), Patch.m_Original.size()))
				return EKugouPatchState::UNSUPPORTED;
			const std::string_view Bytes(Buffer.data(), Patch.m_Original.size());
			AllOriginal &= Bytes == Patch.m_Original;
			AllPatched &= Bytes == Patch.m_Patched;
		}
		return AllOriginal ? EKugouPatchState::ORIGINAL : AllPatched ? EKugouPatchState::PATCHED :
									       EKugouPatchState::UNSUPPORTED;
	}

	bool IsKugouRestoreChunk(uint64_t Offset, std::string_view Original, std::string_view Current, bool AllowPartial)
	{
		if(Original.size() != Current.size())
			return false;
		std::string Expected(Original);
		for(const auto &Patch : KugouPatches())
		{
			for(size_t Byte = 0; Byte < Patch.m_Original.size(); ++Byte)
			{
				const uint64_t Absolute = Patch.m_Offset + Byte;
				if(Absolute >= Offset && Absolute - Offset < Original.size())
				{
					const size_t Index = (size_t)(Absolute - Offset);
					if(Original[Index] != Patch.m_Original[Byte])
						return false;
					Expected[Index] = AllowPartial && Current[Index] == Patch.m_Original[Byte] ? Patch.m_Original[Byte] : Patch.m_Patched[Byte];
				}
			}
		}
		return Expected == Current;
	}
}
