// 数值框回车提交语义：设置卡片的滑条数值框（滚动步长等共用入口）在
// 编辑态按 Enter 必须失焦并提交——此前 DoEditBox 之后没有任何路径在
// 回车时退出编辑态，输入后一直停在选中态（用户实测反馈）。
// IME 组词中的回车是候选确认，不得退出编辑态。
#include <engine/keys.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>

#include <test/support/icon_benchmark_graphics.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	// 数值框路径的 GPU 提交静音：基座对 Blend/Wrap/DrawRect4/quad 容器等抛边界异常。
	// quad 容器系列与 fixture 的 CRealUiTestGraphics 同口径（文字光标/选区容器）。
	class CSilentNumericGraphics : public CIconBenchmarkGraphics
	{
		std::unordered_map<int, int> m_QuadCounts;
		CUIRect m_MapRect{0, 0, 1920, 1080};

	public:
		void SetFramebufferSize(int Width, int Height)
		{
			m_ScreenWidth = m_DrawableWidth = Width;
			m_ScreenHeight = m_DrawableHeight = Height;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			m_MapRect = {X0, Y0, X1 - X0, Y1 - Y0};
		}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_MapRect.x;
			*pY0 = m_MapRect.y;
			*pX1 = m_MapRect.x + m_MapRect.w;
			*pY1 = m_MapRect.y + m_MapRect.h;
		}
		void DrawRect(float X, float Y, float W, float H, ColorRGBA, int, float) override { ++m_Draws; }
		void DrawRect4(float, float, float, float, ColorRGBA, ColorRGBA, ColorRGBA, ColorRGBA, int, float) override {}
		void DrawRectExt(float, float, float, float, float, int) override {}
		void DrawRectExt4(float, float, float, float, ColorRGBA, ColorRGBA, ColorRGBA, ColorRGBA, float, int) override {}
		void DrawCircle(float, float, float, int) override {}
		void DrawRoundedRectAntialias(float, float, float, float, float, int, const ColorRGBA &) override {}
		bool HasRoundedRectSdf() override { return true; }
		bool HasRoundedRectSdfTexture() override { return false; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &) override {}
		void ClipEnable(int, int, int, int) override {}
		void ClipDisable() override {}
		void BlendNone() override {}
		void BlendNormal() override {}
		void BlendAdditive() override {}
		void WrapNormal() override {}
		void WrapClamp() override {}
		void QuadsDraw(CQuadItem *, int) override {}
		void QuadsDrawTL(const CQuadItem *, int) override {}
		void SetColorVertex(const CColorVertex *, size_t) override {}
		void SetColor2(ColorRGBA, ColorRGBA) override {}
		int CreateQuadContainer(bool = true) override
		{
			const int Id = m_NextId++;
			m_QuadCounts.emplace(Id, 0);
			return Id;
		}
		int QuadContainerAddQuads(int ContainerIndex, CQuadItem *, int Num) override
		{
			int &Count = m_QuadCounts.at(ContainerIndex);
			const int Offset = Count;
			Count += Num;
			return Offset;
		}
		void QuadContainerReset(int ContainerIndex) override
		{
			if(ContainerIndex != -1)
				m_QuadCounts.at(ContainerIndex) = 0;
		}
		void QuadContainerUpload(int ContainerIndex) override { (void)m_QuadCounts.at(ContainerIndex); }
		void DeleteQuadContainer(int &ContainerIndex) override
		{
			if(ContainerIndex == -1)
				return;
			m_QuadCounts.erase(ContainerIndex);
			ContainerIndex = -1;
		}
		void RenderQuadContainerEx(int ContainerIndex, int QuadOffset, int QuadDrawNum, float, float, float = 1.0f, float = 1.0f) override
		{
			const int Count = m_QuadCounts.at(ContainerIndex);
			++m_Draws;
			m_Quads += QuadDrawNum == -1 ? Count - QuadOffset : QuadDrawNum;
		}
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override
		{
			if(pTarget != nullptr && pTarget->IsValid())
				throw std::logic_error("unexpected live render target in numeric field fixture");
		}
	};

	class QmNumericFieldEnter : public qm_ui_test::CRealUiFixture
	{
	protected:
		CSilentNumericGraphics m_SilentGraphics;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		std::unique_ptr<qm_ui_test::CRealUiTestKernel> m_pKernel;
		int m_Value = 25;
		int m_Id = 0;
		ui_widget::SNumericFieldState m_State;
		CUIRect m_Rect{40, 40, 280, 24};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 以静音图形重建 UI 内核（基座图形对绘制提交抛边界异常）。
			m_pKernel = std::make_unique<qm_ui_test::CRealUiTestKernel>(&m_SilentGraphics, m_Ui.TextRender(), &m_Input, &m_Client);
			m_Ui.Init(m_pKernel.get());
			m_Ui.MapScreen();
			m_Ui.SetQmAnimationRuntime(&m_AnimRuntime);
			m_Client.AttachAnimRuntime(&m_AnimRuntime);
		}

		// 与生产帧序一致：先推进输入帧沿，再渲染控件。
		void BeginFrame()
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		ui_widget::SNumericFieldOptions Options()
		{
			ui_widget::SNumericFieldOptions Opt;
			Opt.m_pLabel = nullptr; // 无标签模式：输入框位于整行右侧
			return Opt;
		}

		// 输入框在整行右缘（VSplitRight 出 ValueWidth）；点右缘内 6px 必中。
		void ClickInputField()
		{
			const vec2 Position(m_Rect.x + m_Rect.w - 6.0f, m_Rect.y + m_Rect.h * 0.5f);
			const vec2 Pixels = Position * vec2(m_SilentGraphics.WindowWidth(), m_SilentGraphics.WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = KEY_MOUSE_1;
			BeginFrame();
			ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
			m_Input.m_HeldKey = 0;
			BeginFrame();
			ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		}
	};

	TEST_F(QmNumericFieldEnter, EnterDeactivatesInputAndSubmits)
	{
		// 建立初始帧。
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		ASSERT_FALSE(m_State.m_Input.IsActive());

		// 点击数值框进入编辑态。
		ClickInputField();
		ASSERT_TRUE(m_State.m_Input.IsActive()) << "点击数值框未进入编辑态（测试前置失效）";

		// 编辑态按 Enter：必须失焦（修复目标：此前停在选中态）。
		m_Input.m_RawPressedKey = KEY_RETURN;
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		m_Input.m_RawPressedKey = 0;
		EXPECT_FALSE(m_State.m_Input.IsActive()) << "回车后数值框仍停在编辑/选中态";

		// 失焦后回显同步为当前值，且后续帧不重新激活。
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		EXPECT_FALSE(m_State.m_Input.IsActive());
		EXPECT_EQ(m_State.m_Input.GetInteger(), m_Value);
	}

	TEST_F(QmNumericFieldEnter, CompositionEnterKeepsEditing)
	{
		// IME 组词中的回车是候选确认，不得退出编辑态。
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		ClickInputField();
		ASSERT_TRUE(m_State.m_Input.IsActive());

		m_Input.m_Composing = true;
		m_Input.m_RawPressedKey = KEY_RETURN;
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		m_Input.m_RawPressedKey = 0;
		m_Input.m_Composing = false;
		EXPECT_TRUE(m_State.m_Input.IsActive()) << "IME 组词中回车不应退出编辑态";
	}

	TEST_F(QmNumericFieldEnter, KeypadEnterAlsoDeactivates)
	{
		// 小键盘 Enter 与主回车同语义。
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		ClickInputField();
		ASSERT_TRUE(m_State.m_Input.IsActive());

		m_Input.m_RawPressedKey = KEY_KP_ENTER;
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		m_Input.m_RawPressedKey = 0;
		EXPECT_FALSE(m_State.m_Input.IsActive()) << "小键盘回车后数值框仍停在编辑态";

		// 下一帧不得复活（m_pLastActiveItem 残留回归）。
		BeginFrame();
		ui_widget::NumericField(ui_widget::ControlContext(&m_Ui), &m_State, &m_Id, &m_Value, 2, 100, m_Rect, Options());
		EXPECT_FALSE(m_State.m_Input.IsActive()) << "小键盘回车后下一帧输入框复活";
	}
} // namespace
