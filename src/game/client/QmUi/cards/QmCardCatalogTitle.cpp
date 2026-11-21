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
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/components/qmclient/qm_title_badge.h>
#include <game/client/components/qmclient/sponsor_chat_render.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

static SQmTitleBadgePlacement s_TitleFinishedPreview;
static STextContainerIndex s_SponsorChatPreviewContainer;
static SQmTitleBadgePlacement s_ChatTitlePreview;

static constexpr float QmSponsorChatPreviewFontSize(float PreviewHeight, float ChatFontSize)
{
	constexpr float MINIMUM_FONT_SIZE = 8.0f;
	// 最小聊天字号的两倍仍可能低于预览的字号下限。
	return std::clamp(PreviewHeight * 0.40f, MINIMUM_FONT_SIZE, std::max(MINIMUM_FONT_SIZE, ChatFontSize * 2.0f));
}

static_assert(QmSponsorChatPreviewFontSize(30.0f, 1.0f) == 8.0f);
static_assert(QmSponsorChatPreviewFontSize(30.0f, 5.0f) == 10.0f);

void CMenus::ClearQmTitlePreviewContainers()
{
	s_TitleFinishedPreview.DeleteText(TextRender());
	TextRender()->DeleteTextContainer(s_SponsorChatPreviewContainer);
	s_ChatTitlePreview.DeleteText(TextRender());
}

static SQmTitleAppearance QmTitleDraftAppearance()
{
	return {g_Config.m_QmTitleBadgeStyle == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON,
		color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTitleBadgeColor)).Pack(false), g_Config.m_QmTitleOpacity, g_Config.m_QmTitleTextStyle};
}

static float RenderQmTitlePreview(IGraphics *pGraphics, ITextRender *pTextRender, const CQmTitleBadge &Badge, SQmTitleBadgePlacement &Placement,
	const CUIRect &Area, const char *pTitle, float FontSize, float Time, bool Center)
{
	Placement.m_Appearance = QmTitleDraftAppearance();
	const bool TextOnly = Placement.m_Appearance.m_TextStyle != 0;
	const bool Wave = TextOnly && g_Config.m_QmTitleTextWave != 0;
	const std::string Text = QmTitleDisplayText(pTitle, Placement.m_Appearance);
	const float Width = pTextRender->TextWidth(FontSize, Text.c_str());
	const vec2 Desired = TextOnly ? vec2(Width, FontSize) : QmTitleBadgeSize(Width, FontSize);
	if(Desired.x <= 0.0f || Area.w <= 0.0f || Area.h <= 0.0f)
		return 0.0f;
	const float Height = Desired.y + (Wave ? 2.0f * FontSize * QM_TITLE_TEXT_WAVE_AMPLITUDE : 0.0f);
	const vec2 Size = Desired * std::min({1.0f, Area.w / Desired.x, Area.h / Height});
	Placement.m_Rect = {Center ? Area.x + (Area.w - Size.x) * 0.5f : Area.x, Area.y + (Area.h - Size.y) * 0.5f, Size.x, Size.y};
	Placement.PrepareText(pTextRender, Text.c_str(), Wave, Time);
	if(!TextOnly)
		Badge.RenderSurface(pGraphics, Placement.m_Rect, Placement.m_Appearance, 1.0f);
	Placement.RenderText(pTextRender, 1.0f, vec2(0.0f, 0.0f));
	return Size.x;
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
	pTextRender->TextColor(Style == EQmSponsorChatStyle::FLUID ? ui_token::color::NEUTRAL_WHITE.WithAlpha(Color.a) : Color);
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

// 「发言预览」要的是聊天框里那一条消息本身：消息底色 + 发言人名字 + 那句话，
// 不是把整框示例聊天复制一遍。
void qm_card_catalog::QmCardRenderHook::RenderQmSponsorChatMessagePreview(CMenus *pMenus, const CUIRect &Rect, const char *pTitle, int StyleValue, double TimeSeconds)
{
	if(Rect.w <= 1.0f || Rect.h <= 1.0f)
		return;
	const auto Style = static_cast<EQmSponsorChatStyle>(StyleValue);
	ITextRender *pTextRender = pMenus->TextRender();
	CChat *pChat = &pMenus->GameClient()->m_Chat;
	const float FontSize = QmSponsorChatPreviewFontSize(Rect.h, pChat->FontSize());
	const float PaddingX = pChat->MessagePaddingX() * 2.0f;
	const float TeeSize = pChat->MessageTeeSize() * 2.0f;

	// 一条消息的底色，圆角和聊天框同源：预览只画单行，取单行气泡的胶囊半径。
	const ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true));
	DrawRoundedSurface(pMenus->Ui(), Rect, Background, ColorRGBA(), ui_token::chat::BubbleRadius(true, Rect.h, Rect.h));

	const float CenterY = Rect.y + Rect.h / 2.0f;
	float X = Rect.x + PaddingX * 0.5f;

	// 本体是谁就用谁的皮肤、名字和 client id，和聊天框里看到的一致。
	const int LocalId = pMenus->GameClient()->m_Snap.m_LocalClientId;
	const bool HaveLocal = LocalId >= 0 && LocalId < MAX_CLIENTS;
	const char *pName = HaveLocal && pMenus->GameClient()->m_aClients[LocalId].m_aName[0] != '\0' ? pMenus->GameClient()->m_aClients[LocalId].m_aName : pMenus->Client()->PlayerName();

	if(TeeSize > 1.0f && !g_Config.m_ClChatOld)
	{
		CTeeRenderInfo Info;
		if(HaveLocal)
		{
			// 直接用本体在游戏里的那一份渲染信息：皮肤、颜色都是他自己设置的。
			Info = pMenus->GameClient()->m_aClients[LocalId].m_RenderInfo;
		}
		else
		{
			const CSkin *pSkin = pMenus->GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin);
			if(pSkin == nullptr)
				pSkin = pMenus->GameClient()->m_Skins.Find("default");
			if(pSkin != nullptr)
				Info.Apply(pSkin);
		}
		Info.m_Size = TeeSize;
		pMenus->RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + TeeSize * 0.5f, CenterY));
		X += TeeSize + CChat::MESSAGE_TEE_PADDING_RIGHT * 2.0f;
	}

	// 名字前先放 client id（本体 ID），格式和聊天框一样；再画名字和冒号。
	CTextCursor NameCursor;
	NameCursor.SetPosition(vec2(X, CenterY - FontSize / 2.0f));
	NameCursor.m_FontSize = FontSize;
	pTextRender->TextColor(ui_token::color::TEXT_SECONDARY);
	if(g_Config.m_ClShowIds && HaveLocal)
	{
		char aClientId[16] = "";
		pMenus->GameClient()->FormatClientId(LocalId, aClientId, EClientIdFormat::INDENT_FORCE);
		pTextRender->TextEx(&NameCursor, aClientId, -1);
	}
	auto &Auth = pMenus->GameClient()->m_QmClient;
	const bool ShowLocalTitle = HaveLocal ? Auth.ShouldShowPlayerTitle(LocalId) && !pMenus->GameClient()->ShouldHideStreamerIdentity(LocalId) : g_Config.m_QmShowMainTitle != 0;
	if(g_Config.m_QmChatSponsorBadges && ShowLocalTitle)
	{
		if(QmUseDeveloperBadgeFallback(pTitle, HaveLocal && Auth.PlayerHasDeveloperBadge(LocalId), Auth.DeveloperBadge().Available()))
		{
			const CUIRect BadgeRect = QmLayoutDeveloperBadge(NameCursor);
			Auth.DeveloperBadge().Render(pMenus->Graphics(), BadgeRect, 1.0f, TimeSeconds);
		}
		else if(pTitle[0])
		{
			const CUIRect Area{NameCursor.m_X, Rect.y, Rect.x + Rect.w - NameCursor.m_X, Rect.h};
			const float LabelSize = g_Config.m_QmTitleTextStyle != 0 ? FontSize : QmTitleBadgeLabelFontSize(FontSize);
			const float Width = RenderQmTitlePreview(pMenus->Graphics(), pTextRender, Auth.TitleBadge(), s_ChatTitlePreview,
				Area, pTitle, LabelSize, static_cast<float>(TimeSeconds), false);
			NameCursor.m_X += Width + FontSize * 0.15f;
		}
	}
	pTextRender->TextEx(&NameCursor, pName, -1);
	pTextRender->TextEx(&NameCursor, ": ", -1);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	// 内容接在名字后面：游标推进到哪就从哪开始，不然会压在名字上。
	X = NameCursor.m_X;

	// 内容仍走赞助者发言样式渲染。
	CUIRect TextRect{X, Rect.y, std::max(1.0f, Rect.x + Rect.w - X), Rect.h};
	RenderQmSponsorChatPreview(pMenus->Graphics(), pTextRender, TextRect, FontSize, Style, TimeSeconds);
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
				g_Config.m_QmTitleTextStyle = Appearance.m_TextStyle;
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
	// 发言预览改成整套聊天框预览，要放得下系统行、高亮、队伍、好友、普通五条。
	const float ChatPreviewHeight = LineHeight * 2.0f;

	Out = {};
	Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};

	FSettingsCardRenderMeasured Render = [=](CUIRect &Content) {
		const bool ReadOnly = Ctx.m_ReadOnly || pMenus->Ui()->RenderOnly() || pMenus->m_MenuTextPlanCollecting;
		const bool PrewarmOnly = pMenus->Ui()->MeasurementOnly() || pMenus->m_MenuTextPlanCollecting || (Ctx.m_ReadOnly && !pMenus->Ui()->BackgroundRender());
		const CUiScopedMeasurement Measurement(pMenus->Ui(), PrewarmOnly);
		auto &Auth = pMenus->GameClient()->m_QmClient;
		auto &Draft = TitleProfileDraft(Auth, ReadOnly);
		static CButtonContainer s_RedeemButton, s_SaveButton, s_RefreshButton;
		Draft.m_Code.SetEmptyText(Localize("Sponsor code"));
		Draft.m_Title.SetEmptyText(Localize("Custom title"));
		Draft.m_Name.SetEmptyText(Localize("Exact bound nickname"));
		IUiContext TitleCtx = pMenus->SettingsUiContext("settings_sponsor_title", UiScale);
		if(pMenus->Ui()->DrawingSuppressed())
		{
			TitleCtx.m_pAnim = nullptr;
			TitleCtx.m_pTree = nullptr;
		}
		const bool InputBlocked = pMenus->Ui()->IsPopupOpen() || pMenus->GameClient()->m_GameConsole.IsActive();
		const bool Enabled = !ReadOnly && !Auth.TitleBusy() && !InputBlocked;
		CSettingsContentLayoutFlow Flow(Content);
		const auto PreviewButton = [&](CButtonContainer &Button, EQmIcon Icon, const char *pGlyph, const CUIRect &Rect, bool Available, const char *pHint) {
			const bool Clicked = pMenus->Ui()->DoButton_QmIcon(&Button, Icon, pGlyph, 0, &Rect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Available && !InputBlocked) != 0;
			if(!Flow.Input() || ReadOnly)
				return false;
			pMenus->GameClient()->m_Tooltips.DoToolTip(&Button, &Rect, pHint);
			return Clicked;
		};
		const auto SaveButton = [&](CButtonContainer &Button, const CUIRect &Rect) {
			const bool CanSave = Auth.TitleAuthenticated() && !Auth.TitleBusy() && (!Draft.m_BindName || Draft.m_Name.GetString()[0]);
			if(PreviewButton(Button, EQmIcon::CHECK, "\xEE\x86\x82", Rect, CanSave, Localize("Save")))
				Auth.SaveTitleProfile(Draft.m_Title.GetString(), Draft.m_BindName ? Draft.m_Name.GetString() : "", Auth.TitleProfileStyle(), static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle), QmTitleDraftAppearance());
		};
		const auto RowAction = [&](const char *pId, const auto &Action) {
			CUIRect Row;
			if(Flow.Control(pId, LineHeight, Row))
				Action(Row);
			Flow.Space(ui_token::settings::RowGap(LineSpacing));
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
		Flow.Text("title-auth-status", Localize(Auth.TitleStatus()), 0.0f, TipSize, ui_token::settings::RowGap(LineSpacing), [pMenus](const char *pText, float Width, float FontSize) { return pMenus->TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [&](const CUIRect &Row, const char *pText) {
				{
					const ColorRGBA PreviousTextColor = pMenus->TextRender()->GetTextColor();
					pMenus->TextRender()->TextColor(ui_token::color::TEXT_TIP);

					pMenus->Ui()->DoLabel(&Row, pText, TipSize, TEXTALIGN_ML, {.m_MaxWidth = Row.w, .m_EnableWidthCheck = false});
					pMenus->TextRender()->TextColor(PreviousTextColor);
				}
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
				static ui_widget::SPasswordFieldState s_TitleCodeState;
				ui_widget::SPasswordFieldOptions CodeOptions;
				CodeOptions.m_pPlaceholder = Draft.m_Code.GetEmptyText();
				CodeOptions.m_FontSize = BodySize;
				CodeOptions.m_Clearable = true;
				ui_widget::PasswordField(TitleCtx, &Draft.m_Code, Row, &s_TitleCodeState, CodeOptions);
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
			const bool HasPreview = Flow.Control("title-chat-preview", ChatPreviewHeight, ChatPreview);
			Flow.Space(ui_token::settings::RowGap(LineSpacing));
			if(HasPreview && Flow.Drawing() && !pMenus->Ui()->DrawingSuppressed())
			{
				// 标题行按按钮高度开，保存/刷新两个图标就摆在这一行的右端。
				ChatPreview.HSplitTop(Metrics.m_ButtonHeight, &ChatPreviewLabel, &ChatPreviewArea);
				CUIRect PreviewRefreshRect, PreviewSaveRect;
				ChatPreviewLabel.VSplitRight(Metrics.m_ButtonHeight, &ChatPreviewLabel, &PreviewRefreshRect);
				ChatPreviewLabel.VSplitRight(LineSpacing * 0.5f, &ChatPreviewLabel, nullptr);
				ChatPreviewLabel.VSplitRight(Metrics.m_ButtonHeight, &ChatPreviewLabel, &PreviewSaveRect);
				ChatPreviewLabel.VSplitRight(LineSpacing, &ChatPreviewLabel, nullptr);
				SaveButton(s_SaveButton, PreviewSaveRect);
				if(PreviewButton(s_RefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, "\xEE\x80\xB6", PreviewRefreshRect, Auth.CanRefreshTitleProfile(), Localize("Refresh")))
					Auth.RefreshTitleProfile();
			}
			if(HasPreview && Flow.Drawing() && !pMenus->Ui()->DrawingSuppressed())
			{
				{
					const ColorRGBA PreviousTextColor = pMenus->TextRender()->GetTextColor();
					pMenus->TextRender()->TextColor(ui_token::color::TEXT_TIP);

					pMenus->Ui()->DoLabel(&ChatPreviewLabel, Localize("Chat message preview"), TipSize, TEXTALIGN_ML);
					pMenus->TextRender()->TextColor(PreviousTextColor);
				}
				// 标题和预览块之间留一个行距，别贴在一起。
				ChatPreviewArea.HSplitTop(LineSpacing * 0.6f, nullptr, &ChatPreviewArea);
				pMenus->Ui()->ClipEnable(&ChatPreviewArea);
				qm_card_catalog::QmCardRenderHook::RenderQmSponsorChatMessagePreview(pMenus, ChatPreviewArea, Draft.m_Title.GetString(), g_Config.m_QmSponsorChatStyle, Auth.TitleAnimationTime());
				pMenus->Ui()->ClipDisable();
			}

			RowAction("title-display-navigation", [&](CUIRect Row) {
				static CButtonContainer s_DisplaySettings;
				if(ui_widget::SecondaryButton(TitleCtx, &s_DisplaySettings, Localize("Edit title style"), Row) && Enabled)
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
				Flow.Space(ui_token::settings::RowGap(LineSpacing));
				if(HasRow && pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, pValue, pTextId, pLabel, *pValue, &ToggleRow, SingleLineProps) && !ReadOnly)
					*pValue ^= 1;
			};
			VisibilityToggle(&g_Config.m_QmShowNameplateTitle, "qm-title-show-nameplate", Localize("Show titles on nameplates"));

			VisibilityToggle(&g_Config.m_QmShowMainTitle, "qm-title-show-main", Localize("Show main tee title locally"));
			VisibilityToggle(&g_Config.m_QmShowDummyTitle, "qm-title-show-dummy", Localize("Show dummy title locally"));
			VisibilityToggle(&g_Config.m_QmTitleTextWave, "qm-title-text-wave", Localize("Show per-character title waves locally"));

			RowAction("title-local-chat-effects", [&](CUIRect Row) {
				if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &g_Config.m_QmSponsorChatEffects, "qm-sponsor-chat-effects", Localize("Show sponsor chat effects locally"), g_Config.m_QmSponsorChatEffects, &Row) && !ReadOnly)
					g_Config.m_QmSponsorChatEffects ^= 1;
				Hint(&g_Config.m_QmSponsorChatEffects, Row, Localize("Turning this off only changes your display."));
			});
			float SectionReveal = 0.0f;
			if(PersonalizationReveal(pMenus, Content, "deck:qmclient-contributors-title-display", Metrics, &SectionReveal, ReadOnly))
			{
				const CSettingsContentRevealScope SectionRevealScope(pMenus->Ui(), Flow, SectionReveal);
				RowAction("title-nameplate-scale", [&](CUIRect Row) {
					pMenus->DoSettingsScrollbarOption(Page, Tab, "qm-nameplate-title-scale", &g_Config.m_QmNameplateTitleScale, &g_Config.m_QmNameplateTitleScale, &Row, Localize("Nameplate title size"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				});
				RowAction("title-badge-style", [&](CUIRect Row) {
					CUIRect Label, Control;
					SplitSettingsControlRow(Row, Metrics, &Label, &Control, ESettingsControlWidth::LONG_CHOICE);
					pMenus->Ui()->DoLabel(&Label, Localize("Title style"), BodySize, TEXTALIGN_ML);
					static CUi::SDropDownState s_StyleState;
					static CScrollRegion s_StyleScroll;
					static std::array<const char *, 7> s_apNames;
					s_apNames = {Localize("Text 1"), Localize("Text 2"), Localize("Text 3"), Localize("Text 4"), Localize("Text 5"), Localize("Moon"), Localize("Sun")};
					s_StyleState.m_SelectionPopupContext.m_pScrollRegion = &s_StyleScroll;
					const int NumTextStyles = static_cast<int>(s_apNames.size()) - 2;
					const int Selected = g_Config.m_QmTitleTextStyle ? g_Config.m_QmTitleTextStyle - 1 : NumTextStyles + g_Config.m_QmTitleBadgeStyle;
					const int Style = pMenus->DoSettingsDropDown(&Control, Selected, s_apNames.data(), s_apNames.size(), s_StyleState, {}, &g_Config.m_QmTitleTextStyle, &g_Config.m_QmTitleBadgeStyle, &Row);
					if(ReadOnly || Style == Selected || Style < 0)
						return;
					g_Config.m_QmTitleTextStyle = Style < NumTextStyles ? Style + 1 : 0;
					if(Style >= NumTextStyles)
					{
						// 默认配色跟随日月；用户自定义过的颜色在切换形状时保留。
						const auto OldStyle = g_Config.m_QmTitleBadgeStyle == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON;
						const unsigned OldDefault = color_cast<ColorHSLA>(ColorRGBA(QmTitleBadgeDefaultColor(OldStyle))).Pack(false);
						g_Config.m_QmTitleBadgeStyle = Style - NumTextStyles;
						if(g_Config.m_QmTitleBadgeColor == OldDefault)
						{
							const auto NewStyle = g_Config.m_QmTitleBadgeStyle == 1 ? EQmTitleBadgeStyle::SUN : EQmTitleBadgeStyle::MOON;
							g_Config.m_QmTitleBadgeColor = color_cast<ColorHSLA>(ColorRGBA(QmTitleBadgeDefaultColor(NewStyle))).Pack(false);
						}
					}
				});
				if(g_Config.m_QmTitleTextStyle == 0)
				{
					RowAction("title-badge-color", [&](CUIRect Row) {
						static CButtonContainer s_ColorReset;
						SSettingsContentMetrics ColorMetrics = Metrics;
						ColorMetrics.m_LineSpacing = 0.0f;
						pMenus->DoLine_ColorPicker(&s_ColorReset, ColorMetrics, &Row, Localize("Title color"), &g_Config.m_QmTitleBadgeColor,
							ColorRGBA(QmTitleBadgeDefaultColor(QmTitleDraftAppearance().m_Style)), false, nullptr, false, false);
					});
				}
				RowAction("title-badge-opacity", [&](CUIRect Row) {
					pMenus->DoSettingsScrollbarOption(Page, Tab, "qm-title-opacity", &g_Config.m_QmTitleOpacity, &g_Config.m_QmTitleOpacity, &Row, Localize("Title opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				});
			}

			CUIRect Preview, PreviewLabel, TitlePreviewArea;
			const bool HasPreview = Flow.Control("title-finished-preview", PreviewHeight, Preview);
			if(HasPreview)
			{
				// 保存放在预览区右上角：位置贴着它作用的这块区域，不用回头找整行大按钮。
				static CButtonContainer s_SaveAppearance;
				CUIRect SaveAppearanceRect;
				Preview.VSplitRight(Metrics.m_ButtonHeight, &Preview, &SaveAppearanceRect);
				SaveAppearanceRect.h = Metrics.m_ButtonHeight;
				Preview.VSplitRight(LineSpacing * 0.5f, &Preview, nullptr);
				SaveButton(s_SaveAppearance, SaveAppearanceRect);
			}
			if(HasPreview && Flow.Drawing() && !pMenus->Ui()->DrawingSuppressed())
			{
				Preview.HSplitTop(TipSize, &PreviewLabel, &TitlePreviewArea);
				{
					const ColorRGBA PreviousTextColor = pMenus->TextRender()->GetTextColor();
					pMenus->TextRender()->TextColor(ui_token::color::TEXT_TIP);

					pMenus->Ui()->DoLabel(&PreviewLabel, Localize("Title preview"), TipSize, TEXTALIGN_ML);
					pMenus->TextRender()->TextColor(PreviousTextColor);
				}
				TitlePreviewArea.HSplitTop(LineSpacing * 0.6f, nullptr, &TitlePreviewArea);
				TitlePreviewArea.Draw(ui_token::color::PREVIEW_SURFACE, IGraphics::CORNER_ALL, ui_token::radius::BASE);
				TitlePreviewArea.Margin(TipSize * 0.45f, &TitlePreviewArea);
				pMenus->Ui()->ClipEnable(&TitlePreviewArea);
				if(QmUseDeveloperBadgeFallback(Draft.m_Title.GetString(), Auth.PlayerHasDeveloperBadge(pMenus->GameClient()->m_Snap.m_LocalClientId), Auth.DeveloperBadge().Available()))
				{
					const vec2 Size = QmDeveloperBadgeSize(BodySize * 1.4f);
					Auth.DeveloperBadge().Render(pMenus->Graphics(),
						{TitlePreviewArea.x + (TitlePreviewArea.w - Size.x) * 0.5f, TitlePreviewArea.y + (TitlePreviewArea.h - Size.y) * 0.5f, Size.x, Size.y}, 1.0f, Auth.TitleAnimationTime());
				}
				else
					RenderQmTitlePreview(pMenus->Graphics(), pMenus->TextRender(), Auth.TitleBadge(), s_TitleFinishedPreview, TitlePreviewArea,
						Draft.m_Title.GetString()[0] ? Draft.m_Title.GetString() : Localize("Sponsor title"), BodySize * 1.4f, static_cast<float>(Auth.TitleAnimationTime()), true);
				pMenus->Ui()->ClipDisable();
			}
		}
	};
	Out.m_Render = [Render](CUIRect Content) { Render(Content); };
	Out.m_RenderMeasured = std::move(Render);
	UseDeclaredModuleContent(Ctx, Out);
	return true;
}
