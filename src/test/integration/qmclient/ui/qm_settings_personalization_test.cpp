#include <game/client/QmUi/SettingsPersonalization.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmSettingsPersonalization : public qm_ui_test::CRealUiFixture
	{
	};
}

TEST_F(QmSettingsPersonalization, FoldArrowTransitionStartsDuringDraw)
{
	g_Config.m_QmUiMotionLevel = 2;
	// 实心字重走字形过渡，无需读入帧条资产。
	g_Config.m_QmUiIconWeight = 3;
	CButtonContainer Button;
	const CUIRect Row{40.0f, 40.0f, 240.0f, 24.0f};
	const auto Style = ResolveSettingsFoldRowStyle({}, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	const auto FoldRow = [&](bool Expanded) { SettingsFoldRow(&m_Ui, &Button, Row, nullptr, 12.0f, Expanded, Style); };

	{
		const CUiScopedBackgroundRender Draw(&m_Ui, true);
		FoldRow(false);
	}
	ASSERT_EQ(m_AnimRuntime.ActiveTrackCount(), 0);
	AdvanceAnimFrame();

	m_Ui.BeginPreLayoutInput();
	FoldRow(true);
	m_Ui.EndPreLayoutInput();
	EXPECT_EQ(m_AnimRuntime.ActiveTrackCount(), 0);

	{
		const CUiScopedBackgroundRender Draw(&m_Ui, true);
		FoldRow(true);
	}
	EXPECT_GT(m_AnimRuntime.ActiveTrackCount(), 0);
}
