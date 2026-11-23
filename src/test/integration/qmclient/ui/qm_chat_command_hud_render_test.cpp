#include <game/client/components/qmclient/chat_command_hud_render.h>

#include <test/support/qm_real_ui_fixture.h>

// 独立 UI 测试目标未创建聊天表情组件。命令候选误入表情绘制时直接报错。
void CQmChatEmoji::Render(EQmChatEmoji, float, float, float, float, float) const
{
	ADD_FAILURE() << "Unexpected emoji draw in server command completion";
}

using QmChatCommandHudRender = qm_ui_test::CRealUiFixture;

TEST_F(QmChatCommandHudRender, LabelAllocationFailurePreservesOtherLabelsAndAllowsRetry)
{
	CQmChatCommandHud Hud;
	ASSERT_TRUE(Hud.BeginUpdate("/", 1, true, 0));
	Hud.AddServerCommand("info", "", "Information");
	Hud.EndUpdate();
	Hud.SetLayout(20.0f, 200.0f, 250.0f, 140.0f, 14.0f, 0.0f, 400.0f);
	ASSERT_EQ(Hud.Layout().m_VisibleRows, 1);

	auto *pTextRender = m_Ui.TextRender();
	// CRealUiFixture isolates config from zero; command completion overrides the
	// theme text color from config, so keep the test text visible.
	g_Config.m_QmChatCompletionTextColor = CConfig::ms_QmChatCompletionTextColor;
	g_Config.m_QmChatCompletionSelectedTextColor = CConfig::ms_QmChatCompletionSelectedTextColor;
	const ColorRGBA CallerColor(0.2f, 0.3f, 0.4f, 0.8f);
	pTextRender->TextColor(CallerColor);
	const auto Draw = [&] { QmRenderChatCommandHud(Hud, &m_Ui, pTextRender, vec2(0.0f, 0.0f)); };
	Draw();
	const size_t LabelCount = Graphics().m_vTextSubmissions.size();
	ASSERT_GT(LabelCount, 1u);

	Graphics().m_vTextSubmissions.clear();
	Graphics().m_FailBufferObjectCreate = true;
	EXPECT_NO_THROW(Draw());
	EXPECT_FALSE(Graphics().m_FailBufferObjectCreate);
	EXPECT_EQ(Graphics().m_vTextSubmissions.size() + 1, LabelCount);
	EXPECT_EQ(pTextRender->GetTextColor(), CallerColor);
	pTextRender->QmTextFrameEnd();

	Graphics().m_vTextSubmissions.clear();
	Draw();
	EXPECT_EQ(Graphics().m_vTextSubmissions.size(), LabelCount);
	EXPECT_EQ(pTextRender->GetTextColor(), CallerColor);
}
