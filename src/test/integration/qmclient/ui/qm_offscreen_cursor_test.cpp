// 仅替换图形设备尺寸，鼠标累加、裁剪和 UI 坐标换算均执行生产实现。
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class COffscreenUiGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		using IGraphics::SetRenderScreenSize;

		void SetWindowViewport(int Width, int Height, float HiDpiScale)
		{
			SetFramebufferSize(Width, Height);
			m_ScreenHiDPIScale = HiDpiScale;
		}
	};

	class QmOffscreenCursor : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		COffscreenUiGraphics m_Graphics;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_Graphics; }

		void MoveTo(vec2 WindowPosition)
		{
			const vec2 Delta = WindowPosition - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
		}

		void Update()
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
	};
}

TEST_F(QmOffscreenCursor, FourKFrameOn1080pWindowKeepsTheBottomRightReachable)
{
	// 输入先按窗口范围累加，随后才开始本帧的离屏渲染。
	MoveTo({10000.0f, 10000.0f});
	m_Graphics.SetRenderScreenSize(3840, 2160);
	Update();

	EXPECT_EQ(m_Graphics.ScreenWidth(), 3840);
	EXPECT_EQ(m_Graphics.ScreenHeight(), 2160);
	EXPECT_EQ(m_Graphics.WindowWidth(), 1920);
	EXPECT_EQ(m_Graphics.WindowHeight(), 1080);
	EXPECT_EQ(m_Graphics.DrawableSize(), vec2(1920, 1080));
	EXPECT_EQ(m_Ui.UpdatedMousePos(), vec2(1919, 1079));
	const auto *pScreen = m_Ui.Screen();
	const CUIRect BottomRight{pScreen->w * 0.99f, pScreen->h * 0.99f, pScreen->w * 0.01f, pScreen->h * 0.01f};
	EXPECT_TRUE(m_Ui.MouseHovered(&BottomRight));
}

TEST_F(QmOffscreenCursor, HiDpiWindowUsesLogicalWindowCoordinatesDuringExport)
{
	m_Graphics.SetWindowViewport(2560, 1440, 2.0f);
	MoveTo({1152.0f, 648.0f});
	m_Graphics.SetRenderScreenSize(3840, 2160);
	Update();

	EXPECT_EQ(m_Graphics.WindowWidth(), 1280);
	EXPECT_EQ(m_Graphics.WindowHeight(), 720);
	EXPECT_EQ(m_Graphics.ScreenSize(), vec2(3840, 2160));
	EXPECT_FLOAT_EQ(m_Ui.MousePos().x, m_Ui.Screen()->w * 0.9f);
	EXPECT_FLOAT_EQ(m_Ui.MousePos().y, m_Ui.Screen()->h * 0.9f);
}

TEST_F(QmOffscreenCursor, DifferentOutputAspectKeepsPointerAlignedWithStretchedPreview)
{
	MoveTo({1440.0f, 810.0f});
	m_Graphics.SetRenderScreenSize(4096, 2160);
	Update();

	EXPECT_FLOAT_EQ(m_Graphics.ScreenAspect(), 4096.0f / 2160.0f);
	EXPECT_FLOAT_EQ(m_Ui.MousePos().x, m_Ui.Screen()->w * 0.75f);
	EXPECT_FLOAT_EQ(m_Ui.MousePos().y, m_Ui.Screen()->h * 0.75f);
}

TEST_F(QmOffscreenCursor, EnteringLeavingAndRepeatingExportDoesNotMoveThePointer)
{
	MoveTo({1440.0f, 810.0f});
	Update();
	const vec2 Before = m_Ui.MousePos();

	for(int Frame = 0; Frame < 2; ++Frame)
	{
		m_Graphics.SetRenderScreenSize(3840, 2160);
		Update();
		EXPECT_EQ(m_Ui.MousePos(), Before);
		m_Graphics.SetRenderScreenSize(0, 0);
		Update();
		EXPECT_EQ(m_Graphics.ScreenSize(), vec2(1920, 1080));
		EXPECT_EQ(m_Graphics.WindowWidth(), 1920);
		EXPECT_EQ(m_Graphics.WindowHeight(), 1080);
		EXPECT_EQ(m_Ui.MousePos(), Before);
	}
}
