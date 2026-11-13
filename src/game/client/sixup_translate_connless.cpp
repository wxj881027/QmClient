#include <engine/client/sixup_connless.h>

#include <game/client/gameclient.h>

bool CClient::PreprocessConnlessPacket7(CNetChunk *pPacket)
{
	// UDP/KCP收包均借用CNetPacketConstruct::m_aChunkData，容量大于无连接载荷上限。
	return sixup_translation::PreprocessConnless((void *)pPacket->m_pData, pPacket->m_DataSize, NET_MAX_CONNLESS_PAYLOAD);
}
