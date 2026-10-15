// 离散滑条弹簧语义：换档后视觉位置必须渐进滑动到目标档位，不允许瞬跳。
// 生产入口：ui_widget::DiscreteSlider（浏览器筛选、设置卡片共用），
// 弹簧轨道由 QmAnimResolve::ResolveUiAnimSpringValue 驱动。
#include <engine/keys.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiDiscreteSlider.h>

#include <test/support/qm_anim_test_helpers.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <cmath>

namespace
{
	// 滑条绘制只需不抛边界异常；静音 SDF/四边形提交与状态切换，不做外观断言。
	class CSilentSliderGraphics : public CIconBenchmarkGraphics
	{
	public:
		void ClipEnable(int, int, int, int) override {}
		void ClipDisable() override {}
		void BlendNone() override {}
		void BlendNormal() override {}
		void BlendAdditive() override {}
		void WrapNormal() override {}
		void WrapClamp() override {}
		bool HasRoundedRectSdf() override { return true; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &) override {}
		void DrawRect4(float, float, float, float, ColorRGBA, ColorRGBA, ColorRGBA, ColorRGBA, int, float) override {}
	};

	class QmDiscreteSliderSpring : public qm_ui_test::CRealUiFixture
	{
	protected:
		static constexpr int kMin = 0;
		static constexpr int kMax = 6;
		static constexpr float kEpsilon = 0.0001f;

		CSilentSliderGraphics m_SilentGraphics;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		std::unique_ptr<qm_ui_test::CRealUiTestKernel> m_pSliderKernel;
		int m_Value = 0;
		int m_SliderId = 0;
		ui_widget::SDiscreteSliderState m_State;
		CUIRect m_Rect{20, 20, 280, 24};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 以静音图形重建 UI 内核（基座图形对绘制提交抛边界异常）。
			m_pSliderKernel = std::make_unique<qm_ui_test::CRealUiTestKernel>(&m_SilentGraphics, m_Ui.TextRender(), &m_Input, &m_Client);
			m_Ui.Init(m_pSliderKernel.get());
			m_Ui.MapScreen();
			m_Ui.SetQmAnimationRuntime(&m_AnimRuntime);
			m_Client.AttachAnimRuntime(&m_AnimRuntime);
		}

		uint64_t SliderKey(const IUiContext &Ctx) const
		{
			// 与 UiDiscreteSlider.cpp 的轨道键构造保持一致（作用域盐 + 控件指针）。
			return BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0xD15C511Dull, reinterpret_cast<uint64_t>(&m_SliderId));
		}

		// 与生产帧序一致：先推进输入帧沿，再渲染控件。
		void BeginFrame() 
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
	};

	// 程序化换档（不改指针输入）：首帧位置仍在旧档位附近，逐帧渐进收敛。
	TEST_F(QmDiscreteSliderSpring, ValueChangeStartsSpringInsteadOfSnapping)
	{
		CScopedQmFullMotion Motion;
		const auto Ctx = ui_widget::ControlContext(&m_Ui);

		// 首帧建立轨道：无飞入语义，视觉位置 = 目标档位。
		m_Value = 0;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		const float Established = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
		ASSERT_NEAR(Established, 0.0f, kEpsilon);

		m_Value = kMax;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		const float FirstFrame = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
		EXPECT_LT(FirstFrame, 0.5f) << "换档首帧视觉位置瞬跳到目标档，弹簧轨道未生效";

		// 逐帧步进：欠阻尼弹簧允许一次过冲（生产绘制前会 clamp 到 [0,1]），
		// 断言「渐进滑动」而不是单调性：中途必须出现严格介于两档之间的位置，
		// 且在 240 帧内收敛到目标档位。
		bool SawGlidePosition = false;
		float Final = FirstFrame;
		for(int Frame = 0; Frame < 240; ++Frame)
		{
			AdvanceAnimFrame();
			BeginFrame();
			ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
			Final = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
			ASSERT_GE(Final, -0.05f) << "弹簧过冲超出绘制 clamp 容差";
			ASSERT_LE(Final, 1.05f);
			if(Final > 0.05f && Final < 0.95f)
				SawGlidePosition = true;
			if(std::abs(Final - 1.0f) < 0.005f)
				break;
		}
		EXPECT_TRUE(SawGlidePosition) << "滑条位置从未滑过两档之间（瞬跳或未推进）";
		EXPECT_NEAR(Final, 1.0f, 0.005f) << "240 帧内未收敛到目标档位";
	}

	// 点击轨道另一档位（真实指针输入路径）同样必须走弹簧。
	TEST_F(QmDiscreteSliderSpring, TrackClickGlidesToClickedStop)
	{
		CScopedQmFullMotion Motion;
		const auto Ctx = ui_widget::ControlContext(&m_Ui);

		m_Value = 0;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		AdvanceAnimFrame();

		// 光标移到最右档位附近（轨道右端内收 2px，避免边界悬停判定），UI 坐标
		// 换算像素后以增量注入，按下并释放。
		const vec2 Position(m_Rect.x + m_Rect.w - 2.0f, m_Rect.y + m_Rect.h * 0.5f);
		const vec2 Pixels = Position * vec2(m_SilentGraphics.WindowWidth(), m_SilentGraphics.WindowHeight()) / m_Ui.Screen()->Size();
		const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
		m_Ui.OnCursorMove(Delta.x, Delta.y);
		m_Input.m_HeldKey = KEY_MOUSE_1;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		m_Input.m_HeldKey = 0;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		ASSERT_EQ(m_Value, kMax) << "点击最右档位未改变档位值（输入路径失效）";

		const float FirstFrame = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
		EXPECT_LT(FirstFrame, 0.5f) << "点击换档后首帧视觉位置瞬跳，弹簧未生效";
	}

	// 相邻单档移动（归一化间距 1/6，小于像素语义的旧休眠阈值 0.25）：
	// 回归用例——轨道曾被 StartTrack 的静止判定直接完成、瞬跳到目标档。
	// 修复后（阈值取弹簧配置更紧者）必须真正启动并渐进滑动。
	TEST_F(QmDiscreteSliderSpring, AdjacentStopMoveGlidesInsteadOfSnapping)
	{
		CScopedQmFullMotion Motion;
		const auto Ctx = ui_widget::ControlContext(&m_Ui);

		// 建立在第 1 档（归一化 1/6）。
		m_Value = 1;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		AdvanceAnimFrame();

		// 移动到相邻的第 2 档（归一化 2/6，间距 ≈0.167）。
		m_Value = 2;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		const float Start = 1.0f / 6.0f;
		const float Target = 2.0f / 6.0f;
		const float FirstFrame = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
		EXPECT_LT(FirstFrame, Target - 0.02f) << "相邻档位移动瞬跳：轨道未启动（休眠阈值量纲错误回归）";

		// 逐帧步进：必须在两档之间观察到渐进位置，最终收敛到目标档。
		bool SawGlidePosition = false;
		float Final = FirstFrame;
		for(int Frame = 0; Frame < 240; ++Frame)
		{
			AdvanceAnimFrame();
			BeginFrame();
			ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
			Final = m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f);
			if(Final > Start + 0.02f && Final < Target - 0.005f)
				SawGlidePosition = true;
			if(std::abs(Final - Target) < 0.005f)
				break;
		}
		EXPECT_TRUE(SawGlidePosition) << "相邻档位之间未观察到滑动位置（被截断成 snap）";
		EXPECT_NEAR(Final, Target, 0.005f) << "240 帧内未收敛到相邻档位";
	}

	// 动效关闭（motion level 0）时保留即时语义：换档立即到位。
	TEST_F(QmDiscreteSliderSpring, MotionLevelZeroSnapsImmediately)
	{
		ASSERT_EQ(g_Config.m_QmUiMotionLevel, 0);
		const auto Ctx = ui_widget::ControlContext(&m_Ui);

		m_Value = 0;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		m_Value = kMax;
		BeginFrame();
		ui_widget::DiscreteSlider(Ctx, &m_SliderId, m_State, &m_Value, kMin, kMax, m_Rect);
		EXPECT_NEAR(m_AnimRuntime.GetValue(SliderKey(Ctx), EUiAnimProperty::POS_X, -1.0f), 1.0f, 0.005f);
	}
}
