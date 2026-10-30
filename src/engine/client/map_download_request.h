#ifndef ENGINE_CLIENT_MAP_DOWNLOAD_REQUEST_H
#define ENGINE_CLIENT_MAP_DOWNLOAD_REQUEST_H

#include <engine/message.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>

#include <utility>

namespace map_download
{
	template<typename TSend>
	void SendRequest(bool Sixup, int Chunk, TSend &&Send)
	{
		const int MsgId = Sixup ? static_cast<int>(protocol7::NETMSG_REQUEST_MAP_DATA) : static_cast<int>(NETMSG_REQUEST_MAP_DATA);
		CMsgPacker Msg(MsgId, true, Sixup);
		if(!Sixup)
			Msg.AddInt(Chunk);
		Send(Msg);
	}

	template<typename TSend>
	bool ContinueRequest(bool Sixup, int Chunk, int ChunksPerRequest, TSend &&Send)
	{
		// 0.7服务端按窗口连续发送，窗口内不再发请求；0.6仍逐块请求。
		if(Chunk < 0 || (Sixup && (ChunksPerRequest <= 0 || Chunk % ChunksPerRequest != 0)))
			return false;
		SendRequest(Sixup, Chunk, std::forward<TSend>(Send));
		return true;
	}
}

#endif
