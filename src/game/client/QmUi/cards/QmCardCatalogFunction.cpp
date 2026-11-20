#include "QmCardCatalogFunctionMetrics.h"
#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/word_filter_list.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace qm_card_catalog
{
	bool KeywordReplyHasPendingInput();
}

void CMenus::RenderQmFunctionBlockWordsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row, LabelColumn, ControlColumn;
	// 行距由每行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
	// 条件块是否声明由展开状态决定，所以块前那一档也由块内第一行自己带；
	// 块收起时这一档改由块后面的行声明，两条路径都只有一档。
	auto RenderCheckbox = [this, &Flow, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		if(Flow.Control(pText, LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
			RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
	};
	RenderCheckbox(&g_Config.m_QmBlockWordsShowConsole, Localizable("Show blocked words in console"), &g_Config.m_QmBlockWordsShowConsole);
	static CButtonContainer s_BlockWordsConsoleColorId;
	// 色块行只按按钮高度占位、行距自己声明：ConsumedHeight 自带的那档行尾间距会和下一行叠成双倍。
	if(Flow.Control("console-color", CurrentSettingsContentMetrics().m_ButtonHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		DoLine_ColorPicker(&s_BlockWordsConsoleColorId, CurrentSettingsContentMetrics(), &Row, Localize("Console color"), &g_Config.m_QmBlockWordsConsoleColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmBlockWordsConsoleColor)), false);
	RenderCheckbox(&g_Config.m_QmPublicBlockWords, Localizable("Use public word filter"), &g_Config.m_QmPublicBlockWords);
	// 公共词表的计数与状态说明：开关关掉就整块不声明，打开时按展开进度长高、下面的行让位。
	static CSettingsConditionalRows s_PublicWordFilterRows;
	if(s_PublicWordFilterRows.Update(g_Config.m_QmPublicBlockWords, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope PublicWordFilterRevealScope(Ui(), Flow, s_PublicWordFilterRows.Fade(g_Config.m_QmUiMotionLevel));
		const auto &PublicWords = GameClient()->m_Chat.PublicWordFilter();
		char aCount[128];
		str_format(aCount, sizeof(aCount), Localize("Public words: %d"), static_cast<int>(PublicWords.Words().size()));
		const char *pStatus = PublicWords.Refreshing()     ? Localize("Updating public word list...") :
				      !PublicWords.Error().empty() ? Localize(PublicWords.Error().c_str()) :
								     Localize("Public words match literal text in incoming player chat.");

		// 说明行统一取小字档；测量与绘制共用同一个字号变量，不再借正文字号。
		const float HintSize = CurrentSettingsContentMetrics().m_SmallSize;
		auto Measure = [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; };
		auto Render = [this, HintSize](const CUIRect &Rect, const char *pText) { Ui()->DoLabel(&Rect, pText, HintSize, TEXTALIGN_ML, {.m_MaxWidth = Rect.w}); };
		// 块内第一行是 Flow.Text（没有行前间距参数），进块这一档只能由块内 Space 声明：
		// 它跟着展开系数一起缩放，块收起时归零，空出来的位置由下一行自己的行前间距顶上。
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
		Flow.Text("public-word-filter-count", aCount, 0.0f, HintSize, ui_token::settings::RowGap(LineSpacing), Measure, Render, LineHeight);
		// 末行的行尾间距由下一行自己声明，这里置 0，避免同一处叠成双倍。
		Flow.Text("public-word-filter-status", pStatus, 0.0f, HintSize, 0.0f, Measure, Render, LineHeight);
	}
	RenderCheckbox(&g_Config.m_QmBlockWordsEnabled, Localizable("Enable word filter list"), &g_Config.m_QmBlockWordsEnabled);

	const int BlockWordsAction = g_Config.m_QmBlockWordsAction;
	if(Flow.Control("qmclient-word-filter-action", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-action", &LabelColumn, Localize("Behavior"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static CButtonContainer s_aBlockWordsActionButtons[2];
		const char *apActionLabels[] = {Localize("Replace mode"), Localize("Hide player messages")};
		const int NewAction = DoSegmentedChoice(s_aBlockWordsActionButtons, apActionLabels, std::size(apActionLabels), BlockWordsAction, ControlColumn, ui_token::radius::BASE, nullptr, BodySize, &g_Config.m_QmBlockWordsAction);
		if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly() && NewAction >= 0 && NewAction != BlockWordsAction)
			g_Config.m_QmBlockWordsAction = NewAction;
	}
	// 块前这一档改由块内第一行自己声明：文件行 helper 也自带行距了，
	// 块外再留一档会在块收起时叠成双倍。
	static CSettingsConditionalRows s_BlockWordsReplaceRows;
	if(s_BlockWordsReplaceRows.Update(g_Config.m_QmBlockWordsAction == 0, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope BlockWordsReplaceRevealScope(Ui(), Flow, s_BlockWordsReplaceRows.Fade(g_Config.m_QmUiMotionLevel));
		RenderCheckbox(&g_Config.m_QmBlockWordsMultiReplace, Localizable("Use multi-char replacement based on word length"), &g_Config.m_QmBlockWordsMultiReplace);

		if(Flow.Control("qmclient-word-filter-replacement-chars", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			static CLineInputBuffered<8> s_BlockWordsReplaceInput;
			static bool s_BlockWordsReplaceInited = false;
			if(!s_BlockWordsReplaceInited)
			{
				s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
				s_BlockWordsReplaceInited = true;
			}
			else if(!s_BlockWordsReplaceInput.IsActive() && str_comp(s_BlockWordsReplaceInput.GetString(), g_Config.m_QmBlockWordsReplacementChar) != 0)
			{
				s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
			}
			s_BlockWordsReplaceInput.SetEmptyText("*");
			IUiContext ReplacementInputCtx = SettingsUiContext("settings_qmclient_block_words_text_inputs", UiScale);
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-replacement-chars", &LabelColumn, Localize("Replacement chars"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			if(ui_widget::InputField(ReplacementInputCtx, &s_BlockWordsReplaceInput, ControlColumn, "*", BodySize))
			{
				char aReplacement[8];
				str_utf8_truncate(aReplacement, sizeof(aReplacement), s_BlockWordsReplaceInput.GetString(), 1);
				if(aReplacement[0] == '\0')
					str_copy(aReplacement, "*", sizeof(aReplacement));
				str_copy(g_Config.m_QmBlockWordsReplacementChar, aReplacement, sizeof(g_Config.m_QmBlockWordsReplacementChar));
			}
		}

		if(Flow.Control("qmclient-word-filter-match-mode", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-match-mode", &LabelColumn, Localize("Mode"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static CButtonContainer s_aBlockWordsModeButtons[3];
			const char *apModeLabels[] = {Localize("Regular expression"), Localize("Literal"), Localize("Both")};
			const int NewMode = DoSegmentedChoice(s_aBlockWordsModeButtons, apModeLabels, std::size(apModeLabels), g_Config.m_QmBlockWordsMode, ControlColumn, ui_token::radius::BASE, nullptr, BodySize, &g_Config.m_QmBlockWordsMode);
			if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly() && NewMode >= 0 && NewMode != g_Config.m_QmBlockWordsMode)
				g_Config.m_QmBlockWordsMode = NewMode;
		}
	}

	// 单词表文件 helper 的每一行自己声明行距（已改成 A 型），块后不再补边界行距。
	RenderQmWordFilterFileContent(Content, false, LineHeight, LineSpacing, BodySize);
	static CLineInputBuffered<QmWordFilterList::INLINE_CAPACITY> s_BlockWordsInput;
	static bool s_BlockWordsInited = false;
	if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly() && !s_BlockWordsInited)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
		s_BlockWordsInited = true;
	}
	else if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly() && !s_BlockWordsInput.IsActive() && str_comp(s_BlockWordsInput.GetString(), g_Config.m_QmBlockWordsList) != 0)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
	}
	s_BlockWordsInput.SetEmptyText(Localize("Separate with commas"));
	const float InputLineSpacing = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const auto InputLayout = ResolveSettingsControlRowLayout(Content, CurrentSettingsContentMetrics());
	const char *pWords = s_BlockWordsInited ? s_BlockWordsInput.GetString() : g_Config.m_QmBlockWordsList;
	if(Flow.MeasuredControl("qmclient-word-filter-label", pWords, LineHeight, BodySize, InputLayout.m_ControlRect.x - Content.x, InputLineSpacing, 2.0f, Row,
		   [this](const SSettingsContentRowDeclaration &Declaration, float Width) {
			   return qm_card_catalog::CalcQiaFenInputHeight(TextRender(), Declaration.m_Text.c_str(), Width, Declaration.m_FontSize, Declaration.m_TextLineSpacing, Declaration.m_MinimumHeight);
		   }))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-label", &LabelColumn, Localize("Word Filter"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		IUiContext ListInputCtx = SettingsUiContext("qmclient_block_words_input", UiScale);
		ui_widget::SInputFieldOptions InputOptions;
		InputOptions.m_Mode = ui_widget::EInputFieldMode::MULTILINE;
		InputOptions.m_pPlaceholder = Localize("Separate with commas");
		InputOptions.m_FontSize = BodySize;
		InputOptions.m_LineSpacing = InputLineSpacing;
		InputOptions.m_TextAlign = TEXTALIGN_ML;
		if(ui_widget::InputField(ListInputCtx, &s_BlockWordsInput, ControlColumn, InputOptions).m_Changed)
		{
			str_copy(g_Config.m_QmBlockWordsList, s_BlockWordsInput.GetString(), sizeof(g_Config.m_QmBlockWordsList));
		}
	}
}

void CMenus::RenderQmFunctionHJAssistContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row, LabelColumn, ControlColumn;
	// 行距由行自己声明，行后不再补 Space：卡底和下一行的间距都由下一行自己给出。
	const auto RenderCheckbox = [this, &Flow, &Row, LineHeight, LineSpacing, PrewarmOnly](const char *pText, int *pValue) {
		if(Flow.Control(pText, LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
			RenderQmFunctionCheckbox(pValue, pText, Localize(pText), pValue, &Row, PrewarmOnly);
	};
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
		{
			{&g_Config.m_QmAutoUnspecOnUnfreeze, Localizable("Auto unspec on unfreeze"), Localize("Auto unspec on unfreeze")},
			{&g_Config.m_QmAutoUnspecBothOnUnfreeze, Localizable("Auto unspec both tees when either is hammered awake"), Localize("Auto unspec both tees when either is hammered awake")},
			{&g_Config.m_QmAutoSwitchOnUnfreeze, Localizable("Auto switch to the tee that got unfrozen"), Localize("Auto switch to the tee that got unfrozen")},
			{&g_Config.m_QmAutoCloseChatOnUnfreeze, Localizable("Automatically close the current chat after waking from freeze"), Localize("Automatically close the current chat after waking from freeze")},
			{&g_Config.m_QmFreezeWakeupPopup, Localizable("Show wake-up popup on the other tee"), Localize("Show wake-up popup on the other tee")},
			{&g_Config.m_QmAutoTeamLock, Localizable("Auto-lock after joining lockable team"), Localize("Auto-lock after joining lockable team")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
		{
			{&g_Config.m_QmPausedSpectatorFade, "Fade spectators in water", Localize("Fade spectators in water")},
			{&g_Config.m_QmDummyDistanceLink, Localizable("Show dummy distance link"), Localize("Show dummy distance link")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);

	static CSettingsConditionalRows s_AutoTeamLockRows;
	if(s_AutoTeamLockRows.Update(g_Config.m_QmAutoTeamLock, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope AutoTeamLockRevealScope(Ui(), Flow, s_AutoTeamLockRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("hj-assist-1", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-hj-assist-lock-delay", &LabelColumn, Localize("Auto-lock delay (seconds)"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static int s_QmAutoTeamLockDelayInputId;
			RenderQmSettingsSliderWithValueInput(&s_QmAutoTeamLockDelayInputId, ControlColumn, &g_Config.m_QmAutoTeamLockDelay, 0, 30, "s", PrewarmOnly);
		}
	}
	static CSettingsConditionalRows s_PausedSpectatorFadeRows;
	if(s_PausedSpectatorFadeRows.Update(g_Config.m_QmPausedSpectatorFade, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope PausedSpectatorFadeRevealScope(Ui(), Flow, s_PausedSpectatorFadeRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("hj-assist-2", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-hj-assist-spectator-alpha", &LabelColumn, Localize("Spectator opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static int s_QmPausedSpectatorAlphaInputId;
			RenderQmSettingsSliderWithValueInput(&s_QmPausedSpectatorAlphaInputId, ControlColumn, &g_Config.m_QmPausedSpectatorAlpha, 0, 100, "%", PrewarmOnly);
		}
	}
	static CSettingsConditionalRows s_DistanceLinkRows;
	if(s_DistanceLinkRows.Update(g_Config.m_QmDummyDistanceLink, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope RevealScope(Ui(), Flow, s_DistanceLinkRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("hj-assist-link-style", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "hj-assist-link-style", &LabelColumn, Localize("Link style"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static CButtonContainer s_aStyleButtons[2];
			const char *apLabels[] = {Localize("Line"), Localize("Droplet")};
			const int Style = DoSegmentedChoice(s_aStyleButtons, apLabels, std::size(apLabels), g_Config.m_QmDummyDistanceLinkStyle, ControlColumn, ui_token::radius::BASE, nullptr, BodySize, &g_Config.m_QmDummyDistanceLinkStyle);
			if(Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly() && Style >= 0)
				g_Config.m_QmDummyDistanceLinkStyle = Style;
		}
	}
}

// 功能分类卡片统一声明内容行；原页面与搜索页共享同帧输入、最终布局和绘制。
namespace qm_card_catalog
{
	uint64_t BlockWordsLayoutRevision()
	{
		// 原页面和搜索页都从配置同步，不依赖先打开功能页来刷新高度。
		static std::string s_PreviousWords;
		static uint64_t s_Revision = 1;
		if(s_PreviousWords != g_Config.m_QmBlockWordsList)
		{
			s_PreviousWords = g_Config.m_QmBlockWordsList;
			++s_Revision;
		}
		return s_Revision;
	}

	bool BuildFunctionCard(const SQmCardBuildContext &Ctx, const qm_module::EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		using qm_module::EQmModuleId;
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
			MakeModuleCard(
				Ctx,
				ModuleId,
				pStableId,
				pTitle,
				pSubtitle,
				Render,
				Out);
			UseDeclaredModuleContent(Ctx, Out);
		};

		switch(Id)
		{
		case EQmModuleId::Voice:
			Add(Id, "qm:voice", "Voice", "Voice chat settings and diagnostics", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { QmCardRenderHook::RenderQmHudVoiceContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			Out.m_HasPendingPreLayoutInput = VoiceHasPendingInput;
			return true;
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
		case EQmModuleId::Ime:
			Add(Id, "qm:ime", "IME", "Input method candidate bar", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionImeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::MiniFeatures:
		{
			const auto RenderMiniFeatures = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) {
				QmCardRenderHook::RenderQmFunctionMiniFeaturesContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
			};
			Add(Id, "qm:mini_features", "Dream Features", "Only what you can't imagine, nothing Dream can't do",
				[RenderMiniFeatures, ReadOnly](CUIRect &Content) { RenderMiniFeatures(Content, ReadOnly); });
			return true;
		}
		case EQmModuleId::BetterScoreboard:
			Add(Id, "qm:better_scoreboard", "Better scoreboard", "Scoreboard", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionBetterScoreboardContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::JumpHint:
		{
			const auto RenderJumpHint = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionJumpHintContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			Add(Id, "qm:jump_hint", "Position jump hint", "Jump hint text",
				[RenderJumpHint, ReadOnly](CUIRect &Content) { RenderJumpHint(Content, ReadOnly); });
			return true;
		}
		case EQmModuleId::WeaponTrajectory:
		{
			const auto RenderWeaponTrajectory = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionWeaponTrajectoryContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			Add(Id, "qm:weapon_trajectory", "Weapon Trajectory", "Show grenade and laser trajectory preview",
				[RenderWeaponTrajectory, ReadOnly](CUIRect &Content) { RenderWeaponTrajectory(Content, ReadOnly); });
			return true;
		}
		case EQmModuleId::FriendNotify:
		{
			const auto RenderFriendNotify = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect Row;
				// 行距由行自己声明；这是本卡第一行，声明阶段会自动归零。
				if(Flow.Control("spectator-friends-first", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
					QmCardRenderHook::RenderQmFunctionCheckboxRow(pMenus, Row, LineHeight, 0, &g_Config.m_QmSpectatorFriendsFirst, "List friends first in the spectator menu", Localize("List friends first in the spectator menu"), &g_Config.m_QmSpectatorFriendsFirst, PrewarmOnly);
				QmCardRenderHook::RenderQmFunctionFriendNotifyContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
				// 好友码控件仍是「行后补 Space」的写法，这里补一档边界行距。
				Flow.Space(ui_token::settings::RowGap(LineSpacing));
				QmCardRenderHook::RenderFriendCodeControls(pMenus, Content, PrewarmOnly);
			};
			Add(Id, "qm:friend_notify", "Friend Notifications", "Friend online and join notifications",
				[RenderFriendNotify, ReadOnly](CUIRect &Content) { RenderFriendNotify(Content, ReadOnly); });
			return true;
		}
		case EQmModuleId::BlockWords:
			Add(Id, "qm:block_words", "Word Filter", "Chat word filtering", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionBlockWordsContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::Translate:
		{
			Add(Id, "qm:translate", "Translate", "Chat translation settings", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionTranslateContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		}
		case EQmModuleId::TranslateUi:
			Add(Id, "qm:translate_ui", "Translate button", "Customize translate button and menu colors", [pMenus, LineHeight, BodySize, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualTranslateUiContent(pMenus, Content, LineHeight, BodySize, LineSpacing); });
			return true;
		case EQmModuleId::QiaFen:
			Add(Id, "qm:qiafen", "Keyword Reply", "I am a robot", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionKeywordReplyContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			Out.m_HasPendingPreLayoutInput = KeywordReplyHasPendingInput;
			return true;
		case EQmModuleId::PieMenu:
			Add(Id, "qm:pie_menu", "Pie Menu", "Quick action menu for players", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly](CUIRect &Content) {
				qm_card_catalog::QmCardRenderHook::RenderQmFunctionPieMenuContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly);
			});
			return true;
		case EQmModuleId::MapUpload:
			Add(Id, "qm:map_upload", "Map upload", "Upload a saved map to the public test server", [pMenus, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionMapUploadContent(pMenus, Content, LineHeight, BodySize, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::HJAssist:
		{
			const auto RenderHJAssist = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionHJAssistContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			Add(Id, "qm:hj_assist", "HJ Assist", "What's done is done, no use saying more",
				[RenderHJAssist, ReadOnly](CUIRect &Content) { RenderHJAssist(Content, ReadOnly); });
			return true;
		}
		case EQmModuleId::SoloSplit:
		{
			const auto RenderSoloSplit = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionSoloSplitContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly); };
			Add(Id, "qm:solo_split", "Solo Split", "Split main and dummy into different teams for solo-play",
				[RenderSoloSplit, ReadOnly](CUIRect &Content) { RenderSoloSplit(Content, ReadOnly); });
			return true;
		}
		default:
			return false;
		}
	}
} // namespace qm_card_catalog
