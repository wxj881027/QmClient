// 通过真实 CUi 和 GPU 边界记录验证加载界面重入时的裁剪恢复。
#include <test/support/qm_real_ui_fixture.h>

#include <array>
#include <vector>

namespace
{
	class CClipGraphics : public CIconBenchmarkGraphics
	{
	public:
		bool m_Clipped = false;
		std::array<int, 4> m_aClip{};
		std::vector<bool> m_vDrawClipped;

		void ClipEnable(int X, int Y, int W, int H) override
		{
			m_Clipped = true;
			m_aClip = {X, Y, W, H};
		}
		void ClipDisable() override { m_Clipped = false; }
		int CreateRectQuadContainer(float, float, float, float, float, int) override { return m_NextId++; }
		void DeleteQuadContainer(int &Index) override { Index = -1; }
		void RenderQuadContainerEx(int, int, int, float, float, float = 1.0f, float = 1.0f) override
		{
			m_vDrawClipped.push_back(m_Clipped);
		}
	};

	class QmClipSuspend : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;

	protected:
		CClipGraphics m_Graphics;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, nullptr};
		CUi m_Ui;
		const CUIRect m_Outer{10, 20, 200, 160};
		const CUIRect m_Inner{30, 40, 80, 60};

		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmGaussianBlur = 0;
			m_Ui.Init(&m_Kernel);
		}

		void TearDown() override
		{
			m_Ui.ResetClipState();
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}

		void ExpectClip(const CUIRect &Expected)
		{
			ASSERT_TRUE(m_Ui.IsClipped());
			EXPECT_FLOAT_EQ(m_Ui.ClipArea()->x, Expected.x);
			EXPECT_FLOAT_EQ(m_Ui.ClipArea()->y, Expected.y);
			EXPECT_FLOAT_EQ(m_Ui.ClipArea()->w, Expected.w);
			EXPECT_FLOAT_EQ(m_Ui.ClipArea()->h, Expected.h);
			EXPECT_TRUE(m_Graphics.m_Clipped);
		}
	};
}

TEST_F(QmClipSuspend, NestedCardClipsCanCloseAfterLoading)
{
	m_Ui.ClipEnable(&m_Outer);
	const auto OuterPixels = m_Graphics.m_aClip;
	m_Ui.ClipEnable(&m_Inner);
	const auto InnerPixels = m_Graphics.m_aClip;
	{
		CUiScopedClipSuspend LoadingScope(&m_Ui);
		EXPECT_FALSE(m_Ui.IsClipped());
		EXPECT_FALSE(m_Graphics.m_Clipped);
		// 加载界面内部即使清理裁剪，也不能清掉尚未结束的卡片裁剪。
		m_Ui.ResetClipState();
	}
	ExpectClip(m_Inner);
	EXPECT_EQ(m_Graphics.m_aClip, InnerPixels);
	m_Ui.ClipDisable();
	ExpectClip(m_Outer);
	EXPECT_EQ(m_Graphics.m_aClip, OuterPixels);
	m_Ui.ClipDisable();
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(m_Graphics.m_Clipped);
}

TEST_F(QmClipSuspend, LoadingClipDoesNotIntersectCallerClip)
{
	m_Ui.ClipEnable(&m_Inner);
	const CUIRect LoadingClip{300, 250, 100, 80};
	{
		CUiScopedClipSuspend LoadingScope(&m_Ui);
		m_Ui.ClipEnable(&LoadingClip);
		ExpectClip(LoadingClip);
		m_Ui.ClipDisable();
		EXPECT_FALSE(m_Graphics.m_Clipped);
	}
	ExpectClip(m_Inner);
	m_Ui.ClipDisable();
}

TEST_F(QmClipSuspend, EarlyReturnRestoresCallerClip)
{
	m_Ui.ClipEnable(&m_Outer);
	const auto RenderUnavailableBackground = [&]() {
		CUiScopedClipSuspend LoadingScope(&m_Ui);
		EXPECT_FALSE(m_Graphics.m_Clipped);
		return;
	};
	RenderUnavailableBackground();
	ExpectClip(m_Outer);
	m_Ui.ClipDisable();
}

TEST_F(QmClipSuspend, RepeatedLoadingFramesPreserveCallerStack)
{
	m_Ui.ClipEnable(&m_Outer);
	for(int Frame = 0; Frame < 3; ++Frame)
	{
		SCOPED_TRACE(Frame);
		{
			CUiScopedClipSuspend LoadingScope(&m_Ui);
			m_Ui.ClipEnable(&m_Inner);
			m_Ui.ClipDisable();
		}
		ExpectClip(m_Outer);
	}
	m_Ui.ClipDisable();
	EXPECT_FALSE(m_Ui.IsClipped());
}

TEST_F(QmClipSuspend, NestedLoadingRestoresEachCaller)
{
	m_Ui.ClipEnable(&m_Outer);
	{
		CUiScopedClipSuspend LoadingScope(&m_Ui);
		m_Ui.ClipEnable(&m_Inner);
		{
			CUiScopedClipSuspend NestedScope(&m_Ui);
			EXPECT_FALSE(m_Ui.IsClipped());
			EXPECT_FALSE(m_Graphics.m_Clipped);
		}
		ExpectClip(m_Inner);
		m_Ui.ClipDisable();
	}
	ExpectClip(m_Outer);
	m_Ui.ClipDisable();
}

TEST_F(QmClipSuspend, LoadingWithoutCallerClipLeavesClippingDisabled)
{
	// 即使设备残留了裁剪，独立加载界面也应完整绘制。
	m_Graphics.ClipEnable(0, 0, 1, 1);
	{
		CUiScopedClipSuspend LoadingScope(&m_Ui);
		EXPECT_FALSE(m_Ui.IsClipped());
		EXPECT_FALSE(m_Graphics.m_Clipped);
		m_Ui.ClipEnable(&m_Inner);
		m_Ui.ClipDisable();
	}
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(m_Graphics.m_Clipped);
}

TEST_F(QmClipSuspend, PendingBatchesUseTheClipOfTheirOwnScope)
{
	m_Ui.ClipEnable(&m_Outer);
	{
		CUiScopedQuadBatch Batch(&m_Ui);
		m_Ui.RenderBatchableRect(&m_Inner, ColorRGBA(1, 1, 1, 1), IGraphics::CORNER_NONE, 0.0f);
		EXPECT_TRUE(m_Graphics.m_vDrawClipped.empty());
		{
			CUiScopedClipSuspend LoadingScope(&m_Ui);
			ASSERT_EQ(m_Graphics.m_vDrawClipped.size(), 1u);
			EXPECT_TRUE(m_Graphics.m_vDrawClipped[0]);
			m_Ui.RenderBatchableRect(&m_Inner, ColorRGBA(1, 1, 1, 1), IGraphics::CORNER_NONE, 0.0f);
			EXPECT_EQ(m_Graphics.m_vDrawClipped.size(), 1u);
		}
		ASSERT_EQ(m_Graphics.m_vDrawClipped.size(), 2u);
		EXPECT_FALSE(m_Graphics.m_vDrawClipped[1]);
		ExpectClip(m_Outer);
	}
	m_Ui.ClipDisable();
}
