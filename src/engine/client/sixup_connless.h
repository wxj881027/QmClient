#ifndef ENGINE_CLIENT_SIXUP_CONNLESS_H
#define ENGINE_CLIENT_SIXUP_CONNLESS_H

#include <engine/serverbrowser.h>
#include <engine/shared/masterserver.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>

#include <algorithm>
#include <array>

namespace sixup_translation
{
	// Data指向Capacity字节可写空间；失败时保留原数据和长度。
	inline bool PreprocessConnless(void *pData, int &Size, int Capacity)
	{
		if(Size < 0 || Size > Capacity || Size > NET_MAX_CONNLESS_PAYLOAD || (Size > 0 && !pData))
			return false;
		if(Size < SERVERBROWSE_SIZE || mem_comp(pData, SERVERBROWSE_INFO, SERVERBROWSE_SIZE) != 0)
			return true;

		// GetString会原地清理控制字符，先借助固定副本完成全部解析。
		std::array<unsigned char, NET_MAX_CONNLESS_PAYLOAD> aInput;
		mem_copy(aInput.data(), pData, Size);
		CUnpacker Up;
		Up.Reset(aInput.data() + SERVERBROWSE_SIZE, Size - SERVERBROWSE_SIZE);
		CServerInfo Info{};
		auto GetString = [&Up](auto &Buf) {
			str_copy(Buf, Up.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES));
		};
		int Token = Up.GetInt();
		GetString(Info.m_aVersion);
		GetString(Info.m_aName);
		str_clean_whitespaces(Info.m_aName);
		Up.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES); // 主机名。
		GetString(Info.m_aMap);
		GetString(Info.m_aGameType);
		Info.m_Flags = Up.GetInt();
		Up.GetInt(); // 服务端难度。
		Info.m_NumPlayers = Up.GetInt();
		Info.m_MaxPlayers = Up.GetInt();
		Info.m_NumClients = Up.GetInt();
		Info.m_MaxClients = Up.GetInt();
		if(Up.Error() || Info.m_NumPlayers < 0 || Info.m_MaxPlayers < 0 || Info.m_NumClients < 0 || Info.m_MaxClients < 0 ||
			Info.m_NumPlayers > Info.m_NumClients || Info.m_MaxPlayers > Info.m_MaxClients)
			return false;

		const bool Extended = Info.m_MaxPlayers > VANILLA_MAX_CLIENTS || Info.m_MaxClients > VANILLA_MAX_CLIENTS;
		CPacker Out;
		Out.Reset();
		auto PutInt = [&Out](int Value) {
			char aBuf[16];
			str_format(aBuf, sizeof(aBuf), "%d", Value);
			Out.AddString(aBuf);
		};
		if(!Extended)
			Token &= 0xff;
		PutInt(Token);
		Out.AddString(Info.m_aVersion);
		Out.AddString(Info.m_aName);
		Out.AddString(Info.m_aMap);
		if(Extended)
		{
			PutInt(0); // 地图CRC和大小沿用旧转换结果。
			PutInt(0);
		}
		Out.AddString(Info.m_aGameType);
		PutInt(Info.m_Flags);
		PutInt(Info.m_NumPlayers);
		PutInt(Info.m_MaxPlayers);
		PutInt(Info.m_NumClients);
		PutInt(Info.m_MaxClients);
		if(Extended)
			Out.AddString(""); // 保留字段。

		// 保留原有128人转换上限，逐人编码，不分配临时玩家列表。
		const int NumClients = std::min(Info.m_NumClients, (int)SERVERINFO_MAX_CLIENTS);
		for(int i = 0; i < NumClients; ++i)
		{
			char aName[MAX_NAME_LENGTH];
			char aClan[MAX_CLAN_LENGTH];
			GetString(aName);
			GetString(aClan);
			int Country = Up.GetInt();
			if(!in_range(Country, CountryCode::MINIMUM, CountryCode::MAXIMUM))
				Country = CountryCode::DEFAULT;
			const int Score = Up.GetInt();
			const bool Player = !(Up.GetInt() & 1);
			if(Up.Error())
				return false;
			Out.AddString(aName);
			Out.AddString(aClan);
			PutInt(Country);
			PutInt(Score);
			PutInt(Player);
			if(Extended)
				Out.AddString("");
			if(Out.Error())
				return false;
		}
		const int OutputSize = SERVERBROWSE_SIZE + Out.Size();
		if(Out.Error() || OutputSize > std::min(Capacity, (int)NET_MAX_CONNLESS_PAYLOAD))
			return false;
		mem_copy(pData, Extended ? SERVERBROWSE_INFO_EXTENDED : SERVERBROWSE_INFO, SERVERBROWSE_SIZE);
		mem_copy((unsigned char *)pData + SERVERBROWSE_SIZE, Out.Data(), Out.Size());
		Size = OutputSize;
		return true;
	}
}

#endif
