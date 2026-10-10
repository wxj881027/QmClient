// 验证真实 UI 模糊作用域的进入、继承、抑制与恢复。
#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>

#include <cstring>
#include <type_traits>
#include <vector>

namespace
{
	class QmPopupBlurScope : public qm_ui_test::CRealUiFixture
	{
	};
}

TEST_F(QmPopupBlurScope, StandaloneScopeEnablesBlurAndRestoresState)
{
	EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
	{
		CUiScopedGaussianBlur PopupScope(&m_Ui);
		EXPECT_TRUE(m_Ui.GaussianBlurScopeActive());
		EXPECT_FLOAT_EQ(m_Ui.GaussianBlurScopeAlpha(), 1.0f);
	}
	EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
}

TEST_F(QmPopupBlurScope, NestedScopePreservesMenuFadeAndRestoresParent)
{
	for(float Alpha : {0.0f, 0.25f, 1.0f})
	{
		SCOPED_TRACE(Alpha);
		CUiScopedGaussianBlur MenuScope(&m_Ui, Alpha);
		{
			CUiScopedGaussianBlur PopupScope(&m_Ui);
			EXPECT_FLOAT_EQ(m_Ui.GaussianBlurScopeAlpha(), Alpha);
		}
		EXPECT_FLOAT_EQ(m_Ui.GaussianBlurScopeAlpha(), Alpha);
	}
	EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
}

TEST_F(QmPopupBlurScope, PopupCannotEscapeOuterSuppression)
{
	CUiScopedGaussianBlur MenuScope(&m_Ui, 0.25f);
	{
		CUiScopedGaussianBlurSuppression Suppression(&m_Ui);
		{
			CUiScopedGaussianBlur PopupScope(&m_Ui);
			EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
		}
		EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
	}
	EXPECT_TRUE(m_Ui.GaussianBlurScopeActive());
	EXPECT_FLOAT_EQ(m_Ui.GaussianBlurScopeAlpha(), 0.25f);
}

TEST_F(QmPopupBlurScope, DisabledPopupSuppressionRestoresIndependentScope)
{
	CUiScopedGaussianBlur PopupScope(&m_Ui);
	{
		CUiScopedGaussianBlurSuppression Suppression(&m_Ui, true);
		EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
	}
	EXPECT_TRUE(m_Ui.GaussianBlurScopeActive());
}

namespace
{
	// 仅替换 GPU 提交，缓存、失败闩和作用域逻辑均执行生产 CUi。
	class CBlurBoundaryGraphics : public CIconBenchmarkGraphics
	{
		int m_NextTarget = 0;

	public:
		bool m_CaptureSupported = true, m_BlurSupported = true;
		bool m_CaptureSucceeds = true, m_BlurSucceeds = true;
		int m_Captures = 0, m_Blurs = 0, m_Created = 0;
		int m_Surfaces = 0;
		void DrawRect(float, float, float, float, ColorRGBA, int, float) override { ++m_Surfaces; }
		bool HasRoundedRectSdf() override { return true; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &) override { ++m_Surfaces; }
		std::vector<SRenderTargetDrawParams> m_vDraws;
		bool IsBackbufferCaptureSupported() const override { return m_CaptureSupported; }
		bool IsRenderTargetGaussianBlurSupported() const override { return m_BlurSupported; }
		CRenderTargetHandle CreateRenderTarget(int Width, int Height) override
		{
			EXPECT_GT(Width, 0);
			EXPECT_GT(Height, 0);
			// 私有 ID 值对象无公开构造入口，fake 仅构造有效设备句柄。
			static_assert(std::is_trivially_copyable_v<CRenderTargetHandle>);
			static_assert(sizeof(CRenderTargetHandle) == sizeof(int));
			CRenderTargetHandle Handle;
			const int Id = m_NextTarget++;
			std::memcpy(&Handle, &Id, sizeof(Handle));
			++m_Created;
			return Handle;
		}
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override { pTarget->Invalidate(); }
		bool CaptureBackbufferToRenderTarget(CRenderTargetHandle Target) override
		{
			EXPECT_TRUE(Target.IsValid());
			++m_Captures;
			return m_CaptureSucceeds;
		}
		bool GaussianBlurRenderTarget(CRenderTargetHandle Source, const std::array<CRenderTargetHandle, DUAL_KAWASE_PYRAMID_LEVELS> &aTemporary, CRenderTargetHandle Destination, const SGaussianBlurParams &) override
		{
			EXPECT_TRUE(Source.IsValid());
			EXPECT_TRUE(aTemporary.front().IsValid());
			EXPECT_TRUE(Destination.IsValid());
			++m_Blurs;
			return m_BlurSucceeds;
		}
		void DrawRenderTarget(CRenderTargetHandle Target, const SRenderTargetDrawParams &Params) override
		{
			EXPECT_TRUE(Target.IsValid());
			m_vDraws.push_back(Params);
		}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = *pY0 = 0;
			*pX1 = 1920;
			*pY1 = 1080;
		}
		void ClipEnable(int, int, int, int) override {}
		void ClipDisable() override {}
		void BlendNormal() override {}
	};

	class QmUiBlurGraphics : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;

	protected:
		CBlurBoundaryGraphics m_Graphics;
		qm_ui_test::CTestUiClient m_Client;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, nullptr, &m_Client};
		CUi m_Ui;
		CUIRect m_Rect{20, 30, 100, 60};
		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmGaussianBlur = 1;
			g_Config.m_QmBlurMode = 0;
			g_Config.m_QmGraphicsTrace = 0;
			m_Client.AdvanceFrame();
			m_Ui.Init(&m_Kernel);
		}
		void TearDown() override
		{
			m_Ui.ClosePopupMenus();
			g_Config.m_QmGaussianBlur = 0;
			{
				CUiScopedGaussianBlur ReleaseTargets(&m_Ui);
			}
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}
	};
}

TEST_F(QmUiBlurGraphics, MultipleSurfacesCaptureAndBlurOnlyOncePerFrame)
{
	ASSERT_TRUE(m_Ui.PrepareGaussianBlur());
	m_Ui.RenderGaussianBlur(m_Rect, 0.3f);
	m_Ui.RenderGaussianBlur({150, 30, 80, 40}, 0.7f);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 2u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[0].m_Alpha, 0.3f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[1].m_Alpha, 0.7f);
	m_Client.AdvanceFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_Captures, 2);
	EXPECT_EQ(m_Graphics.m_Blurs, 2);
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 3u);
}

TEST_F(QmUiBlurGraphics, CaptureFailureDoesNotRetryUntilNextFrameThenRecovers)
{
	m_Graphics.m_CaptureSucceeds = false;
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	m_Graphics.m_CaptureSucceeds = true;
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 0);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	m_Client.AdvanceFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_TRUE(m_Ui.GaussianBlurTargetReady());
	EXPECT_EQ(m_Graphics.m_Captures, 2);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 1u);
}

TEST_F(QmUiBlurGraphics, BlurFailureDoesNotRetryUntilNextFrameThenRecovers)
{
	m_Graphics.m_BlurSucceeds = false;
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	m_Graphics.m_BlurSucceeds = true;
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	m_Client.AdvanceFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_Captures, 2);
	EXPECT_EQ(m_Graphics.m_Blurs, 2);
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 1u);
}

TEST_F(QmUiBlurGraphics, UnsupportedCaptureOrBlurReturnsFalseWithoutBackdropDraw)
{
	for(int Capability : {0, 1})
	{
		SCOPED_TRACE(Capability);
		m_Graphics.m_CaptureSupported = Capability != 0;
		m_Graphics.m_BlurSupported = Capability != 1;
		EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
		m_Ui.RenderGaussianBlur(m_Rect);
		EXPECT_FALSE(m_Ui.GaussianBlurTargetReady());
		EXPECT_EQ(m_Graphics.m_Created, 0);
		EXPECT_EQ(m_Graphics.m_Captures, 0);
		EXPECT_EQ(m_Graphics.m_Blurs, 0);
		EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	}
}

TEST_F(QmUiBlurGraphics, NestedScopePassesFadeAlphaToGpuAndRestoresParent)
{
	{
		CUiScopedGaussianBlur Parent(&m_Ui, 0.25f);
		{
			CUiScopedGaussianBlur Popup(&m_Ui);
			m_Ui.RenderGaussianBlur(m_Rect, m_Ui.GaussianBlurScopeAlpha());
		}
		EXPECT_FLOAT_EQ(m_Ui.GaussianBlurScopeAlpha(), 0.25f);
		{
			CUiScopedGaussianBlur Explicit(&m_Ui, 0.6f);
			m_Ui.RenderGaussianBlur(m_Rect, m_Ui.GaussianBlurScopeAlpha());
		}
		m_Ui.RenderGaussianBlur(m_Rect, m_Ui.GaussianBlurScopeAlpha());
	}
	EXPECT_FALSE(m_Ui.GaussianBlurScopeActive());
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 3u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[0].m_Alpha, 0.25f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[1].m_Alpha, 0.6f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[2].m_Alpha, 0.25f);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
}

namespace
{
	CUi::EPopupMenuFunctionResult KeepCenteredPopupOpen(void *, CUIRect, bool)
	{
		return CUi::POPUP_KEEP_OPEN;
	}
}

TEST_F(QmUiBlurGraphics, CenteredExplicitNoAnimationClosesTopImmediatelyWithoutExitDraw)
{
	SPopupMenuId Id;
	auto Props = ui_widget::SecondaryPanelProperties();
	Props.m_Animate = false;
	m_Ui.DoPopupMenu(&Id, 0, 0, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Id));
	EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_FALSE(m_Ui.CloseTopPopupMenu());
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Surfaces, 0);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
}

TEST_F(QmUiBlurGraphics, SecondaryPanelDefaultAnimationCanRenderExitAfterLogicalClose)
{
	SPopupMenuId Id;
	auto Props = ui_widget::SecondaryPanelProperties();
	// 关闭遮罩以只观察弹层背板，保留默认居中与动画策略。
	Props.m_BlockUnderlyingPointerInput = false;
	g_Config.m_QmUiPopupBlur = 0;
	m_Ui.DoPopupMenu(&Id, 0, 0, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Id));
	// 输入阶段逻辑关闭立即生效，背板继续由公共栈绘制到退场结束。
	EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
	EXPECT_TRUE(m_Ui.IsPopupVisible(&Id));
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	m_Ui.RenderPopupMenus();
	EXPECT_GT(m_Graphics.m_Surfaces, 0);
	for(int Frame = 0; Frame < 10; ++Frame)
		m_Client.AdvanceFrame();
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Id));
	const int CompletedDraws = m_Graphics.m_Surfaces;
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Surfaces, CompletedDraws);
}

TEST_F(QmUiBlurGraphics, CenteredAnimatedPanelKeepsContentSizeAcrossEntranceFrames)
{
	struct SProbe
	{
		CUIRect m_Rect;
		int m_Calls = 0;
	} Probe;
	SPopupMenuId Id;
	auto Props = ui_widget::SecondaryPanelProperties();
	m_Ui.DoPopupMenu(&Id, 20, 20, 300, 200, &Probe, [](void *pContext, CUIRect View, bool) {
		auto *pProbe = static_cast<SProbe *>(pContext);
		pProbe->m_Rect = View;
		++pProbe->m_Calls;
		return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.RenderPopupMenus();
	const CUIRect First = Probe.m_Rect;
	EXPECT_FLOAT_EQ(First.w, 300.0f - CUi::PopupMenuContentInset());
	EXPECT_FLOAT_EQ(First.h, 200.0f - CUi::PopupMenuContentInset());
	for(int Frame = 0; Frame < 10; ++Frame)
	{
		m_Client.AdvanceFrame();
		m_Ui.RenderPopupMenus();
		EXPECT_FLOAT_EQ(Probe.m_Rect.w, First.w);
		EXPECT_FLOAT_EQ(Probe.m_Rect.h, First.h);
	}
	EXPECT_EQ(Probe.m_Calls, 11);
	EXPECT_LE(Probe.m_Rect.y, First.y);
}

TEST_F(QmPopupBlurScope, BackgroundRenderBlocksControlsAndHotkeysWithoutClipping)
{
	IInput::CEvent Event{};
	Event.m_Key = KEY_RETURN;
	Event.m_Flags = IInput::FLAG_PRESS;
	m_Ui.OnInput(Event);
	const bool WasClipped = m_Ui.IsClipped();
	m_Ui.BeginBackgroundRender();
	EXPECT_TRUE(m_Ui.RenderOnly());
	EXPECT_EQ(m_Ui.IsClipped(), WasClipped);
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
}
