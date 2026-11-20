/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <base/dbg.h>
#include <base/mem.h>

#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmListEntryAnimation.h>
#include <game/client/QmUi/QmLayout.h>
#include <game/client/QmUi/QmLegacy.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/motd.h>
#include <game/client/components/player_points.h>
#include <game/client/components/qmclient/axiom_scores.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/scoreboard_footer.h>
#include <game/client/components/qmclient/scoreboard_layout.h>
#include <game/client/components/qmclient/scoreboard_scroll.h>
#include <game/client/components/qmclient/scoreboard_skin.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	// 记分板未打开时每帧预热的 Axiom 分数条数：够让打开时立刻有数据，又不会无谓刷接口。
	constexpr int AXIOM_SCOREBOARD_PREFETCH_BUDGET = 12;
	static constexpr const char *TIME_WIDTH_TEXT = "00:00:00";
	static constexpr ColorRGBA TEAM_RED_TITLE_COLOR(0.975f, 0.17f, 0.17f);
	static constexpr ColorRGBA TEAM_BLUE_TITLE_COLOR(0.17f, 0.46f, 0.975f);

	uint64_t ScoreboardPresentationNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_extra_scoreboard_presentation"));
		return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
	}

	SUiSpringConfig ScoreboardPresentationSpring()
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 460.0f;
		Spring.m_Damping = 42.0f;
		Spring.m_RestEpsilon = 0.008f;
		Spring.m_RestVelocity = 0.08f;
		return Spring;
	}

	SUiSpringConfig ScoreboardContentSpring(bool Opening)
	{
		SUiSpringConfig Spring = ScoreboardPresentationSpring();
		if(!Opening)
		{
			constexpr float CloseTimeScale = 0.30f;
			Spring.m_Stiffness /= CloseTimeScale * CloseTimeScale;
			Spring.m_Damping /= CloseTimeScale;
			Spring.m_RestVelocity /= CloseTimeScale;
		}
		return Spring;
	}

	// ---- 计分板面板的弹性入场（0 → 过冲 → 100%）----
	// 二阶欠阻尼阶跃响应的峰值超调 Mp = exp(-πζ / sqrt(1-ζ²))，反解出阻尼比
	// ζ = -ln(Mp) / sqrt(π² + ln²(Mp))。这样设置页里的「6%」就真的是 106% 峰值，
	// 不用靠试刚度/阻尼凑近似；ζ=1 时无过冲，正好对应滑杆 0 档。
	// 面板同时驱动立即模式布局的字号重排，过冲在视觉上比同数值的位移更明显，
	// 所以上限压到 15%、阻尼比下限 0.45，避免出现「胀大再缩回」的呼吸感。
	float ScoreboardPopDampingRatio(float Overshoot)
	{
		const float Peak = std::clamp(Overshoot, 0.0f, 0.15f);
		if(Peak <= 0.0001f)
			return 1.0f;
		const float LogPeak = -std::log(Peak);
		return std::clamp(LogPeak / std::sqrt(9.8696044f + LogPeak * LogPeak), 0.45f, 1.0f);
	}

	SUiSpringConfig ScoreboardPopSpring()
	{
		const int Feel = std::clamp(g_Config.m_QmScoreboardPopFeel, 0, 2);
		float Response = 0.30f;
		float Overshoot = std::clamp(g_Config.m_QmScoreboardOvershoot, 0, 15) / 100.0f;
		switch(Feel)
		{
		case 0: // 柔和：慢一点，过冲减半
			Response = 0.38f;
			Overshoot *= 0.5f;
			break;
		case 2: // 弹性：快一点，过冲加半
			Response = 0.26f;
			Overshoot *= 1.5f;
			break;
		default:
			break;
		}
		SUiSpringConfig Spring = SUiSpringConfig::FromResponse(Response, ScoreboardPopDampingRatio(Overshoot));
		Spring.m_RestEpsilon = 0.002f;
		Spring.m_RestVelocity = 0.02f;
		return Spring;
	}

	void StartScoreboardPop(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, float StartScale)
	{
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::SCALE, StartScale);
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = EUiAnimProperty::SCALE;
		Request.m_Target = 1.0f;
		Request.m_Transition.m_Driver = EUiAnimDriver::SPRING;
		Request.m_Transition.m_Spring = ScoreboardPopSpring();
		Request.m_Transition.m_Priority = 3;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
		AnimRuntime.RequestAnimation(Request);
	}

	// 面板还很小的时候先把内容压住：DDNet 是立即模式布局，缩放会连带重排字号，
	// 让文字在 0.1 倍尺寸下以满透明度出现会读作一团噪点。门槛压在面板还很小的一段
	// （0.32→0.70）：门控只是「别在小尺寸下显字」，不能宽到把行的错峰淡入整段吃掉。
	constexpr float SCOREBOARD_CONTENT_SCALE_GATE_FROM = 0.32f;
	constexpr float SCOREBOARD_CONTENT_SCALE_GATE_TO = 0.70f;
	float ScoreboardContentScaleGate(float PanelScale)
	{
		const float T = std::clamp((PanelScale - SCOREBOARD_CONTENT_SCALE_GATE_FROM) /
						   (SCOREBOARD_CONTENT_SCALE_GATE_TO - SCOREBOARD_CONTENT_SCALE_GATE_FROM),
			0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	// ---- 内部文本依次出场 ----
	// Index 是出场序号：0 = 列首行，1..N = 后续玩家行，尾部区块用 FOOTER 常量。
	// 错峰窗口按本列可见行数压缩到 0.16s（慢速波浪 0.30s），保证最后一行与面板
	// 弹跳同时收尾。原来用固定的 14ms/行，行一多窗口就比弹跳长出两三百毫秒，
	// 后半列在面板停稳之后才补出来，读作「后半段瞬移」。
	constexpr int SCOREBOARD_TEXT_ENTRY_FOOTER_INDEX = 1024;
	constexpr float SCOREBOARD_TEXT_ENTRY_WINDOW_SEC = 0.16f;
	constexpr float SCOREBOARD_TEXT_ENTRY_WINDOW_SLOW_SEC = 0.30f;
	float ScoreboardTextEntryFade(float ElapsedSec, int Index, int MotionLevel, int VisibleRows = 0)
	{
		const int Style = std::clamp(g_Config.m_QmScoreboardTextEntry, 0, 2);
		if(Style == 0 || MotionLevel <= 0 || ElapsedSec < 0.0f)
			return 1.0f;
		if(MotionLevel == 1)
		{
			// 减弱动效：整列一起淡入，不铺错峰。
			return std::clamp(ElapsedSec / ui_token::motion::LIST_ENTRY_FADE_REDUCED.m_DurationSec, 0.0f, 1.0f);
		}
		float Step = ui_token::motion::LIST_ENTRY_STAGGER_STEP_SEC;
		if(VisibleRows > 1)
		{
			const float Window = Style == 2 ? SCOREBOARD_TEXT_ENTRY_WINDOW_SLOW_SEC : SCOREBOARD_TEXT_ENTRY_WINDOW_SEC;
			Step = minimum(Step, Window / (float)(VisibleRows - 1));
		}
		// 尾部区块的序号远大于行数：夹到行数上，正好拖在最后一行之后一步。
		const int EffIndex = VisibleRows > 0 ? minimum(Index, VisibleRows) : Index;
		const float Local = ElapsedSec - (float)EffIndex * Step;
		const float Fade = std::clamp(Local / ui_token::motion::LIST_ENTRY_FADE.m_DurationSec, 0.0f, 1.0f);
		return Fade * Fade * (3.0f - 2.0f * Fade);
	}

	// 区块级 alpha 乘算：m_AnimContentAlpha 是所有文本与控件的公共系数，
	// 在调用某个区块绘制前临时压低、返回时还原，就不用逐个绘制点改一遍。
	struct SScoreboardEntryAlphaScope
	{
		float &m_Alpha;
		float m_Saved;
		SScoreboardEntryAlphaScope(float &Alpha, float Factor) : m_Alpha(Alpha), m_Saved(Alpha)
		{
			m_Alpha *= std::clamp(Factor, 0.0f, 1.0f);
		}
		~SScoreboardEntryAlphaScope() { m_Alpha = m_Saved; }
	};

	SUiSpringConfig ScoreboardElementSpring(bool Visible)
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 520.0f;
		Spring.m_Damping = 44.0f;
		Spring.m_RestEpsilon = 0.005f;
		Spring.m_RestVelocity = 0.08f;
		if(!Visible)
		{
			constexpr float ExitTimeScale = 0.38f;
			Spring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
			Spring.m_Damping /= ExitTimeScale;
			Spring.m_RestVelocity /= ExitTimeScale;
		}
		return Spring;
	}

	CUIRect ScaleRectAroundCenter(const CUIRect &Rect, float Scale)
	{
		Scale = std::max(0.01f, Scale);
		const float CenterX = Rect.x + Rect.w * 0.5f;
		const float CenterY = Rect.y + Rect.h * 0.5f;
		const float Width = Rect.w * Scale;
		const float Height = Rect.h * Scale;
		return {CenterX - Width * 0.5f, CenterY - Height * 0.5f, Width, Height};
	}

	uint64_t SoundMuteButtonNodeKey(int Index)
	{
		static constexpr uint64_t NodeBase = 0x73636F72655F6D00ULL; // "score_m"
		return NodeBase + static_cast<uint64_t>(Index);
	}

	uint64_t SoundMuteInfoNodeKey()
	{
		static constexpr uint64_t NodeKey = 0x73636F72655F694EULL; // "score_iN"
		return NodeKey;
	}

	struct SSoundMuteButtonDef
	{
		int CConfig::*m_pConfig;
		const char *m_pIcon;
		const char *m_pTitle;
		const char *m_pDescription;
	};

	static const SSoundMuteButtonDef gs_aSoundMuteButtonDefs[] = {
		{&CConfig::m_ClSndMuteWeapon, FontIcons::FONT_ICON_CIRCLE, "武器音效", "屏蔽主要武器发射与命中相关声音。"},
		{&CConfig::m_ClSndMuteWeaponSwitch, FontIcons::FONT_ICON_ARROWS_LEFT_RIGHT, "武器切换音效", "屏蔽武器切换及相关切换提示音。"},
		{&CConfig::m_ClSndMuteWeaponNoAmmo, FontIcons::FONT_ICON_TRIANGLE_EXCLAMATION, "无弹药提示音", "屏蔽武器无弹药时的提示音。"},
		{&CConfig::m_ClSndMuteHook, FontIcons::FONT_ICON_ARROWS_ROTATE, "钩子音效", "屏蔽钩子发射、收回等相关声音。"},
		{&CConfig::m_ClSndMuteMovement, FontIcons::FONT_ICON_ARROWS_UP_DOWN, "移动音效", "屏蔽行走与跳跃等移动相关声音。"},
		{&CConfig::m_ClSndMutePlayerState, FontIcons::FONT_ICON_HEART_CRACK, "玩家状态音效", "屏蔽玩家状态变化相关声音。"},
		{&CConfig::m_ClSndMutePickup, FontIcons::FONT_ICON_SQUARE_PLUS, "拾取音效", "屏蔽道具与武器拾取相关声音。"},
		{&CConfig::m_ClSndMuteFlag, FontIcons::FONT_ICON_FLAG_CHECKERED, "旗帜音效", "屏蔽 CTF 旗帜事件相关声音。"},
		{&CConfig::m_ClSndMuteMapSound, FontIcons::FONT_ICON_MAP, "地图音效", "屏蔽地图环境与脚本触发音效。"},
	};
	static_assert((sizeof(gs_aSoundMuteButtonDefs) / sizeof(gs_aSoundMuteButtonDefs[0])) == 9, "Sound mute button count mismatch");
	constexpr float CLIENT_BRAND_LABEL_GAP = 3.0f;

	ColorRGBA ClientBrandScoreboardColor(EClientBrand Brand, float Alpha)
	{
		switch(Brand)
		{
		case EClientBrand::QM:
			return ColorRGBA(0.38f, 0.89f, 1.0f, Alpha);
		case EClientBrand::ARG:
			return ColorRGBA(1.0f, 0.66f, 0.28f, Alpha);
		case EClientBrand::NONE:
			return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
		}
		return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
	}

	float ScoreboardUiAlpha(float AlphaScale)
	{
		return ResolveConfiguredInterfaceSurface(AlphaScale).a;
	}

	ColorRGBA ScoreboardUiColorSurface(float AlphaScale, float ColorScale = ui_token::panel::SURFACE_COLOR_SCALE)
	{
		return ResolveConfiguredInterfaceSurface(AlphaScale, ColorScale);
	}

	ColorRGBA ScoreboardWithUiAlpha(ColorRGBA Color, float AlphaScale)
	{
		Color.a = ScoreboardUiAlpha(AlphaScale);
		return Color;
	}

	ColorRGBA ScoreboardDecorationColor(ColorRGBA Color, float AlphaScale = 1.0f)
	{
		Color.a = std::clamp(Color.a * AlphaScale * (g_Config.m_QmUiOpacity / 100.0f), 0.0f, 1.0f);
		return Color;
	}

	ColorRGBA ScoreboardGlassSurface(float AlphaScale)
	{
		return ScoreboardUiColorSurface(AlphaScale);
	}

	constexpr float SCOREBOARD_PANEL_RADIUS = ui_token::radius::CARD;
	// 底栏各条（媒体、旁观者、按键提示）的左右文字内边距：宽度计算与绘制共用同一个值，
	// 两侧留白才对称。宽度另加 4~6px 舍入余量——TextWidth 与实际排版有零点几像素差，
	// 没有余量时右端字符会被 MaxWidth 挤到贴边。
	constexpr float SCOREBOARD_FOOTER_PAD_X = 10.0f;
	// 媒体条上的圆形封面（参考灵动岛）：条高 25、封面 19 居中，左侧只留 3px——
	// 圆是紧贴条端的「实物」，按文字内边距留白反而会把它推到中间。
	constexpr float SCOREBOARD_MEDIA_BAR_HEIGHT = 25.0f;
	constexpr float SCOREBOARD_MEDIA_COVER_SIZE = 19.0f;
	constexpr float SCOREBOARD_MEDIA_COVER_PAD_X = 3.0f;
	constexpr float SCOREBOARD_MEDIA_COVER_GAP = 6.0f;

}

CScoreboard::CScoreboard()
{
	CScoreboard::OnReset();
}

void CScoreboard::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CScoreboard::ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);
	pSelf->SetActive(pResult->GetInteger(0) != 0);
}

void CScoreboard::SetActive(bool Active)
{
	GameClient()->m_Spectator.OnRelease();
	GameClient()->m_Emoticon.OnRelease();

	m_Active = Active;

	if(!IsActive() && m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}
}

bool CScoreboard::MobilePanelContains(vec2 NdcPos) const
{
	const CUIRect &Panel = m_LastPanelNdc;
	return Panel.w > 0.0f && Panel.h > 0.0f && Panel.Inside(NdcPos);
}

bool CScoreboard::MobileRowTapActive() const
{
	// 手机端点整行是“跟随”，玩家卡走行尾的「…」；桌面保持整行点开玩家卡。
	return QmUiMobilePlatform();
}

void CScoreboard::SpectatePlayer(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const bool IsSpectating = GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == ClientId;
	if(IsSpectating)
	{
		GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
		Console()->ExecuteLine("say /spec");
		return;
	}
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(ClientId);
		return;
	}
	// 非旁观时由服务器把客户端切进旁观：/spec "名字"。
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(pName[0] == '\0')
		return;
	char aCommand[2 * MAX_NAME_LENGTH + 32];
	str_copy(aCommand, "say /spec \"");
	char *pDst = aCommand + str_length(aCommand);
	str_escape(&pDst, pName, aCommand + sizeof(aCommand));
	str_append(aCommand, "\"");
	Console()->ExecuteLine(aCommand);
}

void CScoreboard::UpdateQmAxiomScoreMode()
{
	m_QmAxiomScoreModeFrameValid = true;
	m_QmAxiomScoreModeFrame = EQmAxiomMode::NONE;

	if(!GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity())
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	CServerInfo ServerInfo;
	Client()->GetServerInfo(&ServerInfo);
	// 社区分类优先（Axiom 的 Gores / Other 两分类正好对应两个积分模式），
	// 分类拿不到时再看服务器名是否带 AXRace。
	m_QmAxiomScoreModeFrame = QmResolveAxiomModeFromServerContext({ServerInfo.m_aCommunityType, ServerInfo.m_aName});
}

bool CScoreboard::HasQmAxiomScoreMode()
{
	if(!m_QmAxiomScoreModeFrameValid)
		UpdateQmAxiomScoreMode();
	return m_QmAxiomScoreModeFrame != EQmAxiomMode::NONE;
}

void CScoreboard::QmAxiomScorePoints(const char *pPlayerName, bool Visible, char *pBuffer, int BufferSize) const
{
	pBuffer[0] = '\0';
	if(!Visible)
		return;

	const SQmAxiomLookupResult Lookup = GameClient()->m_QmAxiomScores.GetLookup(pPlayerName);
	switch(Lookup.m_Status)
	{
	case EQmAxiomScoreStatus::READY:
		str_format(pBuffer, BufferSize, "%lld", (long long)Lookup.m_Points);
		break;
	case EQmAxiomScoreStatus::FETCHING:
	case EQmAxiomScoreStatus::NOT_REQUESTED:
		str_copy(pBuffer, "...", BufferSize);
		break;
	case EQmAxiomScoreStatus::HTTP_ERROR:
	case EQmAxiomScoreStatus::API_ERROR:
	case EQmAxiomScoreStatus::INVALID_RESPONSE:
		str_copy(pBuffer, "?", BufferSize);
		break;
	case EQmAxiomScoreStatus::NOT_FOUND:
	case EQmAxiomScoreStatus::AMBIGUOUS:
		str_copy(pBuffer, "-", BufferSize);
		break;
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	if(!pSelf->IsActive() ||
		pSelf->GameClient()->m_Menus.IsActive() ||
		pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	pSelf->m_MouseUnlocked = !pSelf->m_MouseUnlocked;

	// 锁定光标时计分板不再接收点击，已打开的玩家菜单必须同步关闭，
	// 否则弹层会在无光标状态下继续占着输入。
	if(!pSelf->m_MouseUnlocked)
		pSelf->Ui()->ClosePopupMenus();

	vec2 OldMousePos = pSelf->Ui()->MousePos();

	if(pSelf->m_LastMousePos == std::nullopt)
	{
		pSelf->SetUiMousePos(pSelf->Ui()->Screen()->Center());
	}
	else
	{
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
	}

	// save pos, so moving the mouse in esc menu doesn't change the position
	pSelf->m_LastMousePos = OldMousePos;
}

void CScoreboard::OnConsoleInit()
{
	Console()->Register("+scoreboard", "", CFGFLAG_CLIENT, ConKeyScoreboard, this, "Show scoreboard");
	Console()->Register("toggle_scoreboard_cursor", "", CFGFLAG_CLIENT, ConToggleScoreboardCursor, this, "Toggle scoreboard cursor");
}

void CScoreboard::OnInit()
{
	m_DeadTeeTexture = Graphics()->LoadTexture("deadtee.png", IStorage::TYPE_ALL);
}

void CScoreboard::OnReset()
{
	m_Active = false;
	m_ServerRecord = -1.0f;
	m_Visibility = 0.0f;
	m_OpenTime = 0.0f;
	m_AnimContentAlpha = 0.0f;
	m_PresentationInitialized = false;
	m_ActiveStartTime = 0;
	m_WasActive = false;
	m_AutoActiveStable = false;
	m_AutoOnFrames = 0;
	m_AutoOffFrames = 0;
	m_TextEntryElapsedSec = -1.0f;
	m_TextEntryStartTime = 0;
	m_MouseUnlocked = false;
	m_RenderInteractions = false;
	m_aCachedTeamModes = {};
	m_ScrollRegion.Reset();
	m_LastMousePos = std::nullopt;
	m_SoundMuteButtonAnimState.Reset();
	m_SoundMuteInfoAnimState.Reset();
}

void CScoreboard::OnRelease()
{
	m_MediaControls.Cancel(*Ui());
	m_Active = false;
	m_Visibility = 0.0f;
	m_OpenTime = 0.0f;
	m_AnimContentAlpha = 0.0f;
	m_PresentationInitialized = false;
	m_ActiveStartTime = 0;
	m_WasActive = false;
	m_AutoActiveStable = false;
	m_AutoOnFrames = 0;
	m_AutoOffFrames = 0;
	m_TextEntryElapsedSec = -1.0f;
	m_TextEntryStartTime = 0;
	m_RenderInteractions = false;
	m_aCachedTeamModes = {};
	m_ScrollRegion.Reset();
	m_SoundMuteButtonAnimState.Reset();
	m_SoundMuteInfoAnimState.Reset();

	if(m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}
}

void CScoreboard::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_RECORD)
	{
		CNetMsg_Sv_Record *pMsg = static_cast<CNetMsg_Sv_Record *>(pRawMsg);
		m_ServerRecord = pMsg->m_ServerTimeBest / 100.0f;
	}
	else if(MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_RecordLegacy *pMsg = static_cast<CNetMsg_Sv_RecordLegacy *>(pRawMsg);
		m_ServerRecord = pMsg->m_ServerTimeBest / 100.0f;
	}
}

bool CScoreboard::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CScoreboard::OnInput(const IInput::CEvent &Event)
{
	if(m_MouseUnlocked && Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		// 手机上的返回键等价于关掉面板；桌面解锁后 ESC 仍然只是把光标锁回去。
		if(QmUiMobilePlatform() && IsActive())
		{
			SetActive(false);
			return true;
		}
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
		return true;
	}
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	return QmScoreboardUiInput(*Ui(), IsActive() && m_MouseUnlocked, Event, CBinds::IsReservedShortcutChord(CBinds::GetModifierMask(Input())));
}

void CScoreboard::RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize, bool Subpixel)
{
	const bool IsMapTitle = Team == TEAM_GAME && !GameClient()->IsTeamPlay();
	if(IsMapTitle && m_MouseUnlocked && GameClient()->m_aMapDescription[0] != '\0')
	{
		const int ButtonResult = Ui()->DoButtonLogic(&m_MapTitleButtonId, 0, &TitleLabel, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(ButtonResult != 0)
		{
			m_MapTitlePopupContext.m_pScoreboard = this;

			m_MapTitlePopupContext.m_FontSize = 12.0f;
			const float MaxWidth = 300.0f;
			const float Margin = 5.0f;
			const char *pDescription = GameClient()->m_aMapDescription;
			const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription) + 0.5f), MaxWidth);
			float TextHeight = 0.0f;
			STextSizeProperties TextSizeProps{};
			TextSizeProps.m_pHeight = &TextHeight;
			TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription, -1, TextWidth, 0, TextSizeProps);

			Ui()->DoPopupMenu(&m_MapTitlePopupContext, Ui()->MouseX(), Ui()->MouseY(), TextWidth + Margin * 2, TextHeight + Margin * 2, &m_MapTitlePopupContext, CMapTitlePopupContext::Render);
		}
		if(Ui()->HotItem() == &m_MapTitleButtonId)
		{
			TitleLabel.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::SMALL_MENU);
		}
	}

	// 地图名始终单行：宽度不够先等比缩字号，缩到下限再省略。
	// 计分板缩放动画会逐帧改变可用宽度，DISALLOW_NEWLINE 只拦显式换行，
	// 词级软折行仍会触发，所以这里同时走 STOP_AT_END，彻底不产生第二行。
	// 连续字号（与灵动岛状态区同一机制）：字体光栅化仍是整像素，但绘制尺寸
	// 使用原始浮点字号，弹簧末端不会在相邻字号档之间来回跳；面板缩放期间
	// 同时关闭整像素对齐，避免慢速收尾把亚像素位移攒成逐像素跳动。
	const CUiScopedFloatFontSize FloatFontSize(TextRender());
	const CUiScopedSubpixelText SubpixelText(TextRender(), Subpixel);
	// 拟合宽度留 1 个 UI 单位余量：字体光栅化按整像素取整，贴着边界算时
	// 实际宽度偶尔会多出不到一个字，STOP_AT_END 就会切掉最后一笔。
	constexpr float SCOREBOARD_TITLE_FIT_MARGIN = 1.0f;
	const float FitWidth = maximum(0.0f, TitleLabel.w - SCOREBOARD_TITLE_FIT_MARGIN);
	const SScoreboardTitleFit Fit = ResolveScoreboardTitleFit(TitleFontSize, 10.0f,
		TextRender()->TextWidth(TitleFontSize, pTitle), FitWidth);
	const SScoreboardTitleLabelStyle Style = ResolveScoreboardTitleLabelStyle(Fit);
	SLabelProperties Props;
	Props.m_MaxWidth = TitleLabel.w;
	Props.m_DisallowNewline = Style.m_DisallowNewline;
	Props.m_StopAtEnd = Style.m_StopAtEnd;
	Props.m_EllipsisAtEnd = Style.m_EllipsisAtEnd;
	// 字号已由 ResolveScoreboardTitleFit 收敛，关闭 DoLabel 的二次缩放：
	// 否则“下限字号仍放不下”时会在测量路径里再缩一档，省略号位置继续跳。
	Props.m_EnableWidthCheck = false;
	Props.m_MinimumFontSize = Fit.m_FontSize;
	Ui()->DoLabel(&TitleLabel, pTitle, Fit.m_FontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
}

void CScoreboard::RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize)
{
	// map best
	char aScore[128] = "";
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(TimeScore || Race7)
	{
		if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET)
		{
			Ui()->RenderTime(ScoreLabel,
				TitleFontSize,
				GameClient()->m_MapBestTimeSeconds,
				GameClient()->m_MapBestTimeSeconds == FinishTime::NOT_FINISHED_MILLIS,
				GameClient()->m_MapBestTimeMillis,
				GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis,
				ScoreboardTitleTimeColor(m_AnimContentAlpha));
			return;
		}
	}
	else if(GameClient()->IsTeamPlay()) // normal score
	{
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if(pGameDataObj)
		{
			str_format(aScore, sizeof(aScore), "%d", Team == TEAM_RED ? pGameDataObj->m_TeamscoreRed : pGameDataObj->m_TeamscoreBlue);
		}
	}
	else
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW &&
			GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId]->m_Score);
		}
		else if(GameClient()->m_Snap.m_pLocalInfo)
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Score);
		}
	}

	const float ScoreTextWidth = aScore[0] != '\0' ? TextRender()->TextWidth(TitleFontSize, aScore, -1, -1.0f, 0) : 0.0f;
	if(ScoreTextWidth != 0.0f)
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle, bool Subpixel)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE || Team == TEAM_GAME, "Team invalid");

	// 标题与 rank1 时间共用同一字号：手机不再单独放大，否则时间会压过地图名。
	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, TIME_WIDTH_TEXT);

	TitleBar.VMargin(10.0f, &TitleBar);
	CUIRect TitleLabel;
	CUIRect ScoreLabel;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle TitleRowStyle;
		TitleRowStyle.m_Axis = EUiAxis::ROW;
		TitleRowStyle.m_Gap = 5.0f;
		TitleRowStyle.m_AlignItems = EUiAlign::STRETCH;
		TitleRowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
		std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
		vTitleChildren.assign(2, SUiLayoutChild{});
		if(Team == TEAM_RED || Team == TEAM_GAME)
		{
			vTitleChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Px(ScoreTextWidth);
			LayoutEngine.ComputeChildren(TitleRowStyle, CUiV2LegacyAdapter::FromCUIRect(TitleBar), vTitleChildren);
			TitleLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			ScoreLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}
		else
		{
			vTitleChildren[0].m_Style.m_Width = SUiLength::Px(ScoreTextWidth);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TitleRowStyle, CUiV2LegacyAdapter::FromCUIRect(TitleBar), vTitleChildren);
			ScoreLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			TitleLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}
	}

	RenderTitle(TitleLabel, Team, pTitle, TitleFontSize, Subpixel);
	RenderTitleScore(ScoreLabel, Team, TitleFontSize);
}

void CScoreboard::RenderServerPlayerCount(CUIRect Rect, const char *pText)
{
	const float ContentAlpha = m_AnimContentAlpha;
	if(ContentAlpha <= 0.001f || pText[0] == '\0')
		return;

	constexpr float FontSize = 10.0f;
	constexpr float PaddingX = 5.0f;

	Rect.VMargin(6.0f, &Rect);
	Rect.HMargin(4.0f, &Rect);
	if(Rect.w <= 1.0f || Rect.h <= 1.0f)
		return;

	Rect.Draw(ScoreboardUiColorSurface(ContentAlpha, 0.18f), IGraphics::CORNER_ALL, 5.0f);

	const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	CUIRect Content = Rect;
	Content.HMargin(PaddingX, &Content);

	const float IconSize = FontSize + 2.0f;
	CUIRect IconRect;
	Content.VSplitLeft(IconSize, &IconRect, &Content);
	Content.VSplitLeft(3.0f, nullptr, &Content);
	IconRect.VMargin((IconRect.h - IconSize) * 0.5f, &IconRect);

	Ui()->DrawQmIcon(IconRect, EQmIcon::USERS, FontIcons::FONT_ICON_USERS, TextColor);
	Ui()->DoLabel(&Content, pText, FontSize, TEXTALIGN_ML);
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	const float ContentAlpha = m_AnimContentAlpha;
	Goals.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, SCOREBOARD_PANEL_RADIUS);
	Goals.VMargin(5.0f, &Goals);

	const float FontSize = 10.0f;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	char aBuf[64];

	if(pGameInfoObj->m_ScoreLimit)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_ML);
	}

	if(pGameInfoObj->m_TimeLimit)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MC);
	}

	if(pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MR);
	}
}

CUIRect CScoreboard::RenderSpectators(CUIRect Area, float &CursorY, float Gap)
{
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);

	const bool IsTeamPlay = GameClient()->IsTeamPlay();
	const float AvailableHeight = QmScoreboardFooterRemainingHeight(Area, CursorY);
	if(AvailableHeight <= 0.0f)
		return {};

	const float CornerRadius = SCOREBOARD_PANEL_RADIUS;

	CTextCursor Cursor;
	Cursor.SetPosition(vec2(0.0f, 0.0f));
	Cursor.m_FontSize = 11.0f;
	Cursor.m_LineWidth = maximum(0.0f, Area.w - 2.0f * SCOREBOARD_FOOTER_PAD_X);
	Cursor.m_MaxLines = maximum(1, round_truncate(AvailableHeight / Cursor.m_FontSize));

	int RemainingSpectators = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) != TEAM_SPECTATORS)
			continue;
		++RemainingSpectators;
	}

	auto RenderList = [&](CTextCursor &ListCursor) {
		TextRender()->TextEx(&ListCursor, Localize("Spectators"));

		if(RemainingSpectators > 0)
		{
			TextRender()->TextEx(&ListCursor, ": ");
		}

		int Remaining = RemainingSpectators;
		bool CommaNeeded = false;
		for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
		{
			if(!pInfo || QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) != TEAM_SPECTATORS)
				continue;

			if(CommaNeeded)
			{
				TextRender()->TextEx(&ListCursor, ", ");
			}

			if(ListCursor.m_LineCount == ListCursor.m_MaxLines && Remaining >= 2)
			{
				// This is less expensive than checking with a separate invisible
				// text cursor though we waste some space at the end of the line.
				char aRemaining[64];
				str_format(aRemaining, sizeof(aRemaining), Localize("%d others…", "Spectators"), Remaining);
				TextRender()->TextEx(&ListCursor, aRemaining);
				break;
			}

			const int ClientId = pInfo->m_ClientId;
			const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
			char aNameBuf[MAX_NAME_LENGTH];
			char aClanBuf[MAX_CLAN_LENGTH];
			GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
			GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));

			if(g_Config.m_ClShowIds && !HideIdentity)
			{
				char aClientId[16];
				GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::NO_INDENT);
				TextRender()->TextEx(&ListCursor, aClientId);
			}

			{
				const char *pClanName = aClanBuf;
				if(pClanName[0] != '\0')
				{
					if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
					{
						TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ContentAlpha));
					}
					else
					{
						TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f, ContentAlpha));
					}

					TextRender()->TextEx(&ListCursor, pClanName);
					TextRender()->TextEx(&ListCursor, " ");

					TextRender()->TextColor(BaseTextColor);
				}
			}

			if(GameClient()->m_aClients[ClientId].m_AuthLevel)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)).WithMultipliedAlpha(ContentAlpha));
			}

			TextRender()->TextEx(&ListCursor, aNameBuf);
			TextRender()->TextColor(BaseTextColor);

			CommaNeeded = true;
			--Remaining;
		}
	};

	// 测量与绘制共用文字路径，保留名称颜色和溢出提示；背景只包住实际行数与最长行宽。
	CTextCursor MeasureCursor = Cursor;
	MeasureCursor.m_Flags = 0;
	RenderList(MeasureCursor);
	const float PanelHeight = QmScoreboardSpectatorPanelHeight(AvailableHeight, MeasureCursor.m_LineCount, Cursor.m_MaxLines, Cursor.m_FontSize, 10.0f);
	// 单行时宽度收到文字实测宽度；折行说明内容本来就撑满，保持整宽。
	const float PanelWidth = MeasureCursor.m_LineCount <= 1 ?
					 minimum(Area.w, MeasureCursor.m_LongestLineWidth + 2.0f * SCOREBOARD_FOOTER_PAD_X + 4.0f) :
					 Area.w;
	const CUIRect SpectatorPanel = QmScoreboardFooterBlock(Area, CursorY, PanelWidth, PanelHeight, Gap);
	if(SpectatorPanel.w <= 0.0f)
		return {};

	// 垂直留白与 QmScoreboardSpectatorPanelHeight 的 padding 参数对应（上下各 5）；
	// 水平单独用 PAD_X，行宽测量时也是同一个值。
	CUIRect SpectatorList = SpectatorPanel;
	SpectatorList.y += 5.0f;
	SpectatorList.h = maximum(0.0f, SpectatorList.h - 10.0f);
	SpectatorList.x += SCOREBOARD_FOOTER_PAD_X;
	SpectatorList.w = maximum(0.0f, SpectatorList.w - 2.0f * SCOREBOARD_FOOTER_PAD_X);
	Cursor.SetPosition(SpectatorList.TopLeft());
	Cursor.m_LineWidth = SpectatorList.w;

	SpectatorPanel.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, CornerRadius);
	RenderList(Cursor);
	return SpectatorPanel;
}

// QmClient: 底栏上方的整宽媒体信息条。远程删除了三个 SMTC 播放控制按钮，
// 只保留“正在播放”文本；控制交给系统媒体面板与音乐来源自身的快捷键。
void CScoreboard::RenderFooter(CUIRect Footer)
{
	const float ContentAlpha = m_AnimContentAlpha;

	bool HasSpectators = false;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo)
			continue;
		if(QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, GameClient()->IsTeamPlay()) == TEAM_SPECTATORS)
		{
			HasSpectators = true;
			break;
		}
	}

	char aMediaBuf[256] = "";
	CSystemMediaControls::SState MediaState;
	if(g_Config.m_QmSmtcEnable && GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState))
	{
		if(MediaState.m_aTitle[0] != '\0' && MediaState.m_aArtist[0] != '\0')
			str_format(aMediaBuf, sizeof(aMediaBuf), "%s - %s", MediaState.m_aTitle, MediaState.m_aArtist);
		else if(MediaState.m_aTitle[0] != '\0')
			str_copy(aMediaBuf, MediaState.m_aTitle, sizeof(aMediaBuf));
		else if(MediaState.m_aArtist[0] != '\0')
			str_copy(aMediaBuf, MediaState.m_aArtist, sizeof(aMediaBuf));
	}

	const bool ShowCursorHint = !m_MouseUnlocked;
	// 底栏从上往下堆叠：每块宽度按实测文字收缩并居中，空块不占位，
	// 所以内容少时整排会向面板贴齐，不会在中间留出一段空白。
	constexpr float FooterGap = 5.0f;
	float CursorY = Footer.y;
	if(aMediaBuf[0] != '\0')
	{
		// 封面纹理由媒体组件统一预烘成圆形透明贴图（和 HUD 灵动岛同一张），
		// 计分板这里只负责摆位，不重新裁圆、也不新传纹理。
		const bool HasCover = MediaState.m_AlbumArtCircular.IsValid() && !MediaState.m_AlbumArtCircular.IsNullTexture();
		// 唱片式自转：只随播放状态推进，暂停定格在当前角度；动效等级 0 不转。
		// time_get() 先折成 double 再转 float 秒，避免大整数在 float 里丢低位。
		const float CoverSpin = m_MediaCoverSpin.Advance(MediaState.m_Playing, g_Config.m_QmUiMotionLevel > 0,
			static_cast<float>(static_cast<double>(time_get()) / static_cast<double>(time_freq())));
		// 文字宽度之外再加 4px 舍入余量：TextWidth 与实际排版有零点几像素差，
		// 没有余量时右端字符会被 MaxWidth 挤到贴边。
		const float MediaWidth = SCOREBOARD_MEDIA_COVER_PAD_X + SCOREBOARD_MEDIA_COVER_SIZE + SCOREBOARD_MEDIA_COVER_GAP +
					 TextRender()->TextWidth(11.0f, aMediaBuf) + SCOREBOARD_FOOTER_PAD_X + 4.0f;
		const CUIRect MediaBar = QmScoreboardFooterBlock(Footer, CursorY, MediaWidth, SCOREBOARD_MEDIA_BAR_HEIGHT, FooterGap);
		if(MediaBar.w > 0.0f)
		{
			MediaBar.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, SCOREBOARD_PANEL_RADIUS);
			const CUIRect Cover = {MediaBar.x + SCOREBOARD_MEDIA_COVER_PAD_X,
				MediaBar.y + (MediaBar.h - SCOREBOARD_MEDIA_COVER_SIZE) * 0.5f,
				SCOREBOARD_MEDIA_COVER_SIZE,
				SCOREBOARD_MEDIA_COVER_SIZE};
			if(HasCover)
			{
				Graphics()->TextureSet(MediaState.m_AlbumArtCircular);
				Graphics()->QuadsBegin();
				// 圆形贴图绕自己的中心转：轮廓不变，只有图案在转，所以不需要额外裁切。
				Graphics()->QuadsSetRotation(CoverSpin);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, ContentAlpha);
				const IGraphics::CQuadItem CoverQuad(Cover.x, Cover.y, Cover.w, Cover.h);
				Graphics()->QuadsDrawTL(&CoverQuad, 1);
				Graphics()->QuadsEnd();
				Graphics()->QuadsSetRotation(0.0f);
				Graphics()->TextureClear();
			}
			else
			{
				// 没有封面时留一颗同尺寸的占位圆 + 音符，保证有/无封面时文字起点一致。
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
				Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * ContentAlpha));
				Graphics()->DrawCircle(Cover.x + Cover.w * 0.5f, Cover.y + Cover.h * 0.5f, Cover.w * 0.5f, 24);
				Graphics()->QuadsEnd();
				const CUIRect Icon = {Cover.x + Cover.w * 0.26f, Cover.y + Cover.h * 0.26f, Cover.w * 0.48f, Cover.h * 0.48f};
				Ui()->DrawQmIcon(Icon, EQmIcon::MUSIC, FontIcons::FONT_ICON_MUSIC, ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f * ContentAlpha));
			}
			CUIRect Label = MediaBar;
			Label.x = Cover.x + Cover.w + SCOREBOARD_MEDIA_COVER_GAP;
			Label.w = maximum(0.0f, MediaBar.x + MediaBar.w - SCOREBOARD_FOOTER_PAD_X - Label.x);
			TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha));
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha));
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w;
			Props.m_EllipsisAtEnd = true;
			Props.m_MinimumFontSize = 11.0f;
			Ui()->DoLabel(&Label, aMediaBuf, 11.0f, TEXTALIGN_ML, Props);
		}
	}
	if(HasSpectators)
		RenderSpectators(QmScoreboardFooterAreaWithHintReserved(Footer, ShowCursorHint, 20.0f, FooterGap), CursorY, FooterGap);
	if(ShowCursorHint)
	{
		char aKey[64];
		GameClient()->m_Binds.GetKey("toggle_scoreboard_cursor", aKey, sizeof(aKey));
		char aHint[128];
		if(aKey[0] == '\0')
			str_format(aHint, sizeof(aHint), Localize("'%s' not bound."), "toggle_scoreboard_cursor");
		else
			str_format(aHint, sizeof(aHint), Localize("Press '%s' to show cursor."), aKey);
		const float HintWidth = TextRender()->TextWidth(11.0f, aHint) + 2.0f * SCOREBOARD_FOOTER_PAD_X + 6.0f;
		const CUIRect Hint = QmScoreboardFooterBlock(Footer, CursorY, HintWidth, 20.0f, 0.0f);
		if(Hint.w > 0.0f)
			RenderCursorHint(Hint, aHint);
	}
}

void CScoreboard::RenderCursorHint(CUIRect Hint, const char *pHint)
{
	const float ContentAlpha = m_AnimContentAlpha;
	Hint.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, SCOREBOARD_PANEL_RADIUS);
	CUIRect Label = Hint;
	Label.x += SCOREBOARD_FOOTER_PAD_X;
	Label.w = maximum(0.0f, Label.w - 2.0f * SCOREBOARD_FOOTER_PAD_X);
	TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha));
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha));
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Props.m_MinimumFontSize = 9.0f;
	Ui()->DoLabel(&Label, pHint, 11.0f, TEXTALIGN_MC, Props);
}

void CScoreboard::RenderSoundMuteBar(CUIRect ScoreboardRect)
{
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	auto &&RestoreTextColors = [&]() {
		TextRender()->TextColor(BaseTextColor);
		TextRender()->TextOutlineColor(BaseOutlineColor);
	};
	RestoreTextColors();

	static CButtonContainer s_aButtons[SOUND_MUTE_BUTTON_COUNT];
	std::array<CUIRect, SOUND_MUTE_BUTTON_COUNT> aAnimatedRects{};

	const int NumButtons = static_cast<int>(sizeof(gs_aSoundMuteButtonDefs) / sizeof(gs_aSoundMuteButtonDefs[0]));
	dbg_assert(NumButtons == SOUND_MUTE_BUTTON_COUNT, "Sound mute button state mismatch");

	const float ButtonSize = 28.0f;
	const float Gap = 4.0f;
	const float ColumnHeight = NumButtons * ButtonSize + (NumButtons - 1) * Gap;
	const float ColumnX = maximum(0.0f, ScoreboardRect.x - ButtonSize - 6.0f);
	const float ColumnY = ScoreboardRect.y + (ScoreboardRect.h - ColumnHeight) * 0.5f;
	const CUIRect *pScreen = Ui()->Screen();

	const float MouseX = Ui()->MouseX();
	const float MouseY = Ui()->MouseY();
	const float ActivationLeft = maximum(0.0f, ScoreboardRect.x - 70.0f);
	const bool InActivationZone = m_MouseUnlocked &&
				      MouseX >= ActivationLeft && MouseX <= ScoreboardRect.x &&
				      MouseY >= ScoreboardRect.y && MouseY <= ScoreboardRect.y + ScoreboardRect.h;

	int NearestIndex = -1;
	if(InActivationZone)
	{
		float BestDistanceSq = std::numeric_limits<float>::max();
		for(int i = 0; i < NumButtons; ++i)
		{
			const float CenterX = ColumnX + ButtonSize * 0.5f;
			const float CenterY = ColumnY + i * (ButtonSize + Gap) + ButtonSize * 0.5f;
			const float Dx = MouseX - CenterX;
			const float Dy = MouseY - CenterY;
			const float DistanceSq = Dx * Dx + Dy * Dy;
			if(DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				NearestIndex = i;
			}
		}
	}

	CUiV2AnimationRuntime *pAnimRuntime = nullptr;
	if(g_Config.m_QmUiMotionLevel > 0 && GameClient()->UiRuntimeV2()->Enabled())
		pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();

	if(pAnimRuntime != nullptr && !m_SoundMuteButtonAnimState.m_Initialized)
	{
		for(int i = 0; i < NumButtons; ++i)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::SCALE, 1.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, 18.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, 0.0f);
			m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = 0.0f;
			m_SoundMuteButtonAnimState.m_aTargetScale[i] = 1.0f;
			m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = 18.0f;
			m_SoundMuteButtonAnimState.m_aTargetReveal[i] = 0.0f;
		}
		m_SoundMuteButtonAnimState.m_Initialized = true;
	}

	if(pAnimRuntime != nullptr && !m_SoundMuteInfoAnimState.m_Initialized)
	{
		SetUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, 14.0f);
		m_SoundMuteInfoAnimState.m_TargetAlpha = 0.0f;
		m_SoundMuteInfoAnimState.m_TargetOffsetX = 14.0f;
		m_SoundMuteInfoAnimState.m_Initialized = true;
	}

	int HoveredNearestIndex = -1;
	const float ClipWidth = ScoreboardRect.x - pScreen->x;
	const bool ClipEnabled = ClipWidth > 0.0f;
	CUIRect ClipRect = *pScreen;
	if(ClipEnabled)
	{
		ClipRect.w = ClipWidth;
		Ui()->ClipEnable(&ClipRect);
	}
	for(int i = 0; i < NumButtons; ++i)
	{
		const bool IsNearest = InActivationZone && i == NearestIndex;
		const bool IsNeighbor = InActivationZone && (i == NearestIndex - 1 || i == NearestIndex + 1);
		const bool IsOuterNeighbor = InActivationZone && (i == NearestIndex - 2 || i == NearestIndex + 2);

		float TargetAlpha = 0.0f;
		float TargetScale = 1.0f;
		float TargetOffsetX = 18.0f;
		float TargetYOffset = 0.0f;

		if(IsNearest)
		{
			TargetAlpha = 1.0f;
			TargetScale = 1.0f;
			TargetOffsetX = 0.0f;
			TargetYOffset = -7.0f;
		}
		else if(IsNeighbor)
		{
			TargetAlpha = 0.65f;
			TargetScale = 1.0f;
			TargetOffsetX = 10.0f;
			TargetYOffset = -4.0f;
		}
		else if(IsOuterNeighbor)
		{
			TargetAlpha = 0.35f;
			TargetScale = 1.0f;
			TargetOffsetX = 16.0f;
			TargetYOffset = -1.0f;
		}

		float Alpha = TargetAlpha;
		float Scale = TargetScale;
		float OffsetX = TargetOffsetX;
		float YOffset = TargetYOffset;
		m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = TargetAlpha;
		m_SoundMuteButtonAnimState.m_aTargetScale[i] = TargetScale;
		m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = TargetOffsetX;
		m_SoundMuteButtonAnimState.m_aTargetReveal[i] = TargetYOffset;
		if(pAnimRuntime != nullptr)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			const SUiSpringConfig ElementSpring = ScoreboardElementSpring(TargetAlpha > 0.01f);
			Alpha = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, m_SoundMuteButtonAnimState.m_aTargetAlpha[i], ElementSpring, 2, 0.003f);
			Scale = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::SCALE, m_SoundMuteButtonAnimState.m_aTargetScale[i], ElementSpring, 2, 0.003f);
			OffsetX = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, m_SoundMuteButtonAnimState.m_aTargetOffsetX[i], ElementSpring, 2, 0.01f);
			YOffset = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, m_SoundMuteButtonAnimState.m_aTargetReveal[i], ElementSpring, 2, 0.01f);
		}

		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		if(Alpha <= 0.01f)
			continue;

		CUIRect Button = {ColumnX, ColumnY + i * (ButtonSize + Gap), ButtonSize, ButtonSize};
		Button.x += OffsetX;
		Button.y += YOffset;
		const float ScaledSize = ButtonSize * std::max(0.01f, Scale);
		Button.x += (ButtonSize - ScaledSize) * 0.5f;
		Button.y += (ButtonSize - ScaledSize) * 0.5f;
		Button.w = ScaledSize;
		Button.h = ScaledSize;

		if(Button.w <= 1.0f || Button.h <= 1.0f)
			continue;

		aAnimatedRects[i] = Button;

		const bool Active = g_Config.*gs_aSoundMuteButtonDefs[i].m_pConfig != 0;
		const bool Clickable = IsNearest && m_RenderInteractions;
		const float RenderAlpha = Alpha * ContentAlpha;
		const ColorRGBA ButtonColor = Active ?
						      ColorRGBA(1.0f, 0.32f, 0.32f, 0.95f * RenderAlpha) :
						      ColorRGBA(0.82f, 0.88f, 0.96f, 0.45f * RenderAlpha);
		if(Ui()->DoButton_FontIcon(&s_aButtons[i], gs_aSoundMuteButtonDefs[i].m_pIcon, Active, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Clickable, ButtonColor) && Clickable)
			g_Config.*gs_aSoundMuteButtonDefs[i].m_pConfig ^= 1;
		RestoreTextColors();

		if(Clickable && Ui()->HotItem() == &s_aButtons[i])
			HoveredNearestIndex = i;
	}
	if(ClipEnabled)
		Ui()->ClipDisable();

	const float InfoTargetAlpha = HoveredNearestIndex >= 0 ? 1.0f : 0.0f;
	const float InfoTargetOffsetX = HoveredNearestIndex >= 0 ? 0.0f : 14.0f;
	float InfoAlpha = InfoTargetAlpha;
	float InfoOffsetX = InfoTargetOffsetX;
	m_SoundMuteInfoAnimState.m_TargetAlpha = InfoTargetAlpha;
	m_SoundMuteInfoAnimState.m_TargetOffsetX = InfoTargetOffsetX;
	if(pAnimRuntime != nullptr)
	{
		const SUiSpringConfig InfoSpring = ScoreboardElementSpring(InfoTargetAlpha > 0.01f);
		InfoAlpha = ResolveUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, m_SoundMuteInfoAnimState.m_TargetAlpha, InfoSpring, 2, 0.003f);
		InfoOffsetX = ResolveUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, m_SoundMuteInfoAnimState.m_TargetOffsetX, InfoSpring, 2, 0.01f);
	}
	InfoAlpha = std::clamp(InfoAlpha, 0.0f, 1.0f);

	if(HoveredNearestIndex >= 0)
		m_SoundMuteInfoAnimState.m_HoveredButton = HoveredNearestIndex;
	else if(InfoAlpha <= 0.01f)
		m_SoundMuteInfoAnimState.m_HoveredButton = -1;

	const int InfoIndex = HoveredNearestIndex >= 0 ? HoveredNearestIndex : m_SoundMuteInfoAnimState.m_HoveredButton;
	if(InfoIndex < 0 || InfoAlpha <= 0.01f)
		return;

	CUIRect Anchor = aAnimatedRects[InfoIndex];
	if(Anchor.w <= 0.0f || Anchor.h <= 0.0f)
		Anchor = {ColumnX, ColumnY + InfoIndex * (ButtonSize + Gap), ButtonSize, ButtonSize};

	const bool Muted = g_Config.*gs_aSoundMuteButtonDefs[InfoIndex].m_pConfig != 0;
	char aTitle[256];
	str_format(aTitle, sizeof(aTitle), "%s (%s)", gs_aSoundMuteButtonDefs[InfoIndex].m_pTitle, Muted ? Localize("Muted") : Localize("Unmuted"));
	const char *pTitle = aTitle;
	const char *pDescription = gs_aSoundMuteButtonDefs[InfoIndex].m_pDescription;
	const float TitleFontSize = 10.0f;
	const float BodyFontSize = 9.0f;
	const float Padding = 6.0f;
	const float InnerGap = 2.0f;
	const float InfoTextWidth = maximum(TextRender()->TextWidth(TitleFontSize, pTitle), TextRender()->TextWidth(BodyFontSize, pDescription));
	const float InfoWidth = std::clamp(InfoTextWidth + Padding * 2.0f, 170.0f, 360.0f);
	const float InfoHeight = Padding * 2.0f + TitleFontSize + InnerGap + BodyFontSize + 2.0f;

	CUIRect InfoRect = {Anchor.x + Anchor.w + 8.0f, Anchor.y + (Anchor.h - InfoHeight) * 0.5f, InfoWidth, InfoHeight};
	InfoRect.x += InfoOffsetX;
	const float ScreenMargin = 5.0f;
	if(InfoRect.x + InfoRect.w + ScreenMargin > pScreen->w)
		InfoRect.x = Anchor.x - InfoRect.w - 8.0f;
	InfoRect.x = std::clamp(InfoRect.x, ScreenMargin, pScreen->w - InfoRect.w - ScreenMargin);
	InfoRect.y = std::clamp(InfoRect.y, ScreenMargin, pScreen->h - InfoRect.h - ScreenMargin);

	InfoRect.Draw(ScoreboardUiColorSurface(InfoAlpha * ContentAlpha, 0.18f), IGraphics::CORNER_ALL, 6.0f);

	CUIRect InfoContent = InfoRect;
	InfoContent.Margin(Padding, &InfoContent);
	CUIRect TitleRect, BodyRect;
	InfoContent.HSplitTop(TitleFontSize + 1.0f, &TitleRect, &BodyRect);
	BodyRect.HSplitTop(InnerGap, nullptr, &BodyRect);

	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha * InfoAlpha));
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha * InfoAlpha));

	SLabelProperties TitleProps;
	TitleProps.m_MaxWidth = TitleRect.w;
	TitleProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleRect, pTitle, TitleFontSize, TEXTALIGN_ML, TitleProps);

	SLabelProperties BodyProps;
	BodyProps.m_MaxWidth = BodyRect.w;
	BodyProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&BodyRect, pDescription, BodyFontSize, TEXTALIGN_ML, BodyProps);

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
}

void CScoreboard::UpdateTeamModeCache()
{
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> aTeamModes{};
	std::array<bool, NUM_DDRACE_TEAMS> aTeamHasPlayer{};
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(GameClient()->m_Snap.m_apPlayerInfos[ClientId] == nullptr)
			continue;

		const int DDTeam = GameClient()->m_Teams.Team(ClientId);
		if(DDTeam < TEAM_FLOCK || DDTeam >= NUM_DDRACE_TEAMS)
			continue;

		aTeamHasPlayer[DDTeam] = true;
		const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
		AccumulateQmScoreboardTeamModeState(aTeamModes[DDTeam], Character.m_HasExtendedDisplayInfo, Character.m_ExtendedData.m_Flags);
	}
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
		ApplyQmScoreboardTeamLockHint(aTeamModes[Team], GameClient()->TClientComponent().IsSoloSplitTeamLocked(Team));
	CacheAndRestoreQmScoreboardTeamModes(aTeamModes, aTeamHasPlayer, m_aCachedTeamModes);
}

void CScoreboard::BuildPlayerRowPlan(int Team, CScoreboardPlayerRowPlan &Plan)
{
	Plan.m_Count = 0;
	Plan.m_aTeamModes = {};
	Plan.m_aTeamHasPlayer = {};
	std::array<int, MAX_CLIENTS> aPreviousSourceDDTeam{};
	std::array<int, MAX_CLIENTS> aNextSourceDDTeam{};
	const bool IsTeamPlay = GameClient()->IsTeamPlay();
	auto &&IsInScoreboardTeam = [&](const CNetObj_PlayerInfo *pInfo) {
		if(pInfo == nullptr)
			return false;
		return QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) == Team;
	};

	int PreviousDDTeam = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		aPreviousSourceDDTeam[i] = PreviousDDTeam;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
		if(IsInScoreboardTeam(pInfo))
			PreviousDDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
	}

	int NextDDTeam = 0;
	for(int i = MAX_CLIENTS - 1; i >= 0; --i)
	{
		aNextSourceDDTeam[i] = NextDDTeam;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
		if(IsInScoreboardTeam(pInfo))
			NextDDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
	}

	for(int RenderDead = 0; RenderDead < 2; ++RenderDead)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!IsInScoreboardTeam(pInfo))
				continue;
			const bool IsDead = (Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD) != 0;
			if(IsDead != (RenderDead != 0))
				continue;

			CScoreboardPlayerRow &Row = Plan.m_aRows[Plan.m_Count++];
			Row.m_pInfo = pInfo;
			Row.m_DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			if(Row.m_DDTeam >= TEAM_FLOCK && Row.m_DDTeam < NUM_DDRACE_TEAMS)
			{
				Plan.m_aTeamHasPlayer[Row.m_DDTeam] = true;
				const auto &Character = GameClient()->m_Snap.m_aCharacters[pInfo->m_ClientId];
				AccumulateQmScoreboardTeamModeState(Plan.m_aTeamModes[Row.m_DDTeam], Character.m_HasExtendedDisplayInfo, Character.m_ExtendedData.m_Flags);
			}
			Row.m_PreviousSourceDDTeam = aPreviousSourceDDTeam[i];
			Row.m_NextSourceDDTeam = aNextSourceDDTeam[i];
			Row.m_Dead = IsDead;
		}
	}
	for(int DDTeam = TEAM_FLOCK; DDTeam < NUM_DDRACE_TEAMS; ++DDTeam)
		ApplyQmScoreboardTeamLockHint(Plan.m_aTeamModes[DDTeam], GameClient()->TClientComponent().IsSoloSplitTeamLocked(DDTeam));
	CacheAndRestoreQmScoreboardTeamModes(Plan.m_aTeamModes, Plan.m_aTeamHasPlayer, m_aCachedTeamModes);
}

void CScoreboard::RenderTeamModeIcons(float x, float y, float IconSize, const SQmScoreboardTeamModeState &State, float Alpha)
{
	if(!State.m_Known || State.m_Flags == 0)
		return;

	Graphics()->BlendNormal();
	auto &&RenderIcon = [&](bool Active, IGraphics::CTextureHandle Texture) {
		if(!Active)
			return;
		Graphics()->TextureSet(Texture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(x, y, IconSize, IconSize);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		x += IconSize + 1.5f;
	};

	RenderIcon(State.Practice(), GameClient()->m_HudSkin.m_SpriteHudPracticeMode);
	RenderIcon(State.Team0Mode(), GameClient()->m_HudSkin.m_SpriteHudTeam0Mode);
	RenderIcon(State.Locked(), GameClient()->m_HudSkin.m_SpriteHudLockMode);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, const CScoreboardPlayerRowPlan &Plan, CScoreboardRenderState &State, bool Scroll, int NumPlayersForSize)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const int NumPlayers = Scroll ? 16 : (NumPlayersForSize >= 0 ? NumPlayersForSize : CountEnd - CountStart);
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;
	const bool ShowPlaytime = g_Config.m_QmScoreboardPlaytime != 0;
	// 积分视图在 Axiom 服显示当前模式分数，游玩时间独立使用 DDStats。
	const bool AxiomScoreColumn = !ShowPlaytime && HasQmAxiomScoreMode();
	const bool ShowStatistics = ShowPlaytime || AxiomScoreColumn || g_Config.m_QmScoreboardPoints != 0;
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	const int FirstRow = std::clamp(CountStart, 0, Plan.m_Count);
	const int EndRow = std::clamp(CountEnd, FirstRow, Plan.m_Count);
	auto &&TeamHasModeIcons = [&](int DDTeam) {
		return DDTeam >= TEAM_FLOCK && DDTeam < NUM_DDRACE_TEAMS && Plan.m_aTeamModes[DDTeam].m_Known && Plan.m_aTeamModes[DDTeam].m_Flags != 0;
	};
	int NumTeamLabels = 0;
	int NumTeamModeLabels = 0;
	for(int RowIndex = FirstRow; RowIndex < EndRow; ++RowIndex)
	{
		const CScoreboardPlayerRow &PlannedRow = Plan.m_aRows[RowIndex];
		if(PlannedRow.m_DDTeam != TEAM_FLOCK && PlannedRow.m_NextSourceDDTeam != PlannedRow.m_DDTeam)
		{
			++NumTeamLabels;
			if(TeamHasModeIcons(PlannedRow.m_DDTeam))
				++NumTeamModeLabels;
		}
	}

	// calculate measurements
	constexpr float HeadlineFontsize = 11.0f;
	const CUIRect PlayerRows = ScoreboardPlayerRowsRect(Scoreboard, HeadlineFontsize * 2.0f);
	// 按原分列容量规划密度，短末列不能因实际人数少而放大字号。
	const SQmScoreboardRowMetrics RowMetrics = QmScoreboardRowMetrics(PlayerRows.h, NumPlayers, NumTeamLabels, NumTeamModeLabels, Scroll);
	const float LineHeight = RowMetrics.m_LineHeight;
	const float TeeSizeMod = RowMetrics.m_TeeSizeMod;
	const float Spacing = RowMetrics.m_Spacing;
	const float RoundRadius = RowMetrics.m_RoundRadius;
	const float FontSize = RowMetrics.m_FontSize;
	// 练习/锁队图标随行字号等比缩小：固定 12px 在人多压缩行高时会比队伍标签文字还大。
	// 基准场景（FontSize=12）下正好等于 SCOREBOARD_TEAM_MODE_ICON_SIZE，行为不变。
	const float TeamModeIconSize = minimum(SCOREBOARD_TEAM_MODE_ICON_SIZE, FontSize);

	// 表头固定，玩家区域使用与服务器列表相同的连续坐标滚动。
	CUIRect ScrollViewport = PlayerRows;
	vec2 ScrollOffset(0.0f, 0.0f);
	if(Scroll)
	{
		SQmScrollRequest ScrollRequest;
		ScrollRequest.m_Profile = EQmScrollProfile::MENU_LIST;
		ScrollRequest.m_RowExtent = LineHeight + Spacing;
		ScrollRequest.m_RowsPerStep = 2;
		CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ScrollRequest));
		ScrollParams.m_ScrollbarAlwaysReserved = true;
		ScrollParams.m_Interactive = m_MouseUnlocked && m_RenderInteractions;
		QmScoreboardScrollAlpha(ScrollParams, ContentAlpha);
		ScrollParams.m_pWheelOwnerId = &m_ScrollRegion;
		ScrollParams.m_WheelOwnerPreRegistered = true;
		Ui()->RegisterWheelOwner(&m_ScrollRegion, EUiWheelOwnerPriority::PAGE, ScrollViewport,
			m_MouseUnlocked && m_RenderInteractions && !Ui()->UnderlyingScrollBlocked());
		const float ContentHeight = ScoreboardRowsHeight(EndRow - FirstRow, NumTeamLabels, NumTeamModeLabels,
			LineHeight, Spacing, FontSize / 1.5f, TeamModeIconSize);
		// 在获取本帧偏移前收紧范围，人数减少时不会先绘制一帧空白。
		m_ScrollRegion.State().Advance(0.0f, {ScrollViewport.h, ContentHeight}, QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f));
		m_ScrollRegion.SetContentHeightForNextFrame(ContentHeight);
		m_ScrollRegion.Begin(&ScrollViewport, &ScrollOffset, &ScrollParams);
		Scoreboard.w = ScrollViewport.w;
		Ui()->ClipDisable();
	}

	const SScoreboardRowRenderDetail RowDetail = ResolveScoreboardRowRenderDetail();
	const bool ShowClientBrand = RowDetail.m_ShowClientBrand && g_Config.m_QmClientShowBadge;
	const float ClientBrandLength = ShowClientBrand ? maximum(TextRender()->TextWidth(FontSize, "Qm"), TextRender()->TextWidth(FontSize, "Arg")) + CLIENT_BRAND_LABEL_GAP : 0.0f;
	const float ClientBrandOffset = Scoreboard.x + 10.0f;
	const float ScoreOffset = Scoreboard.x + 20.0f + ClientBrandLength;
	// 0.6 服务器带毫秒完赛数据时，时间列要为「.000」留出宽度；
	// 小数按 0.7 倍字号绘制，与 RenderTime 的排版一致。
	const bool MillisecondScore = GameClient()->m_ReceivedDDNetPlayerFinishTimes;
	const bool TrueMilliseconds = GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis;
	float ScoreLength = TextRender()->TextWidth(FontSize, TimeScore ? TIME_WIDTH_TEXT : "99999");
	if(TimeScore && MillisecondScore)
	{
		const float MillisLength = TextRender()->TextWidth(FontSize * 0.7f, TrueMilliseconds ? ".000" : ".00");
		// 小数块与秒数有意少量重叠，避免视觉断裂（见 CUi::RenderTime 的 FractionKern）。
		ScoreLength += MillisLength * 0.65f;
	}
	// 统计列复用积分位置，并为带单位的表头预留宽度。
	const char *pStatsLabel = ShowPlaytime ? Localize("Playtime (h)") : (AxiomScoreColumn ? QmAxiomModeName(m_QmAxiomScoreModeFrame) : Localize("Points"));
	const float StatsLength = ShowStatistics ? maximum(TextRender()->TextWidth(FontSize, LowScoreboardWidth ? "99999" : "999999"), TextRender()->TextWidth(HeadlineFontsize, pStatsLabel)) : 0.0f;
	const float StatsOffset = ScoreOffset + ScoreLength + 10.0f;
	const float TeeOffset = ShowStatistics ? (StatsOffset + StatsLength + 10.0f) : (ScoreOffset + ScoreLength + 10.0f);
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float CountryFlagSize = TeeSizeMod * 5.0f;
	const float CountryLength = (LineHeight - Spacing - CountryFlagSize) * 2.0f;
	constexpr float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - 10.0f;
	const float CountryOffset = RowDetail.m_ShowCountry ? PingOffset - CountryLength : PingOffset;
	float NameLength = RowDetail.m_ShowClan ? (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength : maximum(0.0f, PingOffset - NameOffset - 5.0f);
	if(RowDetail.m_ShowClan)
	{
		// 分列后单列更窄：名字与 clan 之间至少留出 5px，剩余宽度按 7:3 分给名字和 clan。
		constexpr float MinMiddleGap = 5.0f;
		const float AvailableMiddle = CountryOffset - NameOffset;
		if(NameLength + MinMiddleGap > AvailableMiddle)
			NameLength = maximum(0.0f, (AvailableMiddle - MinMiddleGap) * 0.7f);
	}
	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = maximum(0.0f, CountryOffset - ClanOffset - 2.5f);

	// render headlines
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, nullptr);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const char *pScore = TimeScore ? Localize("Time") : Localize("Score");
	TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(HeadlineFontsize, pScore), HeadlineY, HeadlineFontsize, pScore);
	if(ShowStatistics)
	{
		TextRender()->Text(StatsOffset + StatsLength - TextRender()->TextWidth(HeadlineFontsize, pStatsLabel), HeadlineY, HeadlineFontsize, pStatsLabel);
	}
	TextRender()->Text(NameOffset, HeadlineY, HeadlineFontsize, Localize("Name"));
	if(RowDetail.m_ShowClan)
	{
		const char *pClanLabel = Localize("Clan");
		TextRender()->Text(ClanOffset + (ClanLength - TextRender()->TextWidth(HeadlineFontsize, pClanLabel)) / 2.0f, HeadlineY, HeadlineFontsize, pClanLabel);
	}
	const char *pPingLabel = Localize("Ping");
	TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(HeadlineFontsize, pPingLabel), HeadlineY, HeadlineFontsize, pPingLabel);

	const CUIRect &RowsViewport = Scroll ? ScrollViewport : PlayerRows;
	Scoreboard = RowsViewport;
	if(Scroll)
		Scoreboard.y += ScrollOffset.y;
	// 皮肤描边和字体像素对齐可能超出逻辑行高，限制在玩家区内。
	Ui()->ClipEnable(&RowsViewport);
	// render player entries
	int PrevDDTeam = -1;
	int &CurrentDDTeamSize = State.m_CurrentDDTeamSize;

	char aBuf[64];
	int MaxTeamSize = Config()->m_SvMaxTeamSize;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const char *pLocalClan = LocalClientId >= 0 ? GameClient()->m_aClients[LocalClientId].m_aClan : "";

	for(int RowIndex = FirstRow; RowIndex < EndRow; ++RowIndex)
	{
		const CScoreboardPlayerRow &PlannedRow = Plan.m_aRows[RowIndex];
		const CNetObj_PlayerInfo *pInfo = PlannedRow.m_pInfo;
		const bool RenderDead = PlannedRow.m_Dead;
		const int DDTeam = PlannedRow.m_DDTeam;
		const int NextDDTeam = PlannedRow.m_NextSourceDDTeam;

		// 文本依次出场：序号相对本列首行（首行 0），错峰窗口按本列行数压缩，
		// 闭式曲线不落每行轨道，滚动虚拟化也不会留下孤儿轨道。
		const float ItemAlpha = (RenderDead ? 0.5f : 1.0f) * ContentAlpha *
					ScoreboardTextEntryFade(m_TextEntryElapsedSec, RowIndex - FirstRow, g_Config.m_QmUiMotionLevel, EndRow - FirstRow);
		ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha);
		TextRender()->TextColor(TextColor);

		if(PrevDDTeam == -1)
			PrevDDTeam = PlannedRow.m_PreviousSourceDDTeam;

		const float TeamFontSize = FontSize / 1.5f;
		const bool EndsDDTeam = DDTeam != TEAM_FLOCK && NextDDTeam != DDTeam;
		const bool HasTeamModeIcons = EndsDDTeam && TeamHasModeIcons(DDTeam);
		const SScoreboardTeamLabelLayout TeamLabelLayout = ResolveScoreboardTeamLabelLayout(
			Scoreboard.x,
			Scoreboard.y,
			LineHeight,
			Spacing,
			TeamFontSize,
			HasTeamModeIcons ? TeamModeIconSize : 0.0f,
			EndsDDTeam);
		CUIRect RowAndSpacing, Row;
		Scoreboard.HSplitTop(LineHeight + TeamLabelLayout.m_RowSpacing, &RowAndSpacing, &Scoreboard);
		RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);
		const float TextY = Row.y + (Row.h - FontSize) / 2.0f;

		// 裁剪外的行只推进队伍计数和布局，不提交文字、旗帜或 Tee 绘制。
		if(Scroll && !m_ScrollRegion.AddRect(RowAndSpacing))
		{
			if(DDTeam != TEAM_FLOCK)
				CurrentDDTeamSize = EndsDDTeam ? 0 : CurrentDDTeamSize + 1;
			PrevDDTeam = DDTeam;
			continue;
		}

		// team background
		if(DDTeam != TEAM_FLOCK)
		{
			const ColorRGBA Color = ScoreboardDecorationColor(GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha));
			// 面板最后一行的队伍背景要贴着卡片圆角收口：用卡片圆角而不是缩放后的行圆角，
			// 否则小圆角填不满面板底部圆角，颜色会溢出到圆角外。
			const bool IsPanelLastRow = RowIndex + 1 == EndRow;
			int TeamRectCorners = 0;
			if(PrevDDTeam != DDTeam)
			{
				TeamRectCorners |= IGraphics::CORNER_T;
			}
			if(NextDDTeam != DDTeam || IsPanelLastRow)
				TeamRectCorners |= IGraphics::CORNER_B;
			RowAndSpacing.Draw(Color, TeamRectCorners, IsPanelLastRow ? SCOREBOARD_PANEL_RADIUS : RoundRadius);

			CurrentDDTeamSize++;

			if(EndsDDTeam)
			{
				if(DDTeam == TEAM_SUPER)
					str_copy(aBuf, Localize("Super"));
				else if(CurrentDDTeamSize > 1)
					str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
				else
					str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
				TextRender()->Text(TeamLabelLayout.m_X, TeamLabelLayout.m_Y, TeamFontSize, aBuf);
				if(HasTeamModeIcons)
				{
					RenderTeamModeIcons(
						TeamLabelLayout.m_X + TextRender()->TextWidth(TeamFontSize, aBuf) + 1.5f,
						TeamLabelLayout.m_IconY,
						TeamModeIconSize,
						Plan.m_aTeamModes[DDTeam],
						ItemAlpha);
				}

				CurrentDDTeamSize = 0;
			}
		}
		PrevDDTeam = DDTeam;

		// background so it's easy to find the local player or the followed one in spectator mode
		if((!GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_Local) ||
			(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && pInfo->m_Local) ||
			(GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId))
		{
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			Row.Draw(ScoreboardDecorationColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(ItemAlpha * 1.45f)), IGraphics::CORNER_ALL, RoundRadius);
		}

		const int ClientId = pInfo->m_ClientId;
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];

		if(m_MouseUnlocked && m_RenderInteractions)
		{
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			// 手机上没有悬停也没有右键：点整行直接跟随，玩家卡交给行尾的「…」按钮。
			const bool MobileRow = MobileRowTapActive();
			CUIRect RowHit = Row;
			CUIRect CardButton = Row;
			if(MobileRow)
			{
				const float CardButtonWidth = minimum(LineHeight, Row.w * 0.5f);
				RowHit.VSplitRight(CardButtonWidth, &RowHit, &CardButton);
				CardButton.HMargin(minimum(2.0f, CardButton.h * 0.2f), &CardButton);
			}

			const int ButtonResult = Ui()->DoButtonLogic(&ClientData, 0, &RowHit, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
			bool OpenCard = ButtonResult != 0 && !MobileRow;
			if(ButtonResult != 0 && MobileRow)
			{
				// 拖动列表后的松手是滚动，不是点行跟随。
				if(!m_MobileRowDragScroll)
				{
					SpectatePlayer(ClientId);
					SetActive(false);
				}
			}
			if(MobileRow)
			{
				const bool HoveredCard = Ui()->MouseHovered(&CardButton);
				const bool CardActive = Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == ClientId;
				CardButton.Draw(HoveredCard || CardActive ? ScoreboardDecorationColor(ui_token::color::TINT_HUD_CARD_ACTIVE.WithMultipliedAlpha(ItemAlpha)) : ScoreboardDecorationColor(ui_token::color::TINT_HUD_CARD.WithMultipliedAlpha(ItemAlpha)), IGraphics::CORNER_ALL, RoundRadius);
				TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha));
				Ui()->DoLabel(&CardButton, FontIcons::FONT_ICON_ELLIPSIS, minimum(LineHeight * 0.7f, 10.0f), TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				if(Ui()->DoButtonLogic(&m_aRowCardButtons[ClientId], 0, &CardButton, BUTTONFLAG_LEFT) && !m_MobileRowDragScroll)
					OpenCard = true;
			}
			if(OpenCard)
			{
				m_ScoreboardPopupContext.m_pScoreboard = this;
				m_ScoreboardPopupContext.m_ClientId = ClientId;
				m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == ClientId ||
								     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == ClientId);
				Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), m_ScoreboardPopupContext.m_IsLocal ? 110.0f : 210.0f, m_ScoreboardPopupContext.m_IsLocal ? 58.5f : 87.5f, &m_ScoreboardPopupContext, PopupScoreboard);
			}

			if(Ui()->HotItem() == &ClientData ||
				(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == ClientId))
			{
				Row.Draw(ScoreboardDecorationColor(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha)), IGraphics::CORNER_ALL, RoundRadius);
			}
		}

		// score
		if(Race7)
		{
			if(pInfo->m_Score == -1)
			{
				aBuf[0] = '\0';
			}
			else
			{
				// 0.7 uses milliseconds and ddnets str_time wants centiseconds
				// 0.7 servers can also send the amount of precision the client should use
				// we ignore that and always show 3 digit precision
				str_time((int64_t)absolute(pInfo->m_Score / 10), TIME_MINS_CENTISECS, aBuf, sizeof(aBuf));
			}
		}
		else if(TimeScore && MillisecondScore)
		{
			// 0.6 服务器若上报了 DDNet 完赛时间，优先显示「秒 + 小数」，
			// 不再退回只有整秒的 m_Score；未完赛或时间未知时留空。
			const int FinishSeconds = ClientData.m_FinishTimeSeconds;
			const bool NotFinished = FinishSeconds == FinishTime::NOT_FINISHED_MILLIS;
			CUIRect ScoreRect = {ScoreOffset, Row.y, ScoreLength, Row.h};
			Ui()->RenderTime(ScoreRect, FontSize, FinishSeconds, NotFinished, ClientData.m_FinishTimeMillis,
				TrueMilliseconds, m_aScoreSecondsText[ClientId], m_aScoreMillisText[ClientId], TextColor);
			// 超过一小时后 RenderTime 只显示到秒，悬停补一条带百分秒的完整时间。
			if(!NotFinished && FinishSeconds >= 60 * 60 && m_MouseUnlocked && m_RenderInteractions)
			{
				char aTimeTooltip[32];
				str_time((int64_t)FinishSeconds * 100 + ClientData.m_FinishTimeMillis / 10, TIME_HOURS_CENTISECS, aTimeTooltip, sizeof(aTimeTooltip));
				GameClient()->m_Tooltips.DoToolTipForRect(&m_aScoreTooltipIds[ClientId], &ScoreRect, aTimeTooltip, ScoreLength * 1.5f);
			}
		}
		else if(TimeScore)
		{
			if(pInfo->m_Score == -9999)
			{
				aBuf[0] = '\0';
			}
			else
			{
				str_time((int64_t)absolute(pInfo->m_Score) * 100, TIME_HOURS, aBuf, sizeof(aBuf));
			}
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Score, -999, 99999));
		}
		if(Race7 || !(TimeScore && MillisecondScore))
			TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(FontSize, aBuf), TextY, FontSize, aBuf);
		const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
		GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));
		const bool IsFriend = ClientData.m_Friend;
		bool IsSameClan = false;
		if(aClanBuf[0] != '\0')
		{
			if(LocalClientId >= 0 && str_comp(aClanBuf, pLocalClan) == 0)
			{
				IsSameClan = true;
			}
		}
		const bool HasWar = !HideIdentity && g_Config.m_QmWarList && g_Config.m_QmWarListScoreboard && GameClient()->m_WarList.GetAnyWar(ClientId);

		const EClientBrand ClientBrand = ShowClientBrand ? GameClient()->ClientBrand(ClientData.m_aName) : EClientBrand::NONE;
		if(!HideIdentity && ClientBrand != EClientBrand::NONE)
		{
			const char *pClientBrandLabel = ClientBrandPrefix(ClientBrand);
			const float ClientBrandWidth = TextRender()->TextWidth(FontSize, pClientBrandLabel);
			const float ClientBrandX = ClientBrandOffset + maximum(0.0f, ClientBrandLength - CLIENT_BRAND_LABEL_GAP - ClientBrandWidth) / 2.0f;
			TextRender()->TextColor(ClientBrandScoreboardColor(ClientBrand, ItemAlpha));
			TextRender()->Text(ClientBrandX, TextY, FontSize, pClientBrandLabel);
			TextRender()->TextColor(TextColor);
		}
		if(ShowStatistics)
		{
			char aPointsValue[32];
			if(ShowPlaytime)
			{
				if(HideIdentity)
					aPointsValue[0] = '\0';
				else
				{
					const SPlayerPlaytimeResult Playtime = GameClient()->m_PlayerPoints.GetPlaytime(ClientData.m_aName);
					if(Playtime.m_Status == EPointsStatus::READY)
						str_format(aPointsValue, sizeof(aPointsValue), "%" PRId64, Playtime.m_Hours);
					else
						str_copy(aPointsValue, Playtime.m_Status == EPointsStatus::FAILED ? "?" : "...");
				}
			}
			else if(AxiomScoreColumn)
			{
				// 隐藏主播身份的玩家不暴露 Axiom 分数。
				const bool PointsVisible = !HideIdentity && !GameClient()->ShouldHideStreamerIdentity(ClientId);
				QmAxiomScorePoints(ClientData.m_aName, PointsVisible, aPointsValue, sizeof(aPointsValue));
			}
			else
			{
				SPlayerPointsResult PointsResult = GameClient()->m_PlayerPoints.GetPoints(ClientData.m_aName);
				if(PointsResult.m_Status == EPointsStatus::READY)
				{
					str_format(aPointsValue, sizeof(aPointsValue), "%d", PointsResult.m_Points);
				}
				else if(PointsResult.m_Status == EPointsStatus::FETCHING || PointsResult.m_Status == EPointsStatus::NOT_REQUESTED)
				{
					str_copy(aPointsValue, "...");
				}
				else // FAILED
				{
					str_copy(aPointsValue, "?");
				}
			}
			if(aPointsValue[0] != '\0')
			{
				const float ValueWidth = TextRender()->TextWidth(FontSize, aPointsValue);
				const float ValueFontSize = ValueWidth > StatsLength ? FontSize * StatsLength / ValueWidth : FontSize;
				TextRender()->Text(StatsOffset + StatsLength - minimum(ValueWidth, StatsLength), Row.y + (Row.h - ValueFontSize) / 2.0f, ValueFontSize, aPointsValue);
			}
		}

		// CTF flag
		if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
			pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
		{
			Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(TeeOffset, Row.y - 2.5f - Spacing / 2.0f, Row.h / 2.0f, Row.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// skin
		if(RenderDead)
		{
			Graphics()->TextureSet(m_DeadTeeTexture);
			Graphics()->QuadsBegin();
			if(GameClient()->IsTeamPlay())
			{
				const ColorRGBA TeamColor = GameClient()->m_Skins7.GetTeamColor(true, 0, GameClient()->m_aClients[pInfo->m_ClientId].m_Team, protocol7::SKINPART_BODY).WithMultipliedAlpha(ItemAlpha);
				Graphics()->SetColor(TeamColor);
			}
			else
			{
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, ItemAlpha);
			}
			CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
			TeeInfo.m_Size *= TeeSizeMod;
			const CUIRect TeeRect = ScoreboardDeadTeeRect(Row, TeeOffset, TeeLength, TeeInfo.m_Size);
			IGraphics::CQuadItem QuadItem(TeeRect.x, TeeRect.y, TeeRect.w, TeeRect.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
			TeeInfo.m_Size *= TeeSizeMod;
			vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f);
			if(RowDetail.m_FullTee)
			{
				RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, TeeRenderPos, EMOTE_NORMAL, vec2(1.0f, 0.0f), ItemAlpha);
			}
			else
				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, TEE_PREVIEW_LAYER_BODY, ItemAlpha);

			// 光标解锁时悬停皮肤显示皮肤名；0.7 下 m_aSkinName 是 body 部件名。
			if(m_MouseUnlocked && m_RenderInteractions && ClientData.m_aSkinName[0] != '\0')
			{
				const CUIRect SkinRect = {TeeOffset, Row.y, TeeLength, Row.h};
				GameClient()->m_Tooltips.DoToolTip(&ClientData, &SkinRect, ClientData.m_aSkinName);
			}
		}

		// name
		{
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(NameOffset, TextY));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.m_LineWidth = NameLength;
			ColorRGBA NameColor = TextColor;
			if(ClientData.m_AuthLevel)
			{
				NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)).WithMultipliedAlpha(ItemAlpha);
			}
			else if(IsFriend)
			{
				NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithMultipliedAlpha(ItemAlpha);
			}
			else if(IsSameClan)
			{
				NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ItemAlpha);
			}
			TextRender()->TextColor(NameColor);
			if(g_Config.m_ClShowIds && !HideIdentity)
			{
				char aClientId[16];
				GameClient()->FormatClientId(ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
				TextRender()->TextEx(&Cursor, aClientId);
			}

			if(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore))
			{
				// 保留文字游标的测量和省略规则，实际图标走统一图集绘制。
				const EFontPreset PreviousPreset = TextRender()->GetFontPreset();
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				CTextCursor IconCursor = Cursor;
				IconCursor.m_Flags &= ~TEXTFLAG_RENDER;
				TextRender()->TextEx(&IconCursor, FontIcons::FONT_ICON_COMMENT_SLASH);
				TextRender()->SetFontPreset(PreviousPreset);
				if(IconCursor.m_GlyphCount > Cursor.m_GlyphCount && !IconCursor.m_Truncated)
				{
					const CUIRect IconRect = {Cursor.m_X, Cursor.m_Y, IconCursor.m_X - Cursor.m_X, FontSize};
					Ui()->DrawQmIcon(IconRect, EQmIcon::COMMENT_SLASH, FontIcons::FONT_ICON_COMMENT_SLASH, NameColor);
				}
				else if(IconCursor.m_Truncated)
				{
					// 连单个图标都放不下时，仍由原文字布局输出省略号。
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->TextEx(&Cursor, FontIcons::FONT_ICON_COMMENT_SLASH);
					TextRender()->SetFontPreset(PreviousPreset);
				}
				IconCursor.m_Flags = Cursor.m_Flags;
				Cursor = std::move(IconCursor);
			}

			// TClient
			if(HasWar)
				TextRender()->TextColor(GameClient()->m_WarList.GetNameplateColor(ClientId).WithMultipliedAlpha(ItemAlpha));

			TextRender()->TextEx(&Cursor, aNameBuf);

			// ready / watching
			if(Client()->IsSixup() && Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_READY)
			{
				TextRender()->TextColor(0.1f, 1.0f, 0.1f, TextColor.a);
				TextRender()->TextEx(&Cursor, "✓");
			}
		}

		// clan
		if(RowDetail.m_ShowClan)
		{
			const char *pClanName = aClanBuf;
			if(IsSameClan)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ItemAlpha));
			}
			else
			{
				TextRender()->TextColor(TextColor);
			}

			// TClient
			if(HasWar)
				TextRender()->TextColor(GameClient()->m_WarList.GetClanColor(ClientId).WithMultipliedAlpha(ItemAlpha));

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(ClanOffset + (ClanLength - minimum(TextRender()->TextWidth(FontSize, pClanName), ClanLength)) / 2.0f, TextY));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.m_LineWidth = ClanLength;
			TextRender()->TextEx(&Cursor, pClanName);
		}

		// country flag
		if(RowDetail.m_ShowCountry)
		{
			const int CountryCode = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : ClientData.m_Country;
			GameClient()->m_CountryFlags.Render(CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * ItemAlpha),
				CountryOffset, Row.y + (Spacing + CountryFlagSize) / 2.0f, CountryLength, Row.h - Spacing - CountryFlagSize, m_ActiveStartTime);
		}

		// ping
		if(g_Config.m_ClEnablePingColor)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(pInfo->m_Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f)).WithMultipliedAlpha(ItemAlpha));
		}
		else
		{
			TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha));
		}
		str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
		TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(FontSize, aBuf), TextY, FontSize, aBuf);
		TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha));
	}
	if(Scroll)
	{
		m_ScrollRegion.End();
	}
	else
	{
		Ui()->ClipDisable();
	}

	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);
}

void CScoreboard::RenderRecordingNotification(float x)
{
	const float ContentAlpha = m_AnimContentAlpha;
	char aBuf[512] = "";

	const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
		if(GameClient()->DemoRecorder(Recorder)->IsRecording())
		{
			char aTime[32];
			str_time((int64_t)GameClient()->DemoRecorder(Recorder)->Length() * 100, TIME_HOURS, aTime, sizeof(aTime));
			str_append(aBuf, pName);
			str_append(aBuf, " ");
			str_append(aBuf, aTime);
			str_append(aBuf, "  ");
		}
	};

	AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
	AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
	AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
	AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));

	if(aBuf[0] == '\0')
		return;

	const float FontSize = 10.0f;

	CUIRect Rect = {x, 0.0f, TextRender()->TextWidth(FontSize, aBuf) + 30.0f, 25.0f};
	Rect.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_B, SCOREBOARD_PANEL_RADIUS);
	CUIRect Circle;
	CUIRect TextRect;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle RowStyle;
		RowStyle.m_Axis = EUiAxis::ROW;
		RowStyle.m_Gap = 5.0f;
		RowStyle.m_Padding.m_Left = 10.0f;
		RowStyle.m_Padding.m_Right = 5.0f;
		RowStyle.m_AlignItems = EUiAlign::STRETCH;
		RowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vChildren;
		std::vector<SUiLayoutChild> &vChildren = s_vChildren;
		vChildren.assign(2, SUiLayoutChild{});
		vChildren[0].m_Style.m_Width = SUiLength::Px(10.0f);
		vChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(RowStyle, CUiV2LegacyAdapter::FromCUIRect(Rect), vChildren);
		Circle = CUiV2LegacyAdapter::ToCUIRect(vChildren[0].m_Box);
		TextRect = CUiV2LegacyAdapter::ToCUIRect(vChildren[1].m_Box);
	}
	Circle.HMargin((Circle.h - Circle.w) / 2.0f, &Circle);
	Circle.Draw(ScoreboardDecorationColor(ColorRGBA(1.0f, 0.0f, 0.0f, ContentAlpha)), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Ui()->DoLabel(&TextRect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::OnRender()
{
	// 「死亡自动显示」与「游戏结束」都由快照派生：存活时也会偶发一两帧
	// m_pLocalCharacter 为空（丢包、实体延迟），重生/换图/观战切换同理。
	// 手动开关（Tab）保持立即响应，自动条件做 2 帧双向外滤——否则一帧噪声就会
	// 开一次计分板，入场动画刚起手就被拉回，实机表现为「入场有时自己打断」。
	// 放在所有提前返回之前：隐藏/离线期间也要能把这层状态收敛回 false。
	{
		const bool RawAutoActive = IsAutoActive();
		if(RawAutoActive)
		{
			m_AutoOnFrames = minimum(m_AutoOnFrames + 1, 2);
			m_AutoOffFrames = 0;
		}
		else
		{
			m_AutoOffFrames = minimum(m_AutoOffFrames + 1, 2);
			m_AutoOnFrames = 0;
		}
		if(m_AutoOnFrames >= 2)
			m_AutoActiveStable = true;
		else if(m_AutoOffFrames >= 2)
			m_AutoActiveStable = false;
	}

	m_RenderInteractions = false;
	if(!IsActive() || !m_MouseUnlocked || GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GetQmFocusModeDecisions().m_HideScoreboard)
		m_MediaControls.Cancel(*Ui());

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	UpdateTeamModeCache();
	UpdateQmAxiomScoreMode();

	if(GetQmFocusModeDecisions().m_HideScoreboard)
		return;

	// 沿用积分预热；切换统计列复用同一份 DDNet 响应。
	if(HasQmAxiomScoreMode())
	{
		// Axiom 排序继续使用当前模式分数，显示游玩时间不改变排序数据源。
		GameClient()->m_QmAxiomScores.SetMode(m_QmAxiomScoreModeFrame);
		// 记分板打开时给足预算铺满一屏，否则只做少量预热；
		// 真正发请求的速度由查分组件按 API 限额控制。
		int Budget = IsActive() ? MAX_CLIENTS : AXIOM_SCOREBOARD_PREFETCH_BUDGET;
		for(int i = 0; i < MAX_CLIENTS && Budget > 0; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_aClients[i].m_Active)
				continue;
			if(GameClient()->ShouldHideStreamerIdentity(i))
				continue;
			Budget--;
			GameClient()->m_QmAxiomScores.EnsureScoreboardQueried(GameClient()->m_aClients[i].m_aName);
		}
	}
	else
	{
		GameClient()->m_QmAxiomScores.SetMode(EQmAxiomMode::NONE);
	}
	const bool NeedDdnetPoints = !HasQmAxiomScoreMode() && (g_Config.m_QmScoreboardPoints || g_Config.m_QmScoreboardSortMode);
	if(g_Config.m_QmScoreboardPlaytime || NeedDdnetPoints)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Snap.m_apPlayerInfos[i] && GameClient()->m_aClients[i].m_Active)
			{
				if(!NeedDdnetPoints && GameClient()->ShouldHideStreamerIdentity(i))
					continue;
				if(g_Config.m_QmScoreboardPlaytime && !GameClient()->ShouldHideStreamerIdentity(i))
					GameClient()->m_PlayerPoints.EnsurePlaytimeQueried(GameClient()->m_aClients[i].m_aName);
				if(NeedDdnetPoints)
					GameClient()->m_PlayerPoints.EnsureQueried(GameClient()->m_aClients[i].m_aName);
			}
		}
	}

	// If scoreboard was opened by death/pause auto activation, ensure cursor locks back when it closes.
	if(!IsActive() && m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}

	const bool WantActive = IsActive();
	// 手机上没有“锁定光标”这一步：计分板一打开就按已解锁处理，玩家行和「…」按钮直接可点。
	if(QmUiMobilePlatform() && WantActive && !m_MouseUnlocked)
	{
		m_MouseUnlocked = true;
		m_LastMousePos = std::nullopt;
	}
	// 显示状态转换检测：记录本次是否由隐藏转为显示（Tab 开合、死亡自动弹出、
	// 游戏结束）；具体是完整重播还是只做打断回弹，由下面的三态动作决定。
	const bool FreshOpen = WantActive && !m_WasActive;
	// 关闭途中重开时，面板和旧文字仍在屏幕上；此刻重播弹出/错峰会先缩没再长出，
	// 读作“关闭又打开”。只有上一轮已经收干净才重置入场基准。
	const bool PanelStillOnScreen = m_Visibility > 0.02f;
	const EScoreboardEntranceAction EntranceAction = ResolveScoreboardEntranceAction(FreshOpen, PanelStillOnScreen);
	if(EntranceAction == EScoreboardEntranceAction::REPLAY)
		m_ActiveStartTime = time_get();
	m_WasActive = WantActive;
	const bool ExtraAnimations = g_Config.m_QmUiMotionLevel > 0 && GameClient()->UiRuntimeV2()->Enabled();
	float PanelOffsetY = 0.0f;
	float PanelScale = 1.0f;
	bool TitleSubpixel = false;
	if(!ExtraAnimations)
	{
		if(!WantActive)
		{
			m_OpenTime = 0.0f;
			m_Visibility = 0.0f;
			m_AnimContentAlpha = 0.0f;
			m_RenderInteractions = false;
			return;
		}

		m_OpenTime = 1.0f;
		m_Visibility = 1.0f;
		m_AnimContentAlpha = 1.0f;
	}
	else
	{
		CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
		const uint64_t PanelNode = ScoreboardPresentationNodeKey("panel");
		const SUiSpringConfig Spring = ScoreboardPresentationSpring();
		if(!m_PresentationInitialized)
		{
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, 0.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::POS_Y, -10.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, 0.985f);
			m_PresentationInitialized = true;
		}

		const float TargetVisibility = WantActive ? 1.0f : 0.0f;
		const float TargetOffsetY = WantActive ? 0.0f : -10.0f;
		const float TargetScale = WantActive ? 1.0f : 0.985f;
		m_Visibility = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, TargetVisibility, Spring, 3, 0.004f), 0.0f, 1.0f);
		m_AnimContentAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, TargetVisibility, ScoreboardContentSpring(WantActive), 3, 0.004f), 0.0f, 1.0f);
		PanelOffsetY = ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::POS_Y, TargetOffsetY, Spring, 3, 0.01f);
		const float BaseScale = std::max(0.01f, ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, TargetScale, Spring, 3, 0.002f));
		// 略小于 100% → 过冲 → 100% 的弹性入场单独占一条轨道：不与上面
		// 1.0/0.985 的收起缩放共用，否则 ResolveUiPresentationStateValue 的目标缓存
		// 会把重开时的起点当成目标没变而吞掉，第二次按 Tab 就没有弹一下了。
		const uint64_t PopNode = ScoreboardPresentationNodeKey("pop");
		if(EntranceAction != EScoreboardEntranceAction::NONE)
		{
			const float CurrentPop = AnimRuntime.GetValue(PopNode, EUiAnimProperty::SCALE, 1.0f);
			const float PopStartScale = ResolveScoreboardPopStartScale(EntranceAction, CurrentPop);
			StartScoreboardPop(AnimRuntime, PopNode, PopStartScale);
		}
		PanelScale = BaseScale * std::max(0.01f, AnimRuntime.GetValue(PopNode, EUiAnimProperty::SCALE, 1.0f));
		// 只有几何仍在变化时才放弃整像素对齐；停稳后回到对齐路径，保持字形清晰。
		TitleSubpixel = AnimRuntime.HasActiveAnimation(PanelNode, EUiAnimProperty::SCALE) ||
				AnimRuntime.HasActiveAnimation(PopNode, EUiAnimProperty::SCALE);
		m_OpenTime = m_Visibility;

		if(!WantActive && m_Visibility <= 0.01f && !AnimRuntime.HasActiveAnimation(PanelNode, EUiAnimProperty::ALPHA))
		{
			m_OpenTime = 0.0f;
			m_Visibility = 0.0f;
			m_AnimContentAlpha = 0.0f;
			m_RenderInteractions = false;
			return;
		}
	}

	// 面板还很小的时候先把内容压住：立即模式布局会按缩放后的高度重排字号，
	// 让文字在 0.1 倍尺寸下满透明度出现会读作一团噪点。门槛压在面板极小的那一段，
	// 面板过半尺寸就完全放开，行的错峰淡入才有可玩的余量。
	if(ExtraAnimations && WantActive)
		m_AnimContentAlpha *= ScoreboardContentScaleGate(PanelScale);

	// 文本错峰只在打开期间存在；关闭时置 -1，所有区块随面板一起淡出，不再依次出场。
	// 起点是「面板弹跳到接近就位」那一帧，不是按下 Tab 那一帧：面板弹跳约 300ms，
	// 行在整个弹跳过程中已经被缩放带着长大，若同时淡入，等面板停稳时整列早已满不
	// 透明——这就是「下方列表瞬间出现」的由来。等面板就位再逐行浮现才有依次感。
	if(!WantActive || !ExtraAnimations)
	{
		m_TextEntryStartTime = 0;
		m_TextEntryElapsedSec = -1.0f;
	}
	else
	{
		// 只有完整重播才重新等待面板就位；打断重开时保留进行中的错峰，
		// 若上一轮还没开始错峰则直接推到已结束，避免行短暂消失又淡入。
		if(FreshOpen)
		{
			if(EntranceAction == EScoreboardEntranceAction::REPLAY)
				m_TextEntryStartTime = 0;
			else if(m_TextEntryStartTime == 0)
				m_TextEntryStartTime = time_get() - time_freq();
		}
		// 面板过冲/收敛期间 PanelScale 会短暂超过 1；0.95 表示「基本就位」。
		// 兜底：面板因任何原因久未到位时，最多等 0.6s 就开始，绝不让列表一直透明。
		const bool EntryReady = PanelScale >= 0.95f ||
					(m_ActiveStartTime > 0 && (double)(time_get() - m_ActiveStartTime) / (double)time_freq() > 0.6);
		if(m_TextEntryStartTime == 0 && EntryReady)
			m_TextEntryStartTime = time_get();
		// 未到起点用 0（行完全透明）。不能沿用 -1：那是「不在入场生命周期」，
		// 语义是「满透明度直接跟随面板淡出」，会让行在等待期间就整块显形。
		m_TextEntryElapsedSec = m_TextEntryStartTime > 0 ? (float)((double)(time_get() - m_TextEntryStartTime) / (double)time_freq()) : 0.0f;
	}

	const bool ScoreboardUiInteractive = WantActive && !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive();
	m_RenderInteractions = ScoreboardUiInteractive;
	if(ScoreboardUiInteractive)
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// 如果记分板处于活动状态，则应同时清除每日公告消息。
	if(WantActive && GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	const float BackgroundAlphaFinal = ExtraAnimations ? m_Visibility : m_AnimContentAlpha;
	const float GaussianBlurAlpha = WantActive ? 1.0f : m_AnimContentAlpha;
	CUiScopedGaussianBlur GaussianBlurScope(Ui(), GaussianBlurAlpha);
	const float ContentOffset = 0.0f;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	CScoreboardPlayerRowPlan RedPlayerRows;
	CScoreboardPlayerRowPlan BluePlayerRows;
	BuildPlayerRowPlan(TEAM_RED, RedPlayerRows);
	if(Teams)
		BuildPlayerRowPlan(TEAM_BLUE, BluePlayerRows);
	const int NumPlayers = Teams ? maximum(RedPlayerRows.m_Count, BluePlayerRows.m_Count) : RedPlayerRows.m_Count;
	// 滚动模式：非队伍玩法且人数超过一屏时，固定行高只显示一列，滚轮查看其余玩家。
	const bool ScrollMode = ScoreboardScrollModeEnabled(Teams, g_Config.m_QmScoreboardScroll, NumPlayers);
	if(!ScrollMode)
		m_ScrollRegion.Reset();
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;

	// Scoreboard width: clamp to screen width for narrow aspect ratios
	const float ScreenMargin = 10.0f;
	CSystemMediaControls::SState MediaState;
	const bool HasMedia = GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
	const float MaxScoreboardWidth = HasMedia ? QmScoreboardMediaMaxWidth(Screen.w, true) : maximum(200.0f, Screen.w - ScreenMargin);
	const int ScoreboardColumns = ScrollMode ? 1 : (Teams ? 2 : (NumPlayers <= 16 ? 1 : (NumPlayers <= 64 ? 2 : 3)));
	const float ClientBrandExtraWidth = g_Config.m_QmClientShowBadge ? maximum(TextRender()->TextWidth(12.0f, "Qm"), TextRender()->TextWidth(12.0f, "Arg")) + CLIENT_BRAND_LABEL_GAP : 0.0f;
	const float BaseScoreboardSmallWidth = ((g_Config.m_QmScoreboardPoints || g_Config.m_QmScoreboardPlaytime) ? (450.0f + 10.0f) : 450.0f) + ClientBrandExtraWidth;
	const float ScoreboardSmallWidth = minimum(BaseScoreboardSmallWidth, MaxScoreboardWidth);
	const float BaseScoreboardWidth = ScrollMode || (!Teams && NumPlayers <= 16) ? ScoreboardSmallWidth : 850.0f + ClientBrandExtraWidth * ScoreboardColumns;
	const float ScoreboardWidth = minimum(BaseScoreboardWidth, MaxScoreboardWidth);
	const float TitleHeight = 30.0f;

	// 手机：面板顶到更高、整体加高，列表能看到更多行（桌面维持原高度）。
	const float ScoreboardTop = QmUiMobilePlatform() ? 40.0f : 75.0f;
	const float ScoreboardHeight = QmUiMobilePlatform() ? QmScoreboardScrollPanelHeight(Screen.h, ScoreboardTop) : QmScoreboardPanelHeight(Screen.h, ScoreboardTop);
	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, ScoreboardTop, ScoreboardWidth, ScoreboardHeight};
	if(ExtraAnimations)
	{
		Scoreboard = ScaleRectAroundCenter(Scoreboard, PanelScale);
		Scoreboard.y += PanelOffsetY;
	}
	if(Screen.w > 0.0f && Screen.h > 0.0f)
		m_LastPanelNdc = {Scoreboard.x / Screen.w, Scoreboard.y / Screen.h, Scoreboard.w / Screen.w, Scoreboard.h / Screen.h};
	// 手机：同一根手指拖动列表时，DoButtonLogic 仍会把它判成"点中某行"，
	// 于是滚动过程中视角被切走。按下后位移超过阈值就按拖动处理，松手不触发跟随。
	if(MobileRowTapActive())
	{
		const vec2 MobileMouse = Ui()->MousePos();
		if(Ui()->MouseButton(0))
		{
			if(!m_MobileRowPressActive)
			{
				m_MobileRowPressActive = true;
				m_MobileRowDragScroll = false;
				m_MobileRowPressPos = MobileMouse;
			}
			else if(distance(MobileMouse, m_MobileRowPressPos) > 8.0f)
				m_MobileRowDragScroll = true;
		}
		else
			m_MobileRowPressActive = false;
	}
	CUIRect ScoreboardContent = Scoreboard;
	ScoreboardContent.y += ContentOffset;
	CScoreboardRenderState RenderState{};
	static CButtonContainer s_ScoreboardSortButton;
	const float SortButtonFontSize = 10.0f;
	const char *pSortLabel = nullptr;
	if(g_Config.m_QmScoreboardSortMode)
		pSortLabel = Localize("Current: Ranks");
	else
		pSortLabel = TimeScore ? Localize("Current: Time") : Localize("Current: Score");
	const float SortButtonWidth = TextRender()->TextWidth(SortButtonFontSize, pSortLabel) + 18.0f;
	const ColorRGBA SortButtonColor = g_Config.m_QmScoreboardSortMode ? ScoreboardWithUiAlpha(ColorRGBA(0.25f, 0.55f, 0.8f, 0.6f), 1.0f) : ScoreboardUiColorSurface(1.0f, 0.18f);
	auto &&DoHeaderButton = [&](CButtonContainer *pButton, const char *pLabel, CUIRect Rect, const ColorRGBA &Color) {
		CUiScopedControlAlphaScale ControlAlpha(Ui(), m_AnimContentAlpha);
		Rect.VMargin(4.0f, &Rect);
		Rect.HMargin(6.0f, &Rect);
		return Ui()->DoButton_PopupMenu(pButton, pLabel, &Rect, SortButtonFontSize, TEXTALIGN_MC, 0.0f, false, m_RenderInteractions, Color);
	};
	auto &&DoSortButton = [&](CUIRect Rect) {
		if(DoHeaderButton(&s_ScoreboardSortButton, pSortLabel, Rect, SortButtonColor))
			g_Config.m_QmScoreboardSortMode ^= 1;
	};
	static CButtonContainer s_ScoreboardStatisticsButton;
	const char *pStatisticsLabel = g_Config.m_QmScoreboardPlaytime ? Localize("Show points") : Localize("Show playtime");
	const float StatisticsButtonWidth = maximum(TextRender()->TextWidth(SortButtonFontSize, Localize("Show points")), TextRender()->TextWidth(SortButtonFontSize, Localize("Show playtime"))) + 18.0f;
	auto &&DoStatisticsButton = [&](CUIRect Rect) {
		if(DoHeaderButton(&s_ScoreboardStatisticsButton, pStatisticsLabel, Rect, ScoreboardUiColorSurface(1.0f, 0.18f)))
		{
			g_Config.m_QmScoreboardPlaytime ^= 1;
			// 主动切回积分时保证该列可见，默认关闭积分查询的配置仍可直接使用按钮。
			if(!g_Config.m_QmScoreboardPlaytime)
				g_Config.m_QmScoreboardPoints = 1;
		}
	};

	// 服务器人数：沿用官方 ESC 服务器信息的「当前 / 上限」，仅真实联机且服务端上报上限时显示。
	const CServerInfo &ServerInfo = Client()->ServerInfo();
	const bool ShowServerPlayers = Client()->State() == IClient::STATE_ONLINE && ServerInfo.m_MaxClients > 0;
	char aServerPlayersBuf[64] = "";
	if(ShowServerPlayers)
		str_format(aServerPlayersBuf, sizeof(aServerPlayersBuf), "%s: %d/%d", Localize("Players"), GameClient()->m_Snap.m_NumPlayers, ServerInfo.m_MaxClients);
	const float ServerPlayersFontSize = 10.0f;
	const float ServerPlayersWidth = ShowServerPlayers ? TextRender()->TextWidth(ServerPlayersFontSize, aServerPlayersBuf) + 34.0f : 0.0f;
	auto &&DoServerPlayers = [&](CUIRect Rect) {
		if(ShowServerPlayers)
			RenderServerPlayerCount(Rect, aServerPlayersBuf);
	};

	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(m_AnimContentAlpha);
	const ColorRGBA BaseTextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(m_AnimContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseTextOutlineColor);

	if(Teams)
	{
		const char *pRedTeamName = GetTeamName(TEAM_RED);
		const char *pBlueTeamName = GetTeamName(TEAM_BLUE);

		// Game over title
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if((pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && pGameDataObj)
		{
			char aTitle[256];
			if(pGameDataObj->m_TeamscoreRed > pGameDataObj->m_TeamscoreBlue)
			{
				TextRender()->TextColor(TEAM_RED_TITLE_COLOR.WithMultipliedAlpha(m_AnimContentAlpha));
				if(pRedTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Red team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pRedTeamName);
				}
			}
			else if(pGameDataObj->m_TeamscoreBlue > pGameDataObj->m_TeamscoreRed)
			{
				TextRender()->TextColor(TEAM_BLUE_TITLE_COLOR.WithMultipliedAlpha(m_AnimContentAlpha));
				if(pBlueTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Blue team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pBlueTeamName);
				}
			}
			else
			{
				TextRender()->TextColor(ColorRGBA(0.91f, 0.78f, 0.33f, 1.0f).WithMultipliedAlpha(m_AnimContentAlpha));
				str_copy(aTitle, Localize("Draw!"));
			}

			const float TitleFontSize = 36.0f;
			CUIRect GameOverTitle = {Scoreboard.x, Scoreboard.y - TitleFontSize - 6.0f + ContentOffset, Scoreboard.w, TitleFontSize};
			Ui()->DoLabel(&GameOverTitle, aTitle, TitleFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(BaseTextColor);
		}

		CUIRect RedScoreboard, BlueScoreboard, RedTitle, BlueTitle;
		CUIRect RedScoreboardContent, BlueScoreboardContent, RedTitleContent, BlueTitleContent;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TwoColumnStyle;
			TwoColumnStyle.m_Axis = EUiAxis::ROW;
			TwoColumnStyle.m_Gap = 7.5f;
			TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
			TwoColumnStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vColumns;
			std::vector<SUiLayoutChild> &vColumns = s_vColumns;
			vColumns.assign(2, SUiLayoutChild{});
			vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(Scoreboard), vColumns);
			RedScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
			BlueScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
		}
		RedScoreboard.HSplitTop(TitleHeight, &RedTitle, &RedScoreboard);
		BlueScoreboard.HSplitTop(TitleHeight, &BlueTitle, &BlueScoreboard);
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TwoColumnStyle;
			TwoColumnStyle.m_Axis = EUiAxis::ROW;
			TwoColumnStyle.m_Gap = 7.5f;
			TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
			TwoColumnStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vColumns;
			std::vector<SUiLayoutChild> &vColumns = s_vColumns;
			vColumns.assign(2, SUiLayoutChild{});
			vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContent), vColumns);
			RedScoreboardContent = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
			BlueScoreboardContent = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
		}
		RedScoreboardContent.HSplitTop(TitleHeight, &RedTitleContent, &RedScoreboardContent);
		BlueScoreboardContent.HSplitTop(TitleHeight, &BlueTitleContent, &BlueScoreboardContent);
		CUIRect SortButton;
		CUIRect ServerPlayers;
		const CUIRect BlueTitleBackground = BlueTitle;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TitleSplitStyle;
			TitleSplitStyle.m_Axis = EUiAxis::ROW;
			TitleSplitStyle.m_AlignItems = EUiAlign::STRETCH;
			TitleSplitStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
			std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
			vTitleChildren.assign(ShowServerPlayers ? 3 : 2, SUiLayoutChild{});
			const int SortButtonIndex = ShowServerPlayers ? 2 : 1;
			vTitleChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			if(ShowServerPlayers)
				vTitleChildren[1].m_Style.m_Width = SUiLength::Px(ServerPlayersWidth);
			vTitleChildren[SortButtonIndex].m_Style.m_Width = SUiLength::Px(SortButtonWidth);
			LayoutEngine.ComputeChildren(TitleSplitStyle, CUiV2LegacyAdapter::FromCUIRect(BlueTitleContent), vTitleChildren);
			BlueTitleContent = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			if(ShowServerPlayers)
				ServerPlayers = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
			SortButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[SortButtonIndex].m_Box);
		}

		RedTitle.Draw(ScoreboardWithUiAlpha(ui_token::color::DANGER, BackgroundAlphaFinal), IGraphics::CORNER_T, ui_token::radius::CARD);
		BlueTitleBackground.Draw(ScoreboardWithUiAlpha(ui_token::color::ACCENT_PRIMARY_DIM, BackgroundAlphaFinal), IGraphics::CORNER_T, ui_token::radius::CARD);
		RedScoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_B, ui_token::radius::CARD);
		BlueScoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_B, ui_token::radius::CARD);

		CUIRect StatisticsButton;
		RedTitleContent.VSplitRight(StatisticsButtonWidth, &RedTitleContent, &StatisticsButton);
		RenderTitleBar(RedTitleContent, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName, TitleSubpixel);
		RenderTitleBar(BlueTitleContent, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName, TitleSubpixel);
		DoStatisticsButton(StatisticsButton);
		DoServerPlayers(ServerPlayers);
		DoSortButton(SortButton);
		// 单队超过 64 人时拆成左右两列：上半区先排满 64 行，其余进右列。
		// 两列共用 64 行密度，右列人少也不会把行高放大到和左列不一致。
		const auto RenderTeamColumn = [&](CUIRect TeamScoreboard, int Team, const CScoreboardPlayerRowPlan &Plan) {
			constexpr int ColumnCapacity = 64;
			if(Plan.m_Count <= ColumnCapacity)
			{
				RenderScoreboard(TeamScoreboard, Team, 0, Plan.m_Count, Plan, RenderState);
				return;
			}
			CUIRect LeftColumn, RightColumn;
			TeamScoreboard.VSplitMid(&LeftColumn, &RightColumn, 2.5f);
			RenderScoreboard(LeftColumn, Team, 0, ColumnCapacity, Plan, RenderState, false, ColumnCapacity);
			RenderScoreboard(RightColumn, Team, ColumnCapacity, Plan.m_Count, Plan, RenderState, false, ColumnCapacity);
		};
		RenderTeamColumn(RedScoreboardContent, TEAM_RED, RedPlayerRows);
		RenderTeamColumn(BlueScoreboardContent, TEAM_BLUE, BluePlayerRows);
	}
	else
	{
		Scoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_ALL, ui_token::radius::CARD);

		const char *pTitle;
		if(pGameInfoObj && (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			pTitle = Localize("Game over");
		}
		else
		{
			pTitle = Client()->GetCurrentMap();
		}

		CUIRect Title;
		CUIRect ScoreboardContentBody = ScoreboardContent;
		ScoreboardContentBody.HSplitTop(TitleHeight, &Title, &ScoreboardContentBody);
		// 标题保持单行：地图名宽度不够时由 RenderTitle 自动缩字号，不再拆行。
		CUIRect SortButton;
		CUIRect ServerPlayers;
		CUIRect ScrollButton;
		CUIRect StatisticsButton;
		const bool ShowScrollButton = !Teams && NumPlayers > 16;
		const char *pScrollLabel = g_Config.m_QmScoreboardScroll ? Localize("Scroll mode: On") : Localize("Scroll mode: Off");
		const float ScrollButtonWidth = TextRender()->TextWidth(SortButtonFontSize, pScrollLabel) + 18.0f;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TitleSplitStyle;
			TitleSplitStyle.m_Axis = EUiAxis::ROW;
			TitleSplitStyle.m_AlignItems = EUiAlign::STRETCH;
			TitleSplitStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
			std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
			int ChildCount = 1;
			if(ShowServerPlayers)
				++ChildCount;
			if(ShowScrollButton)
				++ChildCount;
			++ChildCount; // 排序按钮
			++ChildCount; // 统计列切换按钮
			vTitleChildren.assign(ChildCount, SUiLayoutChild{});
			int ChildIndex = 0;
			vTitleChildren[ChildIndex++].m_Style.m_Width = SUiLength::Flex(1.0f);
			if(ShowServerPlayers)
				vTitleChildren[ChildIndex++].m_Style.m_Width = SUiLength::Px(ServerPlayersWidth);
			const int ScrollButtonIndex = ShowScrollButton ? ChildIndex++ : -1;
			if(ShowScrollButton)
				vTitleChildren[ScrollButtonIndex].m_Style.m_Width = SUiLength::Px(ScrollButtonWidth);
			const int StatisticsButtonIndex = ChildIndex++;
			vTitleChildren[StatisticsButtonIndex].m_Style.m_Width = SUiLength::Px(StatisticsButtonWidth);
			const int SortButtonIndex = ChildIndex++;
			vTitleChildren[SortButtonIndex].m_Style.m_Width = SUiLength::Px(SortButtonWidth);
			LayoutEngine.ComputeChildren(TitleSplitStyle, CUiV2LegacyAdapter::FromCUIRect(Title), vTitleChildren);
			int BoxIndex = 0;
			Title = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[BoxIndex++].m_Box);
			if(ShowServerPlayers)
				ServerPlayers = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[BoxIndex++].m_Box);
			if(ShowScrollButton)
				ScrollButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[ScrollButtonIndex].m_Box);
			StatisticsButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[StatisticsButtonIndex].m_Box);
			SortButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[SortButtonIndex].m_Box);
		}
		RenderTitleBar(Title, TEAM_GAME, pTitle, TitleSubpixel);
		DoServerPlayers(ServerPlayers);
		if(ShowScrollButton)
		{
			static CButtonContainer s_ScoreboardScrollButton;
			const ColorRGBA ScrollButtonColor = g_Config.m_QmScoreboardScroll ? ScoreboardWithUiAlpha(ColorRGBA(0.25f, 0.55f, 0.8f, 0.6f), 1.0f) : ScoreboardUiColorSurface(1.0f, 0.18f);
			if(DoHeaderButton(&s_ScoreboardScrollButton, pScrollLabel, ScrollButton, ScrollButtonColor))
			{
				g_Config.m_QmScoreboardScroll ^= 1;
				m_ScrollRegion.Reset();
			}
		}
		DoStatisticsButton(StatisticsButton);
		DoSortButton(SortButton);

		if(ScrollMode)
		{
			RenderScoreboard(ScoreboardContentBody, TEAM_GAME, 0, RedPlayerRows.m_Count, RedPlayerRows, RenderState, true);
		}
		else if(NumPlayers <= 16)
		{
			RenderScoreboard(ScoreboardContentBody, TEAM_GAME, 0, NumPlayers, RedPlayerRows, RenderState);
		}
		else if(NumPlayers <= 64)
		{
			int PlayersPerSide;
			if(NumPlayers <= 24)
				PlayersPerSide = 12;
			else if(NumPlayers <= 32)
				PlayersPerSide = 16;
			else if(NumPlayers <= 48)
				PlayersPerSide = 24;
			else
				PlayersPerSide = 32;

			CUIRect LeftScoreboard, RightScoreboard;
			{
				CUiV2LayoutEngine LayoutEngine;
				SUiStyle TwoColumnStyle;
				TwoColumnStyle.m_Axis = EUiAxis::ROW;
				TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
				TwoColumnStyle.m_JustifyContent = EUiAlign::START;
				static thread_local std::vector<SUiLayoutChild> s_vColumns;
				std::vector<SUiLayoutChild> &vColumns = s_vColumns;
				vColumns.assign(2, SUiLayoutChild{});
				vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
				vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
				LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContentBody), vColumns);
				LeftScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
				RightScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
			}
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RedPlayerRows, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RedPlayerRows, RenderState);
		}
		else
		{
			const int NumColumns = 3;
			const int PlayersPerColumn = std::ceil(128.0f / NumColumns);
			{
				CUiV2LayoutEngine LayoutEngine;
				SUiStyle ColumnStyle;
				ColumnStyle.m_Axis = EUiAxis::ROW;
				ColumnStyle.m_AlignItems = EUiAlign::STRETCH;
				ColumnStyle.m_JustifyContent = EUiAlign::START;
				static thread_local std::vector<SUiLayoutChild> s_vColumns;
				std::vector<SUiLayoutChild> &vColumns = s_vColumns;
				vColumns.assign(NumColumns, SUiLayoutChild{});
				for(SUiLayoutChild &Child : vColumns)
				{
					Child.m_Style.m_Width = SUiLength::Flex(1.0f);
				}
				LayoutEngine.ComputeChildren(ColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContentBody), vColumns);
				for(int i = 0; i < NumColumns; ++i)
				{
					CUIRect Column = CUiV2LegacyAdapter::ToCUIRect(vColumns[i].m_Box);
					RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RedPlayerRows, RenderState);
				}
			}
		}
	}

	// 尾部区块（声音条、媒体、观众、页脚、投票、录像提示）排在玩家行之后出场。
	{
		SScoreboardEntryAlphaScope EntryAlpha(m_AnimContentAlpha,
			ScoreboardTextEntryFade(m_TextEntryElapsedSec, SCOREBOARD_TEXT_ENTRY_FOOTER_INDEX, g_Config.m_QmUiMotionLevel, NumPlayers));
		RenderSoundMuteBar(ScoreboardContent);
		m_MediaControls.Render(*Ui(), *TextRender(), GameClient()->m_Tooltips, GameClient()->m_SystemMediaControls, ScoreboardContent,
			ScoreboardUiInteractive && m_MouseUnlocked && !GameClient()->m_HudEditor.IsActive(), m_AnimContentAlpha, ScoreboardUiColorSurface(m_AnimContentAlpha));

		CUIRect Spectators = {ScoreboardContent.x, ScoreboardContent.y + ScoreboardContent.h + 5.0f, ScoreboardContent.w, 100.0f};
		if(pGameInfoObj && (pGameInfoObj->m_ScoreLimit || pGameInfoObj->m_TimeLimit || (pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)))
		{
			CUIRect Goals;
			CUIRect SpectatorRest;
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle ColumnStyle;
			ColumnStyle.m_Axis = EUiAxis::COLUMN;
			ColumnStyle.m_Gap = 5.0f;
			ColumnStyle.m_AlignItems = EUiAlign::STRETCH;
			ColumnStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vChildren;
			std::vector<SUiLayoutChild> &vChildren = s_vChildren;
			vChildren.assign(2, SUiLayoutChild{});
			vChildren[0].m_Style.m_Height = SUiLength::Px(25.0f);
			vChildren[1].m_Style.m_Height = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(ColumnStyle, CUiV2LegacyAdapter::FromCUIRect(Spectators), vChildren);
			Goals = CUiV2LegacyAdapter::ToCUIRect(vChildren[0].m_Box);
			SpectatorRest = CUiV2LegacyAdapter::ToCUIRect(vChildren[1].m_Box);
			Spectators = SpectatorRest;
			RenderGoals(Goals);
		}
		RenderFooter(Spectators);
		GameClient()->m_Voting.RenderScoreboard(ScoreboardContent, ScoreboardUiInteractive && m_MouseUnlocked && !GameClient()->m_HudEditor.IsActive(), m_AnimContentAlpha);

		if(!g_Config.m_ClShowhudTimer)
			RenderRecordingNotification((Screen.w / 7) * 4 + 10);
	}

	if(ScoreboardUiInteractive)
	{
		Ui()->RenderPopupMenus();

		if(m_MouseUnlocked && !QmUiMobilePlatform())
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

		Ui()->FinishCheck();
	}

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevTextOutlineColor);
}

bool CScoreboard::IsActive() const
{
	// if statboard is active don't show scoreboard
	if(GameClient()->m_Statboard.IsActive())
		return false;

	// 自动条件用去抖后的稳定态（由 OnRender 每帧更新），单帧快照噪声不会在这里透传。
	return m_Active || m_AutoActiveStable;
}

bool CScoreboard::IsAutoActive() const
{
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(GameClient()->m_Snap.m_pLocalInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// we are not a spectator, check if we are dead and the game isn't paused
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_QmScoreboardOnDeath &&
			!(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
			return true;
	}

	// if the game is over
	if(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		return true;

	return false;
}

const char *CScoreboard::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(GameClient()->ShouldHideStreamerIdentity(pInfo->m_ClientId))
			return nullptr;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

CUi::EPopupMenuFunctionResult CScoreboard::PopupScoreboard(void *pContext, CUIRect View, bool Active)
{
	CScoreboardPopupContext *pPopupContext = static_cast<CScoreboardPopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;
	CUi *pUi = pPopupContext->m_pScoreboard->Ui();

	CGameClient::CClientData &Client = pScoreboard->GameClient()->m_aClients[pPopupContext->m_ClientId];

	if(!Client.m_Active)
		return CUi::POPUP_CLOSE_CURRENT;

	const float Margin = 5.0f;
	View.Margin(Margin, &View);

	CUIRect Label, Container, Action;
	const float ItemSpacing = 2.0f;
	const float FontSize = 12.0f;

	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle HeaderLayoutStyle;
		HeaderLayoutStyle.m_Axis = EUiAxis::COLUMN;
		HeaderLayoutStyle.m_AlignItems = EUiAlign::STRETCH;
		HeaderLayoutStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vHeaderChildren;
		std::vector<SUiLayoutChild> &vHeaderChildren = s_vHeaderChildren;
		vHeaderChildren.assign(2, SUiLayoutChild{});
		vHeaderChildren[0].m_Style.m_Height = SUiLength::Px(FontSize);
		vHeaderChildren[1].m_Style.m_Height = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(HeaderLayoutStyle, CUiV2LegacyAdapter::FromCUIRect(View), vHeaderChildren);
		Label = CUiV2LegacyAdapter::ToCUIRect(vHeaderChildren[0].m_Box);
		View = CUiV2LegacyAdapter::ToCUIRect(vHeaderChildren[1].m_Box);
	}
	char aNameBuf[MAX_NAME_LENGTH];
	pScoreboard->GameClient()->FormatStreamerName(pPopupContext->m_ClientId, aNameBuf, sizeof(aNameBuf));
	pUi->DoLabel(&Label, aNameBuf, FontSize, TEXTALIGN_ML);

	if(!pPopupContext->m_IsLocal)
	{
		const int ActionsNum = 6;
		const float ActionSize = 25.0f;
		const float ActionSpacing = minimum(17.5f, (View.w - (ActionsNum * ActionSize)) / (ActionsNum - 1));
		const float ActionsWidth = ActionsNum * ActionSize + (ActionsNum - 1) * ActionSpacing;
		int ActionCorners = IGraphics::CORNER_ALL;

		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle SectionStyle;
			SectionStyle.m_Axis = EUiAxis::COLUMN;
			SectionStyle.m_AlignItems = EUiAlign::STRETCH;
			SectionStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vSectionChildren;
			std::vector<SUiLayoutChild> &vSectionChildren = s_vSectionChildren;
			vSectionChildren.assign(3, SUiLayoutChild{});
			vSectionChildren[0].m_Style.m_Height = SUiLength::Px(ItemSpacing * 2.0f);
			vSectionChildren[1].m_Style.m_Height = SUiLength::Px(ActionSize);
			vSectionChildren[2].m_Style.m_Height = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(SectionStyle, CUiV2LegacyAdapter::FromCUIRect(View), vSectionChildren);
			Container = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[1].m_Box);
			View = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[2].m_Box);
		}
		Container.VMargin(maximum(0.0f, (Container.w - ActionsWidth) * 0.5f), &Container);
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ActionRowStyle;
		ActionRowStyle.m_Axis = EUiAxis::ROW;
		ActionRowStyle.m_Gap = ActionSpacing;
		ActionRowStyle.m_AlignItems = EUiAlign::STRETCH;
		ActionRowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vActions;
		std::vector<SUiLayoutChild> &vActions = s_vActions;
		vActions.assign(ActionsNum, SUiLayoutChild{});
		for(SUiLayoutChild &Child : vActions)
		{
			Child.m_Style.m_Width = SUiLength::Px(ActionSize);
		}
		LayoutEngine.ComputeChildren(ActionRowStyle, CUiV2LegacyAdapter::FromCUIRect(Container), vActions);

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[0].m_Box);

		ColorRGBA FriendActionColor = Client.m_Friend ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction)) :
								ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction));
		const char *pFriendActionIcon = pUi->HotItem() == &pPopupContext->m_FriendAction && Client.m_Friend ? FontIcons::FONT_ICON_HEART_CRACK : FontIcons::FONT_ICON_HEART;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_FriendAction, pFriendActionIcon, Client.m_Friend, &Action, BUTTONFLAG_LEFT, ActionCorners, true, FriendActionColor))
		{
			if(Client.m_Friend)
			{
				pScoreboard->GameClient()->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
			}
			else
			{
				pScoreboard->GameClient()->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
			}
		}

		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_FriendAction, &Action, Client.m_Friend ? Localize("Remove friend") : Localize("Add friend"));

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[1].m_Box);

		if(pUi->DoButton_FontIcon(&pPopupContext->m_MuteAction, FontIcons::FONT_ICON_BAN, Client.m_ChatIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_ChatIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_MuteAction, &Action, Client.m_ChatIgnore ? Localize("Unmute") : Localize("Mute"));

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[2].m_Box);

		const char *EmoticonActionIcon = Client.m_EmoticonIgnore ? FontIcons::FONT_ICON_COMMENT_SLASH : FontIcons::FONT_ICON_COMMENT;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_EmoticonAction, EmoticonActionIcon, Client.m_EmoticonIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_EmoticonIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_EmoticonAction, &Action, Client.m_EmoticonIgnore ? Localize("Unmute emoticons") : Localize("Mute emoticons"));

		// 复制玩家名字：把目标玩家名写入剪贴板，不改任何配置；与 ESC 玩家页 Copy name 同源。
		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[3].m_Box);
		const bool CanCopyName = Client.m_aName[0] != '\0';
		if(pUi->DoButton_FontIcon(&pPopupContext->m_CopyNameAction, FontIcons::FONT_ICON_USER, 0, &Action, BUTTONFLAG_LEFT, ActionCorners, CanCopyName) && CanCopyName)
		{
			pScoreboard->Input()->SetClipboardText(Client.m_aName);
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_CopyNameAction, &Action, Localize("Copy name"));

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[4].m_Box);
		const bool Sixup = pScoreboard->Client()->IsSixup();
		const bool CanCopySkin = !Sixup && pScoreboard->Client()->State() == IClient::STATE_ONLINE;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_CopySkinAction, FontIcons::FONT_ICON_EYE_DROPPER, 0, &Action, BUTTONFLAG_LEFT, ActionCorners, CanCopySkin) && CanCopySkin)
		{
			if(QmCopyScoreboardSkin(g_Config, Sixup, Client.m_aSkinName, Client.m_UseCustomColor, Client.m_ColorBody, Client.m_ColorFeet))
			{
				if(g_Config.m_ClDummy)
					pScoreboard->GameClient()->SendDummyInfo(false);
				else
					pScoreboard->GameClient()->SendInfo(false);
			}
		}
		char aSkinName[128];
		str_format(aSkinName, sizeof(aSkinName), Localize("Skin: %s"), Client.m_aSkinName);
		char aSkinTooltip[256];
		if(Sixup)
			str_copy(aSkinTooltip, Localize("Skin copying is only available for 0.6 skins"));
		else
			str_format(aSkinTooltip, sizeof(aSkinTooltip), "%s\n%s", Localize("Copy skin"), aSkinName);
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_CopySkinAction, &Action, aSkinTooltip, 240.0f);

		// 复制皮肤 ID：只把皮肤名写入剪贴板，不改动自身皮肤配置；0.7 玩家同样可用（其 m_aSkinName 为 body 部件名）。
		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[5].m_Box);
		const bool CanCopySkinId = Client.m_aSkinName[0] != '\0';
		if(pUi->DoButton_FontIcon(&pPopupContext->m_CopySkinIdAction, FontIcons::FONT_ICON_COPY, 0, &Action, BUTTONFLAG_LEFT, ActionCorners, CanCopySkinId) && CanCopySkinId)
		{
			pScoreboard->Input()->SetClipboardText(Client.m_aSkinName);
		}
		char aSkinIdTooltip[256];
		str_format(aSkinIdTooltip, sizeof(aSkinIdTooltip), "%s\n%s", Localize("Copy skin ID"), aSkinName);
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_CopySkinIdAction, &Action, aSkinIdTooltip, 240.0f);
	}

	const float ButtonSize = 17.5f;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle SectionStyle;
		SectionStyle.m_Axis = EUiAxis::COLUMN;
		SectionStyle.m_AlignItems = EUiAlign::STRETCH;
		SectionStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vSectionChildren;
		std::vector<SUiLayoutChild> &vSectionChildren = s_vSectionChildren;
		vSectionChildren.assign(3, SUiLayoutChild{});
		vSectionChildren[0].m_Style.m_Height = SUiLength::Px(ItemSpacing * 2.0f);
		vSectionChildren[1].m_Style.m_Height = SUiLength::Px(ButtonSize);
		vSectionChildren[2].m_Style.m_Height = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(SectionStyle, CUiV2LegacyAdapter::FromCUIRect(View), vSectionChildren);
		Container = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[1].m_Box);
		View = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[2].m_Box);
	}

	bool IsSpectating = pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active && pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == pPopupContext->m_ClientId;
	ColorRGBA SpectateButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, (IsSpectating ? 0.25f : 0.5f) * pUi->ButtonColorMul(&pPopupContext->m_SpectateButton));
	if(pUi->DoButton_PopupMenu(&pPopupContext->m_SpectateButton, Localize("Spectate"), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, SpectateButtonColor))
	{
		// 与手机端点整行跟随共用同一套逻辑（切自由视角 / 客户端旁观 / 服务器 /spec）。
		pScoreboard->SpectatePlayer(pPopupContext->m_ClientId);
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CScoreboard::CMapTitlePopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CMapTitlePopupContext *pPopupContext = static_cast<CMapTitlePopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;

	pScoreboard->TextRender()->Text(View.x, View.y, pPopupContext->m_FontSize, pScoreboard->GameClient()->m_aMapDescription, View.w);

	return CUi::POPUP_KEEP_OPEN;
}
