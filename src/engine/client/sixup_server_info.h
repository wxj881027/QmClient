#ifndef ENGINE_CLIENT_SIXUP_SERVER_INFO_H
#define ENGINE_CLIENT_SIXUP_SERVER_INFO_H

#include <engine/serverbrowser.h>
#include <engine/shared/packer.h>

namespace sixup_translation
{
	inline bool ApplyServerInfo(CServerInfo &Info, const NETADDR &Address, CUnpacker &Unpacker)
	{
		const int StringFlags = CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES;
		const char *pVersion = Unpacker.GetString(StringFlags);
		const char *pName = Unpacker.GetString(StringFlags);
		Unpacker.GetString(StringFlags); // 主机名不参与当前信息展示。
		Unpacker.GetString(StringFlags); // 地图名继续由实际加载的文件决定。
		const char *pGameType = Unpacker.GetString(StringFlags);
		const int Flags = Unpacker.GetInt();
		Unpacker.GetInt(); // 服务端难度级别。
		const int NumPlayers = Unpacker.GetInt();
		const int MaxPlayers = Unpacker.GetInt();
		const int NumClients = Unpacker.GetInt();
		const int MaxClients = Unpacker.GetInt();
		if(Unpacker.Error() || NumPlayers < 0 || MaxPlayers < 0 || NumClients < 0 || MaxClients < 0 ||
			NumPlayers > NumClients || MaxPlayers > MaxClients)
			return false;

		// 所有字段完整后再提交；其余信息及玩家容器不复制、不覆盖。
		net_addr_str(&Address, Info.m_aAddress, sizeof(Info.m_aAddress), true);
		str_copy(Info.m_aVersion, pVersion);
		str_copy(Info.m_aName, pName);
		str_clean_whitespaces(Info.m_aName);
		str_copy(Info.m_aGameType, pGameType);
		Info.m_Flags = (Info.m_Flags & ~SERVER_FLAG_PASSWORD) | (Flags & SERVER_FLAG_PASSWORD);
		Info.m_NumPlayers = NumPlayers;
		Info.m_MaxPlayers = MaxPlayers;
		Info.m_NumClients = NumClients;
		Info.m_MaxClients = MaxClients;
		return true;
	}
}

#endif
