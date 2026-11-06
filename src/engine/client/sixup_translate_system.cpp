#include "sixup_map_change.h"
#include "sixup_server_info.h"
#include "sixup_system_messages.h"

#include <engine/client/client.h>

int CClient::TranslateSysMsg(int *pMsgId, bool System, CUnpacker *pUnpacker, CPacker *pPacker, CNetChunk *pPacket, bool *pIsExMsg)
{
	*pIsExMsg = false;
	if(!System)
		return -1;

	// ddnet ex
	if(*pMsgId > NETMSG_WHATIS && *pMsgId < NETMSG_RCON_CMD_GROUP_END)
	{
		*pIsExMsg = true;
		return 0;
	}

	pPacker->Reset();

	if(*pMsgId == protocol7::NETMSG_MAP_CHANGE)
	{
		*pMsgId = NETMSG_MAP_CHANGE;
		if(!sixup_translation::TranslateMapChange(m_TranslationContext, *pUnpacker, *pPacker))
		{
			*pMsgId = -1;
			return -1;
		}
		return 0;
	}
	else if(*pMsgId == protocol7::NETMSG_SERVERINFO)
	{
		// side effect only
		// this is a 0.7 only message and not handled in 0.6 code
		*pMsgId = -1;
		return sixup_translation::ApplyServerInfo(m_CurrentServerInfo, pPacket->m_Address, *pUnpacker) ? 0 : -1;
	}
	else if(*pMsgId == protocol7::NETMSG_RCON_AUTH_ON)
	{
		*pMsgId = NETMSG_RCON_AUTH_STATUS;
		pPacker->AddInt(1); // authed
		pPacker->AddInt(1); // cmdlist
		return 0;
	}
	else if(*pMsgId == protocol7::NETMSG_RCON_AUTH_OFF)
	{
		*pMsgId = NETMSG_RCON_AUTH_STATUS;
		pPacker->AddInt(0); // authed
		pPacker->AddInt(0); // cmdlist
		return 0;
	}

	const int Translated = sixup_translation::IncomingSystemMessageId(*pMsgId);
	if(Translated < 0 && *pMsgId != protocol7::NETMSG_INFO &&
		*pMsgId != protocol7::NETMSG_MAPLIST_ENTRY_ADD && *pMsgId != protocol7::NETMSG_MAPLIST_ENTRY_REM)
		dbg_msg("sixup", "drop unknown sys msg=%d", *pMsgId);
	*pMsgId = Translated;

	return -1;
}
