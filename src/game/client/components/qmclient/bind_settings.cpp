#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <deque>
#include <initializer_list>
#include <iterator>
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
	};

	struct SBindEditorUiState
	{
		struct SActionControls
		{
			CButtonContainer m_Remove;
			char m_LabelId = 0;
		};

		CBindSlot m_Selected = CBindSlot(KEY_A, KeyModifier::NONE);
		int m_ModifierMask = KeyModifier::NONE;
		bool m_HasSelected = true;
		bool m_CapturePending = false;
		bool m_KeySelectionHeld = false;
		CUIRect m_KeyboardRect{};
		int m_KeyButtonCount = 0;
		std::array<CButtonContainer, 128> m_aKeyButtons{};
		std::array<CButtonContainer, 4> m_aModifierButtons{};
		std::deque<SActionControls> m_vActionControls;
		CButtonContainer m_CaptureButton;
		CButtonContainer m_ClearCaptureButton;
		std::string m_Error;
	};

	SBindEditorUiState &BindEditorState()
	{
		static SBindEditorUiState s_State;
		return s_State;
	}

	int ModifierBit(const int Modifier)
	{
		return 1 << Modifier;
	}

	void SetBindEditorError(SBindEditorUiState &State, const char *pMessage)
	{
		State.m_Error = pMessage != nullptr ? pMessage : "";
	}
}

void CMenus::RenderSettingsQmClientBindDeck(CUIRect MainView, const bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_bind", UiScale);
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
	const uint64_t CardLayoutRevision = str_quickhash("qmclient-bind") ^ GameClient()->m_Binds.Revision();
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [Context](std::vector<SSettingsCardDefinition> &vCards) {
		SSettingsCardDefinition Definition;
		if(qm_card_catalog::BuildBindCard(Context, Definition))
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
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	qm_card_order::CModel &OrderModel = ReadOnly ? s_PrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_PrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "bind", DefinitionsRevision, BuildDefinitions, OrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
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

	CUIRect Left, Right;
	const bool Stacked = Content.w < 760.0f;
	if(Stacked)
		Left = Right = Content;
	else
		Content.VSplitLeft(std::min(Content.w * 0.72f, Content.w - 220.0f), &Left, &Right);
	Right.VMargin(LineSpacing * 0.75f, &Right);
	Left.VMargin(LineSpacing * 0.75f, &Left);
	const CUIRect KeyboardStart = Left;

	// 按键名沿用键帽标识，避免翻译成普通词义。
	static constexpr std::array<const char *, 4> s_apModifierLabels = {"Ctrl", "Alt", "Shift", "Win"};
	static constexpr std::array<int, 4> s_aModifierIds = {KeyModifier::CTRL, KeyModifier::ALT, KeyModifier::SHIFT, KeyModifier::GUI};
	CUIRect ModifierRow;
	Left.HSplitTop(LineHeight, &ModifierRow, &Left);
	const float ModifierButtonWidth = (ModifierRow.w - LineSpacing * 3.0f) / 4.0f;
	for(size_t Index = 0; Index < s_apModifierLabels.size(); ++Index)
	{
		CUIRect Button;
		ModifierRow.VSplitLeft(ModifierButtonWidth, &Button, &ModifierRow);
		if(Index + 1 < s_apModifierLabels.size())
			ModifierRow.VSplitLeft(LineSpacing, nullptr, &ModifierRow);
		const bool Checked = (State.m_ModifierMask & ModifierBit(s_aModifierIds[Index])) != 0;
		if(!ReadOnly && DoButton_Menu(&State.m_aModifierButtons[Index], s_apModifierLabels[Index], Checked, &Button))
		{
			State.m_ModifierMask ^= ModifierBit(s_aModifierIds[Index]);
			if(State.m_HasSelected)
				State.m_Selected.m_ModifierMask = State.m_ModifierMask;
			State.m_Error.clear();
		}
	}

	Left.HSplitTop(LineSpacing, nullptr, &Left);
	State.m_KeyButtonCount = 0;
	const bool KeyCaptureActive = GameClient()->m_KeyBinder.IsActive();
	if(!ReadOnly && !Ui()->MouseInside(&State.m_KeyboardRect))
		State.m_KeySelectionHeld = false;
	const bool FollowHover = !ReadOnly && !KeyCaptureActive && !State.m_CapturePending && !State.m_KeySelectionHeld && !Ui()->IsPopupOpen() && CLineInput::GetActiveInput() == nullptr;
	const auto DrawKey = [this, &State, ReadOnly, BodySize, FollowHover](const SBindKeyTile &Tile, const CUIRect &Rect) {
		if(Tile.m_Spacer)
			return;
		if(State.m_KeyButtonCount >= (int)State.m_aKeyButtons.size())
			return;
		const CBindSlot Slot(Tile.m_Key, State.m_ModifierMask);
		const bool Hovered = Ui()->MouseHovered(&Rect);
		if(Hovered && FollowHover && State.m_Selected != Slot)
		{
			State.m_Selected = Slot;
			State.m_HasSelected = true;
			State.m_Error.clear();
		}
		const bool Selected = State.m_HasSelected && State.m_Selected == Slot;

		const auto *pImage = Tile.m_pImage != nullptr ? FindMenuImage(Tile.m_pImage) : nullptr;
		DrawRoundedSurface(Ui(), Rect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), 4.0f);
		if(pImage != nullptr)
		{
			Graphics()->TextureSet(Hovered || Selected ? pImage->m_OrgTexture : pImage->m_GreyTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			const float Size = std::min(Rect.w, Rect.h);
			const IGraphics::CQuadItem Quad(Rect.x + (Rect.w - Size) * 0.5f, Rect.y + (Rect.h - Size) * 0.5f, Size, Size);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		else
		{
			Ui()->DoLabel(&Rect, Localize(Tile.m_pLabel, Tile.m_pContext != nullptr ? Tile.m_pContext : ""), std::min(BodySize, Rect.h * CUi::ms_FontmodHeight), TEXTALIGN_MC);
		}
		if(Selected)
			DrawRoundedSurface(Ui(), Rect, ColorRGBA(0.2f, 0.75f, 1.0f, 0.18f), ColorRGBA(0.3f, 0.85f, 1.0f, 0.75f), 4.0f, 1.0f);
		else if(Hovered)
			DrawRoundedSurface(Ui(), Rect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), ColorRGBA(), 4.0f);
		if(GameClient()->m_Binds.Get(Slot)[0] != '\0')
		{
			CUIRect Indicator;
			Rect.HSplitTop(2.0f, &Indicator, nullptr);
			Indicator.Draw(ColorRGBA(0.3f, 0.78f, 0.65f, 0.9f), IGraphics::CORNER_T, 2.0f);
		}
		const int ButtonResult = ReadOnly ? 0 : Ui()->DoButtonLogic(&State.m_aKeyButtons[State.m_KeyButtonCount], Selected ? 1 : 0, &Rect, BUTTONFLAG_LEFT);
		++State.m_KeyButtonCount;
		if(ButtonResult != 0 && !ReadOnly)
		{
			State.m_Selected = Slot;
			State.m_HasSelected = true;
			// 点击后穿过其他键位前往编辑面板时，保留用户明确选中的键。
			State.m_KeySelectionHeld = true;
			State.m_Error.clear();
		}
	};

	const auto RenderKeyRow = [&](const std::initializer_list<SBindKeyTile> Tiles) {
		if(Tiles.size() == 0)
			return;
		CUIRect Row;
		Left.HSplitTop(LineHeight, &Row, &Left);
		float TotalUnits = 0.0f;
		for(const SBindKeyTile &Tile : Tiles)
			TotalUnits += Tile.m_Units;
		const float UnitGap = LineSpacing * 0.45f;
		const float Unit = std::max(1.0f, (Row.w - UnitGap * (Tiles.size() - 1)) / std::max(1.0f, TotalUnits));
		float X = Row.x;
		for(const SBindKeyTile &Tile : Tiles)
		{
			CUIRect TileRect{X, Row.y, std::max(1.0f, Unit * Tile.m_Units - UnitGap), Row.h};
			DrawKey(Tile, TileRect);
			X += Unit * Tile.m_Units;
		}
		Left.HSplitTop(LineSpacing, nullptr, &Left);
	};
	const float KeyboardRowHeight = LineHeight * 1.25f;
	const float KeyboardUnitGap = LineSpacing * 0.45f;
	constexpr float KeyboardColumns = 23.0f;
	const float KeyboardUnit = std::max(1.0f, Left.w / KeyboardColumns);
	const auto RenderPhysicalKeyRow = [&](const std::initializer_list<SBindKeyTile> Tiles) {
		if(Tiles.size() == 0)
			return;
		CUIRect Row;
		Left.HSplitTop(KeyboardRowHeight, &Row, &Left);
		float X = Row.x;
		for(const SBindKeyTile &Tile : Tiles)
		{
			const float TileWidth = std::max(1.0f, KeyboardUnit * Tile.m_Units - (Tile.m_Spacer ? 0.0f : KeyboardUnitGap));
			CUIRect TileRect{X, Row.y, TileWidth, Row.h};
			DrawKey(Tile, TileRect);
			X += KeyboardUnit * Tile.m_Units;
		}
		Left.HSplitTop(LineSpacing, nullptr, &Left);
	};

	// 按真实 104 键键盘的行列排列，导航区和数字区按物理位置插入空白间隔。
	RenderPhysicalKeyRow({{KEY_ESCAPE, "Esc", "keyboard_escape"}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_F1, "F1", "keyboard_f1"}, {KEY_F2, "F2", "keyboard_f2"}, {KEY_F3, "F3", "keyboard_f3"}, {KEY_F4, "F4", "keyboard_f4"}, {KEY_UNKNOWN, nullptr, nullptr, 0.35f, nullptr, true}, {KEY_F5, "F5", "keyboard_f5"}, {KEY_F6, "F6", "keyboard_f6"}, {KEY_F7, "F7", "keyboard_f7"}, {KEY_F8, "F8", "keyboard_f8"}, {KEY_UNKNOWN, nullptr, nullptr, 0.35f, nullptr, true}, {KEY_F9, "F9", "keyboard_f9"}, {KEY_F10, "F10", "keyboard_f10"}, {KEY_F11, "F11", "keyboard_f11"}, {KEY_F12, "F12", "keyboard_f12"}, {KEY_UNKNOWN, nullptr, nullptr, 1.3f, nullptr, true}, {KEY_PRINTSCREEN, "Prt", "keyboard_printscreen"}, {KEY_SCROLLLOCK, "Scr", "keyboard_scroll_lock"}, {KEY_PAUSE, "Pau", "keyboard_pause"}});
	RenderPhysicalKeyRow({{KEY_GRAVE, "`", "keyboard_tilde"}, {KEY_1, "1", "keyboard_1"}, {KEY_2, "2", "keyboard_2"}, {KEY_3, "3", "keyboard_3"}, {KEY_4, "4", "keyboard_4"}, {KEY_5, "5", "keyboard_5"}, {KEY_6, "6", "keyboard_6"}, {KEY_7, "7", "keyboard_7"}, {KEY_8, "8", "keyboard_8"}, {KEY_9, "9", "keyboard_9"}, {KEY_0, "0", "keyboard_0"}, {KEY_MINUS, "-", "keyboard_minus"}, {KEY_EQUALS, "=", "keyboard_equals"}, {KEY_BACKSPACE, "Backspace", "keyboard_backspace", 2.0f}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_INSERT, "Ins", "keyboard_insert"}, {KEY_HOME, "Home", "keyboard_home"}, {KEY_PAGEUP, "PgU", "keyboard_page_up"}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_NUMLOCKCLEAR, "Num", "keyboard_numlock"}, {KEY_KP_DIVIDE, "/", nullptr}, {KEY_KP_MULTIPLY, "*", nullptr}, {KEY_KP_MINUS, "-", "keyboard_minus"}});
	RenderPhysicalKeyRow({{KEY_TAB, "Tab", "keyboard_tab", 1.5f}, {KEY_Q, "Q", "keyboard_q"}, {KEY_W, "W", "keyboard_w"}, {KEY_E, "E", "keyboard_e"}, {KEY_R, "R", "keyboard_r"}, {KEY_T, "T", "keyboard_t"}, {KEY_Y, "Y", "keyboard_y"}, {KEY_U, "U", "keyboard_u"}, {KEY_I, "I", "keyboard_i"}, {KEY_O, "O", "keyboard_o"}, {KEY_P, "P", "keyboard_p"}, {KEY_LEFTBRACKET, "[", "keyboard_bracket_open"}, {KEY_RIGHTBRACKET, "]", "keyboard_bracket_close"}, {KEY_BACKSLASH, "\\", "keyboard_slash_back"}, {KEY_UNKNOWN, nullptr, nullptr, 0.95f, nullptr, true}, {KEY_DELETE, "Del", "keyboard_delete"}, {KEY_END, "End", "keyboard_end"}, {KEY_PAGEDOWN, "PgD", "keyboard_page_down"}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_KP_7, "7", nullptr}, {KEY_KP_8, "8", nullptr}, {KEY_KP_9, "9", nullptr}, {KEY_KP_PLUS, "+", "keyboard_numpad_plus"}});
	RenderPhysicalKeyRow({{KEY_CAPSLOCK, "Caps", "keyboard_capslock", 1.75f}, {KEY_A, "A", "keyboard_a"}, {KEY_S, "S", "keyboard_s"}, {KEY_D, "D", "keyboard_d"}, {KEY_F, "F", "keyboard_f"}, {KEY_G, "G", "keyboard_g"}, {KEY_H, "H", "keyboard_h"}, {KEY_J, "J", "keyboard_j"}, {KEY_K, "K", "keyboard_k"}, {KEY_L, "L", "keyboard_l"}, {KEY_SEMICOLON, ";", "keyboard_semicolon"}, {KEY_APOSTROPHE, "'", "keyboard_apostrophe"}, {KEY_RETURN, "Enter", "keyboard_return", 2.0f}, {KEY_UNKNOWN, nullptr, nullptr, 3.7f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_KP_4, "4", nullptr}, {KEY_KP_5, "5", nullptr}, {KEY_KP_6, "6", nullptr}});
	RenderPhysicalKeyRow({{KEY_LSHIFT, "Shift", "keyboard_shift", 2.25f}, {KEY_Z, "Z", "keyboard_z"}, {KEY_X, "X", "keyboard_x"}, {KEY_C, "C", "keyboard_c"}, {KEY_V, "V", "keyboard_v"}, {KEY_B, "B", "keyboard_b"}, {KEY_N, "N", "keyboard_n"}, {KEY_M, "M", "keyboard_m"}, {KEY_COMMA, ",", "keyboard_comma"}, {KEY_PERIOD, ".", "keyboard_period"}, {KEY_SLASH, "/", "keyboard_slash_forward"}, {KEY_RSHIFT, "Shift", "keyboard_shift", 2.25f}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true}, {KEY_UP, "Up", "keyboard_arrow_up"}, {KEY_UNKNOWN, nullptr, nullptr, 1.0f, nullptr, true}, {KEY_UNKNOWN, nullptr, nullptr, 0.95f, nullptr, true}, {KEY_KP_1, "1", nullptr}, {KEY_KP_2, "2", nullptr}, {KEY_KP_3, "3", nullptr}, {KEY_KP_ENTER, "Ent", "keyboard_numpad_enter"}});
	RenderPhysicalKeyRow({{KEY_LCTRL, "Ctrl", "keyboard_ctrl", 1.25f}, {KEY_LGUI, "Win", "keyboard_win", 1.25f}, {KEY_LALT, "Alt", "keyboard_alt", 1.25f}, {KEY_SPACE, "Space", "keyboard_space", 6.0f}, {KEY_RALT, "Alt", "keyboard_alt", 1.25f}, {KEY_RGUI, "Win", "keyboard_win", 1.25f}, {KEY_MENU, Localizable("Menu", "Bind keyboard key"), nullptr, 1.25f}, {KEY_RCTRL, "Ctrl", "keyboard_ctrl", 1.25f}, {KEY_UNKNOWN, nullptr, nullptr, 0.45f, nullptr, true}, {KEY_LEFT, "Left", "keyboard_arrow_left"}, {KEY_DOWN, "Down", "keyboard_arrow_down"}, {KEY_RIGHT, "Right", "keyboard_arrow_right"}, {KEY_UNKNOWN, nullptr, nullptr, 0.95f, nullptr, true}, {KEY_KP_0, "0", nullptr, 2.0f}, {KEY_KP_PERIOD, ".", nullptr}});

	Left.HSplitTop(LineSpacing * 0.65f, nullptr, &Left);
	CUIRect MouseHeader;
	Left.HSplitTop(LineHeight, &MouseHeader, &Left);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_BIND, QMCLIENT_SETTINGS_TAB_BIND, "qm-bind-editor-mouse", &MouseHeader, Localize("Mouse"), BodySize, TEXTALIGN_ML, {}, (int)MouseHeader.w);
	RenderKeyRow({{KEY_MOUSE_1, "Mouse 1", "mouse_left", 1.4f}, {KEY_MOUSE_2, "Mouse 2", "mouse_right", 1.4f}, {KEY_MOUSE_3, "Mouse 3", "mouse_scroll", 1.4f}, {KEY_MOUSE_4, "Mouse 4", "mouse_side_back", 1.4f}, {KEY_MOUSE_5, "Mouse 5", "mouse_side_forward", 1.4f}, {KEY_MOUSE_WHEEL_UP, "Wheel up", "mouse_scroll_up", 1.4f}, {KEY_MOUSE_WHEEL_DOWN, "Wheel down", "mouse_scroll_down", 1.4f}});
	RenderKeyRow({{KEY_MOUSE_WHEEL_LEFT, "Wheel left", "mouse_horizontal", 1.4f}, {KEY_MOUSE_WHEEL_RIGHT, "Wheel right", "mouse_horizontal", 1.4f}});
	if(!ReadOnly)
		State.m_KeyboardRect = {KeyboardStart.x, KeyboardStart.y, KeyboardStart.w, Left.y - KeyboardStart.y};

	if(Stacked)
	{
		Right = Left;
		Right.HSplitTop(LineSpacing, nullptr, &Right);
	}
	CBindSlot ActiveSlot = State.m_Selected;
	if(ActiveSlot.m_Key == KEY_UNKNOWN)
		ActiveSlot = CBindSlot(KEY_A, State.m_ModifierMask);

	char aActiveKeyName[128];
	GameClient()->m_Binds.GetKeyBindName(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask, aActiveKeyName, sizeof(aActiveKeyName));
	Right.HSplitTop(LineHeight, nullptr, &Right);
	CUIRect SelectedLabel;
	Right.HSplitTop(LineHeight, &SelectedLabel, &Right);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_BIND, QMCLIENT_SETTINGS_TAB_BIND, "qm-bind-editor-selected-key", &SelectedLabel, aActiveKeyName, BodySize, TEXTALIGN_ML, {}, (int)SelectedLabel.w);
	CUIRect CaptureRow;
	Right.HSplitTop(LineHeight, &CaptureRow, &Right);
	const CBindSlot PreviousSlot = ActiveSlot;
	const std::string PreviousBinding = GameClient()->m_Binds.Get(PreviousSlot.m_Key, PreviousSlot.m_ModifierMask);
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
			GameClient()->m_Binds.Bind(PreviousSlot.m_Key, "", false, PreviousSlot.m_ModifierMask);
			State.m_Error.clear();
		}
		else
		{
			char aNewKeyName[128];
			GameClient()->m_Binds.GetKeyBindName(CaptureResult.m_Bind.m_Key, CaptureResult.m_Bind.m_ModifierMask, aNewKeyName, sizeof(aNewKeyName));
			if(qm_bind_editor::FitsConfigLine(aNewKeyName, PreviousBinding))
			{
				GameClient()->m_Binds.Bind(PreviousSlot.m_Key, "", false, PreviousSlot.m_ModifierMask);
				GameClient()->m_Binds.Bind(CaptureResult.m_Bind.m_Key, PreviousBinding.c_str(), false, CaptureResult.m_Bind.m_ModifierMask);
				State.m_Selected = CaptureResult.m_Bind;
				State.m_ModifierMask = CaptureResult.m_Bind.m_ModifierMask;
				State.m_HasSelected = true;
				State.m_KeySelectionHeld = true;
				State.m_Error.clear();
			}
			else
				SetBindEditorError(State, Localize("This bind is already over the local config line limit."));
		}
		ActiveSlot = State.m_Selected;
		GameClient()->m_Binds.GetKeyBindName(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask, aActiveKeyName, sizeof(aActiveKeyName));
	}
	Right.HSplitTop(LineSpacing, nullptr, &Right);

	const std::string ExistingBinding = GameClient()->m_Binds.Get(ActiveSlot.m_Key, ActiveSlot.m_ModifierMask);
	qm_bind_editor::SCommands Parsed = qm_bind_editor::SplitCommands(ExistingBinding);
	if(!ReadOnly && !Parsed.m_Complete)
		SetBindEditorError(State, Localize("This bind contains an unfinished quote and cannot be edited safely."));

	CUIRect BoundHeader;
	Right.HSplitTop(LineHeight, &BoundHeader, &Right);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_BIND, QMCLIENT_SETTINGS_TAB_BIND, "qm-bind-editor-current", &BoundHeader, Localize("Bound functions"), BodySize, TEXTALIGN_ML, {}, (int)BoundHeader.w);
	Right.HSplitTop(LineSpacing * 0.4f, nullptr, &Right);
	if(!ReadOnly)
		State.m_vActionControls.resize(Parsed.m_vCommands.size());
	if(Parsed.m_vCommands.empty())
	{
		CUIRect EmptyRow;
		Right.HSplitTop(LineHeight, &EmptyRow, &Right);
		Ui()->DoLabel(&EmptyRow, Localize("No functions bound"), BodySize, TEXTALIGN_ML);
		Right.HSplitTop(LineSpacing, nullptr, &Right);
	}
	else
	{
		for(size_t Index = 0; Index < Parsed.m_vCommands.size(); ++Index)
		{
			CUIRect Row, Remove;
			Right.HSplitTop(LineHeight, &Row, &Right);
			Row.VSplitRight(LineHeight, &Row, &Remove);
			const bool CanRemove = !ReadOnly && Parsed.m_Complete && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive();
			if(Index < State.m_vActionControls.size() && Ui()->DoButton_QmIcon(&State.m_vActionControls[Index].m_Remove, EQmIcon::TRASH, FontIcons::FONT_ICON_TRASH, CanRemove ? 0 : -1, &Remove, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanRemove)
			{
				Parsed.m_vCommands.erase(Parsed.m_vCommands.begin() + Index);
				const std::string Updated = qm_bind_editor::JoinCommands(Parsed.m_vCommands);
				GameClient()->m_Binds.Bind(ActiveSlot.m_Key, Updated.c_str(), false, ActiveSlot.m_ModifierMask);
				break;
			}
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(1.0f, Row.w - LineSpacing);
			Props.m_EllipsisAtEnd = true;
			Props.m_MinimumFontSize = std::max(8.0f, BodySize * 0.82f);
			Ui()->DoLabel(&Row, Parsed.m_vCommands[Index].c_str(), BodySize, TEXTALIGN_ML, Props);
			if(!ReadOnly && Index < State.m_vActionControls.size())
			{
				GameClient()->m_Tooltips.DoToolTip(&State.m_vActionControls[Index].m_Remove, &Remove, Localize("Delete"));
				GameClient()->m_Tooltips.DoToolTipForRect(&State.m_vActionControls[Index].m_LabelId, &Row, Parsed.m_vCommands[Index].c_str(), 400.0f);
			}
			Right.HSplitTop(LineSpacing * 0.35f, nullptr, &Right);
		}
	}

	Right.HSplitTop(LineSpacing * 0.35f, nullptr, &Right);
	RenderQmBindCommandEditor(Right, ActiveSlot, ReadOnly);

	if(!State.m_Error.empty())
	{
		Right.HSplitTop(LineSpacing * 0.4f, nullptr, &Right);
		CUIRect ErrorRow;
		Right.HSplitTop(LineHeight, &ErrorRow, &Right);
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
		Ui()->DoLabel(&ErrorRow, State.m_Error.c_str(), BodySize * 0.86f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	CommitSettingsColumnContentFlow(Content, Left, Right);
}
