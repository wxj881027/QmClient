#include "decorative_projectiles.h"

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/localization.h>

#include <algorithm>

void CQmDecorativeProjectiles::OnConsoleInit()
{
	Console()->Register("qm_throw", "s[type]", CFGFLAG_CLIENT, ConThrow, this, "Throw a decorative grass, tomato or egg projectile");
}

void CQmDecorativeProjectiles::ConThrow(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CQmDecorativeProjectiles *>(pUserData)->Throw(QmDecorativeThrow::TypeFromName(pResult->GetString(0)));
}

void CQmDecorativeProjectiles::EnsureResources()
{
	if(m_ResourcesAttempted)
		return;
	m_ResourcesAttempted = true;
	for(int Type = 0; Type < QmDecorativeThrow::COUNT; ++Type)
	{
		char aPath[128];
		str_format(aPath, sizeof(aPath), "qm_throwables/%s.png", QmDecorativeThrow::NAMES[Type]);
		CImageInfo Image;
		if(!Graphics()->LoadPng(Image, aPath, IStorage::TYPE_ALL))
			continue;
		// 掩码按显示尺寸采样，避免高分辨率素材放大碰撞查询成本。
		std::array<unsigned char, 64 * 64 * 4> aMask{};
		for(int Y = 0; Y < 64; ++Y)
			for(int X = 0; X < 64; ++X)
				aMask[(Y * 64 + X) * 4 + 3] = Image.PixelColor(
									   std::min(Image.m_Width - 1, (2 * X + 1) * Image.m_Width / 128),
									   std::min(Image.m_Height - 1, (2 * Y + 1) * Image.m_Height / 128))
										      .a > 0.05f ?
								      255 :
								      0;
		m_aMasks[Type].Build(aMask.data(), 64, 64);
		m_aTextures[Type] = Graphics()->LoadTextureRawMove(Image, 0, aPath);
	}
}

void CQmDecorativeProjectiles::OnShutdown()
{
	for(auto &Texture : m_aTextures)
		Graphics()->UnloadTexture(&Texture);
	m_ResourcesAttempted = false;
}

void CQmDecorativeProjectiles::OnReset()
{
	for(auto &Projectile : m_aProjectiles)
		Projectile.m_Active = false;
	m_aRemoteLastThrow.fill(0);
	m_LastThrow = 0;
}

bool CQmDecorativeProjectiles::Spawn(int Type, int Owner, vec2 Origin, vec2 Direction)
{
	if(Type < 0 || Type >= QmDecorativeThrow::COUNT || !QmDecorativeThrow::ValidGeometry(Origin, Direction))
		return false;
	EnsureResources();
	if(!m_aTextures[Type].IsValid() || m_aMasks[Type].NumRects() == 0)
		return false;
	auto *pSlot = &m_aProjectiles[0];
	for(auto &Projectile : m_aProjectiles)
	{
		if(!Projectile.m_Active)
		{
			pSlot = &Projectile;
			break;
		}
		if(Projectile.m_LifeTime < pSlot->m_LifeTime)
			pSlot = &Projectile;
	}
	pSlot->Init(Origin, normalize(Direction) * 900.0f + vec2(0.0f, -200.0f), Type, 0.75f, Owner, g_Config.m_QmEmoticonProjectileDuration);
	pSlot->m_StopOnCollision = true;
	const auto Solid = [this](int X, int Y) { return Collision()->CheckPoint(X * 32.0f + 16.0f, Y * 32.0f + 16.0f); };
	if(!pSlot->PlaceOutside(m_aMasks[Type], Solid))
	{
		pSlot->m_Active = false;
		return false;
	}
	return true;
}

void CQmDecorativeProjectiles::Throw(int Type)
{
	if(!g_Config.m_QmDecorativeThrows || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter)
		return;
	const int Owner = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(Owner < 0 || Owner >= MAX_CLIENTS)
		return;
	const int64_t Now = time_get();
	if(m_LastThrow != 0 && Now - m_LastThrow < time_freq())
		return;
	const vec2 Aim = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	const vec2 Direction = length(Aim) > 0.0001f ? normalize(Aim) : vec2(1.0f, 0.0f);
	const vec2 Origin = GameClient()->m_aClients[Owner].m_RenderPos - vec2(0.0f, 20.0f);
	if(Spawn(Type, Owner, Origin, Direction))
	{
		m_LastThrow = Now;
		GameClient()->m_QmClient.SendQmDecorativeThrow(Type, Owner, Origin, Direction);
	}
}

void CQmDecorativeProjectiles::Draw(int Type, vec2 Position, float Size, float Alpha, float Angle)
{
	if(!m_aTextures[Type].IsValid())
		return;
	Graphics()->TextureSet(m_aTextures[Type]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadsSetRotation(Angle);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	IGraphics::CQuadItem Quad(Position.x, Position.y, Size, Size);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CQmDecorativeProjectiles::RenderSelector(vec2 Center, int Selected, float Scale, float Alpha)
{
	EnsureResources();
	const char *apLabels[] = {Localize("Grass"), Localize("Tomato"), Localize("Egg")};
	for(int Type = 0; Type < QmDecorativeThrow::COUNT; ++Type)
	{
		const vec2 Position = Center + direction(2.0f * pi * Type / QmDecorativeThrow::COUNT) * (125.0f * Scale);
		Draw(Type, Position, (Type == Selected ? 100.0f : 80.0f) * Scale, Alpha);
		CUIRect Label = {Position.x - 50.0f, Position.y + 42.0f * Scale, 100.0f, 18.0f};
		SLabelProperties Properties;
		Properties.m_MaxWidth = Label.w;
		Properties.m_EllipsisAtEnd = true;
		Properties.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
		Ui()->DoLabel(&Label, apLabels[Type], 12.0f, TEXTALIGN_MC, Properties);
	}
}

void CQmDecorativeProjectiles::OnRender()
{
	SQmRealtimeMessage Message;
	while(GameClient()->m_QmClient.PopQmDecorativeThrow(Message))
	{
		const int Id = Message.m_PlayerId;
		if(Client()->State() != IClient::STATE_ONLINE || !Message.m_HasDecorativeThrow ||
			Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_aClients[Id].m_Active)
			continue;
		char aServer[NETADDR_MAXSTRSIZE];
		net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
		const auto &Player = GameClient()->m_aClients[Id];
		if(!QmDecorativeThrow::ShouldShowRemote(g_Config.m_QmDecorativeThrows, g_Config.m_ClShowEmotes,
			   g_Config.m_QmShowOtherLaunchEmotes, Player.m_EmoticonIgnore,
			   NormalizeQmServerAddress(Message.m_ThrowServerAddress.c_str()).c_str(), NormalizeQmServerAddress(aServer).c_str(),
			   Message.m_ThrowPlayerName.c_str(), Player.m_aName, Message.m_ThrowOrigin, Player.m_RenderPos))
			continue;
		const int64_t Now = time_get();
		if(m_aRemoteLastThrow[Id] != 0 && Now - m_aRemoteLastThrow[Id] < time_freq())
			continue;
		if(Spawn(Message.m_ThrowType, Id, Message.m_ThrowOrigin, Message.m_ThrowDirection))
			m_aRemoteLastThrow[Id] = Now;
	}
	if(!g_Config.m_QmDecorativeThrows || Client()->State() != IClient::STATE_ONLINE)
	{
		OnReset();
		return;
	}
	if(std::none_of(m_aProjectiles.begin(), m_aProjectiles.end(), [](const auto &Projectile) { return Projectile.m_Active; }))
		return;
	QmEmoticon::SPlayerBox aBoxes[MAX_CLIENTS];
	int NumBoxes = 0;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		if(GameClient()->m_Snap.m_aCharacters[Id].m_Active)
			aBoxes[NumBoxes++] = {Id, GameClient()->m_aClients[Id].m_RenderPos, CCharacterCore::PhysicalSize() * 0.5f};
	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	float Width, Height;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
	const vec2 Center = GameClient()->m_Camera.m_Center;
	Graphics()->MapScreen(Center.x - Width * 0.5f, Center.y - Height * 0.5f, Center.x + Width * 0.5f, Center.y + Height * 0.5f);
	const auto Solid = [this](int X, int Y) { return Collision()->CheckPoint(X * 32.0f + 16.0f, Y * 32.0f + 16.0f); };
	for(auto &Projectile : m_aProjectiles)
	{
		if(!Projectile.m_Active)
			continue;
		const int Owner = Projectile.m_OwnerClientId;
		if(Owner < 0 || Owner >= MAX_CLIENTS || !GameClient()->m_aClients[Owner].m_Active ||
			(Owner != GameClient()->m_aLocalIds[0] && Owner != GameClient()->m_aLocalIds[1] &&
				(!g_Config.m_ClShowEmotes || !g_Config.m_QmShowOtherLaunchEmotes || GameClient()->m_aClients[Owner].m_EmoticonIgnore)))
		{
			Projectile.m_Active = false;
			continue;
		}
		Projectile.Update(std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f), m_aMasks[Projectile.m_Emoticon], Solid, aBoxes, NumBoxes, &GameClient()->m_Teams);
		if(Projectile.m_Impacted)
		{
			GameClient()->m_Effects.HammerHit(Projectile.m_Pos, 0.55f, 0.0f);
			Projectile.m_Impacted = false;
		}
		if(Projectile.m_Active)
			Draw(Projectile.m_Emoticon, Projectile.m_Pos, Projectile.Size(), Projectile.FadeAlpha() * GameClient()->m_Players.PlayerRenderAlpha(Owner), Projectile.m_Angle);
	}
	Graphics()->MapScreen(X0, Y0, X1, Y1);
}
