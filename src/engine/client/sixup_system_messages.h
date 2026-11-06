#ifndef ENGINE_CLIENT_SIXUP_SYSTEM_MESSAGES_H
#define ENGINE_CLIENT_SIXUP_SYSTEM_MESSAGES_H

#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>
#include <engine/shared/uuid_manager.h>

namespace sixup_translation
{
	// 仅处理保留原载荷的接收分支；地图切换、服务器信息和认证另行处理。
	inline int IncomingSystemMessageId(int MsgId)
	{
		if(MsgId >= OFFSET_UUID)
			return MsgId;
		switch(MsgId)
		{
		case protocol7::NETMSG_MAP_DATA: return NETMSG_MAP_DATA;
		case protocol7::NETMSG_CON_READY: return NETMSG_CON_READY;
		case protocol7::NETMSG_SNAP: return NETMSG_SNAP;
		case protocol7::NETMSG_SNAPEMPTY: return NETMSG_SNAPEMPTY;
		case protocol7::NETMSG_SNAPSINGLE: return NETMSG_SNAPSINGLE;
		case protocol7::NETMSG_SNAPSMALL: return NETMSG_SNAPSMALL;
		case protocol7::NETMSG_INPUTTIMING: return NETMSG_INPUTTIMING;
		case protocol7::NETMSG_RCON_LINE: return NETMSG_RCON_LINE;
		case protocol7::NETMSG_RCON_CMD_ADD: return NETMSG_RCON_CMD_ADD;
		case protocol7::NETMSG_RCON_CMD_REM: return NETMSG_RCON_CMD_REM;
		case protocol7::NETMSG_PING: return NETMSG_PING;
		case protocol7::NETMSG_PING_REPLY: return NETMSG_PING_REPLY;
		default: return -1;
		}
	}

	// 仅转换系统消息编号，NoTranslate与UUID/载荷打包仍由发送入口负责。
	inline int OutgoingSystemMessageId(int MsgId)
	{
		if(MsgId >= OFFSET_UUID)
			return MsgId;
		switch(MsgId)
		{
		case NETMSG_INFO:
		case NETMSG_REQUEST_MAP_DATA: return MsgId;
		case NETMSG_READY: return protocol7::NETMSG_READY;
		case NETMSG_RCON_CMD: return protocol7::NETMSG_RCON_CMD;
		case NETMSG_ENTERGAME: return protocol7::NETMSG_ENTERGAME;
		case NETMSG_INPUT: return protocol7::NETMSG_INPUT;
		case NETMSG_RCON_AUTH: return protocol7::NETMSG_RCON_AUTH;
		case NETMSG_PING: return protocol7::NETMSG_PING;
		case NETMSG_PING_REPLY: return protocol7::NETMSG_PING_REPLY;
		default: return -1;
		}
	}
}

#endif
