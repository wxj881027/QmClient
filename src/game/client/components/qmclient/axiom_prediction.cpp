#include <engine/shared/config.h>

#include <game/client/gameclient.h>

// 仅在线客户端启用；公共碰撞类和服务端不读取客户端配置。
// 角色侧的 Qm 四边形钩子与地块处理属于预测实体，定义在 character.cpp。
void CGameClient::UpdateQmAxiomPrediction()
{
	const bool Enabled = Client()->State() == IClient::STATE_ONLINE && g_Config.m_QmAxiomQuadPrediction;
	m_Collision.SetQmAxiomQuadCollision(Enabled);
	if(!m_Collision.QmAxiomQuadCollisionEnabled())
		return;

	const auto *pInfo = static_cast<const CNetObj_GameInfo *>(Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_GAMEINFO, 0));
	const int RoundStart = pInfo ? pInfo->m_RoundStartTick : 0;
	m_Collision.SetQmAxiomQuadRoundStartTick(RoundStart);
	const bool Predicted = g_Config.m_ClPredict &&
			       !(m_Snap.m_SpecInfo.m_Active && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW);
	const double Tick = Predicted ?
				    Client()->PredGameTick(g_Config.m_ClDummy) - 1 + double(Client()->PredIntraGameTick(g_Config.m_ClDummy)) :
				    mix<double>(Client()->PrevGameTick(g_Config.m_ClDummy), Client()->GameTick(g_Config.m_ClDummy), Client()->IntraGameTick(g_Config.m_ClDummy));
	m_Collision.SetQmAxiomQuadTime(pInfo ? (Tick - RoundStart) / Client()->GameTickSpeed() : 0.0);
}
