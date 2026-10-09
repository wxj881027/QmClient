#include "music_lyrics_krc.h"

#include <engine/external/json-parser/json.h>
#include <engine/external/zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <limits>
#include <vector>

namespace QmMusicLyrics
{
	namespace
	{
		constexpr char KRC_MAGIC[4] = {'k', 'r', 'c', '1'};

		bool HasMagic(std::string_view Data)
		{
			return Data.size() >= 4 && std::memcmp(Data.data(), KRC_MAGIC, 4) == 0;
		}

		std::string_view Trim(std::string_view Value)
		{
			while(!Value.empty() && std::isspace((unsigned char)Value.front()) != 0)
				Value.remove_prefix(1);
			while(!Value.empty() && std::isspace((unsigned char)Value.back()) != 0)
				Value.remove_suffix(1);
			return Value;
		}

		bool ParseUnsigned(std::string_view Value, int64_t *pOut)
		{
			if(pOut == nullptr || Value.empty())
				return false;
			int64_t Number = 0;
			const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
			if(Result.ec != std::errc{} || Result.ptr != Value.data() + Value.size() || Number < 0)
				return false;
			*pOut = Number;
			return true;
		}

		// 解析 LRC 风格 [mm:ss.xx] / [mm:ss.xxx] 时间戳。
		bool ParseLrcStyleTime(std::string_view Value, int64_t *pOut)
		{
			if(pOut == nullptr)
				return false;
			Value = Trim(Value);
			const size_t Colon = Value.find(':');
			if(Colon == std::string_view::npos || Colon == 0 || Colon + 1 >= Value.size())
				return false;
			int64_t Minutes = 0;
			if(!ParseUnsigned(Value.substr(0, Colon), &Minutes))
				return false;
			std::string_view SecondsPart = Value.substr(Colon + 1);
			const size_t Dot = SecondsPart.find_first_of(".,");
			std::string_view WholeSeconds = Dot == std::string_view::npos ? SecondsPart : SecondsPart.substr(0, Dot);
			if(WholeSeconds.empty())
				return false;
			int64_t Seconds = 0;
			if(!ParseUnsigned(WholeSeconds, &Seconds) || Seconds >= 60)
				return false;
			int64_t FractionMs = 0;
			if(Dot != std::string_view::npos)
			{
				const std::string_view Fraction = SecondsPart.substr(Dot + 1);
				if(Fraction.empty() || Fraction.size() > 3)
					return false;
				int64_t FractionValue = 0;
				if(!ParseUnsigned(Fraction, &FractionValue))
					return false;
				if(Fraction.size() == 1)
					FractionMs = FractionValue * 100;
				else if(Fraction.size() == 2)
					FractionMs = FractionValue * 10;
				else
					FractionMs = FractionValue;
			}
			if(Minutes > (std::numeric_limits<int64_t>::max() - Seconds * 1000 - FractionMs) / 60000)
				return false;
			*pOut = Minutes * 60000 + Seconds * 1000 + FractionMs;
			return true;
		}

		// 压缩体积不能约束展开后的内存，分块解压并限制歌词正文。
		bool Inflate(const std::vector<uint8_t> &Input, std::string *pOut)
		{
			if(pOut == nullptr || Input.empty() || Input.size() > UINT_MAX)
				return false;
			z_stream Stream{};
			Stream.next_in = const_cast<Bytef *>(Input.data());
			Stream.avail_in = (uInt)Input.size();
			if(inflateInit(&Stream) != Z_OK)
				return false;
			struct SInflateCleanup
			{
				z_stream *m_pStream;
				~SInflateCleanup() { inflateEnd(m_pStream); }
			} Cleanup{&Stream};
			std::string Output;
			std::array<uint8_t, 16384> aChunk;
			for(;;)
			{
				Stream.next_out = aChunk.data();
				Stream.avail_out = aChunk.size();
				const int Ret = inflate(&Stream, Z_NO_FLUSH);
				const size_t Produced = aChunk.size() - Stream.avail_out;
				if(Produced > KRC_MAX_TEXT_BYTES - Output.size())
					return false;
				Output.append((const char *)aChunk.data(), Produced);
				if(Ret == Z_STREAM_END)
					break;
				if(Ret != Z_OK || (Produced == 0 && Stream.avail_in == 0))
					return false;
			}
			// 跳过 UTF-8 BOM。
			if(Output.size() >= 3 && (uint8_t)Output[0] == 0xEF && (uint8_t)Output[1] == 0xBB && (uint8_t)Output[2] == 0xBF)
				Output.erase(0, 3);
			*pOut = std::move(Output);
			return true;
		}

		bool TryDecryptWithKey(std::string_view Payload, const uint8_t *pKey, size_t KeySize, std::string *pOutText)
		{
			if(pKey == nullptr || KeySize == 0 || Payload.empty() || pOutText == nullptr)
				return false;
			std::vector<uint8_t> Xored(Payload.size());
			for(size_t Index = 0; Index < Payload.size(); ++Index)
				Xored[Index] = (uint8_t)Payload[Index] ^ pKey[Index % KeySize];
			return Inflate(Xored, pOutText);
		}

		// 标准解密:载荷从偏移 9 起,标志位决定密钥策略。
		bool TryDecryptStandard(std::string_view Data, std::string *pOutText)
		{
			if(Data.size() < 9)
				return false;
			const uint8_t Flag = (uint8_t)Data[8];
			const std::string_view Payload = Data.substr(9);
			if(Flag == 1)
			{
				// 新版前 1024 字节使用 1024 字节密钥表。该表在公开资料中
				// 版本不一,当前以 64 字节表兜底尝试;线上绝大多数为旧版。
				if(TryDecryptWithKey(Payload, KRC_KEY64, sizeof(KRC_KEY64), pOutText))
					return true;
			}
			return TryDecryptWithKey(Payload, KRC_KEY64, sizeof(KRC_KEY64), pOutText);
		}

		// 旧版解密:跳过 4 字节魔数,载荷用 16 字节密钥表。
		bool TryDecryptLegacy(std::string_view Data, std::string *pOutText)
		{
			if(Data.size() < 5)
				return false;
			return TryDecryptWithKey(Data.substr(4), KRC_KEY16, sizeof(KRC_KEY16), pOutText);
		}

		void SetDerivedEnds(std::vector<NeteaseLyrics::SLine> &vLines)
		{
			std::stable_sort(vLines.begin(), vLines.end(), [](const NeteaseLyrics::SLine &A, const NeteaseLyrics::SLine &B) { return A.m_StartMs < B.m_StartMs; });
			for(size_t Index = 0; Index < vLines.size(); ++Index)
			{
				if(vLines[Index].m_EndMs > vLines[Index].m_StartMs)
					continue;
				for(size_t Next = Index + 1; Next < vLines.size(); ++Next)
				{
					if(vLines[Next].m_StartMs > vLines[Index].m_StartMs)
					{
						vLines[Index].m_EndMs = vLines[Next].m_StartMs;
						break;
					}
				}
			}
		}

		unsigned char Base64Value(char C)
		{
			if(C >= 'A' && C <= 'Z')
				return (unsigned char)(C - 'A');
			if(C >= 'a' && C <= 'z')
				return (unsigned char)(C - 'a' + 26);
			if(C >= '0' && C <= '9')
				return (unsigned char)(C - '0' + 52);
			if(C == '+')
				return 62;
			if(C == '/')
				return 63;
			return 0xFF;
		}

		bool Base64Decode(std::string_view In, std::vector<uint8_t> *pOut)
		{
			if(pOut == nullptr)
				return false;
			pOut->clear();
			pOut->reserve(In.size() * 3 / 4 + 4);
			uint32_t Acc = 0;
			int Bits = 0;
			for(const char C : In)
			{
				if(C == '=' || C == '\n' || C == '\r' || C == ' ' || C == '\t')
					continue;
				const unsigned char Value = (unsigned char)C < 128 ? Base64Value(C) : 0xFF;
				if(Value == 0xFF)
					return false;
				Acc = (Acc << 6) | Value;
				Bits += 6;
				if(Bits >= 8)
				{
					Bits -= 8;
					pOut->push_back((uint8_t)((Acc >> Bits) & 0xFF));
				}
			}
			return true;
		}
	}

	bool DecryptKrc(std::string_view Data, std::string *pOutText, std::string *pError)
	{
		if(pOutText == nullptr)
			return false;
		pOutText->clear();
		if(!HasMagic(Data))
		{
			if(pError)
				*pError = "bad krc magic";
			return false;
		}
		std::string Candidate;
		if(TryDecryptStandard(Data, &Candidate) || TryDecryptLegacy(Data, &Candidate))
		{
			*pOutText = std::move(Candidate);
			return true;
		}
		if(pError)
			*pError = "krc decrypt failed";
		return false;
	}

	bool ParseKrcText(std::string_view Text, NeteaseLyrics::STimeline *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		pOut->Clear();
		if(!NeteaseLyrics::IsValidUtf8(Text))
		{
			if(pError)
				*pError = "invalid UTF-8";
			return false;
		}
		std::vector<NeteaseLyrics::SLine> vLines;
		size_t Offset = 0;
		while(Offset <= Text.size())
		{
			const size_t Newline = Text.find('\n', Offset);
			std::string_view Line = Text.substr(Offset, Newline == std::string_view::npos ? Text.size() - Offset : Newline - Offset);
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			Line = Trim(Line);
			if(Line.size() >= 3 && (uint8_t)Line[0] == 0xEF && (uint8_t)Line[1] == 0xBB && (uint8_t)Line[2] == 0xBF)
				Line.remove_prefix(3);
			if(Line.empty() || Line.front() != '[')
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			const size_t BracketEnd = Line.find(']');
			if(BracketEnd == std::string_view::npos)
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			const std::string_view Inside = Line.substr(1, BracketEnd - 1);
			NeteaseLyrics::SLine Parsed;
			const size_t Comma = Inside.find(',');
			if(Comma != std::string_view::npos)
			{
				// 毫秒对 [startMs,durationMs](旧版)。
				int64_t StartMs = 0;
				int64_t DurationMs = 0;
				if(!ParseUnsigned(Trim(Inside.substr(0, Comma)), &StartMs) ||
					!ParseUnsigned(Trim(Inside.substr(Comma + 1)), &DurationMs) ||
					StartMs > std::numeric_limits<int64_t>::max() - DurationMs)
				{
					if(Newline == std::string_view::npos)
						break;
					Offset = Newline + 1;
					continue;
				}
				Parsed.m_StartMs = StartMs;
				Parsed.m_EndMs = DurationMs > 0 ? StartMs + DurationMs : -1;
			}
			else if(!ParseLrcStyleTime(Inside, &Parsed.m_StartMs))
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			std::string_view Body = Line.substr(BracketEnd + 1);
			std::string PlainText;
			bool Malformed = false;
			size_t BodyOffset = 0;
			while(BodyOffset < Body.size())
			{
				if(Body[BodyOffset] != '<')
				{
					const size_t Next = Body.find('<', BodyOffset);
					const size_t End = Next == std::string_view::npos ? Body.size() : Next;
					PlainText.append(Body.substr(BodyOffset, End - BodyOffset));
					BodyOffset = End;
					continue;
				}
				const size_t TagEnd = Body.find('>', BodyOffset + 1);
				if(TagEnd == std::string_view::npos)
				{
					Malformed = true;
					break;
				}
				const std::string_view Tag = Body.substr(BodyOffset + 1, TagEnd - BodyOffset - 1);
				const size_t C1 = Tag.find(',');
				if(C1 == std::string_view::npos)
				{
					Malformed = true;
					break;
				}
				const size_t C2 = Tag.find(',', C1 + 1);
				int64_t WordOffsetMs = 0;
				int64_t WordDurationMs = 0;
				const std::string_view Second = C2 == std::string_view::npos ? Tag.substr(C1 + 1) : Tag.substr(C1 + 1, C2 - C1 - 1);
				if(!ParseUnsigned(Trim(Tag.substr(0, C1)), &WordOffsetMs) || !ParseUnsigned(Trim(Second), &WordDurationMs))
				{
					Malformed = true;
					break;
				}
				const size_t WordStart = TagEnd + 1;
				const size_t NextMarker = Body.find('<', WordStart);
				const size_t WordEnd = NextMarker == std::string_view::npos ? Body.size() : NextMarker;
				const std::string WordText(Body.substr(WordStart, WordEnd - WordStart));
				PlainText += WordText;
				if(WordDurationMs > 0 && WordOffsetMs >= 0)
				{
					if(Parsed.m_StartMs <= std::numeric_limits<int64_t>::max() - WordOffsetMs &&
						Parsed.m_StartMs + WordOffsetMs <= std::numeric_limits<int64_t>::max() - WordDurationMs)
					{
						const int64_t WordStartMs = Parsed.m_StartMs + WordOffsetMs;
						Parsed.m_vWords.push_back({WordStartMs, WordStartMs + WordDurationMs, WordText});
					}
				}
				BodyOffset = WordEnd;
			}
			if(!Malformed)
			{
				Parsed.m_Text = std::move(PlainText);
				if(!Parsed.m_Text.empty())
					vLines.push_back(std::move(Parsed));
			}
			if(Newline == std::string_view::npos)
				break;
			Offset = Newline + 1;
		}
		if(vLines.empty())
		{
			if(pError)
				*pError = "no valid timed lyric lines";
			return false;
		}
		SetDerivedEnds(vLines);
		pOut->m_vLines = std::move(vLines);
		pOut->m_HasTiming = true;
		return true;
	}

	std::vector<std::string> ExtractKrcTranslation(std::string_view Text)
	{
		std::vector<std::string> Result;
		constexpr std::string_view Tag = "[language:";
		const size_t Start = Text.find(Tag);
		if(Start == std::string_view::npos)
			return Result;
		const size_t ContentStart = Start + Tag.size();
		const size_t End = Text.find(']', ContentStart);
		if(End == std::string_view::npos || End == ContentStart)
			return Result;
		std::vector<uint8_t> JsonBytes;
		if(!Base64Decode(Text.substr(ContentStart, End - ContentStart), &JsonBytes) || JsonBytes.empty())
			return Result;
		json_value *pRoot = json_parse((const char *)JsonBytes.data(), JsonBytes.size());
		if(pRoot == nullptr || pRoot->type != json_object)
		{
			if(pRoot != nullptr)
				json_value_free(pRoot);
			return Result;
		}
		const auto FindField = [](const json_value *pObject, const char *pName) -> const json_value * {
			if(pObject == nullptr || pObject->type != json_object)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		};
		const json_value *pContent = FindField(pRoot, "content");
		if(pContent != nullptr && pContent->type == json_array)
		{
			for(unsigned int BlockIndex = 0; BlockIndex < pContent->u.array.length; ++BlockIndex)
			{
				const json_value *pBlock = pContent->u.array.values[BlockIndex];
				if(pBlock == nullptr || pBlock->type != json_object)
					continue;
				const json_value *pType = FindField(pBlock, "type");
				if(pType == nullptr || pType->type != json_integer || pType->u.integer != 1)
					continue; // 只取中文翻译;type=0 是罗马音
				const json_value *pLyricContent = FindField(pBlock, "lyricContent");
				if(pLyricContent == nullptr || pLyricContent->type != json_array)
					continue;
				Result.reserve(pLyricContent->u.array.length);
				for(unsigned int LineIndex = 0; LineIndex < pLyricContent->u.array.length; ++LineIndex)
				{
					const json_value *pFragments = pLyricContent->u.array.values[LineIndex];
					if(pFragments == nullptr || pFragments->type != json_array)
					{
						Result.emplace_back();
						continue;
					}
					std::string LineText;
					for(unsigned int FragmentIndex = 0; FragmentIndex < pFragments->u.array.length; ++FragmentIndex)
					{
						const json_value *pFragment = pFragments->u.array.values[FragmentIndex];
						if(pFragment != nullptr && pFragment->type == json_string && pFragment->u.string.ptr != nullptr)
							LineText.append(pFragment->u.string.ptr, pFragment->u.string.length);
					}
					while(!LineText.empty() && std::isspace((unsigned char)LineText.front()) != 0)
						LineText.erase(LineText.begin());
					while(!LineText.empty() && std::isspace((unsigned char)LineText.back()) != 0)
						LineText.pop_back();
					Result.push_back(std::move(LineText));
				}
				break;
			}
		}
		json_value_free(pRoot);
		return Result;
	}

	bool ParseKrcData(std::string_view Data, SLyricsData *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		SLyricsData Result;
		std::string Text;
		if(!DecryptKrc(Data, &Text, pError))
			return false;
		if(!ParseKrcText(Text, &Result.m_Timeline, pError))
			return false;
		Result.m_vTranslations = ExtractKrcTranslation(Text);
		// 翻译轨按行序号对齐;行数不一致时保留可对齐部分,不影响主时间轴。
		if(Result.m_vTranslations.size() > Result.m_Timeline.m_vLines.size())
			Result.m_vTranslations.resize(Result.m_Timeline.m_vLines.size());
		*pOut = std::move(Result);
		return true;
	}
} // namespace QmMusicLyrics
