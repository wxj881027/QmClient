#include "decorative_throw_policy.h"
#include "qmclient.h"

#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/gameclient.h>

void CQmClient::SendQmDecorativeThrow(int Type, int PlayerId, vec2 Origin, vec2 Direction)
{
	if(Type < 0 || Type >= QmDecorativeThrow::COUNT || !QmDecorativeThrow::ValidGeometry(Origin, Direction) ||
		!g_Config.m_QmDecorativeThrows || !m_pQmRealtimeTransport || !m_QmRealtimeHelloSent ||
		m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED || Client()->State() != IClient::STATE_ONLINE ||
		(PlayerId != GameClient()->m_aLocalIds[0] && PlayerId != GameClient()->m_aLocalIds[1]))
		return;
	const std::string Presence = BuildQmRealtimePresence(false);
	if(!m_pQmRealtimeTransport->SendText(Presence.c_str(), Presence.size()))
		return;
	char aServer[NETADDR_MAXSTRSIZE];
	net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("decorative_throw");
	Writer.WriteAttribute("projectile");
	Writer.WriteStrValue(QmDecorativeThrow::NAMES[Type]);
	Writer.WriteAttribute("player_id");
	Writer.WriteIntValue(PlayerId);
	Writer.WriteAttribute("server_address");
	Writer.WriteStrValue(aServer);
	Writer.WriteAttribute("session_id");
	Writer.WriteStrValue(m_aQmDeveloperSessionId);
	Writer.EndObject();
	std::string Body = Writer.GetOutputString();
	// 字符串交给 JSON 写入器转义，仅把已验证的有限数值写入连续方向字段。
	char aGeometry[256];
	str_format(aGeometry, sizeof(aGeometry), ",\"origin\":{\"x\":%.3f,\"y\":%.3f},\"direction\":{\"x\":%.6f,\"y\":%.6f}}",
		Origin.x, Origin.y, Direction.x, Direction.y);
	Body.pop_back();
	Body += aGeometry;
	m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size());
}

void CQmClient::QueueQmDecorativeThrow(SQmRealtimeMessage Message)
{
	if(!Message.m_HasDecorativeThrow || Message.m_ThrowClientId == m_aQmClientPlaytimeClientId)
		return;
	if(m_QmDecorativeThrowEvents.size() >= 32)
		m_QmDecorativeThrowEvents.pop_front();
	m_QmDecorativeThrowEvents.push_back(std::move(Message));
}

bool CQmClient::PopQmDecorativeThrow(SQmRealtimeMessage &Message)
{
	if(m_QmDecorativeThrowEvents.empty())
		return false;
	Message = std::move(m_QmDecorativeThrowEvents.front());
	m_QmDecorativeThrowEvents.pop_front();
	return true;
}
