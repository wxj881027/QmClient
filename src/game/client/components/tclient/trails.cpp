#include "trails.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/components/effects.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

void CTrails::OnReset()
{
	for(auto &State : m_aTrailStates)
		State.Reset();
	std::fill(std::begin(m_aPositionSources), std::end(m_aPositionSources), -1);
	m_LastDummy = m_LastStyle = m_LastLength = -1;
}

void CTrails::OnNewSnapshot()
{
	// 快速死亡再出生可能发生在两帧渲染之间，因此在快照入口清理，而非只检查 OnRender。
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			m_aTrailStates[ClientId].Reset();
	for(int i = 0; i < Client()->SnapNumItems(IClient::SNAP_CURRENT); ++i)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, i);
		if(Item.m_Type == NETEVENTTYPE_DEATH)
		{
			const int ClientId = static_cast<const CNetEvent_Death *>(Item.m_pData)->m_ClientId;
			if(ClientId >= 0 && ClientId < MAX_CLIENTS)
				m_aTrailStates[ClientId].Reset();
		}
	}
}

void CTrails::RenderTeeTrails()
{
	if(GameClient()->IsRenderingDummyMiniMap())
		return;
	if(!g_Config.m_TcTeeTrail)
	{
		OnReset();
		return;
	}
	const int Style = qm_tee_trail::ResolveStyle(g_Config.m_TcTeeTrailStyle);
	if(m_LastDummy != g_Config.m_ClDummy || m_LastStyle != Style || m_LastLength != g_Config.m_TcTeeTrailLength)
		OnReset();
	m_LastDummy = g_Config.m_ClDummy;
	m_LastStyle = Style;
	m_LastLength = g_Config.m_TcTeeTrailLength;

	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const float PixelSize = std::max(0.025f, (X1 - X0) / std::max(1, Graphics()->ScreenWidth()));
	const bool ZoomAllowed = GameClient()->m_Camera.ZoomAllowed();
	// 生命周期使用同一游戏时钟；暂停 Demo 不老化，回退由 State 清空。
	const double Time = double(Client()->GameTick(g_Config.m_ClDummy)) + Client()->IntraGameTick(g_Config.m_ClDummy);
	Graphics()->TextureClear();
	Graphics()->BlendNormal();
	Graphics()->QuadsBegin();
	bool Additive = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		auto &State = m_aTrailStates[ClientId];
		const bool Local = GameClient()->IsLocalClientId(ClientId);
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || (!Local && (!g_Config.m_TcTeeTrailOthers || !ZoomAllowed)))
		{
			State.Reset();
			continue;
		}
		const auto &Data = GameClient()->m_aClients[ClientId];
		const auto &Snapshot = GameClient()->m_Snap.m_aCharacters[ClientId];
		const float Intra = std::clamp(Data.m_IsPredicted ? Client()->PredIntraGameTick(g_Config.m_ClDummy) : Client()->IntraGameTick(g_Config.m_ClDummy), 0.0f, 1.0f);
		const vec2 Velocity = mix(vec2(Data.m_RenderPrev.m_VelX, Data.m_RenderPrev.m_VelY), vec2(Data.m_RenderCur.m_VelX, Data.m_RenderCur.m_VelY), Intra) / 256.0f;
		const float Speed = length(Velocity);
		const float MovementBudget = Data.m_RenderCur.m_Weapon == WEAPON_NINJA ? std::max(Speed, float(g_pData->m_Weapons.m_Ninja.m_Velocity)) : Speed;
		const int Source = int(Data.m_IsPredicted) | (g_Config.m_TcRemoveAnti << 1) | (int(g_Config.m_TcUnpredOthersInFreeze && Client()->m_IsLocalFrozen) << 2) | (g_Config.m_TcSwapGhosts << 3) | (int(GameClient()->m_TClient.IsFastInputActive()) << 4);
		const bool SourceChanged = m_aPositionSources[ClientId] != Source;
		m_aPositionSources[ClientId] = Source;
		// 拦截传送端点的插值窗口，否则远端 Tee 会在几帧内沿传送直线留下假轨迹。
		const float RenderJump = distance(vec2(Data.m_RenderPrev.m_X, Data.m_RenderPrev.m_Y), vec2(Data.m_RenderCur.m_X, Data.m_RenderCur.m_Y));
		const float ServerJump = distance(vec2(Snapshot.m_Prev.m_X, Snapshot.m_Prev.m_Y), vec2(Snapshot.m_Cur.m_X, Snapshot.m_Cur.m_Y));
		const float ServerSpeed = std::max(length(vec2(Snapshot.m_Prev.m_VelX, Snapshot.m_Prev.m_VelY)), length(vec2(Snapshot.m_Cur.m_VelX, Snapshot.m_Cur.m_VelY))) / 256.0f;
		const int TickGap = std::max(1, Client()->GameTick(g_Config.m_ClDummy) - Client()->PrevGameTick(g_Config.m_ClDummy));
		const float ServerMovementBudget = Snapshot.m_Cur.m_Weapon == WEAPON_NINJA ? std::max(ServerSpeed, float(g_pData->m_Weapons.m_Ninja.m_Velocity)) : ServerSpeed;
		if(RenderJump > 48.0f + MovementBudget * (Data.m_IsPredicted ? 1 : TickGap) * 2.5f || ServerJump > 48.0f + ServerMovementBudget * TickGap * 2.5f)
		{
			State.Reset();
			continue;
		}
		// 此组件位于 players 之前；只采样最终 m_RenderPos 一次，预测 ghost 不进入此入口。
		// Ninja 冲刺等移动的核心速度可能为零；通过已检查的端点位移补足视觉速度。
		const float VisualSpeed = std::max(Speed, RenderJump / (Data.m_IsPredicted ? 1.0f : float(TickGap)));
		State.Update(Data.m_RenderPos, Time, VisualSpeed, qm_tee_trail::Lifetime(Style, m_LastLength, VisualSpeed), SourceChanged);
		State.Export(m_vTrail);
		if(m_vTrail.size() < 2)
			continue;
		float MinX = m_vTrail[0].m_Pos.x, MaxX = MinX, MinY = m_vTrail[0].m_Pos.y, MaxY = MinY;
		float Alpha = g_Config.m_TcTeeTrailAlpha / 100.0f;
		if(GameClient()->IsOtherTeam(ClientId))
			Alpha *= g_Config.m_ClShowOthersAlpha / 100.0f;
		if(Alpha <= 0)
			continue;
		for(const auto &Part : m_vTrail)
		{
			MinX = std::min(MinX, Part.m_Pos.x);
			MaxX = std::max(MaxX, Part.m_Pos.x);
			MinY = std::min(MinY, Part.m_Pos.y);
			MaxY = std::max(MaxY, Part.m_Pos.y);
		}
		// 连同存活的尾部一起裁剪，头部出屏时不截掉仍在屏幕内的拖尾。
		// 采样与老化仍然每帧执行，只有不可见轨迹的配色和几何构建被跳过。
		const float Margin = g_Config.m_TcTeeTrailWidth * 3.0f + 360.0f;
		if(MaxX < X0 - Margin || MinX > X1 + Margin || MaxY < Y0 - Margin || MinY > Y1 + Margin)
			continue;
		for(auto &Part : m_vTrail)
		{
			switch(g_Config.m_TcTeeTrailColorMode)
			{
			case COLORMODE_TEE:
				Part.m_Col = Data.m_RenderInfo.m_CustomColoredSkin ? Data.m_RenderInfo.m_ColorBody : Data.m_RenderInfo.m_BloodColor;
				break;
			case COLORMODE_RAINBOW:
				Part.m_Col = color_cast<ColorRGBA>(ColorHSLA(float(std::fmod(Part.m_Time / (std::max(5, m_LastLength) * 2.0) + ClientId * 0.37, 1.0)), 1.0f, 0.5f));
				break;
			case COLORMODE_SPEED:
				Part.m_Col = color_cast<ColorRGBA>(ColorHSLA(0.66f * (1 - std::clamp(Part.m_Speed / 30.0f, 0.0f, 1.0f)), 1.0f, 0.55f));
				break;
			case COLORMODE_RANDOM:
			{
				const unsigned Seed = unsigned(ClientId) * 0x45d9f3bu + unsigned(Part.m_Tick) * 0x119de1f3u;
				Part.m_Col = color_cast<ColorRGBA>(ColorHSLA((Seed & 0xffff) / 65535.0f, 1.0f, 0.55f));
				break;
			}
			default:
				Part.m_Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcTeeTrailColor));
				break;
			}
			Part.m_Col.a = Alpha;
		}
		qm_tee_trail::BuildEffect(m_vTrail, Style, g_Config.m_TcTeeTrailStyleColors != 0, Time, g_Config.m_TcTeeTrailWidth, ClientId * 131 + 17, m_vQuads, PixelSize, g_Config.m_TcTeeTrailTaper != 0, g_Config.m_TcTeeTrailFade != 0);
		for(const auto &Quad : m_vQuads)
		{
			if(Quad.m_Additive != Additive)
			{
				Graphics()->QuadsEnd();
				Additive = Quad.m_Additive;
				if(Additive)
					Graphics()->BlendAdditive();
				else
					Graphics()->BlendNormal();
				Graphics()->QuadsBegin();
			}
			// SetColor4 的后两个参数对应左下 / 右下，与自由四边形的底边顺序一致。
			Graphics()->SetColor4(Quad.m_aColor[0], Quad.m_aColor[1], Quad.m_aColor[3], Quad.m_aColor[2]);
			const IGraphics::CFreeformItem Item(Quad.m_aPos[0], Quad.m_aPos[1], Quad.m_aPos[3], Quad.m_aPos[2]);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->SetColor(1, 1, 1, 1);
	Graphics()->QuadsSetRotation(0);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CTrails::OnRender()
{
	if(GameClient()->m_RankGhost.IsViewModeActive())
	{
		OnReset();
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		OnReset();
		return;
	}

	if(!GameClient()->m_Snap.m_pGameInfoObj)
	{
		OnReset();
		return;
	}

	RenderTeeTrails();
}
