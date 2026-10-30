#ifndef ENGINE_CLIENT_MESSAGE_REPACK_H
#define ENGINE_CLIENT_MESSAGE_REPACK_H

#include "sixup_system_messages.h"

#include <base/log.h>

#include <engine/message.h>
#include <engine/shared/network.h>

#include <generated/protocolglue.h>

namespace client_message
{
	// 源消息与输出缓冲独立；失败的输出不得进入录像或网络发送。
	inline bool Repack(const CMsgPacker &Msg, CPacker &Packer, bool Sixup, const CUuidManager &Uuids, int Flags)
	{
		Packer.Reset();
		if(Msg.Error() || Msg.m_MsgId < 0)
			return false;
		int MsgId = Msg.m_MsgId;
		if(Sixup && !Msg.m_NoTranslate)
		{
			if(Msg.m_System)
			{
				MsgId = sixup_translation::OutgoingSystemMessageId(MsgId);
				if(MsgId < 0)
				{
					log_error("net", "0.7 DROP send sys %d", Msg.m_MsgId);
					return false;
				}
			}
			else
			{
				if(MsgId < OFFSET_UUID)
					MsgId = Msg_SixToSeven(MsgId);
				if(MsgId < 0)
					return false;
			}
		}

		if(Msg.m_MsgId < OFFSET_UUID)
		{
			if(MsgId >= OFFSET_UUID)
				return false;
			Packer.AddInt((MsgId << 1) | (Msg.m_System ? 1 : 0));
		}
		else
		{
			if(Msg.m_MsgId - OFFSET_UUID >= Uuids.NumUuids())
				return false;
			Packer.AddInt(Msg.m_System ? 1 : 0);
			Uuids.PackUuid(Msg.m_MsgId, &Packer);
		}
		Packer.AddRaw(Msg.Data(), Msg.Size());
		return !Packer.Error() && ((Flags & MSGFLAG_NOSEND) != 0 || Packer.Size() <= NET_MAX_CHUNK_SIZE);
	}
}

#endif
