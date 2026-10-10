// 头衔卡片独立于页面，贡献者页和全局搜索共用同一份控件与预览。
#include "QmCardCatalog.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/components/qmclient/qm_title_color.h>
#include <game/client/components/qmclient/qm_title_render.h>
#include <game/client/components/qmclient/qm_title_style.h>
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

struct SQmTitleStylePreviewContext
{
	// 预览文字用风格自身采样出的颜色，基底色必须够亮，否则暗色风格会变成一坨黑。
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA m_OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	float m_OutlineRadius = 1.0f;
	float m_FontSize = 10.0f;
	float m_TimeSec = 0.0f;
	char m_aPreviewText[64] = "";
	// 「跟随服务器」条目预览用的风格 id：有服务端风格时是它，否则是本地当前选择。
	char m_aFollowPreviewStyleId[32] = "";
	// 弹层回调是普通函数指针（不带 this），所以渲染接口由调用方在建上下文时一并带入。
	// 两者都是当帧有效：弹层在卡片绘制之后、同一帧内渲染。
	ITextRender *m_pTextRender = nullptr;
	CRenderTools *m_pRenderTools = nullptr;
};

// 样式预览的文本容器按风格序号缓存；弹层每帧重建可见条目，这里逐项复用自己的容器。
static std::array<STextContainerIndex, 64> s_aTitleStylePreviewContainers;
static STextContainerIndex s_TitleFinishedPreviewContainer;
static STextContainerIndex s_SponsorChatPreviewContainer;

void CMenus::ClearQmTitlePreviewContainers()
{
	// 清理全部条目，包括已折叠或滚出屏幕的预览；删除后下次绘制会重新创建。
	TextRender()->DeleteTextContainer(s_TitleFinishedPreviewContainer);
	TextRender()->DeleteTextContainer(s_SponsorChatPreviewContainer);
	for(auto &Container : s_aTitleStylePreviewContainers)
		TextRender()->DeleteTextContainer(Container);
}

// 卡片内展开的风格列表：可见行数、行高/间距/内边距，测量与绘制必须用同一组常量。
constexpr int MAX_VISIBLE_STYLE_ITEMS = 6;
constexpr float STYLE_ITEM_HEIGHT = 17.0f;
constexpr float STYLE_ITEM_SPACING = 2.0f;
constexpr float STYLE_ITEM_INSET = 3.0f;

// 展开面板高度：与设置页行流的「行高 + 行距」对齐，末尾只留内边距、不留间距。
constexpr float ResolveQmTitleStylePanelHeight(const int VisibleItemCount)
{
	if(VisibleItemCount <= 0)
		return 0.0f;
	return (float)VisibleItemCount * (STYLE_ITEM_HEIGHT + STYLE_ITEM_SPACING) - STYLE_ITEM_SPACING + 2.0f * STYLE_ITEM_INSET;
}

// 「头衔风格」选择行的展开状态与命中矩形：展开时卡片高度跟着变，因此测量阶段也要读它。
static bool s_TitleStyleExpanded = false;
static int s_TitleStyleFieldId;
static CUIRect s_TitleStyleFieldRect{};
static CUIRect s_TitleStylePanelRect{};
static CScrollRegion s_TitleStyleListScrollRegion;
// 按风格序号分配稳定 id；滚动时首尾可能同时露出半行，不能复用可见槽位的 id。
static std::array<CButtonContainer, 64> s_aTitleStyleItemIds;

struct SQmTitleStylePreviewEntryContext
{
	CUIRect m_RowRect;
	float m_Padding;
	float m_FontSize;
};

// 风格条目的自定义前景：画「[赞助者] + 该风格」的实时效果。pEntry 只用于判空，文本固定取自上下文。
static void RenderQmTitleStylePreviewEntry(void *pContext, const SQmTitleStylePreviewEntryContext &EntryCtx, int Index, const char *pEntry)
{
	const SQmTitleStylePreviewContext *pPreview = static_cast<const SQmTitleStylePreviewContext *>(pContext);
	if(pPreview == nullptr || pEntry == nullptr)
		return;

	const CUIRect Row = EntryCtx.m_RowRect;
	// 第 0 项是「跟随服务器」：展示服务端当前下发的风格；服务端没下发时退回本地当前选择，
	// 再没有就退回第一个风格——这一项永远要有东西可看。
	const bool FollowServerEntry = Index == 0;
	const SQmTitleStyle *pStyle = FollowServerEntry ? QmTitleStyleById(pPreview->m_aFollowPreviewStyleId) : QmTitleStyleByIndex(Index - 1);
	if(FollowServerEntry && pStyle == nullptr)
		pStyle = QmTitleStyleByIndex(0);
	if(pStyle == nullptr || pPreview->m_aPreviewText[0] == '\0')
		return;

	// 条目只画效果本体：整条宽度都归预览，文字位置按条目居中。
	CUIRect PreviewRect = Row;
	PreviewRect.Margin(EntryCtx.m_Padding + 1.0f, &PreviewRect);
	if(PreviewRect.w <= 1.0f || PreviewRect.h <= 1.0f)
		return;

	ITextRender *pTextRender = pPreview->m_pTextRender;
	if(pTextRender == nullptr || pPreview->m_pRenderTools == nullptr)
		return;
	CTextCursor PreviewCursor;
	// 容器在局部原点生成，屏幕位置只在渲染时叠加一次。
	PreviewCursor.SetPosition(vec2(0.0f, 0.0f));
	PreviewCursor.m_FontSize = pPreview->m_FontSize;
	// 预览按自身宽度居中；过宽时裁在条目里，不缩字号（缩了反而看不清效果）。
	const float PreviewTextWidth = pTextRender->TextWidth(PreviewCursor.m_FontSize, pPreview->m_aPreviewText);
	const float PreviewX = PreviewRect.x + std::max(0.0f, (PreviewRect.w - PreviewTextWidth) * 0.5f);

	// 条目预览一律显示风格自身的颜色（不套本地配色档），这里是在挑风格，不是预览当前档位。
	SQmTitleRenderStyle PreviewStyle = QmTitleResolveRenderStyle(pStyle->m_pId, false, pStyle->m_pId);
	PreviewStyle.m_ColorOverride = false;
	QmTitleRenderFillCursor(pTextRender, PreviewCursor, pPreview->m_aPreviewText, PreviewCursor.m_FontSize, PreviewStyle, pPreview->m_TimeSec, 1.0f, QmTitleShimmerFromConfig());
	const ColorRGBA PreviousTextColor = pTextRender->GetTextColor();
	pTextRender->TextColor(pPreview->m_TextColor);
	STextContainerIndex &PreviewContainer = s_aTitleStylePreviewContainers[Index];
	pTextRender->DeleteTextContainer(PreviewContainer);
	pTextRender->CreateOrAppendTextContainer(PreviewContainer, &PreviewCursor, pPreview->m_aPreviewText);
	pTextRender->TextColor(PreviousTextColor);

	const STextBoundingBox PreviewBounds = pTextRender->GetBoundingBoxTextContainer(PreviewContainer);
	const float PreviewY = PreviewRect.y + (PreviewRect.h - PreviewBounds.m_H) / 2.0f;

	// 预览必须走与实际名牌相同的档位，否则用户挑完风格回到游戏里会发现效果不一样。
	if(g_Config.m_QmTitleEffect != (int)EQmTitleEffect::QM_TITLE_EFFECT_CLASSIC)
	{
		SQmTitlePolishStyle PreviewPolish;
		PreviewPolish.m_TextColor = pPreview->m_TextColor;
		if(g_Config.m_QmTitleEffect == (int)EQmTitleEffect::QM_TITLE_EFFECT_SOLID || g_Config.m_QmTitleEffect == (int)EQmTitleEffect::QM_TITLE_EFFECT_OFF)
		{
			PreviewPolish.m_GlowAlpha = 0.0f;
			PreviewPolish.m_HighlightAlpha = 0.0f;
		}
		pPreview->m_pRenderTools->RenderTitleContainerWithPolishedEffects(PreviewContainer, PreviewPolish, PreviewX, PreviewY);
		return;
	}

	SQmTitleEffectStyle PreviewEffect;
	PreviewEffect.m_TextColor = pPreview->m_TextColor;
	PreviewEffect.m_OutlineColor = pPreview->m_OutlineColor;
	PreviewEffect.m_OutlineRadius = pPreview->m_OutlineRadius;
	pPreview->m_pRenderTools->RenderTitleContainerWithCalamityEffects(PreviewContainer, PreviewEffect, PreviewX, PreviewY);
}

// 成品预览沿用名牌的配色优先级与空间效果参数，文本使用尚未保存的输入值。
// 风格由调用方选取当前草稿；颜色与效果仍复用名牌解析，保存前也能看到新风格。
static void RenderQmTitleFinishedPreview(ITextRender *pTextRender, CRenderTools *pRenderTools, const CUIRect &PreviewRect, const char *pTitle, const char *pStyleId, bool ServerRainbow, float FontSize, float TimeSec)
{
	if(PreviewRect.w <= 1.0f || PreviewRect.h <= 1.0f)
		return;
	char aPreviewText[72];
	str_format(aPreviewText, sizeof(aPreviewText), "[%s]", pTitle[0] != '\0' ? pTitle : Localize("Sponsor title"));
	const SQmTitleColorStyle ColorStyle = ResolveQmTitleColorStyle(g_Config.m_QmTitleColorMode, g_Config.m_QmTitleColor, g_Config.m_QmTitleOpacity, ServerRainbow);
	const SQmTitleRenderStyle RenderStyle = QmTitleResolveRenderStyle(pStyleId, false, "");
	const bool DynamicStyle = RenderStyle.m_pStyle != nullptr;
	const bool CustomColor = ColorStyle.m_Mode != EQmTitleColorMode::FOLLOW_SERVER;
	const bool VertexColored = (DynamicStyle && !RenderStyle.m_ColorOverride) || ColorStyle.m_Rainbow || !CustomColor;
	const float Alpha = CustomColor ? ColorStyle.m_Alpha : 1.0f;
	const ColorRGBA Color = VertexColored ? ColorRGBA(1.0f, 1.0f, 1.0f, Alpha) : ColorStyle.m_Color;
	const ColorRGBA OutlineColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha);

	CTextCursor PreviewCursor;
	PreviewCursor.SetPosition(vec2(0.0f, 0.0f));
	PreviewCursor.m_FontSize = FontSize;
	// 括号各自取相邻的字色，因此除彩虹档外整串一次性上色即可。
	if(DynamicStyle && RenderStyle.m_ColorOverride)
	{
		// 本地配色档压过风格颜色：只取风格的浮动与掠光，颜色由下面的档位决定。
		QmTitleRenderFillMotionOffsets(pTextRender, PreviewCursor, aPreviewText, FontSize, RenderStyle, TimeSec, QmTitleShimmerFromConfig());
	}
	else if(DynamicStyle)
	{
		QmTitleRenderFillCursor(pTextRender, PreviewCursor, aPreviewText, FontSize, RenderStyle, TimeSec, 1.0f, QmTitleShimmerFromConfig());
	}
	if(ColorStyle.m_Rainbow && (!DynamicStyle || RenderStyle.m_ColorOverride))
	{
		// 彩虹只跨头衔本体，与游戏内一致：游戏里 [] 是单独绘制的，不参与彩虹分段。
		char aTitleOnly[64];
		str_copy(aTitleOnly, pTitle[0] != '\0' ? pTitle : Localize("Sponsor title"));
		// 跳过左括号写色段，随后恢复游标计数，文本容器仍从整串开头生成。
		PreviewCursor.m_CharCount = 1;
		QmAddTitleRainbowSplits(PreviewCursor, aTitleOnly, 1.0f);
		PreviewCursor.m_CharCount = 0;
	}
	else if(!DynamicStyle || RenderStyle.m_ColorOverride)
	{
		PreviewCursor.m_vColorSplits.emplace_back(0, -1, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}

	const ColorRGBA PreviousTextColor = pTextRender->GetTextColor();
	pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	STextContainerIndex &PreviewContainer = s_TitleFinishedPreviewContainer;
	if(PreviewContainer.Valid())
		pTextRender->RecreateTextContainerSoft(PreviewContainer, &PreviewCursor, aPreviewText);
	else
		pTextRender->CreateOrAppendTextContainer(PreviewContainer, &PreviewCursor, aPreviewText);
	pTextRender->TextColor(PreviousTextColor);
	const STextBoundingBox Bounds = pTextRender->GetBoundingBoxTextContainer(PreviewContainer);
	const float PreviewX = PreviewRect.x + (PreviewRect.w - Bounds.m_W) / 2.0f - Bounds.m_X;
	const float PreviewY = PreviewRect.y + (PreviewRect.h - Bounds.m_H) / 2.0f - Bounds.m_Y;

	const int Effect = std::clamp(g_Config.m_QmTitleEffect, 0, 3);
	if(Effect == (int)EQmTitleEffect::QM_TITLE_EFFECT_POLISHED || Effect == (int)EQmTitleEffect::QM_TITLE_EFFECT_SOLID)
	{
		SQmTitlePolishStyle PreviewPolish;
		PreviewPolish.m_TextColor = Color;
		PreviewPolish.m_TextAlpha = Alpha;
		if(VertexColored || Effect == (int)EQmTitleEffect::QM_TITLE_EFFECT_SOLID)
		{
			PreviewPolish.m_GlowAlpha = 0.0f;
			PreviewPolish.m_HighlightAlpha = 0.0f;
		}
		pRenderTools->RenderTitleContainerWithPolishedEffects(PreviewContainer, PreviewPolish, PreviewX, PreviewY);
	}
	else if(DynamicStyle && Effect == (int)EQmTitleEffect::QM_TITLE_EFFECT_CLASSIC)
	{
		SQmTitleEffectStyle PreviewEffect;
		PreviewEffect.m_TextColor = Color;
		PreviewEffect.m_OutlineColor = OutlineColor;
		PreviewEffect.m_OutlineRadius = 2.0f;
		if(g_Config.m_QmTitleBloom > 0)
		{
			const float Breathe = std::pow(std::sin(TimeSec * 2.0f / pi) * 0.65f, 5.0f);
			PreviewEffect.m_BloomRadius = 4.0f;
			PreviewEffect.m_BloomPulse = 16.0f * Breathe;
			PreviewEffect.m_BloomRotation = TimeSec * 1.7f;
			PreviewEffect.m_BloomAlpha = g_Config.m_QmTitleBloom >= 2 ? 0.28f : 0.18f;
			PreviewEffect.m_BloomDraws = g_Config.m_QmTitleBloom >= 2 ? 16 : 6;
		}
		pRenderTools->RenderTitleContainerWithCalamityEffects(PreviewContainer, PreviewEffect, PreviewX, PreviewY);
	}
	else
		pTextRender->RenderTextContainer(PreviewContainer, Color, OutlineColor, PreviewX, PreviewY);
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
	pTextRender->TextColor(Color);
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
	const CQmChatGradientPaint Paint(pTextRender, Cursor, pText, Style != EQmSponsorChatStyle::PLATINUM ? &Gradient : nullptr);
	if(s_SponsorChatPreviewContainer.Valid())
		pTextRender->RecreateTextContainerSoft(s_SponsorChatPreviewContainer, &Cursor, pText);
	else
		pTextRender->CreateOrAppendTextContainer(s_SponsorChatPreviewContainer, &Cursor, pText);
	pTextRender->TextColor(PreviousColor);
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const vec2 PixelSize((ScreenX1 - ScreenX0) / std::max(1, pGraphics->ScreenWidth()), (ScreenY1 - ScreenY0) / std::max(1, pGraphics->ScreenHeight()));
	const SQmSponsorChatSweep SweepState = QmSponsorChatSweepState(TimeSeconds - s_StartTime, pTextRender->GetTextContainerRenderedLineCount(s_SponsorChatPreviewContainer), true);
	QmRenderSponsorChatText(pTextRender, s_SponsorChatPreviewContainer, Style, 1.0f, PixelSize, 0.0f, 0.0f, FontSize, SweepState);
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
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float TipSize = Metrics.m_SmallSize;
	const float PreviewHeight = LineHeight * 3.0f + LineSpacing * 2.0f;

	Out = {};
	Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
	// 高度只做常数次算术；直接读取展开状态，普通页和搜索页无需维护各自的布局版本。
	Out.m_MeasureEachFrame = DisplayCard;
	Out.m_Measure = [=](float) {
		const int Rows = DisplayCard ? (g_Config.m_QmTitleAdvanced ? 20 : 10) : 8;
		float Height = Rows * (LineHeight + LineSpacing) + PreviewHeight;
		if(!DisplayCard)
			Height += LineSpacing;
		if(DisplayCard && s_TitleStyleExpanded)
			Height += LineSpacing + ResolveQmTitleStylePanelHeight(std::min(QmTitleStyleCount() + 1, MAX_VISIBLE_STYLE_ITEMS));
		return Height;
	};

	FSettingsCardRenderMeasured Render = [=](CUIRect &Content) {
		const bool ReadOnly = Ctx.m_ReadOnly || pMenus->Ui()->RenderOnly() || pMenus->m_MenuTextPlanCollecting;
		if(ReadOnly)
			pMenus->Ui()->BeginRenderOnly();
		auto &Auth = pMenus->GameClient()->m_QmClient;
		auto &Draft = TitleProfileDraft(Auth, ReadOnly);
		const bool TitleStyleExpanded = s_TitleStyleExpanded;
		const bool TitleAdvanced = g_Config.m_QmTitleAdvanced != 0;
		static CButtonContainer s_RedeemButton, s_SaveButton, s_RefreshButton;
		static SQmTitleStylePreviewContext s_TitleStylePreviewContext;
		Draft.m_Code.SetHidden(true);
		Draft.m_Code.SetEmptyText(Localize("Sponsor code"));
		Draft.m_Title.SetEmptyText(Localize("Custom title"));
		Draft.m_Name.SetEmptyText(Localize("Exact bound nickname"));
		IUiContext TitleCtx = pMenus->SettingsUiContext("settings_sponsor_title", UiScale);
		if(ReadOnly)
		{
			TitleCtx.m_pAnim = nullptr;
			TitleCtx.m_pTree = nullptr;
		}
		const bool Enabled = !ReadOnly && !Auth.TitleBusy() && !pMenus->Ui()->IsPopupOpen() && !pMenus->GameClient()->m_GameConsole.IsActive();
		const auto NextRow = [&]() {
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			return Row;
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
		if(!DisplayCard)
		{
			CUIRect Row = NextRow();
			pMenus->Ui()->DoLabel(&Row, Localize(Auth.TitleStatus()), TipSize, TEXTALIGN_ML);
			Hint(&Draft.m_Revision, Row, Localize("Permanent access; up to 4 online IP addresses"));
			Row = NextRow();
			CUIRect Button;
			Row.VSplitRight(Row.w * 0.3f, &Row, &Button);
			ui_widget::InputField(TitleCtx, &Draft.m_Code, Row, Draft.m_Code.GetEmptyText(), BodySize, nullptr, true);
			Hint(&Draft.m_Code, Row, Localize("Redeem your sponsor code to unlock a custom title, shown in chat and on your nameplate."));
			if(pMenus->DoButton_Menu(&s_RedeemButton, Localize("Redeem"), 0, &Button) && Enabled && !Auth.TitleAuthenticated())
				Auth.RedeemTitleCode(Draft.m_Code.GetString());
			Row = NextRow();
			pMenus->Ui()->DoLabel(&Row, Localize("Custom title"), TipSize, TEXTALIGN_ML);
			Row = NextRow();
			ui_widget::InputField(TitleCtx, &Draft.m_Title, Row, Draft.m_Title.GetEmptyText(), BodySize, nullptr, true);
			Hint(&Draft.m_Title, Row, Localize("Up to 6 Chinese or 12 ASCII characters"));
			Row = NextRow();
			if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, &Draft.m_BindName, "qm-title-bind-name", Localize("Only show with this nickname"), Draft.m_BindName, &Row) && Enabled && Auth.TitleAuthenticated())
				Draft.m_BindName ^= 1;
			Hint(&Draft.m_BindName, Row, Localize("Limit the title to the exact nickname below."));
			Row = NextRow();
			ui_widget::InputField(TitleCtx, &Draft.m_Name, Row, Draft.m_Name.GetEmptyText(), BodySize, nullptr, true);
			Row = NextRow();
			CUIRect ChatStyleLabel, ChatStyleControl;
			Row.VSplitLeft(Row.w * 0.5f, &ChatStyleLabel, &ChatStyleControl);
			pMenus->DoSettingsMenuLabel(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, "qm-sponsor-chat-style", &ChatStyleLabel, Localize("Chat message style"), BodySize, TEXTALIGN_ML);
			static CUi::SDropDownState s_ChatStyleState;
			static CScrollRegion s_ChatStyleScroll;
			static std::array<const char *, 3> s_apChatStyleNames;
			s_apChatStyleNames = {Localize("Normal"), Localize("Soft glow"), Localize("Platinum highlight")};
			s_ChatStyleState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatStyleScroll;
			CUi::SDropDownProperties ChatStyleProperties;
			ChatStyleProperties.m_Enabled = Auth.TitleAuthenticated() && !Auth.TitleBusy() && !pMenus->GameClient()->m_GameConsole.IsActive();
			ChatStyleProperties.m_FontSize = BodySize;
			if(ReadOnly)
				pMenus->Ui()->BeginRenderOnly();
			const int ChatStyle = pMenus->DoSettingsDropDown(&ChatStyleControl, g_Config.m_QmSponsorChatStyle, s_apChatStyleNames.data(), s_apChatStyleNames.size(), s_ChatStyleState, ChatStyleProperties, &g_Config.m_QmSponsorChatStyle, nullptr, &Row);
			if(ReadOnly)
				pMenus->Ui()->EndRenderOnly();
			if(!ReadOnly && ChatStyleProperties.m_Enabled)
				g_Config.m_QmSponsorChatStyle = ChatStyle;
			Hint(&g_Config.m_QmSponsorChatStyle, Row, Localize("Saved with your title and shared with other QmClient players."));

			CUIRect ChatPreview, ChatPreviewLabel, ChatPreviewArea;
			Content.HSplitTop(PreviewHeight, &ChatPreview, &Content);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			ChatPreview.HSplitTop(TipSize, &ChatPreviewLabel, &ChatPreviewArea);
			pMenus->Ui()->DoLabel(&ChatPreviewLabel, Localize("Chat message preview"), TipSize, TEXTALIGN_ML);
			pMenus->Ui()->ClipEnable(&ChatPreviewArea);
			RenderQmSponsorChatPreview(pMenus->Graphics(), pMenus->TextRender(), ChatPreviewArea, BodySize, static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle), Auth.TitleAnimationTime());
			pMenus->Ui()->ClipDisable();
			Row = NextRow();
			Row.VSplitMid(&Row, &Button, LineSpacing);
			if(pMenus->DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &Row) && Enabled && Auth.TitleAuthenticated() && (!Draft.m_BindName || Draft.m_Name.GetString()[0]))
				Auth.SaveTitleProfile(Draft.m_Title.GetString(), Draft.m_BindName ? Draft.m_Name.GetString() : "", g_Config.m_QmTitleStyleEnabled ? g_Config.m_QmTitleStyle : "", static_cast<EQmSponsorChatStyle>(g_Config.m_QmSponsorChatStyle));
			if(pMenus->DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &Button) && Enabled)
				Auth.RefreshTitleProfile();
		}
		else
		{
			CUIRect Row;
			SLabelProperties SingleLineProps;
			SingleLineProps.m_DisallowNewline = true;
			SingleLineProps.m_StopAtEnd = true;
			SingleLineProps.m_MinimumFontSize = 6.0f;
			const auto VisibilityToggle = [&](int *pValue, const char *pTextId, const char *pLabel) {
				const CUIRect ToggleRow = NextRow();
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, pValue, pTextId, pLabel, *pValue, &ToggleRow, SingleLineProps) && !ReadOnly)
					*pValue ^= 1;
			};
			VisibilityToggle(&g_Config.m_QmShowNameplateTitle, "qm-title-show-nameplate", Localize("Show titles on nameplates"));
			VisibilityToggle(&g_Config.m_QmNameplateTitleAboveName, "qm-title-above-name", Localize("Show titles above player names"));
			VisibilityToggle(&g_Config.m_QmShowMainTitle, "qm-title-show-main", Localize("Show main tee title locally"));
			VisibilityToggle(&g_Config.m_QmShowDummyTitle, "qm-title-show-dummy", Localize("Show dummy title locally"));
			Row = NextRow();
			if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, &g_Config.m_QmSponsorChatEffects, "qm-sponsor-chat-effects", Localize("Show sponsor chat effects locally"), g_Config.m_QmSponsorChatEffects, &Row) && !ReadOnly)
				g_Config.m_QmSponsorChatEffects ^= 1;
			Hint(&g_Config.m_QmSponsorChatEffects, Row, Localize("Turning this off only changes your display."));
			// 常用风格与波浪幅度保持直达，其余外观参数统一收进高级模式。
			{
				// 风格选择：条目里渲染「头衔文本 + 该风格」的实时效果，整列收进下拉，
				// 最多显示六项，更多风格在面板内部滚动。
				// 条目只画效果本体：统一用「[赞助者]」当文本，不显示效果名、也不显示玩家名字。
				const char *pSponsorPreview = "[赞助者]";
				str_copy(s_TitleStylePreviewContext.m_aPreviewText, pSponsorPreview);
				s_TitleStylePreviewContext.m_FontSize = TipSize;
				s_TitleStylePreviewContext.m_TimeSec = (float)Auth.TitleAnimationTime();
				// 条目预览一律显示风格自身的颜色：这里是在挑风格，不是预览当前档位。
				// 服务端是否已下发风格只影响第 0 项「跟随服务器」显示哪个效果，不影响其它条目。
				const char *pServerStyleId = Auth.PlayerTitleStyle(pMenus->GameClient()->m_Snap.m_LocalClientId);
				s_TitleStylePreviewContext.m_aFollowPreviewStyleId[0] = '\0';
				if(pServerStyleId[0] != '\0')
					str_copy(s_TitleStylePreviewContext.m_aFollowPreviewStyleId, pServerStyleId);
				else if(g_Config.m_QmTitleStyleEnabled)
					str_copy(s_TitleStylePreviewContext.m_aFollowPreviewStyleId, g_Config.m_QmTitleStyle);
				// 弹层回调不带 this，渲染接口在这里带上；两者当帧有效。
				s_TitleStylePreviewContext.m_pTextRender = pMenus->TextRender();
				s_TitleStylePreviewContext.m_pRenderTools = pMenus->GameClient()->RenderTools();

				// 选择行：点一下就在卡片内向下展开效果列表（不用浮层，层级与高度都可控）。
				const SQmTitleStyle *pCurrentStyle = g_Config.m_QmTitleStyleEnabled ? QmTitleStyleById(g_Config.m_QmTitleStyle) : nullptr;
				Row = NextRow();
				CUIRect TitleStyleLabel, TitleStyleField;
				Row.VSplitLeft(Row.w * 0.45f, &TitleStyleLabel, &TitleStyleField);
				pMenus->Ui()->DoLabel(&TitleStyleLabel, Localize("Title style"), BodySize, TEXTALIGN_ML);
				TitleStyleField.VSplitLeft(LineSpacing, nullptr, &TitleStyleField);
				const bool FieldHovered = !ReadOnly && pMenus->Ui()->MouseInside(&TitleStyleField);
				DrawRoundedSurface(pMenus->Ui(), TitleStyleField, ColorRGBA(1.0f, 1.0f, 1.0f, FieldHovered ? 0.14f : 0.06f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), ui_token::radius::BASE);
				CUIRect FieldText = TitleStyleField;
				FieldText.VMargin(2.0f, &FieldText);
				FieldText.VSplitRight(LineHeight, &FieldText, nullptr);
				pMenus->Ui()->DoLabel(&FieldText, pCurrentStyle != nullptr ? pCurrentStyle->m_pLabel : Localize("Server-assigned style"), TipSize, TEXTALIGN_ML);
				pMenus->Ui()->DoLabel(&TitleStyleField, TitleStyleExpanded ? "▲" : "▼", TipSize, TEXTALIGN_MR);
				if(!ReadOnly && pMenus->Ui()->DoButtonLogic(&s_TitleStyleFieldId, TitleStyleExpanded ? 1 : 0, &TitleStyleField, BUTTONFLAG_LEFT))
					s_TitleStyleExpanded = !TitleStyleExpanded;

				if(TitleStyleExpanded)
				{
					// 展开区：卡片内自绘列表，条目只有 [赞助者] 的效果本体。
					// 面板高度与 m_Measure 的算法逐字对应，不能再出现内容比测量值高一行的情况。
					// 31 个风格一屏放不下，用滚动区按可见项渲染（同时避免每帧建 31 个文本容器）。
					const int VisibleStyleItems = std::min(QmTitleStyleCount() + 1, MAX_VISIBLE_STYLE_ITEMS);
					CUIRect Panel;
					Content.HSplitTop(ResolveQmTitleStylePanelHeight(VisibleStyleItems), &Panel, &Content);
					Content.HSplitTop(LineSpacing, nullptr, &Content);
					s_TitleStylePanelRect = Panel;
					DrawRoundedSurface(pMenus->Ui(), Panel, ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), ui_token::radius::BASE);

					CUIRect ListArea = Panel;
					ListArea.Margin(STYLE_ITEM_INSET, &ListArea);
					CScrollRegionParams ListScrollParams;
					ListScrollParams.m_ScrollUnit = STYLE_ITEM_HEIGHT + STYLE_ITEM_SPACING;
					vec2 ListScrollOffset;
					s_TitleStyleListScrollRegion.Begin(&ListArea, &ListScrollOffset, &ListScrollParams);
					CUIRect Items = ListArea;
					Items.y += ListScrollOffset.y;

					for(int ItemIndex = 0; ItemIndex < QmTitleStyleCount() + 1; ++ItemIndex)
					{
						CUIRect Item;
						Items.HSplitTop(STYLE_ITEM_HEIGHT, &Item, &Items);
						Items.HSplitTop(STYLE_ITEM_SPACING, nullptr, &Items);
						if(!s_TitleStyleListScrollRegion.AddRect(Item))
							continue;
						const bool Selected = ItemIndex == 0 ? !g_Config.m_QmTitleStyleEnabled :
										       g_Config.m_QmTitleStyleEnabled && pCurrentStyle == QmTitleStyleByIndex(ItemIndex - 1);
						const bool ItemHovered = !ReadOnly && pMenus->Ui()->MouseInside(&Item);
						if(Selected || ItemHovered)
							DrawRoundedSurface(pMenus->Ui(), Item, ColorRGBA(1.0f, 1.0f, 1.0f, Selected ? 0.22f : 0.10f), ColorRGBA(), ui_token::radius::TIGHT);
						// 预览只画效果本体：第 0 项画服务端当前风格，其余画对应风格。
						const SQmTitleStylePreviewEntryContext ItemCtx{Item, STYLE_ITEM_INSET, TipSize};
						RenderQmTitleStylePreviewEntry(&s_TitleStylePreviewContext, ItemCtx, ItemIndex, "");
						if(!ReadOnly && pMenus->Ui()->DoButtonLogic(&s_aTitleStyleItemIds[ItemIndex], Selected ? 1 : 0, &Item, BUTTONFLAG_LEFT))
						{
							if(ItemIndex == 0)
							{
								// 回滚口：恢复「跟随服务端下发的风格」，本地选择随之停用。
								g_Config.m_QmTitleStyleEnabled = 0;
							}
							else
							{
								const SQmTitleStyle *pPicked = QmTitleStyleByIndex(ItemIndex - 1);
								if(pPicked != nullptr)
								{
									str_copy(g_Config.m_QmTitleStyle, pPicked->m_pId);
									g_Config.m_QmTitleStyleEnabled = 1;
								}
							}
							// 选择任意风格后立即恢复风格配色；仍可随后手动选择单色或彩虹。
							g_Config.m_QmTitleColorMode = (int)EQmTitleColorMode::FOLLOW_SERVER;
							s_TitleStyleExpanded = false;
							pMenus->GameClient()->m_Chat.RebuildChat();
						}
					}
					s_TitleStyleListScrollRegion.End();
				}
				// 点在选择行以外的地方收起列表；选择行自己的点击只负责展开/收起。
				// 命中的是整行（标签 + 控件），否则点标签会落进下面的输入框区域。
				s_TitleStyleFieldRect = Row;
				if(s_TitleStyleExpanded && !ReadOnly && pMenus->Ui()->MouseButtonClicked(0) &&
					pMenus->Ui()->ActiveItem() == nullptr && !pMenus->Ui()->MouseInside(&s_TitleStyleFieldRect) && !pMenus->Ui()->MouseInside(&s_TitleStylePanelRect))
					s_TitleStyleExpanded = false;

				// 动画方式与强度共用普通页、搜索页和成品预览的配置。
				Row = NextRow();
				CUIRect MotionLabel, MotionControl;
				Row.VSplitLeft(Row.w * 0.55f, &MotionLabel, &MotionControl);
				pMenus->DoSettingsMenuLabel(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, "qm-title-motion-mode", &MotionLabel, Localize("Title motion"), BodySize, TEXTALIGN_ML);
				static CUi::SDropDownState s_TitleMotionState;
				static CScrollRegion s_TitleMotionScroll;
				const std::array<const char *, 2> apMotionNames = {Localize("Vertical wave"), Localize("Pop wave")};
				s_TitleMotionState.m_SelectionPopupContext.m_pScrollRegion = &s_TitleMotionScroll;
				const int MotionMode = pMenus->DoSettingsDropDown(&MotionControl, g_Config.m_QmTitleMotionMode, apMotionNames.data(), apMotionNames.size(), s_TitleMotionState, {}, &g_Config.m_QmTitleMotionMode, nullptr, &Row);
				if(!ReadOnly && MotionMode != g_Config.m_QmTitleMotionMode)
				{
					g_Config.m_QmTitleMotionMode = MotionMode;
					pMenus->GameClient()->m_Chat.RebuildChat();
				}
				Row = NextRow();
				if(pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-bob-amplitude", &g_Config.m_QmTitleBobAmplitude, &g_Config.m_QmTitleBobAmplitude, &Row, Localize("Wave amount"), 0, 3, &CUi::ms_LinearScrollbarScale, 0, ""))
					pMenus->GameClient()->m_Chat.RebuildChat();
				Hint(&g_Config.m_QmTitleBobAmplitude, Row, Localize("Strength of the vertical or pop wave. Zero keeps the title still."));
			}
			Row = NextRow();
			if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, &g_Config.m_QmTitleInstantHints, "qm-title-instant-hints", Localize("Instant compact hints"), g_Config.m_QmTitleInstantHints, &Row) && !ReadOnly)
				g_Config.m_QmTitleInstantHints ^= 1;
			// 高级模式只控制这些入口是否展开，不重置已经保存的参数。
			Row = NextRow();
			if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, &g_Config.m_QmTitleAdvanced, "qm-title-advanced", Localize("Advanced mode"), TitleAdvanced, &Row) && !ReadOnly)
				g_Config.m_QmTitleAdvanced ^= 1;
			if(TitleAdvanced)
			{
				// 预热时只绘制控件，不消费输入或写回配置。
				if(ReadOnly)
					pMenus->Ui()->BeginRenderOnly();
				bool AppearanceChanged = false;
				CUIRect Label, Control;
				static CUi::SDropDownState s_TitleColorModeState, s_TitleEffectState, s_TitleBloomState;
				static CScrollRegion s_TitleColorModeScroll, s_TitleEffectScroll, s_TitleBloomScroll;
				static std::vector<const char *> s_TitleColorModeNames, s_TitleEffectNames, s_TitleBloomNames;
				s_TitleColorModeNames = {Localize("Use style colors", "Title color mode"), Localize("Single color"), Localize("Rainbow")};
				s_TitleEffectNames = {Localize("Polished"), Localize("Solid"), Localize("Classic"), Localize("Off")};
				s_TitleBloomNames = {Localize("Off"), Localize("Subtle"), Localize("Full")};
				Row = NextRow();
				Row.VSplitLeft(Row.w * 0.55f, &Label, &Control);
				pMenus->DoSettingsMenuLabel(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, "qm-title-color-mode", &Label, Localize("Title color"), BodySize, TEXTALIGN_ML);
				Hint(&g_Config.m_QmTitleColorMode, Row, Localize("Single color and Rainbow override the color of animated styles; the style keeps its motion"));
				s_TitleColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_TitleColorModeScroll;
				const int ColorMode = pMenus->DoSettingsDropDown(&Control, g_Config.m_QmTitleColorMode, s_TitleColorModeNames.data(), s_TitleColorModeNames.size(), s_TitleColorModeState, {}, &g_Config.m_QmTitleColorMode, nullptr, &Row);
				if(!ReadOnly && ColorMode != g_Config.m_QmTitleColorMode)
				{
					g_Config.m_QmTitleColorMode = ColorMode;
					AppearanceChanged = true;
				}

				Row = NextRow();
				static CButtonContainer s_TitleColorResetId;
				SSettingsContentMetrics ColorMetrics = Metrics;
				ColorMetrics.m_LineSpacing = 0.0f;
				static unsigned int s_LastTitleColor = g_Config.m_QmTitleColor;
				pMenus->DoLine_ColorPicker(&s_TitleColorResetId, ColorMetrics, &Row, Localize("Single color"), &g_Config.m_QmTitleColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTitleColor)), false, nullptr, false, false);
				AppearanceChanged |= s_LastTitleColor != g_Config.m_QmTitleColor;
				if(!ReadOnly)
					s_LastTitleColor = g_Config.m_QmTitleColor;
				Row = NextRow();
				AppearanceChanged |= pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-opacity", &g_Config.m_QmTitleOpacity, &g_Config.m_QmTitleOpacity, &Row, Localize("Title opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				Hint(&g_Config.m_QmTitleOpacity, Row, Localize("Applies to single-color and rainbow overrides."));
				Row = NextRow();
				AppearanceChanged |= pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-phase", &g_Config.m_QmTitlePhase, &g_Config.m_QmTitlePhase, &Row, Localize("Light band"), 0, 200);
				Hint(&g_Config.m_QmTitlePhase, Row, Localize("Zero uses the style default. Higher values make color bands narrower."));

				Row = NextRow();
				Row.VSplitLeft(Row.w * 0.55f, &Label, &Control);
				pMenus->DoSettingsMenuLabel(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, "qm-title-effect", &Label, Localize("Text effect"), BodySize, TEXTALIGN_ML);
				s_TitleEffectState.m_SelectionPopupContext.m_pScrollRegion = &s_TitleEffectScroll;
				const int Effect = pMenus->DoSettingsDropDown(&Control, g_Config.m_QmTitleEffect, s_TitleEffectNames.data(), s_TitleEffectNames.size(), s_TitleEffectState, {}, &g_Config.m_QmTitleEffect, nullptr, &Row);
				if(!ReadOnly && Effect != g_Config.m_QmTitleEffect)
				{
					g_Config.m_QmTitleEffect = Effect;
					AppearanceChanged = true;
				}
				Row = NextRow();
				Row.VSplitLeft(Row.w * 0.55f, &Label, &Control);
				pMenus->DoSettingsMenuLabel(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, "qm-title-bloom", &Label, Localize("Classic title glow"), BodySize, TEXTALIGN_ML);
				s_TitleBloomState.m_SelectionPopupContext.m_pScrollRegion = &s_TitleBloomScroll;
				const int Bloom = pMenus->DoSettingsDropDown(&Control, g_Config.m_QmTitleBloom, s_TitleBloomNames.data(), s_TitleBloomNames.size(), s_TitleBloomState, {}, &g_Config.m_QmTitleBloom, nullptr, &Row);
				if(!ReadOnly && Bloom != g_Config.m_QmTitleBloom)
				{
					g_Config.m_QmTitleBloom = Bloom;
					AppearanceChanged = true;
				}
				Row = NextRow();
				AppearanceChanged |= pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-shimmer-speed", &g_Config.m_QmTitleShimmerSpeed, &g_Config.m_QmTitleShimmerSpeed, &Row, Localize("Shimmer speed"), 0, 400);
				Hint(&g_Config.m_QmTitleShimmerSpeed, Row, Localize("Highlight movement in Polished mode. Zero turns it off."));
				Row = NextRow();
				AppearanceChanged |= pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-bob-wavelength", &g_Config.m_QmTitleBobWavelength, &g_Config.m_QmTitleBobWavelength, &Row, Localize("Wave spacing"), 16, 1024, &CUi::ms_LinearScrollbarScale, 0, "px");
				Row = NextRow();
				AppearanceChanged |= pMenus->DoSettingsScrollbarOption(CMenus::SETTINGS_CONTRIBUTORS, -1, "qm-title-bob-speed", &g_Config.m_QmTitleBobSpeed, &g_Config.m_QmTitleBobSpeed, &Row, Localize("Wave speed"), 0, 2000);
				Hint(&g_Config.m_QmTitleBobSpeed, Row, Localize("Higher values make the wave move faster. Zero stops the movement."));
				Row = NextRow();
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTRIBUTORS, -1, -1, &g_Config.m_QmTitleBobPixelSnap, "qm-title-bob-pixel-snap", Localize("Snap wave to pixels"), g_Config.m_QmTitleBobPixelSnap, &Row) && !ReadOnly)
				{
					g_Config.m_QmTitleBobPixelSnap ^= 1;
					AppearanceChanged = true;
				}
				if(ReadOnly)
					pMenus->Ui()->EndRenderOnly();
				if(!ReadOnly && AppearanceChanged)
					pMenus->GameClient()->m_Chat.RebuildChat();
			}

			CUIRect Preview, PreviewLabel, TitlePreviewArea;
			Content.HSplitTop(PreviewHeight, &Preview, &Content);
			Preview.HSplitTop(TipSize, &PreviewLabel, &TitlePreviewArea);
			pMenus->Ui()->DoLabel(&PreviewLabel, Localize("Title preview"), TipSize, TEXTALIGN_ML);
			const char *pPreviewStyleId = g_Config.m_QmTitleStyleEnabled ? g_Config.m_QmTitleStyle : Auth.PlayerTitleStyle(pMenus->GameClient()->m_Snap.m_LocalClientId);
			if(pPreviewStyleId[0] == '\0')
				pPreviewStyleId = Auth.TitleProfileStyle();
			pMenus->Ui()->ClipEnable(&TitlePreviewArea);
			RenderQmTitleFinishedPreview(pMenus->TextRender(), pMenus->GameClient()->RenderTools(), TitlePreviewArea, Draft.m_Title.GetString(), pPreviewStyleId, pMenus->GameClient()->IsQmDeveloperRainbow(pMenus->GameClient()->m_Snap.m_LocalClientId), BodySize * 1.4f, (float)Auth.TitleAnimationTime());
			pMenus->Ui()->ClipDisable();
		}
		if(ReadOnly)
			pMenus->Ui()->EndRenderOnly();
	};
	Out.m_Render = [Render](CUIRect Content) { Render(Content); };
	Out.m_RenderMeasured = std::move(Render);
	if(DisplayCard && !Ctx.m_ReadOnly)
	{
		Out.m_PreLayoutInput = [=](CUIRect Content) {
			if(pMenus->m_MenuTextPlanCollecting || pMenus->Ui()->RenderOnly())
				return false;
			Content.HSplitTop(5.0f * (LineHeight + LineSpacing), nullptr, &Content);
			CUIRect StyleRow, StyleField;
			Content.HSplitTop(LineHeight, &StyleRow, &Content);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			StyleRow.VSplitLeft(StyleRow.w * 0.45f + LineSpacing, nullptr, &StyleField);
			bool Changed = false;
			if(pMenus->Ui()->DoButtonLogic(&s_TitleStyleFieldId, s_TitleStyleExpanded, &StyleField, BUTTONFLAG_LEFT))
			{
				s_TitleStyleExpanded = !s_TitleStyleExpanded;
				Changed = true;
			}
			if(s_TitleStyleExpanded)
				Content.HSplitTop(ResolveQmTitleStylePanelHeight(std::min(QmTitleStyleCount() + 1, MAX_VISIBLE_STYLE_ITEMS)) + LineSpacing, nullptr, &Content);
			Content.HSplitTop(2.0f * (LineHeight + LineSpacing), nullptr, &Content);
			CUIRect AdvancedRow;
			Content.HSplitTop(LineHeight, &AdvancedRow, &Content);
			if(pMenus->Ui()->DoButtonLogic(&g_Config.m_QmTitleAdvanced, g_Config.m_QmTitleAdvanced, &AdvancedRow, BUTTONFLAG_LEFT))
			{
				g_Config.m_QmTitleAdvanced ^= 1;
				Changed = true;
			}
			return Changed;
		};
	}
	return true;
}
