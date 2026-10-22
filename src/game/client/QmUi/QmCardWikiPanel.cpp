// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmCardWikiPanel.h"

#include "QmCardRegistry.h"
#include "QmCardWiki.h"
#include "QmWikiMarkdown.h"
#include "QmWikiMarkdownRender.h"
#include "QmWikiEditor.h"
#include "QmWikiMedia.h"
#include "QmWikiStore.h"
#include "SecondaryPanel.h"
#include "SettingsPageLayout.h"
#include "UiButtons.h"
#include "UiContext.h"
#include "UiSurfaceText.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <base/system.h>

#include <engine/shared/localization.h>
#include <engine/storage.h>

#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	// 未发布阶段允许在客户端直接改 Wiki 文案（写入用户目录的 wiki/ 覆盖文件）；
	// 发布版本把这里改成 false 就变回只读，不需要改其它逻辑。
	constexpr bool WIKI_AUTHORING_ENABLED = true;

	// 弹层回调只拿到上下文指针，绘制依赖与内容必须在打开时快照下来。
	struct SWikiPopupContext
	{
		CUi *m_pUi = nullptr;
		ITextRender *m_pTextRender = nullptr;
		CUiV2AnimationRuntime *m_pAnim = nullptr;
		CUiV2Tree *m_pTree = nullptr;
		IStorage *m_pStorage = nullptr;
		SUiTheme m_Theme;
		float m_UiScale = 1.0f;
		ESettingsSecondaryPanelKind m_Kind = ESettingsSecondaryPanelKind::LIBRARY;

		// 内置兜底内容（卡片三段或长文档），同时用于“还原内置”。
		SWikiDocument m_Fallback{};
		const char *m_apCardParagraphs[3]{};
		SWikiSection m_aCardSections[3]{};
		char m_aTitle[96] = "";
		bool m_IsCard = true;

		// 内容、目录、草稿与保存/还原全部由编辑状态机维护，面板只负责绘制。
		SWikiEditorState m_Editor;
		// 编辑框是 UI 组件：内容由状态机提供，输入变化回写给状态机。
		CLineInputBuffered<65536> m_EditInput;
		bool m_ResetScroll = true;
		// 点击目录后让滚动区把目标块顶到顶部。
		int m_TocTarget = -1;
		// 正文跳转先记下来，等本次绘制结束后再换内容，避免布局中途失效。
		char m_aPendingLink[128] = "";
	};

	SWikiPopupContext gs_WikiPopupContext;
	SPopupMenuId gs_WikiPopupId;

	// 卡片之间可以互相跳转；外部地址交给系统浏览器。
	void NavigateTo(SWikiPopupContext *pPopup, const char *pTarget)
	{
		if(pTarget == nullptr || pTarget[0] == '\0')
			return;
		const bool IsCard = str_startswith(pTarget, "qm:") != nullptr || str_startswith(pTarget, "tclient:") != nullptr || str_startswith(pTarget, "deck:") != nullptr;
		if(!IsCard)
		{
			open_link(pTarget);
			return;
		}
		const SCardWikiCopy *pCopy = FindCardWikiCopy(pTarget);
		if(pCopy == nullptr)
			return;
		const auto *pDefault = qm_card_registry::FindByStableId(pTarget);
		const char *pTitle = pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : pTarget;
		pPopup->m_apCardParagraphs[0] = pCopy->m_pWhat;
		pPopup->m_apCardParagraphs[1] = pCopy->m_pHow;
		pPopup->m_apCardParagraphs[2] = pCopy->m_pAdvice;
		const char *apHeadings[3] = {Localizable("What this controls"), Localizable("How to use it"), Localizable("Good to know")};
		for(int Index = 0; Index < 3; ++Index)
			pPopup->m_aCardSections[Index] = {apHeadings[Index], &pPopup->m_apCardParagraphs[Index], 1};
		pPopup->m_Fallback = {pTarget, pTitle, pPopup->m_aCardSections, 3};
		pPopup->m_IsCard = true;
		str_copy(pPopup->m_aTitle, pTitle, sizeof(pPopup->m_aTitle));
		pPopup->m_Editor.LeaveEditing();
		pPopup->m_ResetScroll = true;
		pPopup->m_TocTarget = -1;
		pPopup->m_Editor.Load(pTarget, pPopup->m_Fallback, true);
	}

	CUi::EPopupMenuFunctionResult RenderWikiPopup(void *pContext, CUIRect View, bool Active)
	{
		auto *pPopup = static_cast<SWikiPopupContext *>(pContext);
		if(pPopup == nullptr || pPopup->m_pUi == nullptr || pPopup->m_pTextRender == nullptr)
			return CUi::POPUP_CLOSE_CURRENT;

		// 图片解码按帧限额：布局只查缓存，可见的图每帧最多真正解码一张。
		qm_wiki_media::ResetFrameBudget(1);

		// 每帧刷新来源帧：菜单失活后本回调不再运行，弹层栈会把面板当作孤儿收掉，
		// 否则它会留在栈上继续屏蔽全局鼠标输入（幽灵模态）。
		pPopup->m_pUi->RefreshPopupMenuSource(&gs_WikiPopupId, true, pPopup->m_pUi->PopupSourceFrame());

		// 上一帧请求的跳转在这里落地：此时没有正在迭代的正文，可以安全换内容。
		if(pPopup->m_aPendingLink[0] != '\0')
		{
			char aTarget[128];
			str_copy(aTarget, pPopup->m_aPendingLink, sizeof(aTarget));
			pPopup->m_aPendingLink[0] = '\0';
			NavigateTo(pPopup, aTarget);
		}

		IUiContext Ctx;
		Ctx.m_pUi = pPopup->m_pUi;
		Ctx.m_pTextRender = pPopup->m_pTextRender;
		Ctx.m_pAnim = pPopup->m_pAnim;
		Ctx.m_pTree = pPopup->m_pTree;
		Ctx.m_pTheme = &pPopup->m_Theme;
		Ctx.m_UiScale = pPopup->m_UiScale;
		Ctx.m_pStorage = pPopup->m_pStorage;
		Ctx.m_ScopeHash = MakeUiScopeHash("qm_wiki_document");

		const float UiScale = std::clamp(pPopup->m_UiScale, 0.5f, 2.0f);
		const float BodySize = ui_token::font::BODY * UiScale;
		const float TipSize = ui_token::font::TIP * UiScale;
		const float Gap = ui_token::spacing::MD * UiScale;
		const SUiTheme &Theme = *Ctx.m_pTheme;

		static ui_widget::SSecondaryPanelLabel s_Title;
		static CButtonContainer s_CloseButton;
		ui_widget::CSecondaryPanel Panel(Ctx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(std::max(0.0f, Ctx.m_pUi->Screen()->w - 16.0f), pPopup->m_Kind, UiScale), {});
		const char *pTitle = pPopup->m_aTitle[0] != '\0' ? pPopup->m_aTitle : (pPopup->m_Fallback.m_pTitle != nullptr ? Localize(pPopup->m_Fallback.m_pTitle) : Localize("Card guide"));
		if(Panel.Header(s_Title, s_CloseButton, pTitle))
			return CUi::POPUP_CLOSE_CURRENT;

		CUIRect Content = Panel.ContentRect();
		if(Content.w <= 0.0f || Content.h <= 0.0f)
			return CUi::POPUP_KEEP_OPEN;

		// 编辑态的 Esc 只退出编辑（草稿留在输入框里，再点「编辑」继续），不整块关面板。
		// 弹层栈在回调之后才处理 Esc，这里先消费掉就能拦住关闭。
		if(pPopup->m_Editor.m_Editing && Ctx.m_pUi->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			pPopup->m_Editor.LeaveEditing();

		CUiScopedSurfaceText SurfaceText(pPopup->m_pTextRender, Theme.m_Surface);
		const ColorRGBA Foreground = ResolveConfiguredTextColor(Theme.m_Surface);
		const ColorRGBA Muted = Foreground.WithMultipliedAlpha(0.62f);
		const ColorRGBA ErrorColor = Theme.m_Accent;

		// 顶部工具条：左侧状态，右侧操作按钮。
		const float ButtonHeight = std::clamp(BodySize * 1.85f, 22.0f * UiScale, 34.0f * UiScale);
		const float ButtonWidth = std::clamp(BodySize * 5.4f, 62.0f * UiScale, 120.0f * UiScale);
		CUIRect Toolbar;
		Content.HSplitTop(ButtonHeight, &Toolbar, &Content);
		Content.HSplitTop(Gap * 0.5f, nullptr, &Content);
		CUIRect Buttons = Toolbar;
		// 默认不显示任何状态词：只有保存、还原或报错之后才出现一行结果提示。
		const char *pStatusText = pPopup->m_Editor.m_aStatus;
		CUIRect StatusRect = Toolbar;

		static CButtonContainer s_EditButton;
		static CButtonContainer s_SaveButton;
		static CButtonContainer s_RevertButton;
		static CButtonContainer s_CancelButton;

		if(!pPopup->m_Editor.m_Editing)
		{
			if(WIKI_AUTHORING_ENABLED)
			{
				CUIRect EditRect;
				Buttons.VSplitRight(ButtonWidth, &Buttons, &EditRect);
				if(ui_widget::SecondaryButton(Ctx, &s_EditButton, Localize("Edit"), EditRect))
				{
					pPopup->m_Editor.BeginEditing();
					pPopup->m_EditInput.Set(pPopup->m_Editor.m_Draft.c_str());
				}
				StatusRect.w = std::max(0.0f, StatusRect.w - EditRect.w - Gap);
			}
		}
		else
		{
			CUIRect SaveRect;
			CUIRect RevertRect;
			CUIRect CancelRect;
			Buttons.VSplitRight(ButtonWidth * 0.8f, &Buttons, &CancelRect);
			Buttons.VSplitRight(ButtonWidth * 0.8f, &Buttons, &RevertRect);
			Buttons.VSplitRight(ButtonWidth, &Buttons, &SaveRect);
			StatusRect.w = std::max(0.0f, StatusRect.w - (SaveRect.w + RevertRect.w + CancelRect.w + Gap * 3.0f));
			if(ui_widget::PrimaryButton(Ctx, &s_SaveButton, Localize("Save"), SaveRect) && pPopup->m_Editor.Save())
				pPopup->m_EditInput.Set(pPopup->m_Editor.m_Draft.c_str());
			if(ui_widget::SecondaryButton(Ctx, &s_RevertButton, Localize("Reset"), RevertRect))
			{
				pPopup->m_Editor.ResetToBuiltIn();
				pPopup->m_EditInput.Set(pPopup->m_Editor.m_Draft.c_str());
			}
			if(ui_widget::SecondaryButton(Ctx, &s_CancelButton, Localize("Done"), CancelRect))
			{
				pPopup->m_Editor.LeaveEditing();
				pPopup->m_Editor.SetStatus("", false);
			}
		}

		if(StatusRect.w > 0.0f && pStatusText != nullptr && pStatusText[0] != '\0')
		{
			// 状态点：错误红、草稿强调色、内置内容弱化——一眼能看出内容来自哪里。
			const bool IsDraft = !pPopup->m_Editor.m_StatusIsError && pPopup->m_Editor.m_Content.m_HasUserOverride;
			const ColorRGBA DotColor = pPopup->m_Editor.m_StatusIsError ? ErrorColor : (IsDraft ? Theme.m_Accent : Muted.WithMultipliedAlpha(0.45f));
			const float DotSize = 6.0f * UiScale;
			CUIRect DotRect;
			DotRect.x = StatusRect.x;
			DotRect.y = StatusRect.y + (StatusRect.h - DotSize) * 0.5f;
			DotRect.w = DotSize;
			DotRect.h = DotSize;
			DotRect.Draw(DotColor, IGraphics::CORNER_ALL, DotSize * 0.5f);
			StatusRect.x += DotSize + 6.0f * UiScale;
			StatusRect.w = std::max(0.0f, StatusRect.w - DotSize - 6.0f * UiScale);
			SLabelProperties Props;
			Props.m_MaxWidth = StatusRect.w;
			Props.m_EllipsisAtEnd = true;
			Props.SetColor(pPopup->m_Editor.m_StatusIsError ? ErrorColor : Muted);
			Ctx.m_pUi->DoLabel(&StatusRect, pStatusText, TipSize, TEXTALIGN_ML, Props);
		}

		if(!pPopup->m_Editor.m_Editing)
		{
			static CScrollRegion s_Scroll;
			if(pPopup->m_ResetScroll)
			{
				s_Scroll.Reset();
				pPopup->m_ResetScroll = false;
			}
			CScrollRegionParams ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, UiScale);
			ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::POPUP;
			ScrollParams.m_pSliderThemeColor = &Theme.m_Accent;
			vec2 ScrollOffset;
			s_Scroll.Begin(&Content, &ScrollOffset, &ScrollParams);
			CUIRect Body = Content;
			Body.x += ScrollOffset.x;
			Body.y += ScrollOffset.y;

			// 阅读宽度：超过这个宽度就居中留白，避免一行文字横跨整个面板。
			const float ReadingWidth = std::min(Body.w, 760.0f * UiScale);
			if(Body.w > ReadingWidth)
			{
				Body.x += (Body.w - ReadingWidth) * 0.5f;
				Body.w = ReadingWidth;
			}

			SWikiRenderStyle Style;
			Style.m_BodySize = BodySize;
			Style.m_UiScale = UiScale;
			Style.m_Text = Foreground;
			Style.m_Muted = Muted;
			Style.m_Accent = Theme.m_Accent;
			Style.m_Surface = Theme.m_Surface;
			Style.m_CodeSurface = Foreground.WithMultipliedAlpha(0.07f);
			Style.m_Line = Foreground.WithMultipliedAlpha(0.14f);

			// 目录：文档有 2 个以上标题时列在正文最前面，点击把该小节顶到可视区顶部。
			if(pPopup->m_Editor.m_vTocBlocks.size() >= 2)
			{
				const std::vector<SWikiBlock> &vBlocks = pPopup->m_Editor.m_Markdown.Blocks();
				const float TocTop = Body.y;
				const float TocLineHeight = TipSize * 1.9f;
				CUIRect TocTitle;
				Body.HSplitTop(TocLineHeight, &TocTitle, &Body);
				SLabelProperties TocTitleProps;
				TocTitleProps.SetColor(Muted);
				Ctx.m_pUi->DoLabel(&TocTitle, Localize("Contents"), TipSize, TEXTALIGN_ML, TocTitleProps);
				// 每个条目的按钮 ID 用标题块下标，内容换文档时 ID 随之变化不会串。
				static int s_aTocIds[64];
				for(size_t TocIndex = 0; TocIndex < pPopup->m_Editor.m_vTocBlocks.size() && TocIndex < 64; ++TocIndex)
				{
					const int BlockIndex = pPopup->m_Editor.m_vTocBlocks[TocIndex];
					if(BlockIndex < 0 || BlockIndex >= static_cast<int>(vBlocks.size()))
						continue;
					const SWikiBlock &Heading = vBlocks[BlockIndex];
					CUIRect Row;
					Body.HSplitTop(TocLineHeight, &Row, &Body);
					const float Indent = static_cast<float>(std::max(0, Heading.m_Level - 1)) * TipSize * 1.1f;
					CUIRect Text = Row;
					Text.x += Indent;
					Text.w = std::max(1.0f, Text.w - Indent);
					std::string Label;
					for(const SWikiSpan &Span : Heading.m_Spans)
						Label += Span.m_Text;
					const bool Hovered = Ctx.m_pUi->MouseHovered(&Row);
					s_aTocIds[TocIndex] = BlockIndex;
					if(Ctx.m_pUi->DoButtonLogic(&s_aTocIds[TocIndex], 0, &Row, BUTTONFLAG_LEFT))
						pPopup->m_TocTarget = BlockIndex;
					SLabelProperties LabelProps;
					LabelProps.m_MaxWidth = Text.w;
					LabelProps.m_EllipsisAtEnd = true;
					LabelProps.SetColor(Hovered ? Theme.m_Accent : Muted);
					Ctx.m_pUi->DoLabel(&Text, Label.c_str(), TipSize, TEXTALIGN_ML, LabelProps);
					if(Hovered)
					{
						CUIRect Underline = Text;
						Underline.h = std::max(1.0f, UiScale);
						Underline.y = Text.y + Text.h - Underline.h;
						Underline.w = std::min(Text.w, Ctx.m_pTextRender->TextWidth(TipSize, Label.c_str()));
						Underline.Draw(Theme.m_Accent.WithMultipliedAlpha(0.55f), IGraphics::CORNER_NONE, 0.0f);
					}
				}
				Body.HSplitTop(Gap * 0.5f, nullptr, &Body);
				// 目录本身也要进滚动区的内容高度，否则滚到顶时目录会被裁掉。
				CUIRect TocArea = Content;
				TocArea.y = TocTop;
				TocArea.h = std::max(0.0f, Body.y - TocTop);
				s_Scroll.AddRect(TocArea, false);
			}

			if(pPopup->m_Editor.m_Markdown.Blocks().empty())
			{
				CUIRect Empty;
				Body.HSplitTop(BodySize * 1.6f, &Empty, &Body);
				SLabelProperties Props;
				Props.m_MaxWidth = Empty.w;
				Props.SetColor(Muted);
				Ctx.m_pUi->DoLabel(&Empty, Localize("No content yet"), BodySize, TEXTALIGN_TL, Props);
			}
			else
			{
				static SWikiLinkContext s_Links;
				const std::function<void(const char *)> OnLink = [pPopup](const char *pTarget) {
					str_copy(pPopup->m_aPendingLink, pTarget != nullptr ? pTarget : "", sizeof(pPopup->m_aPendingLink));
				};
				s_Links.m_pOnClick = &OnLink;
				qm_wiki_markdown::RenderBlocks(Ctx, pPopup->m_Editor.m_Markdown.Blocks(), Body, Style, &s_Scroll, &s_Links, pPopup->m_TocTarget);
				if(!Ctx.m_pUi->RenderOnly())
					pPopup->m_TocTarget = -1;
			}
			s_Scroll.End();
			return CUi::POPUP_KEEP_OPEN;
		}

		// 编辑模式：整个正文区是一块固定高度的多行输入框，文字在其中纵向滚动。
		// 之前把输入框撑到整段文本高度再交给外层滚动区：滚动区不跟随光标，长文末尾
		// 打字时光标直接跑到可视区外，编辑者以为换行没生效。光标跟随与滚轮都收回输入框。
		const float Pad = ui_token::spacing::SM * UiScale;
		const float LineSpacing = BodySize * 0.55f;
		const float LineWidth = std::max(1.0f, Content.w - Pad * 2.0f);
		pPopup->m_EditInput.SetEmptyText(Localize("Write the guide here"));
		CUIRect EditRect;
		EditRect.x = Content.x + Pad;
		EditRect.y = Content.y + Pad;
		EditRect.w = LineWidth;
		EditRect.h = std::max(LineSpacing * 4.0f, Content.h - Pad * 2.0f);
		if(pPopup->m_Editor.m_ResetEditScroll)
		{
			pPopup->m_EditInput.SetScrollOffsetY(0.0f);
			pPopup->m_EditInput.SetScrollOffsetYChange(0.0f);
			pPopup->m_Editor.m_ResetEditScroll = false;
		}
		CUIRect EditSurface = EditRect;
		EditSurface.Margin(-Pad * 0.5f, &EditSurface);
		EditSurface.Draw(Foreground.WithMultipliedAlpha(0.05f), IGraphics::CORNER_ALL, ui_token::radius::BASE * UiScale);
		CUi::SEditBoxRenderOptions EditOptions;
		EditOptions.m_DrawBackground = false;
		EditOptions.m_ProcessInput = !pPopup->m_pUi->RenderOnly();
		// 编辑长文本时光标必须跟手：输入动效的弹簧会让光标越过真实位置，
		// 看起来像新字符插到了光标前面。这里只在本次绘制内关闭动效。
		const int SavedMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 0;
		if(pPopup->m_pUi->DoEditBoxMultiLine(&pPopup->m_EditInput, &EditRect, BodySize, LineSpacing, TEXTALIGN_TL, EditOptions))
			pPopup->m_Editor.SetDraft(pPopup->m_EditInput.GetString());
		g_Config.m_QmUiMotionLevel = SavedMotionLevel;
		return CUi::POPUP_KEEP_OPEN;
	}

	void StartWikiPopup(const IUiContext &Ctx, const SWikiDocument &Fallback, const char *pId, const char *pTitle, ESettingsSecondaryPanelKind Kind, bool IsCard)
	{
		gs_WikiPopupContext.m_pUi = Ctx.m_pUi;
		gs_WikiPopupContext.m_pTextRender = Ctx.m_pTextRender;
		gs_WikiPopupContext.m_pAnim = Ctx.m_pAnim;
		gs_WikiPopupContext.m_pTree = Ctx.m_pTree;
		gs_WikiPopupContext.m_pStorage = Ctx.m_pStorage;
		gs_WikiPopupContext.m_Theme = ResolveConfiguredSecondaryPanelTheme();
		gs_WikiPopupContext.m_UiScale = Ctx.m_UiScale;
		gs_WikiPopupContext.m_Kind = Kind;
		gs_WikiPopupContext.m_IsCard = IsCard;
		gs_WikiPopupContext.m_Fallback = Fallback;
		gs_WikiPopupContext.m_EditInput.Clear();
		gs_WikiPopupContext.m_ResetScroll = true;
		str_copy(gs_WikiPopupContext.m_aTitle, pTitle != nullptr ? pTitle : "");

		qm_wiki_store::Init(Ctx.m_pStorage);
		if(Ctx.m_pStorage != nullptr && Ctx.m_pUi != nullptr)
			qm_wiki_media::Init(Ctx.m_pStorage, Ctx.m_pUi->Graphics());
		gs_WikiPopupContext.m_Editor.Load(pId, Fallback, IsCard);

		const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*Ctx.m_pUi->Screen(), Kind, gs_WikiPopupContext.m_UiScale);
		// 要求来源每帧刷新（回调里刷新）：菜单失活后回调不再运行，弹层栈会把面板当孤儿收掉，
		// 不会留下看不见却继续屏蔽全局鼠标输入的面板。
		SPopupMenuProperties Props = ui_widget::SecondaryPanelProperties();
		Props.m_RequireSourceRefresh = true;
		Props.m_SourceFrame = Ctx.m_pUi->PopupSourceFrame();
		Ctx.m_pUi->DoPopupMenu(&gs_WikiPopupId, PanelRect.x, PanelRect.y, PanelRect.w, PanelRect.h, &gs_WikiPopupContext, RenderWikiPopup, Props);
	}
} // namespace

void OpenCardWikiPopup(const IUiContext &Ctx, const char *pStableId, const char *pTitle)
{
	const SCardWikiCopy *pCopy = FindCardWikiCopy(pStableId);
	if(pCopy == nullptr)
		return;
	gs_WikiPopupContext.m_apCardParagraphs[0] = pCopy->m_pWhat;
	gs_WikiPopupContext.m_apCardParagraphs[1] = pCopy->m_pHow;
	gs_WikiPopupContext.m_apCardParagraphs[2] = pCopy->m_pAdvice;
	// 标题也必须是可提取的 i18n source（Localizable 只做标记），
	// 否则运行时查不到译文，面板会直接显示英文标题。
	const char *apHeadings[3] = {Localizable("What this controls"), Localizable("How to use it"), Localizable("Good to know")};
	for(int Index = 0; Index < 3; ++Index)
		gs_WikiPopupContext.m_aCardSections[Index] = {apHeadings[Index], &gs_WikiPopupContext.m_apCardParagraphs[Index], 1};
	SWikiDocument Document = {pStableId, pTitle, gs_WikiPopupContext.m_aCardSections, 3};
	// 卡片说明较短，沿用资源库面板尺寸；标题直接用卡片自己已本地化的标题。
	StartWikiPopup(Ctx, Document, pStableId, pTitle, ESettingsSecondaryPanelKind::LIBRARY, true);
}

void OpenWikiDocumentPopup(const IUiContext &Ctx, const SWikiDocument &Document)
{
	// 长文档用公告面板的整屏高度，滚动更长但一屏能看更多。
	StartWikiPopup(Ctx, Document, Document.m_pId, nullptr, ESettingsSecondaryPanelKind::NEWS, false);
}
