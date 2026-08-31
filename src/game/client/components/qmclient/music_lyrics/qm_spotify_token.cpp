#include "qm_spotify_token.h"

#include "qm_spotify_crypto.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace QmSpotifyToken
{
	namespace
	{
		constexpr const char *TOKEN_BASE_URL = "https://open.spotify.com/api/token";

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

		// 还原单个字节值:value[i] ^ ((i % 33) + 9),再转为十进制字符串。
		std::string TransformValue(int Value, size_t Index)
		{
			const int Transformed = Value ^ ((int)(Index % 33) + 9);
			char aBuffer[16];
			int Written = 0;
			if(Transformed == 0)
				aBuffer[Written++] = '0';
			else
			{
				int Tmp = Transformed;
				char aReversed[16];
				int Reversed = 0;
				while(Tmp > 0)
				{
					aReversed[Reversed++] = (char)('0' + Tmp % 10);
					Tmp /= 10;
				}
				while(Reversed > 0)
					aBuffer[Written++] = aReversed[--Reversed];
			}
			return std::string(aBuffer, Written);
		}

		bool ParsePayload(const json_value *pRoot, STotpSecret *pOut)
		{
			if(pOut == nullptr || pRoot == nullptr || pRoot->type != json_object)
				return false;
			// 取数字 key 最大的一组(Spotify 会轮换 secret 版本)。
			int64_t BestVersion = -1;
			const json_value *pBestArray = nullptr;
			for(unsigned int Index = 0; Index < pRoot->u.object.length; ++Index)
			{
				const auto &Entry = pRoot->u.object.values[Index];
				if(Entry.name == nullptr || Entry.value == nullptr || Entry.value->type != json_array)
					continue;
				int64_t Version = 0;
				bool Valid = true;
				for(const char *p = Entry.name; *p != '\0'; ++p)
				{
					if(*p < '0' || *p > '9')
					{
						Valid = false;
						break;
					}
					Version = Version * 10 + (*p - '0');
				}
				if(Valid && Version > BestVersion)
				{
					BestVersion = Version;
					pBestArray = Entry.value;
				}
			}
			if(pBestArray == nullptr)
				return false;

			std::string Secret;
			for(unsigned int Index = 0; Index < pBestArray->u.array.length; ++Index)
			{
				const json_value *pValue = pBestArray->u.array.values[Index];
				if(pValue == nullptr || pValue->type != json_integer)
					return false;
				Secret += TransformValue((int)pValue->u.integer, Index);
			}
			if(Secret.empty())
				return false;
			pOut->m_Secret = std::move(Secret);
			pOut->m_Version = std::to_string(BestVersion);
			return true;
		}
	} // namespace

	std::string BundledSecretPayload()
	{
		// 与 Lyricify-Lyrics-Helper 内置兜底一致的三组 secret(版本 59/60/61)。
		return "{\"59\":[123,105,79,70,110,59,52,125,60,49,80,70,89,75,80,86,63,53,123,37,117,49,52,93,77,62,47,86,48,104,68,72],"
		       "\"60\":[79,109,69,123,90,65,46,74,94,34,58,48,70,71,92,85,122,63,91,64,87,87],"
		       "\"61\":[44,55,47,42,70,40,34,114,76,74,50,111,120,97,75,76,94,102,43,69,49,120,118,80,64,78]}";
	}

	bool ParseSecretPayload(std::string_view Json, STotpSecret *pOut)
	{
		if(Json.empty())
			return false;
		const std::string Copy(Json);
		json_value *pRoot = json_parse(Copy.c_str(), Copy.size());
		if(pRoot == nullptr)
			return false;
		const bool Result = ParsePayload(pRoot, pOut);
		json_value_free(pRoot);
		return Result;
	}

	std::string GenerateTotp(const std::string &Secret, int64_t ServerTimeSeconds)
	{
		const int64_t Counter = ServerTimeSeconds / 30;
		unsigned char aCounter[8];
		for(unsigned int i = 0; i < 8; ++i)
			aCounter[i] = (unsigned char)((uint64_t)Counter >> (56 - i * 8));

		const std::string Digest = QmSpotifyCrypto::HmacSha1(
			(const unsigned char *)Secret.data(), Secret.size(), aCounter, sizeof(aCounter));
		const unsigned char Offset = (unsigned char)((unsigned char)Digest[19] & 0x0F);
		const uint32_t Binary =
			(((uint32_t)(unsigned char)Digest[Offset] & 0x7F) << 24) |
			((uint32_t)(unsigned char)Digest[Offset + 1] << 16) |
			((uint32_t)(unsigned char)Digest[Offset + 2] << 8) |
			((uint32_t)(unsigned char)Digest[Offset + 3]);
		const uint32_t Code = Binary % 1000000u;
		char aBuffer[8];
		snprintf(aBuffer, sizeof(aBuffer), "%06u", Code);
		return std::string(aBuffer);
	}

	std::string NormalizeSpDc(std::string_view Value)
	{
		// 用户可能直接粘贴完整 Cookie 头("sp_dc=xxx; ...")或带引号。
		std::string Result;
		Result.reserve(Value.size());
		size_t Start = 0;
		while(Start < Value.size() && (Value[Start] == ' ' || Value[Start] == '\t' || Value[Start] == '"'))
			++Start;
		size_t End = Value.size();
		while(End > Start && (Value[End - 1] == ' ' || Value[End - 1] == '\t' || Value[End - 1] == '"' || Value[End - 1] == ';'))
			--End;
		const std::string_view Trimmed = Value.substr(Start, End - Start);
		constexpr std::string_view PREFIX = "sp_dc=";
		std::string_view Core = Trimmed;
		if(Core.size() > PREFIX.size() && Core.substr(0, PREFIX.size()) == PREFIX)
			Core = Core.substr(PREFIX.size());
		// 只保留第一个分号之前的内容(容忍 "sp_dc=xxx; sp_landing=..." 这类粘贴)。
		const size_t Semicolon = Core.find(';');
		if(Semicolon != std::string_view::npos)
			Core = Core.substr(0, Semicolon);
		while(!Core.empty() && (Core.back() == ' ' || Core.back() == '\t' || Core.back() == '"'))
			Core = Core.substr(0, Core.size() - 1);
		Result.assign(Core);
		return Result;
	}

	std::string BuildTokenUrl(const STotpSecret &Secret, int64_t ServerTimeSeconds, bool Legacy)
	{
		const std::string Totp = GenerateTotp(Secret.m_Secret, ServerTimeSeconds);
		std::string Query = "reason=";
		Query += Legacy ? "transport" : "init";
		Query += "&productType=web-player";
		Query += "&totp=";
		Query += Totp;
		Query += "&totpVer=";
		Query += Secret.m_Version;
		Query += "&totpServer=";
		Query += Totp;
		if(Legacy)
		{
			Query += "&ts=";
			Query += std::to_string(ServerTimeSeconds);
		}
		return std::string(TOKEN_BASE_URL) + "?" + Query;
	}
} // namespace QmSpotifyToken
