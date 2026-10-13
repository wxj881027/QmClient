// 页面进度读取连接真实 UI 阶段与生产动画后端；不启动客户端进程。
#include <game/client/QmUi/UiMotion.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmPageSwitchMotion : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUiV2AnimationRuntime m_Animation;
		static constexpr uint64_t NODE = 0x12345678;

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			g_Config.m_QmUiMotionLevel = 2;
		}

		IUiContext Context()
		{
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pAnim = &m_Animation;
			return Ctx;
		}

		float Read() { return ReadUiSwitchAnimationProgress(Context(), NODE); }
	};
}

TEST_F(QmPageSwitchMotion, BackgroundKeepsCurrentProgressAndForegroundRestoresContinuously)
{
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	m_Animation.Advance(0.04f);
	const float Before = Read();
	ASSERT_GT(Before, 0.0f);
	ASSERT_LT(Before, 1.0f);
	m_Ui.BeginBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), Before);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
	EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
	m_Animation.Advance(0.03f);
	const float During = Read();
	EXPECT_FLOAT_EQ(During, m_Animation.GetValue(NODE, EUiAnimProperty::POS_X, -1.0f));
	EXPECT_LT(During, Before);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), During);
}

TEST_F(QmPageSwitchMotion, FirstBackgroundReadDoesNotInitializeAnEntry)
{
	m_Ui.BeginBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	EXPECT_FLOAT_EQ(m_Animation.GetValue(NODE, EUiAnimProperty::POS_X, -1.0f), -1.0f);
	m_Ui.EndBackgroundRender();
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	EXPECT_FLOAT_EQ(Read(), 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
}

TEST_F(QmPageSwitchMotion, MeasurementSuppressesOffsetWithoutConsumingCurrentProgress)
{
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	m_Animation.Advance(0.04f);
	const float Before = Read();
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_FLOAT_EQ(Read(), 0.0f);
		EXPECT_FLOAT_EQ(m_Animation.GetValue(NODE, EUiAnimProperty::POS_X, -1.0f), Before);
	}
	EXPECT_FLOAT_EQ(Read(), Before);
}

TEST_F(QmPageSwitchMotion, DisablingMotionImmediatelyRemovesForegroundAndBackgroundOffset)
{
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	m_Animation.Advance(0.04f);
	ASSERT_GT(Read(), 0.0f);
	m_Ui.BeginBackgroundRender();
	g_Config.m_QmUiMotionLevel = 0;
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	g_Config.m_QmUiMotionLevel = 2;
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	EXPECT_FLOAT_EQ(Read(), 1.0f);
}

TEST_F(QmPageSwitchMotion, ReverseSelectionKeepsEntrySideAndProgressWithoutQueuingOldPages)
{
	float Direction = ResolveUiSwitchDirection(m_Animation, NODE, 0.0f, 1.0f);
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	m_Animation.Advance(0.04f);
	const float Before = Read();
	for(int Index = 0; Index < 4; ++Index)
	{
		Direction = ResolveUiSwitchDirection(m_Animation, NODE, Direction, -1.0f);
		BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
		EXPECT_FLOAT_EQ(Direction, 1.0f);
		EXPECT_FLOAT_EQ(Read(), Before);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
		EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
	}
	m_Animation.Advance(0.25f);
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	Direction = ResolveUiSwitchDirection(m_Animation, NODE, Direction, -1.0f);
	EXPECT_FLOAT_EQ(Direction, -1.0f);
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	EXPECT_FLOAT_EQ(Read(), 1.0f);
}

TEST_F(QmPageSwitchMotion, ReducedMotionFinishesSoonerAndRemainsContinuousInBackground)
{
	g_Config.m_QmUiMotionLevel = 1;
	BeginUiSwitchAnimation(m_Animation, NODE, 0.18f);
	m_Animation.Advance(0.02f);
	const float Before = Read();
	ASSERT_GT(Before, 0.0f);
	ASSERT_LT(Before, 1.0f);
	m_Ui.BeginBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), Before);
	m_Animation.Advance(0.08f);
	EXPECT_FLOAT_EQ(Read(), 0.0f);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Read(), 0.0f);
}
