#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsContentColumns.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_mouse_diagram.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	struct SBindKeyTile
	{
		int m_Key;
		const char *m_pLabel;
		const char *m_pImage;
		float m_Units = 1.0f;
		const char *m_pContext = nullptr;
		bool m_Spacer = false;
		// 纵向跨几行（小键盘的 + 和 Enter 各占两行）；横向跨列直接用 m_Units。
		int m_SpanRows = 1;
		// 方向键这类字形走图标绘制路径（按字形包围盒居中），不再当普通文字画：
		// 文字居中按字体的 advance 宽度算，箭头字形自带偏侧留白，视觉重心会偏。
		int m_IconId = -1;
		const char *m_pIconGlyph = nullptr;
	};

	// 绑定撤销栈：每次破坏性写回前记录该槽位的整串命令。捕获换键会同时改旧键（解绑）
	// 和新键（写入），所以一条记录最多带两个槽位。栈深 8，够回退最近几次手滑。
	constexpr int BIND_UNDO_DEPTH = 8;
	struct SBindUndoEntry
	{
		int m_Count = 0;
		int m_aKeys[2] = {};
		int m_aModifierMasks[2] = {};
		std::string m_aBefore[2];
	};

	void PushBindUndo(std::deque<SBindUndoEntry> &vUndo, SBindUndoEntry Entry)
	{
		vUndo.push_back(std::move(Entry));
		while((int)vUndo.size() > BIND_UNDO_DEPTH)
			vUndo.pop_front();
	}

	void RecordBindUndo(std::deque<SBindUndoEntry> &vUndo, const CBinds &Binds, const CBindSlot &Slot)
	{
		SBindUndoEntry Entry;
		Entry.m_Count = 1;
		Entry.m_aKeys[0] = Slot.m_Key;
		Entry.m_aModifierMasks[0] = Slot.m_ModifierMask;
		Entry.m_aBefore[0] = Binds.Get(Slot.m_Key, Slot.m_ModifierMask);
		PushBindUndo(vUndo, std::move(Entry));
	}

	void RecordBindUndoTwo(std::deque<SBindUndoEntry> &vUndo, const CBinds &Binds, const CBindSlot &First, const CBindSlot &Second)
	{
		SBindUndoEntry Entry;
		Entry.m_Count = 2;
		Entry.m_aKeys[0] = First.m_Key;
		Entry.m_aModifierMasks[0] = First.m_ModifierMask;
		Entry.m_aBefore[0] = Binds.Get(First.m_Key, First.m_ModifierMask);
		Entry.m_aKeys[1] = Second.m_Key;
		Entry.m_aModifierMasks[1] = Second.m_ModifierMask;
		Entry.m_aBefore[1] = Binds.Get(Second.m_Key, Second.m_ModifierMask);
		PushBindUndo(vUndo, std::move(Entry));
	}

	// 撤销一次：把记录的槽位整串还原，后写回的覆盖先写回的。
	bool ApplyBindUndo(std::deque<SBindUndoEntry> &vUndo, CBinds &Binds)
	{
		if(vUndo.empty())
			return false;
		const SBindUndoEntry Entry = vUndo.back();
		vUndo.pop_back();
		for(int Index = 0; Index < Entry.m_Count; ++Index)
			Binds.Bind(Entry.m_aKeys[Index], Entry.m_aBefore[Index].c_str(), false, Entry.m_aModifierMasks[Index]);
		return true;
	}

	struct SBindEditorUiState
	{
		struct SActionControls
		{
			CButtonContainer m_Remove;
			// 整行点击：进入这一条命令的就地编辑。
			CButtonContainer m_Row;
			char m_LabelId = 0;
		};

		CBindSlot m_Selected = CBindSlot(KEY_A, KeyModifier::NONE);
		int m_ModifierMask = KeyModifier::NONE;
		bool m_HasSelected = true;
		bool m_CapturePending = false;
		// 选中由用户明确指定（点击某颗键、键位捕获、行跳转）后置位：悬停不再改变选中，
		// 直到左键松开且指针离开键盘区。鼠标按下瞬间也会置位，见键面渲染。
		bool m_KeySelectionHeld = false;
		// 左键在键面上按下期间的一次性冻结：按下置位，抬起就解开并恢复悬停跟随；
		// 真正的点击随后由 DoButtonLogic 重新置 m_KeySelectionHeld，两条语义不混用。
		bool m_KeyPressFrozen = false;
		// 行跳转要求的一次性键位揭示：FocusBindKey 置位，键面画到那颗键时消费。
		// 不复用 m_KeySelectionHeld——鼠标离开键面时那个标记会被清掉。
		bool m_RevealSelectedKey = false;
		CUIRect m_KeyboardRect{};
		int m_KeyButtonCount = 0;
		std::array<CButtonContainer, 128> m_aKeyButtons{};
		std::array<CButtonContainer, 4> m_aModifierButtons{};
		std::deque<SActionControls> m_vActionControls;
		// 就地编辑已绑定动作：m_EditActionIndex 是正在编辑的行，-1 表示列表处于只读态。
		// m_EditActionSlot 记住编辑开始时选中的键位，写回永远用这个槽位，换选中键也不会写错。
		int m_EditActionIndex = -1;
		CBindSlot m_EditActionSlot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		// 编辑行的屏幕矩形：点在这一块外面就把正在编辑的内容写回并关闭编辑。
		CUIRect m_EditActionRect{};
		bool m_EditActionRectValid = false;
		CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> m_EditActionInput;
		CButtonContainer m_EditActionCommit;
		CButtonContainer m_EditActionCancel;
		// 撤销栈：最近 8 次写回前的内容，撤销按钮一次回退一条。删除不再走确认气泡，
		// 点了就删、删了能撤——状态机少一套，"取消却也删了"这类路径就不存在了。
		std::deque<SBindUndoEntry> m_vBindUndo;
		CButtonContainer m_UndoButton;
		CButtonContainer m_CaptureButton;
		CButtonContainer m_ClearCaptureButton;
		std::string m_Error;
		// 中性状态提示（如"已删除，可撤销"）：和错误分开，避免把成功操作画成报错。
		std::string m_Status;
	};

	SBindEditorUiState &BindEditorState()
	{
		static SBindEditorUiState s_State;
		return s_State;
	}

	// 强调色统一取主题里配置的「界面强调色」：键面选中描边、已绑定下划线、行左侧标记、
	// 编辑行描边共用这一个来源，用户改强调色时整页一起变。
	// 固定蓝 token 不会随主题变，之前那种写死颜色的做法已经去掉。
	ColorRGBA BindEditorAccent(CUi *pUi)
	{
		return pUi->QmControlTheme().m_Accent.WithAlpha(1.0f);
	}

	int ModifierBit(const int Modifier)
	{
		return 1 << Modifier;
	}

	void SetBindEditorError(SBindEditorUiState &State, const char *pMessage)
	{
		State.m_Error = pMessage != nullptr ? pMessage : "";
	}

	// 写回成功后的中性提示：文案只用已有翻译键（删除命令 + 撤销），不新增英文条目。
	std::string BindEditorDeletedStatus()
	{
		char aStatus[128];
		str_format(aStatus, sizeof(aStatus), "%s · %s", Localize("Delete command"), Localize("Undo"));
		return aStatus;
	}

	void SetBindEditorDeletedStatus(SBindEditorUiState &State)
	{
		State.m_Status = BindEditorDeletedStatus();
	}

	// 键面二级揭示：卡片级 RequestReveal 只能把整张键盘卡拉进视口，窗口很矮时
	// 目标键仍在折叠线以下。渲染 bind 卡期间把 deck 的滚动区记在这里，DrawKey
	// 就能把具体那颗键再滚一次；渲染结束立刻清空，搜索页等其它入口复用这张卡时
	// 不会拿别的页面的滚动区去滚。
	CScrollRegion *g_pBindDeckScrollRegion = nullptr;
}

void CMenus::FocusBindKey(int Key, int Modifier)
{
	if(Key <= 0 || Key >= KEY_LAST)
		return;
	// 只改选择，不碰绑定本身：键盘卡片下一帧就会把这个键画成选中态。
	SBindEditorUiState &State = BindEditorState();
	State.m_Selected = CBindSlot(Key, Modifier);
	State.m_HasSelected = true;
	State.m_KeySelectionHeld = true;
	State.m_Error.clear();
	// 键盘卡片在卡片列表里可能已经滚出视口，跳转后把它拉回来；
	// 稳定 ID 与工坊/注册表一致，改名或换布局都不用同步改这里。
	m_SettingsCardDeck.RequestReveal("qm:bind_editor");
	// 卡片进视口之后还要把这一颗键滚到折叠线以上，键面绘制时消费。
	State.m_RevealSelectedKey = true;
}

void CMenus::RenderSettingsQmClientBindDeck(CUIRect MainView, const bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext CardCtx = SettingsUiContext("settings_controls_bind", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}

	static CScrollRegion s_ScrollRegion;
	static qm_card_order::CModel s_PrewarmOrderModel;
	static CSettingsCardDeck s_PrewarmDeck;
	static bool s_PrewarmOrderInitialized = false;
	if(ReadOnly && !s_PrewarmOrderInitialized)
	{
		s_PrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_PrewarmOrderInitialized = true;
	}

	qm_card_catalog::SQmCardBuildContext Context;
	Context.m_pMenus = this;
	Context.m_ReadOnly = ReadOnly;
	Context.m_Page = Page;
	Context.m_Metrics = Metrics;
	Context.m_LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	Context.m_UiContext = CardCtx;
	const uint64_t CardLayoutRevision = str_quickhash("settings-controls-bind") ^ GameClient()->m_Binds.Revision();
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [Context](std::vector<SSettingsCardDefinition> &vCards) {
		SSettingsCardDefinition Definition;
		if(qm_card_catalog::BuildBindCard(Context, Definition))
			vCards.push_back(std::move(Definition));
		if(qm_card_catalog::BuildBindOverviewCard(Context, Definition))
			vCards.push_back(std::move(Definition));
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	qm_card_order::CModel &OrderModel = Ui()->MeasurementOnly() ? s_PrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_PrewarmDeck : m_SettingsCardDeck;
	g_pBindDeckScrollRegion = ReadOnly ? nullptr : &s_ScrollRegion;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "controls", DefinitionsRevision, BuildDefinitions, OrderModel, Ui()->MeasurementOnly() ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	g_pBindDeckScrollRegion = nullptr;
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientBindCard(CUIRect &Content, const bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	SBindEditorUiState &State = BindEditorState();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Content.w);
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float BodySize = Metrics.m_BodySize;

	const bool Stacked = Content.w < 760.0f;
	const float Inset = LineSpacing * 0.75f;
	const float LeftWidth = std::min(Content.w * 0.72f, Content.w - 220.0f) - 2.0f * Inset;
	CUIRect Columns = Content;
	Columns.VMargin(Inset, &Columns);
	static SSettingsContentColumnsState s_Columns;
	const auto RenderKeyboard = [&](CUIRect &Left) {
		CSettingsContentLayoutFlow Flow(Left);
		const CUIRect KeyboardStart = Left;
		int KeyRow = 0;
		// 键面唯一网格：键盘排、鼠标排和修饰键共用同一套单元宽、行高与间隙，
		// 同一块键面里所有键因此同高同宽，长键只按 m_Units 拉宽。
		// 22.5 单元：主键区 15 + 间隔 .25 + 导航 3 + 间隔 .25 + 小键盘 4，
		// 六行总和完全一致，最右边缘因此落在同一条竖线上。
		constexpr float KeyGridColumns = 22.5f;
		// 修饰键占 2.5 单元：和普通键同一套几何，宽度只为容纳 Shift 这类整词。
		constexpr float ModifierUnits = 2.5f;
		const float KeyUnit = std::max(1.0f, Left.w / KeyGridColumns);
		// 键帽铺满槽位之后统一用窄缝：缝隙只留 1px 级别的呼吸，不再出现缝比键帽宽。
		const float KeyGap = std::max(2.0f, KeyUnit * 0.10f);
		const float KeyRowHeight = LineHeight * 1.25f;
		// 键与空白走同一套宽度公式，行尾右边缘才不会因为"空白不少减一个缝"而参差。
		const auto KeyTileWidth = [KeyUnit, KeyGap](const float Units) { return std::max(1.0f, KeyUnit * Units - KeyGap); };
		// 按键名沿用键帽标识，避免翻译成普通词义。
		static constexpr std::array<const char *, 4> s_apModifierLabels = {"Ctrl", "Alt", "Shift", "Win"};
		static constexpr std::array<int, 4> s_aModifierIds = {KeyModifier::CTRL, KeyModifier::ALT, KeyModifier::SHIFT, KeyModifier::GUI};
		CUIRect ModifierRow;
		if(Flow.Control("bind-modifiers", KeyRowHeight, ModifierRow))
		{
			for(size_t Index = 0; Index < s_apModifierLabels.size(); ++Index)
			{
				const CUIRect Button{ModifierRow.x + (float)Index * KeyUnit * ModifierUnits, ModifierRow.y, KeyTileWidth(ModifierUnits), ModifierRow.h};
				const bool Checked = (State.m_ModifierMask & ModifierBit(s_aModifierIds[Index])) != 0;
				ui_widget::SButtonSurfaceOptions Options;
				// 修饰键与普通键共用同一套几何，只靠选中填充色区分开关状态。
				Options.m_Selected = Checked;
				const ColorRGBA Fill = ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), &State.m_aModifierButtons[Index], Button, Options);
				CUiScopedSurfaceText SurfaceText(TextRender(), Fill);
				Ui()->DoLabel(&Button, s_apModifierLabels[Index], std::min(BodySize, Button.h * CUi::ms_FontmodHeight), TEXTALIGN_MC);
				if(!ReadOnly && Ui()->DoButtonLogic(&State.m_aModifierButtons[Index], Checked, &Button, BUTTONFLAG_LEFT))
				{
					State.m_ModifierMask ^= ModifierBit(s_aModifierIds[Index]);
					if(State.m_HasSelected)
						State.m_Selected.m_ModifierMask = State.m_ModifierMask;
					State.m_Error.clear();
				}
			}
		}
		Flow.Space(LineSpacing);
		if(!Flow.Declaring())
			State.m_KeyButtonCount = 0;
		const bool KeyCaptureActive = GameClient()->m_KeyBinder.IsActive();
		// 抬起即解除"按下期间冻结"：按下到松手之间悬停不改变选中，松手后立刻恢复悬停跟随。
		// 真正落在某颗键上的点击会在下面 DoButtonLogic 返回时重新置位，保留用户点选的那颗键。
		if(!ReadOnly && State.m_KeyPressFrozen && !Ui()->MouseButton(0))
		{
			State.m_KeyPressFrozen = false;
			State.m_KeySelectionHeld = false;
		}
		// 松手且指针在键盘区外才解除冻结。鼠标按着不放（拖出键盘区再拖回来）时也保持冻结，
		// 否则拖动中途悬停会重新接管选中，和"按下到松手之间悬停不改变选中"相矛盾。
		if(!ReadOnly && !Ui()->MouseButton(0) && !Ui()->MouseInside(&State.m_KeyboardRect))
			State.m_KeySelectionHeld = false;

		// 键面文字最小字号：键宽不够时一直缩到大约原字号的六成再省略号。
		// Enter / Home / Down / Right 这些名字比单个字母长，只缩到 TIP 会被截成
		// Ent / Ho / Do / Rig，玩家认不出是哪个键，所以下限要低于 TIP。
		const float MinKeyLabelSize = ui_token::font::TIP * 0.62f * Metrics.m_UiScale;
		// 左键在键面上按下就冻结悬停跟随：DoButtonLogic 只在抬起时返回 1，若等它标记，
		// 按住不放把鼠标拖到数字键盘区或别的键上时，悬停会把选中抢走或跳走。
		// 冻结建立在"按下的那颗键"上，所以在键盘空白处、修饰键上按下不会顺手冻住悬停。
		// 鼠标图和键帽共用这一段：两种键面是同一套按下语义。
		const auto NoteZonePress = [this, &State, ReadOnly](const bool Hovered) {
			if(!ReadOnly && Hovered && Ui()->MouseButtonClicked(0) != 0)
			{
				State.m_KeySelectionHeld = true;
				State.m_KeyPressFrozen = true;
			}
		};
		// 键面通用状态层：选中/悬停描边、已绑定下划线、冲突点与点击写回。
		// 键盘键帽和鼠标图共用这一份实现，两边的选中语义、描边宽度和冲突标记不会再各写一套。
		const auto DrawZoneState = [this, &State, ReadOnly](const CBindSlot &Slot, const CUIRect &Rect, const CUIRect &Plate, const float Radius, const int Corners, const bool Hovered, const bool Selected, const float DotInset, const int BoundStyle, const CButtonContainer *pButton) -> int {
			// 选中：沿表面补一圈强调色描边（不按命中矩形画，避免描边离表面一圈）。
			if(Selected)
				DrawRoundedSurface(Ui(), Plate, ui_token::color::COLOR_TRANSPARENT, BindEditorAccent(Ui()), Radius, ui_token::feedback::ICON_BORDER_WIDTH, Corners);
			// 悬停但未选中：白色高亮描边（和其它按钮悬停同源），让「鼠标在这」和「选中了」分得开。
			else if(Hovered)
				DrawRoundedSurface(Ui(), Plate, ui_token::color::COLOR_TRANSPARENT, ui_token::feedback::ACTIVE_BORDER, Radius, ui_token::feedback::ICON_BORDER_WIDTH, Corners);
			// 已绑定标记：键帽贴着底边一条 2px 强调色下划线；鼠标区域改成居中的一小截短划，
			// 贴在圆角底边上会从圆弧两侧戳出去，整条又太抢眼。两种都用主题强调色。
			if(GameClient()->m_Binds.Get(Slot)[0] != '\0')
			{
				if(BoundStyle == 0)
				{
					const float Thickness = 2.0f * ui_token::feedback::ICON_BORDER_WIDTH;
					const float UnderlineInset = std::min(Radius, Plate.w * 0.2f);
					const CUIRect Underline{Plate.x + UnderlineInset, Plate.y + Plate.h - Thickness, std::max(1.0f, Plate.w - 2.0f * UnderlineInset), Thickness};
					DrawRoundedSurface(Ui(), Underline, Ui()->ScaleControlAlpha(BindEditorAccent(Ui())), ui_token::color::COLOR_TRANSPARENT, Thickness * 0.5f);
				}
				else if(BoundStyle == 1)
				{
					// 鼠标区域：居中的一截强调色短划，离底边留一档，不压住板上文字。
					const float Thickness = std::max(1.0f, 1.5f * ui_token::feedback::ICON_BORDER_WIDTH);
					const float UnderlineWidth = std::min(Plate.w * 0.5f, std::max(Thickness, Plate.h * 0.9f));
					const CUIRect Underline{Plate.x + (Plate.w - UnderlineWidth) * 0.5f, Plate.y + Plate.h - Thickness - std::max(1.5f, Plate.h * 0.06f), UnderlineWidth, Thickness};
					DrawRoundedSurface(Ui(), Underline, Ui()->ScaleControlAlpha(BindEditorAccent(Ui())).WithMultipliedAlpha(0.85f), ui_token::color::COLOR_TRANSPARENT, Thickness * 0.5f);
				}
			}
			if(pButton == nullptr)
				return 0;
			return ReadOnly ? 0 : Ui()->DoButtonLogic(pButton, Selected ? 1 : 0, &Rect, BUTTONFLAG_LEFT);
		};
		const auto DrawKey = [this, &State, ReadOnly, BodySize, MinKeyLabelSize, &NoteZonePress, &DrawZoneState, UiScale = Metrics.m_UiScale](const SBindKeyTile &Tile, const CUIRect &Rect) {
			if(Tile.m_Spacer)
				return;
			if(State.m_KeyButtonCount >= (int)State.m_aKeyButtons.size())
				return;
			const CBindSlot Slot(Tile.m_Key, State.m_ModifierMask);
			// 悬停判定用纯几何、且不看趟次：这张卡会走「交互趟 + 背景出图趟」，
			// 最终出图那一趟 RenderOnly() 为真，任何 ReadOnly/MouseHovered 条件都会把
			// 边框高亮挡掉——之前键面看起来「没有悬浮」就是这个原因。
			const bool Hovered = Ui()->MouseInside(&Rect);
			// 悬停只负责视觉反馈，绝不写选中：选中是点击结果，只有点了别的键才换。
			// 早期版本用悬停跟随选中，结果鼠标一移开就"选中没了、右边的已绑定功能也换人"。
			NoteZonePress(Hovered);
			const bool Selected = State.m_HasSelected && State.m_Selected == Slot;
			// 行跳转的二级揭示：先登记这颗键（登记即请求滚动到它），再照常绘制。
			if(Selected && State.m_RevealSelectedKey && !ReadOnly && g_pBindDeckScrollRegion != nullptr)
			{
				State.m_RevealSelectedKey = false;
				g_pBindDeckScrollRegion->AddRect(Rect, true);
			}

			// 键帽底板自己画：圆角矩形直接铺满槽位（左右各内缩 1px），不再拿贴图当底板。
			// 贴图是固定比例的方键帽，放进长键槽位只能缩成一小块白方块，蓝条黄点就像
			// 飘在空中；自绘底板之后键帽本体和槽位同宽同高，长键也是完整的一整条。
			const float PlateInset = std::max(1.0f, std::floor(UiScale * 0.5f));
			const CUIRect Plate{Rect.x + PlateInset, Rect.y + PlateInset, std::max(1.0f, Rect.w - 2.0f * PlateInset), std::max(1.0f, Rect.h - 2.0f * PlateInset)};
			const ColorRGBA PlateFill = Selected ? ui_token::color::TINT_CONTROL_SELECTED : Hovered ? ui_token::color::TINT_CONTROL_HOVER :
														  ui_token::color::SURFACE_ELEVATED;
			DrawRoundedSurface(Ui(), Plate, PlateFill, ui_token::color::SURFACE_SHADOW.WithAlpha(0.45f), ui_token::radius::BASE, ui_token::feedback::ICON_BORDER_WIDTH * 0.5f);
			// 键名/图标居中：文字按可用宽度自动缩字号，方向键用箭头字符，避免 Left/Rig 被截断。
			// 命中判定仍然用整个格子 Rect，边缘也点得中。
			CUIRect LabelRect = Plate;
			LabelRect.VMargin(std::max(2.0f, Plate.w * 0.05f), &LabelRect);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(1.0f, LabelRect.w);
			Props.m_MinimumFontSize = MinKeyLabelSize;
			Props.m_EllipsisAtEnd = true;
			// 非空表示这颗键的名字被截断了，需要悬浮提示补全名。
			const char *pTooltipText = nullptr;
			if(Tile.m_IconId >= 0)
			{
				// 图标路径：由图标绘制按字形包围盒居中，不手填像素偏移。
				const float IconSize = std::min(BodySize * 1.15f, Plate.h * 0.78f);
				Ui()->DoLabel_QmIcon(&LabelRect, (EQmIcon)Tile.m_IconId, Tile.m_pIconGlyph, IconSize, TEXTALIGN_MC);
			}
			else
			{
				// 键名固定字号优先：先按固定字号量一次宽度，放得下就直接画——只量一次、
				// 只请求一套字形。DoLabel 内部开了宽度收缩时会从当前字号逐级 Size-- 量下去，
				// 113 个键 × 十几级 TextWidth、每个字号还要新字形，首帧足以把字体图集顶到扩容
				// （扩容要全量重传两张 16MB 纹理），这就是"刚打开卡一下"的来源。
				// 只有极少数长标签（Backspace / CapsLock）放不下时才降字号，且最多再试三档，
				// 仍放不下就固定到最小档 + 省略号。
				const char *pKeyLabel = Localize(Tile.m_pLabel, Tile.m_pContext != nullptr ? Tile.m_pContext : "");
				const float FixedLabelSize = std::min(BodySize * 0.9f, Plate.h * CUi::ms_FontmodHeight);
				float LabelSize = FixedLabelSize;
				if(TextRender()->TextWidth(LabelSize, pKeyLabel) > LabelRect.w + 0.5f)
				{
					for(int Step = 0; Step < 3 && LabelSize > MinKeyLabelSize && TextRender()->TextWidth(LabelSize, pKeyLabel) > LabelRect.w + 0.5f; ++Step)
						LabelSize = std::max(MinKeyLabelSize, LabelSize * 0.88f);
				}
				// 缩到最小档还是放不下，键帽上就会带省略号：这种键补一条悬浮提示显示全名。
				if(TextRender()->TextWidth(LabelSize, pKeyLabel) > LabelRect.w + 0.5f)
					pTooltipText = pKeyLabel;
				SLabelProperties FixedProps = Props;
				// 字号已经自己算好，关掉内部收缩，避免又一次逐级搜索。
				FixedProps.m_EnableWidthCheck = false;
				Ui()->DoLabel(&LabelRect, pKeyLabel, LabelSize, TEXTALIGN_MC, FixedProps);
			}
			// 状态层与点击写回统一交给 DrawZoneState：键帽和鼠标图的描边、下划线、
			// 冲突点因此永远同一套，不会一边改了另一边忘了跟。
			const CButtonContainer *pButton = State.m_KeyButtonCount < (int)State.m_aKeyButtons.size() ? &State.m_aKeyButtons[State.m_KeyButtonCount] : nullptr;
			const int ButtonResult = DrawZoneState(Slot, Rect, Plate, ui_token::radius::BASE, IGraphics::CORNER_ALL, Hovered, Selected, PlateInset, 0, pButton);
			if(pButton != nullptr)
			{
				++State.m_KeyButtonCount;
				// 只给被截断的键弹提示：完整键名放得下时再弹一次「A」纯属挡视线。
				if(!ReadOnly && pTooltipText != nullptr)
					GameClient()->m_Tooltips.DoToolTipForRect(pButton, &Rect, pTooltipText);
			}
			if(ButtonResult != 0 && !ReadOnly)
			{
				State.m_Selected = Slot;
				State.m_HasSelected = true;
				// 点击后穿过其他键位前往编辑面板时，保留用户明确选中的键。
				State.m_KeySelectionHeld = true;
				State.m_Error.clear();
				State.m_Status.clear();
			}
		};

		// 鼠标不再排成一排键帽：按顶视图自绘一只鼠标，左右键、滚轮三段、滚轮倾斜和
		// 两个侧键各自独立命中。状态（选中/悬停/已绑定/冲突）全部走键帽那套 DrawZoneState。
		// 区域文字：大键用完整键名，滚轮与侧键放不下，用 M3/M4/M5 与箭头，完整名字在悬停提示里。
		static constexpr std::array<const char *, qm_bind_mouse::ZONE_COUNT> s_apZoneLabels = {"Mouse 1", "Mouse 2", nullptr, "M3", nullptr, nullptr, nullptr, "M5", "M4"};
		static constexpr std::array<const char *, qm_bind_mouse::ZONE_COUNT> s_apZoneNames = {"Mouse 1", "Mouse 2", "Wheel up", "Mouse 3", "Wheel down", "Wheel left", "Wheel right", "Mouse 5", "Mouse 4"};
		static constexpr std::array<int, qm_bind_mouse::ZONE_COUNT> s_aZoneIcons = {-1, -1, (int)EQmIcon::CHEVRON_UP, -1, (int)EQmIcon::CHEVRON_DOWN, (int)EQmIcon::CHEVRON_LEFT, (int)EQmIcon::CHEVRON_RIGHT, -1, -1};
		// 字形常量不是 constexpr（textrender.h 里的 static const char *），这里跟着用 static const。
		static const std::array<const char *, qm_bind_mouse::ZONE_COUNT> s_apZoneIconGlyphs = {nullptr, nullptr, FontIcons::FONT_ICON_CHEVRON_UP, nullptr, FontIcons::FONT_ICON_CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_LEFT, FontIcons::FONT_ICON_CHEVRON_RIGHT, nullptr, nullptr};
		const auto RenderMouseDiagram = [this, &State, ReadOnly, &NoteZonePress, &DrawZoneState, BodySize, PlotScale = Metrics.m_UiScale](const CUIRect &Block) {
			const qm_bind_mouse::SDiagram Diagram = qm_bind_mouse::Layout(Block);
			// 行跳转的二级揭示：选中的是鼠标键时把整张图拉进视口（那颗键不画在键盘排里）。
			if(!ReadOnly && State.m_RevealSelectedKey && State.m_HasSelected && qm_bind_mouse::IsMouseKey(State.m_Selected.m_Key) && g_pBindDeckScrollRegion != nullptr)
			{
				State.m_RevealSelectedKey = false;
				g_pBindDeckScrollRegion->AddRect(Diagram.m_Body, true);
			}
			// 本体：和键帽同一套深色玻璃材质——整只鼠标是一块深色剪影，不再把上半截
			// 单独做成「深色屏幕」。左右键分界和按键区/掌托分界用两道细亮缝表达，
			// 和真实鼠标顶视图一个读法；垫一层下移的阴影让它从卡片上浮起来。
			const float ShadowOffset = std::max(1.0f, BodySize * 0.14f);
			const CUIRect BodyShadow{Diagram.m_Body.x, Diagram.m_Body.y + ShadowOffset, Diagram.m_Body.w, Diagram.m_Body.h};
			DrawRoundedSurface(Ui(), BodyShadow, ui_token::color::SURFACE_OVERLAY.WithMultipliedAlpha(0.5f), ui_token::color::COLOR_TRANSPARENT, Diagram.m_BodyRadius);
			DrawRoundedSurface(Ui(), Diagram.m_Body, ui_token::color::SURFACE_GLASS, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.14f), Diagram.m_BodyRadius, ui_token::feedback::ICON_BORDER_WIDTH);
			const CUIRect &ButtonZone = Diagram.m_aZones[qm_bind_mouse::ZONE_MOUSE1].m_Rect;
			const float SeamWidth = std::max(2.0f, Diagram.m_Body.w * 0.022f);
			const float ButtonBottom = ButtonZone.y + ButtonZone.h;
			const CUIRect VerticalSeam{Diagram.m_Body.x + Diagram.m_Body.w * 0.5f - SeamWidth * 0.5f, ButtonZone.y, SeamWidth, ButtonZone.h};
			DrawRoundedSurface(Ui(), VerticalSeam, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.10f), ui_token::color::COLOR_TRANSPARENT, SeamWidth * 0.5f);
			const float SeamInset = Diagram.m_Body.w * 0.03f;
			const CUIRect HorizontalSeam{Diagram.m_Body.x + SeamInset, ButtonBottom, Diagram.m_Body.w - 2.0f * SeamInset, std::max(1.0f, Diagram.m_Body.w * 0.008f)};
			DrawRoundedSurface(Ui(), HorizontalSeam, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.08f), ui_token::color::COLOR_TRANSPARENT, HorizontalSeam.h * 0.5f);
			// 滚轮整根画成一枚竖胶囊：上滚 / 中键 / 下滚是它里面的三段。三段各画一枚小药丸
			// 会叠成十字，看起来像方向键；整根胶囊才有「滚轮」的样子。
			const auto DrawWheelBackdrop = [this, &Diagram]() {
				DrawRoundedSurface(Ui(), Diagram.m_Wheel, ui_token::color::SURFACE_ELEVATED, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.10f), Diagram.m_WheelRadius, ui_token::feedback::ICON_BORDER_WIDTH * 0.5f);
				DrawRoundedSurface(Ui(), Diagram.m_Wheel, ui_token::color::TINT_PART, ui_token::color::COLOR_TRANSPARENT, Diagram.m_WheelRadius);
				// 段间用浅色细纹分隔：整根胶囊因此像有纹路的滚轮，而不是三块拼起来的按钮。
				const float DividerWidth = Diagram.m_Wheel.w * 0.62f;
				const float DividerThickness = std::max(1.0f, Diagram.m_Wheel.w * 0.035f);
				const float DividerX = Diagram.m_Wheel.x + (Diagram.m_Wheel.w - DividerWidth) * 0.5f;
				for(const int UpperZone : {qm_bind_mouse::ZONE_WHEEL_UP, qm_bind_mouse::ZONE_MOUSE3})
				{
					const CUIRect &Upper = Diagram.m_aZones[UpperZone].m_Rect;
					const CUIRect Divider{DividerX, Upper.y + Upper.h, DividerWidth, DividerThickness};
					DrawRoundedSurface(Ui(), Divider, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.12f), ui_token::color::COLOR_TRANSPARENT, DividerThickness * 0.5f);
				}
			};
			// 悬停按纯几何定唯一区域（滚轮压在左右键上时，底下那颗大键不能跟着亮）；
			// 和键面一样不看趟次，否则最终出图那趟会把反馈盖掉。
			const int TopZone = qm_bind_mouse::HitTest(Diagram, Ui()->MouseX(), Ui()->MouseY());
			const float PlateInset = std::max(1.0f, std::floor(PlotScale * 0.5f));
			for(const int ZoneIndex : qm_bind_mouse::ZONE_DRAW_ORDER)
			{
				if(ZoneIndex == qm_bind_mouse::ZONE_WHEEL_UP)
					DrawWheelBackdrop();
				const qm_bind_mouse::SZone &Zone = Diagram.m_aZones[ZoneIndex];
				const CBindSlot Slot(Zone.m_Key, State.m_ModifierMask);
				// 悬停同样用纯几何 + 命中优先级：MouseHovered 的趟次条件会把反馈盖掉。
				const bool Hovered = TopZone == ZoneIndex;
				NoteZonePress(Hovered);
				const bool Selected = State.m_HasSelected && State.m_Selected == Slot;
				const CUIRect Plate{Zone.m_Rect.x + PlateInset, Zone.m_Rect.y + PlateInset, std::max(1.0f, Zone.m_Rect.w - 2.0f * PlateInset), std::max(1.0f, Zone.m_Rect.h - 2.0f * PlateInset)};
				const bool WheelSegment = ZoneIndex == qm_bind_mouse::ZONE_WHEEL_UP || ZoneIndex == qm_bind_mouse::ZONE_MOUSE3 || ZoneIndex == qm_bind_mouse::ZONE_WHEEL_DOWN;
				const bool TiltPad = ZoneIndex == qm_bind_mouse::ZONE_WHEEL_LEFT || ZoneIndex == qm_bind_mouse::ZONE_WHEEL_RIGHT;
				const bool SideButton = ZoneIndex == qm_bind_mouse::ZONE_MOUSE4 || ZoneIndex == qm_bind_mouse::ZONE_MOUSE5;
				// 左右键直接落在本体剪影上（只靠两道缝区分）；侧键在本体外侧，需要自己一块板；
				// 滚轮段落在整根胶囊上；倾斜键平时不铺底，悬停/选中才浮出一小块底。
				if(SideButton)
					DrawRoundedSurface(Ui(), Plate, ui_token::color::SURFACE_ELEVATED, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.10f), Zone.m_Radius, ui_token::feedback::ICON_BORDER_WIDTH * 0.5f, Zone.m_Corners);
				else if(TiltPad && (Hovered || Selected))
					DrawRoundedSurface(Ui(), Plate, ui_token::color::TINT_PART_ACTIVE, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.10f), Zone.m_Radius, ui_token::feedback::ICON_BORDER_WIDTH * 0.5f);
				if(WheelSegment && (Hovered || Selected))
					DrawRoundedSurface(Ui(), Plate, ui_token::color::SURFACE_HIGHLIGHT, ui_token::color::COLOR_TRANSPARENT, Zone.m_Radius, 0.0f, Zone.m_Corners);
				else if(!WheelSegment && !TiltPad && (Selected || Hovered))
					DrawRoundedSurface(Ui(), Plate, Selected ? ui_token::color::TINT_ZONE_SELECTED : ui_token::color::TINT_ZONE_HOVER, ui_token::color::COLOR_TRANSPARENT, Zone.m_Radius, 0.0f, Zone.m_Corners);
				CUIRect LabelRect = Plate;
				LabelRect.VMargin(std::max(1.0f, Plate.w * 0.06f), &LabelRect);
				// 左右键的名字放在板子下沿：滚轮整簇压在接缝上方，居中写会被滚轮盖住。
				const bool LowerNameBand = ZoneIndex == qm_bind_mouse::ZONE_MOUSE1 || ZoneIndex == qm_bind_mouse::ZONE_MOUSE2;
				if(LowerNameBand)
				{
					LabelRect.y += Plate.h * 0.62f;
					LabelRect.h = std::max(1.0f, Plate.h * 0.34f);
				}
				if(s_apZoneIconGlyphs[ZoneIndex] != nullptr)
				{
					// 倾斜箭头平时压暗一档，悬停/选中才回到正常前景色；字号单独放大一点，
					// 否则它比滚轮里的箭头还小，缩成一个看不见的点。
					const bool FadedTilt = TiltPad && !Hovered && !Selected;
					if(FadedTilt)
						TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
					const float IconSize = TiltPad ? std::min(BodySize * 0.95f, std::min(LabelRect.w, LabelRect.h) * 0.9f) :
									 std::min(BodySize * 1.15f, std::min(LabelRect.w, LabelRect.h) * 0.72f);
					Ui()->DoLabel_QmIcon(&LabelRect, (EQmIcon)s_aZoneIcons[ZoneIndex], s_apZoneIconGlyphs[ZoneIndex], IconSize, TEXTALIGN_MC);
					if(FadedTilt)
						TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				else if(s_apZoneLabels[ZoneIndex] != nullptr)
				{
					// 键名走普通标签收缩（最多九颗，不是 113 颗键面，逐级量宽的成本可以接受）。
					SLabelProperties Props;
					Props.m_MaxWidth = std::max(1.0f, LabelRect.w);
					Props.m_MinimumFontSize = ui_token::font::TIP * 0.62f * PlotScale;
					Props.m_EllipsisAtEnd = true;
					const float LabelSize = LowerNameBand ? std::min(BodySize * 0.95f, LabelRect.h * 0.82f) :
										std::min(BodySize * 0.82f, std::min(LabelRect.w, LabelRect.h) * 0.72f);
					Ui()->DoLabel(&LabelRect, s_apZoneLabels[ZoneIndex], LabelSize, TEXTALIGN_MC, Props);
				}
				const CButtonContainer *pButton = nullptr;
				if(State.m_KeyButtonCount < (int)State.m_aKeyButtons.size())
				{
					pButton = &State.m_aKeyButtons[State.m_KeyButtonCount];
					++State.m_KeyButtonCount;
				}
				// 滚轮三段在胶囊里已经有两道纹路，再画已绑定短线会把滚轮变成一排横线，
				// 所以滚轮段不画已绑定标记（左右键、侧键、倾斜键照常画）。
				const int BoundStyle = WheelSegment ? -1 : 1;
				const int ButtonResult = DrawZoneState(Slot, Zone.m_Rect, Plate, Zone.m_Radius, Zone.m_Corners, Hovered, Selected, std::max(1.0f, Zone.m_Radius * 0.2f), BoundStyle, pButton);
				if(pButton != nullptr)
					GameClient()->m_Tooltips.DoToolTip(pButton, &Zone.m_Rect, Localize(s_apZoneNames[ZoneIndex]));
				if(ButtonResult != 0 && !ReadOnly)
				{
					State.m_Selected = Slot;
					State.m_HasSelected = true;
					State.m_KeySelectionHeld = true;
					State.m_Error.clear();
					State.m_Status.clear();
				}
			}
		};
		const auto RenderPhysicalKeyRow = [&](const std::initializer_list<SBindKeyTile> Tiles) {
			if(Tiles.size() == 0)
				return;
			CUIRect Row;
			char aId[64];
			str_format(aId, sizeof(aId), "bind-keyboard-row-%d", KeyRow++);
			const bool HasRow = Flow.Control(aId, KeyRowHeight, Row);
			Flow.Space(LineSpacing);
			if(!HasRow)
				return;
			float X = Row.x;
			for(const SBindKeyTile &Tile : Tiles)
			{
				// 跨行键把下面几行的高度一起吃掉：行高 + 行距逐行累加，正好落在下一行的
				// 边界上（小键盘的 + 和 Enter 各跨两行）。
				const float TileHeight = Row.h + (float)(Tile.m_SpanRows - 1) * (Row.h + LineSpacing);
				CUIRect TileRect{X, Row.y, KeyTileWidth(Tile.m_Units), TileHeight};
				DrawKey(Tile, TileRect);
				X += KeyUnit * Tile.m_Units;
			}
		};

		// 按真实 104 键键盘排列。每行都是 22.5 单元：主键区 15 + 间隔 0.25 + 导航 3 +
		// 间隔 0.25 + 小键盘 4，所以六行最右边缘落在同一条竖线上（F12 对齐 Backspace）。
		// 小键盘的 + 与 Enter 用 m_SpanRows=2 纵向跨两行，0 用 m_Units=2 横向跨两列。
		RenderPhysicalKeyRow({{KEY_ESCAPE, "Esc", nullptr}, {KEY_UNKNOWN, nullptr, nullptr, 0.5f, nullptr, true},
			{KEY_F1, "F1", nullptr}, {KEY_F2, "F2", nullptr}, {KEY_F3, "F3", nullptr}, {KEY_F4, "F4", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.5f, nullptr, true},
			{KEY_F5, "F5", nullptr}, {KEY_F6, "F6", nullptr}, {KEY_F7, "F7", nullptr}, {KEY_F8, "F8", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.5f, nullptr, true},
			{KEY_F9, "F9", nullptr}, {KEY_F10, "F10", nullptr}, {KEY_F11, "F11", nullptr}, {KEY_F12, "F12", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.5f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_PRINTSCREEN, "Prt", nullptr}, {KEY_SCROLLLOCK, "Scr", nullptr}, {KEY_PAUSE, "Pau", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_NUMLOCKCLEAR, "Num", nullptr}, {KEY_KP_DIVIDE, "/", nullptr}, {KEY_KP_MULTIPLY, "*", nullptr}, {KEY_KP_MINUS, "-", nullptr}});
		RenderPhysicalKeyRow({{KEY_GRAVE, "~", nullptr}, {KEY_1, "1", nullptr}, {KEY_2, "2", nullptr}, {KEY_3, "3", nullptr},
			{KEY_4, "4", nullptr}, {KEY_5, "5", nullptr}, {KEY_6, "6", nullptr}, {KEY_7, "7", nullptr}, {KEY_8, "8", nullptr},
			{KEY_9, "9", nullptr}, {KEY_0, "0", nullptr}, {KEY_MINUS, "-", nullptr}, {KEY_EQUALS, "=", nullptr},
			{KEY_BACKSPACE, "Backspace", nullptr, 2.0f}, {KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_INSERT, "Ins", nullptr}, {KEY_HOME, "Home", nullptr}, {KEY_PAGEUP, "PgU", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_KP_7, "7", nullptr}, {KEY_KP_8, "8", nullptr}, {KEY_KP_9, "9", nullptr},
			{KEY_KP_PLUS, "+", nullptr, 1.0f, nullptr, false, 2}});
		RenderPhysicalKeyRow({{KEY_TAB, "Tab", nullptr, 1.5f}, {KEY_Q, "Q", nullptr}, {KEY_W, "W", nullptr}, {KEY_E, "E", nullptr},
			{KEY_R, "R", nullptr}, {KEY_T, "T", nullptr}, {KEY_Y, "Y", nullptr}, {KEY_U, "U", nullptr}, {KEY_I, "I", nullptr},
			{KEY_O, "O", nullptr}, {KEY_P, "P", nullptr}, {KEY_LEFTBRACKET, "[", nullptr}, {KEY_RIGHTBRACKET, "]", nullptr},
			{KEY_BACKSLASH, "\\", nullptr, 1.5f}, {KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_DELETE, "Del", nullptr}, {KEY_END, "End", nullptr}, {KEY_PAGEDOWN, "PgD", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_KP_4, "4", nullptr}, {KEY_KP_5, "5", nullptr}, {KEY_KP_6, "6", nullptr},
			{KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true}});
		RenderPhysicalKeyRow({{KEY_CAPSLOCK, "Caps", nullptr, 1.75f}, {KEY_A, "A", nullptr}, {KEY_S, "S", nullptr}, {KEY_D, "D", nullptr},
			{KEY_F, "F", nullptr}, {KEY_G, "G", nullptr}, {KEY_H, "H", nullptr}, {KEY_J, "J", nullptr}, {KEY_K, "K", nullptr},
			{KEY_L, "L", nullptr}, {KEY_SEMICOLON, ";", nullptr}, {KEY_APOSTROPHE, "'", nullptr}, {KEY_RETURN, "Enter", nullptr, 2.25f},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 3.0f, nullptr, true},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_KP_1, "1", nullptr}, {KEY_KP_2, "2", nullptr}, {KEY_KP_3, "3", nullptr},
			{KEY_KP_ENTER, "Ent", nullptr, 1.0f, nullptr, false, 2}});
		// 导航区：上行只有 ↑，两边空位各 1 单元（与下行三颗键同宽、同一条键缝），
		// 因此 ↑ 的槽位矩形与下行的 ↓ 完全重合：x = 15 + 0.25 + 1 单元，w = 1 单元 - 键缝。
		RenderPhysicalKeyRow({{KEY_LSHIFT, "Shift", nullptr, 2.25f}, {KEY_Z, "Z", nullptr}, {KEY_X, "X", nullptr}, {KEY_C, "C", nullptr},
			{KEY_V, "V", nullptr}, {KEY_B, "B", nullptr}, {KEY_N, "N", nullptr}, {KEY_M, "M", nullptr}, {KEY_COMMA, ",", nullptr},
			{KEY_PERIOD, ".", nullptr}, {KEY_SLASH, "/", nullptr}, {KEY_RSHIFT, "Shift", nullptr, 2.75f},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true},
			{KEY_UP, nullptr, nullptr, 1.0f, nullptr, false, 1, (int)EQmIcon::CHEVRON_UP, FontIcons::FONT_ICON_CHEVRON_UP}, {KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_KP_0, "0", nullptr, 2.0f}, {KEY_KP_PERIOD, ".", nullptr}, {KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true}});
		RenderPhysicalKeyRow({{KEY_LCTRL, "Ctrl", nullptr, 1.25f}, {KEY_LGUI, "Win", nullptr, 1.25f}, {KEY_LALT, "Alt", nullptr, 1.25f},
			{KEY_SPACE, "Space", nullptr, 6.25f}, {KEY_RALT, "Alt", nullptr, 1.25f}, {KEY_RGUI, "Win", nullptr, 1.25f},
			{KEY_MENU, Localizable("Menu", "Bind keyboard key"), nullptr, 1.25f}, {KEY_RCTRL, "Ctrl", nullptr, 1.25f},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true},
			{KEY_LEFT, nullptr, nullptr, 1.0f, nullptr, false, 1, (int)EQmIcon::CHEVRON_LEFT, FontIcons::FONT_ICON_CHEVRON_LEFT},
			{KEY_DOWN, nullptr, nullptr, 1.0f, nullptr, false, 1, (int)EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN},
			{KEY_RIGHT, nullptr, nullptr, 1.0f, nullptr, false, 1, (int)EQmIcon::CHEVRON_RIGHT, FontIcons::FONT_ICON_CHEVRON_RIGHT},
			{KEY_UNKNOWN, nullptr, nullptr, 0.25f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 4.0f, nullptr, true}});

		Flow.Space(LineSpacing * 0.65f);
		CUIRect MouseHeader;
		if(Flow.Control("bind-mouse-header", LineHeight, MouseHeader))
			DoSettingsMenuLabel(SETTINGS_CONTROLS, -1, -1, "qm-bind-editor-mouse", &MouseHeader, Localize("Mouse"), BodySize, TEXTALIGN_ML, {}, (int)MouseHeader.w);
		// 鼠标图自己占一块固定高度的行：高度只由行高决定，测量和绘制两趟算出来的卡片高度一致。
		CUIRect MouseBlock;
		if(Flow.Control("bind-mouse-diagram", qm_bind_mouse::DiagramHeight(LineHeight), MouseBlock))
			RenderMouseDiagram(MouseBlock);
		Flow.Space(LineSpacing);
		if(!ReadOnly)
			State.m_KeyboardRect = {KeyboardStart.x, KeyboardStart.y, KeyboardStart.w, Left.y - KeyboardStart.y};
	};
	const auto RenderActions = [&](CUIRect &Right) {
		CSettingsContentLayoutFlow Flow(Right);
		const bool KeyCaptureActive = GameClient()->m_KeyBinder.IsActive();
		CBindSlot ActiveSlot = State.m_Selected;
		if(ActiveSlot.m_Key == KEY_UNKNOWN)
			ActiveSlot = CBindSlot(KEY_A, State.m_ModifierMask);

		char aActiveKeyName[128];
		GameClient()->m_Binds.GetKeyBindName(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask, aActiveKeyName, sizeof(aActiveKeyName));
		CUIRect HeadingGap;
		Flow.Control("bind-actions-top-gap", LineHeight, HeadingGap);
		CUIRect SelectedLabel;
		if(Flow.Control("bind-selected-key", LineHeight, SelectedLabel))
			DoSettingsMenuLabel(SETTINGS_CONTROLS, -1, -1, "qm-bind-editor-selected-key", &SelectedLabel, aActiveKeyName, BodySize, TEXTALIGN_ML, {}, (int)SelectedLabel.w);
		CUIRect CaptureRow;
		if(Flow.Control("bind-key-capture", LineHeight, CaptureRow))
		{
			const CBindSlot PreviousSlot = ActiveSlot;
			const CKeyBinder::CKeyReaderResult CaptureResult = GameClient()->m_KeyBinder.DoKeyReader(&State.m_CaptureButton, &State.m_ClearCaptureButton, &CaptureRow, PreviousSlot, false, BodySize);
			const bool CaptureStarted = !KeyCaptureActive && GameClient()->m_KeyBinder.IsActive();
			if(!ReadOnly && CaptureStarted)
			{
				// 捕获开始后冻结悬停目标，后续帧仍由同一个 key reader 编辑该键位。
				State.m_Selected = PreviousSlot;
				State.m_ModifierMask = PreviousSlot.m_ModifierMask;
				State.m_HasSelected = true;
				State.m_CapturePending = true;
			}
			if(!ReadOnly && !GameClient()->m_KeyBinder.IsActive())
				State.m_CapturePending = false;
			if(!ReadOnly && CaptureResult.m_Bind != PreviousSlot)
			{
				if(CaptureResult.m_Bind.m_Key == KEY_UNKNOWN)
				{
					// 捕获行右边那个垃圾桶会把这个键上的所有绑定一次清掉：点了就清，
					// 写回前先记撤销点，状态栏给"可撤销"提示——不再弹确认、不留待办状态。
					// 这条分支只由显式清空（垃圾桶 / 右键键名按钮）触发，按 Esc 中断捕获
					// 走的是 m_Aborted，m_Bind 保持原值，不会进到这里。
					try
					{
						auto vUndo = State.m_vBindUndo;
						RecordBindUndo(vUndo, GameClient()->m_Binds, PreviousSlot);
						std::string Status = BindEditorDeletedStatus();
						if(GameClient()->m_Binds.Rebind(PreviousSlot, CaptureResult.m_Bind, ""))
						{
							State.m_vBindUndo.swap(vUndo);
							State.m_Status.swap(Status);
							State.m_Error.clear();
						}
					}
					catch(const std::bad_alloc &)
					{
					}
					catch(const std::length_error &)
					{
					}
				}
				else
				{
					char aNewKeyName[128];
					GameClient()->m_Binds.GetKeyBindName(CaptureResult.m_Bind.m_Key, CaptureResult.m_Bind.m_ModifierMask, aNewKeyName, sizeof(aNewKeyName));
					const char *pPreviousBinding = GameClient()->m_Binds.Get(PreviousSlot.m_Key, PreviousSlot.m_ModifierMask);
					// 行限制校验也会构造序列化字符串，失败时保留原绑定和选中位置。
					try
					{
						if(qm_bind_editor::FitsConfigLine(aNewKeyName, pPreviousBinding))
						{
							// 撤销记录和新命令都准备成功，才一起发布；失败不挤掉旧撤销点。
							auto vUndo = State.m_vBindUndo;
							RecordBindUndoTwo(vUndo, GameClient()->m_Binds, PreviousSlot, CaptureResult.m_Bind);
							if(GameClient()->m_Binds.Rebind(PreviousSlot, CaptureResult.m_Bind, pPreviousBinding))
							{
								State.m_vBindUndo.swap(vUndo);
								State.m_Selected = CaptureResult.m_Bind;
								State.m_ModifierMask = CaptureResult.m_Bind.m_ModifierMask;
								State.m_HasSelected = true;
								State.m_KeySelectionHeld = true;
								State.m_Error.clear();
							}
						}
						else
							SetBindEditorError(State, Localize("This bind is already over the local config line limit."));
					}
					catch(const std::bad_alloc &)
					{
					}
					catch(const std::length_error &)
					{
					}
				}
				ActiveSlot = State.m_Selected;
				GameClient()->m_Binds.GetKeyBindName(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask, aActiveKeyName, sizeof(aActiveKeyName));
			}
		}
		Flow.Space(LineSpacing);

		const std::string ExistingBinding = GameClient()->m_Binds.Get(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask);
		qm_bind_editor::SCommands Parsed = qm_bind_editor::SplitCommands(ExistingBinding);
		if(!ReadOnly && !Parsed.m_Complete)
			SetBindEditorError(State, Localize("This bind contains an unfinished quote and cannot be edited safely."));

		CUIRect BoundHeader;
		if(Flow.Control("bind-bound-functions", LineHeight, BoundHeader))
		{
			// 「撤销」放在标题右侧，做成图标按钮：撤销上一次删除 / 清空 / 修改写回。
			// 图标取图标库现成的「逆时针回退箭头」（ARROW_ROTATE_LEFT，arrow-counter-clockwise），
			// 一眼就是 undo；循环箭头那类像刷新，不用。空栈时传 -1 走现成的禁用斜线状态。
			CUIRect UndoRect;
			const float UndoSize = std::max(LineHeight, LineHeight * Metrics.m_UiScale);
			BoundHeader.VSplitRight(std::min(UndoSize, BoundHeader.w * 0.4f), &BoundHeader, &UndoRect);
			DoSettingsMenuLabel(SETTINGS_CONTROLS, -1, -1, "qm-bind-editor-current", &BoundHeader, Localize("Bound functions"), BodySize, TEXTALIGN_ML, {}, (int)BoundHeader.w);
			const bool UndoClicked = ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), &State.m_UndoButton, EQmIcon::ARROW_ROTATE_LEFT, FontIcons::FONT_ICON_ARROW_ROTATE_LEFT, State.m_vBindUndo.empty() ? -1 : 0, UndoRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) != 0;
			GameClient()->m_Tooltips.DoToolTip(&State.m_UndoButton, &UndoRect, Localize("Undo"));
			if(UndoClicked && ApplyBindUndo(State.m_vBindUndo, GameClient()->m_Binds))
			{
				// 撤销后列表、下划线和冲突标记都由状态重算，这里只收掉可能失效的编辑态。
				Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
				State.m_EditActionIndex = -1;
				State.m_EditActionRectValid = false;
				State.m_Error.clear();
				State.m_Status.clear();
			}
		}
		Flow.Space(LineSpacing * 0.4f);
		// 就地编辑写回：读整串当前绑定、只替换被编辑的那一条、拼回原顺序再写。
		// 槽位用编辑开始时记下的 m_EditActionSlot：即使选中键已经换掉，也只会写回原来那一颗键。
		// 写回路径和「添加功能」完全一致（CBinds::Bind，持久化由 binds 的配置回调负责）。
		const auto ApplyActionEdit = [&](const int EditIndex) {
			const CBindSlot EditSlot = State.m_EditActionSlot;
			qm_bind_editor::SCommands Current = qm_bind_editor::SplitCommands(GameClient()->m_Binds.Get(EditSlot.m_Key, EditSlot.m_ModifierMask));
			const std::string EditedText = State.m_EditActionInput.GetString();
			const qm_bind_editor::SCommands Edited = qm_bind_editor::SplitCommands(EditedText);
			if(!Current.m_Complete)
			{
				SetBindEditorError(State, Localize("This bind contains an unfinished quote and cannot be edited safely."));
				return false;
			}
			if(!Edited.m_Complete || Edited.m_vCommands.empty() || !Console()->LineIsValid(EditedText.c_str()))
			{
				SetBindEditorError(State, Localize("The command is empty, incomplete, or exceeds the local config line limit."));
				return false;
			}
			if(EditIndex < 0 || EditIndex >= (int)Current.m_vCommands.size())
				return false;
			std::vector<std::string> vCommands = Current.m_vCommands;
			vCommands.erase(vCommands.begin() + EditIndex);
			vCommands.insert(vCommands.begin() + EditIndex, Edited.m_vCommands.begin(), Edited.m_vCommands.end());
			const std::string Updated = qm_bind_editor::JoinCommands(vCommands);
			char aKeyName[128];
			GameClient()->m_Binds.GetKeyBindName(EditSlot.m_Key, EditSlot.m_ModifierMask, aKeyName, sizeof(aKeyName));
			if(!qm_bind_editor::FitsConfigLine(aKeyName, Updated))
			{
				SetBindEditorError(State, Localize("This bind is already over the local config line limit."));
				return false;
			}
			// 编辑写回也进撤销栈：改错了能一键退回改之前的整串命令。
			RecordBindUndo(State.m_vBindUndo, GameClient()->m_Binds, EditSlot);
			GameClient()->m_Binds.Bind(EditSlot.m_Key, Updated.c_str(), false, EditSlot.m_ModifierMask);
			State.m_Error.clear();
			return true;
		};
		// 收尾：写回成功才退出编辑；写不回去（命令非法）保留编辑态与草稿，错误提示还在下方。
		const auto CommitActionEdit = [&]() -> bool {
			if(State.m_EditActionIndex < 0)
				return false;
			if(!ApplyActionEdit(State.m_EditActionIndex))
				return false;
			State.m_EditActionIndex = -1;
			State.m_EditActionRectValid = false;
			Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
			return true;
		};
		// 选中键一换（点别的键、键位捕获、行跳转）先把正在编辑的内容写回原来的键，
		// 不再静默丢掉；写不回去的那份草稿不能安全落到别的键上，只能取消。
		if(!ReadOnly && State.m_EditActionIndex >= 0 && State.m_EditActionSlot != ActiveSlot)
		{
			if(!CommitActionEdit())
			{
				Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
				State.m_EditActionIndex = -1;
				State.m_EditActionRectValid = false;
			}
		}
		// 卡片内容会被画两趟：交互趟（处理输入）与 RenderOnly 的外观趟（只提交外观）。
		// 绘制和可点判定都不能只看 ReadOnly——外观趟会把按钮、编辑行整段跳过，看起来
		// 就是"图标消失、点行没反应"。输入本身仍由 DoButtonLogic / InputField 按 RenderOnly 隔离。
		const bool ContentDraw = !ReadOnly || Ui()->BackgroundRender();
		if(ContentDraw)
			State.m_vActionControls.resize(Parsed.m_vCommands.size());
		// 编辑中的那条动作已经被别处删掉（行下标越界）时同样取消，避免残留编辑框指向空行。
		if(!ReadOnly && State.m_EditActionIndex >= (int)Parsed.m_vCommands.size())
		{
			Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
			State.m_EditActionIndex = -1;
			State.m_EditActionRectValid = false;
		}
		if(Parsed.m_vCommands.empty())
		{
			CUIRect EmptyRow;
			if(Flow.Control("bind-empty-functions", LineHeight, EmptyRow))
				Ui()->DoLabel(&EmptyRow, Localize("No functions bound"), BodySize, TEXTALIGN_ML);
			Flow.Space(LineSpacing);
		}
		else
		{
			// 把一条多命令绑定显示成从上到下的指令节点链：每个节点仍对应
			// 一个真实控制台命令，删除节点只重组原命令串，不改变执行顺序。
			const float NodeHeight = std::max(LineHeight * 1.55f, LineHeight + LineSpacing);
			const bool CanEditRows = ContentDraw && Parsed.m_Complete && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive();
			IUiContext ActionCtx = SettingsUiContext("qm_bind_editor_action_edit", Metrics.m_UiScale);
			if(ReadOnly)
			{
				ActionCtx.m_pAnim = nullptr;
				ActionCtx.m_pTree = nullptr;
			}
			// 编辑中点在行外任意位置 = 保存并关闭：不需要专门去点 ✓，也不用先按取消。
			// 点在输入框或 ✓/✕ 上仍在编辑行内，交给下面的控件自己处理。
			if(!ReadOnly && State.m_EditActionIndex >= 0 && State.m_EditActionRectValid && Ui()->MouseButtonClicked(0) != 0)
			{
				CUIRect Hit = State.m_EditActionRect;
				const float Margin = 2.0f * Metrics.m_UiScale;
				Hit.x -= Margin;
				Hit.y -= Margin;
				Hit.w += 2.0f * Margin;
				Hit.h += 2.0f * Margin;
				if(!Ui()->MouseInside(&Hit))
					CommitActionEdit();
			}
			int RemoveRow = -1;
			for(size_t Index = 0; Index < Parsed.m_vCommands.size(); ++Index)
			{
				CUIRect Row, Remove;
				char aId[64];
				str_format(aId, sizeof(aId), "bind-flow-node-%d", (int)Index);
				const bool HasRow = Flow.Control(aId, NodeHeight, Row);
				Flow.Space(LineSpacing * 0.35f);
				if(!HasRow)
					continue;
				SBindEditorUiState::SActionControls *pControls = Index < State.m_vActionControls.size() ? &State.m_vActionControls[Index] : nullptr;
				// 就地编辑：输入框吃满剩余宽度，右边同一行贴「✓ 确认 / ✕ 取消」两个图标按钮。
				// 不再要求 pControls 非空——之前多画一行按钮，测量趟和内容趟的布局高度不一致，
				// 内容趟已经排到卡片高度之外，Flow.Control 一直返回 false，按钮就整排画不出来。
				// 现在编辑行固定在同一行内完成布局，两趟走同一分支，只有交互被 ContentDraw 跳过。
				if(State.m_EditActionIndex == (int)Index && State.m_EditActionSlot == ActiveSlot)
				{
					// 编辑行只铺普通行底色：强调色交给输入框自己的聚焦框，不再把 ✓/✕
					// 一起圈进一块高亮边框里。行矩形记进状态，"点行外即保存"靠它判断。
					DrawRoundedSurface(Ui(), Row, ui_token::color::SURFACE_ELEVATED, ui_token::color::BORDER_SUBTLE, ui_token::radius::BASE);
					State.m_EditActionRect = Row;
					State.m_EditActionRectValid = true;
					const float Scale = Metrics.m_UiScale;
					// 图标按钮沿用同页图标按钮的尺寸口径：正方形、边长取行高，留出按钮间距。
					const float IconButtonSize = std::max(LineHeight, NodeHeight - ui_token::spacing::XS * Scale);
					const float IconGap = ui_token::spacing::XS * Scale;
					CUIRect InputRect, ActionArea, ConfirmIcon, CancelIcon;
					Row.VSplitRight(2.0f * IconButtonSize + 2.0f * IconGap, &InputRect, &ActionArea);
					ActionArea.VSplitLeft(IconGap, nullptr, &ActionArea);
					ActionArea.VSplitLeft(IconButtonSize, &ConfirmIcon, &ActionArea);
					ActionArea.VSplitLeft(IconGap, nullptr, &ActionArea);
					ActionArea.VSplitLeft(IconButtonSize, &CancelIcon, nullptr);
					ui_widget::SInputFieldOptions EditOptions;
					EditOptions.m_FontSize = BodySize;
					EditOptions.m_ProcessInput = true;
					EditOptions.m_Clearable = true;
					EditOptions.m_pPlaceholder = Localize("Command");
					const ui_widget::SInputFieldResult EditResult = ui_widget::InputField(ActionCtx, &State.m_EditActionInput, InputRect, EditOptions);
					// 编辑中的高亮只落在输入框边框上（换成主题强调色），不再把右边的 ✓/✕
					// 一起圈进一块高亮框里。
					DrawRoundedSurface(Ui(), InputRect, ui_token::color::COLOR_TRANSPARENT, BindEditorAccent(Ui()), ui_token::radius::BASE, ui_token::feedback::ICON_BORDER_WIDTH);
					bool Commit = EditResult.m_Submitted;
					bool CancelRow = false;
					if(ContentDraw)
					{
						// 勾 / 叉都用现成图标按钮，字形取已有的 FontAwesome 字形，不自绘。
						Commit = Commit || ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), &State.m_EditActionCommit, EQmIcon::CHECK, "\xEE\x86\x82", 0, ConfirmIcon, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) != 0;
						// Esc 只在外观趟之外消费：RenderOnly 的绘制趟吞掉热键会影响别的界面。
						CancelRow = (!Ui()->RenderOnly() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) ||
							    ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), &State.m_EditActionCancel, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, CancelIcon, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) != 0;
						GameClient()->m_Tooltips.DoToolTip(&State.m_EditActionCommit, &ConfirmIcon, Localize("Save"));
						GameClient()->m_Tooltips.DoToolTip(&State.m_EditActionCancel, &CancelIcon, Localize("Cancel"));
					}
					if(Commit)
					{
						// 写不回去（命令非法）时保留编辑态和草稿，错误提示已经在下方。
						if(CommitActionEdit())
							Parsed.m_vCommands[Index] = State.m_EditActionInput.GetString();
					}
					else if(CancelRow)
					{
						State.m_EditActionIndex = -1;
						State.m_EditActionRectValid = false;
						Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
					}
					continue;
				}
				Row.VSplitRight(LineHeight, &Row, &Remove);
				// 画出来的状态必须和能不能点一致：上一版视觉态里掺了 ReadOnly / 外观趟，
				// 结果画出禁用斜线却仍可点，用户因此误删了一条。现在"是否可删"只看这条
				// 动作本身，实际可点再排除弹层与键位捕获。
				const bool RemoveAvailable = Parsed.m_Complete && !GameClient()->m_KeyBinder.IsActive();
				const bool CanRemoveNow = RemoveAvailable && !Ui()->IsPopupOpen();
				if(Index > 0)
				{
					CUIRect Connector{Row.x + LineHeight * 0.28f, Row.y - LineSpacing * 0.35f, 2.0f, LineSpacing * 0.7f};
					DrawRoundedSurface(Ui(), Connector, BindEditorAccent(Ui()).WithMultipliedAlpha(0.55f), ui_token::color::COLOR_TRANSPARENT, 1.0f);
				}
				CUIRect Node;
				Row.VMargin(2.0f, &Node);
				// 所有行同一个底色：深浅交替会让两条命令看起来像两种不同状态。
				const ColorRGBA NodeSurface = ui_token::color::SURFACE_ELEVATED;
				// 行状态只用「底色 + 边框 + 左侧条」表达，不做整块填色：
				//   默认 = 普通底色 + 弱描边；悬停 = 浅高亮底 + 白色高亮描边
				//   （白色透明度取按钮悬停描边同一个 token）；正在编辑这一行 = 强调色描边。
				// 悬停判定用纯几何：只看指针是否落在行矩形内。
				// VisualMouseHovered/MouseHovered 都带趟次条件（RenderOnly、内容/背景深度相等、
				// 弹层遮罩），这套页面会走多次绘制，任何一趟判成 false 都会把悬停反馈盖掉——
				// 之前悬停一直不出现就是这个原因。纯几何判定与趟次无关，哪一趟画都能出反馈。
				const bool RowEditing = State.m_EditActionIndex == (int)Index && State.m_EditActionSlot == ActiveSlot;
				// 悬停同样只看几何、不看趟次：最终出图那一趟 RenderOnly() 为真，加了 ReadOnly
				// 条件反而会把行悬停盖掉。
				const bool RowHovered = Ui()->MouseInside(&Node);
				DrawRoundedSurface(Ui(), Node, NodeSurface, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::BASE);
				if(RowHovered)
					DrawRoundedSurface(Ui(), Node, ui_token::color::TINT_ROW_ACTIVE, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::BASE);
				const ColorRGBA RowBorder = RowEditing ? BindEditorAccent(Ui()) : RowHovered ? ui_token::feedback::ACTIVE_BORDER :
													       ui_token::color::BORDER_SUBTLE;
				DrawRoundedSurface(Ui(), Node, ui_token::color::COLOR_TRANSPARENT, RowBorder, ui_token::radius::BASE, ui_token::feedback::ICON_BORDER_WIDTH);
				CUIRect Marker, Label;
				Node.VSplitLeft(LineHeight * 0.55f, &Marker, &Label);
				Marker.VMargin(LineHeight * 0.30f, &Marker);
				DrawRoundedSurface(Ui(), Marker, BindEditorAccent(Ui()), ui_token::color::COLOR_TRANSPARENT, Marker.w * 0.5f);
				Label.VMargin(LineSpacing * 0.6f, &Label);
				SLabelProperties Props;
				Props.m_MaxWidth = std::max(1.0f, Label.w - LineSpacing);
				Props.m_EllipsisAtEnd = true;
				Props.m_MinimumFontSize = std::max(8.0f, BodySize * 0.82f);
				Ui()->DoLabel(&Label, Parsed.m_vCommands[Index].c_str(), BodySize, TEXTALIGN_ML, Props);
				if(pControls != nullptr && CanEditRows && Ui()->DoButtonLogic(&pControls->m_Row, 0, &Node, BUTTONFLAG_LEFT) && Ui()->MouseHovered(&Node))
				{
					State.m_EditActionIndex = (int)Index;
					State.m_EditActionSlot = ActiveSlot;
					State.m_EditActionRectValid = false;
					State.m_EditActionInput.Set(Parsed.m_vCommands[Index].c_str());
					State.m_EditActionInput.Activate(EInputPriority::UI);
					Ui()->SetActiveItem(&State.m_EditActionInput);
					State.m_Error.clear();
				}
				// 删除只用共享图标按钮：悬停底色、按下缩放和禁用斜线都由它统一处理。
				// 图标走危险色，配 Delete 提示，读起来是"删掉这一条"而不是"这行出错了"。
				bool RemoveClicked = false;
				if(pControls != nullptr)
				{
					// 删除图标走默认前景色（跟随主题 token），不再染危险红：
					// 红色让每条命令看起来像报错，删一条是常规操作。
					RemoveClicked = ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), &pControls->m_Remove, EQmIcon::TRASH, FontIcons::FONT_ICON_TRASH, RemoveAvailable ? 0 : -1, Remove, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) != 0;
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					// 只留删除图标上的提示：行里已经把命令完整显示出来了，再弹一条一样的
					// tooltip 会盖住第一行内容，纯属挡视线。
					GameClient()->m_Tooltips.DoToolTip(&pControls->m_Remove, &Remove, Localize("Delete"));
					// 点了就删：只记下要删的行号，循环结束后统一写回，一次点击最多删一条。
					if(RemoveClicked && CanRemoveNow && RemoveRow < 0)
						RemoveRow = (int)Index;
				}
			}
			// 行删除在这里落地：删一条只重组剩下的动作（删掉最后一条时整串为空，键位一并
			// 解绑），写回前先记撤销点，状态栏给"可撤销"提示。删除不再经过确认弹窗。
			if(!ReadOnly && RemoveRow >= 0 && RemoveRow < (int)Parsed.m_vCommands.size())
			{
				RecordBindUndo(State.m_vBindUndo, GameClient()->m_Binds, ActiveSlot);
				qm_bind_editor::SCommands Remaining = Parsed;
				Remaining.m_vCommands.erase(Remaining.m_vCommands.begin() + RemoveRow);
				const std::string Updated = qm_bind_editor::JoinCommands(Remaining.m_vCommands);
				GameClient()->m_Binds.Bind(ActiveSlot.m_Key, Updated.c_str(), false, ActiveSlot.m_ModifierMask);
				Ui()->ReleaseActiveTextInput(&State.m_EditActionInput);
				State.m_EditActionIndex = -1;
				State.m_EditActionRectValid = false;
				State.m_Error.clear();
				SetBindEditorDeletedStatus(State);
			}
		}

		Flow.Space(LineSpacing * 0.35f);
		RenderQmBindCommandEditor(Right, ActiveSlot, ReadOnly);

		// 两趟都排这一行：测量趟不排、外观趟排会让卡片内容高度算错。
		const bool HasStatusLine = !State.m_Error.empty() || !State.m_Status.empty();
		if(HasStatusLine)
		{
			Flow.Space(LineSpacing * 0.4f);
			CUIRect ErrorRow;
			if(Flow.Control(State.m_Error.empty() ? "bind-editor-status" : "bind-editor-error", LineHeight, ErrorRow))
			{
				if(State.m_Error.empty())
				{
					TextRender()->TextColor(ui_token::color::TEXT_TIP);
					Ui()->DoLabel(&ErrorRow, State.m_Status.c_str(), BodySize * 0.86f, TEXTALIGN_ML);
				}
				else
				{
					TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
					Ui()->DoLabel(&ErrorRow, State.m_Error.c_str(), BodySize * 0.86f, TEXTALIGN_ML);
				}
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	};
	RenderSettingsContentColumns("bind-editor-columns", Columns, s_Columns, LeftWidth, Stacked ? LineSpacing : 2.0f * Inset, Stacked,
		TextRender()->GlyphAtlasRevision(),
		[this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; },
		{RenderKeyboard, RenderActions});
	Content.y = Columns.y;
	Content.h = Columns.h;
}
