#include "QmCardCatalogFunctionMetrics.h"
#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

void CMenus::RenderQmFunctionHJAssistContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	const auto RenderCheckbox = [this, &Content, LineHeight, LineSpacing, PrewarmOnly](const char *pText, int *pValue) {
		RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, pValue, pText, Localize(pText), pValue, PrewarmOnly);
	};
	RenderCheckbox(Localizable("Auto unspec on unfreeze"), &g_Config.m_QmAutoUnspecOnUnfreeze);
	RenderCheckbox(Localizable("Auto switch to the tee that got unfrozen"), &g_Config.m_QmAutoSwitchOnUnfreeze);
	RenderCheckbox(Localizable("Automatically close the current chat after waking from freeze"), &g_Config.m_QmAutoCloseChatOnUnfreeze);
	RenderCheckbox(Localizable("Show wake-up popup on the other tee"), &g_Config.m_QmFreezeWakeupPopup);
	RenderCheckbox(Localizable("Auto team lock"), &g_Config.m_QmAutoTeamLock);
	if(g_Config.m_QmAutoTeamLock)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-hj-assist-lock-delay", &LabelColumn, Localize("Lock delay"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static int s_QmAutoTeamLockDelayInputId;
		RenderQmSettingsSliderWithValueInput(&s_QmAutoTeamLockDelayInputId, ControlColumn, &g_Config.m_QmAutoTeamLockDelay, 0, 30, "s", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	RenderCheckbox(Localizable("Fade spectators in water"), &g_Config.m_QmPausedSpectatorFade);
	if(g_Config.m_QmPausedSpectatorFade)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-hj-assist-spectator-alpha", &LabelColumn, Localize("Spectator opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static int s_QmPausedSpectatorAlphaInputId;
		RenderQmSettingsSliderWithValueInput(&s_QmPausedSpectatorAlphaInputId, ControlColumn, &g_Config.m_QmPausedSpectatorAlpha, 0, 100, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}

// 功能分类卡片模块（17 张）：卡片的高度测量、重测版本、预布局输入与内容渲染都在这里，
// 页面（栖梦「功能」页、搜索页）只声明"这一页有这些卡"。
// 词条过滤/关键词回复的内容量由菜单层每帧刷新的 SQmFunctionCardLayoutState 提供；
// 收藏地图卡片的内容量直接读取收藏数量。
namespace qm_card_catalog
{
	namespace
	{
		using qm_module::EQmModuleId;

		float MeasureFunctionCardHeight(const SSettingsContentMetrics &Metrics, CMenus *pMenus, const float LabelWidth, const SQmFunctionCardLayoutState Layout, const EQmModuleId Id, const float ContentWidth)
		{
			const float LineHeight = Metrics.m_LineHeight;
			const float BodySize = Metrics.m_BodySize;
			const float LineSpacing = Metrics.m_LineSpacing;
			const float UiScale = Metrics.m_UiScale;
			const auto Rows = [&Metrics](const float Count) { return CardRows(Metrics, Count); };
			const auto Row = [&Metrics](const float Spacing = 1.0f) { return CardRow(Metrics, Spacing); };
			switch(Id)
			{
			case EQmModuleId::GoresActor:
				return !g_Config.m_TcFreezeChatEnabled ? Row() : Row() * (g_Config.m_TcFreezeChatEmoticon ? 5.0f : 4.0f);
			case EQmModuleId::Gores:
				return Row() * (3.0f + (g_Config.m_QmAxiomAutoLogin ? 2.0f : 0.0f) + ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 7.0f : 0.0f)) + LineHeight;
			case EQmModuleId::KeyBinds: return Rows(8.0f);
			case EQmModuleId::Emoticons: return Rows(3.0f);
			case EQmModuleId::BetterScoreboard: return Rows(5.0f);
			case EQmModuleId::BlockWords: return Row() * (g_Config.m_QmBlockWordsAction == 0 ? 7.0f : 4.0f) + CalcQiaFenInputHeight(QmCardRenderHook::TextRenderer(pMenus), g_Config.m_QmBlockWordsList, std::max(1.0f, ContentWidth - LabelWidth), BodySize, std::clamp(2.0f * UiScale, 1.0f, 2.0f), LineHeight);
			case EQmModuleId::Translate:
			{
				const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
				const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
				const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
				const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
				float Height = Rows(9.0f) + LineHeight * 1.6f + LineSpacing * 1.35f;
				if(IsFtapiBackend)
					Height += Row() + LineHeight * 0.8f + LineSpacing;
				if(IsTencentCloudBackend)
					Height += Row() * 4.0f;
				else if(IsLibreTranslateBackend)
					Height += Row() * 2.0f;
				if(IsLlmBackend)
				{
					Height += Row() * 7.0f + LineHeight + LineSpacing * 0.5f;
					if(g_Config.m_QmTranslateLlmEnableThinking && (g_Config.m_QmTranslateLlmProvider == 2 || g_Config.m_QmTranslateLlmProvider == 3))
						Height += Metrics.m_SmallSize + Metrics.m_LineSpacing;
				}
				return Height;
			}
			case EQmModuleId::TranslateUi: return Rows(5.0f);
			case EQmModuleId::QiaFen:
				return Row() * (4.0f + (float)Layout.m_KeywordRulesCount) + (Layout.m_KeywordRulesHalfFilled ? Row() : 0.0f);
			case EQmModuleId::PieMenu:
				return QmPieMenuContentHeight(ContentWidth, LineHeight, BodySize, LineSpacing, g_Config.m_QmPieMenuEnabled != 0, g_Config.m_QmPieFollowName[0] != '\0');
			case EQmModuleId::FavoriteMaps:
			{
				const size_t FavoriteCount = QmCardRenderHook::FavoriteMapCount(pMenus);
				return Rows((float)std::max<size_t>(1, std::min<size_t>(FavoriteCount, 64)));
			}
			case EQmModuleId::MapUpload:
			{
				float Height = LineHeight * 8.0f + LineSpacing * 6.0f;
				for(const char *pText : QmMapUploadInstructions())
					Height += QmMapUploadHelpLineHeight(QmCardRenderHook::TextRenderer(pMenus), pText, ContentWidth, BodySize, LineHeight) + LineSpacing;
				return Height;
			}
			default: return Rows(1.0f);
			}
		}

		uint64_t MeasureFunctionCardRevision(const SQmCardBuildContext &Ctx, const EQmModuleId Id)
		{
			const SQmFunctionCardLayoutState Layout = Ctx.m_pFunctionLayout != nullptr ? *Ctx.m_pFunctionLayout : SQmFunctionCardLayoutState{};
			switch(Id)
			{
			case EQmModuleId::GoresActor:
				return g_Config.m_TcFreezeChatEnabled ? 1u | (g_Config.m_TcFreezeChatEmoticon ? 2u : 0u) : 0u;
			case EQmModuleId::Gores:
				return (g_Config.m_QmAxiomAutoLogin ? 1u : 0u) |
				       ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 2u : 0u);
			case EQmModuleId::Emoticons:
				return (g_Config.m_QmShowOtherSuperEmotes ? 1u : 0u) |
				       (g_Config.m_QmShowOtherLaunchEmotes ? 2u : 0u);
			case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory != 0 ? 1u : 0u;
			case EQmModuleId::FriendNotify:
				return (g_Config.m_QmFriendOnlineAutoRefresh ? 1u : 0u) |
				       (g_Config.m_QmFriendEnterBroadcast ? 2u : 0u) |
				       (g_Config.m_QmFriendEnterAutoGreet ? 4u : 0u);
			case EQmModuleId::BlockWords: return Layout.m_BlockWordsRevision * 2u + (g_Config.m_QmBlockWordsAction == 0 ? 0u : 1u);
			case EQmModuleId::Translate:
				if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
					return 1u;
				if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
					return 2u;
				if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
					return 3u;
				if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
					return 4u | ((g_Config.m_QmTranslateLlmEnableThinking && (g_Config.m_QmTranslateLlmProvider == 2 || g_Config.m_QmTranslateLlmProvider == 3)) ? 8u : 0u);
				return 0u;
			case EQmModuleId::QiaFen: return Layout.m_KeywordRulesRevision;
			case EQmModuleId::PieMenu: return (g_Config.m_QmPieMenuEnabled ? 1u : 0u) | (g_Config.m_QmPieFollowName[0] != '\0' ? 2u : 0u);
			case EQmModuleId::FavoriteMaps: return Layout.m_FavoriteMapsRevision;
			case EQmModuleId::MapUpload: return 1u;
			case EQmModuleId::HJAssist: return (g_Config.m_QmAutoTeamLock ? 1u : 0u) | (g_Config.m_QmPausedSpectatorFade ? 2u : 0u);
			default: return 0u;
			}
		}
	} // namespace

	bool BuildFunctionCard(const SQmCardBuildContext &Ctx, const EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float BodySize = Metrics.m_BodySize;
		const float LineSpacing = Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const float UiScale = Metrics.m_UiScale;
		const float ButtonHeight = Metrics.m_ButtonHeight;
		const float CardPadding = Ctx.m_Padding;
		const float CardCornerRadius = Ctx.m_CornerRadius;
		const bool ReadOnly = Ctx.m_ReadOnly;

		const auto Add = [&](const EQmModuleId ModuleId, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			const SQmFunctionCardLayoutState MeasureLayout = Ctx.m_pFunctionLayout != nullptr ? *Ctx.m_pFunctionLayout : SQmFunctionCardLayoutState{};
			MakeModuleCard(
				Ctx, ModuleId, pStableId, pTitle, pSubtitle, Render,
				[Metrics, pMenus, LabelWidth, MeasureLayout, ModuleId](float ContentWidth) { return MeasureFunctionCardHeight(Metrics, pMenus, LabelWidth, MeasureLayout, ModuleId, ContentWidth); },
				MeasureFunctionCardRevision(Ctx, ModuleId),
				{},
				Out);
		};

		switch(Id)
		{
		case EQmModuleId::GoresActor:
			Add(Id, "qm:gores_actor", "Gores Actor", "Auto chat when dying in water", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionGoresActorContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::Gores:
			Add(Id, "qm:gores", "Gores Mode", "Gores auto weapon switch", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionGoresContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::KeyBinds:
			Add(Id, "qm:key_binds", "Key Bindings", "Common key bindings", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionKeyBindsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth); });
			return true;
		case EQmModuleId::Emoticons:
			Add(Id, "qm:emoticons", "Emoticons", "Large emoticons and launch mode", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionEmoticonsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth); });
			return true;
		case EQmModuleId::MiniFeatures:
		{
			const auto RenderMiniFeatures = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) {
				QmCardRenderHook::RenderQmFunctionCheckboxRow(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmChatCommandCompletion, "Show command completion in chat", Localize("Show command completion in chat"), &g_Config.m_QmChatCommandCompletion, PrewarmOnly);
				QmCardRenderHook::RenderQmFunctionMiniFeaturesContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
			};
			MakeModuleCard(
				Ctx, Id, "qm:mini_features", "Dream Features", "Only what you can't imagine, nothing Dream can't do",
				[RenderMiniFeatures, ReadOnly](CUIRect &Content) { RenderMiniFeatures(Content, ReadOnly); },
				[RenderMiniFeatures](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderMiniFeatures(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		case EQmModuleId::BetterScoreboard:
			Add(Id, "qm:better_scoreboard", "Better scoreboard", "Scoreboard", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionBetterScoreboardContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::JumpHint:
		{
			const auto RenderJumpHint = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionJumpHintContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			MakeModuleCard(
				Ctx, Id, "qm:jump_hint", "Position jump hint", "Jump hint text",
				[RenderJumpHint, ReadOnly](CUIRect &Content) { RenderJumpHint(Content, ReadOnly); },
				[RenderJumpHint](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderJumpHint(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		case EQmModuleId::WeaponTrajectory:
		{
			const auto RenderWeaponTrajectory = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionWeaponTrajectoryContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			MakeModuleCard(
				Ctx, Id, "qm:weapon_trajectory", "Weapon Trajectory", "Show grenade and laser trajectory preview",
				[RenderWeaponTrajectory, ReadOnly](CUIRect &Content) { RenderWeaponTrajectory(Content, ReadOnly); },
				[RenderWeaponTrajectory](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderWeaponTrajectory(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		case EQmModuleId::FriendNotify:
		{
			const auto RenderFriendNotify = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) {
				QmCardRenderHook::RenderQmFunctionCheckboxRow(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmSpectatorFriendsFirst, "List friends first in the spectator menu", Localize("List friends first in the spectator menu"), &g_Config.m_QmSpectatorFriendsFirst, PrewarmOnly);
				QmCardRenderHook::RenderQmFunctionFriendNotifyContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
			};
			MakeModuleCard(
				Ctx, Id, "qm:friend_notify", "Friend Notifications", "Friend online and join notifications",
				[RenderFriendNotify, ReadOnly](CUIRect &Content) { RenderFriendNotify(Content, ReadOnly); },
				[RenderFriendNotify](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderFriendNotify(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		case EQmModuleId::BlockWords:
			Add(Id, "qm:block_words", "Word Filter", "Chat word filtering", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionBlockWordsContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::Translate:
			Add(Id, "qm:translate", "Translate", "Chat translation settings", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionTranslateContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::TranslateUi:
			Add(Id, "qm:translate_ui", "Translate button", "Customize translate button and menu colors", [pMenus, LineHeight, BodySize, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualTranslateUiContent(pMenus, Content, LineHeight, BodySize, LineSpacing); });
			return true;
		case EQmModuleId::QiaFen:
			Add(Id, "qm:qiafen", "Keyword Reply", "I am a robot", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionKeywordReplyContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::PieMenu:
			Add(Id, "qm:pie_menu", "Pie Menu", "Quick action menu for players", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionPieMenuContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly); });
			return true;
		case EQmModuleId::MapUpload:
			Add(Id, "qm:map_upload", "Map upload", "Upload a saved map to the public test server", [pMenus, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionMapUploadContent(pMenus, Content, LineHeight, BodySize, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::FavoriteMaps:
			Add(Id, "qm:favorite_maps", "Favorite maps", "Your favorite map manager", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionFavoriteMapsContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::HJAssist:
		{
			const auto RenderHJAssist = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionHJAssistContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			MakeModuleCard(
				Ctx, Id, "qm:hj_assist", "HJ Assist", "What's done is done, no use saying more",
				[RenderHJAssist, ReadOnly](CUIRect &Content) { RenderHJAssist(Content, ReadOnly); },
				[RenderHJAssist](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderHJAssist(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		// 本地专属卡（远程目录无此项）。本地页面用 PrewarmOnly 探针测量真实渲染高度：
		// menus_qmclient.cpp:5244 把探针渲染作为 AddCard 的第 6 参传入，:5225-5229 以
		// CUIRect{0,0,ContentWidth,9999} 渲染后取 9999 - Probe.h 作为高度。
		// 目录的静态行数公式（MeasureFunctionCardHeight）没有该卡分支，若沿用会落回默认高度，
		// 故此处必须自行传入探针式 Measure。
		case EQmModuleId::SoloSplit:
		{
			const auto RenderSoloSplit = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionSoloSplitContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			MakeModuleCard(
				Ctx, Id, "qm:solo_split", "Solo Split", "Split main and dummy into different teams for solo-play",
				[RenderSoloSplit, ReadOnly](CUIRect &Content) { RenderSoloSplit(Content, ReadOnly); },
				[RenderSoloSplit](float ContentWidth) {
					CUIRect Probe{0.0f, 0.0f, ContentWidth, 9999.0f};
					RenderSoloSplit(Probe, true);
					return 9999.0f - Probe.h;
				},
				MeasureFunctionCardRevision(Ctx, Id), {}, Out);
			return true;
		}
		default:
			return false;
		}
	}
} // namespace qm_card_catalog
