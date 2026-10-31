// 头衔卡片独立于页面，贡献者页和全局搜索共用同一份控件与预览。
#include "QmCardCatalogInternal.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/components/qmclient/qm_title_badge.h>
#include <game/client/components/qmclient/sponsor_chat_render.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

static STextContainerIndex s_TitleFinishedPreviewContainer;
static STextContainerIndex s_SponsorChatPreviewContainer;

void CMenus::ClearQmTitlePreviewContainers()
{
	TextRender()->DeleteTextContainer(s_TitleFinishedPreviewContainer);
	TextRender()->DeleteTextContainer(s_SponsorChatPreviewContainer);
}

static SQmTitleAppearance QmTitleDraftAppearance()
{
	return {g_Config.m_QmTitleBadgeStyle == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON,
		color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTitleBadgeColor)).Pack(false), g_Config.m_QmTitleOpacity};
}

static void RenderQmTitleFinishedPreview(IGraphics *pGraphics, ITextRender *pTextRender, const CQmTitleBadge &TitleBadge, const CUIRect &Area, const char *pTitle, float FontSize)
{
	const std::string Text = QmTitleBadgeText(pTitle[0] ? pTitle : Localize("Sponsor title"));
	const vec2 DesiredSize = QmTitleBadgeSize(pTextRender->TextWidth(FontSize, Text.c_str()), FontSize);
	if(DesiredSize.x <= 0.0f || Area.w <= 0.0f || Area.h <= 0.0f)
		return;
	FontSize *= std::min({1.0f, Area.w / DesiredSize.x, Area.h / DesiredSize.y});
	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	Cursor.m_CalculateVisualBoundingBox = true;
	Cursor.SetPosition(vec2(0.0f, 0.0f));
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	pTextRender->TextColor(ui_token::color::NEUTRAL_WHITE);
	if(s_TitleFinishedPreviewContainer.Valid())
		pTextRender->RecreateTextContainerSoft(s_TitleFinishedPreviewContainer, &Cursor, Text.c_str());
	else
		pTextRender->CreateOrAppendTextContainer(s_TitleFinishedPreviewContainer, &Cursor, Text.c_str());
	pTextRender->TextColor(PreviousColor);
	const auto Bounds = pTextRender->GetBoundingBoxTextContainer(s_TitleFinishedPreviewContainer);
	const vec2 Size = QmTitleBadgeSize(Bounds.m_W, FontSize);
	const CUIRect Rect{Area.x + (Area.w - Size.x) * 0.5f, Area.y + (Area.h - Size.y) * 0.5f, Size.x, Size.y};
	const SQmTitleAppearance Appearance = QmTitleDraftAppearance();
	TitleBadge.RenderSurface(pGraphics, Rect, Appearance, 1.0f);
	const vec2 TextPosition = QmTitleBadgeTextPosition(Rect, QmTitleBadgeVisualBounds(Cursor));
	pTextRender->RenderTextContainer(s_TitleFinishedPreviewContainer, QmTitleBadgePalette(Appearance).m_Text,
		ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), TextPosition.x, TextPosition.y);
}

static void RenderQmSponsorChatPreview(IGraphics *pGraphics, ITextRender *pTextRender, const CUIRect &Rect, float FontSize, EQmSponsorChatStyle Style, double TimeSeconds)
{
	if(Rect.w <= 1.0f || Rect.h <= 1.0f)
		return;
	static EQmSponsorChatStyle s_PreviousStyle = EQmSponsorChatStyle::NONE;
	static double s_StartTime = 0.0;
	static double s_LastTime = -1.0;
	if(Style != s_PreviousStyle || !s_SponsorChatPreviewContainer.Valid() || s_LastTime < 0.0 || TimeSeconds < s_LastTime || TimeSeconds - s_LastTime > 0.5)
		s_StartTime = TimeSeconds;
	s_PreviousStyle = Style;
	s_LastTime = TimeSeconds;
	const char *pText = Localize("See you at the next checkpoint.");
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	pTextRender->TextColor(Style == EQmSponsorChatStyle::FLUID ? ColorRGBA(1, 1, 1, Color.a) : Color);
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(Rect.x, Rect.y + (Rect.h - FontSize) / 2.0f));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = Rect.w;
	Cursor.m_MaxLines = 1;
	Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
	Cursor.m_TrackLineRanges = Style == EQmSponsorChatStyle::PLATINUM;
	if(Style == EQmSponsorChatStyle::PLATINUM)
		QmSponsorChatAddPlatinumSplits(Cursor, pText, Color.a);
	const auto Gradient = QmChatGradientStyle(g_Config, EQmChatGradientRole::NORMAL, Color);
	const CQmChatGradientPaint Paint(pTextRender, Cursor, pText, Style == EQmSponsorChatStyle::NONE ? &Gradient : nullptr);
	if(s_SponsorChatPreviewContainer.Valid())
		pTextRender->RecreateTextContainerSoft(s_SponsorChatPreviewContainer, &Cursor, pText);
	else
		pTextRender->CreateOrAppendTextContainer(s_SponsorChatPreviewContainer, &Cursor, pText);
	pTextRender->TextColor(PreviousColor);
	const SQmSponsorChatSweep SweepState = QmSponsorChatSweepState(TimeSeconds - s_StartTime, pTextRender->GetTextContainerRenderedLineCount(s_SponsorChatPreviewContainer), true);
	QmRenderSponsorChatText(pGraphics, pTextRender, s_SponsorChatPreviewContainer, Style, 1.0f, 0.0f, 0.0f, FontSize, SweepState, TimeSeconds - s_StartTime);
}

namespace
{
	struct SQmTitleProfileDraft
	{
		CLineInputBuffered<64> m_Code;
		CLineInputBuffered<64> m_Title;
		CLineInputBuffered<64> m_Name;
		int m_BindName = 0;
		int m_Revision = -1;
	};

	SQmTitleProfileDraft &TitleProfileDraft(CQmClient &Auth, bool ReadOnly)
	{
		static SQmTitleProfileDraft s_Draft;
		if(!ReadOnly && s_Draft.m_Revision != Auth.TitleRevision())
		{
			s_Draft.m_Title.Set(Auth.TitleText());
			s_Draft.m_Name.Set(Auth.TitleBoundName());
			s_Draft.m_BindName = Auth.TitleBoundName()[0] != '\0';
			s_Draft.m_Revision = Auth.TitleRevision();
			if(Auth.TitleAuthenticated())
			{
				s_Draft.m_Code.Set("");
				g_Config.m_QmSponsorChatStyle = static_cast<int>(Auth.TitleProfileChatStyle());
				const auto &Appearance = Auth.TitleProfileAppearance();
				g_Config.m_QmTitleBadgeStyle = Appearance.m_Style == EQmTitleBadgeStyle::SUN ? 1 : 0;
				g_Config.m_QmTitleBadgeColor = color_cast<ColorHSLA>(ColorRGBA(Appearance.m_Color)).Pack(false);
				g_Config.m_QmTitleOpacity = Appearance.m_Opacity;
			}
		}
		return s_Draft;
	}
}

bool qm_card_catalog::QmCardRenderHook::BuildTitleCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
{
	if(Ctx.m_pMenus == nullptr || pStableId == nullptr)
		return false;
	const bool DisplayCard = str_comp(pStableId, "deck:qmclient-contributors-title-display") == 0;
	if(!DisplayCard && str_comp(pStableId, "deck:qmclient-contributors-title") != 0)
		return false;
	const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
	if(pDefault == nullptr)
		return false;

	CMenus *pMenus = Ctx.m_pMenus;
	const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
	const int Page = DisplayCard ? CMenus::SETTINGS_APPEARANCE : CMenus::SETTINGS_CONTRIBUTORS;
	const int Tab = DisplayCard ? CMenus::APPEARANCE_TAB_NAME_PLATE : -1;
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float TipSize = Metrics.m_SmallSize;
	const float PreviewHeight = LineHeight * 3.0f + LineSpacing * 2.0f;

	Out = {};
	Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};

	FSettingsCardRenderMeasured Render = [=](CUIRect &Content) {
		const bool ReadOnly = Ctx.m_ReadOnly || pMenus->Ui()->RenderOnly() || pMenus->m_MenuTextPlanCollecting;
		const bool PrewarmOnly = pMenus->Ui()->MeasurementOnly() || pMenus->m_MenuTextPlanCollecting || (Ctx.m_ReadOnly && !pMenus->Ui()->BackgroundRender());
		const CUiScopedMeasurement Measurement(pMenus->Ui(), PrewarmOnly);
		auto &Auth = pMenus->GameClient()->m_QmClient;
		auto &Draft = TitleProfileDraft(Auth, ReadOnly);
		static CButtonContainer s_RedeemButton, s_SaveButton, s_RefreshButton;
		Draft.m_Code.SetHidden(true);
		Draft.m_Code.SetEmptyText(Localize("Sponsor code"));
		Draft.m_Title.SetEmptyText(Localize("Custom title"));
		Draft.m_Name.SetEmptyText(Localize("Exact bound nickname"));
		IUiContext TitleCtx = pMenus->SettingsUiContext("settings_sponsor_title", UiScale);
		if(pMenus->Ui()->DrawingSuppressed())
		{
			TitleCtx.m_pAnim = nullptr;
			TitleCtx.m_pTree = nullptr;
		}
		const bool Enabled = !ReadOnly && !Auth.TitleBusy() && !pMenus->Ui()->IsPopupOpen() && !pMenus->GameClient()->m_GameConsole.IsActive();
		CSettingsContentLayoutFlow Flow(Content);
		const auto RowAction = [&](const char *pId, const auto &Action) {
			CUIRect Row;
			if(Flow.Control(pId, LineHeight, Row))
				Action(Row);
			Flow.Space(LineSpacing);
		};
		const auto Hint = [pMenus, ReadOnly, TipSize](const void *pId, const CUIRect &Rect, const char *pText) {
			if(ReadOnly || pMenus->Ui()->RenderOnly() || pMenus->Ui()->IsPopupOpen())
				return;
			const float Width = std::min(360.0f, Rect.w);
			if(g_Config.m_QmTitleInstantHints)
				pMenus->GameClient()->m_Tooltips.DoSmallToolTip(pId, &Rect, pText, TipSize, Width);
			else
				pMenus->GameClient()->m_Tooltips.DoToolTipForRect(pId, &Rect, pText, Width);
		};
		Flow.Text("title-auth-status", Localize(Auth.TitleStatus()), 0.0f, TipSize, LineSpacing, [pMenus](const char *pText, float Width, float FontSize) { return pMenus->TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [&](const CUIRect &Row, const char *pText) {
			pMenus->Ui()->DoLabel(&Row, pText, TipSize, TEXTALIGN_ML, {.m_MaxWidth = Row.w, .m_EnableWidthCheck = false});
			Hint(&Draft.m_Revision, Row, Localize("Permanent access; up to 4 online IP addresses")); }, LineHeight);
		if(!DisplayCard)
		{
			CUIRect Button;

			RowAction("title-redeem", [&](CUIRect Row) {
				static ui_widget::SSettingsActionGroupState s_RedeemWidths;
				const char *apLabels[] = {Localize("Redeem")};
				const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_RedeemWidths, Row, apLabels, 1, Metrics, ESettingsActionGroupWidth::CONTENT);
				const auto InputActions = ResolveSettingsInputActionsLayout(Row, 1, Metrics, Actions.m_aButtons[0].w);
				Row = InputActions.m_Input;
				Button = InputActions.m_Actions.m_aButtons[0];
				ui_widget::InputField(TitleCtx, &Draft.m_Code, Row, Draft.m_Code.GetEmptyText(), BodySize, nullptr, true);
				Hint(&Draft.m_Code, Row, Localize("Redeem your sponsor code to unlock a custom title, shown in chat and on your nameplate."));
				if(pMenus->DoButton_Menu(&s_RedeemButton, Localize("Redeem"), 0, &Button) && Enabled && !Auth.TitleAuthenticated())
					Auth.RedeemTitleCode(Draft.m_Code.GetString());
			});
			RowAction("title-label", [&](CUIRect Row) {
				pMenus->Ui()->DoLabel(&Row, Localize("Custom title"), TipSize, TEXTALIGN_ML);
			});
			RowAction("title-input", [&](CUIRect Row) {
				ui_widget::InputField(TitleCtx, &Draft.m_Title, Row, Draft.m_Title.GetEmptyText(), BodySize, nullptr, true);
				Hint(&Draft.m_Title, Row, Localize("Up to 6 Chinese or 12 ASCII characters"));
			});
			RowAction("title-bind-name", [&](CUIRect Row) {
				if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &Draft.m_BindName, "qm-title-bind-name", Localize("Only show with this nickname"), Draft.m_BindName, &Row) && Enabled && Auth.TitleAuthenticated())
					Draft.m_BindName ^= 1;
				Hint(&Draft.m_BindName, Row, Localize("Limit the title to the exact nickname below."));
			});
			RowAction("title-bound-name", [&](CUIRect Row) {
				ui_widget::InputField(TitleCtx, &Draft.m_Name, Row, Draft.m_Name.GetEmptyText(), BodySize, nullptr, true);
			});
			RowAction("title-chat-style", [&](CUIRect Row) {
				CUIRect ChatStyleLabel, ChatStyleControl;
				SplitSettingsControlRow(Row, Metrics, &ChatStyleLabel, &ChatStyleControl, ESettingsControlWidth::LONG_CHOICE);
				pMenus->DoSettingsMenuLabel(Page, Tab, -1, "qm-sponsor-chat-style", &ChatStyleLabel, Localize("Chat message style"), BodySize, TEXTALIGN_ML);
				static CUi::SDropDownState s_ChatStyleState;
				static CScrollRegion s_ChatStyleScroll;
				static std::array<const char *, 3> s_apChatStyleNames;
				s_apChatStyleNames = {Localize("Normal"), Localize("Platinum highlight"), Localize("Fluid")};
				s_ChatStyleState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatStyleScroll;
				CUi::SDropDownProperties ChatStyleProperties;
				ChatStyleProperties.m_Enabled = Auth.TitleAuthenticated() && !Auth.TitleBusy() && !pMenus->GameClient()->m_GameConsole.IsActive();
				ChatStyleProperties.m_FontSize = BodySize;
				const int ChatStyle = pMenus->DoSettingsDropDown(&ChatStyleControl, g_Config.m_QmSponsorChatStyle, s_apChatStyleNames.data(), s_apChatStyleNames.size(), s_ChatStyleState, ChatStyleProperties, &g_Config.m_QmSponsorChatStyle, nullptr, &Row);
				if(!ReadOnly && ChatStyleProperties.m_Enabled)
					g_Config.m_QmSponsorChatStyle = ChatStyle;
				Hint(&g_Config.m_QmSponsorChatStyle, Row, Localize("Saved with your title and shared with other QmClient players."));
			});

			CUIRect ChatPreview, ChatPreviewLabel, ChatPreviewArea;
			const bool HasPreview = Flow.Control("title-chat-preview", PreviewHeight, ChatPreview);
			Flow.Space(LineSpacing);
			if(HasPreview && Flow.Drawing() && !pMenus->Ui()->DrawingSuppressed())
			{
				ChatPreview.HSplitTop(TipSize, &ChatPreviewLabel, &ChatPreviewArea);
				pMenus->Ui()->DoLabel(&ChatPreviewLabel, Localize("Chat message preview"), TipSize, TEXTALIGN_ML);
				pMenus->Ui()->ClipEnable(&ChatPreviewArea);
				RenderQmSponsorChatPreview(pMenus->Graphics(), pMenus->TextRender(), ChatPreviewArea, BodySize, static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle), Auth.TitleAnimationTime());
				pMenus->Ui()->ClipDisable();
			}

			RowAction("title-profile-actions", [&](CUIRect Row) {
				static ui_widget::SSettingsActionGroupState s_ProfileActionWidths;
				const char *apActions[] = {Localize("Save"), Localize("Refresh")};
				const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_ProfileActionWidths, Row, apActions, 2, Metrics);
				Row = Actions.m_aButtons[0];
				Button = Actions.m_aButtons[1];
				if(pMenus->DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &Row) && Enabled && Auth.TitleAuthenticated() && (!Draft.m_BindName || Draft.m_Name.GetString()[0]))
					Auth.SaveTitleProfile(Draft.m_Title.GetString(), Draft.m_BindName ? Draft.m_Name.GetString() : "", Auth.TitleProfileStyle(), static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle), QmTitleDraftAppearance());
				if(pMenus->DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &Button) && Enabled)
					Auth.RefreshTitleProfile();
			});
			RowAction("title-display-navigation", [&](CUIRect Row) {
				static CButtonContainer s_DisplaySettings;
				if(ui_widget::SecondaryButton(TitleCtx, &s_DisplaySettings, Localize("Title display"), Row) && Enabled)
				{
					const auto *pDisplayCard = qm_card_registry::FindByStableId("deck:qmclient-contributors-title-display");
					if(pDisplayCard)
						pMenus->NavigateToSettingsCard(qm_card_registry::ResolveCardNavigationTarget(*pDisplayCard, pMenus->SettingsCardOrderModel()));
				}
			});
		}
		else
		{
			SLabelProperties SingleLineProps;
			SingleLineProps.m_DisallowNewline = true;
			SingleLineProps.m_StopAtEnd = true;
			SingleLineProps.m_MinimumFontSize = 6.0f;
			const auto VisibilityToggle = [&](int *pValue, const char *pTextId, const char *pLabel) {
				CUIRect ToggleRow;
				const bool HasRow = Flow.Control(pTextId, LineHeight, ToggleRow);
				Flow.Space(LineSpacing);
				if(HasRow && pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, pValue, pTextId, pLabel, *pValue, &ToggleRow, SingleLineProps) && !ReadOnly)
					*pValue ^= 1;
			};
			VisibilityToggle(&g_Config.m_QmShowNameplateTitle, "qm-title-show-nameplate", Localize("Show titles on nameplates"));

			VisibilityToggle(&g_Config.m_QmShowMainTitle, "qm-title-show-main", Localize("Show main tee title locally"));
			VisibilityToggle(&g_Config.m_QmShowDummyTitle, "qm-title-show-dummy", Localize("Show dummy title locally"));

			RowAction("title-local-chat-effects", [&](CUIRect Row) {
				if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &g_Config.m_QmSponsorChatEffects, "qm-sponsor-chat-effects", Localize("Show sponsor chat effects locally"), g_Config.m_QmSponsorChatEffects, &Row) && !ReadOnly)
					g_Config.m_QmSponsorChatEffects ^= 1;
				Hint(&g_Config.m_QmSponsorChatEffects, Row, Localize("Turning this off only changes your display."));
			});
			if(Personalization(pMenus, Content, "deck:qmclient-contributors-title-display", Metrics, ReadOnly))
			{
				RowAction("title-nameplate-scale", [&](CUIRect Row) {
					pMenus->DoSettingsScrollbarOption(Page, Tab, "qm-nameplate-title-scale", &g_Config.m_QmNameplateTitleScale, &g_Config.m_QmNameplateTitleScale, &Row, Localize("Nameplate title size"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				});
				VisibilityToggle(&g_Config.m_QmNameplateTitleAboveName, "qm-title-above-name", Localize("Show titles above player names"));
				RowAction("title-badge-style", [&](CUIRect Row) {
					CUIRect Label, Control;
					SplitSettingsControlRow(Row, Metrics, &Label, &Control, ESettingsControlWidth::LONG_CHOICE);
					pMenus->Ui()->DoLabel(&Label, Localize("Title style"), BodySize, TEXTALIGN_ML);
					static CUi::SDropDownState s_StyleState;
					static CScrollRegion s_StyleScroll;
					const std::array<const char *, 2> aNames{Localize("Moon"), Localize("Sun")};
					s_StyleState.m_SelectionPopupContext.m_pScrollRegion = &s_StyleScroll;
					const int Style = pMenus->DoSettingsDropDown(&Control, g_Config.m_QmTitleBadgeStyle, aNames.data(), aNames.size(), s_StyleState, {}, &g_Config.m_QmTitleBadgeStyle, nullptr, &Row);
					if(!ReadOnly && Style != g_Config.m_QmTitleBadgeStyle)
					{
						// 默认配色跟随日月；用户自定义过的颜色在切换形状时保留。
						const auto OldStyle = g_Config.m_QmTitleBadgeStyle == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON;
						const unsigned OldDefault = color_cast<ColorHSLA>(ColorRGBA(QmTitleBadgeDefaultColor(OldStyle))).Pack(false);
						g_Config.m_QmTitleBadgeStyle = Style;
						if(g_Config.m_QmTitleBadgeColor == OldDefault)
						{
							const auto NewStyle = Style == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON;
							g_Config.m_QmTitleBadgeColor = color_cast<ColorHSLA>(ColorRGBA(QmTitleBadgeDefaultColor(NewStyle))).Pack(false);
						}
					}
				});
				RowAction("title-badge-color", [&](CUIRect Row) {
					static CButtonContainer s_ColorReset;
					SSettingsContentMetrics ColorMetrics = Metrics;
					ColorMetrics.m_LineSpacing = 0.0f;
					pMenus->DoLine_ColorPicker(&s_ColorReset, ColorMetrics, &Row, Localize("Title color"), &g_Config.m_QmTitleBadgeColor,
						ColorRGBA(QmTitleBadgeDefaultColor(QmTitleDraftAppearance().m_Style)), false, nullptr, false, false);
				});
				RowAction("title-badge-opacity", [&](CUIRect Row) {
					pMenus->DoSettingsScrollbarOption(Page, Tab, "qm-title-opacity", &g_Config.m_QmTitleOpacity, &g_Config.m_QmTitleOpacity, &Row, Localize("Title opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				});
			}
			RowAction("title-badge-save", [&](CUIRect Row) {
				static CButtonContainer s_SaveAppearance;
				if(pMenus->DoButton_Menu(&s_SaveAppearance, Localize("Save"), 0, &Row) && Enabled && Auth.TitleAuthenticated() && (!Draft.m_BindName || Draft.m_Name.GetString()[0]))
					Auth.SaveTitleProfile(Draft.m_Title.GetString(), Draft.m_BindName ? Draft.m_Name.GetString() : "", Auth.TitleProfileStyle(), static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle), QmTitleDraftAppearance());
			});

			CUIRect Preview, PreviewLabel, TitlePreviewArea;
			if(Flow.Control("title-finished-preview", PreviewHeight, Preview) && Flow.Drawing() && !pMenus->Ui()->DrawingSuppressed())
			{
				Preview.HSplitTop(TipSize, &PreviewLabel, &TitlePreviewArea);
				pMenus->Ui()->DoLabel(&PreviewLabel, Localize("Title preview"), TipSize, TEXTALIGN_ML);
				pMenus->Ui()->ClipEnable(&TitlePreviewArea);
				if(Auth.PlayerHasDeveloperBadge(pMenus->GameClient()->m_Snap.m_LocalClientId) && Auth.DeveloperBadge().Available())
				{
					const vec2 Size = QmDeveloperBadgeSize(BodySize * 1.4f);
					Auth.DeveloperBadge().Render(pMenus->Graphics(),
						{TitlePreviewArea.x + (TitlePreviewArea.w - Size.x) * 0.5f, TitlePreviewArea.y + (TitlePreviewArea.h - Size.y) * 0.5f, Size.x, Size.y}, 1.0f, Auth.TitleAnimationTime());
				}
				else
					RenderQmTitleFinishedPreview(pMenus->Graphics(), pMenus->TextRender(), pMenus->GameClient()->m_QmClient.TitleBadge(), TitlePreviewArea, Draft.m_Title.GetString(), BodySize * 1.4f);
				pMenus->Ui()->ClipDisable();
			}
		}
	};
	Out.m_Render = [Render](CUIRect Content) { Render(Content); };
	Out.m_RenderMeasured = std::move(Render);
	UseDeclaredModuleContent(Ctx, Out);
	return true;
}
