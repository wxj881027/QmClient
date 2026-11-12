// 验证真实 UI 模糊作用域的进入、继承、抑制与恢复。
#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiSurface.h>

#include <test/support/qm_anim_test_helpers.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>

#include <cstring>
#include <type_traits>
#include <vector>

namespace
{
	class QmPopupBlurScope : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestUiClient m_Client;
		IClient *UiClient() override { return &m_Client; }
	};
}

TEST_F(QmPopupBlurScope, AnchoredConfirmationUsesRequestedPositionAndCanReopen)
{
	CUi::SConfirmPopupContext Context;
	Context.YesNoButtons();
	str_copy(Context.m_aMessage, "Download this asset?");
	m_Ui.ShowPopupConfirm(40, 50, &Context, false);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_FLOAT_EQ(pPopup->x, 40);
	EXPECT_FLOAT_EQ(pPopup->y, 50);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
	m_Ui.ShowPopupConfirm(100, 150, &Context, false);
	pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_FLOAT_EQ(pPopup->x, 100);
	EXPECT_FLOAT_EQ(pPopup->y, 150);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	m_Ui.ClosePopupMenus();
}

TEST_F(QmPopupBlurScope, AnchoredConfirmationRepositionsAtViewportEdges)
{
	CUi::SConfirmPopupContext Context;
	Context.YesNoButtons();
	str_copy(Context.m_aMessage, "Download this asset?");
	const CUIRect Screen = *m_Ui.Screen();
	m_Ui.ShowPopupConfirm(Screen.w - 1, Screen.h - 1, &Context, false);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_GE(pPopup->x, Screen.x);
	EXPECT_GE(pPopup->y, Screen.y);
	EXPECT_LE(pPopup->x + pPopup->w, Screen.x + Screen.w);
	EXPECT_LE(pPopup->y + pPopup->h, Screen.y + Screen.h);
	m_Ui.ClosePopupMenus();
}

TEST_F(QmPopupBlurScope, DefaultConfirmationRetainsCenteredPlacement)
{
	CUi::SConfirmPopupContext Context;
	Context.YesNoButtons();
	str_copy(Context.m_aMessage, "Delete this asset?");
	m_Ui.ShowPopupConfirm(40, 50, &Context);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	const CUIRect Screen = *m_Ui.Screen();
	EXPECT_FLOAT_EQ(pPopup->x + pPopup->w * 0.5f, Screen.x + Screen.w * 0.5f);
	EXPECT_FLOAT_EQ(pPopup->y + pPopup->h * 0.5f, Screen.y + Screen.h * 0.5f);
	m_Ui.ClosePopupMenus();
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
		bool m_MultisampleTargets = false;
		int m_Captures = 0, m_Blurs = 0, m_Created = 0;
		int m_MultisampleTargetsCreated = 0;
		int m_BeginTargets = 0, m_EndTargets = 0;
		int m_Surfaces = 0;
		bool m_HasSdf = true;
		std::vector<std::pair<CUIRect, ColorRGBA>> m_vRectangles;
		std::vector<CFreeformItem> m_vBorderMesh;
		std::vector<std::pair<CUIRect, ColorRGBA>> m_vAntialiasEdges;
		// 设备负责外缘羽化；这里记录该边界，不把它当作整面矩形填充。
		void DrawRoundedRectAntialias(float X, float Y, float W, float H, float, int, const ColorRGBA &Color) override
		{
			m_vAntialiasEdges.emplace_back(CUIRect{X, Y, W, H}, Color);
		}
		void DrawRect(float X, float Y, float W, float H, ColorRGBA Color, int, float) override
		{
			++m_Surfaces;
			m_vRectangles.emplace_back(CUIRect{X, Y, W, H}, Color);
		}
		bool HasRoundedRectSdf() override { return m_HasSdf; }
		bool HasRoundedRectSdfTexture() override { return false; }
		void TextureSet(CTextureHandle) override {}
		void QuadsBegin() override {}
		void SetColor(ColorRGBA) override {}
		void QuadsDrawFreeform(const CFreeformItem *pItems, int Count) override { m_vBorderMesh.insert(m_vBorderMesh.end(), pItems, pItems + Count); }
		void QuadsEnd() override {}
		std::vector<SRoundedRectSdfParams> m_vRoundedSubmissions;
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override
		{
			++m_Surfaces;
			m_vRoundedSubmissions.push_back(Params);
		}
		std::vector<SRenderTargetDrawParams> m_vDraws;
		bool IsRenderTargetSupported() const override { return true; }
		bool IsBackbufferCaptureSupported() const override { return m_CaptureSupported; }
		bool IsRenderTargetGaussianBlurSupported() const override { return m_BlurSupported; }
		bool IsMultisampleRenderTargetSupported() const override { return m_MultisampleTargets; }
		int MultiSamplingCount() const override { return m_MultisampleTargets ? 4 : 0; }
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
		CRenderTargetHandle CreateRenderTarget(int Width, int Height, int SampleCount) override
		{
			EXPECT_GT(SampleCount, 1);
			++m_MultisampleTargetsCreated;
			return CreateRenderTarget(Width, Height);
		}
		bool BeginRenderTarget(CRenderTargetHandle Target, ColorRGBA) override
		{
			EXPECT_TRUE(Target.IsValid());
			++m_BeginTargets;
			return true;
		}
		void EndRenderTarget() override { ++m_EndTargets; }
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
		bool m_ClipEnabled = false;
		void ClipEnable(int, int, int, int) override { m_ClipEnabled = true; }
		void ClipDisable() override { m_ClipEnabled = false; }
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
		CUiV2AnimationRuntime m_AnimRuntime;
		CUIRect m_Rect{20, 30, 100, 60};
		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmGaussianBlur = 1;
			g_Config.m_QmBlurMode = 0;
			g_Config.m_QmGraphicsTrace = 0;
			m_Client.AdvanceFrame();
			m_Ui.Init(&m_Kernel);
			// 真实 UI 环境默认挂动画运行时（生产 gameclient 恒挂）；动效等级
			// 默认 0 时资格门仍禁动画，需要动画的测试显式开 FullMotion。
			m_Ui.SetQmAnimationRuntime(&m_AnimRuntime);
			m_Client.AttachAnimRuntime(&m_AnimRuntime);
		}
		void TearDown() override
		{
			m_Ui.ClosePopupMenus();
			m_Ui.SetQmAnimationRuntime(nullptr);
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

TEST_F(QmUiBlurGraphics, ForegroundSurfaceWithoutBackgroundBoundaryDoesNotCapture)
{
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_FALSE(m_Ui.GaussianBlurTargetReady());
	EXPECT_EQ(m_Graphics.m_Captures, 0);
	EXPECT_EQ(m_Graphics.m_Blurs, 0);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
}

TEST_F(QmUiBlurGraphics, CaptureSupportedBackgroundSkipsOffscreenLayer)
{
	m_Graphics.m_MultisampleTargets = true;
	EXPECT_FALSE(m_Ui.BeginBackgroundLayer(ColorRGBA(0.1f, 0.2f, 0.3f, 1.0f)));
	m_Ui.BeginBackgroundFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_MultisampleTargetsCreated, 0);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_TRUE(m_Ui.GaussianBlurTargetReady());
}

TEST_F(QmUiBlurGraphics, LayerFallbackAvoidsSwapchainCaptureWhenCaptureUnsupported)
{
	m_Graphics.m_CaptureSupported = false;
	m_Graphics.m_MultisampleTargets = true;
	ASSERT_TRUE(m_Ui.BeginBackgroundLayer(ColorRGBA(0.1f, 0.2f, 0.3f, 1.0f)));
	m_Ui.EndBackgroundLayer();
	m_Ui.BeginBackgroundFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_MultisampleTargetsCreated, 1);
	EXPECT_GE(m_Graphics.m_BeginTargets, 2); // 背景层 + 1/4 降采样
	EXPECT_EQ(m_Graphics.m_Captures, 0);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_TRUE(m_Ui.GaussianBlurTargetReady());
}

TEST_F(QmUiBlurGraphics, ResetClipStateClearsEmptyAndActiveClip)
{
	m_Graphics.m_ClipEnabled = true;
	m_Ui.ResetClipState();
	EXPECT_FALSE(m_Graphics.m_ClipEnabled);
	EXPECT_FALSE(m_Ui.IsClipped());

	CUIRect Clip = {0.0f, 0.0f, 100.0f, 100.0f};
	m_Ui.ClipEnable(&Clip);
	ASSERT_TRUE(m_Ui.IsClipped());
	m_Ui.ResetClipState();
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(m_Graphics.m_ClipEnabled);
}

TEST_F(QmUiBlurGraphics, BackgroundCompositeClearsStaleClipBeforeFullScreenDraw)
{
	m_Graphics.m_CaptureSupported = false;
	m_Graphics.m_MultisampleTargets = true;
	m_Graphics.m_ClipEnabled = true;
	ASSERT_TRUE(m_Ui.BeginBackgroundLayer(ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f)));
	m_Ui.EndBackgroundLayer();
	EXPECT_FALSE(m_Graphics.m_ClipEnabled);
	ASSERT_FALSE(m_Graphics.m_vDraws.empty());
}

TEST_F(QmUiBlurGraphics, BeginBackgroundFrameIsIdempotentPerFrame)
{
	m_Ui.BeginBackgroundFrame();
	m_Ui.BeginBackgroundFrame();
	EXPECT_TRUE(m_Ui.BackgroundFrameBegun());
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
}

TEST_F(QmUiBlurGraphics, MultipleSurfacesCaptureAndBlurOnlyOncePerFrame)
{
	m_Ui.BeginBackgroundFrame();
	ASSERT_TRUE(m_Ui.GaussianBlurTargetReady());
	m_Ui.RenderGaussianBlur(m_Rect, 0.3f);
	m_Ui.RenderGaussianBlur({150, 30, 80, 40}, 0.7f);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 2u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[0].m_Alpha, 0.3f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[1].m_Alpha, 0.7f);
	m_Client.AdvanceFrame();
	m_Ui.BeginBackgroundFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_Captures, 2);
	EXPECT_EQ(m_Graphics.m_Blurs, 2);
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 3u);
}

TEST_F(QmUiBlurGraphics, CaptureFailureDoesNotRetryUntilNextFrameThenRecovers)
{
	m_Graphics.m_CaptureSucceeds = false;
	m_Ui.BeginBackgroundFrame();
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	m_Graphics.m_CaptureSucceeds = true;
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 0);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	m_Client.AdvanceFrame();
	m_Ui.BeginBackgroundFrame();
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_TRUE(m_Ui.GaussianBlurTargetReady());
	EXPECT_EQ(m_Graphics.m_Captures, 2);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 1u);
}

TEST_F(QmUiBlurGraphics, BlurFailureDoesNotRetryUntilNextFrameThenRecovers)
{
	m_Graphics.m_BlurSucceeds = false;
	m_Ui.BeginBackgroundFrame();
	EXPECT_FALSE(m_Ui.PrepareGaussianBlur());
	m_Graphics.m_BlurSucceeds = true;
	m_Ui.RenderGaussianBlur(m_Rect);
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	m_Client.AdvanceFrame();
	m_Ui.BeginBackgroundFrame();
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
		m_Client.AdvanceFrame();
		m_Graphics.m_CaptureSupported = Capability != 0;
		m_Graphics.m_BlurSupported = Capability != 1;
		m_Ui.BeginBackgroundFrame();
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
	m_Ui.BeginBackgroundFrame();
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
	CScopedQmFullMotion FullMotion;
	SPopupMenuId Id;
	auto Props = ui_widget::SecondaryPanelProperties();
	// 关闭遮罩以只观察弹层背板，保留默认居中与动画策略。
	Props.m_BlockUnderlyingPointerInput = false;
	g_Config.m_QmGaussianBlur = 0;
	m_Ui.DoPopupMenu(&Id, 0, 0, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Id));
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&Id));
	for(int Frame = 0; Frame < 10; ++Frame)
		m_Client.AdvanceFrame();
	m_Ui.RenderPopupMenus();
	// 输入阶段逻辑关闭立即生效，背板继续由公共栈绘制到退场结束。
	EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
	EXPECT_TRUE(m_Ui.IsPopupVisible(&Id));
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	m_Client.AdvanceFrame();
	m_Graphics.m_vRoundedSubmissions.clear();
	m_Ui.RenderPopupMenus();
	EXPECT_GT(m_Graphics.m_Surfaces, 0);
	ASSERT_FALSE(m_Graphics.m_vRoundedSubmissions.empty());
	// 居中模态弹层退场保持原宽高并向下微沉，不向内缩窄宽高。
	// 引擎轨道接管后，退场首帧从当前实际值续接（若入场尚在收尾则略小于 1），
	// 宽高与全开值差在亚像素级——用 NEAR 断言几何形状而非精确数值。
	EXPECT_NEAR(m_Graphics.m_vRoundedSubmissions.back().m_Rect.z, 160.0f, 0.01f);
	EXPECT_NEAR(m_Graphics.m_vRoundedSubmissions.back().m_Rect.w, 100.0f, 0.01f);
	for(int Frame = 0; Frame < 10; ++Frame)
		m_Client.AdvanceFrame();
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Id));
	const int CompletedDraws = m_Graphics.m_Surfaces;
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Surfaces, CompletedDraws);
}

TEST_F(QmUiBlurGraphics, LogicalCloseStopsContentCallbacksWhileExitRemainsVisible)
{
	CScopedQmFullMotion FullMotion;
	g_Config.m_QmGaussianBlur = 0;
	SPopupMenuId Id;
	int Calls = 0;
	auto Props = ui_widget::SecondaryPanelProperties();
	m_Ui.DoPopupMenu(&Id, 0, 0, 160, 100, &Calls, [](void *pContext, CUIRect, bool) {
		++*static_cast<int *>(pContext);
		return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.RenderPopupMenus();
	for(int Frame = 0; Frame < 20; ++Frame)
		m_Client.AdvanceFrame();
	m_Ui.RenderPopupMenus();
	ASSERT_GT(Calls, 0);
	const int CallsBeforeClose = Calls;
	m_Ui.ClosePopupMenu(&Id);
	ASSERT_FALSE(m_Ui.IsPopupOpen(&Id));
	ASSERT_TRUE(m_Ui.IsPopupVisible(&Id));
	for(int Frame = 0; Frame < 20; ++Frame)
	{
		m_Client.AdvanceFrame();
		m_Ui.RenderPopupMenus();
	}
	EXPECT_EQ(Calls, CallsBeforeClose);
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Id));
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

TEST_F(QmUiBlurGraphics, PopupStackRefreshesPageSnapshotAndReusesItAcrossDropdowns)
{
	g_Config.m_QmGaussianBlur = 1;
	m_Ui.BeginBackgroundFrame();
	ASSERT_TRUE(m_Ui.GaussianBlurTargetReady());
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	SPopupMenuId First, Second;
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	Props.m_HasAnchorSurface = true;
	Props.m_AnchorSurface = {20, 10, 160, 20};
	m_Ui.DoPopupMenu(&First, 20, 30, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	Props.m_AnchorSurface = {200, 10, 160, 20};
	m_Ui.DoPopupMenu(&Second, 200, 30, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_GE(m_Graphics.m_vDraws.size(), 2u);
}

TEST_F(QmUiBlurGraphics, DisabledBackdropBlurDoesNotRefreshOrDrawCachedMenuBlur)
{
	m_Ui.BeginBackgroundFrame();
	ASSERT_TRUE(m_Ui.GaussianBlurTargetReady());
	g_Config.m_QmGaussianBlur = 0;
	SPopupMenuId Id;
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	m_Ui.DoPopupMenu(&Id, 20, 30, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_EQ(m_Graphics.m_Blurs, 1);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
}

TEST_F(QmUiBlurGraphics, FallbackBorderNeverFillsTranslucentInputInterior)
{
	m_Graphics.m_HasSdf = false;
	for(int Corners : std::array<int, 3>{0, IGraphics::CORNER_TL, IGraphics::CORNER_ALL})
	{
		for(float Alpha : {0.0f, 0.3f, 1.0f})
		{
			SCOPED_TRACE(Corners);
			SCOPED_TRACE(Alpha);
			m_Graphics.m_vRectangles.clear();
			m_Graphics.m_vBorderMesh.clear();
			m_Graphics.m_vAntialiasEdges.clear();
			const ColorRGBA Fill(0.1f, 0.1f, 0.1f, Alpha), Accent(1, 0, 0, 0.8f);
			ASSERT_TRUE(DrawRoundedSurface(&m_Ui, m_Rect, Fill, Accent, 8, 2, Corners));
			ASSERT_EQ(m_Graphics.m_vRectangles.size(), 1u);
			EXPECT_EQ(m_Graphics.m_vRectangles.front().second, Fill);
			ASSERT_FALSE(m_Graphics.m_vBorderMesh.empty());
			ASSERT_EQ(m_Graphics.m_vAntialiasEdges.size(), 1u);
			EXPECT_EQ(m_Graphics.m_vAntialiasEdges.front().second, Accent);
			const vec2 Center = m_Rect.Center();
			for(const auto &Quad : m_Graphics.m_vBorderMesh)
			{
				const float Left = std::min({Quad.m_X0, Quad.m_X1, Quad.m_X2, Quad.m_X3});
				const float Right = std::max({Quad.m_X0, Quad.m_X1, Quad.m_X2, Quad.m_X3});
				const float Top = std::min({Quad.m_Y0, Quad.m_Y1, Quad.m_Y2, Quad.m_Y3});
				const float Bottom = std::max({Quad.m_Y0, Quad.m_Y1, Quad.m_Y2, Quad.m_Y3});
				EXPECT_FALSE(Center.x > Left && Center.x < Right && Center.y > Top && Center.y < Bottom);
			}
		}
	}
}

TEST_F(QmUiBlurGraphics, PopupBlurRecoversWhenMasterSwitchIsEnabledAgain)
{
	g_Config.m_QmGaussianBlur = 0;
	SPopupMenuId Id;
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	m_Ui.DoPopupMenu(&Id, 20, 30, 160, 100, nullptr, KeepCenteredPopupOpen, Props);
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Captures, 0);
	EXPECT_TRUE(m_Graphics.m_vDraws.empty());
	g_Config.m_QmGaussianBlur = 1;
	m_Client.AdvanceFrame();
	m_Ui.BeginBackgroundFrame();
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Graphics.m_Captures, 1);
	EXPECT_FALSE(m_Graphics.m_vDraws.empty());
}
