#include "dummy_distance_link.h"

#include <engine/graphics.h>
#include <engine/shared/snapshot.h>

#include <game/client/gameclient.h>

#include <array>
#include <cmath>

namespace qm_dummy_link
{
	std::optional<SEndpoint> CView::CurrentEndpoint(CGameClient &GameClient, int Connection)
	{
		const int ClientId = GameClient.m_aLocalIds[Connection];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return std::nullopt;
		const auto &Character = GameClient.m_Snap.m_aCharacters[ClientId];
		const auto &Tuning = GameClient.m_aTuning[Connection];
		const bool LegacySolo = !Tuning.m_PlayerHooking && !Tuning.m_PlayerCollision;
		if(Character.m_Active)
			return SEndpoint{GameClient.m_aClients[ClientId].m_RenderPos, Character.m_HasExtendedData ? GameClient.m_aClients[ClientId].m_Solo : LegacySolo};

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
			Fallback.m_Endpoint = SEndpoint{vec2(Current.m_X, Current.m_Y), pExtended ? (pExtended->m_Flags & CHARACTERFLAG_SOLO) != 0 : LegacySolo};
		}
		return Fallback.m_Endpoint;
	}

	namespace
	{
		void DrawDroplet(IGraphics &Graphics, const SLink &Link)
		{
			if(Link.m_Distance == 0.0f)
			{
				Graphics.DrawCircle(Link.m_Main.x, Link.m_Main.y, HAMMER_REACH, 32);
				return;
			}

			static const auto s_aContour = [] {
				std::array<vec2, 34> aContour;
				aContour.front() = vec2(TIP_REACH, 0);
				const float TangentAngle = std::acos(HAMMER_REACH / TIP_REACH);
				for(size_t Index = 1; Index < aContour.size(); ++Index)
					aContour[Index] = direction(TangentAngle + (2.0f * pi - 2.0f * TangentAngle) * (Index - 1) / (aContour.size() - 2)) * HAMMER_REACH;
				return aContour;
			}();
			const vec2 Normal(-Link.m_Direction.y, Link.m_Direction.x);
			const auto World = [&](vec2 Point) { return Link.m_Main + Link.m_Direction * Point.x + Normal * Point.y; };
			std::array<IGraphics::CFreeformItem, s_aContour.size() / 2> aFan;
			for(size_t Index = 0; Index < aFan.size(); ++Index)
			{
				const size_t First = Index * 2;
				aFan[Index] = IGraphics::CFreeformItem(Link.m_Main, World(s_aContour[First]), World(s_aContour[(First + 2) % s_aContour.size()]), World(s_aContour[First + 1]));
			}
			Graphics.QuadsDrawFreeform(aFan.data(), aFan.size());
		}
	}

	std::optional<SLink> CView::CurrentLink(CGameClient &GameClient)
	{
		if(GameClient.Client()->State() != IClient::STATE_ONLINE || !GameClient.Client()->DummyConnected())
			return std::nullopt;
		return MakeLink(CurrentEndpoint(GameClient, IClient::CONN_MAIN), CurrentEndpoint(GameClient, IClient::CONN_DUMMY), GameClient.m_aTuning[IClient::CONN_MAIN].m_HookLength);
	}

	void Draw(IGraphics &Graphics, const SLink &Link, bool Droplet)
	{
		const ColorRGBA Color = Link.m_InHookRange ? ColorRGBA(0.40f, 0.95f, 0.65f, Link.m_Alpha) : ColorRGBA(0.85f, 0.90f, 1.0f, Link.m_Alpha);
		Graphics.TextureClear();
		Graphics.BlendNormal();
		if(Droplet)
		{
			Graphics.QuadsBegin();
			Graphics.SetColor(Color.WithAlpha(Link.m_Alpha * 0.35f));
			DrawDroplet(Graphics, Link);
			Graphics.QuadsEnd();
		}
		if(const auto Start = Link.LineStart(Droplet))
		{
			IGraphics::CLineItem Line(*Start, Link.m_Dummy);
			Graphics.LinesBegin();
			Graphics.SetColor(Color);
			Graphics.LinesDraw(&Line, 1);
			Graphics.LinesEnd();
		}
	}
}
