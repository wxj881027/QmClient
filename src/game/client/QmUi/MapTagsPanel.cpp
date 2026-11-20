#include "MapTagsPanel.h"

#include "QmServerInfoGeometry.h"
#include "UiButtons.h"
#include "UiForms.h"
#include "UiSurface.h"
#include "UiSurfaceText.h"
#include "UiTag.h"

#include <base/color.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/map_tags.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

void CQmMapTagsPanel::Render(CGameClient *pGameClient, CUIRect View)
{
	CUi *pUi = pGameClient->Ui();
	const auto &Server = pGameClient->Client()->ServerInfo();
	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	if(LocalId < 0 || pGameClient->Client()->State() != IClient::STATE_ONLINE)
		return;
	const char *pPlayer = pGameClient->m_aClients[LocalId].m_aName;
	const char *pCommunity = Server.m_aCommunityId;
	const char *pMapName = Server.m_aMap;
	const std::string Context = std::string(pCommunity) + "\n" + pMapName + "\n" + pPlayer;
	if(Context != m_Context)
	{
		m_Context = Context;
		m_Input.Clear();
		m_bComposing = false;
		m_vUploadQueue.clear();
		m_TagButtons.clear();
		m_LocalTagChipButtons.clear();
		m_LocalTagRemoveButtons.clear();
		// 换图/换服后旧的展开态与拖动状态都不再指向任何东西，必须一起清掉：
		// 否则新图上会莫名其妙先展开一颗同名标签，或残留一条拖到一半的幽灵。
		m_ExpandedTag.clear();
		m_VisibleTag.clear();
		m_ExpandProgress = 0.0f;
		m_LocalTagDrag.Reset();
	}
	auto &Tags = pGameClient->m_QmClient.m_MapTags;
	Tags.SetContext(pCommunity, pMapName, pPlayer);
	Tags.Update(pGameClient->Http(), time_get() / (double)time_freq());
	const auto &Snapshot = Tags.Snapshot();
	const IUiContext Ctx = ui_widget::ControlContext(pUi);
	const float Scale = QmServerInfoGeometry::FitScale(View, 280.0f, 160.0f);
	if(Scale <= 0.0f)
		return;
	m_LocalTags.Load(pGameClient->Storage());
	const std::vector<std::string> MyTags = m_LocalTags.Get(pCommunity, pMapName);
	// 队列化提交：一次点上传会把还没上过服务器的本地标签逐个提交（写入通道同一时刻
	// 只允许一个请求），因此这里每帧最多取队首一个。
	if(!m_vUploadQueue.empty() && Tags.Ready() && Snapshot.m_Eligible && !Tags.Busy())
	{
		const std::string &Front = m_vUploadQueue.front();
		// 排队后标签可能已被删除：先确认仍在本地；提交被拒时不丢队首，
		// 下一帧仍可重试，避免静默跳过。
		if(!m_LocalTags.Has(pCommunity, pMapName, Front.c_str()))
			m_vUploadQueue.erase(m_vUploadQueue.begin());
		else if(!QmMapTags::ValidTag(Front.c_str()))
			m_vUploadQueue.erase(m_vUploadQueue.begin());
		else if(Tags.Submit(pGameClient->Http(), Front.c_str()))
			m_vUploadQueue.erase(m_vUploadQueue.begin());
	}
	pUi->ClipEnable(&View);
	SLabelProperties LabelProps;
	LabelProps.m_MinimumFontSize = 0.1f;
	// 此面板的控件随可用空间缩放，按钮文字与命中区域使用相同的尺寸。
	auto DoButton = [&](CButtonContainer *pButton, const char *pText, CUIRect Rect, bool Disabled, bool Primary = false, float FontSize = ui_token::font::BODY) {
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Role = Primary ? EUiButtonRole::PRIMARY : EUiButtonRole::SECONDARY;
		// 外观**不按 Disabled 淡化**：小按钮（「添加」）在输入为空时只是暂时点不动，
		// 再乘 0.65 alpha 会让它在浅色背板上几乎看不见，玩家读不出这里有个确认动作。
		// 能不能点由下面的 DoButtonLogic 决定（Disabled 时直接返回 false）。
		Options.m_Enabled = true;
		// 胶囊端头：圆角随按钮高度走，缩放后仍是完整胶囊。
		Options.m_Radius = Rect.h * 0.5f;
		const ColorRGBA Fill = ui_widget::DrawButtonSurface(Ctx, pButton, Rect, Options);
		CUiScopedSurfaceText SurfaceText(pGameClient->TextRender(), Fill);
		if(Disabled)
			pGameClient->TextRender()->TextColor(pGameClient->TextRender()->GetTextColor().WithMultipliedAlpha(0.65f));
		pUi->DoLabel(&Rect, pText, FontSize, TEXTALIGN_MC, LabelProps);
		if(Disabled && !pUi->RenderOnly() && pUi->CheckActiveItem(pButton))
			pUi->SetActiveItem(nullptr);
		const bool Clicked = !Disabled && !pUi->RenderOnly() && pUi->DoButtonLogic(pButton, 0, &Rect, BUTTONFLAG_LEFT) != 0;
		if(Clicked)
			pUi->SetKeyboardFocus(pButton);
		return Clicked;
	};
	// 一颗可点的标签胶囊：外观完全由 ui_tag 决定（固定几何、按文本派生的固定色、
	// 文字跟随文本颜色模式），这里只补命中与悬浮提亮。
	// pColorKey 是取色用的词：云端标签显示的文本带「×N +1」后缀，颜色必须按**词本身**
	// 派生，否则赞同数一变整颗胶囊就换色（同一个词在本地/云端也不一致）。
	auto DoTagChip = [&](CButtonContainer *pButton, const CUIRect &Slot, const char *pText, float Alpha, bool Enabled, const char *pColorKey = nullptr) {
		const float Width = ui_tag::MeasureWidth(pGameClient->TextRender(), pText, Scale);
		const CUIRect Chip = ui_tag::FitRect(Slot, Width, Scale);
		const bool Clicked = Enabled && !pUi->RenderOnly() && pUi->DoButtonLogic(pButton, 0, &Chip, BUTTONFLAG_LEFT) != 0;
		ui_tag::DrawInRect(pUi, Chip, pText, ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), pColorKey != nullptr ? pColorKey : pText, Alpha, Scale);
		if(Enabled && pUi->MouseHovered(&Chip))
			DrawRoundedSurface(pUi, Chip, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Alpha), ColorRGBA(), Chip.h * 0.5f);
		return Clicked;
	};

	const float RowHeight = ui_token::server_info::TAGS_PRESET_ROW_HEIGHT * Scale;
	const float Gap = ui_token::server_info::TAGS_LIST_GAP * Scale;
	// 三行的标签列共用一个宽度：令牌基线之外再按当前语言的实际文字放宽。
	// 「本地标签 / 云端标签 / 标签示例」都按实测宽度对齐，换语言也不会挤到胶囊上。
	const char *pPresetLabel = Localize("Tag examples:");
	const char *pLocalLabel = Localize("Local tags:");
	const char *pCloudLabel = Localize("Cloud tags:");
	const float LabelWidth = std::max({ui_token::server_info::TAGS_PRESET_LABEL_WIDTH * Scale,
		pGameClient->TextRender()->TextWidth(ui_token::font::SMALL * Scale, pPresetLabel) + Gap,
		pGameClient->TextRender()->TextWidth(ui_token::font::SMALL * Scale, pLocalLabel) + Gap,
		pGameClient->TextRender()->TextWidth(ui_token::font::SMALL * Scale, pCloudLabel) + Gap});

	// 状态文本只保留「确实需要玩家知道」的几条：资格判定失败、没通关。
	// 「加载中」由列表空态负责，不在这里重复一遍；「新标签需要审核」属于上传动作的
	// 规则，已经移到上传按钮的悬浮提示里——输入框这边加的是**只属于自己**的标签，
	// 不存在审核。
	const char *pStatus = Tags.Status();
	if(!pStatus[0])
	{
		if(Snapshot.m_EligibilityUnavailable)
			pStatus = Localize("Completion records unavailable. Try refreshing.");
		else if(Tags.Ready() && !Snapshot.m_Eligible)
			pStatus = Localize("Finish this map before submitting tags.");
	}
	const bool ShowStatus = pStatus[0] != '\0';
	const float StatusHeight = ShowStatus ? ui_token::font::SMALL * 1.4f * Scale : 0.0f;

	CUIRect Header, UploadSlot, Upload, RefreshSlot, Refresh;
	View.HSplitTop(ui_token::server_info::TAGS_HEADER_HEIGHT * Scale, &Header, &View);
	// 右上角两个图标按钮：上传（把我的标签提交审核）+ 刷新已审核列表。
	Header.VSplitRight(Header.h, &Header, &RefreshSlot);
	Header.VSplitRight(Gap, &Header, nullptr);
	Header.VSplitRight(Header.h, &Header, &UploadSlot);
	const float IconSize = ui_token::server_info::TAGS_ICON_SIZE * Scale;
	RefreshSlot.VMargin((RefreshSlot.h - IconSize) * 0.5f, &Refresh);
	Refresh.h = IconSize;
	UploadSlot.VMargin((UploadSlot.h - IconSize) * 0.5f, &Upload);
	Upload.h = IconSize;
	pUi->DoLabel(&Header, Localize("Map tags"), ui_token::font::TITLE, TEXTALIGN_ML, LabelProps);

	auto IsListed = [&](const char *pTag) {
		for(const auto &Tag : Snapshot.m_vTags)
			if(str_comp_nocase(Tag.m_Text.c_str(), pTag) == 0)
				return true;
		return false;
	};
	bool HasPendingUpload = false;
	for(const auto &Tag : MyTags)
	{
		if(!IsListed(Tag.c_str()))
		{
			HasPendingUpload = true;
			break;
		}
	}
	const bool UploadEnabled = Tags.Ready() && Snapshot.m_Eligible && HasPendingUpload && !Tags.Busy();
	// 上传按钮恒为「可用外观」：它只在这一刻没活可干（全在服务器上 / 还没通关 / 正在提交），
	// 不是「这个控件坏了」。画成禁用会让图标变暗再压一条红斜线，玩家连它是什么都看不出，
	// 提示也只能靠 tooltip 讲——所以这里让它是正常亮度的图标，点击时机由 UploadEnabled 决定。
	const bool UploadClicked = pUi->DoButton_QmIcon(&m_UploadButton, EQmIcon::ARROW_UP_RIGHT_FROM_SQUARE, FontIcons::FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Upload, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true);
	if(UploadClicked && UploadEnabled)
	{
		m_vUploadQueue.clear();
		for(const auto &Tag : MyTags)
		{
			if(!IsListed(Tag.c_str()))
				m_vUploadQueue.push_back(Tag);
		}
	}
	// 上传按钮的说明固定是「这个按钮做什么」那一句，**任何状态都先给这一句**；
	// 当前不能点（加载中 / 没通关 / 都已在服务器上）时再补一句原因，而不是拿原因
	// 顶掉说明——玩家点上去首先要知道这颗图标是干什么的。
	char aUploadTip[512];
	str_copy(aUploadTip, Localize("Upload local tags for review. Tags stay local until approved."), sizeof(aUploadTip));
	if(!Tags.Ready())
	{
		str_append(aUploadTip, " ");
		str_append(aUploadTip, Localize("Loading map tags…"));
	}
	else if(!Snapshot.m_Eligible)
	{
		str_append(aUploadTip, " ");
		str_append(aUploadTip, Localize("Finish this map before submitting tags."));
	}
	else if(!HasPendingUpload)
	{
		str_append(aUploadTip, " ");
		str_append(aUploadTip, Localize("All my tags are already on the server."));
	}
	// 走矩形悬浮：按钮禁用时 DoButtonLogic 不登记热项，普通 DoToolTip 会因为「热项不是它」
	// 整条提示都不显示——这正是上传图标悬浮没有说明的原因。
	pGameClient->m_Tooltips.DoToolTipForRect(&m_UploadButton, &Upload, aUploadTip);
	if(pUi->DoButton_QmIcon(&m_RefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &Refresh, BUTTONFLAG_LEFT))
		Tags.Refresh();
	pGameClient->m_Tooltips.DoToolTipForRect(&m_RefreshButton, &Refresh, Localize("Refresh"));

	View.HSplitTop(ui_token::server_info::TAGS_HEADER_GAP * Scale, nullptr, &View);

	// 版式：**本地标签是第一行**（紧跟标题，不再被底部大块留白挤到下面），
	// 「标签示例：」钉在面板底边，中间整块留给云端标签（可换行，标签多了往下长）。
	CUIRect PresetRow, StatusRow, CloudArea;
	View.HSplitBottom(RowHeight, &View, &PresetRow);
	if(ShowStatus)
	{
		View.HSplitBottom(StatusHeight, &View, &StatusRow);
		View.HSplitBottom(Gap, &View, nullptr);
	}
	CUIRect LocalRow;
	View.HSplitTop(RowHeight, &LocalRow, &CloudArea);
	CloudArea.HSplitTop(Gap, nullptr, &CloudArea);

	// 云端标签：同样走 ui_tag（固定胶囊、按标签文本派生的固定色），点的语义
	// 仍是「赞同」——点击后服务器会给这个词 +1。空态就写在这一行里，
	// 不再单独占一整块列表区（以前那块空区把下面几行全推到面板底部）。
	char aLabel[128];
	CUIRect CloudLabelArea, CloudChips;
	CloudArea.VSplitLeft(LabelWidth, &CloudLabelArea, &CloudChips);
	CUIRect CloudLabel = CloudLabelArea;
	CloudLabel.h = std::min(CloudLabel.h, RowHeight);
	pUi->DoLabel(&CloudLabel, pCloudLabel, ui_token::font::SMALL, TEXTALIGN_ML, LabelProps);
	if(Snapshot.m_vTags.empty())
	{
		CUIRect EmptyText = CloudChips;
		EmptyText.h = std::min(EmptyText.h, RowHeight);
		pUi->DoLabel(&EmptyText, Tags.Ready() ? Localize("None right now.") : Localize("Loading map tags…"), ui_token::font::SMALL, TEXTALIGN_ML, LabelProps);
	}
	else
	{
		const float ChipHeight = ui_tag::HEIGHT * Scale;
		const float RowStep = ChipHeight + Gap;
		const float Right = CloudChips.x + CloudChips.w;
		float X = CloudChips.x;
		float Y = CloudChips.y;
		for(const auto &Tag : Snapshot.m_vTags)
		{
			str_format(aLabel, sizeof(aLabel), "%s ×%d%s", Tag.m_Text.c_str(), Tag.m_Count, Tag.m_Voted ? "" : "  +1");
			const float Width = ui_tag::MeasureWidth(pGameClient->TextRender(), aLabel, Scale);
			// 单个标签比整行还宽就直接跳过：胶囊不缩、不截字，宁可这条看不见。
			if(Width > CloudChips.w)
				continue;
			if(X + Width > Right)
			{
				X = CloudChips.x;
				Y += RowStep;
			}
			if(Y + ChipHeight > CloudChips.y + CloudChips.h + 0.5f)
				break;
			const CUIRect Cell{X, Y, Width, ChipHeight};
			auto &Button = m_TagButtons[Tag.m_Id];
			const bool Enabled = Snapshot.m_Eligible && !Tag.m_Voted && !Tags.Busy();
			if(DoTagChip(&Button, Cell, aLabel, Enabled ? 1.0f : 0.6f, Enabled, Tag.m_Text.c_str()))
				Tags.Submit(pGameClient->Http(), Tag.m_Text.c_str());
			X += Width + Gap;
		}
	}

	// 标签示例：四个服务端内置的常用词，点一下把词填进输入框（等于「照着敲了」）。
	// 这一行钉在面板底边，上面留出的空间都归云端标签。
	CUIRect PresetLabel, Presets;
	PresetRow.VSplitLeft(LabelWidth, &PresetLabel, &Presets);
	pUi->DoLabel(&PresetLabel, pPresetLabel, ui_token::font::SMALL, TEXTALIGN_ML, LabelProps);
	static constexpr const char *PRESETS[] = {"hj", "fly", "Gores", "Hook"};
	bool PresetClicked = false;
	for(size_t PresetIndex = 0; PresetIndex < std::size(PRESETS); ++PresetIndex)
	{
		const float Width = ui_tag::MeasureWidth(pGameClient->TextRender(), PRESETS[PresetIndex], Scale);
		if(Presets.w < Width)
			break;
		CUIRect Slot;
		Presets.VSplitLeft(Width, &Slot, &Presets);
		Presets.VSplitLeft(Gap * 0.5f, nullptr, &Presets);
		if(DoTagChip(&m_aPresetButtons[PresetIndex], Slot, PRESETS[PresetIndex], 1.0f, true))
		{
			m_Input.Set(PRESETS[PresetIndex]);
			m_bComposing = true;
			// 点示例等于「开始输入」：焦点直接交给输入框，不用再点一次。
			pUi->SetActiveItem(&m_Input);
			m_Input.SelectAll();
			PresetClicked = true;
		}
	}

	// 本地标签：我自己给这张图打的词（只存本地、不上传，所以行名就叫「本地标签」）。
	// 每颗胶囊可点、可拖：**点击才在它右侧展开 ×**（× 的宽度与透明度从 0 生长出来，
	// 所以未点击时一行都是紧凑的纯标签），按住拖动则改顺序。
	CUIRect CurrentRow = LocalRow;
	// VSplitLeft 会把 CurrentRow 裁成剩余区，取消判定的「点在这一行里」要用整行。
	const CUIRect CurrentRowFrame = CurrentRow;
	CUIRect CurrentLabel, CurrentChips;
	CurrentRow.VSplitLeft(LabelWidth, &CurrentLabel, &CurrentChips);
	pUi->DoLabel(&CurrentLabel, pLocalLabel, ui_token::font::SMALL, TEXTALIGN_ML, LabelProps);
	// 行内所有零件同高：胶囊、× 与 + 都吃这一档（ui_tag::HEIGHT），输入框同样。
	const float ItemHeight = ui_tag::HEIGHT * Scale;
	const float ActionSize = std::min(ui_token::server_info::TAGS_ACTION_SIZE * Scale, ItemHeight);
	const float ItemGap = 3.0f * Scale;
	// 输入框约 5 字宽（标签上限 10 字，超出由输入框自己横向滚动），右侧跟「添加」。
	const float ComposerInputWidth = 5.0f * ui_token::font::SMALL * Scale + 22.0f * Scale;
	const float ComposerAddWidth = ui_token::server_info::TAGS_SUBMIT_WIDTH * Scale;
	const float ComposerWidth = m_bComposing ? std::min(CurrentChips.w, ComposerInputWidth + ItemGap * 2.0f + ComposerAddWidth) : 0.0f;

	// 展开进度：线速度推进（约 70ms 长完）。收起方向要等进度归零才清 m_VisibleTag，
	// 否则 × 会在动画半途整颗消失，看起来像闪一下。
	if(!pUi->RenderOnly())
	{
		const double Now = pGameClient->Client()->LocalTime();
		const float DeltaTime = (float)std::clamp(Now - m_ExpandAnimationTime, 0.0, 0.1);
		m_ExpandAnimationTime = Now;
		const float Target = m_ExpandedTag.empty() ? 0.0f : 1.0f;
		const float Step = 14.0f * DeltaTime;
		m_ExpandProgress = m_ExpandProgress < Target ? std::min(Target, m_ExpandProgress + Step) : std::max(Target, m_ExpandProgress - Step);
		if(Target <= 0.0f && m_ExpandProgress <= 0.0f)
			m_VisibleTag.clear();
	}
	// × 的当前宽度：只有正在展开/收起的那一颗吃它，其余标签不占位。
	const float RemoveWidth = ActionSize * std::clamp(m_ExpandProgress, 0.0f, 1.0f);

	// 解布局：每颗 = [标签胶囊][×（展开时）]，行内不换行，放不下就截断。
	// 先算完再交互：拖动排序与落点判定都要整行的胶囊矩形，边画边算拿不到后面的位置。
	constexpr int MaxLocalChips = SQmPieOptionPoolLayout::MAX_CHIPS;
	CUIRect aChipRects[MaxLocalChips] = {};
	CUIRect aRemoveRects[MaxLocalChips] = {};
	int aChipTags[MaxLocalChips] = {};
	int ChipCount = 0;
	float CursorX = CurrentChips.x;
	const float RowRight = CurrentChips.x + CurrentChips.w;
	for(int TagIndex = 0; TagIndex < (int)MyTags.size() && ChipCount < MaxLocalChips; ++TagIndex)
	{
		const bool Expanding = m_VisibleTag == MyTags[TagIndex];
		const float Width = ui_tag::MeasureWidth(pGameClient->TextRender(), MyTags[TagIndex].c_str(), Scale);
		const float Trail = Expanding ? RemoveWidth + ItemGap : 0.0f;
		// 行末恒占一个动作位：输入框（展开时）或 +（收起时），标签再多也不占掉它。
		const float Reserve = m_bComposing ? ComposerWidth + ItemGap : ActionSize + ItemGap;
		if(CursorX + Width + ItemGap + Trail + Reserve > RowRight + 0.5f)
			break;
		aChipRects[ChipCount] = {CursorX, CurrentChips.y + (CurrentChips.h - ItemHeight) * 0.5f, Width, ItemHeight};
		aChipTags[ChipCount] = TagIndex;
		CursorX += Width + ItemGap;
		// × 在胶囊**右侧**：锚点贴着胶囊右缘，宽度按进度向右生长，
		// 所以展开时是胶囊本身不动、右邻标签整体右移。
		if(Trail > 0.0f)
		{
			aRemoveRects[ChipCount] = {CursorX, CurrentChips.y + (CurrentChips.h - ActionSize) * 0.5f, RemoveWidth, ActionSize};
			CursorX += Trail;
		}
		ChipCount++;
	}
	// 输入框的落点：最后一颗标签之后（不是整行剩余区，避免标签少时输入框飘到行中间）。
	const CUIRect ComposerFrame = {CursorX, CurrentChips.y, std::min(std::max(0.0f, RowRight - CursorX), ComposerWidth), CurrentChips.h};

	int ClickedChip = -1;
	int ClickedRemove = -1;
	for(int Slot = 0; Slot < ChipCount; ++Slot)
	{
		const int TagIndex = aChipTags[Slot];
		const std::string &Tag = MyTags[TagIndex];
		const bool DraggingThis = m_LocalTagDrag.Active() && m_LocalTagDrag.m_Index == TagIndex;
		// 标签本体：按住拖动时 DoButtonLogic 仍要跑（它维持 active item 的抓取），
		// 但返回值在拖动中一律忽略——否则松手会被算成一次「点击展开」。
		auto &ChipButton = m_LocalTagChipButtons[Tag];
		const int ChipClicked = pUi->DoButtonLogic(&ChipButton, 0, &aChipRects[Slot], BUTTONFLAG_LEFT);
		ui_tag::DrawInRect(pUi, aChipRects[Slot], Tag.c_str(), ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), Tag.c_str(), DraggingThis ? 0.85f : 1.0f, Scale);
		if(DraggingThis)
			DrawRoundedSurface(pUi, aChipRects[Slot], ui_token::color::COLOR_TRANSPARENT, ui_token::feedback::ACTIVE_BORDER, aChipRects[Slot].h * 0.5f, ui_token::feedback::ICON_BORDER_WIDTH);
		else if(pUi->MouseHovered(&aChipRects[Slot]))
			DrawRoundedSurface(pUi, aChipRects[Slot], ui_token::color::PREVIEW_SURFACE, ColorRGBA(), aChipRects[Slot].h * 0.5f);
		if(ChipClicked && !m_LocalTagDrag.m_Dragging)
			ClickedChip = TagIndex;
		if(!pUi->RenderOnly())
			pGameClient->m_Tooltips.DoToolTipForRect(&ChipButton, &aChipRects[Slot], Localize("Click to manage this tag, drag to reorder"));

		// ×：走**全站图标按钮件**（`DoButton_QmIcon`）——圆底、悬浮反馈、图标墨迹居中
		// 全由 token 与 `DrawQmIcon` 决定，不再自绘圆底。自绘那版取的是通用 TINT_PART，
		// 与 + 按钮的 ICON role 不是同一色阶，两颗并排一眼能看出不是一套件。
		// 「从左边逐段长出来」只靠外层裁切：按钮本身始终是完整尺寸，被 RemoveSlot 裁出多少画多少。
		const CUIRect &RemoveSlot = aRemoveRects[Slot];
		if(RemoveSlot.w <= 0.5f)
			continue;
		auto &RemoveButton = m_LocalTagRemoveButtons[Tag];
		CUIRect RemoveRect = {RemoveSlot.x, RemoveSlot.y, ActionSize, ActionSize};
		pUi->ClipEnable(&RemoveSlot);
		const int RemoveClicked = pUi->DoButton_QmIcon(&RemoveButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &RemoveRect, BUTTONFLAG_LEFT);
		pUi->ClipDisable();
		// × 只在基本长成后才响应点击：动画途中点到的是「正在变的形状」，不该触发删除。
		if(RemoveSlot.w >= ActionSize * 0.6f && RemoveClicked && !m_LocalTagDrag.m_Dragging)
			ClickedRemove = TagIndex;
		if(!pUi->RenderOnly())
			pGameClient->m_Tooltips.DoToolTipForRect(&RemoveButton, &RemoveRect, Localize("Delete this tag"));
	}

	// 拖动排序：起手记录槽位，指针离开一档阈值才算拖动，松手按落点插入。
	if(!pUi->RenderOnly())
	{
		int HoverSlot = -1;
		for(int Slot = 0; Slot < ChipCount; ++Slot)
		{
			if(pUi->MouseHovered(&aChipRects[Slot]))
				HoverSlot = Slot;
		}
		if(!m_LocalTagDrag.Active() && pUi->MouseButtonClicked(0) && HoverSlot >= 0)
		{
			m_LocalTagDrag.m_Pool = 0;
			m_LocalTagDrag.m_Index = aChipTags[HoverSlot];
			m_LocalTagDrag.m_Option = aChipTags[HoverSlot];
			m_LocalTagDrag.m_Start = pUi->MousePos();
			m_LocalTagDrag.m_GhostRect = aChipRects[HoverSlot];
			m_LocalTagDrag.m_GrabOffset = pUi->MousePos() - vec2(aChipRects[HoverSlot].x, aChipRects[HoverSlot].y);
			m_LocalTagDrag.m_Dragging = false;
		}
		if(m_LocalTagDrag.Active() && pUi->MouseButton(0))
		{
			if(!m_LocalTagDrag.m_Dragging && distance(pUi->MousePos(), m_LocalTagDrag.m_Start) > ui_token::spacing::XS)
				m_LocalTagDrag.m_Dragging = true;
		}
		else if(m_LocalTagDrag.Active())
		{
			if(m_LocalTagDrag.m_Dragging && m_LocalTagDrag.m_Index >= 0)
			{
				// 落点在「移除被拖项之后」的序列里，和 CLocalTags::Move 的最终索引同义。
				int SkipSlot = -1;
				for(int Slot = 0; Slot < ChipCount; ++Slot)
				{
					if(aChipTags[Slot] == m_LocalTagDrag.m_Index)
						SkipSlot = Slot;
				}
				const int TargetIndex = QmResolveChipDropSlot(aChipRects, ChipCount, SkipSlot, pUi->MousePos());
				m_LocalTags.Move(pGameClient->Storage(), pCommunity, pMapName, m_LocalTagDrag.m_Index, TargetIndex);
			}
			else if(m_LocalTagDrag.m_Index >= 0)
			{
				// 原地单击（没拖动）= 展开/收起这颗标签的 ×。
				ClickedChip = m_LocalTagDrag.m_Index;
			}
			m_LocalTagDrag.Reset();
		}
	}

	if(ClickedRemove >= 0)
	{
		const std::string &Removed = MyTags[ClickedRemove];
		if(m_LocalTags.Remove(pGameClient->Storage(), pCommunity, pMapName, Removed.c_str()))
		{
			// 本地删除保存成功后，再取消同名标签的提交和排队上传。
			Tags.CancelSubmit(Removed.c_str());
			m_vUploadQueue.erase(std::remove(m_vUploadQueue.begin(), m_vUploadQueue.end(), Removed), m_vUploadQueue.end());
			m_ExpandedTag.clear();
			m_VisibleTag.clear();
			m_ExpandProgress = 0.0f;
		}
	}
	else if(ClickedChip >= 0 && ClickedChip < (int)MyTags.size())
	{
		const std::string &Tag = MyTags[ClickedChip];
		if(m_ExpandedTag == Tag)
		{
			m_ExpandedTag.clear();
		}
		else
		{
			m_ExpandedTag = Tag;
			m_VisibleTag = Tag;
			// 换一颗展开时从 0 长：否则新 × 会按上一颗的进度直接满宽出现。
			m_ExpandProgress = 0.0f;
		}
	}

	// 行末的 +：输入框收起时它就是「加一颗本地标签」的入口，展开时被输入框取代。
	// 它和标签同高同档，视觉上是同一排里的最后一颗。
	if(!m_bComposing && RowRight - CursorX >= ActionSize)
	{
		CUIRect AddRect{CursorX, CurrentChips.y + (CurrentChips.h - ActionSize) * 0.5f, ActionSize, ActionSize};
		if(pUi->DoButton_QmIcon(&m_AddTagButton, EQmIcon::PLUS, FontIcons::FONT_ICON_PLUS, 0, &AddRect, BUTTONFLAG_LEFT))
		{
			m_Input.Clear();
			m_bComposing = true;
			// 点 + 就直接能打字：把焦点交给输入框，同一帧就画出聚焦态。
			pUi->SetActiveItem(&m_Input);
		}
		if(!pUi->RenderOnly())
			pGameClient->m_Tooltips.DoToolTipForRect(&m_AddTagButton, &AddRect, Localize("Add a tag"));
	}

	// 点别处（不在任何标签上、也不在输入框里）收起 ×：与「点开才展开」配套，
	// 不留一颗永远挂着的删除钮。
	if(!m_ExpandedTag.empty() && !pUi->RenderOnly() && pUi->MouseButtonClicked(0) && !pUi->MouseHovered(&ComposerFrame))
	{
		bool PressedChip = false;
		for(int Slot = 0; Slot < ChipCount; ++Slot)
		{
			if(pUi->MouseHovered(&aChipRects[Slot]) || pUi->MouseHovered(&aRemoveRects[Slot]))
				PressedChip = true;
		}
		if(!PressedChip)
			m_ExpandedTag.clear();
	}

	// 拖动中的幽灵胶囊与插入指示线：画在最后，压在标签行之上。
	if(!pUi->RenderOnly() && m_LocalTagDrag.m_Dragging && m_LocalTagDrag.m_Index >= 0 && m_LocalTagDrag.m_Index < (int)MyTags.size())
	{
		int SkipSlot = -1;
		for(int Slot = 0; Slot < ChipCount; ++Slot)
		{
			if(aChipTags[Slot] == m_LocalTagDrag.m_Index)
				SkipSlot = Slot;
		}
		const int TargetIndex = QmResolveChipDropSlot(aChipRects, ChipCount, SkipSlot, pUi->MousePos());
		// 指示线：落在某颗之前就画在它左缘，落点在末尾就画在最后一颗非拖项的右缘。
		int Seen = 0;
		float MarkerX = CurrentChips.x;
		bool MarkerFound = false;
		float LastRight = CurrentChips.x;
		for(int Slot = 0; Slot < ChipCount; ++Slot)
		{
			if(aChipTags[Slot] == m_LocalTagDrag.m_Index)
				continue;
			if(!MarkerFound && Seen == TargetIndex)
			{
				MarkerX = aChipRects[Slot].x - ItemGap * 0.5f;
				MarkerFound = true;
			}
			LastRight = aChipRects[Slot].x + aChipRects[Slot].w;
			Seen++;
		}
		if(!MarkerFound)
			MarkerX = LastRight + ItemGap * 0.5f;
		DrawRoundedSurface(pUi, CUIRect{MarkerX - 1.0f, CurrentChips.y + (CurrentChips.h - ItemHeight) * 0.5f, 2.0f, ItemHeight},
			ui_token::feedback::ACTIVE_BORDER, ColorRGBA(), 1.0f);

		CUIRect Ghost = m_LocalTagDrag.m_GhostRect;
		Ghost.x = pUi->MousePos().x - m_LocalTagDrag.m_GrabOffset.x;
		Ghost.y = pUi->MousePos().y - m_LocalTagDrag.m_GrabOffset.y;
		CUIRect GhostShadow = Ghost;
		GhostShadow.x += ui_token::elevation::SHADOW_X_LOW;
		GhostShadow.y += ui_token::elevation::SHADOW_Y_MED;
		DrawRoundedSurface(pUi, GhostShadow, ui_token::color::SURFACE_SHADOW, ui_token::color::COLOR_TRANSPARENT, ui_tag::RADIUS * Scale);
		ui_tag::DrawInRect(pUi, Ghost, MyTags[m_LocalTagDrag.m_Index].c_str(), ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), MyTags[m_LocalTagDrag.m_Index].c_str(), 0.95f, Scale);
	}

	bool bCommitTag = false;
	if(m_bComposing)
	{
		CUIRect Composer = ComposerFrame;
		CUIRect InputRect, AddButtonRect;
		Composer.VSplitLeft(std::min(ComposerInputWidth, Composer.w), &InputRect, &Composer);
		Composer.VSplitLeft(ItemGap * 2.0f, nullptr, &Composer);
		Composer.VSplitLeft(std::min(ComposerAddWidth, Composer.w), &AddButtonRect, &Composer);
		// 与胶囊等高：点 + 只是把一颗 16 高的按钮换成一颗 16 高的输入框，行高不动。
		InputRect.HMargin((InputRect.h - ItemHeight) * 0.5f, &InputRect);
		AddButtonRect.HMargin((AddButtonRect.h - ItemHeight) * 0.5f, &AddButtonRect);
		ui_widget::SInputFieldOptions InputOptions;
		InputOptions.m_pPlaceholder = Localize("New tag");
		InputOptions.m_FontSize = ui_tag::FONT_SIZE * Scale;
		const ui_widget::SInputFieldResult InputResult = ui_widget::InputField(Ctx, &m_Input, InputRect, InputOptions);
		bCommitTag = InputResult.m_Submitted;
		if(DoButton(&m_ComposerAddButton, Localize("Add"), AddButtonRect, !QmMapTags::ValidTag(m_Input.GetString()), true, ui_tag::FONT_SIZE * Scale))
			bCommitTag = true;
		if(!bCommitTag && !PresetClicked)
		{
			// 点到别处（输入框失焦，或落在底栏之外）就等于放弃这次添加：清空并收起，
			// 把 + 还回去。点示例不算放弃——那是在继续填这个词；指针还在输入框/「添加」
			// 这块里时也不算：按「添加」本身就会让输入框失焦，不能把它当成放弃。
			const bool PointerInComposer = pUi->MouseHovered(&ComposerFrame);
			const bool ClickedOutside = pUi->MouseButton(0) && !PointerInComposer && !pUi->MouseHovered(&CurrentRowFrame) && !pUi->MouseHovered(&PresetRow);
			if(!PointerInComposer && (InputResult.m_Deactivated || ClickedOutside))
			{
				m_Input.Clear();
				m_bComposing = false;
			}
		}
	}
	if(bCommitTag)
	{
		// 只加到「我的标签」：本地文件、不上传，也不需要通关资格。
		if(m_LocalTags.Add(pGameClient->Storage(), pCommunity, pMapName, m_Input.GetString()))
		{
			m_Input.Clear();
			m_bComposing = false;
		}
	}

	if(ShowStatus)
		pUi->DoLabel(&StatusRow, pStatus, ui_token::font::SMALL, TEXTALIGN_TL, LabelProps);
	pUi->ClipDisable();
}
