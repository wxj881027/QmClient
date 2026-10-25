#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmIslandNotice.h>
#include <game/client/QmUi/QmIslandSurface.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/qm_markdown.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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
	const SUiTheme PanelTheme = ResolveConfiguredSecondaryPanelTheme();
	IUiContext Ctx = SettingsUiContext("qm_new_features_popup");
	Ctx.m_pTheme = &PanelTheme;
	CUiScopedSurfaceText PanelText(TextRender(), PanelTheme.m_Surface);
	const ColorRGBA PanelForeground = ResolveConfiguredTextColor(PanelText.Surface());
	const float UiScale = std::clamp(g_Config.m_QmUiScale / 100.0f, 0.5f, 2.0f);
	const float HeadlineSize = ui_token::font::HEADLINE * UiScale;
	const float BodySize = ui_token::font::BODY * UiScale;
	const float TipSize = ui_token::font::TIP * UiScale;
	const float Gap = ui_token::spacing::MD * UiScale;
	const float ButtonH = ui_token::secondary_panel::NEWS_ACTION_HEIGHT * UiScale;
	const float JumpButtonW = 110.0f * UiScale;

	const CUIRect Panel = Screen;
	static ui_widget::SSecondaryPanelLabel s_Title;
	static CButtonContainer s_HeaderClose;
	ui_widget::CSecondaryPanel PanelHeader(Ctx, Panel, !Ui()->RenderOnly(), ui_widget::ResolveSecondaryPanelMetrics(std::max(0.0f, Ui()->Screen()->w - 16.0f), ESettingsSecondaryPanelKind::NEWS, UiScale), {});
	if(PanelHeader.Header(s_Title, s_HeaderClose, Localize("New features")))
	{
		m_Popup = POPUP_NONE;
		return;
	}
	CUIRect Inner = PanelHeader.ContentRect();

	CUIRect SubTitleRow;
	Inner.HSplitTop(TipSize * 1.6f, &SubTitleRow, &Inner);
	TextRender()->TextColor(PanelForeground.WithMultipliedAlpha(0.65f));
	Ui()->DoLabel(&SubTitleRow, Localize("Feature guide and announcements"), TipSize, TEXTALIGN_ML);
	TextRender()->TextColor(PanelForeground);
	Inner.HSplitTop(Gap, nullptr, &Inner);

	CQmClient &QmClient = GameClient()->m_QmClient;
	// 仅在公告页面实际绘制时标记已读，收到网络更新本身不会确认阅读。
	QmClient.MarkQmNewsRead();
	if(m_QmNewFeaturesScrollReset && QmClient.HasDeveloperCredential())
		QmClient.QmNewsReloadDraft();
	// 底部：仅保留检查更新入口；关闭交给标题栏 X 与点击面板外。
	CUIRect ButtonRow;
	Inner.HSplitBottom(ButtonH, &Inner, &ButtonRow);
	Inner.HSplitBottom(Gap, &Inner, nullptr);
	if(QmClient.HasDeveloperCredential())
	{
		CUIRect DevRow, ReloadDraftRect, PublishRect, OpenFolderRect, DevStatus;
		Inner.HSplitBottom(ButtonH, &Inner, &DevRow);
		Inner.HSplitBottom(Gap, &Inner, nullptr);
		static ui_widget::SSettingsActionGroupState s_DevActionWidths;
		const char *apDevActions[] = {Localize("Reload"), Localize("Publish"), Localize("Open folder")};
		const auto DevActions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_DevActionWidths, DevRow, apDevActions, 3, ResolveSettingsContentMetrics(DevRow.w), ESettingsActionGroupWidth::CONTENT);
		ReloadDraftRect = DevActions.m_aButtons[0];
		PublishRect = DevActions.m_aButtons[1];
		OpenFolderRect = DevActions.m_aButtons[2];
		const float ActionsRight = OpenFolderRect.x + OpenFolderRect.w;
		DevStatus = {ActionsRight, DevRow.y, std::max(0.0f, DevRow.x + DevRow.w - ActionsRight), DevRow.h};
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
			TextRender()->TextColor(PanelForeground.WithMultipliedAlpha(0.65f));
			Ui()->DoLabel(&DevStatus, pDevStatus, TipSize, TEXTALIGN_ML);
			TextRender()->TextColor(PanelForeground);
		}
	}
	static ui_widget::SSettingsActionGroupState s_NewsActionWidths;
	const char *apNewsActions[] = {Localize("Check for updates")};
	const auto NewsActions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_NewsActionWidths, ButtonRow, apNewsActions, 1, ResolveSettingsContentMetrics(ButtonRow.w));
	static CButtonContainer s_CheckUpdatesButton;
	if(ui_widget::SecondaryButton(Ctx, &s_CheckUpdatesButton, Localize("Check for updates"), NewsActions.m_aButtons[0]))
	{
		ShowQmUpdatePopup();
		return;
	}

	// 条目区：内容超出时滚动；每次打开回到顶部。滚动条与速度沿用设置页策略，滑块用面板强调色。
	static CScrollRegion s_ScrollRegion;
	if(m_QmNewFeaturesScrollReset)
	{
		s_ScrollRegion.Reset();
		m_QmNewFeaturesScrollReset = false;
	}
	CScrollRegionParams ScrollParams = QmSettingsScrollRegionParams(1.0f);
	ScrollParams.m_pSliderThemeColor = &PanelTheme.m_Accent;
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
					DrawRoundedSurface(Ctx, Separator, PanelTheme.m_Border, ColorRGBA(), 0.0f);
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
				TextRender()->TextColor(PanelForeground.WithMultipliedAlpha(0.65f));
				Ui()->DoLabel(&Marker, aMarker, BodySize, TEXTALIGN_TL);
				TextRender()->TextColor(PanelForeground);
			}
			if(Block.m_Kind == qm_md::EBlockKind::QUOTE)
			{
				CUIRect Bar = TextRect;
				Bar.w = 3.0f;
				DrawRoundedSurface(Ctx, Bar, ui_token::color::ACCENT_PRIMARY_DIM, ColorRGBA(), 0.0f);
				TextRect.VSplitLeft(10.0f * UiScale, nullptr, &TextRect);
			}
			TextRender()->TextColor(Block.m_Kind == qm_md::EBlockKind::HEADING1 || Block.m_Kind == qm_md::EBlockKind::HEADING2 || Block.m_Kind == qm_md::EBlockKind::HEADING3 ? PanelForeground : PanelForeground.WithMultipliedAlpha(0.72f));
			Ui()->DoLabel(&TextRect, Text.c_str(), FontSize, TEXTALIGN_TL, {.m_MaxWidth = TextRect.w});
			TextRender()->TextColor(PanelForeground);
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
			DrawRoundedSurface(Ctx, Separator, PanelTheme.m_Border, ColorRGBA(), 0.0f);
		}

		CUIRect TitleRect = JumpRow;
		TextRender()->TextColor(PanelForeground);
		Ui()->DoLabel(&TitleRect, Localize(Entry.m_pName), HeadlineSize, TEXTALIGN_ML);
		TextRender()->TextColor(PanelForeground);

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
		TextRender()->TextColor(PanelForeground.WithMultipliedAlpha(0.72f));
		Ui()->DoLabel(&SummaryRow, Localize(Entry.m_pSummary), BodySize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(PanelForeground);

		EntryContent.HSplitTop(UsageLines * TipSize * 1.6f, &UsageRow, &EntryContent);
		EntryContent.HSplitTop(EntryLines * TipSize * 1.6f, &EntryRow, &EntryContent);
		TextRender()->TextColor(PanelForeground.WithMultipliedAlpha(0.65f));
		char aUsage[512];
		str_format(aUsage, sizeof(aUsage), Localize("Usage: %s"), Localize(Entry.m_pUsage));
		Ui()->DoLabel(&UsageRow, aUsage, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		Ui()->DoLabel(&EntryRow, aEntryLine, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(PanelForeground);
	}
	s_ScrollRegion.End();
}

void CMenus::RenderQmUpdatePopup(CUIRect Screen)
{
	auto &Update = GameClient()->m_TClient;
	const int State = static_cast<int>(Update.QmClientUpdateStatus());
	if(State != m_QmUpdatePopupState)
	{
		m_QmUpdatePopupState = State;
		log_info("qm-update", "popup_state=%d", State);
	}
	const SUiTheme PanelTheme = ResolveConfiguredSecondaryPanelTheme();
	IUiContext Ctx = SettingsUiContext("qm_update_popup");
	Ctx.m_pTheme = &PanelTheme;
	CUiScopedSurfaceText PanelText(TextRender(), PanelTheme.m_Surface);
	const float RequestedScale = std::clamp(g_Config.m_QmUiScale / 100.0f, 0.5f, 2.0f);
	const float ViewportWidth = std::max(0.0f, Ui()->Screen()->w - 16.0f);
	const CUIRect Panel = Screen;
	static ui_widget::SSecondaryPanelLabel s_Title, s_TitleCaption;
	static CButtonContainer s_HeaderClose;
	ui_widget::CSecondaryPanel PanelHeader(Ctx, Panel, !Ui()->RenderOnly(), ui_widget::ResolveSecondaryPanelMetrics(ViewportWidth, ESettingsSecondaryPanelKind::UPDATE, RequestedScale), {});
	// 版本与更新状态合并为标题右侧小字；正文只保留进度、说明与操作。
	char aTitleCaption[160];
	if(State == static_cast<int>(EQmClientUpdateStatus::CURRENT))
		str_format(aTitleCaption, sizeof(aTitleCaption), Localize("Current version: %s (up to date)"), CLIENT_RELEASE_VERSION);
	else if(State == static_cast<int>(EQmClientUpdateStatus::IDLE))
		str_format(aTitleCaption, sizeof(aTitleCaption), Localize("Current version: %s"), CLIENT_RELEASE_VERSION);
	else
		str_copy(aTitleCaption, Update.QmClientUpdateStatusText());
	if(PanelHeader.Header(s_Title, s_HeaderClose, Localize("Update"), &s_TitleCaption, aTitleCaption))
	{
		Update.m_UpdatePopupRequested = false;
		m_Popup = POPUP_NONE;
		return;
	}
	CUIRect Content = PanelHeader.ContentRect();
	if(Content.w <= 0.0f || Content.h <= 0.0f)
		return;
	const float Scale = ui_widget::ResolveUpdatePanelContentScale(Content, RequestedScale);
	const float Gap = std::min(ui_token::spacing::MD * Scale, ViewportWidth * 0.02f);
	const float BodySize = ui_token::font::BODY * Scale;
	CUIRect Row;
	CUIRect Buttons;
	// 底部单行操作区：主操作、次操作与版本发布页；关闭交给标题栏 X 与点击外部。
	Content.HSplitBottom(BodySize * ui_token::secondary_panel::UPDATE_ACTION_LINE_FACTOR, &Content, &Buttons);
	Content.HSplitBottom(Gap, &Content, nullptr);
	// 下载能力固定在操作区上方，说明可独立滚动，缺包提示不会被长说明挤掉。
#if defined(CONF_QMCLIENT_PORTABLE)
	constexpr bool QmUpdatePortableBuild = true;
#else
	constexpr bool QmUpdatePortableBuild = false;
#endif
	if(Update.QmClientUpdateStatus() == EQmClientUpdateStatus::AVAILABLE && !Update.CanDownloadQmClientUpdate())
	{
		const char *pPackage = QmUpdatePortableBuild ? Localize("No downloadable portable package for this version") :
							       Localize("No compatible Windows update package for this version");
		CUIRect Availability;
		Content.HSplitBottom(QmWrappedLineCount(TextRender(), BodySize, pPackage, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Content, &Availability);
		Ui()->DoLabel(&Availability, pPackage, BodySize, TEXTALIGN_TL, {.m_MaxWidth = Availability.w});
		Content.HSplitBottom(Gap, &Content, nullptr);
	}
	// 下载进度固定在说明滚动区外，长发布说明不会遮住下载结果。
	if(Update.IsUpdateDownloading() && Update.m_UpdateDownloadTasks.Get(qm_update::CDownloadTasks::PACKAGE))
	{
		const auto &Task = Update.m_UpdateDownloadTasks.Get(qm_update::CDownloadTasks::PACKAGE);
		char aProgress[128];
		const bool KnownSize = Task->Size() > 0;
		const int Percent = std::clamp(Task->Progress(), 0, 100);
		if(KnownSize)
			str_format(aProgress, sizeof(aProgress), "%.1f / %.1f MiB (%d%%)", Task->Current() / 1048576.0, Task->Size() / 1048576.0, Percent);
		else
			str_format(aProgress, sizeof(aProgress), Localize("Downloaded: %.1f MiB (total size unknown)"), std::max(0.0, Task->Current()) / 1048576.0);
		Content.HSplitTop(BodySize * 1.7f, &Row, &Content);
		Ui()->DoLabel(&Row, aProgress, BodySize, TEXTALIGN_ML, {.m_MaxWidth = Row.w});
		CUIRect Bar;
		Content.HSplitTop(std::max(4.0f, Gap), &Bar, &Content);
		DrawRoundedSurface(Ctx, Bar, PanelTheme.m_Border, ColorRGBA(), ui_token::radius::CARD);
		if(KnownSize)
			Bar.w *= Percent / 100.0f;
		else
		{
			// 无 Content-Length 时显示活动条，不伪造百分比或已完成状态。
			Bar.x += Bar.w * 0.7f * (0.5f + 0.5f * std::sin(Client()->LocalTime() * 3.0f));
			Bar.w *= 0.3f;
		}
		if(Bar.w > 0.0f)
			DrawRoundedSurface(Ctx, Bar, PanelTheme.m_Accent, ColorRGBA(), ui_token::radius::CARD);
		Content.HSplitTop(Gap, nullptr, &Content);
	}
	static CScrollRegion s_ScrollRegion;
	if(m_QmUpdateScrollReset)
	{
		s_ScrollRegion.Reset();
		m_QmUpdateScrollReset = false;
	}
	// 滚动条几何与滚轮速度沿用设置页外层策略，弹窗与设置页手感一致；
	// 滑块显式使用面板主题强调色，避免弹窗表面对比不足时回落成前景白。
	CScrollRegionParams ScrollParams = QmSettingsScrollRegionParams(1.0f);
	ScrollParams.m_pSliderThemeColor = &PanelTheme.m_Accent;
	vec2 ScrollOffset;
	s_ScrollRegion.Begin(&Content, &ScrollOffset, &ScrollParams);
	Content.x += ScrollOffset.x;
	Content.y += ScrollOffset.y;
	const auto Label = [&](const CUIRect &Rect, const char *pText, float FontSize, int Alignment) {
		if(s_ScrollRegion.AddRect(Rect))
			Ui()->DoLabel(&Rect, pText, FontSize, Alignment, {.m_MaxWidth = Rect.w});
	};
	if(Update.m_UpdateReady)
	{
		const char *pReady = Localize("Download verified. Exit and install to replace this version; your settings are preserved. Later installs when you exit.");
		Content.HSplitTop(QmWrappedLineCount(TextRender(), BodySize, pReady, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Row, &Content);
		Label(Row, pReady, BodySize, TEXTALIGN_TL);
	}
	const auto *pSource = Update.IsUpdateChecking() ? Update.m_UpdateMetadataRequest.Source() : Update.m_UpdateDownloadAttempt.Current();
	if(pSource && (Update.IsUpdateChecking() || Update.IsUpdateDownloading()))
	{
		const char *pSourceText = pSource->m_Prefix.empty() ? "GitHub" : pSource->m_Prefix.c_str();
		Content.HSplitTop(QmWrappedLineCount(TextRender(), BodySize, pSourceText, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Row, &Content);
		Label(Row, pSourceText, BodySize, TEXTALIGN_TL);
	}
	if(Update.m_UpdateCheckFailed && Update.m_aUpdateError[0])
	{
		Content.HSplitTop(QmWrappedLineCount(TextRender(), BodySize, Update.m_aUpdateError, std::max(1.0f, Content.w)) * BodySize * 1.6f + Gap, &Row, &Content);
		Label(Row, Update.m_aUpdateError, BodySize, TEXTALIGN_TL);
	}
	if(Update.m_FetchedQmClientUpdateInfo)
	{
		// 按元数据/字体/宽度版本缓存段落和测量；每帧只绘制滚动区域内的段落。
		struct SNoteParagraph
		{
			std::string m_Text;
			std::string m_Marker;
			qm_md::EBlockKind m_Kind = qm_md::EBlockKind::PARAGRAPH;
			float m_FontScale = 1;
			float m_Height = 0;
			float m_Indent = 0;
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
				Paragraph.m_Kind = Block.m_Kind;
				if(Block.m_Kind == qm_md::EBlockKind::BULLET)
					Paragraph.m_Marker = "•";
				else if(Block.m_Kind == qm_md::EBlockKind::NUMBERED)
					Paragraph.m_Marker = std::to_string(Block.m_Number) + ".";
				for(const auto &Span : Block.m_vSpans)
					Paragraph.m_Text += Span.m_Text;
				// 发布说明中的链接与设置标记仅作文本，不执行第三方动作。
				if(Block.m_Kind == qm_md::EBlockKind::SETTINGS_BUTTON)
					Paragraph.m_Text = Block.m_SettingsLabel;
				if(Block.m_Kind == qm_md::EBlockKind::HEADING1 || Block.m_Kind == qm_md::EBlockKind::HEADING2 || Block.m_Kind == qm_md::EBlockKind::HEADING3)
					Paragraph.m_FontScale = 1.15f;
				if(Block.m_Kind == qm_md::EBlockKind::QUOTE)
					Paragraph.m_FontScale = ui_token::font::TIP / ui_token::font::BODY;
				if(!Paragraph.m_Text.empty() || Block.m_Kind == qm_md::EBlockKind::SEPARATOR)
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
			// 发布者已提供标题时直接使用，不再叠加同义的“发布说明”标题。
			const auto FirstKind = s_vNotes.front().m_Kind;
			if(FirstKind != qm_md::EBlockKind::HEADING1 && FirstKind != qm_md::EBlockKind::HEADING2 && FirstKind != qm_md::EBlockKind::HEADING3)
			{
				Content.HSplitTop(BodySize * 1.8f, &Row, &Content);
				Label(Row, Localize("Release notes"), BodySize, TEXTALIGN_ML);
			}
			if(s_NotesWidth != Content.w || s_NotesSize != BodySize || s_FontRevision != TextRender()->GlyphAtlasRevision())
			{
				for(auto &Paragraph : s_vNotes)
				{
					const float Size = BodySize * Paragraph.m_FontScale;
					Paragraph.m_Indent = Paragraph.m_Marker.empty() ? 0.0f : std::min(Content.w * 0.25f, TextRender()->TextWidth(Size, Paragraph.m_Marker.c_str()) + Gap);
					Paragraph.m_Height = Paragraph.m_Kind == qm_md::EBlockKind::SEPARATOR ? 1.0f + Gap * 0.5f :
														QmWrappedLineCount(TextRender(), Size, Paragraph.m_Text.c_str(), std::max(1.0f, Content.w - Paragraph.m_Indent)) * Size * 1.6f + Gap * 0.5f;
				}
				s_NotesWidth = Content.w;
				s_NotesSize = BodySize;
				s_FontRevision = TextRender()->GlyphAtlasRevision();
			}
			for(const auto &Paragraph : s_vNotes)
			{
				Content.HSplitTop(Paragraph.m_Height, &Row, &Content);
				if(Paragraph.m_Kind == qm_md::EBlockKind::SEPARATOR)
				{
					if(s_ScrollRegion.AddRect(Row))
					{
						Row.h = 1.0f;
						DrawRoundedSurface(Ctx, Row, PanelTheme.m_Border, ColorRGBA(), 0.0f);
					}
					continue;
				}
				if(Paragraph.m_Indent > 0.0f)
				{
					CUIRect Marker;
					Row.VSplitLeft(Paragraph.m_Indent, &Marker, &Row);
					Label(Marker, Paragraph.m_Marker.c_str(), BodySize * Paragraph.m_FontScale, TEXTALIGN_TL);
				}
				const ColorRGBA TextColor = TextRender()->GetTextColor();
				if(Paragraph.m_Kind == qm_md::EBlockKind::QUOTE)
					TextRender()->TextColor(TextColor.WithMultipliedAlpha(0.65f));
				Label(Row, Paragraph.m_Text.c_str(), BodySize * Paragraph.m_FontScale, TEXTALIGN_TL);
				TextRender()->TextColor(TextColor);
			}
		}
	}
	s_ScrollRegion.End();
	// 按可用宽度均分按钮；长译文由共享按钮的文本适配处理。
	CUIRect Action, Recheck, Manual;
	static CButtonContainer s_Action, s_Recheck, s_Manual;
	const bool Busy = Update.IsUpdateChecking() || Update.IsUpdateDownloading();
	// 缺包时仍可重新检查发布者后补的附件；下载能力由生产包选择器阻断。
	const EQmClientUpdateAction UpdateAction = ResolveQmClientUpdateAction(Busy, Update.m_UpdateReady, Update.m_FetchedQmClientUpdateInfo, Update.m_UpdateCheckFailed, Update.CanDownloadQmClientUpdate());
	const char *pAction = UpdateAction == EQmClientUpdateAction::CANCEL   ? Localize("Cancel") :
			      UpdateAction == EQmClientUpdateAction::INSTALL  ? Localize("Exit and install") :
			      UpdateAction == EQmClientUpdateAction::DOWNLOAD ? Localize("Download update") :
			      UpdateAction == EQmClientUpdateAction::RETRY    ? Localize("Retry") :
										Localize("Check for updates");
	const bool ShowRecheck = UpdateAction == EQmClientUpdateAction::DOWNLOAD;
	const char *pRecheck = Update.m_UpdateReady ? Localize("Discard update") : ShowRecheck ? Localize("Check again") :
												 Localize("New features");
	static ui_widget::SSettingsActionGroupState s_UpdateActionWidths;
	const char *apActions[] = {pAction, pRecheck, Localize("Release page")};
	const auto ActionLayout = ui_widget::PrepareSettingsActionGroup(*Ui(), s_UpdateActionWidths, Buttons, apActions, 3, ResolveSettingsContentMetrics(Buttons.w));
	Action = ActionLayout.m_aButtons[0];
	Recheck = ActionLayout.m_aButtons[1];
	Manual = ActionLayout.m_aButtons[2];
	if(ui_widget::PrimaryButton(Ctx, &s_Action, pAction, Action, Update.m_UpdateInstallerStarted))
	{
		if(UpdateAction == EQmClientUpdateAction::CANCEL)
		{
			Update.CancelQmClientUpdate();
		}
		else if(UpdateAction == EQmClientUpdateAction::INSTALL)
			Client()->Quit();
		else if(UpdateAction == EQmClientUpdateAction::DOWNLOAD)
			Update.RequestQmClientUpdateDownload();
		else
			Update.RequestQmClientUpdateCheck();
	}
	if(ui_widget::SecondaryButton(Ctx, &s_Recheck, pRecheck, Recheck, Update.m_UpdateInstallerStarted))
	{
		if(Update.m_UpdateReady)
			Update.CancelQmClientUpdate();
		else if(ShowRecheck)
			Update.RequestQmClientUpdateCheck();
		else
			ShowQmNewFeaturesPopup();
	}
	if(ui_widget::SecondaryButton(Ctx, &s_Manual, Localize("Release page"), Manual))
		Client()->ViewLink(qm_update::MANUAL_DOWNLOAD_URL);
}
