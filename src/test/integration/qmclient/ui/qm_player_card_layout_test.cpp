// 玩家页右侧信息卡的分区几何、图标复制命中区与剪贴板行为。
// 真实 CUi 执行控件绘制与命中测试；fake 只替换 GPU 提交、输入设备与客户端时钟。
#include <engine/keys.h>

#include <game/client/QmUi/QmPlayerCard.h>
#include <game/client/QmUi/QmPlayersMenu.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>

namespace
{
	constexpr int ACTION_COUNT = static_cast<int>(QmPlayersUi::EPlayerCardAction::COUNT);
	constexpr int COPY_COUNT = static_cast<int>(QmPlayersUi::EPlayerCardCopy::COUNT);
	constexpr float EPSILON = 0.001f;

	bool InsideRect(const CUIRect &Outer, const CUIRect &Inner)
	{
		return Inner.x >= Outer.x - EPSILON &&
		       Inner.y >= Outer.y - EPSILON &&
		       Inner.x + Inner.w <= Outer.x + Outer.w + EPSILON &&
		       Inner.y + Inner.h <= Outer.y + Outer.h + EPSILON;
	}

	class QmPlayerCardLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		std::array<CButtonContainer, COPY_COUNT> m_aCopyButtons{};
		std::array<CButtonContainer, ACTION_COUNT> m_aActionButtons{};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		// 与生产菜单同一条鼠标路径：屏幕坐标换算成设备像素，再推进一帧输入。
		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		IUiContext CardContext()
		{
			IUiContext Ctx = ui_widget::ControlContext(&m_Ui);
			Ctx.m_ScopeHash = MakeUiScopeHash("qm_player_card_test");
			Ctx.m_pTextRender = m_Ui.TextRender();
			Ctx.m_FrameDt = 1.0f / 60.0f;
			Ctx.m_UiScale = 1.0f;
			return Ctx;
		}
	};
}

TEST_F(QmPlayerCardLayout, SectionsStayInsideTheCardAtExtremeSizes)
{
	const CUIRect aCards[] = {
		{10, 20, 320, 420},
		{10, 20, 220, 180},
		{10, 20, 180, 90},
		{10, 20, 120, 40},
	};
	for(const CUIRect &Card : aCards)
	{
		const auto Layout = QmPlayersUi::PlayerCardLayout(Card, ACTION_COUNT);
		const CUIRect aSections[] = {Layout.m_Header, Layout.m_Actions, Layout.m_IconRow, Layout.m_Status, Layout.m_Details};
		for(const CUIRect &Section : aSections)
		{
			EXPECT_TRUE(InsideRect(Card, Section)) << "card " << Card.w << "x" << Card.h;
			EXPECT_GE(Section.w, 0.0f);
			EXPECT_GE(Section.h, 0.0f);
		}
		// 分区自上而下不重叠，顺序固定为头部 → 操作 → 图标 → 状态 → 细节。
		EXPECT_LE(Layout.m_Header.y + Layout.m_Header.h, Layout.m_Actions.y + EPSILON);
		EXPECT_LE(Layout.m_Actions.y + Layout.m_Actions.h, Layout.m_IconRow.y + EPSILON);
		EXPECT_LE(Layout.m_IconRow.y + Layout.m_IconRow.h, Layout.m_Status.y + EPSILON);
		EXPECT_LE(Layout.m_Status.y + Layout.m_Status.h, Layout.m_Details.y + EPSILON);
	}
}

TEST_F(QmPlayerCardLayout, HeightShortfallCollapsesTrailingSectionsInsteadOfOverflowing)
{
	// 只有头部高度可用时：头部分到全部高度，其余分区归零而不是翻出卡片。
	const CUIRect Card{0, 0, 240, ui_token::player_card::HEADER_HEIGHT};
	const auto Layout = QmPlayersUi::PlayerCardLayout(Card, ACTION_COUNT);
	EXPECT_FLOAT_EQ(Layout.m_Header.h, ui_token::player_card::HEADER_HEIGHT);
	EXPECT_FLOAT_EQ(Layout.m_Actions.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_IconRow.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Status.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Details.h, 0.0f);
}

TEST_F(QmPlayerCardLayout, ListAndCardKeepExactlyOneTokenGap)
{
	const float Gap = ui_token::player_card::LIST_CARD_GAP;

	const CUIRect Wide{10, 20, 900, 400};
	const auto WideLayout = QmPlayersUi::Panels(Wide);
	EXPECT_TRUE(InsideRect(Wide, WideLayout.m_List));
	EXPECT_TRUE(InsideRect(Wide, WideLayout.m_Details));
	EXPECT_FLOAT_EQ(WideLayout.m_Details.x - (WideLayout.m_List.x + WideLayout.m_List.w), Gap);
	EXPECT_FLOAT_EQ(WideLayout.m_List.y, WideLayout.m_Details.y);
	EXPECT_FLOAT_EQ(WideLayout.m_List.h, WideLayout.m_Details.h);

	const CUIRect Narrow{10, 20, 400, 300};
	const auto NarrowLayout = QmPlayersUi::Panels(Narrow);
	EXPECT_TRUE(InsideRect(Narrow, NarrowLayout.m_List));
	EXPECT_TRUE(InsideRect(Narrow, NarrowLayout.m_Details));
	EXPECT_FLOAT_EQ(NarrowLayout.m_Details.y - (NarrowLayout.m_List.y + NarrowLayout.m_List.h), Gap);
	EXPECT_FLOAT_EQ(NarrowLayout.m_List.x, NarrowLayout.m_Details.x);
	EXPECT_FLOAT_EQ(NarrowLayout.m_List.w, NarrowLayout.m_Details.w);
}

TEST_F(QmPlayerCardLayout, IconRowButtonsAreSquareAndRightAligned)
{
	const CUIRect Row{40, 40, 200, 24};
	const auto Layout = QmPlayersUi::PlayerCardIconRowLayout(Row, COPY_COUNT);
	for(int Index = 0; Index < COPY_COUNT; ++Index)
	{
		const CUIRect &Cell = Layout.m_aCells[Index];
		EXPECT_TRUE(InsideRect(Row, Cell));
		EXPECT_FLOAT_EQ(Cell.w, Cell.h);
		EXPECT_FLOAT_EQ(Cell.w, ui_token::player_card::ICON_BUTTON_SIZE);
		EXPECT_FLOAT_EQ(Cell.y, Row.y + (Row.h - Cell.h) * 0.5f);
		if(Index + 1 < COPY_COUNT)
			EXPECT_FLOAT_EQ(Layout.m_aCells[Index + 1].x - (Cell.x + Cell.w), ui_token::player_card::ICON_BUTTON_GAP);
	}
	EXPECT_FLOAT_EQ(Layout.m_aCells[COPY_COUNT - 1].x + Layout.m_aCells[COPY_COUNT - 1].w, Row.x + Row.w);
}

TEST_F(QmPlayerCardLayout, IconRowShrinksToStayInsideANarrowRow)
{
	const CUIRect Row{40, 40, 50, 24};
	const auto Layout = QmPlayersUi::PlayerCardIconRowLayout(Row, COPY_COUNT);
	for(int Index = 0; Index < COPY_COUNT; ++Index)
	{
		const CUIRect &Cell = Layout.m_aCells[Index];
		EXPECT_TRUE(InsideRect(Row, Cell));
		EXPECT_FLOAT_EQ(Cell.w, Cell.h);
		EXPECT_LT(Cell.w, ui_token::player_card::ICON_BUTTON_SIZE);
	}
}

TEST_F(QmPlayerCardLayout, ActionGridPutsTheOddButtonOnAFullWidthLastRow)
{
	const CUIRect Actions{0, 0, 200, QmPlayersUi::PlayerCardActionsHeight(ACTION_COUNT)};
	const auto Grid = QmPlayersUi::PlayerCardActionGrid(Actions, ACTION_COUNT);
	EXPECT_EQ(Grid.m_Rows, 3);
	const float HalfWidth = (Actions.w - ui_token::player_card::ACTION_GAP) / 2.0f;
	for(int Index = 0; Index < ACTION_COUNT; ++Index)
	{
		EXPECT_TRUE(InsideRect(Actions, Grid.m_aCells[Index]));
		if(Index < 4)
			EXPECT_FLOAT_EQ(Grid.m_aCells[Index].w, HalfWidth);
	}
	EXPECT_FLOAT_EQ(Grid.m_aCells[4].w, Actions.w);
	EXPECT_FLOAT_EQ(Grid.m_aCells[0].y, Actions.y);
	EXPECT_FLOAT_EQ(Grid.m_aCells[2].y, Actions.y + ui_token::player_card::ACTION_HEIGHT + ui_token::player_card::ACTION_GAP);
}

TEST_F(QmPlayerCardLayout, ClickingCopyNameWritesTheClipboardAndReturnsItsAction)
{
	const IUiContext Ctx = CardContext();
	// 行占满屏幕宽度，图标按钮靠右；取第一个（复制名字）按钮的中心作为点击位置。
	const vec2 Screen = m_Ui.Screen()->Size();
	const CUIRect RowRect{Screen.x, Screen.y * 0.5f, 200.0f, ui_token::player_card::ICON_BUTTON_SIZE};
	const auto Layout = QmPlayersUi::PlayerCardIconRowLayout(RowRect, COPY_COUNT);
	const vec2 ClickPosition = Layout.m_aCells[0].Center();

	const QmPlayersUi::SPlayerCardCopySpec aSpecs[] = {
		{EQmIcon::USER, FontIcons::FONT_ICON_USER, "Player", "Copy name"},
		{EQmIcon::USERS, FontIcons::FONT_ICON_USERS, "Clan", "Copy clan"},
		{EQmIcon::COPY, FontIcons::FONT_ICON_COPY, "default", "Copy skin ID"},
	};

	Frame({1, 1}, false);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardCopy::NONE));
	Frame(ClickPosition, true);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardCopy::NONE));
	EXPECT_TRUE(m_Input.m_Clipboard.empty());
	Frame(ClickPosition, false);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardCopy::NAME));
	EXPECT_EQ(m_Input.m_Clipboard, "Player");
}

TEST_F(QmPlayerCardLayout, EmptyCopyTextDisablesTheButtonAndKeepsTheClipboard)
{
	const IUiContext Ctx = CardContext();
	const vec2 Screen = m_Ui.Screen()->Size();
	const CUIRect RowRect{Screen.x, Screen.y * 0.5f, 200.0f, ui_token::player_card::ICON_BUTTON_SIZE};
	const auto Layout = QmPlayersUi::PlayerCardIconRowLayout(RowRect, COPY_COUNT);
	const vec2 ClickPosition = Layout.m_aCells[0].Center();
	m_Input.m_Clipboard = "untouched";

	// 采集为空（例如流式模式下隐藏皮肤）时按钮置灰：点了不返回动作，也不覆盖剪贴板。
	const QmPlayersUi::SPlayerCardCopySpec aSpecs[] = {
		{EQmIcon::USER, FontIcons::FONT_ICON_USER, "", "Copy name"},
		{EQmIcon::USERS, FontIcons::FONT_ICON_USERS, "Clan", "Copy clan"},
		{EQmIcon::COPY, FontIcons::FONT_ICON_COPY, "default", "Copy skin ID"},
	};

	Frame({1, 1}, false);
	QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT);
	Frame(ClickPosition, true);
	QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT);
	Frame(ClickPosition, false);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardCopies(Ctx, m_aCopyButtons.data(), RowRect, aSpecs, COPY_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardCopy::NONE));
	EXPECT_EQ(m_Input.m_Clipboard, "untouched");
}

TEST_F(QmPlayerCardLayout, ClickingAnActionButtonReportsThatAction)
{
	const IUiContext Ctx = CardContext();
	const CUIRect Actions{0, 40, 240, QmPlayersUi::PlayerCardActionsHeight(ACTION_COUNT)};
	const auto Grid = QmPlayersUi::PlayerCardActionGrid(Actions, ACTION_COUNT);
	const vec2 ClickPosition = Grid.m_aCells[static_cast<int>(QmPlayersUi::EPlayerCardAction::FOLLOW)].Center();

	const QmPlayersUi::SPlayerCardActionSpec aSpecs[] = {
		{"Friends", false, true},
		{"Follow server", false, true},
		{"Mute", false, true},
		{"Mute emoticons", false, true},
		{"Copy skin", false, true},
	};

	Frame({1, 1}, false);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardActions(Ctx, m_aActionButtons.data(), Actions, aSpecs, ACTION_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardAction::NONE));
	Frame(ClickPosition, true);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardActions(Ctx, m_aActionButtons.data(), Actions, aSpecs, ACTION_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardAction::NONE));
	Frame(ClickPosition, false);
	EXPECT_EQ(static_cast<int>(QmPlayersUi::PlayerCardActions(Ctx, m_aActionButtons.data(), Actions, aSpecs, ACTION_COUNT)), static_cast<int>(QmPlayersUi::EPlayerCardAction::FOLLOW));
}
