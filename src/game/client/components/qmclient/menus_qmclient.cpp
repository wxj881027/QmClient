#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmIslandNotice.h>
#include <game/client/QmUi/QmIslandSurface.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/QmModuleTypes.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiDogfood.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h>
#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/input_overlay.h>
#include <game/client/components/qmclient/keyword_reply_rules.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_map_upload.h>
#include <game/client/components/qmclient/qm_markdown.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qm_sponsor_authors.h>
#include <game/client/components/qmclient/qm_title_color.h>
#include <game/client/components/qmclient/qm_title_render.h>
#include <game/client/components/qmclient/qm_title_style.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	NUMBER_OF_TCLIENT_TABS
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

[[maybe_unused]] static float s_Time = 0.0f;
[[maybe_unused]] static bool s_StartedTime = false;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
extern bool g_CommandBindCacheInitialized;

namespace
{
	// Visual Deck 需要完整的模块表，才能在切换单张卡片的折叠状态时保留其他 tab 的历史配置。
	const std::array<qm_module::SQmModuleEntry, qm_module::QmModuleCount> s_aQmModuleDefaults = {{{qm_module::EQmModuleId::Info, qm_module::EQmModuleColumn::Full, 0, "info"},
		{qm_module::EQmModuleId::ChatBubble, qm_module::EQmModuleColumn::Left, 0, "chat_bubble"},
		{qm_module::EQmModuleId::SkinAppearance, qm_module::EQmModuleColumn::Left, 1, "skin_appearance"},
		{qm_module::EQmModuleId::SkinTransition, qm_module::EQmModuleColumn::Left, 2, "skin_transition"},
		{qm_module::EQmModuleId::FocusMode, qm_module::EQmModuleColumn::Left, 3, "focus_mode"},
		{qm_module::EQmModuleId::GoresActor, qm_module::EQmModuleColumn::Left, 3, "gores_actor"},
		{qm_module::EQmModuleId::Gores, qm_module::EQmModuleColumn::Left, 4, "gores"},
		{qm_module::EQmModuleId::KeyBinds, qm_module::EQmModuleColumn::Left, 5, "key_binds"},
		{qm_module::EQmModuleId::BetterScoreboard, qm_module::EQmModuleColumn::Left, 6, "better_scoreboard"},
		{qm_module::EQmModuleId::MiniFeatures, qm_module::EQmModuleColumn::Left, 7, "mini_features"},
		{qm_module::EQmModuleId::JumpHint, qm_module::EQmModuleColumn::Left, 7, "jump_hint"},
		{qm_module::EQmModuleId::WeaponTrajectory, qm_module::EQmModuleColumn::Left, 8, "weapon_trajectory"},
		{qm_module::EQmModuleId::Coords, qm_module::EQmModuleColumn::Left, 9, "coords"},
		{qm_module::EQmModuleId::Streamer, qm_module::EQmModuleColumn::Left, 10, "streamer"},
		{qm_module::EQmModuleId::FriendNotify, qm_module::EQmModuleColumn::Left, 11, "friend_notify"},
		{qm_module::EQmModuleId::BlockWords, qm_module::EQmModuleColumn::Left, 12, "block_words"},
		{qm_module::EQmModuleId::Translate, qm_module::EQmModuleColumn::Left, 14, "translate"},
		{qm_module::EQmModuleId::TranslateUi, qm_module::EQmModuleColumn::Left, 15, "translate_ui"},
		{qm_module::EQmModuleId::QiaFen, qm_module::EQmModuleColumn::Left, 13, "qiafen"},
		{qm_module::EQmModuleId::PieMenu, qm_module::EQmModuleColumn::Left, 16, "pie_menu"},
		{qm_module::EQmModuleId::CameraView, qm_module::EQmModuleColumn::Right, 0, "camera_view"},
		{qm_module::EQmModuleId::WeaponAnimation, qm_module::EQmModuleColumn::Right, 1, "weapon_animation"},
		{qm_module::EQmModuleId::EntityOverlay, qm_module::EQmModuleColumn::Right, 2, "entity_overlay"},
		{qm_module::EQmModuleId::Laser, qm_module::EQmModuleColumn::Right, 3, "laser"},
		{qm_module::EQmModuleId::PlayerStats, qm_module::EQmModuleColumn::Right, 4, "player_stats"},
		{qm_module::EQmModuleId::CollisionHitbox, qm_module::EQmModuleColumn::Right, 5, "collision_hitbox"},
		{qm_module::EQmModuleId::HJAssist, qm_module::EQmModuleColumn::Right, 7, "hj_assist"},
		{qm_module::EQmModuleId::DebugGraph, qm_module::EQmModuleColumn::Right, 9, "debug_graph"},
		{qm_module::EQmModuleId::InputOverlay, qm_module::EQmModuleColumn::Right, 10, "input_overlay"},
		{qm_module::EQmModuleId::HudNotifications, qm_module::EQmModuleColumn::Right, 11, "hud_notifications"},
		{qm_module::EQmModuleId::Voice, qm_module::EQmModuleColumn::Right, 12, "voice"},
		{qm_module::EQmModuleId::DummyMiniView, qm_module::EQmModuleColumn::Right, 13, "dummy_miniview"},
		{qm_module::EQmModuleId::DynamicIsland, qm_module::EQmModuleColumn::Right, 14, "dynamic_island"},
		{qm_module::EQmModuleId::SystemMediaControls, qm_module::EQmModuleColumn::Right, 15, "system_media_controls"},
		{qm_module::EQmModuleId::Lyrics, qm_module::EQmModuleColumn::Right, 16, "lyrics"},
		{qm_module::EQmModuleId::Background3D, qm_module::EQmModuleColumn::Right, 17, "background_3d"},
		{qm_module::EQmModuleId::DebugMode, qm_module::EQmModuleColumn::Right, 19, "debug_mode"},
		{qm_module::EQmModuleId::BindStatusHud, qm_module::EQmModuleColumn::Right, 20, "bind_status_hud"},
		{qm_module::EQmModuleId::SoloSplit, qm_module::EQmModuleColumn::Left, 17, "solo_split"},
		// 本地差异：远程把表情卡放在 Left/17、地图上传卡放在 Right/8。
		// 本地 Left/17 已被独有的 SoloSplit 占用，故表情卡追加到 Left/18；
		// Right/8 随速通计时器删除而空出，但地图上传卡仍保持在列尾，
		// 不改动既有卡片的既有顺序。
		{qm_module::EQmModuleId::Emoticons, qm_module::EQmModuleColumn::Left, 18, "emoticons"},
		{qm_module::EQmModuleId::MapUpload, qm_module::EQmModuleColumn::Right, 21, "map_upload"},
		{qm_module::EQmModuleId::Steam, qm_module::EQmModuleColumn::Right, 22, "steam"},
		{qm_module::EQmModuleId::WaterHammerHighlight, qm_module::EQmModuleColumn::Right, 4, "water_hammer"},
		{qm_module::EQmModuleId::GoresDrownBoard, qm_module::EQmModuleColumn::Right, 23, "gores_drown_board"},
		{qm_module::EQmModuleId::Ime, qm_module::EQmModuleColumn::Left, 19, "ime"},
		{qm_module::EQmModuleId::AppearancePreset, qm_module::EQmModuleColumn::Full, 1, "appearance_preset"},
		{qm_module::EQmModuleId::Tooltip, qm_module::EQmModuleColumn::Left, 9, "tooltip"}}};
}

using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;

struct SQmGlobalSearchResults
{
	std::vector<SQmGlobalSearchCard> m_vAllVisibleCards;
};

namespace
{
	void CollectGlobalSearchResults(const char *pSearch, bool Sixup, const qm_card_order::CModel &Model, SQmGlobalSearchResults &Out)
	{
		Out.m_vAllVisibleCards.clear();
		// 无搜索内容时结果为空：搜索页默认只显示搜索输入卡片，不展示全量卡片。
		if(pSearch == nullptr || pSearch[0] == '\0')
			return;
		std::vector<SQmGlobalSearchCard> vCards = qm_card_registry::SearchCards(pSearch, Model);
		Out.m_vAllVisibleCards.reserve(vCards.size());
		for(SQmGlobalSearchCard &Card : vCards)
		{
			const char *pTab = Card.m_Target.m_pTab;
			if(pTab != nullptr && str_comp(pTab, "global-search") == 0)
				continue;
			if(pTab != nullptr && ((Sixup && str_comp(pTab, "tee") == 0) || (!Sixup && str_comp(pTab, "tee7") == 0)))
				continue;
			Out.m_vAllVisibleCards.push_back(std::move(Card));
		}
	}

	bool PerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogQmPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/qmclient", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	[[maybe_unused]] void LogTClientPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/tclient", pStage, DurationMs, Force, nullptr, nullptr, nullptr, pExtra);
	}

	const char *QmSettingsTabName(int Tab)
	{
		switch(Tab)
		{
		case CMenus::QMCLIENT_SETTINGS_TAB_VISUAL: return "visuals";
		case CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION: return "functions";
		case CMenus::QMCLIENT_SETTINGS_TAB_HUD: return "hud";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONTRIBUTORS: return "contributors";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONFIG: return "config";
		case CMenus::QMCLIENT_SETTINGS_TAB_BIND: return "bind";
		default: return "unknown";
		}
	}

	struct SSectionCullContext
	{
		float m_ViewportTop;
		float m_ViewportBottom;
		float m_PrefetchPadding;
	};

	bool IsSectionVisible(const CUIRect &SectionRect, const SSectionCullContext &Context)
	{
		return SectionRect.y + SectionRect.h >= Context.m_ViewportTop - Context.m_PrefetchPadding &&
		       SectionRect.y <= Context.m_ViewportBottom + Context.m_PrefetchPadding;
	}

	uint64_t HashBytesFnv1a64(uint64_t Hash, const void *pData, size_t DataSize)
	{
		const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
		for(size_t i = 0; i < DataSize; ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	template<typename T>
	uint64_t HashValueFnv1a64(uint64_t Hash, const T &Value)
	{
		return HashBytesFnv1a64(Hash, &Value, sizeof(Value));
	}

	uint64_t HashStringFnv1a64(uint64_t Hash, const char *pString)
	{
		return pString == nullptr ? Hash : HashBytesFnv1a64(Hash, pString, str_length(pString));
	}

}

[[maybe_unused]] static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

[[maybe_unused]] static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = std::clamp(Tab, 0, NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1);

	const int PreviousTab = m_QmClientSettingsTab;
	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;

	g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
	m_QmClientSettingsTab = Tab;
	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	RenderSettings(MainView);
	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	m_QmClientSettingsTab = PreviousTab;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

CMenus::SSettingsQmScrollFrame CMenus::BeginSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, CUIRect *pView, float ContentHeight, const SQmSettingsCardStyle &CardStyle, float UiScale, float PreviousOffsetY, bool Enabled)
{
	SSettingsQmScrollFrame Frame;
	Frame.m_ViewRect = *pView;
	Frame.m_ClipRect = *pView;
	Frame.m_PreviousOffsetY = PreviousOffsetY;
	Frame.m_Enabled = Enabled;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, g_Config.m_UiSmoothScrollTime / 1000.0f);
	Frame.m_Style = ScrollPolicy.m_Style;
	(void)CardStyle;
	if(!Enabled)
		return Frame;

	const SQmScrollConfig ScrollConfig = QmSettingsScrollConfig(UiScale, g_Config.m_UiSmoothScrollTime / 1000.0f);

	SQmScrollContainerInput ScrollInput;
	ScrollInput.m_Hovered = Ui()->MouseHovered(pView);
	ScrollInput.m_MouseValid = true;
	ScrollInput.m_MouseX = Ui()->MouseX();
	ScrollInput.m_MouseY = Ui()->MouseY();
	ScrollInput.m_MouseDown = Ui()->MouseButton(0);
	ScrollInput.m_MousePressed = Ui()->MouseButtonClicked(0);

	const SQmScrollContainerFrame ProbeFrame = ScrollContainer.PreviewFrame(ScrollState, *pView, ContentHeight, Frame.m_Style);
	CUIRect WheelHotRect = ProbeFrame.m_ClipRect;
	if(ProbeFrame.m_ScrollbarVisible)
		WheelHotRect.w += Frame.m_Style.m_ScrollbarWidth;
	ScrollInput.m_Hovered = Ui()->MouseHovered(&WheelHotRect);
	ScrollInput.m_ModifierPressed = Input()->ModifierIsPressed();
	ScrollInput.m_AltPressed = Input()->AltIsPressed();

	if(ProbeFrame.m_ScrollbarVisible)
	{
		const void *pScrollbarId = &ScrollContainer;
		ScrollInput.m_ThumbHovered = Ui()->MouseHovered(&ProbeFrame.m_ScrollbarThumbRect);
		ScrollInput.m_TrackHovered = Ui()->MouseHovered(&ProbeFrame.m_ScrollbarTrackRect) && !ScrollInput.m_ThumbHovered;
		if(ScrollInput.m_ThumbHovered || ScrollInput.m_TrackHovered)
			Ui()->SetHotItem(pScrollbarId);
		if((Ui()->HotItem() == pScrollbarId || ScrollInput.m_ThumbHovered || ScrollInput.m_TrackHovered) && ScrollInput.m_MousePressed)
			Ui()->SetActiveItem(pScrollbarId);
		if(Ui()->CheckActiveItem(pScrollbarId))
		{
			ScrollInput.m_ThumbHovered = ScrollInput.m_ThumbHovered || ScrollContainer.ScrollbarDragActive(ScrollState);
			ScrollInput.m_TrackHovered = ScrollInput.m_TrackHovered && !ScrollContainer.ScrollbarDragActive(ScrollState);
			if(!ScrollInput.m_MouseDown)
				Ui()->SetActiveItem(nullptr);
		}
	}

	Frame.m_Frame = ScrollContainer.Update(ScrollState, *pView, ContentHeight, GameClient()->UiRuntimeV2()->FrameDt(), ScrollInput, Frame.m_Style, ScrollConfig);
	Frame.m_ClipRect = Frame.m_Frame.m_ClipRect;
	Frame.m_Offset.y = -Frame.m_Frame.m_Offset;
	*pView = Frame.m_ClipRect;
	Ui()->ClipEnable(&Frame.m_ClipRect);
	return Frame;
}

void CMenus::RenderQmSettingsSliderWithValueInput(const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix, bool PrewarmOnly, unsigned Flags)
{
	const int OriginalValue = *pValue;
	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(pId);
	ui_widget::SNumericFieldOptions Options;
	Options.m_Flags = Flags;
	Options.m_pSuffix = pSuffix;
	Options.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
	Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;

	const float UiScale = std::clamp(ControlColumn.h / ui_token::settings::ROW_HEIGHT, 0.78f, 1.0f);
	IUiContext InputCtx = SettingsUiContext("qmclient_slider_input", UiScale);
	if(PrewarmOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}
	ui_widget::NumericField(InputCtx, pState, pId, pValue, MinValue, MaxValue, ControlColumn, Options);
	if(PrewarmOnly || Ui()->RenderOnly())
		*pValue = OriginalValue;
}

// 被禅模式/Gores 等临时接管的配置项：设置页灰化显示并提示接管来源；未被接管返回 nullptr。
static const char *QmTemporaryOverrideTooltip(const char *pOwnerId)
{
	if(pOwnerId == nullptr)
		return nullptr;
	if(str_comp(pOwnerId, "qm_zen_mode") == 0)
		return Localize("Controlled by Zen mode");
	if(str_comp(pOwnerId, "qm_gores_mode") == 0)
		return Localize("Controlled by Gores mode");
	return Localize("Temporarily controlled by a mode toggle");
}

const char *CMenus::TemporaryOverrideTooltip(const int *pValue) const
{
	return QmTemporaryOverrideTooltip(ConfigManager() != nullptr ? ConfigManager()->SaveValueOverrideOwner(pValue) : nullptr);
}

bool CMenus::RenderQmFunctionCheckbox(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly, const char *pTooltip)
{
	const int OriginalValue = *pValue;
	const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
	SLabelProperties LabelProps;
	if(pOverrideTooltip != nullptr)
	{
		LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
		// 灰化行用 ProcessInput=false 绘制，不会自己占 hover；补一次只读的按钮逻辑
		// 让 HotItem 指向本行，CTooltips 才会激活提示（返回值丢弃，不写值）。
		if(!PrewarmOnly && !Ui()->RenderOnly())
		{
			Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(pId, pRect, pOverrideTooltip);
		}
	}
	// 被临时接管的项灰化并停止响应点击：接管期间用户改它会被接管逻辑覆盖。
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pId, pTextId, pText, *pValue, pRect, LabelProps, pOverrideTooltip == nullptr) != 0;
	if(Changed)
		*pValue ^= 1;
	if(pTooltip != nullptr)
		GameClient()->m_Tooltips.DoToolTip(pId, pRect, pTooltip);
	if(PrewarmOnly || Ui()->RenderOnly())
		*pValue = OriginalValue;
	return Changed;
}

bool CMenus::RenderQmVisualCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
	SLabelProperties LabelProps;
	if(pOverrideTooltip != nullptr)
	{
		LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
		// 灰化行不占 hover，补一次只读的按钮逻辑让提示能激活（返回值丢弃，不写值）。
		if(!Ui()->RenderOnly())
		{
			Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(pId, &Row, pOverrideTooltip);
		}
	}
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pId, pTextId, pText, *pValue, &Row, LabelProps, pOverrideTooltip == nullptr) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmVisualLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign, const SLabelProperties &LabelProps)
{
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
}

void CMenus::FinishSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, SSettingsQmScrollFrame &Frame, const CUIRect &EndRect, float *pContentHeight, float *pPreviousOffsetY, bool TrackScrollActive)
{
	if(!Frame.m_Enabled)
		return;

	Ui()->ClipDisable();
	*pContentHeight = maximum(0.0f, std::ceil(EndRect.y + EndRect.h - (Frame.m_ClipRect.y + Frame.m_Offset.y)));
	Frame.m_Frame = ScrollContainer.PreviewFrame(ScrollState, Frame.m_ViewRect, *pContentHeight, Frame.m_Style);
	CUIRect WheelHotRect = Frame.m_Frame.m_ClipRect;
	if(Frame.m_Frame.m_ScrollbarVisible)
		WheelHotRect.w += Frame.m_Style.m_ScrollbarWidth;
	const void *pWheelOwnerId = &ScrollContainer;
	const bool WheelEligible = Frame.m_Frame.m_ScrollbarVisible && !Ui()->UnderlyingScrollBlocked() && Ui()->MouseHovered(&WheelHotRect);
	Ui()->RegisterWheelOwner(pWheelOwnerId, EUiWheelOwnerPriority::PAGE, WheelHotRect, WheelEligible);
	float WheelDelta = 0.0f;
	if(Ui()->TryConsumeWheel(pWheelOwnerId, &WheelDelta))
	{
		const SQmScrollConfig ScrollConfig = QmSettingsScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
		ScrollContainer.ScrollByWheel(ScrollState, WheelDelta, Frame.m_ViewRect.h, *pContentHeight, ScrollConfig);
		Frame.m_Frame = ScrollContainer.PreviewFrame(ScrollState, Frame.m_ViewRect, *pContentHeight, Frame.m_Style);
	}
	const float CurrentOffsetY = Frame.m_Frame.m_ScrollbarVisible ? Frame.m_Frame.m_Offset : 0.0f;
	if(TrackScrollActive)
	{
		m_SettingsScrollActive = m_SettingsScrollActive || absolute(CurrentOffsetY - Frame.m_PreviousOffsetY) > 0.01f;
		if(pPreviousOffsetY != nullptr)
			*pPreviousOffsetY = CurrentOffsetY;
	}
	if(Frame.m_Frame.m_ScrollbarVisible)
	{
		DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarTrackRect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), ColorRGBA(), Frame.m_Frame.m_ScrollbarTrackRect.w * 0.5f);
		DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarThumbRect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.34f), ColorRGBA(), Frame.m_Frame.m_ScrollbarThumbRect.w * 0.5f);
	}
}

bool CMenus::RenderQmHudCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, &Row) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

bool CMenus::HandleQmHudCheckboxInput(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	const CUIRect VisibleContent = Content;
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const CUIRect HitRect = Row.Intersection(VisibleContent);
	const bool Changed = HitRect.w > 0.0f && HitRect.h > 0.0f && Ui()->DoButtonLogic(pId, *pValue, &HitRect, BUTTONFLAG_LEFT) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmHudLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign, const SLabelProperties &LabelProps)
{
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
}

void CMenus::RenderQmHudKeyBindRow(CUIRect &Content, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pLabel, const char *pCommand, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto CurrentBindIt = g_CommandBindCache.find(pCommand);
	if(CurrentBindIt != g_CommandBindCache.end())
		Bind = CurrentBindIt->second;

	CUIRect BindRow, BindLabel, BindKey;
	Content.HSplitTop(LineHeight, &BindRow, &Content);
	BindRow.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	Ui()->DoLabel(&BindLabel, pLabel, BodySize, TEXTALIGN_ML);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &BindKey, Bind, false);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign(std::string(pCommand), Result.m_Bind);
		}
		else
		{
			g_CommandBindCache.erase(pCommand);
		}
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

bool CMenus::ToggleQmHudCountdownLocation(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	// 本地差异：远程此入口只做按钮逻辑、不绘制标签（其卡片模块也未另画标签），
	// 而本地既有实现带可见文案（"Follow Tee" / "Show in Dynamic Island"）。
	// 按控件 id 选择对应标签，保留本地界面文案，避免迁出后标签消失。
	if(pId == qm_card_catalog::SwitchCountdownFollowTeeId())
		return RenderQmHudCheckbox(Content, LineHeight, LineSpacing, pId, "qmclient-switch-countdown-follow-tee", Localize("Follow Tee"), pValue);
	return RenderQmHudCheckbox(Content, LineHeight, LineSpacing, pId, "qmclient-switch-countdown-media-island", Localize("Show in Dynamic Island"), pValue);
}

void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_hud", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 注意：目录的 Hud 清单含独立的 qm:lyrics 卡（本地此前把歌词画在 SMTC 卡内，已在上方移出），
	// 故切换后 HUD 页会多出一张歌词卡——这是远程的结构意图，注册表与布局表本地早已具备。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::HudCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_HudPrewarmOrderModel;
	static bool s_HudPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_HudPrewarmDeck;
	if(ReadOnly && !s_HudPrewarmOrderModelInitialized)
	{
		s_HudPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_HudPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_HudPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_HudPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "hud", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

// 原页和搜索页从卡片模块取得相同的编辑状态与布局版本。
static qm_card_catalog::SQmFunctionCardLayoutState ResolveFunctionCardLayoutState(size_t FavoriteMapCount)
{
	qm_card_catalog::SQmFunctionCardLayoutState State;
	State.m_BlockWordsRevision = qm_card_catalog::BlockWordsLayoutRevision();
	State.m_FavoriteMapsRevision = qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
	qm_card_catalog::FillKeywordReplyLayoutState(State);
	return State;
}

void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_function", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
	qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
	auto ToggleCollapsed = [](void *pUser, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		const bool WasCollapsed = s_aCollapsed[Index];
		const bool Collapsed = !WasCollapsed;
		if(!qm_card_collapse::SetQmModuleCollapsed(Id, Collapsed))
			return;
		s_aCollapsed[Index] = Collapsed;
		if(Id == EQmModuleId::FavoriteMaps && WasCollapsed != Collapsed && !Collapsed)
		{
			const size_t FavoriteMapCount = static_cast<CMenus *>(pUser)->GameClient()->TClientComponent().GetFavoriteMaps().size();
			qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
		}
	};
	auto MeasureContentRevision = [this](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id, ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size()));
	};
	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 内容量状态（词条过滤/关键词回复/收藏地图）经 m_pFunctionLayout 注入，目录测量依赖它。
	const qm_card_catalog::SQmFunctionCardLayoutState FunctionCardLayout = ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size());
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pFunctionLayout = &FunctionCardLayout;
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		CardBuild.m_pToggleCollapsedUser = this;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::FunctionCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_FunctionPrewarmOrderModel;
	static bool s_FunctionPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_FunctionPrewarmDeck;
	if(ReadOnly && !s_FunctionPrewarmOrderModelInitialized)
	{
		s_FunctionPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_FunctionPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_FunctionPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_FunctionPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "function", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_visual", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::VisualCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_VisualPrewarmOrderModel;
	static bool s_VisualPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_VisualPrewarmDeck;
	if(ReadOnly && !s_VisualPrewarmOrderModelInitialized)
	{
		s_VisualPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_VisualPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_VisualPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_VisualPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "visual", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsGlobalSearch(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsGlobalSearchContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsQmClient(CUIRect MainView, bool PrewarmOnly)
{
	// 文本计划收集等 RenderOnly 场景也要保证绑定缓存可用（幂等）。
	EnsureSettingsBindCache();

	RenderSettingsQmClientContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float SmallSize = Metrics.m_SmallSize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext SearchCtx = SettingsUiContext("settings_global_search", UiScale);
	if(ReadOnly)
	{
		SearchCtx.m_pAnim = nullptr;
		SearchCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_GlobalSearchScrollRegion;
	static std::array<bool, qm_module::QmModuleCount> s_aGlobalSearchCollapsed = {};
	static std::array<CButtonContainer, qm_module::QmModuleCount> s_aGlobalSearchCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aGlobalSearchCollapsed);
	auto ToggleSearchCollapsed = [](void *pUser, qm_module::EQmModuleId Id) {
		bool *pCollapsed = static_cast<bool *>(pUser);
		if(pCollapsed == nullptr)
			return;
		const int Index = std::clamp((int)Id, 0, (int)qm_module::QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !pCollapsed[Index]))
			pCollapsed[Index] = !pCollapsed[Index];
	};

	CLineInputBuffered<128> &ModuleSearchInput = m_GlobalCardSearchInput;
	const char *pModuleSearch = ModuleSearchInput.GetString();
	struct SGlobalSearchCache
	{
		bool m_Valid = false;
		std::string m_Search;
		std::string m_Language;
		bool m_Sixup = false;
		uint64_t m_TextGeneration = 0;
		uint64_t m_LayoutRevision = 0;
		SQmGlobalSearchResults m_Results;
		qm_card_order::CModel m_Model;
	};
	static SGlobalSearchCache s_GlobalSearchCache;
	static qm_card_order::CModel s_GlobalSearchPrewarmOrderModel;
	static CSettingsCardDeck s_GlobalSearchPrewarmDeck;
	static qm_card_catalog::SQmCardBuildContext s_GlobalSearchCardBuild;
	static qm_card_catalog::SQmFunctionCardLayoutState s_GlobalSearchFunctionCardLayout;
	static std::unordered_map<std::string, CButtonContainer> s_GlobalSearchActionButtons;
	// 分类布局只用于解析搜索和导航；以下两个模型仅属于搜索页，不持久化。
	const qm_card_order::CModel &CardOrderModel = SettingsCardOrderModel();
	qm_card_order::CModel &DeckOrderModel = ReadOnly ? s_GlobalSearchPrewarmOrderModel : s_GlobalSearchCache.m_Model;
	CSettingsCardDeck &CardDeck = ReadOnly ? s_GlobalSearchPrewarmDeck : m_SettingsCardDeck;
	const char *pLanguage = g_Config.m_ClLanguagefile;
	const uint64_t LayoutRevision = CardOrderModel.LayoutRevision();
	if(!s_GlobalSearchCache.m_Valid || s_GlobalSearchCache.m_Search != (pModuleSearch != nullptr ? pModuleSearch : "") || s_GlobalSearchCache.m_Language != (pLanguage != nullptr ? pLanguage : "") || s_GlobalSearchCache.m_Sixup != Client()->IsSixup() || s_GlobalSearchCache.m_LayoutRevision != LayoutRevision || s_GlobalSearchCache.m_TextGeneration != m_MenuTextPoolGeneration)
	{
		s_GlobalSearchCache.m_Search = pModuleSearch != nullptr ? pModuleSearch : "";
		s_GlobalSearchCache.m_Language = pLanguage != nullptr ? pLanguage : "";
		s_GlobalSearchCache.m_Sixup = Client()->IsSixup();
		s_GlobalSearchCache.m_LayoutRevision = LayoutRevision;
		s_GlobalSearchCache.m_TextGeneration = m_MenuTextPoolGeneration;
		CollectGlobalSearchResults(pModuleSearch, s_GlobalSearchCache.m_Sixup, CardOrderModel, s_GlobalSearchCache.m_Results);
		auto &vResults = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vResults.size() + 1);
		// 搜索输入卡片不再进入卡组：它脱离滚动流手动绘制（默认居中，搜索后置顶）。
		vEntries.push_back({"deck:global-search-empty", "global-search", 0, -1});
		for(size_t Index = 0; Index < vResults.size(); ++Index)
		{
			// 搜索结果卡片使用半宽：交替放入左右两列（0=整宽保留给搜索输入与空态卡片）。
			const int ResultColumn = Index % 2 == 0 ? 1 : 2;
			vEntries.push_back({vResults[Index].m_pStableId, "global-search", ResultColumn, (int)(Index / 2)});
		}
		s_GlobalSearchCache.m_Model.SetEntries(vEntries);
		s_GlobalSearchPrewarmOrderModel.SetEntries(std::move(vEntries));
		s_GlobalSearchCache.m_Valid = true;
	}
	const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
	const int SearchMatchedGlobalCardCount = (int)SearchVisibleGlobalCards.size();
	s_GlobalSearchFunctionCardLayout = ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size());
	qm_card_catalog::SQmCardBuildContext StandardPrepare;
	StandardPrepare.m_pMenus = this;
	StandardPrepare.m_ReadOnly = ReadOnly;
	StandardPrepare.m_Page = Page;
	StandardPrepare.m_Metrics = Metrics;
	StandardPrepare.m_UiContext = SearchCtx;
	StandardPrepare.m_pScrollRegion = ReadOnly ? nullptr : &s_GlobalSearchScrollRegion;
	std::vector<const char *> vStandardIds;
	vStandardIds.reserve(SearchVisibleGlobalCards.size());
	for(const auto &Card : SearchVisibleGlobalCards)
		vStandardIds.push_back(Card.m_pStableId);
	uint64_t CardLayoutRevision = str_quickhash("global-search");
	CardLayoutRevision ^= qm_card_catalog::QmCardRenderHook::PrepareStandardCards(StandardPrepare, vStandardIds);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::QmCardRenderHook::PrepareTClientCards(StandardPrepare, vStandardIds);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(pModuleSearch != nullptr ? pModuleSearch : "");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ LayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Client()->IsSixup() ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureModuleCardsRevision(s_GlobalSearchFunctionCardLayout);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ TranslationTestLayoutRevision();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureContentRevision(g_Localization.Languages().size(), GameClient()->m_MenuBackground.GetThemes().size());
	if(std::any_of(SearchVisibleGlobalCards.begin(), SearchVisibleGlobalCards.end(), [](const SQmGlobalSearchCard &Card) { return str_startswith(Card.m_pStableId, "deck:controls-") != nullptr; }))
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::QmCardRenderHook::PrepareControlsCards(this, MainView.w, ReadOnly, ReadOnly ? nullptr : &s_GlobalSearchScrollRegion);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_BlockWordsRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_KeywordRulesRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_FavoriteMapsRevision;
	for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(Card.m_pStableId);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	s_GlobalSearchCardBuild = {};
	s_GlobalSearchCardBuild.m_pMenus = this;
	s_GlobalSearchCardBuild.m_ReadOnly = ReadOnly;
	s_GlobalSearchCardBuild.m_Page = Page;
	s_GlobalSearchCardBuild.m_Metrics = Metrics;
	s_GlobalSearchCardBuild.m_LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	s_GlobalSearchCardBuild.m_UiContext = SearchCtx;
	s_GlobalSearchCardBuild.m_pScrollRegion = ReadOnly ? nullptr : &s_GlobalSearchScrollRegion;
	s_GlobalSearchCardBuild.m_Padding = CardStyle.m_Padding;
	s_GlobalSearchCardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
	s_GlobalSearchCardBuild.m_pCollapsed = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pCollapseButtons = s_aGlobalSearchCollapseButtons.data();
	s_GlobalSearchCardBuild.m_pToggleCollapsed = ToggleSearchCollapsed;
	s_GlobalSearchCardBuild.m_pToggleCollapsedUser = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pFunctionLayout = &s_GlobalSearchFunctionCardLayout;

	const bool HasSearchQuery = pModuleSearch != nullptr && pModuleSearch[0] != '\0';
	// s_GlobalSearchCardBuild 是函数内静态对象，直接引用即可，无需附加捕获。
	const auto BuildDefinitions = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, HasSearchQuery, ReadOnly, SearchCtx, &SearchVisibleGlobalCards](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(SearchMatchedGlobalCardCount + 1);
		// 搜索输入卡片已脱离卡组，在 RenderCached 之前手动绘制并定位。

		SSettingsCardDefinition EmptyCard;
		EmptyCard.m_Spec = {"deck:global-search-empty", Localize("Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-results")};
		EmptyCard.m_Measure = [LineHeight](float) { return LineHeight; };
		EmptyCard.m_Render = [this, SmallSize, LineHeight](CUIRect Content) {
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			DoSettingsMenuLabel(SETTINGS_SEARCH, -1, -1, "qmclient-search-no-matching-features", &Row, Localize("No matching features found. Try other keywords"), SmallSize, TEXTALIGN_ML, {}, (int)Row.w);
		};
		EmptyCard.m_IsVisible = [HasSearchQuery, SearchMatchedGlobalCardCount] { return HasSearchQuery && SearchMatchedGlobalCardCount == 0; };
		vCards.push_back(std::move(EmptyCard));

		for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		{
			SSettingsCardDefinition Definition;
			if(!qm_card_catalog::BuildCard(s_GlobalSearchCardBuild, Card.m_pStableId, Definition))
			{
				const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(Card.m_pStableId);
				Definition.m_Spec = {Card.m_pStableId, pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : Localize("Global card"), qm_card_registry::ResolveLocalizedDescription(Card.m_pStableId)};
				// 兜底卡无法复用原页面的卡片构建器（构建闭包绑定在各自页面），
				// 退化为「描述 + 定位按钮」的导航卡，而不是只有一行定位。
				Definition.m_Measure = [LineHeight, LineSpacing](float) { return LineHeight + LineSpacing * 0.65f + LineHeight; };
				CButtonContainer *pOpenButton = &s_GlobalSearchActionButtons[std::string("open:") + Card.m_pStableId];
				Definition.m_Render = [this, Description = Card.m_Description, Target = Card.m_Target, pOpenButton, ReadOnly, LineHeight, LineSpacing, SmallSize](CUIRect Content) {
					CUIRect Row;
					Content.HSplitTop(LineHeight, &Row, &Content);
					SLabelProperties DescProps;
					DescProps.m_MaxWidth = Row.w;
					DescProps.m_EllipsisAtEnd = true;
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
					Ui()->DoLabel(&Row, Description.c_str(), SmallSize, TEXTALIGN_ML, DescProps);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
					Content.HSplitTop(LineHeight, &Row, &Content);
					const char *pLabel = Localize("Locate");
					const float TextWidth = TextRender()->TextWidth(SmallSize, pLabel);
					CUIRect LocateButton = {Row.x, Row.y, std::min(Row.w, TextWidth + 16.0f), Row.h};
					if(LocateButton.w <= 0.0f)
						return;
					if(!ReadOnly && Ui()->DoButtonLogic(pOpenButton, 0, &LocateButton, BUTTONFLAG_LEFT))
					{
						NavigateToSettingsCard(Target);
						Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
						m_GlobalCardSearchInput.Deactivate();
						return;
					}
					// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
					const bool Hovered = !ReadOnly && Ui()->MouseHovered(&LocateButton);
					const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, Hovered ? 0.28f : 0.18f);
					DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, 5.0f);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				};
				vCards.push_back(std::move(Definition));
				continue;
			}
			CButtonContainer *pLocateButton = &s_GlobalSearchActionButtons[std::string("locate:") + Card.m_pStableId];
			Definition.m_LeadingHeaderActionWidth = TextRender()->TextWidth(SmallSize, Localize("Locate")) + 16.0f * UiScale;
			Definition.m_LeadingHeaderAction = [this, Target = Card.m_Target, pLocateButton, ReadOnly, UiScale, SmallSize, SearchCtx](const SSettingsCardFrame &Frame, bool) {
				if(ReadOnly)
					return;
				// 定位使用 Deck 预留的最左操作区，不再覆盖宽度或说明按钮。
				const char *pLabel = Localize("Locate");
				const CUIRect LocateButton = Frame.m_LeadingHeaderActionRect;
				if(LocateButton.w <= 0.0f || LocateButton.h <= 0.0f)
					return;
				if(Ui()->DoButtonLogic(pLocateButton, 0, &LocateButton, BUTTONFLAG_LEFT))
				{
					NavigateToSettingsCard(Target);
					Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
					m_GlobalCardSearchInput.Deactivate();
					return;
				}
				// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
				const bool Hovered = Ui()->MouseHovered(&LocateButton);
				const float Radius = std::min(ui_token::radius::TIGHT * UiScale, std::min(LocateButton.w, LocateButton.h) * 0.25f);
				const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, Hovered ? 0.28f : 0.18f);
				if(LocateButton.w < TextRender()->TextWidth(SmallSize, pLabel) + 16.0f * UiScale)
				{
					RenderSettingsCardHeaderIcon(SearchCtx, LocateButton, EQmIcon::ARROW_RIGHT, FontIcons::FONT_ICON_CHEVRON_RIGHT, 1.0f, pLocateButton);
					GameClient()->m_Tooltips.DoSmallToolTip(pLocateButton, &LocateButton, pLabel, SmallSize);
				}
				else
				{
					DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, Radius);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				}
			};
			vCards.push_back(std::move(Definition));
		}
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	const SCardMotionSpec Motion = SettingsCardMotionSpec();

	// —— 搜索输入卡片：脱离卡组手动绘制 ——
	// 无搜索内容时在视口内垂直居中；输入内容后置顶并把下方空间让位给结果卡片，
	// 两种状态间的过渡沿用卡组让位动画的时长与缓动。
	static CButtonContainer s_GlobalSearchInputCollapseButton;
	static bool s_GlobalSearchInputCollapsed = false;
	const float InputContentHeight = s_GlobalSearchInputCollapsed ? 0.0f : 2.0f * LineHeight + LineSpacing;
	const SSettingsCardSpec InputSpec{"deck:global-search-input", Localize("Feature Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-input")};
	const float InputChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, Page.m_ContentViewport.w, 0.0f}, InputSpec, InputContentHeight, UiScale).m_Rect.h;
	const float CenteredOffsetY = std::max(0.0f, (Page.m_ContentViewport.h - InputChromeHeight) * 0.5f);
	const float TargetOffsetY = HasSearchQuery ? 0.0f : CenteredOffsetY;
	float InputOffsetY = TargetOffsetY;
	CUiV2AnimationRuntime *pSearchAnimRuntime = ReadOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	if(pSearchAnimRuntime != nullptr)
	{
		const uint64_t InputOffsetKey = BuildUiAnimNodeKey(str_quickhash("global-search"), str_quickhash("global-search-input-offset"));
		InputOffsetY = ResolveUiAnimValue(*pSearchAnimRuntime, InputOffsetKey, EUiAnimProperty::POS_Y, TargetOffsetY, Motion.m_ReflowDuration, EEasing::EASE_OUT);
	}
	{
		const CUIRect InputSlot{Page.m_ContentViewport.x, Page.m_ContentViewport.y + InputOffsetY, Page.m_ContentViewport.w, 0.0f};
		SSettingsCardVisualState InputVisualState;
		InputVisualState.m_Collapsed = s_GlobalSearchInputCollapsed;
		InputVisualState.m_ClipContent = true;
		const auto RenderInputContent = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, ReadOnly](CUIRect Content) {
			if(Content.h <= 0.0f || Content.w <= 0.0f)
				return;
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			IUiContext InputCtx = SettingsUiContext("settings_global_search", UiScale);
			if(ReadOnly)
			{
				InputCtx.m_pAnim = nullptr;
				InputCtx.m_pTree = nullptr;
			}
			ui_widget::InputField(InputCtx, &m_GlobalCardSearchInput, Row, BodySize, !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			char aSearchHint[64];
			str_format(aSearchHint, sizeof(aSearchHint), Localize("Found %d global cards"), SearchMatchedGlobalCardCount);
			TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
			Ui()->DoLabel(&Row, aSearchHint, SmallSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		};
		const auto InputHeaderAction = [this, ReadOnly, UiScale, SearchCtx](const SSettingsCardFrame &Frame, bool) mutable {
			if(ReadOnly)
				return;
			if(Ui()->DoButtonLogic(&s_GlobalSearchInputCollapseButton, 0, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
				s_GlobalSearchInputCollapsed = !s_GlobalSearchInputCollapsed;
			RenderSettingsCardCollapseButton(SearchCtx, Frame.m_HandleRect, s_GlobalSearchInputCollapsed, 1.0f, &s_GlobalSearchInputCollapseButton);
		};
		SettingsCard(SearchCtx, InputSlot, InputSpec, InputVisualState, SettingsCardDeckVisualOptions(), [InputContentHeight](float) { return InputContentHeight; }, RenderInputContent, InputHeaderAction);
	}

	// 结果卡组从输入卡片底部（含动画中的位置）开始，让位动画期间跟随卡片一起上移。
	SSettingsPageLayoutFrame DeckPage = Page;
	{
		const float DeckTopDelta = InputOffsetY + InputChromeHeight + Page.m_CardGap;
		const auto ShiftRectDown = [DeckTopDelta](CUIRect &Rect) {
			Rect.y += DeckTopDelta;
			Rect.h = std::max(0.0f, Rect.h - DeckTopDelta);
		};
		ShiftRectDown(DeckPage.m_ScrollViewport);
		ShiftRectDown(DeckPage.m_UnreservedScrollViewport);
		ShiftRectDown(DeckPage.m_ContentViewport);
		ShiftRectDown(DeckPage.m_aColumns[0]);
		ShiftRectDown(DeckPage.m_aColumns[1]);
	}

	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = false;
	// 搜索结果按匹配顺序排列，不修改分类页的持久布局。
	InputState.m_AllowHeaderDrag = false;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	CardDeck.RenderCached(SearchCtx, DeckPage, "global-search", DefinitionsRevision, BuildDefinitions, DeckOrderModel, ReadOnly ? nullptr : &s_GlobalSearchScrollRegion, InputState, Motion, SettingsCardDeckVisualOptions());
}

void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;

	// feat-003 dogfood: when dbg_qm_ui_dogfood is on, take over the QmClient
	// settings panel and render the widget gallery. First visible verification
	// of feat-002 (animation runtime) + feat-003 (tokens + 11 widgets).
	if(g_Config.m_DbgQmUiDogfood != 0)
	{
		if(PrewarmOnly)
			return;

		IUiContext Ctx;
		Ctx.m_pUi = Ui();
		Ctx.m_pMenus = this;
		Ctx.m_pTextRender = TextRender();
		Ctx.m_pTooltips = &GameClient()->m_Tooltips;
		Ctx.m_pAnim = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
		Ctx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
		Ctx.m_ScopeHash = MakeUiScopeHash("qm_ui_dogfood");
		Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		RenderQmUiDogfood(Ctx, MainView);
		return;
	}

	CPerfTimer RenderTimer;
	const float QmClientUiScale = ResolveSettingsContentMetrics(MainView.w).m_UiScale;
	bool TabTransitionActive = false;
	static bool s_QmTabTelemetryInitialized = false;
	static int s_PrevQmTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	CUIRect TabContentClip = MainView;

	{
		if(m_QmClientSettingsTab < 0 || m_QmClientSettingsTab >= NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
		// 贡献者子页签已并入顶层「贡献者」页；旧持久化索引占位值回落到首个子页签。
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONTRIBUTORS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;

		CUIRect TabBar;
		const SSettingsSubTabLayoutFrame QmClientSubTabs = ResolveSettingsSubTabLayout(MainView, QmClientUiScale);
		TabBar = QmClientSubTabs.m_TabBarRect;
		MainView = QmClientSubTabs.m_ContentRect;
		// 贡献者枚举保留占位以兼容旧配置，可见页签使用独立列表。
		constexpr int aVisibleQmTabs[] = {QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_CONFIG, QMCLIENT_SETTINGS_TAB_BIND};
		constexpr int NumQmTabs = (int)std::size(aVisibleQmTabs);
		const float TabWidth = TabBar.w / (float)NumQmTabs;
		static CButtonContainer s_aPageTabs[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize("Visuals");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize("Functions");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize("HUD");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize("Config");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_BIND] = Localize("Bind");

		{
			CPerfTimer StageTimer;
			// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签文字 —— 滑块压在文字之下。
			CUIRect aQmTabSlots[NumQmTabs];
			CUIRect QmTabsRemainder = TabBar;
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
				QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);
			int ActiveQmTabSlot = -1;
			for(int Slot = 0; Slot < NumQmTabs; ++Slot)
			{
				if(aVisibleQmTabs[Slot] == m_QmClientSettingsTab)
					ActiveQmTabSlot = Slot;
			}
			const IUiContext QmTabBarCtx = TabBarUiContext();
			ui_widget::CapsuleTabBarChrome(QmTabBarCtx, MakeUiScopeHash("settings_qmclient_tabs_capsule"), ui_widget::CapsuleTabBarRowRect(aQmTabSlots, NumQmTabs), ActiveQmTabSlot >= 0 ? &aQmTabSlots[ActiveQmTabSlot] : nullptr, SettingsCapsuleTabBarStyle());
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
			{
				const int PageTab = aVisibleQmTabs[Tab];
				const bool ClickedTab = DoButton_MenuTab(&s_aPageTabs[PageTab], apQmTabNames[PageTab], m_QmClientSettingsTab == PageTab, &aQmTabSlots[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
				if(!PrewarmOnly && ClickedTab)
					m_QmClientSettingsTab = PageTab;
			}

			char aTabExtra[96];
			str_format(aTabExtra, sizeof(aTabExtra), "tab=%s", QmSettingsTabName(m_QmClientSettingsTab));
			LogQmPerfStage(Client(), "tabbar", StageTimer.ElapsedMs(), false, aTabExtra);
		}

		if(!PrewarmOnly)
		{
			if(!s_QmTabTelemetryInitialized)
			{
				s_PrevQmTab = m_QmClientSettingsTab;
				s_QmTabTelemetryInitialized = true;
			}
			else if(m_QmClientSettingsTab != s_PrevQmTab)
			{
				if(PerfDebugEnabled())
				{
					char aPayload[128];
					str_format(aPayload, sizeof(aPayload), "event=tab_switch from=%s to=%s", QmSettingsTabName(s_PrevQmTab), QmSettingsTabName(m_QmClientSettingsTab));
					QmPerfLogPayload("perf/qmclient", aPayload, Client(), CurrentQmUiPerfPage());
				}
				s_PrevQmTab = m_QmClientSettingsTab;
			}
		}

		CUIRect ContentView = MainView;
		// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面局部状态叠加。
		TabTransitionActive = false;
		TabContentClip = MainView;
		if(TabTransitionActive)
		{
			Ui()->ClipEnable(&TabContentClip);
		}

		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONFIG)
		{
			CPerfTimer StageTimer;
			if(!PrewarmOnly)
				RenderSettingsTClientConfigs(ContentView);
			char aConfigExtra[96];
			str_format(aConfigExtra, sizeof(aConfigExtra), "tab=%s transition=%d", QmSettingsTabName(m_QmClientSettingsTab), TabTransitionActive ? 1 : 0);
			LogQmPerfStage(Client(), "config_tab_total", StageTimer.ElapsedMs(), TabTransitionActive, aConfigExtra);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			LogQmPerfStage(Client(), "render_total", RenderTimer.ElapsedMs(), false, aConfigExtra);
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_BIND)
		{
			RenderSettingsQmClientBindDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_VISUAL)
		{
			RenderSettingsQmClientVisualDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_FUNCTION)
		{
			RenderSettingsQmClientFunctionDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_HUD)
		{
			RenderSettingsQmClientHudDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		MainView = ContentView;
	}
}

std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
bool g_CommandBindCacheInitialized = false;

void CMenus::ClearQmClientSettingsSearchInputs()
{
	if(Ui()->ActiveItem() == &m_GlobalCardSearchInput)
	{
		Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
	}
	else
	{
		m_GlobalCardSearchInput.Deactivate();
	}
	m_GlobalCardSearchInput.Clear();
}

void CMenus::RenderSponsorNudge(CUIRect Screen)
{
	CGameClient *pGameClient = GameClient();
	// 两种触发共用同一套灵动岛表现：启动提醒，以及关掉提醒后的那句问话。
	const bool Visible = pGameClient->SponsorNudgeVisible() || pGameClient->SponsorNudgeFarewellActive();
	// 出场动画收完之前必须继续画，否则「收缩成黑球再上滑」会退化成瞬间消失。
	if(!qm_island::NeedsRender(m_QmSponsorNudgeNotice, Visible))
	{
		qm_island::Reset(m_QmSponsorNudgeNotice);
		return;
	}
	const float DeltaSeconds = GameClient()->UiRuntimeV2()->FrameDt();
	const float UiScale = g_Config.m_QmUiScale / 100.0f;
	// 顶部居中；主体高度取 HUD 动态岛同一档设计高度的量级，保证「一眼是灵动岛」。
	// 倒计时环整条都在主体外轮廓外侧，所以外边距还要留出环的宽度。
	const float BodyH = 26.0f * UiScale;
	const qm_island::SNoticeLayout Layout = qm_island::ResolveLayout(
		Screen, 300.0f * UiScale, BodyH, 10.0f * UiScale, 2.0f * UiScale, 1.5f * UiScale);
	// 入场「掉落 → 展开」与出场「收缩 → 上滑」共用同两条弹簧，先后顺序由状态机门控。
	CUiV2AnimationRuntime &Anim = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t NodeBase = MakeUiScopeHash("menu_sponsor_nudge_island");
	const SHudMediaIslandEntranceSpringResult Entrance = qm_island::ResolveSprings(
		Anim, NodeBase ^ 0x11u, NodeBase ^ 0x22u, m_QmSponsorNudgeNotice, Visible);
	m_QmSponsorNudgeNotice.m_DropProgress = Entrance.m_DropProgress;
	m_QmSponsorNudgeNotice.m_ExpandProgress = Entrance.m_ExpandProgress;
	// 倒计时只在完整展开后开始，否则形变阶段就把时间吃掉一截；到时收起，转入出场动画。
	if(qm_island::AdvanceCountdown(m_QmSponsorNudgeNotice, Visible, DeltaSeconds))
		pGameClient->DismissSponsorNudge(false);
	const float Remaining = qm_island::RemainingFraction(m_QmSponsorNudgeNotice);
	const SHudMediaIslandEntrancePose Pose = QmHudMediaIslandEntrancePose(
		Layout.m_Body, Layout.m_Body.h * 0.5f, ui_token::color::SURFACE_ELEVATED, Entrance.m_ExpandProgress, Entrance.m_DropProgress, Screen.y);
	// 组 SDF 状态：主体是胶囊；倒计时环贴主体外轮廓绕一圈（完全展开后随 ContentAlpha 淡入）。
	// 不画外圈阴影：整块岛与黑球都只留本体轮廓。
	SHudMediaIslandSdfRenderState SdfState;
	SdfState.m_MainRect = Pose.m_Rect;
	SdfState.m_MainRadius = Pose.m_Rect.h * 0.5f;
	SdfState.m_MainCorners = IGraphics::CORNER_ALL;
	SdfState.m_BackgroundColor = Pose.m_BackgroundColor;
	SdfState.m_ScreenPixelSize = Ui()->PixelSize();
	SdfState.m_OutlineRingThickness = Layout.m_RingThickness;
	SdfState.m_OutlineRingOffset = Layout.m_RingOffset;
	SdfState.m_ItemCount = 1;
	SdfState.m_Items[0].m_Center = vec2(Pose.m_Rect.x + Pose.m_Rect.w * 0.5f, Pose.m_Rect.y + Pose.m_Rect.h * 0.5f);
	SdfState.m_Items[0].m_Radii = vec2(Pose.m_Rect.w * 0.5f, Pose.m_Rect.h * 0.5f);
	SdfState.m_Items[0].m_ContentAlpha = Pose.m_ContentAlpha;
	// 进度取「剩余」：弧线随时间消耗，时间到走满一圈。
	SdfState.m_Items[0].m_CountdownProgress = Remaining;
	SdfState.m_Items[0].m_RingColor = qm_island::CountdownRingColor(Remaining);
	SdfState.m_Rect = QmHudMediaIslandSdfOuterRect(SdfState);
	qm_island::Render(Graphics(), SdfState);
	if(Ui()->RenderOnly())
		return;
	char aText[256];
	if(pGameClient->SponsorNudgeFarewellActive())
		str_copy(aText, Localize("Really? Not even a little?"), sizeof(aText));
	else if(!pGameClient->m_QmClient.QmSponsorNames().empty())
		str_format(aText, sizeof(aText), Localize("Still free after %d sponsors. Take a look?"), (int)pGameClient->m_QmClient.QmSponsorNames().size());
	else
		str_copy(aText, Localize("Thanks for supporting QmClient"), sizeof(aText));
	// 内容淡入必须跟着入场 pose 的 ContentAlpha，否则形变期间文字会先于外形出现。
	const ColorRGBA TextColor = ui_token::color::TEXT_PRIMARY.WithMultipliedAlpha(Pose.m_ContentAlpha);
	CUIRect TextRect = Pose.m_Rect;
	TextRect.Margin(10.0f * UiScale, &TextRect);
	TextRender()->TextColor(TextColor);
	Ui()->DoLabel(&TextRect, aText, ui_token::font::BODY * UiScale, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

namespace
{
	// 「新功能」弹窗条目：名称 / 说明 / 用法 / 入口。
	// 入口按「设置 → QmClient → 页签 → 卡片」拼装，全部复用已有译文；
	// 带 tab 与 stableId 时额外显示跳转按钮，直接跳到设置页对应卡片。
	struct SQmNewFeatureEntry
	{
		const char *m_pName;
		const char *m_pSummary;
		const char *m_pUsage;
		const char *m_pSection;
		const char *m_pCardTab;
		const char *m_pCardStableId;
	};

	// 表内字符串用 Localizable 标记为 i18n 源 key，绘制时再走 Localize。
	const SQmNewFeatureEntry g_aQmNewFeatures[] = {
		{
			Localizable("Sponsor title"),
			Localizable("Redeem your sponsor code to unlock a custom title, shown in chat and on your nameplate."),
			Localizable("Enter the sponsor code, press Redeem, then write the title and press Save. Tick \"Only show with this nickname\" to bind it to one nickname."),
			Localizable("Contributors"),
			"qmclient-contributors",
			"deck:qmclient-contributors-title",
		},
		{
			Localizable("DDRace HUD Pro"),
			Localizable("Adds dummy key, hammer, control and copy status to the HUD, plus your own bind status list."),
			Localizable("Switch on the status rows you need, then list the binds you want to watch."),
			Localizable("HUD"),
			"hud",
			"qm:bind_status_hud",
		},
		{
			Localizable("Lyrics"),
			Localizable("Shows lyrics from NetEase Cloud Music, Soda Music, Kugou and QQ Music on the dynamic island."),
			Localizable("Pick the source that matches your music app and keep it playing; the lyrics follow automatically."),
			Localizable("HUD"),
			"hud",
			// 歌词没有独立设置卡，开关在灵动岛卡内；指向不存在的卡会让跳转落空。
			"qm:dynamic_island",
		},
		{
			Localizable("Weapon animation"),
			Localizable("Your weapon slides and rotates in when you switch, and flips while reloading."),
			Localizable("Enable weapon switch animation or reload animation, then tune range, duration, rotation and easing."),
			Localizable("Visuals"),
			"visual",
			"qm:weapon_animation",
		},
		{
			Localizable("Skin transition"),
			Localizable("Plays an animation whenever your skin changes, including skins stolen with the hammer."),
			Localizable("Enable skin transition animation, then choose the type, scope, duration and easing."),
			Localizable("Visuals"),
			"visual",
			"qm:skin_transition",
		},
		{
			Localizable("Message merging"),
			Localizable("Merges chat messages repeated within a short time into a single line."),
			Localizable("Turn on \"Message merging\" in Dream Features."),
			Localizable("Functions"),
			"function",
			"qm:mini_features",
		},
	};

	// 文本按 m_MaxWidth 换行后的行数，用于给弹窗条目预留高度。
	int QmWrappedLineCount(ITextRender *pTextRender, float FontSize, const char *pText, float MaxWidth)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
			return 1;
		int LineCount = 0;
		STextSizeProperties TextSizeProps{};
		TextSizeProps.m_pLineCount = &LineCount;
		pTextRender->TextWidth(FontSize, pText, -1, MaxWidth, 0, TextSizeProps);
		return maximum(1, LineCount);
	}
}

void CMenus::RenderQmNewFeaturesPopup(CUIRect Screen)
{
	const IUiContext Ctx = SettingsUiContext("qm_new_features_popup");
	const float UiScale = std::clamp(g_Config.m_QmUiScale / 100.0f, 0.5f, 2.0f);
	const float TitleSize = ui_token::font::TITLE * UiScale;
	const float HeadlineSize = ui_token::font::HEADLINE * UiScale;
	const float BodySize = ui_token::font::BODY * UiScale;
	const float TipSize = ui_token::font::TIP * UiScale;
	const float Padding = ui_token::spacing::LG * UiScale;
	const float Gap = ui_token::spacing::MD * UiScale;
	const float ButtonH = 26.0f * UiScale;
	const float JumpButtonW = 110.0f * UiScale;

	// 遮罩：弹窗期间抢占启动菜单的绘制，先压暗整屏再画面板。
	Screen.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_NONE, 0.0f);

	CUIRect Panel;
	// 小窗口下先夹住上限，避免 std::clamp 出现 lo > hi。
	const float MaxPanelW = std::max(160.0f, Screen.w - 2.0f * ui_token::spacing::LG);
	const float MaxPanelH = std::max(200.0f, Screen.h - 2.0f * ui_token::spacing::LG);
	Panel.w = std::clamp(820.0f * UiScale, 160.0f, MaxPanelW);
	Panel.h = std::clamp(Screen.h - 4.0f * ui_token::spacing::LG, 200.0f, MaxPanelH);
	Panel.x = Screen.x + (Screen.w - Panel.w) * 0.5f;
	Panel.y = Screen.y + (Screen.h - Panel.h) * 0.5f;

	CUIRect ShadowRect = Panel;
	ShadowRect.x += ui_token::elevation::SHADOW_X_HIGH;
	ShadowRect.y += ui_token::elevation::SHADOW_Y_HIGH;
	DrawRoundedSurface(Ctx, ShadowRect, ui_token::color::SURFACE_SHADOW, ColorRGBA(), ui_token::radius::CARD);
	DrawRoundedSurface(Ctx, Panel, ui_token::color::SURFACE_ELEVATED, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD);

	CUIRect Inner;
	Panel.Margin(Padding, &Inner);

	const char *pTitle = Localize("New features");
	char aVersion[64];
	str_format(aVersion, sizeof(aVersion), "%s %s", CLIENT_NAME, CLIENT_RELEASE_VERSION);

	CUIRect TitleRow, SubTitleRow;
	Inner.HSplitTop(TitleSize * 1.3f, &TitleRow, &Inner);
	Ui()->DoLabel(&TitleRow, pTitle, TitleSize, TEXTALIGN_ML);
	Inner.HSplitTop(TipSize * 1.6f, &SubTitleRow, &Inner);
	TextRender()->TextColor(ui_token::color::TEXT_TIP);
	Ui()->DoLabel(&SubTitleRow, aVersion, TipSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Inner.HSplitTop(Gap, nullptr, &Inner);

	CQmClient &QmClient = GameClient()->m_QmClient;
	// 仅在公告页面实际绘制时标记已读，收到网络更新本身不会确认阅读。
	QmClient.MarkQmNewsRead();
	if(m_QmNewFeaturesScrollReset && QmClient.HasDeveloperCredential())
		QmClient.QmNewsReloadDraft();
	// 底部：关闭按钮及仅限开发者凭据的公告草稿操作。
	CUIRect ButtonRow, CloseButtonRect;
	Inner.HSplitBottom(ButtonH, &Inner, &ButtonRow);
	Inner.HSplitBottom(Gap, &Inner, nullptr);
	if(QmClient.HasDeveloperCredential())
	{
		CUIRect DevRow, ReloadDraftRect, PublishRect, OpenFolderRect, DevStatus;
		Inner.HSplitBottom(ButtonH, &Inner, &DevRow);
		Inner.HSplitBottom(Gap, &Inner, nullptr);
		const float DevButtonW = std::min(DevRow.w * 0.28f, 150.0f * UiScale);
		DevRow.VSplitLeft(DevButtonW, &ReloadDraftRect, &DevRow);
		DevRow.VSplitLeft(Gap, nullptr, &DevRow);
		DevRow.VSplitLeft(DevButtonW, &PublishRect, &DevRow);
		DevRow.VSplitLeft(Gap, nullptr, &DevRow);
		DevRow.VSplitLeft(DevButtonW, &OpenFolderRect, &DevStatus);
		DevStatus.VSplitLeft(Gap, nullptr, &DevStatus);

		static CButtonContainer s_ReloadDraftButton, s_PublishButton, s_OpenFolderButton;
		if(ui_widget::SecondaryButton(Ctx, &s_ReloadDraftButton, Localize("Reload"), ReloadDraftRect, QmClient.QmNewsPublishing()))
			QmClient.QmNewsReloadDraft();
		if(ui_widget::PrimaryButton(Ctx, &s_PublishButton, Localize("Publish"), PublishRect, QmClient.QmNewsPublishing() || QmClient.QmNewsDraft()[0] == '\0'))
			QmClient.QmNewsPublishDraft();
		if(ui_widget::SecondaryButton(Ctx, &s_OpenFolderButton, Localize("Open folder"), OpenFolderRect))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient", aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
		}

		const char *pDevStatus = nullptr;
		switch(QmClient.QmNewsStatus())
		{
		case CQmClient::ENewsStatus::PUBLISHING: pDevStatus = Localize("Publishing…"); break;
		case CQmClient::ENewsStatus::PUBLISH_DENIED:
		case CQmClient::ENewsStatus::PUBLISH_TOO_LARGE:
		case CQmClient::ENewsStatus::PUBLISH_FAILED: pDevStatus = Localize("Publish failed"); break;
		case CQmClient::ENewsStatus::PUBLISHED: pDevStatus = Localize("Published"); break;
		default:
			if(QmClient.QmNewsDraft()[0] == '\0')
				pDevStatus = Localize("Draft file is empty");
			break;
		}
		if(pDevStatus != nullptr)
		{
			TextRender()->TextColor(ui_token::color::TEXT_TIP);
			Ui()->DoLabel(&DevStatus, pDevStatus, TipSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
	CloseButtonRect = ButtonRow;
	CloseButtonRect.w = std::min(ButtonRow.w, 200.0f * UiScale);
	CloseButtonRect.x = ButtonRow.x + (ButtonRow.w - CloseButtonRect.w) * 0.5f;
	static CButtonContainer s_CloseButton;
	if(ui_widget::PrimaryButton(Ctx, &s_CloseButton, Localize("Close"), CloseButtonRect) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_Popup = POPUP_NONE;
		return;
	}

	// 条目区：内容超出时滚动；每次打开回到顶部。
	static CScrollRegion s_ScrollRegion;
	if(m_QmNewFeaturesScrollReset)
	{
		s_ScrollRegion.Reset();
		m_QmNewFeaturesScrollReset = false;
	}
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3.0f * BodySize;
	vec2 ScrollOffset;
	CUIRect ScrollArea = Inner;
	s_ScrollRegion.Begin(&ScrollArea, &ScrollOffset, &ScrollParams);

	CUIRect Content = ScrollArea;
	Content.x += ScrollOffset.x;
	Content.y += ScrollOffset.y;

	// 中心服广播有内容时优先显示受限 Markdown；没有广播时保留本地静态新功能列表。
	if(QmClient.HasQmMarkdownBroadcast())
	{
		static std::vector<qm_md::SBlock> s_vBroadcastBlocks;
		static int s_BroadcastRevision = -1;
		if(s_BroadcastRevision != QmClient.QmMarkdownBroadcastRevision())
		{
			s_vBroadcastBlocks = qm_md::Parse(QmClient.QmMarkdownBroadcast());
			s_BroadcastRevision = QmClient.QmMarkdownBroadcastRevision();
		}
		static CButtonContainer s_aBroadcastButtons[16] = {};
		static CButtonContainer s_aBroadcastLinkButtons[16] = {};
		int BroadcastButtonIndex = 0;
		int BroadcastLinkButtonIndex = 0;
		for(const qm_md::SBlock &Block : s_vBroadcastBlocks)
		{
			if(Block.m_Kind == qm_md::EBlockKind::SETTINGS_BUTTON)
			{
				const qm_card_registry::SCardDefault *pCard = qm_card_registry::FindByStableId(Block.m_SettingsCardId.c_str());
				if(pCard == nullptr || pCard->m_pDefaultTab == nullptr)
					continue;
				CUIRect ButtonRect;
				Content.HSplitTop(ButtonH + Gap * 0.5f, &ButtonRect, &Content);
				if(s_ScrollRegion.AddRect(ButtonRect) && BroadcastButtonIndex < (int)std::size(s_aBroadcastButtons))
				{
					ButtonRect.w = std::min(ButtonRect.w, 240.0f * UiScale);
					const char *pLabel = Block.m_SettingsLabel.empty() ? Localize("Open settings") : Block.m_SettingsLabel.c_str();
					if(ui_widget::SecondaryButton(Ctx, &s_aBroadcastButtons[BroadcastButtonIndex], pLabel, ButtonRect))
					{
						qm_card_registry::SCardNavigationTarget Target;
						Target.m_pTab = pCard->m_pDefaultTab;
						Target.m_pStableId = pCard->m_pStableId;
						NavigateToSettingsCard(Target);
						m_Popup = POPUP_NONE;
						SetShowStart(false);
						SetMenuPage(PAGE_SETTINGS);
						s_ScrollRegion.End();
						return;
					}
				}
				++BroadcastButtonIndex;
				continue;
			}
			if(Block.m_Kind == qm_md::EBlockKind::SEPARATOR)
			{
				CUIRect Separator;
				Content.HSplitTop(1.0f + Gap * 0.5f, &Separator, &Content);
				if(s_ScrollRegion.AddRect(Separator))
				{
					Separator.h = 1.0f;
					DrawRoundedSurface(Ctx, Separator, ui_token::color::BORDER_SUBTLE, ColorRGBA(), 0.0f);
				}
				continue;
			}
			std::string Text;
			for(const qm_md::SSpan &Span : Block.m_vSpans)
				Text += Span.m_Text;
			if(Text.empty())
				continue;
			float FontSize = BodySize;
			if(Block.m_Kind == qm_md::EBlockKind::HEADING1)
				FontSize = HeadlineSize * 1.25f;
			else if(Block.m_Kind == qm_md::EBlockKind::HEADING2)
				FontSize = HeadlineSize * 1.1f;
			else if(Block.m_Kind == qm_md::EBlockKind::HEADING3)
				FontSize = HeadlineSize;
			const float Indent = Block.m_Kind == qm_md::EBlockKind::BULLET || Block.m_Kind == qm_md::EBlockKind::NUMBERED ? 18.0f * UiScale : 0.0f;
			const int LineCount = QmWrappedLineCount(TextRender(), FontSize, Text.c_str(), std::max(80.0f, Content.w - Indent));
			CUIRect TextRect;
			Content.HSplitTop(LineCount * FontSize * 1.55f + Gap * 0.5f, &TextRect, &Content);
			if(!s_ScrollRegion.AddRect(TextRect))
				continue;
			if(Indent > 0.0f)
			{
				char aMarker[16];
				if(Block.m_Kind == qm_md::EBlockKind::BULLET)
					str_copy(aMarker, "•");
				else
					str_format(aMarker, sizeof(aMarker), "%d.", Block.m_Number);
				CUIRect Marker;
				TextRect.VSplitLeft(Indent, &Marker, &TextRect);
				TextRender()->TextColor(ui_token::color::TEXT_TIP);
				Ui()->DoLabel(&Marker, aMarker, BodySize, TEXTALIGN_TL);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			if(Block.m_Kind == qm_md::EBlockKind::QUOTE)
			{
				CUIRect Bar = TextRect;
				Bar.w = 3.0f;
				DrawRoundedSurface(Ctx, Bar, ui_token::color::ACCENT_PRIMARY_DIM, ColorRGBA(), 0.0f);
				TextRect.VSplitLeft(10.0f * UiScale, nullptr, &TextRect);
			}
			TextRender()->TextColor(Block.m_Kind == qm_md::EBlockKind::HEADING1 || Block.m_Kind == qm_md::EBlockKind::HEADING2 || Block.m_Kind == qm_md::EBlockKind::HEADING3 ? ui_token::color::TEXT_PRIMARY : ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&TextRect, Text.c_str(), FontSize, TEXTALIGN_TL, {.m_MaxWidth = TextRect.w});
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			for(const qm_md::SSpan &Span : Block.m_vSpans)
			{
				if(Span.m_Link.empty() || BroadcastLinkButtonIndex >= (int)std::size(s_aBroadcastLinkButtons))
					continue;
				CUIRect LinkRect;
				Content.HSplitTop(ButtonH + Gap * 0.5f, &LinkRect, &Content);
				if(s_ScrollRegion.AddRect(LinkRect))
				{
					LinkRect.w = std::min(LinkRect.w, 240.0f * UiScale);
					if(ui_widget::SecondaryButton(Ctx, &s_aBroadcastLinkButtons[BroadcastLinkButtonIndex], Span.m_Text.c_str(), LinkRect))
						Client()->ViewLink(Span.m_Link.c_str());
				}
				++BroadcastLinkButtonIndex;
			}
		}
		s_ScrollRegion.End();
		return;
	}
	const float TextWidth = std::max(80.0f, Content.w - JumpButtonW - Gap - ui_token::spacing::MD * UiScale);

	static CButtonContainer s_aJumpButtons[std::size(g_aQmNewFeatures)] = {};
	for(size_t Index = 0; Index < std::size(g_aQmNewFeatures); ++Index)
	{
		const SQmNewFeatureEntry &Entry = g_aQmNewFeatures[Index];
		char aEntryPath[192];
		str_format(aEntryPath, sizeof(aEntryPath), "%s → QmClient → %s → %s", Localize("Settings"), Localize(Entry.m_pSection), Localize(Entry.m_pName));
		char aEntryLine[256];
		str_format(aEntryLine, sizeof(aEntryLine), Localize("Entry: %s"), aEntryPath);
		const int SummaryLines = QmWrappedLineCount(TextRender(), BodySize, Localize(Entry.m_pSummary), TextWidth);
		const int UsageLines = QmWrappedLineCount(TextRender(), TipSize, Localize(Entry.m_pUsage), TextWidth);
		const int EntryLines = QmWrappedLineCount(TextRender(), TipSize, aEntryLine, TextWidth);
		const float EntryHeight = HeadlineSize * 1.3f + ui_token::spacing::XS * UiScale +
					  SummaryLines * BodySize * 1.5f + (UsageLines + EntryLines) * TipSize * 1.6f +
					  Gap;

		CUIRect EntryRect;
		Content.HSplitTop(EntryHeight, &EntryRect, &Content);
		if(!s_ScrollRegion.AddRect(EntryRect))
			continue;

		CUIRect EntryContent = EntryRect;
		CUIRect JumpRow;
		EntryContent.HSplitTop(HeadlineSize * 1.3f, &JumpRow, &EntryContent);
		if(Index > 0)
		{
			CUIRect Separator;
			Separator.x = EntryRect.x;
			Separator.w = EntryRect.w;
			Separator.h = 1.0f;
			Separator.y = EntryRect.y - Gap * 0.5f;
			DrawRoundedSurface(Ctx, Separator, ui_token::color::BORDER_SUBTLE, ColorRGBA(), 0.0f);
		}

		CUIRect TitleRect = JumpRow;
		TextRender()->TextColor(ui_token::color::TEXT_PRIMARY);
		Ui()->DoLabel(&TitleRect, Localize(Entry.m_pName), HeadlineSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(Entry.m_pCardTab != nullptr && Entry.m_pCardStableId != nullptr)
		{
			CUIRect JumpButtonRect = JumpRow;
			JumpButtonRect.VSplitRight(JumpButtonW, nullptr, &JumpButtonRect);
			JumpButtonRect.h = minimum(JumpButtonRect.h, ButtonH);
			if(ui_widget::SecondaryButton(Ctx, &s_aJumpButtons[Index], Localize("Open settings"), JumpButtonRect))
			{
				qm_card_registry::SCardNavigationTarget Target;
				Target.m_pTab = Entry.m_pCardTab;
				Target.m_pStableId = Entry.m_pCardStableId;
				NavigateToSettingsCard(Target);
				m_Popup = POPUP_NONE;
				SetShowStart(false);
				SetMenuPage(PAGE_SETTINGS);
				s_ScrollRegion.End();
				return;
			}
		}

		CUIRect SummaryRow, UsageRow, EntryRow;
		EntryContent.HSplitTop(SummaryLines * BodySize * 1.5f, &SummaryRow, &EntryContent);
		TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
		Ui()->DoLabel(&SummaryRow, Localize(Entry.m_pSummary), BodySize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		EntryContent.HSplitTop(UsageLines * TipSize * 1.6f, &UsageRow, &EntryContent);
		EntryContent.HSplitTop(EntryLines * TipSize * 1.6f, &EntryRow, &EntryContent);
		TextRender()->TextColor(ui_token::color::TEXT_TIP);
		char aUsage[512];
		str_format(aUsage, sizeof(aUsage), Localize("Usage: %s"), Localize(Entry.m_pUsage));
		Ui()->DoLabel(&UsageRow, aUsage, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		Ui()->DoLabel(&EntryRow, aEntryLine, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_ScrollRegion.End();
}

bool CMenus::RenderQmFunctionCheckboxRow(CUIRect &Content, const float LineHeight, const float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue, const bool PrewarmOnly, const char *pTooltip)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const bool Changed = RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &Row, PrewarmOnly, pTooltip);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmUpdatePopup(CUIRect Screen)
{
	auto &Update = GameClient()->m_TClient;
	const int State = Update.IsUpdateChecking() ? 1 : Update.IsUpdateDownloading() ? 2 :
						  Update.m_UpdateReady                 ? 3 :
						  Update.m_UpdateCheckFailed           ? 4 :
											 0;
	if(State != m_QmUpdatePopupState)
	{
		m_QmUpdatePopupState = State;
		log_info("qm-update", "popup_state=%d", State);
	}
	const IUiContext Ctx = SettingsUiContext("qm_update_popup");
	const float Scale = std::clamp(g_Config.m_QmUiScale / 100.0f, 0.5f, 2.0f);
	const float Padding = std::min(ui_token::spacing::LG * Scale, Screen.w * 0.04f);
	const float Gap = std::min(ui_token::spacing::MD * Scale, Screen.w * 0.02f);
	const float BodySize = ui_token::font::BODY * Scale;
	const float TitleSize = ui_token::font::TITLE * Scale;
	Screen.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_NONE, 0.0f);
	CUIRect Panel = Screen;
	Panel.w = std::min(600.0f * Scale, std::max(0.0f, Screen.w - 2 * Padding));
	Panel.h = std::min(350.0f * Scale, std::max(0.0f, Screen.h - 2 * Padding));
	Panel.x += (Screen.w - Panel.w) * 0.5f;
	Panel.y += (Screen.h - Panel.h) * 0.5f;
	DrawRoundedSurface(Ctx, Panel, ui_token::color::SURFACE_ELEVATED, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD);
	CUIRect Content;
	Panel.Margin(Padding, &Content);
	CUIRect Row;
	Content.HSplitTop(TitleSize * 1.5f, &Row, &Content);
	Ui()->DoLabel(&Row, Localize("Update"), TitleSize, TEXTALIGN_ML);
	CUIRect Buttons;
	Content.HSplitBottom(BodySize * 2.4f, &Content, &Buttons);
	Content.HSplitBottom(Gap, &Content, nullptr);
	// 下载能力固定在操作区上方，说明可独立滚动，缺包提示不会被长说明挤掉。
#if defined(CONF_QMCLIENT_PORTABLE)
	constexpr bool QmUpdatePortableBuild = true;
#else
	constexpr bool QmUpdatePortableBuild = false;
#endif
	if(Update.m_FetchedQmClientUpdateInfo)
	{
		const bool PackageAvailable = Update.m_UpdateRelease.m_PackageAvailable || Update.m_UpdateUseSetup;
		const char *pPackage = PackageAvailable      ? Localize("Update package available") :
				       QmUpdatePortableBuild ? Localize("No downloadable portable package for this version") :
							       Localize("No compatible Windows update package for this version");
		CUIRect Availability;
		Content.HSplitBottom(QmWrappedLineCount(TextRender(), BodySize, pPackage, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Content, &Availability);
		Ui()->DoLabel(&Availability, pPackage, BodySize, TEXTALIGN_TL, {.m_MaxWidth = Availability.w});
	}
	static CScrollRegion s_ScrollRegion;
	if(m_QmUpdateScrollReset)
	{
		s_ScrollRegion.Reset();
		m_QmUpdateScrollReset = false;
	}
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = BodySize * 3;
	vec2 ScrollOffset;
	s_ScrollRegion.Begin(&Content, &ScrollOffset, &ScrollParams);
	Content.x += ScrollOffset.x;
	Content.y += ScrollOffset.y;
	const auto Label = [&](const CUIRect &Rect, const char *pText, float FontSize, int Alignment) {
		if(s_ScrollRegion.AddRect(Rect))
			Ui()->DoLabel(&Rect, pText, FontSize, Alignment, {.m_MaxWidth = Rect.w});
	};
	const char *pStatus = Localize("Update");
	if(Update.m_UpdateCheckFailed)
		pStatus = Update.m_UpdateNetworkError ? Localize("Network error") : Localize("Update failed. Please try again");
	else if(Update.m_UpdateReady)
		pStatus = Localize("The update is ready and will be installed when you exit.");
	else if(Update.IsUpdateChecking())
		pStatus = Localize("(Fetching Update Info)");
	else if(Update.IsUpdateDownloading())
		pStatus = Localize("Downloading update...");
	else if(Update.NeedQmClientUpdate())
		pStatus = Localize("(Update required)");
	else if(Update.m_FetchedQmClientUpdateInfo)
		pStatus = Localize("You are already on the latest version");
	Content.HSplitTop(QmWrappedLineCount(TextRender(), BodySize, pStatus, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Row, &Content);
	Label(Row, pStatus, BodySize, TEXTALIGN_TL);
	char aVersion[128];
	const bool DifferentVersion = Update.m_FetchedQmClientUpdateInfo && str_comp(CLIENT_RELEASE_VERSION, Update.m_UpdateRelease.m_aVersion) != 0;
	str_format(aVersion, sizeof(aVersion), "QmClient %s%s%s", CLIENT_RELEASE_VERSION,
		DifferentVersion ? " → " : "", DifferentVersion ? Update.m_UpdateRelease.m_aVersion : "");
	Content.HSplitTop(BodySize * 1.7f, &Row, &Content);
	Label(Row, aVersion, BodySize, TEXTALIGN_ML);
	if(Update.IsUpdateDownloading() && Update.m_pUpdatePackageTask)
	{
		const auto &Task = Update.m_pUpdatePackageTask;
		char aProgress[128];
		str_format(aProgress, sizeof(aProgress), "%.1f / %.1f MiB (%d%%)", Task->Current() / 1048576.0, Task->Size() / 1048576.0, Task->Progress());
		Content.HSplitTop(BodySize * 1.7f, &Row, &Content);
		Label(Row, aProgress, BodySize, TEXTALIGN_ML);
		CUIRect Bar;
		Content.HSplitTop(std::max(2.0f, Gap * 0.5f), &Bar, &Content);
		if(s_ScrollRegion.AddRect(Bar))
		{
			DrawRoundedSurface(Ctx, Bar, ui_token::color::BORDER_SUBTLE, ColorRGBA(), ui_token::radius::CARD);
			Bar.w *= std::clamp(Task->Progress() / 100.0f, 0.0f, 1.0f);
			DrawRoundedSurface(Ctx, Bar, ui_token::color::ACCENT_PRIMARY_DIM, ColorRGBA(), ui_token::radius::CARD);
		}
	}
	const auto *pSource = Update.IsUpdateChecking() ? Update.m_UpdateMetadataRequest.Source() : Update.m_UpdateDownloadAttempt.Current();
	if(pSource && (Update.IsUpdateChecking() || Update.IsUpdateDownloading()))
	{
		Content.HSplitTop(BodySize * 1.7f, &Row, &Content);
		Label(Row, pSource->m_Prefix.empty() ? "GitHub" : pSource->m_Prefix.c_str(), BodySize, TEXTALIGN_ML);
	}
	if(Update.m_UpdateCheckFailed && Update.m_aUpdateError[0])
	{
		Content.HSplitTop(QmWrappedLineCount(TextRender(), BodySize, Update.m_aUpdateError, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Row, &Content);
		Label(Row, Update.m_aUpdateError, BodySize, TEXTALIGN_TL);
	}
	if(Update.m_FetchedQmClientUpdateInfo)
	{
		Content.HSplitTop(BodySize * 1.8f, &Row, &Content);
		Label(Row, Localize("Release notes"), BodySize, TEXTALIGN_ML);
		// 按元数据/字体/宽度版本缓存段落和测量；每帧只绘制滚动区域内的段落。
		struct SNoteParagraph
		{
			std::string m_Text;
			float m_FontScale = 1;
			float m_Height = 0;
		};
		static std::vector<SNoteParagraph> s_vNotes;
		static uint64_t s_NotesRevision = UINT64_MAX, s_FontRevision = UINT64_MAX;
		static float s_NotesWidth = -1, s_NotesSize = -1;
		if(s_NotesRevision != Update.m_UpdateInfoRevision)
		{
			s_vNotes.clear();
			for(const auto &Block : qm_md::Parse(Update.m_UpdateRelease.m_Notes.c_str()))
			{
				SNoteParagraph Paragraph;
				if(Block.m_Kind == qm_md::EBlockKind::BULLET)
					Paragraph.m_Text = "• ";
				else if(Block.m_Kind == qm_md::EBlockKind::NUMBERED)
					Paragraph.m_Text = std::to_string(Block.m_Number) + ". ";
				for(const auto &Span : Block.m_vSpans)
					Paragraph.m_Text += Span.m_Text;
				// 发布说明中的链接与设置标记仅作文本，不执行第三方动作。
				if(Block.m_Kind == qm_md::EBlockKind::SETTINGS_BUTTON)
					Paragraph.m_Text = Block.m_SettingsLabel;
				if(Block.m_Kind == qm_md::EBlockKind::HEADING1 || Block.m_Kind == qm_md::EBlockKind::HEADING2 || Block.m_Kind == qm_md::EBlockKind::HEADING3)
					Paragraph.m_FontScale = 1.15f;
				if(!Paragraph.m_Text.empty())
					s_vNotes.push_back(std::move(Paragraph));
			}
			s_NotesRevision = Update.m_UpdateInfoRevision;
			s_NotesWidth = -1;
		}
		if(s_vNotes.empty())
		{
			Content.HSplitTop(BodySize * 1.7f, &Row, &Content);
			Label(Row, Localize("No release notes provided"), BodySize, TEXTALIGN_TL);
		}
		else
		{
			if(s_NotesWidth != Content.w || s_NotesSize != BodySize || s_FontRevision != TextRender()->GlyphAtlasRevision())
			{
				for(auto &Paragraph : s_vNotes)
				{
					const float Size = BodySize * Paragraph.m_FontScale;
					Paragraph.m_Height = QmWrappedLineCount(TextRender(), Size, Paragraph.m_Text.c_str(), std::max(1.0f, Content.w)) * Size * 1.6f + Gap * 0.5f;
				}
				s_NotesWidth = Content.w;
				s_NotesSize = BodySize;
				s_FontRevision = TextRender()->GlyphAtlasRevision();
			}
			for(const auto &Paragraph : s_vNotes)
			{
				Content.HSplitTop(Paragraph.m_Height, &Row, &Content);
				Label(Row, Paragraph.m_Text.c_str(), BodySize * Paragraph.m_FontScale, TEXTALIGN_TL);
			}
		}
	}
	s_ScrollRegion.End();
	// 按可用宽度均分按钮；长译文由共享按钮的文本适配处理。
	CUIRect Action, Manual, Close;
	const float ButtonWidth = std::max(0.0f, (Buttons.w - 2 * Gap) / 3);
	Buttons.VSplitLeft(ButtonWidth, &Action, &Buttons);
	Buttons.VSplitLeft(Gap, nullptr, &Buttons);
	Buttons.VSplitLeft(ButtonWidth, &Manual, &Buttons);
	Buttons.VSplitLeft(Gap, nullptr, &Close);
	static CButtonContainer s_Action, s_Manual, s_Close;
	const bool Busy = Update.IsUpdateChecking() || Update.IsUpdateDownloading();
	// 缺包时仍可重新检查发布者后补的附件；下载能力由生产包选择器阻断。
	const char *pAction = Busy ? Localize("Cancel") : Update.m_UpdateReady                                      ? Localize("Quit") :
						  Update.m_FetchedQmClientUpdateInfo && !Update.m_UpdateCheckFailed ? Localize("Check for updates") :
														      Localize("Retry");
	if(ui_widget::PrimaryButton(Ctx, &s_Action, pAction, Action))
	{
		if(Busy)
		{
			Update.CancelQmClientUpdate();
			m_Popup = POPUP_NONE;
		}
		else if(Update.m_UpdateReady)
			Client()->Quit();
		else
			Update.RequestQmClientUpdateCheckAndUpdate();
	}
	if(ui_widget::SecondaryButton(Ctx, &s_Manual, "GitHub", Manual))
		Client()->ViewLink(qm_update::MANUAL_DOWNLOAD_URL);
	if(ui_widget::SecondaryButton(Ctx, &s_Close, Localize("Close"), Close) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Update.m_UpdatePopupRequested = false;
		m_Popup = POPUP_NONE;
	}
}

uint64_t CMenus::TranslationTestLayoutRevision() const
{
	return static_cast<uint64_t>(m_TranslateProbe.Pending()) | (static_cast<uint64_t>(m_TranslateProbe.HasResult()) << 1) | (static_cast<uint64_t>(m_TranslateProbeDiagnostics != 0) << 2) | (static_cast<uint64_t>(m_TranslateProbe.Response().m_Notice) << 3) | (static_cast<uint64_t>(str_length(m_TranslateProbe.Response().m_Text)) << 8) | (static_cast<uint64_t>(GameClient()->m_Translate.LastDiagnostic().m_Notice) << 24);
}
