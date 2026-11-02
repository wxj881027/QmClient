// 滚动条出现/消失时，保留宽度必须连续过渡，避免好友列表卡片瞬间换行。
#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmScrollbarReserveTransition : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;

	protected:
		qm_ui_test::CRealUiTestGraphics m_Graphics;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, nullptr};
		CUi m_Ui;
		CUiV2AnimationRuntime m_Runtime;
		CScrollRegion m_Region;

		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmUiMotionLevel = 2;
			m_Ui.Init(&m_Kernel);
			m_Ui.SetQmAnimationRuntime(&m_Runtime);
		}

		void TearDown() override
		{
			m_Ui.SetQmAnimationRuntime(nullptr);
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}

		float Begin(float ContentHeight)
		{
			m_Region.SetContentHeightForNextFrame(ContentHeight);
			CScrollRegionParams Params;
			Params.m_ScrollbarThickness = 10.0f;
			Params.m_AnimateScrollbarReserve = true;
			CUIRect View{0.0f, 0.0f, 200.0f, 100.0f};
			vec2 Offset;
			m_Region.Begin(&View, &Offset, &Params);
			m_Ui.ClipDisable();
			return View.w;
		}
	};
}

TEST_F(QmScrollbarReserveTransition, ReserveWidthFollowsScrollbarInBothDirections)
{
	// 首次出现直接落在目标宽度，页面打开时没有滚动条飞入。
	EXPECT_NEAR(Begin(300.0f), 190.0f, 0.01f);

	// 内容不再溢出时先保留上一帧宽度，动画由运行时推进。
	EXPECT_NEAR(Begin(20.0f), 190.0f, 0.01f);
	m_Runtime.Advance(0.02f);
	const float Expanding = Begin(20.0f);
	EXPECT_GT(Expanding, 190.0f);
	EXPECT_LT(Expanding, 200.0f);

	m_Runtime.Advance(0.5f);
	EXPECT_NEAR(Begin(20.0f), 200.0f, 0.01f);

	// 反向出现同样连续，不把滚动条厚度一次性塞回布局。
	EXPECT_NEAR(Begin(300.0f), 200.0f, 0.01f);
	m_Runtime.Advance(0.02f);
	const float Shrinking = Begin(300.0f);
	EXPECT_GT(Shrinking, 190.0f);
	EXPECT_LT(Shrinking, 200.0f);

	m_Runtime.Advance(0.5f);
	EXPECT_NEAR(Begin(300.0f), 190.0f, 0.01f);
}
