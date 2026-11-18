#include "dummy_distance_link.h"

#include <engine/graphics.h>
#include <engine/shared/snapshot.h>

#include <game/client/gameclient.h>

#include <array>
#include <cmath>

namespace qm_dummy_link
{
	static SEndpoint SnapshotEndpoint(vec2 Pos, const CNetObj_DDNetCharacter *pExtended, bool LegacySolo)
	{
		SEndpoint Endpoint{Pos, LegacySolo};
		if(pExtended)
		{
			Endpoint.m_Solo = (pExtended->m_Flags & CHARACTERFLAG_SOLO) != 0;
			Endpoint.m_Frozen = pExtended->m_FreezeEnd != 0 || (pExtended->m_Flags & CHARACTERFLAG_MOVEMENTS_DISABLED) != 0;
		}
		return Endpoint;
	}

	std::optional<SEndpoint> CView::CurrentEndpoint(CGameClient &GameClient, int Connection)
	{
		const int ClientId = GameClient.m_aLocalIds[Connection];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return std::nullopt;
		const auto &Character = GameClient.m_Snap.m_aCharacters[ClientId];
		const auto &Tuning = GameClient.m_aTuning[Connection];
		const bool LegacySolo = !Tuning.m_PlayerHooking && !Tuning.m_PlayerCollision;
		if(Character.m_Active)
		{
			auto Endpoint = SnapshotEndpoint(GameClient.m_aClients[ClientId].m_RenderPos, Character.m_HasExtendedData ? &Character.m_ExtendedData : nullptr, LegacySolo);
			Endpoint.m_HookTargetPos = GameClient.HookCollTargetPosition(ClientId, GameClient.Client()->IntraGameTick(g_Config.m_ClDummy));
			return Endpoint;
		}

		const int Tick = GameClient.Client()->GameTick(Connection);
		auto &Fallback = m_aFallback[Connection];
		if(Fallback.m_Tick == Tick && Fallback.m_ClientId == ClientId)
			return Fallback.m_Endpoint;
		Fallback = {Tick, ClientId, std::nullopt};
		const CSnapshot *pSnap = GameClient.Client()->CurrentSnapshotForConnection(Connection);
		const auto *pChar = pSnap ? static_cast<const CNetObj_Character *>(pSnap->FindItem(NETOBJTYPE_CHARACTER, ClientId)) : nullptr;
		if(pChar && (pChar->m_Tick == 0 || (pChar->m_Tick <= Tick && Tick - pChar->m_Tick <= 3 * GameClient.Client()->GameTickSpeed())))
		{
			CNetObj_Character Current = *pChar;
			if(Current.m_Tick && Current.m_Tick < Tick)
				GameClient.EvolveSnapshotCharacter(&Current, Tick);
			const auto *pExtended = static_cast<const CNetObj_DDNetCharacter *>(pSnap->FindItem(NETOBJTYPE_DDNETCHARACTER, ClientId));
			Fallback.m_Endpoint = SnapshotEndpoint(vec2(Current.m_X, Current.m_Y), pExtended, LegacySolo);
		}
		return Fallback.m_Endpoint;
	}

	namespace
	{
		void DrawDrops(IGraphics &Graphics, const SLink &Link, float Pixel, ColorRGBA Fill)
		{
			constexpr size_t Segments = 64;
			static const auto s_aUnit = [] {
				std::array<vec2, Segments> aUnit;
				for(size_t Index = 0; Index < Segments; ++Index)
					aUnit[Index] = direction(2.0f * pi * Index / Segments);
				return aUnit;
			}();
			const vec2 Side(-Link.m_Direction.y, Link.m_Direction.x);
			std::array<vec2, Segments> aInner, aOuter;
			for(size_t Index = 0; Index < Segments; ++Index)
			{
				const vec2 Local = Link.m_Shape.Offset(s_aUnit[Index]);
				aInner[Index] = Link.m_Direction * Local.x + Side * Local.y;
			}
			for(size_t Index = 0; Index < Segments; ++Index)
			{
				const vec2 Tangent = aInner[(Index + 1) % Segments] - aInner[(Index + Segments - 1) % Segments];
				aOuter[Index] = aInner[Index] + normalize(vec2(Tangent.y, -Tangent.x)) * Pixel;
			}
			for(const bool AtDummy : {false, true})
			{
				const vec2 Center = AtDummy ? Link.m_Dummy : Link.m_Main;
				const auto World = [&](vec2 Offset) { return AtDummy ? Center - Offset : Center + Offset; };
				std::array<IGraphics::CFreeformItem, Segments / 2> aFan;
				std::array<IGraphics::CFreeformItem, Segments> aEdge;
				for(size_t Index = 0; Index < Segments; ++Index)
				{
					const size_t Next = (Index + 1) % Segments;
					aEdge[Index] = IGraphics::CFreeformItem(World(aInner[Index]), World(aInner[Next]), World(aOuter[Index]), World(aOuter[Next]));
					if(Index % 2 == 0)
						aFan[Index / 2] = IGraphics::CFreeformItem(Center, World(aInner[Index]), World(aInner[(Next + 1) % Segments]), World(aInner[Next]));
				}
				Graphics.SetColor(Fill);
				Graphics.QuadsDrawFreeform(aFan.data(), aFan.size());
				const ColorRGBA Clear = Fill.WithAlpha(0.0f);
				const IGraphics::CColorVertex aColors[] = {{0, Fill}, {1, Fill}, {2, Clear}, {3, Clear}};
				Graphics.SetColorVertex(aColors, std::size(aColors));
				Graphics.QuadsDrawFreeform(aEdge.data(), aEdge.size());
			}
		}

		void DrawConnector(IGraphics &Graphics, const SLink &Link, float Pixel, ColorRGBA Color)
		{
			const vec2 Start = Link.LineStart();
			const vec2 End = Link.LineEnd();
			const vec2 Side = vec2(-Link.m_Direction.y, Link.m_Direction.x) * Pixel;
			const auto Strip = [&](float From, float To, ColorRGBA Inner, ColorRGBA Outer) {
				const IGraphics::CFreeformItem Quad(Start + Side * From, End + Side * From, Start + Side * To, End + Side * To);
				const IGraphics::CColorVertex aColors[] = {{0, Inner}, {1, Inner}, {2, Outer}, {3, Outer}};
				Graphics.SetColorVertex(aColors, std::size(aColors));
				Graphics.QuadsDrawFreeform(&Quad, 1);
			};
			Strip(-0.85f, 0.85f, Color, Color);
			Strip(-0.85f, -1.85f, Color, Color.WithAlpha(0.0f));
			Strip(0.85f, 1.85f, Color, Color.WithAlpha(0.0f));
		}
	}

	std::optional<SLink> CView::CurrentLink(CGameClient &GameClient)
	{
		if(!g_Config.m_QmDummyDistanceLink || GameClient.Client()->State() != IClient::STATE_ONLINE || !GameClient.Client()->DummyConnected())
		{
			m_Motion = {};
			return std::nullopt;
		}
		const int Active = g_Config.m_ClDummy;
		const int ActiveId = GameClient.m_aLocalIds[Active];
		const CTuningParams &Tuning = GameClient.HookCollTuning(ActiveId);
		auto Link = MakeLink(CurrentEndpoint(GameClient, IClient::CONN_MAIN), CurrentEndpoint(GameClient, IClient::CONN_DUMMY), Tuning.m_HookLength, Active == IClient::CONN_DUMMY);
		if(Link && g_Config.m_QmDummyDistanceLinkStyle != 0)
		{
			const uint64_t Frame = GameClient.Client()->PerfFrame();
			const float Dt = Frame != m_LastDropletFrame ? GameClient.Client()->RenderFrameTime() : 0.0f;
			m_LastDropletFrame = Frame;
			Link->m_Shape = m_Motion.Advance(Link->m_Distance, Dt, g_Config.m_QmUiMotionLevel);
		}
		else
			m_Motion = {};
		return Link;
	}

	void Draw(IGraphics &Graphics, const SLink &Link)
	{
		if(Link.m_Distance <= 0.0f)
			return;
		float Left, Top, Right, Bottom;
		Graphics.GetScreen(&Left, &Top, &Right, &Bottom);
		const float Pixel = (Right - Left) / std::max(Graphics.ScreenWidth(), 1);
		const ColorRGBA Color = Link.m_InHookRange ? ColorRGBA(0.18f, 1.0f, 0.40f, Link.m_Alpha) : ColorRGBA(0.85f, 0.90f, 1.0f, Link.m_Alpha);
		Graphics.TextureClear();
		Graphics.BlendNormal();
		Graphics.QuadsBegin();
		DrawConnector(Graphics, Link, Pixel, Color);
		if(Link.m_Shape.m_Radius > Pixel * 0.1f)
			DrawDrops(Graphics, Link, Pixel, Color.WithAlpha(Link.m_Alpha * (Link.m_InHookRange ? 0.82f : 0.55f)));
		Graphics.QuadsEnd();
	}
}
