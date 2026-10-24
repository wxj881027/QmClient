// 卡片说明面板的三个面板级行为：ⓘ 在绘制阶段开面板、目录跳转真的滚动、
// Esc 先退编辑再关面板。只隔离 GPU 与输入设备，使用真实 CUi、真实排版与真实弹层栈。
#include <engine/keys.h>

#include <game/client/QmUi/QmCardWikiPanel.h>
#include <game/client/QmUi/QmWikiDocs.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmWikiPanel : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		// 面板入口要求文字渲染与存储；测试不碰用户目录，存储留空走内置正文。
		IUiContext MakeContext()
		{
			IUiContext Ctx = ui_widget::ControlContext(&m_Ui);
			Ctx.m_pTextRender = m_Ui.TextRender();
			Ctx.m_UiScale = 1.0f;
			return Ctx;
		}

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		// 面板状态是文件级静态量，用例之间必须清掉，否则下一个用例的 IsOpen
		// 会去问上一个用例已经销毁的 CUi。
		void TearDown() override
		{
			qm_wiki_panel_test::ResetState();
			qm_ui_test::CRealUiFixture::TearDown();
		}

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		// 真实一帧：先推进输入与 UI，再渲染弹层；面板的绘制与命中也在这里发生。
		void Render(vec2 Position = {0.0f, 0.0f}, bool Pressed = false)
		{
			Frame(Position, Pressed);
			m_Ui.RenderPopupMenus();
		}

		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}
	};
}

TEST_F(QmWikiPanel, InfoButtonOpensPanelInTheDrawPhaseAndPanelSurvivesLaterFrames)
{
	CUIRect InfoButton{40.0f, 40.0f, 16.0f, 16.0f};
	const vec2 Center{InfoButton.x + InfoButton.w * 0.5f, InfoButton.y + InfoButton.h * 0.5f};
	IUiContext Ctx = MakeContext();

	// 悬停一帧建立 hot item；按下那一帧只登记 ActiveItem，释放才是点击。
	Render(Center, false);
	Frame(Center, true);
	EXPECT_FALSE(DoCardWikiInfoButton(Ctx, "deck:appearance-chat-messages", "Chat messages", InfoButton));
	Frame(Center, false);
	EXPECT_TRUE(DoCardWikiInfoButton(Ctx, "deck:appearance-chat-messages", "Chat messages", InfoButton));
	ASSERT_TRUE(qm_wiki_panel_test::IsOpen());

	// 回归：面板打开后不能被后续帧逐帧关掉（折叠/展开卡片的绘制路径曾经踩过）。
	for(int Index = 0; Index < 3; ++Index)
	{
		Render({0.0f, 0.0f}, false);
		EXPECT_TRUE(qm_wiki_panel_test::IsOpen()) << "panel closed on frame " << Index;
	}
}

TEST_F(QmWikiPanel, InfoButtonIsInertWithoutAWikiEntry)
{
	CUIRect InfoButton{40.0f, 40.0f, 16.0f, 16.0f};
	const vec2 Center{InfoButton.x + InfoButton.w * 0.5f, InfoButton.y + InfoButton.h * 0.5f};
	IUiContext Ctx = MakeContext();
	Render(Center, false);
	Frame(Center, true);
	EXPECT_FALSE(DoCardWikiInfoButton(Ctx, "deck:this-card-has-no-wiki-entry", "No entry", InfoButton));
	Frame(Center, false);
	EXPECT_FALSE(DoCardWikiInfoButton(Ctx, "deck:this-card-has-no-wiki-entry", "No entry", InfoButton));
	EXPECT_FALSE(qm_wiki_panel_test::IsOpen());
}

TEST_F(QmWikiPanel, EscapeLeavesEditingFirstAndClosesThePanelOnlyWhenReading)
{
	IUiContext Ctx = MakeContext();
	OpenCardWikiPopup(Ctx, "deck:appearance-chat-messages", "Chat messages");
	Render();
	ASSERT_TRUE(qm_wiki_panel_test::IsOpen());
	ASSERT_TRUE(qm_wiki_panel_test::BeginEditing());
	Render();
	ASSERT_TRUE(qm_wiki_panel_test::IsEditing());

	// 编辑态：Esc 只退出编辑，面板留着（草稿不丢）。
	Key(KEY_ESCAPE);
	Render();
	EXPECT_FALSE(qm_wiki_panel_test::IsEditing());
	EXPECT_TRUE(qm_wiki_panel_test::IsOpen());

	// 阅读态：同一个 Esc 由弹层栈处理，关掉面板。
	Key(KEY_ESCAPE);
	Render();
	EXPECT_FALSE(qm_wiki_panel_test::IsOpen());
}

TEST_F(QmWikiPanel, TocClickScrollsTheTargetSectionTowardsTheTop)
{
	IUiContext Ctx = MakeContext();
	OpenWikiDocumentPopup(Ctx, qm_wiki_docs::VideoExport());
	Render();
	ASSERT_TRUE(qm_wiki_panel_test::IsOpen());

	const int RowCount = qm_wiki_panel_test::TocRowCount();
	ASSERT_GE(RowCount, 3);
	ASSERT_NEAR(qm_wiki_panel_test::ScrollOffsetY(), 0.0f, 1.0f);

	// 点最后一节目录：它在视口外，KEEP_IN_VIEW 必须真的滚动。
	// 命中区域就是画出来的那一行。
	const CUIRect Row = qm_wiki_panel_test::TocRowRect(RowCount - 1);
	ASSERT_GT(Row.w, 0.0f);
	const vec2 Center{Row.x + Row.w * 0.5f, Row.y + Row.h * 0.5f};
	Render(Center, false);
	EXPECT_TRUE(qm_wiki_panel_test::TocRowHovered(RowCount - 1)) << "row=" << Row.x << "," << Row.y << " " << Row.w << "x" << Row.h;
	Render(Center, true);
	Render(Center, false);
	// 先确认点击真的落在目录行上：没点中时 ScrollOffsetY 也会是 0，不能只看滚动。
	// 返回值是正文块下标（最后一节在 10 个目录项之后，所以不等于目录行号）。
	EXPECT_GE(qm_wiki_panel_test::LastTocClick(), 0) << "toc click did not register";
	// 滚动区按帧推进；给它几帧落到目标位置。
	for(int Index = 0; Index < 30; ++Index)
		Render({0.0f, 0.0f}, false);
	EXPECT_LT(qm_wiki_panel_test::ScrollOffsetY(), -1.0f);
}
