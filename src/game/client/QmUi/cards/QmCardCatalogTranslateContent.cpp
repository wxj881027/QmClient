#include "QmCardCatalogInternal.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace FontIcons;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;

void CMenus::RenderQmFunctionTranslateContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float CardLabelWidth, bool PrewarmOnly)
{
	// 标签宽度必须按当前卡片内容计算；沿用整页宽度会把端点和模型输入框压缩到不可读。
	const float LabelWidth = std::min({CardLabelWidth, Content.w * 0.45f, std::clamp(Content.w * 0.30f, 150.0f, 260.0f)});
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	CUIRect Row, LabelCol, ControlCol;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_translate_text_inputs", BodySize / ui_token::font::BODY);
	auto RenderCheckbox = [this, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float VMargin) {
		CUIRect CheckBoxRect;
		pRect->HSplitTop(VMargin, &CheckBoxRect, pRect);
		return RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &CheckBoxRect, PrewarmOnly);
	};
	auto RenderLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign = TEXTALIGN_ML, const SLabelProperties &LabelProps = {}) {
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly, &Row, &LabelCol](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pId, &Row, pValue, &LabelCol);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};
	const auto RenderHelp = [this, &Content, LabelWidth, SmallSize, LineSpacing](const char *pText, float FontSize = 0.0f) {
		const float TextSize = FontSize > 0.0f ? FontSize : SmallSize;
		const CUIRect HelpRow = ConsumeSettingsWrappedTextRow(Content, LabelWidth, TextSize, LineSpacing, [this, pText, TextSize](float Width) {
			return TextRender()->TextBoundingBox(TextSize, pText, -1, Width).m_H + TextSize * 0.25f;
		});
		SLabelProperties Props;
		Props.m_MaxWidth = HelpRow.w;
		Props.m_EnableWidthCheck = false;
		Ui()->DoLabel(&HelpRow, pText, TextSize, TEXTALIGN_ML, Props);
	};
	// 标题和文本输入共用整行提示，内部输入不改变已登记的标题锚点。
	const auto RenderTextInput = [this, &TextInputCtx, &Row, &LabelCol](CLineInput *pInput, const CUIRect &Rect, auto &&...Arguments) {
		if(pInput != nullptr)
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pInput, &Row, pInput->GetString(), &LabelCol);
		return ui_widget::InputField(TextInputCtx, pInput, Rect, std::forward<decltype(Arguments)>(Arguments)...);
	};
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateAuto, "Auto translate received messages", Localize("Auto translate received messages"), &g_Config.m_QmTranslateAuto, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateAutoOutgoing, "Auto translate sent messages", Localize("Auto translate sent messages"), &g_Config.m_QmTranslateAutoOutgoing, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	RenderLabel("qmclient-translation-text-size", &LabelCol, Localize("Translated text size"), BodySize);
	RenderSliderWithValueInput(&g_Config.m_QmChatTranslationSize, ControlCol, &g_Config.m_QmChatTranslationSize, 50, 100, "%");
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	const auto TranslateBackendDropDownNames = NTranslateUi::NamesWithCustom(NTranslateUi::BackendNames());
	if(!PrewarmOnly && !Ui()->RenderOnly())
		NTranslateUi::NormalizeBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend));
	static CUi::SDropDownState s_TranslateBackendDropDownState;
	static CScrollRegion s_TranslateBackendDropDownScrollRegion;
	s_TranslateBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TranslateBackendDropDownScrollRegion;

	const int BackendSelectedOld = NTranslateUi::BackendIndexForDisplay(g_Config.m_QmTranslateBackend);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	CUIElement &TranslationServiceLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-translation-service");
	DoSettingsLabelStreamed(TranslationServiceLabel, &LabelCol, Localize("Translation service"), BodySize, TEXTALIGN_ML);
	const int BackendSelectedNew = DoSettingsDropDown(&ControlCol, BackendSelectedOld, TranslateBackendDropDownNames.data(), TranslateBackendDropDownNames.size(), s_TranslateBackendDropDownState, {}, g_Config.m_QmTranslateBackend, nullptr, &Row);
	if(!PrewarmOnly && !Ui()->RenderOnly())
		NTranslateUi::CommitBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend), BackendSelectedOld, BackendSelectedNew);
	const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
	const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
	const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
	const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
	const bool IsMymemoryBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0;
	const bool IsBaiduBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "baidu") == 0;
	const bool IsDeeplBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// MyMemory 免注册说明
	if(IsMymemoryBackend)
	{
		GameClient()->m_Tooltips.DoToolTipForRect(&s_TranslateBackendDropDownState, &Row, Localize("MyMemory needs no registration (anonymous daily quota)"));
	}

	// DeepL 说明与 API Key 输入
	if(IsDeeplBackend)
	{
		GameClient()->m_Tooltips.DoToolTipForRect(&s_TranslateBackendDropDownState, &Row, Localize("DeepL API quota depends on your subscription. Get an API key at deepl.com."));

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-deepl-key", &LabelCol, Localize("API key"), BodySize);
		static CLineInput s_TranslateDeeplKey(g_Config.m_QmTranslateDeeplKey, sizeof(g_Config.m_QmTranslateDeeplKey));
		s_TranslateDeeplKey.SetHidden(true);
		RenderTextInput(&s_TranslateDeeplKey, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	if(IsBaiduBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-baidu-app-id", &LabelCol, Localize("APP ID"), BodySize);
		static CLineInput s_TranslateBaiduAppId(g_Config.m_QmTranslateBaiduAppId, sizeof(g_Config.m_QmTranslateBaiduAppId));
		RenderTextInput(&s_TranslateBaiduAppId, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-baidu-key", &LabelCol, Localize("API key"), BodySize);
		static CLineInput s_TranslateBaiduKey(g_Config.m_QmTranslateBaiduKey, sizeof(g_Config.m_QmTranslateBaiduKey));
		s_TranslateBaiduKey.SetHidden(true);
		RenderTextInput(&s_TranslateBaiduKey, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// 探测通过相同生产后端执行；预热和 RenderOnly 不创建请求或延长 owner。
	if(!PrewarmOnly && !Ui()->RenderOnly())
		m_TranslateProbe.Touch();
	Content.HSplitTop(LineHeight, &Row, &Content);
	static CButtonContainer s_TestTranslationButton;
	static CButtonContainer s_CancelTranslationTestButton;
	const bool PendingTest = m_TranslateProbe.Pending();
	if(PendingTest)
	{
		if(!PrewarmOnly && !Ui()->RenderOnly() && DoButton_Menu(&s_CancelTranslationTestButton, Localize("Cancel translation test"), 0, &Row))
			m_TranslateProbe.Cancel();
	}
	else if(!PrewarmOnly && !Ui()->RenderOnly() && DoButton_Menu(&s_TestTranslationButton, Localize("Test translation"), 0, &Row))
		m_TranslateProbe.Start(*Http(), "Hello, please hook me.", g_Config.m_QmTranslateTarget, "en");
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&m_TranslateProbeDiagnostics, "Translation diagnostics", Localize("Translation diagnostics"), &m_TranslateProbeDiagnostics, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(m_TranslateProbe.Pending())
		RenderHelp(Localize("Testing translation..."));
	else if(m_TranslateProbe.HasResult())
	{
		const CTranslateResponse &Result = m_TranslateProbe.Response();
		if(Result.m_Error)
			RenderHelp(Localize(TranslateNoticeSource(Result.m_Notice == ETranslateNotice::NONE ? ETranslateNotice::INVALID_RESPONSE : Result.m_Notice)));
		else
			RenderHelp(Result.m_Text);
		if(m_TranslateProbeDiagnostics)
		{
			char aDiagnostic[128];
			str_format(aDiagnostic, sizeof(aDiagnostic), "%s | HTTP %d | %s", m_TranslateProbe.Service(), Result.m_HttpStatus, Result.m_Error ? Localize("Failed") : Localize("Success"));
			RenderHelp(aDiagnostic);
		}
	}

	if(m_TranslateProbeDiagnostics)
	{
		const STranslateDiagnostic &Diagnostic = GameClient()->m_Translate.LastDiagnostic();
		if(Diagnostic.m_aService[0])
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s | HTTP %d", Diagnostic.m_aService, Diagnostic.m_HttpStatus);
			RenderHelp(aStatus);
			RenderHelp(Localize(TranslateNoticeSource(Diagnostic.m_Notice)));
		}
	}

	// FTAPI 自动翻译开关（仅在 FTAPI 后端时显示）
	if(IsFtapiBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderCheckbox(&g_Config.m_QmTranslateFtapiAutoEnable, "Enable FTAPI auto-translate (may overload the service)", Localize("Enable FTAPI auto-translate (may overload the service)"), &g_Config.m_QmTranslateFtapiAutoEnable, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// FTAPI 警告提示
		GameClient()->m_Tooltips.DoToolTipForRect(&g_Config.m_QmTranslateFtapiAutoEnable, &Row, Localize("⚠️ FTAPI is a free service. Excessive use may cause service suspension."));
	}

	auto RenderLanguageDropDownWithCustomInput = [this, BodySize, PrewarmOnly, &RenderTextInput](const CUIRect &HelpRow, const CUIRect &ControlColumn, const char *const *apNames, const char *const *apCodes, int Count, CUi::SDropDownState &DropDownState, char *pConfigValue, size_t ConfigValueSize, CLineInput &LineInput, const char *pEmptyText) {
		CUIRect DropRect, EditRect;
		ControlColumn.VSplitMid(&DropRect, &EditRect);
		DropRect.VMargin(1.0f, &DropRect);
		EditRect.VMargin(1.0f, &EditRect);

		auto FindIndex = [](const char *pValue, const char *const *apConfigCodes, int ConfigCodeCount) -> int {
			for(int i = 0; i < ConfigCodeCount; ++i)
			{
				if(str_comp(pValue, apConfigCodes[i]) == 0)
					return i;
			}
			return -1;
		};

		const int OldSel = FindIndex(pConfigValue, apCodes, Count);
		// 自定义语言代码不能在普通重绘时被第一项覆盖；显式展示“自定义”项。
		std::vector<const char *> vNames(apNames, apNames + Count);
		vNames.push_back(Localize("Custom…"));
		const int SelectedIndex = NTranslateUi::CustomSelectionIndex(OldSel, Count);
		const int NewSel = DoSettingsDropDown(&DropRect, SelectedIndex, vNames.data(), static_cast<int>(vNames.size()), DropDownState, {}, pConfigValue, nullptr, &HelpRow);
		if(!PrewarmOnly && !Ui()->RenderOnly())
			NTranslateUi::CommitSelection(pConfigValue, ConfigValueSize, apCodes, Count, SelectedIndex, NewSel);

		if(!LineInput.IsActive() && str_comp(LineInput.GetString(), pConfigValue) != 0)
			LineInput.Set(pConfigValue);
		LineInput.SetEmptyText(pEmptyText);
		const bool WasActive = LineInput.IsActive();
		const bool SubmitPressed = !PrewarmOnly && (Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
		const bool ClickedOutside = !PrewarmOnly && (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)) && !Ui()->MouseHovered(&EditRect);
		ui_widget::STextFieldOptions LanguageInputOptions;
		LanguageInputOptions.m_pPlaceholder = pEmptyText;
		LanguageInputOptions.m_FontSize = BodySize;
		LanguageInputOptions.m_Corners = IGraphics::CORNER_ALL;
		LanguageInputOptions.m_TextAlign = TEXTALIGN_MC;
		RenderTextInput(&LineInput, EditRect, LanguageInputOptions);
		if(WasActive && (SubmitPressed || ClickedOutside))
		{
			str_copy(pConfigValue, LineInput.GetString(), ConfigValueSize);
		}
	};
	auto RenderSliderWithNumberInput = [&RenderSliderWithValueInput](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix);
	};

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	CUIElement &TargetLanguageLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-target-language");
	DoSettingsLabelStreamed(TargetLanguageLabel, &LabelCol, Localize("Translate received messages to"), BodySize, TEXTALIGN_ML);

	// 下拉框 + 输入框组合
	{
		const auto &LangNames = NTranslateUi::LanguageNames();
		const auto &LangCodes = NTranslateUi::LanguageCodes();
		static CUi::SDropDownState s_TargetLangDropDown;

		static CLineInput s_TranslateTarget(g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget));
		RenderLanguageDropDownWithCustomInput(Row, ControlCol, LangNames.data(), LangCodes.data(), LangCodes.size(), s_TargetLangDropDown, g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget), s_TranslateTarget, "zh");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	// Endpoint 配置 - 根据后端类型显示不同的端点输入
	if(IsTencentCloudBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-tencent-endpoint", &LabelCol, Localize("Endpoint"), BodySize);
		static CLineInput s_TranslateEndpoint(g_Config.m_QmTranslateTcEndpoint, sizeof(g_Config.m_QmTranslateTcEndpoint));
		s_TranslateEndpoint.SetEmptyText("https://tmt.tencentcloudapi.com/");
		RenderTextInput(&s_TranslateEndpoint, ControlCol, "https://tmt.tencentcloudapi.com/", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	else if(IsLibreTranslateBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-aliyun-endpoint", &LabelCol, Localize("Endpoint"), BodySize);
		static CLineInput s_TranslateEndpoint(g_Config.m_QmTranslateLibreEndpoint, sizeof(g_Config.m_QmTranslateLibreEndpoint));
		s_TranslateEndpoint.SetEmptyText("http://localhost:5000");
		RenderTextInput(&s_TranslateEndpoint, ControlCol, "http://localhost:5000", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	// LLM 后端的端点配置在 Provider 选择区域显示

	if(IsTencentCloudBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-region", &LabelCol, Localize("Region"), BodySize);
		static CLineInput s_TranslateRegion(g_Config.m_QmTranslateTcRegion, sizeof(g_Config.m_QmTranslateTcRegion));
		s_TranslateRegion.SetEmptyText("ap-guangzhou");
		RenderTextInput(&s_TranslateRegion, ControlCol, "ap-guangzhou", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-secret-id", &LabelCol, Localize("SecretId"), BodySize);
		static CLineInput s_TranslateSecretId(g_Config.m_QmTranslateTcSecretId, sizeof(g_Config.m_QmTranslateTcSecretId));
		s_TranslateSecretId.SetEmptyText(Localize("Tencent Cloud SecretId"));
		RenderTextInput(&s_TranslateSecretId, ControlCol, Localize("Tencent Cloud SecretId"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-secret-key", &LabelCol, Localize("SecretKey"), BodySize);
		static CLineInput s_TranslateSecretKey(g_Config.m_QmTranslateTcSecretKey, sizeof(g_Config.m_QmTranslateTcSecretKey));
		s_TranslateSecretKey.SetEmptyText(Localize("Tencent Cloud SecretKey"));
		s_TranslateSecretKey.SetHidden(true);
		RenderTextInput(&s_TranslateSecretKey, ControlCol, Localize("Tencent Cloud SecretKey"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	else if(IsLibreTranslateBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-api-key", &LabelCol, Localize("API key"), BodySize);
		static CLineInput s_TranslateKey(g_Config.m_QmTranslateLibreKey, sizeof(g_Config.m_QmTranslateLibreKey));
		s_TranslateKey.SetHidden(true);
		RenderTextInput(&s_TranslateKey, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	if(IsLlmBackend)
	{
		// LLM Provider 选择
		const std::array<const char *, 4> LlmProviderDropDownNames = {
			Localize("Zhipu AI"),
			Localize("DeepSeek"),
			Localize("OpenAI"),
			Localize("Custom")};
		static CUi::SDropDownState s_LlmProviderDropDownState;
		static CScrollRegion s_LlmProviderDropDownScrollRegion;
		s_LlmProviderDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_LlmProviderDropDownScrollRegion;

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		CUIElement &LlmProviderLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-llm-provider");
		DoSettingsLabelStreamed(LlmProviderLabel, &LabelCol, Localize("LLM provider"), BodySize, TEXTALIGN_ML);
		const int NewProvider = DoSettingsDropDown(&ControlCol, g_Config.m_QmTranslateLlmProvider, LlmProviderDropDownNames.data(), LlmProviderDropDownNames.size(), s_LlmProviderDropDownState, {}, &g_Config.m_QmTranslateLlmProvider, nullptr, &Row);
		// 写回前校验范围，防止异常返回值（如越界防御收敛出的 -1）污染配置
		if(!PrewarmOnly && !Ui()->RenderOnly() && NewProvider != g_Config.m_QmTranslateLlmProvider && NewProvider >= 0 && NewProvider < static_cast<int>(LlmProviderDropDownNames.size()))
		{
			g_Config.m_QmTranslateLlmProvider = NewProvider;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_QmTranslateLlmProvider == 3)
		{
			const std::array<const char *, 2> AuthNames = {Localize("Bearer API key"), Localize("Local service without authentication")};
			static CUi::SDropDownState s_LlmCustomAuth;
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
			RenderLabel("qmclient-llm-authentication", &LabelCol, Localize("Authentication"), BodySize);
			const int Auth = DoSettingsDropDown(&ControlCol, g_Config.m_QmTranslateLlmCustomAuth, AuthNames.data(), AuthNames.size(), s_LlmCustomAuth, {}, &g_Config.m_QmTranslateLlmCustomAuth, nullptr, &Row);
			if(!PrewarmOnly && !Ui()->RenderOnly() && Auth >= 0 && Auth < 2)
				g_Config.m_QmTranslateLlmCustomAuth = Auth;
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			const std::array<const char *, 4> ThinkingNames = {Localize("Use server defaults"), "thinking", "enable_thinking", "chat_template_kwargs"};
			static CUi::SDropDownState s_LlmCustomThinking;
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
			RenderLabel("qmclient-llm-thinking-parameters", &LabelCol, Localize("Thinking parameters"), BodySize);
			const int Thinking = DoSettingsDropDown(&ControlCol, g_Config.m_QmTranslateLlmCustomThinking, ThinkingNames.data(), ThinkingNames.size(), s_LlmCustomThinking, {}, &g_Config.m_QmTranslateLlmCustomThinking, nullptr, &Row);
			GameClient()->m_Tooltips.DoToolTipForRect(&s_LlmCustomThinking, &Row, Localize("Thinking controls depend on the model server. Server defaults do not guarantee that thinking is disabled."));
			if(!PrewarmOnly && !Ui()->RenderOnly() && Thinking >= 0 && Thinking < 4)
				g_Config.m_QmTranslateLlmCustomThinking = Thinking;
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			RenderCheckbox(&g_Config.m_QmTranslateLlmCustomParameters, "Send sampling and token limit parameters", Localize("Send sampling and token limit parameters"), &g_Config.m_QmTranslateLlmCustomParameters, &Row, LineHeight);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
		}

		// 各 Provider 的 API Key 输入框（静态变量，分别绑定到不同配置）
		static CLineInput s_LlmApiKeyZhipu(g_Config.m_QmTranslateLlmKeyZhipu, sizeof(g_Config.m_QmTranslateLlmKeyZhipu));
		static CLineInput s_LlmApiKeyDeepseek(g_Config.m_QmTranslateLlmKeyDeepseek, sizeof(g_Config.m_QmTranslateLlmKeyDeepseek));
		static CLineInput s_LlmApiKeyOpenai(g_Config.m_QmTranslateLlmKeyOpenai, sizeof(g_Config.m_QmTranslateLlmKeyOpenai));
		static CLineInput s_LlmApiKeyCustom(g_Config.m_QmTranslateLlmKeyCustom, sizeof(g_Config.m_QmTranslateLlmKeyCustom));

		// 根据 Provider 显示对应的 API Key 输入框
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);

		CLineInput *pActiveKeyInput = nullptr;
		// 各分支都会赋值，默认分支覆盖其余取值，无需初始值。
		const char *pKeyLabel;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			pKeyLabel = Localize("Zhipu API key");
			s_LlmApiKeyZhipu.SetEmptyText("ZHIPU_API_KEY");
			s_LlmApiKeyZhipu.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyZhipu;
			break;
		case 1: // DeepSeek
			pKeyLabel = Localize("DeepSeek API key");
			s_LlmApiKeyDeepseek.SetEmptyText("DEEPSEEK_API_KEY");
			s_LlmApiKeyDeepseek.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyDeepseek;
			break;
		case 2: // OpenAI
			pKeyLabel = Localize("OpenAI API key");
			s_LlmApiKeyOpenai.SetEmptyText("OPENAI_API_KEY");
			s_LlmApiKeyOpenai.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyOpenai;
			break;
		case 3: // Custom
		default:
			pKeyLabel = Localize("Custom API key");
			s_LlmApiKeyCustom.SetEmptyText("API_KEY");
			s_LlmApiKeyCustom.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyCustom;
			break;
		}

		Ui()->DoLabel(&LabelCol, pKeyLabel, BodySize, TEXTALIGN_ML);
		if(pActiveKeyInput)
			RenderTextInput(pActiveKeyInput, ControlCol, pActiveKeyInput->GetEmptyText(), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 各 Provider 的模型配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-model", &LabelCol, Localize("Model"), BodySize);

		static CLineInput s_LlmModelZhipu(g_Config.m_QmTranslateLlmModelZhipu, sizeof(g_Config.m_QmTranslateLlmModelZhipu));
		static CLineInput s_LlmModelDeepseek(g_Config.m_QmTranslateLlmModelDeepseek, sizeof(g_Config.m_QmTranslateLlmModelDeepseek));
		static CLineInput s_LlmModelOpenai(g_Config.m_QmTranslateLlmModelOpenai, sizeof(g_Config.m_QmTranslateLlmModelOpenai));
		static CLineInput s_LlmModelCustom(g_Config.m_QmTranslateLlmModelCustom, sizeof(g_Config.m_QmTranslateLlmModelCustom));

		CLineInput *pActiveModelInput = nullptr;
		char *pModelConfigValue = nullptr;
		size_t ModelConfigSize = 0;
		const char *pModelEmptyText = "model-name";
		// 非空表示该 Provider 提供预设模型列表（下拉 + 自定义输入组合）
		std::vector<const char *> vModelPresets;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			pModelEmptyText = "glm-4.7-flash";
			s_LlmModelZhipu.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelZhipu;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelZhipu;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelZhipu);
			vModelPresets = {"glm-4.7-flash", "glm-4.7-flashx", "glm-4.7", "glm-5.1"};
			break;
		case 1: // DeepSeek
			pModelEmptyText = "deepseek-flash";
			s_LlmModelDeepseek.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelDeepseek;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelDeepseek;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelDeepseek);
			vModelPresets = {"deepseek-flash", "deepseek-v4-pro"};
			break;
		case 2: // OpenAI
			pModelEmptyText = "gpt-5.6-luna";
			s_LlmModelOpenai.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelOpenai;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelOpenai;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelOpenai);
			vModelPresets = {"gpt-5.6-luna", "gpt-5.6-terra", "gpt-5.6-sol"};
			break;
		case 3: // Custom
		default:
			s_LlmModelCustom.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelCustom;
			break;
		}
		if(pActiveModelInput && vModelPresets.empty())
		{
			// 自定义 Provider 无预设，保持纯文本输入
			RenderTextInput(pActiveModelInput, ControlCol, pActiveModelInput->GetEmptyText(), BodySize);
		}
		else if(pActiveModelInput)
		{
			// 预设下拉 + 自定义输入组合；末位「自定义」仅切换为手输，不覆盖当前值
			static CUi::SDropDownState s_LlmModelDropDownState;
			static CScrollRegion s_LlmModelDropDownScrollRegion;
			s_LlmModelDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_LlmModelDropDownScrollRegion;

			std::vector<const char *> vModelNames(vModelPresets);
			vModelNames.push_back(Localize("Custom…"));
			const int CustomIndex = static_cast<int>(vModelNames.size()) - 1;
			auto FindPresetIndex = [&vModelPresets, CustomIndex](const char *pValue) -> int {
				for(size_t i = 0; i < vModelPresets.size(); ++i)
				{
					if(str_comp(pValue, vModelPresets[i]) == 0)
						return static_cast<int>(i);
				}
				return CustomIndex;
			};

			CUIRect ModelDropRect, ModelEditRect;
			ControlCol.VSplitMid(&ModelDropRect, &ModelEditRect);
			ModelDropRect.VMargin(1.0f, &ModelDropRect);
			ModelEditRect.VMargin(1.0f, &ModelEditRect);

			const int ModelOldSel = FindPresetIndex(pModelConfigValue);
			const int ModelNewSel = DoSettingsDropDown(&ModelDropRect, ModelOldSel, vModelNames.data(), vModelNames.size(), s_LlmModelDropDownState, {}, pModelConfigValue, nullptr, &Row);
			if(ModelNewSel >= 0 && ModelNewSel != ModelOldSel && ModelNewSel != CustomIndex)
				str_copy(pModelConfigValue, vModelPresets[ModelNewSel], ModelConfigSize);

			if(!pActiveModelInput->IsActive() && str_comp(pActiveModelInput->GetString(), pModelConfigValue) != 0)
				pActiveModelInput->Set(pModelConfigValue);
			pActiveModelInput->SetEmptyText(pModelEmptyText);
			const bool ModelWasActive = pActiveModelInput->IsActive();
			const bool ModelSubmitPressed = !PrewarmOnly && (Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
			const bool ModelClickedOutside = !PrewarmOnly && (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)) && !Ui()->MouseHovered(&ModelEditRect);
			ui_widget::STextFieldOptions ModelInputOptions;
			ModelInputOptions.m_pPlaceholder = pModelEmptyText;
			ModelInputOptions.m_FontSize = BodySize;
			ModelInputOptions.m_Corners = IGraphics::CORNER_ALL;
			ModelInputOptions.m_TextAlign = TEXTALIGN_MC;
			RenderTextInput(pActiveModelInput, ModelEditRect, ModelInputOptions);
			if(ModelWasActive && (ModelSubmitPressed || ModelClickedOutside))
				str_copy(pModelConfigValue, pActiveModelInput->GetString(), ModelConfigSize);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// 出站目标语言（常驻）
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	RenderLabel("qmclient-translate-send-target-language", &LabelCol, Localize("Translate outgoing messages to"), BodySize);

	// 下拉框 + 输入框组合
	{
		const auto &apOutTargetNames = NTranslateUi::LanguageNames();
		const auto &apOutTargetCodes = NTranslateUi::LanguageCodes();
		static CUi::SDropDownState s_OutTargetLangDropDown;

		static CLineInput s_TargetLang(g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget));
		RenderLanguageDropDownWithCustomInput(Row, ControlCol, apOutTargetNames.data(), apOutTargetCodes.data(), apOutTargetCodes.size(), s_OutTargetLangDropDown, g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget), s_TargetLang, "en");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// 【高级】选项折叠开关
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmTranslateShowAdvanced, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_QmTranslateShowAdvanced)
	{
		// 本地语言检测阈值
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-minimum-match-chars", &LabelCol, Localize("Minimum match chars"), BodySize);
		{
			static int s_LocalDetectMinCharsSelectorId;
			RenderSliderWithNumberInput(&s_LocalDetectMinCharsSelectorId, ControlCol, &g_Config.m_QmTranslateLocalDetectMinChars, 1, 12);
		}
		Content.HSplitTop(LineSpacing * 0.5f, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-target-language-ratio", &LabelCol, Localize("Target language ratio"), BodySize);
		{
			static int s_LocalDetectRatioSelectorId;
			RenderSliderWithNumberInput(&s_LocalDetectRatioSelectorId, ControlCol, &g_Config.m_QmTranslateLocalDetectRatio, 50, 100);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// LLM 端点（高级；Custom Provider 必填，始终显示）
	if(IsLlmBackend && (g_Config.m_QmTranslateShowAdvanced || g_Config.m_QmTranslateLlmProvider == 3))
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-endpoint-optional", &LabelCol, Localize("Endpoint (optional)"), BodySize);

		static CLineInput s_LlmEndpointZhipu(g_Config.m_QmTranslateLlmEndpointZhipu, sizeof(g_Config.m_QmTranslateLlmEndpointZhipu));
		static CLineInput s_LlmEndpointDeepseek(g_Config.m_QmTranslateLlmEndpointDeepseek, sizeof(g_Config.m_QmTranslateLlmEndpointDeepseek));
		static CLineInput s_LlmEndpointOpenai(g_Config.m_QmTranslateLlmEndpointOpenai, sizeof(g_Config.m_QmTranslateLlmEndpointOpenai));
		static CLineInput s_LlmEndpointCustom(g_Config.m_QmTranslateLlmEndpointCustom, sizeof(g_Config.m_QmTranslateLlmEndpointCustom));

		CLineInput *pActiveEndpointInput = nullptr;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			s_LlmEndpointZhipu.SetEmptyText("https://open.bigmodel.cn/api/paas/v4/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointZhipu;
			break;
		case 1: // DeepSeek
			s_LlmEndpointDeepseek.SetEmptyText("https://api.deepseek.com/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointDeepseek;
			break;
		case 2: // OpenAI
			s_LlmEndpointOpenai.SetEmptyText("https://api.openai.com/v1/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointOpenai;
			break;
		case 3: // Custom
		default:
			s_LlmEndpointCustom.SetEmptyText("https://api.example.com/v1/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointCustom;
			break;
		}
		if(pActiveEndpointInput)
			RenderTextInput(pActiveEndpointInput, ControlCol, pActiveEndpointInput->GetEmptyText(), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// LLM 并发、思考模式与提示词（高级）
	if(IsLlmBackend && g_Config.m_QmTranslateShowAdvanced)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-concurrency", &LabelCol, Localize("Concurrency (0 = auto)"), BodySize);
		static int s_LlmConcurrencySelectorId;
		RenderSliderWithNumberInput(&s_LlmConcurrencySelectorId, ControlCol, &g_Config.m_QmTranslateLlmConcurrency, 0, 20);
		if(g_Config.m_QmTranslateLlmProvider == 0)
			GameClient()->m_Tooltips.DoToolTipForRect(&s_LlmConcurrencySelectorId, &Row, Localize("Zhipu free models allow only 1 concurrent request"));
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 显示当前有效并发数
		{
			const int EffectiveConcurrency = GetTranslateConcurrency();

			// 显示有效并发数
			char aBuf[64];
			if(g_Config.m_QmTranslateLlmConcurrency == 0)
			{
				str_format(aBuf, sizeof(aBuf), Localize("Auto concurrency: %d (smart default)"), EffectiveConcurrency);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), Localize("Manual concurrency: %d"), EffectiveConcurrency);
			}
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, nullptr, &ControlCol);
			Ui()->DoLabel(&ControlCol, aBuf, SmallSize, TEXTALIGN_ML);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 思考模式开关
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderCheckbox(&g_Config.m_QmTranslateLlmEnableThinking, "Enable thinking mode (slower)", Localize("Enable thinking mode (slower)"), &g_Config.m_QmTranslateLlmEnableThinking, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 思考模式提示
		if(g_Config.m_QmTranslateLlmEnableThinking)
		{
			const char *pHint = nullptr;
			if(g_Config.m_QmTranslateLlmProvider == 2) // OpenAI
			{
				pHint = Localize("Thinking mode requires a reasoning model");
			}
			else if(g_Config.m_QmTranslateLlmProvider == 3) // Custom
			{
				pHint = Localize("Make sure the backend supports OpenAI-compatible thinking parameters");
			}

			if(pHint)
			{
				GameClient()->m_Tooltips.DoToolTipForRect(&g_Config.m_QmTranslateLlmEnableThinking, &Row, pHint);
			}
		}

		// 自定义提示词配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-custom-prompt-template", &LabelCol, Localize("Custom prompt template"), BodySize);
		static CLineInput s_CustomPrompt(g_Config.m_QmTranslateSystemPrompt, sizeof(g_Config.m_QmTranslateSystemPrompt));
		s_CustomPrompt.SetEmptyText(Localize("Leave empty to use default prompt"));
		RenderTextInput(&s_CustomPrompt, ControlCol, Localize("Leave empty to use default prompt"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	if(g_Config.m_QmTranslateShowAdvanced)
	{
		// 原文语言由接收、发送翻译共用
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-send-source-language", &LabelCol, Localize("Original message language"), BodySize);
		if(!PrewarmOnly && !Ui()->RenderOnly())
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmTranslateSource, &Row, Localize("Original language for both received and outgoing messages. Use Auto to detect each message."));

		// 下拉框 + 输入框组合
		{
			const auto apSourceNames = NTranslateUi::SourceLanguageNames();
			const auto apSourceCodes = NTranslateUi::SourceLanguageCodes();
			static CUi::SDropDownState s_SourceLangDropDown;

			static CLineInput s_SourceLang(g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource));
			RenderLanguageDropDownWithCustomInput(Row, ControlCol, apSourceNames.data(), apSourceCodes.data(), apSourceCodes.size(), s_SourceLangDropDown, g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource), s_SourceLang, "auto");
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 接收翻译方式
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-receive-method", &LabelCol, Localize("Receive translation method"), BodySize);
		{
			const std::array<const char *, 2> apIncomingModeNames = {
				Localize("Translate only when not in target language"),
				Localize("Always translate"),
			};
			static CUi::SDropDownState s_IncomingModeDropDown;
			const int OldIncomingMode = std::clamp(g_Config.m_QmTranslateAutoMode, 0, 1);
			const int NewIncomingMode = DoSettingsDropDown(&ControlCol, OldIncomingMode, apIncomingModeNames.data(), apIncomingModeNames.size(), s_IncomingModeDropDown, {}, &g_Config.m_QmTranslateAutoMode, nullptr, &Row);
			if(NewIncomingMode != OldIncomingMode)
				g_Config.m_QmTranslateAutoMode = NewIncomingMode;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 发送翻译方式
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-send-method", &LabelCol, Localize("Send translation method"), BodySize);
		{
			const std::array<const char *, 2> apOutgoingModeNames = {
				Localize("Translate only when needed"),
				Localize("Always translate"),
			};
			static CUi::SDropDownState s_OutgoingModeDropDown;
			const int OldMode = std::clamp(g_Config.m_QmTranslateAutoOutgoingMode, 0, 1);
			const int NewMode = DoSettingsDropDown(&ControlCol, OldMode, apOutgoingModeNames.data(), apOutgoingModeNames.size(), s_OutgoingModeDropDown, {}, &g_Config.m_QmTranslateAutoOutgoingMode, nullptr, &Row);
			if(NewMode != OldMode)
				g_Config.m_QmTranslateAutoOutgoingMode = NewMode;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}
