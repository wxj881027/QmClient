#include "QmCardCatalogFunctionMetrics.h"
#include "QmCardCatalogInternal.h"
#include "QmCardMeasureRevision.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

void CMenus::RenderQmFunctionBlockWordsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmBlockWordsShowConsole, Localizable("Show blocked words in console"), &g_Config.m_QmBlockWordsShowConsole);
	static CButtonContainer s_BlockWordsConsoleColorId;
	DoLine_ColorPicker(&s_BlockWordsConsoleColorId, CurrentSettingsContentMetrics(), &Content, Localize("Console color"), &g_Config.m_QmBlockWordsConsoleColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
	RenderCheckbox(&g_Config.m_QmBlockWordsEnabled, Localizable("Enable word filter list"), &g_Config.m_QmBlockWordsEnabled);

	const int BlockWordsAction = g_Config.m_QmBlockWordsAction;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-action", &LabelColumn, Localize("Behavior"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	CUIRect ActionRow = ControlColumn;
	CUIRect ActionButton;
	static CButtonContainer s_BlockWordsActionReplace, s_BlockWordsActionHide;
	const float ActionWidth = ActionRow.w / 2.0f;
	ActionRow.VSplitLeft(ActionWidth, &ActionButton, &ActionRow);
	if(DoButtonLineSize_Menu(&s_BlockWordsActionReplace, Localize("Replace mode"), BlockWordsAction == 0, &ActionButton, LineHeight, false, 0, IGraphics::CORNER_L, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_QmBlockWordsAction = 0;
	if(DoButtonLineSize_Menu(&s_BlockWordsActionHide, Localize("Hide player messages"), BlockWordsAction == 1, &ActionRow, LineHeight, false, 0, IGraphics::CORNER_R, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_QmBlockWordsAction = 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(BlockWordsAction == 0)
	{
		RenderCheckbox(&g_Config.m_QmBlockWordsMultiReplace, Localizable("Use multi-char replacement based on word length"), &g_Config.m_QmBlockWordsMultiReplace);

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
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-replacement-chars", &LabelColumn, Localize("Replacement chars"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		if(ui_widget::InputField(ReplacementInputCtx, &s_BlockWordsReplaceInput, ControlColumn, "*", BodySize))
		{
			char aReplacement[8];
			str_utf8_truncate(aReplacement, sizeof(aReplacement), s_BlockWordsReplaceInput.GetString(), 1);
			if(aReplacement[0] == '\0')
				str_copy(aReplacement, "*", sizeof(aReplacement));
			str_copy(g_Config.m_QmBlockWordsReplacementChar, aReplacement, sizeof(g_Config.m_QmBlockWordsReplacementChar));
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-match-mode", &LabelColumn, Localize("Mode"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		CUIRect ModeRow = ControlColumn;
		CUIRect ModeButton;
		static CButtonContainer s_BlockWordsModeRegex, s_BlockWordsModeFull, s_BlockWordsModeBoth;
		const float ModeWidth = ModeRow.w / 3.0f;
		ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
		if(DoButtonLineSize_Menu(&s_BlockWordsModeRegex, Localize("Regular expression"), g_Config.m_QmBlockWordsMode == 0, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_L, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 0;
		ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
		if(DoButtonLineSize_Menu(&s_BlockWordsModeFull, Localize("Literal"), g_Config.m_QmBlockWordsMode == 1, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_NONE, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 1;
		if(DoButtonLineSize_Menu(&s_BlockWordsModeBoth, Localize("Both"), g_Config.m_QmBlockWordsMode == 2, &ModeRow, LineHeight, false, 0, IGraphics::CORNER_R, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 2;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	static CLineInputBuffered<1024> s_BlockWordsInput;
	static bool s_BlockWordsInited = false;
	if(!s_BlockWordsInited)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
		s_BlockWordsInited = true;
	}
	else if(!s_BlockWordsInput.IsActive() && str_comp(s_BlockWordsInput.GetString(), g_Config.m_QmBlockWordsList) != 0)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
	}
	s_BlockWordsInput.SetEmptyText(Localize("Separate with commas"));
	const float InputLineSpacing = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const float InputHeight = qm_card_catalog::CalcQiaFenInputHeight(TextRender(), s_BlockWordsInput.GetString(), Content.w - LabelWidth, BodySize, InputLineSpacing, LineHeight);
	Content.HSplitTop(InputHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
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
				return !g_Config.m_QmFreezeChatEnabled ? Row() : Row() * (g_Config.m_QmFreezeChatEmoticon ? 5.0f : 4.0f);
			case EQmModuleId::Gores:
				return Row() * (3.0f + (g_Config.m_QmAxiomAutoLogin ? 2.0f : 0.0f) + ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 7.0f : 0.0f)) + LineHeight;
			case EQmModuleId::KeyBinds: return Rows(8.0f);
			case EQmModuleId::Emoticons: return Rows(3.0f);
			case EQmModuleId::Ime: return Rows(8.0f);
			case EQmModuleId::BetterScoreboard: return Rows(5.0f);
			case EQmModuleId::BlockWords: return Row() * (g_Config.m_QmBlockWordsAction == 0 ? 7.0f : 4.0f) + CalcQiaFenInputHeight(QmCardRenderHook::TextRenderer(pMenus), g_Config.m_QmBlockWordsList, std::max(1.0f, ContentWidth - LabelWidth), BodySize, std::clamp(2.0f * UiScale, 1.0f, 2.0f), LineHeight);
			case EQmModuleId::Translate:
			{
				// 行数口径与 RenderQmFunctionTranslateContent 的渲染分支一一对应，逐块注释；
				// 此处无字体上下文时按单行估算；运行时探针复用说明行的实际换行测高。
				// 本公式是卡片自身按行计算高度的能力，仅在无菜单上下文时作为兜底。
				const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
				const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
				const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
				const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
				const bool IsMymemoryBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0;
				const bool IsBaiduBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "baidu") == 0;
				const bool IsDeeplBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0;
				const bool ShowAdvanced = g_Config.m_QmTranslateShowAdvanced != 0;
				// 常驻行：收/发自动翻译、译文字号、翻译服务、收/发目标语言、高级选项开关。
				float Height = Rows(9.0f); // 常驻测试按钮与诊断开关
				if(IsBaiduBackend)
					Height += Row() * 2.0f;
				if(IsMymemoryBackend)
					Height += Metrics.m_SmallSize + LineSpacing; // MyMemory 免注册说明
				else if(IsDeeplBackend)
					Height += Row() + Metrics.m_SmallSize + LineSpacing; // DeepL 说明 + API key 行
				if(IsFtapiBackend)
					Height += Row() + LineHeight * 0.8f + LineSpacing; // 自动翻译开关 + 警告文案
				if(IsTencentCloudBackend)
					Height += Row() * 4.0f; // Endpoint/Region/SecretId/SecretKey
				else if(IsLibreTranslateBackend)
					Height += Row() * 2.0f; // Endpoint/API key
				if(IsLlmBackend)
				{
					Height += Row() * 3.0f; // Provider/API key/Model
					if(g_Config.m_QmTranslateLlmProvider == 3)
						Height += Row() * 3.0f + Metrics.m_SmallSize + LineSpacing;
					if(ShowAdvanced || g_Config.m_QmTranslateLlmProvider == 3)
						Height += Row(); // Endpoint (optional)：Custom 常显，其余高级展开时显示
					if(ShowAdvanced)
					{
						Height += Row() * 4.0f; // 并发、有效并发、思考模式开关、自定义提示词
						if(g_Config.m_QmTranslateLlmProvider == 0)
							Height += Metrics.m_SmallSize + LineSpacing; // 智谱免费档并发提示
						if(g_Config.m_QmTranslateLlmEnableThinking && (g_Config.m_QmTranslateLlmProvider == 2 || g_Config.m_QmTranslateLlmProvider == 3))
							Height += Metrics.m_SmallSize + LineSpacing; // 思考模式提示
					}
				}
				if(ShowAdvanced)
					Height += LineHeight + LineSpacing * 0.5f + Row() * 4.0f; // 最小匹配字符 + 语言占比/原文语言/收发翻译方式
				return Height;
			}
			case EQmModuleId::TranslateUi: return Rows(6.0f);
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
			const uint64_t Revision = MeasureModuleCardRevision(Id, Ctx.m_pFunctionLayout != nullptr ? *Ctx.m_pFunctionLayout : SQmFunctionCardLayoutState{});
			return Id == EQmModuleId::Translate && Ctx.m_pMenus ? Revision ^ (Ctx.m_pMenus->TranslationTestLayoutRevision() << 16) : Revision;
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
		case EQmModuleId::Ime:
			Add(Id, "qm:ime", "IME", "Input method candidate bar", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmFunctionImeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
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
			Add(Id, "qm:pie_menu", "Pie Menu", "Quick action menu for players", [pMenus, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly, Page = Ctx.m_Page](CUIRect &Content) {
				const bool FullWidth = !Page.m_TwoColumns || Content.w > (Page.m_ContentViewport.w + Page.m_aColumns[0].w) * 0.5f - 2.0f * CardPadding;
				qm_card_catalog::QmCardRenderHook::RenderQmFunctionPieMenuContent(pMenus, Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, ReadOnly, FullWidth ? 4 : 2);
			});
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
