/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "infomessages.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/streamer_word_filter.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/localization.h>

#include <cstdint>
#include <new>
#include <stdexcept>

static constexpr float ROW_HEIGHT = 46.0f;
// 击杀消息使用 1800 逻辑高度，是普通 HUD 坐标的六倍。
static constexpr float FONT_SIZE = ui_token::hud::font::CAPTION * 6.0f;
static constexpr float RACE_FLAG_SIZE = 52.0f;
static constexpr int NUM_FLAG_QUADS = 4;

template<typename TOperation>
static bool TryInfoMessageOperation(TOperation &&Operation)
{
	try
	{
		Operation();
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

static void RenderInfoMessageText(ITextRender &TextRender, const STextContainerIndex &Index, ColorRGBA Color, float &X, float Y)
{
	X -= TextRender.GetBoundingBoxTextContainer(Index).m_W;
	// 绘制时可能补传文字缓冲。失败时保留容器，下一次绘制会重试。
	TryInfoMessageOperation([&] {
		TextRender.RenderTextContainer(Index, Color, TextRender.DefaultTextOutlineColor(), X, Y + (ROW_HEIGHT - FONT_SIZE) / 2.0f);
	});
}

void CInfoMessages::OnWindowResize()
{
	for(auto &InfoMsg : m_aInfoMsgs)
	{
		DeleteTextContainers(InfoMsg);
	}
}

void CInfoMessages::OnReset()
{
	m_InfoMsgCurrent = 0;
	for(auto &InfoMsg : m_aInfoMsgs)
	{
		ResetMessage(InfoMsg);
	}
}

void CInfoMessages::DeleteTextContainers(CInfoMsg &InfoMsg)
{
	STextContainerIndex *apIndices[] = {&InfoMsg.m_VictimTextContainerIndex, &InfoMsg.m_KillerTextContainerIndex, &InfoMsg.m_DiffTextContainerIndex, &InfoMsg.m_TimeTextContainerIndex};
	for(STextContainerIndex *pIndex : apIndices)
	{
		if(!TryInfoMessageOperation([&] { TextRender()->DeleteTextContainer(*pIndex); }))
			TextRender()->ReleaseTemporaryTextContainer(*pIndex);
	}
}

void CInfoMessages::ResetMessage(CInfoMsg &InfoMsg)
{
	InfoMsg.m_Tick = -1;
	std::fill(std::begin(InfoMsg.m_apVictimManagedTeeRenderInfos), std::end(InfoMsg.m_apVictimManagedTeeRenderInfos), nullptr);
	InfoMsg.m_pKillerManagedTeeRenderInfo = nullptr;
	DeleteTextContainers(InfoMsg);
}

void CInfoMessages::OnInit()
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	const bool SpritesPrepared = TryInfoMessageOperation([&] {
		m_SpriteQuadContainerIndex = Graphics()->CreateQuadContainer(false);

		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 0.f, 0.f, 28.f, 56.f);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 0.f, 0.f, 28.f, 56.f);

		Graphics()->QuadsSetSubset(1, 0, 0, 1);
		Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 0.f, 0.f, 28.f, 56.f);
		Graphics()->QuadsSetSubset(1, 0, 0, 1);
		Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 0.f, 0.f, 28.f, 56.f);

		for(int i = 0; i < NUM_WEAPONS; ++i)
		{
			float ScaleX, ScaleY;
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteBody, ScaleX, ScaleY);
			Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 96.f * ScaleX, 96.f * ScaleY);
		}

		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		m_QuadOffsetRaceFlag = Graphics()->QuadContainerAddSprite(m_SpriteQuadContainerIndex, 0.0f, 0.0f, RACE_FLAG_SIZE, RACE_FLAG_SIZE);
		if(m_QuadOffsetRaceFlag == NUM_FLAG_QUADS + NUM_WEAPONS)
			Graphics()->QuadContainerUpload(m_SpriteQuadContainerIndex);
	});

	if(!SpritesPrepared || m_QuadOffsetRaceFlag != NUM_FLAG_QUADS + NUM_WEAPONS)
	{
		// 旗帜和武器依赖固定的偏移，不能使用只构建了一部分的容器。
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DeleteQuadContainer(m_SpriteQuadContainerIndex);
		m_QuadOffsetRaceFlag = -1;
	}
}

CInfoMessages::CInfoMsg CInfoMessages::CreateInfoMsg(EType Type)
{
	CInfoMsg InfoMsg;
	InfoMsg.m_Type = Type;
	InfoMsg.m_Tick = Client()->GameTick(g_Config.m_ClDummy);

	for(int i = 0; i < MAX_KILLMSG_TEAM_MEMBERS; i++)
	{
		InfoMsg.m_aVictimIds[i] = -1;
		InfoMsg.m_apVictimManagedTeeRenderInfos[i] = nullptr;
	}
	InfoMsg.m_VictimDDTeam = 0;
	InfoMsg.m_aVictimName[0] = '\0';
	InfoMsg.m_VictimTextContainerIndex.Reset();

	InfoMsg.m_KillerId = -1;
	InfoMsg.m_aKillerName[0] = '\0';
	InfoMsg.m_KillerTextContainerIndex.Reset();
	InfoMsg.m_pKillerManagedTeeRenderInfo = nullptr;

	InfoMsg.m_Weapon = -1;
	InfoMsg.m_ModeSpecial = 0;
	InfoMsg.m_FlagCarrierBlue = -1;
	InfoMsg.m_TeamSize = 0;

	InfoMsg.m_TimeMs = 0;
	InfoMsg.m_Diff = 0;
	InfoMsg.m_aTimeText[0] = '\0';
	InfoMsg.m_aDiffText[0] = '\0';
	InfoMsg.m_TimeTextContainerIndex.Reset();
	InfoMsg.m_DiffTextContainerIndex.Reset();
	InfoMsg.m_RecordPersonal = false;
	return InfoMsg;
}

void CInfoMessages::AddInfoMsg(const CInfoMsg &InfoMsg)
{
	dbg_assert(InfoMsg.m_TeamSize >= 0 && InfoMsg.m_TeamSize <= MAX_KILLMSG_TEAM_MEMBERS, "Info message team size invalid");
	dbg_assert(InfoMsg.m_KillerId < 0 || InfoMsg.m_pKillerManagedTeeRenderInfo != nullptr, "Info message killer invalid");
	for(int i = 0; i < InfoMsg.m_TeamSize; i++)
	{
		dbg_assert(InfoMsg.m_aVictimIds[i] >= 0 && InfoMsg.m_apVictimManagedTeeRenderInfos[i] != nullptr, "Info message victim invalid");
	}

	const float Height = 1.5f * 400.0f * 3.0f;
	const float Width = Height * Graphics()->ScreenAspect();

	struct SRestoreScreen
	{
		IGraphics &m_Graphics;
		CScreenRect m_Screen;
		~SRestoreScreen() { m_Graphics.MapScreen(m_Screen); }
	};
	const SRestoreScreen RestoreScreen{*Graphics(), Graphics()->GetScreen()};
	Graphics()->MapScreen(0, 0, Width, Height);

	m_InfoMsgCurrent = (m_InfoMsgCurrent + 1) % MAX_INFOMSGS;
	DeleteTextContainers(m_aInfoMsgs[m_InfoMsgCurrent]);
	m_aInfoMsgs[m_InfoMsgCurrent] = InfoMsg;
	CreateTextContainersIfNotCreated(m_aInfoMsgs[m_InfoMsgCurrent]);
}

void CInfoMessages::CreateTextContainersIfNotCreated(CInfoMsg &InfoMsg)
{
	const auto CreateText = [&](STextContainerIndex &Index, const char *pText, ColorRGBA Color) {
		CTextCursor Cursor;
		Cursor.m_FontSize = FONT_SIZE;
		TextRender()->TextColor(Color);
		if(!TryInfoMessageOperation([&] { TextRender()->CreateTextContainer(Index, &Cursor, pText); }))
			TextRender()->ReleaseTemporaryTextContainer(Index);
	};

	const auto &&NameColor = [&](int ClientId) -> ColorRGBA {
		unsigned Color;
		if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		{
			Color = g_Config.m_ClKillMessageHighlightColor;
		}
		else
		{
			Color = g_Config.m_ClKillMessageNormalColor;
		}
		return color_cast<ColorRGBA>(ColorHSLA(Color));
	};

	if(!InfoMsg.m_VictimTextContainerIndex.Valid() && InfoMsg.m_aVictimName[0] != '\0')
		CreateText(InfoMsg.m_VictimTextContainerIndex, InfoMsg.m_aVictimName, NameColor(InfoMsg.m_aVictimIds[0]));

	if(!InfoMsg.m_KillerTextContainerIndex.Valid() && InfoMsg.m_aKillerName[0] != '\0')
		CreateText(InfoMsg.m_KillerTextContainerIndex, InfoMsg.m_aKillerName, NameColor(InfoMsg.m_KillerId));

	if(!InfoMsg.m_DiffTextContainerIndex.Valid() && InfoMsg.m_aDiffText[0] != '\0')
	{
		ColorRGBA Color = TextRender()->DefaultTextColor();
		if(InfoMsg.m_Diff > 0)
			Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
		else if(InfoMsg.m_Diff < 0)
			Color = ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f);

		CreateText(InfoMsg.m_DiffTextContainerIndex, InfoMsg.m_aDiffText, Color);
	}

	if(!InfoMsg.m_TimeTextContainerIndex.Valid() && InfoMsg.m_aTimeText[0] != '\0')
		CreateText(InfoMsg.m_TimeTextContainerIndex, InfoMsg.m_aTimeText, TextRender()->DefaultTextColor());

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CInfoMessages::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	TryInfoMessageOperation([&] {
		switch(MsgType)
		{
		case NETMSGTYPE_SV_KILLMSGTEAM:
			OnTeamKillMessage(static_cast<CNetMsg_Sv_KillMsgTeam *>(pRawMsg));
			break;
		case NETMSGTYPE_SV_KILLMSG:
			OnKillMessage(static_cast<CNetMsg_Sv_KillMsg *>(pRawMsg));
			break;
		case NETMSGTYPE_SV_RACEFINISH:
			OnRaceFinishMessage(static_cast<CNetMsg_Sv_RaceFinish *>(pRawMsg));
			break;
		}
	});
}

void CInfoMessages::OnTeamKillMessage(const CNetMsg_Sv_KillMsgTeam *pMsg)
{
	std::vector<std::pair<int, int>> vStrongWeakSorted;
	vStrongWeakSorted.reserve(MAX_CLIENTS);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(GameClient()->m_aClients[ClientId].m_Active &&
			GameClient()->m_Teams.Team(ClientId) == pMsg->m_Team)
		{
			CCharacter *pChr = GameClient()->m_GameWorld.GetCharacterById(ClientId);
			vStrongWeakSorted.emplace_back(ClientId, pMsg->m_First == ClientId ? MAX_CLIENTS : (pChr ? pChr->GetStrongWeakId() : 0));
		}
	}
	std::stable_sort(vStrongWeakSorted.begin(), vStrongWeakSorted.end(), [](auto &Left, auto &Right) { return Left.second > Right.second; });

	CInfoMsg Kill = CreateInfoMsg(TYPE_KILL);
	Kill.m_TeamSize = minimum<int>(vStrongWeakSorted.size(), MAX_KILLMSG_TEAM_MEMBERS);

	Kill.m_VictimDDTeam = pMsg->m_Team;
	for(int i = 0; i < Kill.m_TeamSize; i++)
	{
		const int VictimId = vStrongWeakSorted[i].first;
		Kill.m_aVictimIds[i] = VictimId;
		Kill.m_apVictimManagedTeeRenderInfos[i] = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[VictimId]);
	}
	str_format(Kill.m_aVictimName, sizeof(Kill.m_aVictimName), Localize("Team %d"), pMsg->m_Team);

	AddInfoMsg(Kill);
}

void CInfoMessages::OnKillMessage(const CNetMsg_Sv_KillMsg *pMsg)
{
	CInfoMsg Kill = CreateInfoMsg(TYPE_KILL);

	if(GameClient()->m_aClients[pMsg->m_Victim].m_Active)
	{
		Kill.m_TeamSize = 1;
		Kill.m_aVictimIds[0] = pMsg->m_Victim;
		Kill.m_VictimDDTeam = GameClient()->m_Teams.Team(Kill.m_aVictimIds[0]);
		GameClient()->FormatStreamerName(Kill.m_aVictimIds[0], Kill.m_aVictimName, sizeof(Kill.m_aVictimName));
		Kill.m_apVictimManagedTeeRenderInfos[0] = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[Kill.m_aVictimIds[0]]);
	}

	if(GameClient()->m_aClients[pMsg->m_Killer].m_Active)
	{
		Kill.m_KillerId = pMsg->m_Killer;
		GameClient()->FormatStreamerName(Kill.m_KillerId, Kill.m_aKillerName, sizeof(Kill.m_aKillerName));
		Kill.m_pKillerManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[Kill.m_KillerId]);
	}

	Kill.m_Weapon = pMsg->m_Weapon;
	Kill.m_ModeSpecial = pMsg->m_ModeSpecial;
	Kill.m_FlagCarrierBlue = GameClient()->m_Snap.m_pGameDataObj ? GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue : -1;

	if(Kill.m_TeamSize == 0 && Kill.m_KillerId == -1 && Kill.m_Weapon < 0)
	{
		return; // message would be empty
	}

	AddInfoMsg(Kill);
}

void CInfoMessages::OnRaceFinishMessage(const CNetMsg_Sv_RaceFinish *pMsg)
{
	if(!GameClient()->m_aClients[pMsg->m_ClientId].m_Active)
	{
		return;
	}

	CInfoMsg Finish = CreateInfoMsg(TYPE_FINISH);

	Finish.m_TeamSize = 1;
	Finish.m_aVictimIds[0] = pMsg->m_ClientId;
	Finish.m_VictimDDTeam = GameClient()->m_Teams.Team(Finish.m_aVictimIds[0]);
	GameClient()->FormatStreamerName(Finish.m_aVictimIds[0], Finish.m_aVictimName, sizeof(Finish.m_aVictimName));
	Finish.m_apVictimManagedTeeRenderInfos[0] = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[pMsg->m_ClientId]);

	Finish.m_TimeMs = pMsg->m_Time;
	Finish.m_Diff = pMsg->m_Diff;
	Finish.m_RecordPersonal = pMsg->m_RecordPersonal || pMsg->m_RecordServer;
	if(Finish.m_Diff)
	{
		char aBuf[64];
		str_time_float(absolute(static_cast<int64_t>(Finish.m_Diff)) / 1000.0f, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
		str_format(Finish.m_aDiffText, sizeof(Finish.m_aDiffText), "(%c%s)", Finish.m_Diff < 0 ? '-' : '+', aBuf);
	}
	str_time_float(pMsg->m_Time / 1000.0f, TIME_HOURS_CENTISECS, Finish.m_aTimeText, sizeof(Finish.m_aTimeText));

	AddInfoMsg(Finish);
}

void CInfoMessages::RenderKillMsg(const CInfoMsg &InfoMsg, float x, float y)
{
	ColorRGBA TextColor;
	if(InfoMsg.m_VictimDDTeam)
		TextColor = GameClient()->GetDDTeamColor(InfoMsg.m_VictimDDTeam, 0.75f);
	else
		TextColor = TextRender()->DefaultTextColor();

	const auto *pGameInfo = GameClient()->m_Snap.m_pGameInfoObj;
	const bool CanRenderFlags = m_SpriteQuadContainerIndex >= 0 && pGameInfo && (pGameInfo->m_GameFlags & GAMEFLAG_FLAGS);

	// render victim name
	if(InfoMsg.m_VictimTextContainerIndex.Valid())
		RenderInfoMessageText(*TextRender(), InfoMsg.m_VictimTextContainerIndex, TextColor, x, y);

	// render victim flag
	x -= 24.0f;
	if(CanRenderFlags && (InfoMsg.m_ModeSpecial & 1))
	{
		int QuadOffset;
		if(InfoMsg.m_aVictimIds[0] == InfoMsg.m_FlagCarrierBlue)
		{
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
			QuadOffset = 0;
		}
		else
		{
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
			QuadOffset = 1;
		}
		Graphics()->RenderQuadContainerAsSprite(m_SpriteQuadContainerIndex, QuadOffset, x, y - 16);
	}

	// render victim tees
	for(int j = (InfoMsg.m_TeamSize - 1); j >= 0; j--)
	{
		RenderTools()->RenderIdleTeeAtCenter(&InfoMsg.m_apVictimManagedTeeRenderInfos[j]->TeeRenderInfo(), vec2(x, y + ROW_HEIGHT / 2.0f), EMOTE_PAIN, vec2(-1, 0));
		x -= 44.0f;
	}

	// render weapon
	x -= 32.0f;
	if(m_SpriteQuadContainerIndex >= 0 && InfoMsg.m_Weapon >= 0)
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeapons[InfoMsg.m_Weapon]);
		Graphics()->RenderQuadContainerAsSprite(m_SpriteQuadContainerIndex, NUM_FLAG_QUADS + InfoMsg.m_Weapon, x, y + 28);
	}
	x -= 52.0f;

	// render killer (only if different from victim)
	if(InfoMsg.m_aVictimIds[0] != InfoMsg.m_KillerId)
	{
		// render killer flag
		if(CanRenderFlags && (InfoMsg.m_ModeSpecial & 2))
		{
			int QuadOffset;
			if(InfoMsg.m_KillerId == InfoMsg.m_FlagCarrierBlue)
			{
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
				QuadOffset = 2;
			}
			else
			{
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
				QuadOffset = 3;
			}
			Graphics()->RenderQuadContainerAsSprite(m_SpriteQuadContainerIndex, QuadOffset, x - 56, y - 16);
		}

		// render killer tee
		x -= 24.0f;
		if(InfoMsg.m_pKillerManagedTeeRenderInfo != nullptr)
		{
			RenderTools()->RenderIdleTeeAtCenter(&InfoMsg.m_pKillerManagedTeeRenderInfo->TeeRenderInfo(), vec2(x, y + ROW_HEIGHT / 2.0f), EMOTE_ANGRY, vec2(1, 0));
		}
		x -= 32.0f;

		// render killer name
		if(InfoMsg.m_KillerTextContainerIndex.Valid())
			RenderInfoMessageText(*TextRender(), InfoMsg.m_KillerTextContainerIndex, TextColor, x, y);
	}
}

void CInfoMessages::RenderFinishMsg(const CInfoMsg &InfoMsg, float x, float y)
{
	// render time diff
	if(InfoMsg.m_DiffTextContainerIndex.Valid())
		RenderInfoMessageText(*TextRender(), InfoMsg.m_DiffTextContainerIndex, TextRender()->DefaultTextColor(), x, y);

	// render time
	if(InfoMsg.m_TimeTextContainerIndex.Valid())
		RenderInfoMessageText(*TextRender(), InfoMsg.m_TimeTextContainerIndex, TextRender()->DefaultTextColor(), x, y);

	// render flag
	x -= RACE_FLAG_SIZE;
	if(m_SpriteQuadContainerIndex >= 0)
	{
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_RACEFLAG].m_Id);
		Graphics()->RenderQuadContainerAsSprite(m_SpriteQuadContainerIndex, m_QuadOffsetRaceFlag, x, y);
	}

	// render victim name
	if(InfoMsg.m_VictimTextContainerIndex.Valid())
	{
		ColorRGBA TextColor;
		if(InfoMsg.m_VictimDDTeam)
			TextColor = GameClient()->GetDDTeamColor(InfoMsg.m_VictimDDTeam, 0.75f);
		else
			TextColor = TextRender()->DefaultTextColor();
		RenderInfoMessageText(*TextRender(), InfoMsg.m_VictimTextContainerIndex, TextColor, x, y);
	}

	// render victim tee
	x -= 24.0f;
	const int Emote = InfoMsg.m_RecordPersonal ? EMOTE_HAPPY : EMOTE_NORMAL;
	RenderTools()->RenderIdleTeeAtCenter(&InfoMsg.m_apVictimManagedTeeRenderInfos[0]->TeeRenderInfo(), vec2(x, y + ROW_HEIGHT / 2.0f), Emote, vec2(-1, 0));
}

void CInfoMessages::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(GetQmFocusModeDecisions().m_HideInfoMessages)
		return;

	const float Height = 1.5f * 400.0f * 3.0f;
	const float Width = Height * Graphics()->ScreenAspect();

	Graphics()->MapScreen(0, 0, Width, Height);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	int Showfps = g_Config.m_ClShowPerf;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		Showfps = 0;
#endif
	const float StartX = Width - 10.0f;
	const bool HasTopRightTextInfo = (g_Config.m_ClShowPerf || g_Config.m_ClShowPacketLoss) && Client()->State() != IClient::STATE_DEMOPLAYBACK;
	float StartY = 30.0f + (Showfps ? 100.0f : 0.0f) + (HasTopRightTextInfo ? 100.0f : 0.0f);

	float y = StartY;
	for(int i = 1; i <= MAX_INFOMSGS; i++)
	{
		CInfoMsg &InfoMsg = m_aInfoMsgs[(m_InfoMsgCurrent + i) % MAX_INFOMSGS];
		if(InfoMsg.m_Tick == -1)
		{
			continue;
		}
		if(InfoMessageExpired(Client()->GameTick(g_Config.m_ClDummy), InfoMsg.m_Tick, Client()->GameTickSpeed()))
		{
			ResetMessage(InfoMsg);
			continue;
		}
		if(InfoMsg.m_Type == EType::TYPE_FINISH && GameClient()->m_FinishSummary.SuppressesFinishMessage(InfoMsg.m_aVictimIds[0], InfoMsg.m_TimeMs))
			continue;

		bool FilteredName = QmStreamerWordFilter::Filter(InfoMsg.m_aVictimName, g_Config.m_QmStreamerFilterWords != 0);
		FilteredName |= QmStreamerWordFilter::Filter(InfoMsg.m_aKillerName, g_Config.m_QmStreamerFilterWords != 0);
		if(FilteredName)
			DeleteTextContainers(InfoMsg);
		CreateTextContainersIfNotCreated(InfoMsg);

		if(InfoMsg.m_Type == EType::TYPE_KILL && g_Config.m_ClShowKillMessages)
		{
			RenderKillMsg(InfoMsg, StartX, y);
			y += ROW_HEIGHT;
		}
		else if(InfoMsg.m_Type == EType::TYPE_FINISH && g_Config.m_ClShowFinishMessages)
		{
			RenderFinishMsg(InfoMsg, StartX, y);
			y += ROW_HEIGHT;
		}
	}
}
