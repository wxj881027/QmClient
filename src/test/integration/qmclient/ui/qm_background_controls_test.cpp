// 真实公共组件与文字引擎协作，仅在 GPU 边界记录外观提交。
#include <engine/keys.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmPieMenuSettingsControls.h>
#include <game/client/QmUi/SettingsCardMeasure.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiDiscreteSlider.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/components/qmclient/scoreboard_scroll.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_anim_test_helpers.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace
{
	class CBackgroundGraphics : public CIconBenchmarkGraphics
	{
		CUIRect m_MapRect{0, 0, 1920, 1080};
		CUIRect m_UiMapRect{0, 0, 1920, 1080};
		bool m_HasUiMapRect = false;
		std::array<ColorRGBA, 4> m_aColors{ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1)};
		std::vector<CQuadItem> m_vPendingQuads;
		std::vector<std::array<ColorRGBA, 4>> m_vPendingColors;
		std::unordered_map<int, int> m_QuadCounts;

		CUIRect PixelRect(const CUIRect &Rect) const
		{
			const float ScaleX = ScreenWidth() / m_MapRect.w;
			const float ScaleY = ScreenHeight() / m_MapRect.h;
			const float X0 = (Rect.x - m_MapRect.x) * ScaleX;
			const float Y0 = (Rect.y - m_MapRect.y) * ScaleY;
			const float X1 = (Rect.x + Rect.w - m_MapRect.x) * ScaleX;
			const float Y1 = (Rect.y + Rect.h - m_MapRect.y) * ScaleY;
			// 字形 quad 使用负高度；GPU 绘制两个端点围成的区域，而不是拒绝该矩形。
			return {std::min(X0, X1), std::min(Y0, Y1), std::abs(X1 - X0), std::abs(Y1 - Y0)};
		}

		void RecordQuad(const CQuadItem &Quad, const std::array<ColorRGBA, 4> &Colors)
		{
			const CUIRect RawRect{Quad.m_X, Quad.m_Y, Quad.m_Width, Quad.m_Height};
			const CUIRect Pixels = PixelRect(RawRect);
			// 缓存图标在局部坐标绘制并临时平移投影；断言观察统一的 UI 坐标。
			const CUIRect UiRect{m_UiMapRect.x + Pixels.x * m_UiMapRect.w / ScreenWidth(),
				m_UiMapRect.y + Pixels.y * m_UiMapRect.h / ScreenHeight(),
				Pixels.w * m_UiMapRect.w / ScreenWidth(), Pixels.h * m_UiMapRect.h / ScreenHeight()};
			const auto Color = *std::max_element(Colors.begin(), Colors.end(), [](const auto &Left, const auto &Right) { return Left.a < Right.a; });
			m_vQuads.push_back({UiRect, Color, Visible(RawRect) && Color.a > 0.0f, RawRect, Pixels, m_MapRect, m_PixelClip, m_Clipped});
		}

	public:
		bool IsBackbufferCaptureSupported() const override { return false; }
		bool IsRenderTargetGaussianBlurSupported() const override { return false; }
		struct SQuadSubmission
		{
			CUIRect m_Rect;
			ColorRGBA m_Color;
			bool m_Visible;
			CUIRect m_RawRect;
			CUIRect m_Pixels;
			CUIRect m_MapRect;
			CUIRect m_PixelClip;
			bool m_Clipped;
		};
		std::vector<SRoundedRectSdfParams> m_vSurfaces;
		std::vector<bool> m_vSurfaceVisibility;
		std::vector<SQuadSubmission> m_vQuads;
		CUIRect m_PixelClip{};
		bool m_Clipped = false;

		bool Visible(const CUIRect &Rect) const
		{
			const CUIRect Pixels = PixelRect(Rect);
			const CUIRect Clip = m_Clipped ? m_PixelClip : CUIRect{0, 0, static_cast<float>(ScreenWidth()), static_cast<float>(ScreenHeight())};
			return Pixels.w > 0.0f && Pixels.h > 0.0f && Clip.w > 0.0f && Clip.h > 0.0f &&
			       std::max(Pixels.x, Clip.x) < std::min(Pixels.x + Pixels.w, Clip.x + Clip.w) &&
			       std::max(Pixels.y, Clip.y) < std::min(Pixels.y + Pixels.h, Clip.y + Clip.h);
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			m_MapRect = {X0, Y0, X1 - X0, Y1 - Y0};
			if(!m_HasUiMapRect)
			{
				m_UiMapRect = m_MapRect;
				m_HasUiMapRect = true;
			}
		}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_MapRect.x;
			*pY0 = m_MapRect.y;
			*pX1 = m_MapRect.x + m_MapRect.w;
			*pY1 = m_MapRect.y + m_MapRect.h;
		}
		bool HasRoundedRectSdf() override { return true; }
		void ClipEnable(int X, int Y, int W, int H) override
		{
			m_Clipped = true;
			const int Left = std::clamp(X, 0, ScreenWidth());
			const int Top = std::clamp(Y, 0, ScreenHeight());
			const int Right = std::clamp(X + std::max(W, 0), Left, ScreenWidth());
			const int Bottom = std::clamp(Y + std::max(H, 0), Top, ScreenHeight());
			m_PixelClip = {static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right - Left), static_cast<float>(Bottom - Top)};
		}
		void ClipDisable() override { m_Clipped = false; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override
		{
			m_vSurfaces.push_back(Params);
			m_vSurfaceVisibility.push_back(Visible({Params.m_Rect.x, Params.m_Rect.y, Params.m_Rect.z, Params.m_Rect.w}) &&
						       (Params.m_FillColor.a > 0.0f || (Params.m_BorderColor.a > 0.0f && Params.m_Params.x > 0.0f)));
		}

		// 使用真实文字引擎的非缓冲设备路径，记录字形矩形和填色重放的可见性。
		bool IsTextBufferingEnabled() override { return false; }
		void SetColor(float R, float G, float B, float A) override { SetColor(ColorRGBA(R, G, B, A)); }
		void SetColor(ColorRGBA Color) override { m_aColors.fill(Color); }
		void SetColorVertex(const CColorVertex *pColors, size_t Num) override
		{
			for(size_t i = 0; i < Num; ++i)
				m_aColors.at(pColors[i].m_Index) = ColorRGBA(pColors[i].m_R, pColors[i].m_G, pColors[i].m_B, pColors[i].m_A);
		}
		void QuadsSetSubsetFree(float, float, float, float, float, float, float, float, int = -1) override {}
		void QuadsBegin() override
		{
			m_vPendingQuads.clear();
			m_vPendingColors.clear();
			SetColor(ColorRGBA(1, 1, 1, 1));
		}
		void QuadsDrawTL(const CQuadItem *pQuads, int Num) override
		{
			for(int i = 0; i < Num; ++i)
			{
				m_vPendingQuads.push_back(pQuads[i]);
				m_vPendingColors.push_back(m_aColors);
			}
		}
		void QuadsDrawFreeform(const CFreeformItem *pQuads, int Num) override
		{
			for(int i = 0; i < Num; ++i)
			{
				const auto &Quad = pQuads[i];
				const float Left = std::min({Quad.m_X0, Quad.m_X1, Quad.m_X2, Quad.m_X3});
				const float Top = std::min({Quad.m_Y0, Quad.m_Y1, Quad.m_Y2, Quad.m_Y3});
				const float Right = std::max({Quad.m_X0, Quad.m_X1, Quad.m_X2, Quad.m_X3});
				const float Bottom = std::max({Quad.m_Y0, Quad.m_Y1, Quad.m_Y2, Quad.m_Y3});
				m_vPendingQuads.emplace_back(Left, Top, Right - Left, Bottom - Top);
				m_vPendingColors.push_back(m_aColors);
			}
		}
		void ChangeColorOfQuadVertices(size_t Offset, unsigned char R, unsigned char G, unsigned char B, unsigned char A) override
		{
			m_vPendingColors.at(Offset).fill(ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f));
		}
		void FlushVertices(bool KeepVertices = false) override
		{
			for(size_t i = 0; i < m_vPendingQuads.size(); ++i)
				RecordQuad(m_vPendingQuads[i], m_vPendingColors[i]);
			if(!KeepVertices)
			{
				m_vPendingQuads.clear();
				m_vPendingColors.clear();
			}
		}
		void DrawRect4(float X, float Y, float W, float H, ColorRGBA TopLeft, ColorRGBA TopRight, ColorRGBA BottomLeft, ColorRGBA BottomRight, int, float) override
		{
			RecordQuad(CQuadItem(X, Y, W, H), {TopLeft, TopRight, BottomLeft, BottomRight});
		}
		void QuadsEnd() override { FlushVertices(); }
		void QuadsEndKeepVertices() override { FlushVertices(true); }
		void QuadsDrawCurrentVertices(bool KeepVertices = true) override { FlushVertices(KeepVertices); }

		~CBackgroundGraphics() override
		{
			if(!::testing::Test::HasFailure())
				return;
			// 失败时保留提交瞬间的投影与裁剪，避免只看到恢复后的设备状态。
			std::fprintf(stderr, "background recorder: %zu quad submissions\n", m_vQuads.size());
			const size_t Begin = m_vQuads.size() > 16 ? m_vQuads.size() - 16 : 0;
			for(size_t i = Begin; i < m_vQuads.size(); ++i)
			{
				const auto &Quad = m_vQuads[i];
				std::fprintf(stderr,
					"quad[%zu] visible=%d ui=(%g,%g,%g,%g) raw=(%g,%g,%g,%g) rgba=(%g,%g,%g,%g) pixels=(%g,%g,%g,%g) map=(%g,%g,%g,%g) clipped=%d clip=(%g,%g,%g,%g)\n",
					i, Quad.m_Visible, Quad.m_Rect.x, Quad.m_Rect.y, Quad.m_Rect.w, Quad.m_Rect.h,
					Quad.m_RawRect.x, Quad.m_RawRect.y, Quad.m_RawRect.w, Quad.m_RawRect.h,
					Quad.m_Color.r, Quad.m_Color.g, Quad.m_Color.b, Quad.m_Color.a,
					Quad.m_Pixels.x, Quad.m_Pixels.y, Quad.m_Pixels.w, Quad.m_Pixels.h,
					Quad.m_MapRect.x, Quad.m_MapRect.y, Quad.m_MapRect.w, Quad.m_MapRect.h,
					Quad.m_Clipped, Quad.m_PixelClip.x, Quad.m_PixelClip.y, Quad.m_PixelClip.w, Quad.m_PixelClip.h);
			}
		}

		int CreateQuadContainer(bool = true) override
		{
			const int Id = m_NextId++;
			m_QuadCounts.emplace(Id, 0);
			return Id;
		}
		int QuadContainerAddQuads(int Id, CQuadItem *, int Num) override
		{
			int &Count = m_QuadCounts.at(Id);
			const int Offset = Count;
			Count += Num;
			return Offset;
		}
		void QuadContainerReset(int Id) override
		{
			if(Id != -1)
				m_QuadCounts.at(Id) = 0;
		}
		void QuadContainerUpload(int Id) override { (void)m_QuadCounts.at(Id); }
		void DeleteQuadContainer(int &Id) override
		{
			m_QuadCounts.erase(Id);
			Id = -1;
		}
		void RenderQuadContainerEx(int Id, int Offset, int Num, float, float, float = 1.0f, float = 1.0f) override
		{
			m_Quads += Num == -1 ? m_QuadCounts.at(Id) - Offset : Num;
		}
	};

	class QmBackgroundControlsIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		CBackgroundGraphics m_Recording;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		std::unique_ptr<qm_ui_test::CRealUiTestKernel> m_pRecordingKernel;
		CTestInfo m_RecordingInfo;
		std::unique_ptr<IStorage> m_pRecordingStorage;
		std::unique_ptr<IConsole> m_pRecordingConsole;
		std::unique_ptr<IKernel> m_pRecordingEngineKernel;
		std::unique_ptr<IEngineTextRender> m_pRecordingText;
		bool m_RecordingTextInitialized = false;
		CButtonContainer m_Button;
		CLineInputBuffered<64> m_Text;
		CLineInputNumber m_Integer;
		ui_widget::SNumericFieldState m_Number;
		int m_Value = 123;
		bool m_Toggle = true;
		CUIRect m_Rect{20, 20, 280, 24};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			g_Config.m_QmUiAccentOpacity = 60;
			g_Config.m_QmUiDropdownOpacity = 40;
			g_Config.m_QmUiInputOpacity = 60;
			m_pRecordingKernel = std::make_unique<qm_ui_test::CRealUiTestKernel>(&m_Recording, m_Ui.TextRender(), &m_Input, &m_Client);
			m_Ui.Init(m_pRecordingKernel.get());
			m_Ui.MapScreen();
			m_Text.Set("server filter");
			m_Text.SetEmptyText("original placeholder");
			m_Text.SetCursorOffset(5);
			m_Text.SetSelection(2, 5);
			m_Integer.Set("pending integer");
			m_Number.m_Input.Set("pending numeric");
		}

		void UseRecordingTextEngine()
		{
			m_RecordingInfo.m_DeleteTestStorageFilesOnSuccess = false;
			m_pRecordingStorage = m_RecordingInfo.CreateTestStorage();
			ASSERT_NE(m_pRecordingStorage, nullptr);
			m_Recording.m_pStorage = m_pRecordingStorage.get();
			m_pRecordingConsole = CreateConsole(CFGFLAG_CLIENT);
			m_pRecordingEngineKernel.reset(IKernel::Create());
			m_pRecordingEngineKernel->RegisterInterface<IConsole>(m_pRecordingConsole.get(), false);
			m_pRecordingEngineKernel->RegisterInterface<IStorage>(m_pRecordingStorage.get(), false);
			m_pRecordingEngineKernel->RegisterInterface<IGraphics>(&m_Recording, false);
			m_pRecordingText.reset(CreateEngineTextRender());
			m_pRecordingEngineKernel->RegisterInterface<IEngineTextRender>(m_pRecordingText.get(), false);
			ASSERT_TRUE(m_pRecordingText->Init());
			m_RecordingTextInitialized = true;
			ASSERT_TRUE(m_pRecordingText->LoadFonts());
			m_pRecordingKernel = std::make_unique<qm_ui_test::CRealUiTestKernel>(&m_Recording, m_pRecordingText.get(), &m_Input, &m_Client);
			m_Ui.Init(m_pRecordingKernel.get());
			m_Ui.MapScreen();
		}

		void TearDown() override
		{
			CRealUiFixture::TearDown();
			if(m_RecordingTextInitialized)
				m_pRecordingText->Shutdown();
		}

		void DrawControls()
		{
			const auto Ctx = ui_widget::ControlContext(&m_Ui);
			EXPECT_FALSE(ui_widget::PrimaryButton(Ctx, &m_Button, "Connect", m_Rect));
			ui_widget::SInputFieldOptions InputOptions;
			InputOptions.m_pPlaceholder = "replacement";
			EXPECT_FALSE(ui_widget::InputField(Ctx, &m_Text, m_Rect, InputOptions).m_Changed);
			EXPECT_FALSE(ui_widget::IntegerField(Ctx, &m_Integer, &m_Value, 0, 100, m_Rect, {}).m_Changed);
			EXPECT_FALSE(ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 100, m_Rect, {}));
			EXPECT_FALSE(ui_widget::Toggle(Ctx, &m_Toggle, &m_Toggle, m_Rect));
		}

		void DrawOpacityControls(bool On = false)
		{
			m_Recording.m_vSurfaces.clear();
			g_Config.m_QmUiDropdownOpacity = 40;
			g_Config.m_QmUiInputOpacity = 60;
			const auto Ctx = ui_widget::ControlContext(&m_Ui);
			m_Ui.BeginBackgroundRender();
			ui_widget::DrawButtonSurface(Ctx, &m_Button, m_Rect, {});
			ui_widget::InputField(Ctx, &m_Text, m_Rect, ui_widget::SInputFieldOptions{});
			ui_widget::DrawToggle(Ctx, &m_Toggle, On, m_Rect, true, false);
			m_Ui.EndBackgroundRender();
		}

		void ExpectVisibleDropdown()
		{
			ASSERT_EQ(m_Recording.m_vSurfaces.size(), m_Recording.m_vSurfaceVisibility.size());
			bool VisibleFill = false;
			bool VisibleBorder = false;
			for(size_t i = 0; i < m_Recording.m_vSurfaces.size(); ++i)
			{
				if(!m_Recording.m_vSurfaceVisibility[i])
					continue;
				const auto &Surface = m_Recording.m_vSurfaces[i];
				VisibleFill = VisibleFill || Surface.m_FillColor.a > 0.0f;
				VisibleBorder = VisibleBorder || (Surface.m_BorderColor.a > 0.0f && Surface.m_Params.x > 0.0f);
			}
			EXPECT_TRUE(VisibleFill);
			EXPECT_TRUE(VisibleBorder);
			EXPECT_TRUE(std::any_of(m_Recording.m_vQuads.begin(), m_Recording.m_vQuads.end(), [&](const auto &Quad) {
				return Quad.m_Visible && Quad.m_Rect.x > m_Rect.x + m_Rect.w / 2.0f;
			}));
		}

		void ExpectUnchanged()
		{
			EXPECT_EQ(m_Value, 123);
			EXPECT_TRUE(m_Toggle);
			EXPECT_STREQ(m_Text.GetString(), "server filter");
			EXPECT_STREQ(m_Text.GetEmptyText(), "original placeholder");
			EXPECT_EQ(m_Text.GetCursorOffset(), 5u);
			EXPECT_EQ(m_Text.GetSelectionStart(), 2u);
			EXPECT_EQ(m_Text.GetSelectionEnd(), 5u);
			EXPECT_STREQ(m_Integer.GetString(), "pending integer");
			EXPECT_STREQ(m_Number.m_Input.GetString(), "pending numeric");
			EXPECT_FALSE(m_Number.m_HasSyncedValue);
		}
	};
}

TEST_F(QmBackgroundControlsIntegration, PreLayoutMenuCommitsClickWithoutSubmittingCachedText)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	CUIElement Element;
	Element.Init(&m_Ui, 1);
	SMenuButtonProperties Props;
	const auto Label = [] { return "Cached menu"; };
	m_Ui.DoButton_Menu(Element, &m_Button, Label, &m_Rect, Props);
	ASSERT_TRUE(Element.Rect(0)->m_UITextContainer.Valid());
	ASSERT_FALSE(m_Recording.m_vQuads.empty());
	const vec2 Pixels = m_Rect.Center() * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
	m_Ui.OnCursorMove(Delta.x, Delta.y);
	const auto Frame = [&](bool Pressed) {
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		m_Recording.m_vQuads.clear();
		m_Recording.m_vSurfaces.clear();
		m_Ui.BeginPreLayoutInput();
		const int Result = m_Ui.DoButton_Menu(Element, &m_Button, Label, &m_Rect, Props);
		m_Ui.EndPreLayoutInput();
		EXPECT_TRUE(m_Recording.m_vQuads.empty());
		EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
		return Result;
	};
	EXPECT_EQ(Frame(false), 0);
	EXPECT_EQ(Frame(true), 0);
	EXPECT_EQ(Frame(false), 1);
	m_Ui.BeginBackgroundRender();
	EXPECT_EQ(m_Ui.DoButton_Menu(Element, &m_Button, Label, &m_Rect, Props), 0);
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Recording.m_vQuads.empty());
}

TEST_F(QmBackgroundControlsIntegration, PreLayoutFieldsConsumeEditsAndKeepCaretWithoutDrawing)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	for(const auto Mode : {ui_widget::EInputFieldMode::TEXT, ui_widget::EInputFieldMode::MULTILINE})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		ui_widget::SInputFieldOptions Options;
		Options.m_Mode = Mode;
		m_Text.Set("edit");
		m_Text.SetCursorOffset(m_Text.GetLength());
		m_Ui.SetActiveItem(&m_Text);
		m_Text.Activate(EInputPriority::UI);
		m_Text.WasChanged();
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_TEXT;
		str_copy(Event.m_aText, "!");
		ASSERT_TRUE(m_Ui.OnInput(Event));
		m_Recording.m_vQuads.clear();
		m_Recording.m_vSurfaces.clear();
		m_Ui.BeginPreLayoutInput();
		const auto Result = ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
		m_Ui.EndPreLayoutInput();
		EXPECT_TRUE(Result.m_Changed);
		EXPECT_STREQ(m_Text.GetString(), "edit!");
		EXPECT_EQ(m_Text.GetCursorOffset(), 5u);
		EXPECT_GT(m_Text.GetCaretPosition().x, m_Rect.x);
		EXPECT_TRUE(m_Recording.m_vQuads.empty());
		EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
		m_Ui.BeginBackgroundRender(true);
		EXPECT_FALSE(ui_widget::InputField(Ctx, &m_Text, m_Rect, Options).m_Changed);
		m_Ui.EndBackgroundRender(true);
		EXPECT_FALSE(m_Recording.m_vQuads.empty());
		EXPECT_FALSE(m_Recording.m_vSurfaces.empty());
		m_Recording.m_vQuads.clear();
		m_Recording.m_vSurfaces.clear();
		Options.m_ProcessInput = false;
		m_Ui.BeginPreLayoutInput();
		EXPECT_FALSE(ui_widget::InputField(Ctx, &m_Text, m_Rect, Options).m_Changed);
		m_Ui.EndPreLayoutInput();
		EXPECT_TRUE(m_Recording.m_vQuads.empty());
		EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
		m_Ui.ReleaseActiveTextInput(&m_Text);
	}
}

TEST_F(QmBackgroundControlsIntegration, GeometryOnlyLineInputResolvesDragSelectionAndMatchesDrawLayout)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	m_Text.Set("alpha beta");
	m_Text.SetCursorOffset(0);
	m_Text.Activate(EInputPriority::UI);
	auto *pSelection = m_Text.GetMouseSelection();
	pSelection->m_Selecting = true;
	pSelection->m_PressMouse = vec2(m_Rect.x - 10.0f, m_Rect.Center().y);
	pSelection->m_ReleaseMouse = vec2(m_Rect.x + m_Rect.w + 10.0f, m_Rect.Center().y);
	const auto Geometry = m_Text.Render(&m_Rect, 12.0f, TEXTALIGN_ML, true, -1.0f, 0.0f, {}, CLineInput::ERenderMode::GEOMETRY);
	EXPECT_EQ(m_Text.GetSelectionStart(), 0u);
	EXPECT_EQ(m_Text.GetSelectionEnd(), m_Text.GetLength());
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	const vec2 Caret = m_Text.GetCaretPosition();
	pSelection->m_Selecting = false;
	const auto Draw = m_Text.Render(&m_Rect, 12.0f, TEXTALIGN_ML, true, -1.0f, 0.0f);
	EXPECT_FLOAT_EQ(Geometry.m_W, Draw.m_W);
	EXPECT_FLOAT_EQ(Geometry.m_H, Draw.m_H);
	EXPECT_FLOAT_EQ(Caret.x, m_Text.GetCaretPosition().x);
	EXPECT_FLOAT_EQ(Caret.y, m_Text.GetCaretPosition().y);
	EXPECT_FALSE(m_Recording.m_vQuads.empty());
}

TEST_F(QmBackgroundControlsIntegration, MeasurementFieldsAndLabelsSubmitNoTextAndPreservePendingState)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	CUIElement Label;
	Label.Init(&m_Ui, 1);
	m_Ui.DoLabelStreamed(*Label.Rect(0), &m_Rect, "Cached label", 12.0f, TEXTALIGN_ML);
	m_Text.Activate(EInputPriority::UI);
	m_Text.WasChanged();
	m_Text.Append(" pending");
	const auto Cursor = m_Text.GetCursorOffset();
	const auto SelectionStart = m_Text.GetSelectionStart();
	const auto SelectionEnd = m_Text.GetSelectionEnd();
	m_Recording.m_vQuads.clear();
	m_Recording.m_vSurfaces.clear();
	m_Ui.BeginBackgroundRender();
	m_Ui.BeginRenderOnly();
	EXPECT_FALSE(m_Ui.DoEditBox(&m_Text, &m_Rect, 12.0f));
	EXPECT_FALSE(m_Ui.DoEditBoxMultiLine(&m_Text, &m_Rect, 12.0f, 0.0f));
	m_Ui.DoLabel(&m_Rect, "Immediate label", 12.0f, TEXTALIGN_ML);
	m_Ui.DoLabelStreamed(*Label.Rect(0), &m_Rect, "Cached label", 12.0f, TEXTALIGN_ML);
	m_Ui.RenderLabelTextContainerAligned(*Label.Rect(0), &m_Rect, TEXTALIGN_ML);
	m_Ui.EndRenderOnly();
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
	EXPECT_TRUE(m_Text.WasChanged());
	EXPECT_EQ(m_Text.GetCursorOffset(), Cursor);
	EXPECT_EQ(m_Text.GetSelectionStart(), SelectionStart);
	EXPECT_EQ(m_Text.GetSelectionEnd(), SelectionEnd);
	m_Ui.DoLabelStreamed(*Label.Rect(0), &m_Rect, "Cached label", 12.0f, TEXTALIGN_ML);
	EXPECT_FALSE(m_Recording.m_vQuads.empty());
	m_Ui.EndBackgroundRender();
}

TEST_F(QmBackgroundControlsIntegration, PositionedPreviewTextSuppressesInputAndMeasurementAndRestoresBackground)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	m_Ui.BeginPreLayoutInput();
	m_Ui.DoText(m_Rect.x, m_Rect.y, 12.0f, "Font preview");
	m_Ui.EndPreLayoutInput();
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	m_Ui.BeginBackgroundRender();
	m_Ui.BeginRenderOnly();
	m_Ui.DoText(m_Rect.x, m_Rect.y, 12.0f, "Font preview");
	m_Ui.EndRenderOnly();
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	m_Ui.DoText(m_Rect.x, m_Rect.y, 12.0f, "Font preview");
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Recording.m_vQuads.empty());
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsPlaceholderGlyphPositionAndColor)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	m_Text.Clear();
	ui_widget::SInputFieldOptions Options;
	Options.m_pPlaceholder = "Filter servers";
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
	const auto Baseline = m_Recording.m_vQuads;
	ASSERT_FALSE(Baseline.empty());
	m_Recording.m_vQuads.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
	m_Ui.EndBackgroundRender();
	ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
	{
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
		EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
	}
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayDrawsCachedNumericLabel)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	CUIElement Label;
	Label.Init(&m_Ui, 1);
	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = "Volume";
	Options.m_pLabelElement = &Label;
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
	const auto Baseline = m_Recording.m_vQuads;
	ASSERT_FALSE(Baseline.empty());
	m_Recording.m_vQuads.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
	m_Ui.EndBackgroundRender();
	// 数值输入与滑块路径不同，独立观察左侧缓存标签的字形提交。
	const auto LabelGlyphs = [&](const auto &Quads) {
		return std::count_if(Quads.begin(), Quads.end(), [&](const auto &Quad) {
			return Quad.m_Visible && Quad.m_Rect.x < m_Rect.x + m_Rect.w * 0.25f;
		});
	};
	ASSERT_GT(LabelGlyphs(Baseline), 0);
	EXPECT_EQ(LabelGlyphs(m_Recording.m_vQuads), LabelGlyphs(Baseline));
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayPreservesScrolledInputAndMultilineLayout)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	for(const auto Mode : {ui_widget::EInputFieldMode::TEXT, ui_widget::EInputFieldMode::MULTILINE})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		m_Ui.SetEnabled(true);
		m_Text.Set("A long server command with arguments and further settings extending beyond the field");
		m_Text.SetScrollOffset(12.0f);
		ui_widget::SInputFieldOptions Options;
		Options.m_Mode = Mode;
		const auto Ctx = ui_widget::ControlContext(&m_Ui);
		m_Recording.m_vQuads.clear();
		ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
		const auto Baseline = m_Recording.m_vQuads;
		ASSERT_FALSE(Baseline.empty());
		const float Offset = m_Text.GetScrollOffset();
		m_Recording.m_vQuads.clear();
		m_Ui.SetEnabled(false);
		m_Ui.BeginBackgroundRender();
		ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
		m_Ui.EndBackgroundRender();
		EXPECT_FLOAT_EQ(m_Text.GetScrollOffset(), Offset);
		ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
		for(size_t i = 0; i < Baseline.size(); ++i)
		{
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
		}
	}
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsNumericGlyphsAndSpecialValues)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	struct SCase
	{
		const char *m_pSuffix;
		int m_Value;
		const char *m_pMaxText;
		unsigned m_Flags;
		bool m_Pending;
	};
	for(const auto &Case : {SCase{"", 123, nullptr, 0, false},
		    SCase{"%", 123, nullptr, 0, false},
		    SCase{"", 200, "Maximum", 0, false},
		    SCase{"", 0, nullptr, CUi::SCROLLBAR_OPTION_INFINITE, false},
		    SCase{"%", 123, nullptr, 0, true}})
	{
		SCOPED_TRACE(Case.m_pSuffix);
		SCOPED_TRACE(Case.m_Value);
		SCOPED_TRACE(Case.m_Pending);
		m_Ui.SetEnabled(true);
		ui_widget::SNumericFieldState Number;
		m_Value = Case.m_Value;
		ui_widget::SNumericFieldOptions Options;
		Options.m_pSuffix = Case.m_pSuffix;
		Options.m_pMaxText = Case.m_pMaxText;
		Options.m_Flags = Case.m_Flags;
		ui_widget::NumericField(Ctx, &Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
		if(Case.m_Pending)
		{
			Number.m_HasPendingValue = true;
			Number.m_PendingStoredValue = 156;
		}
		m_Recording.m_vQuads.clear();
		ui_widget::NumericField(Ctx, &Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
		const auto Baseline = m_Recording.m_vQuads;
		ASSERT_FALSE(Baseline.empty());
		m_Recording.m_vQuads.clear();
		m_Ui.SetEnabled(false);
		m_Ui.BeginBackgroundRender();
		ui_widget::NumericField(Ctx, &Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
		m_Ui.EndBackgroundRender();
		ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
		for(size_t i = 0; i < Baseline.size(); ++i)
		{
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
			EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
		}
	}
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsReadOnlyFieldGlyphsAndPlaceholder)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	for(const char *pText : {"", "read only field"})
	{
		SCOPED_TRACE(pText);
		m_Ui.SetEnabled(true);
		m_Text.Set(pText);
		m_Recording.m_vQuads.clear();
		ui_widget::ReadOnlyTextField(Ctx, &m_Text, m_Rect, "placeholder", 12.0f);
		const auto Baseline = m_Recording.m_vQuads;
		ASSERT_FALSE(Baseline.empty());
		m_Recording.m_vQuads.clear();
		m_Ui.SetEnabled(false);
		m_Ui.BeginBackgroundRender();
		ui_widget::ReadOnlyTextField(Ctx, &m_Text, m_Rect, "placeholder", 12.0f);
		m_Ui.EndBackgroundRender();
		ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
		for(size_t i = 0; i < Baseline.size(); ++i)
		{
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
			EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
		}
	}
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsIntegerFieldGlyphs)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Value = 123;
	ui_widget::IntegerField(Ctx, &m_Integer, &m_Value, 0, 200, m_Rect, {});
	m_Recording.m_vQuads.clear();
	ui_widget::IntegerField(Ctx, &m_Integer, &m_Value, 0, 200, m_Rect, {});
	const auto Baseline = m_Recording.m_vQuads;
	ASSERT_FALSE(Baseline.empty());
	m_Recording.m_vQuads.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	ui_widget::IntegerField(Ctx, &m_Integer, &m_Value, 0, 200, m_Rect, {});
	m_Ui.EndBackgroundRender();
	ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
	{
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
		EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
	}
}

TEST_F(QmBackgroundControlsIntegration, NonEditableInputUsesSameLayoutUnderConsole)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	for(auto Mode : {ui_widget::EInputFieldMode::TEXT, ui_widget::EInputFieldMode::MULTILINE})
	{
		m_Ui.SetEnabled(true);
		m_Text.Set("read only input");
		ui_widget::SInputFieldOptions Options;
		Options.m_Mode = Mode;
		Options.m_ProcessInput = false;
		m_Recording.m_vQuads.clear();
		ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
		const auto Baseline = m_Recording.m_vQuads;
		ASSERT_FALSE(Baseline.empty());
		m_Recording.m_vQuads.clear();
		m_Ui.SetEnabled(false);
		m_Ui.BeginBackgroundRender();
		ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
		m_Ui.EndBackgroundRender();
		ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
		for(size_t i = 0; i < Baseline.size(); ++i)
		{
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
			EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
			EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
		}
	}
}

TEST_F(QmBackgroundControlsIntegration, MultilineBackgroundDoesNotConsumePendingEditNotifications)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Text.WasChanged();
	m_Text.WasCursorChanged();
	m_Text.Append(" pending");
	ui_widget::SInputFieldOptions Options;
	Options.m_Mode = ui_widget::EInputFieldMode::MULTILINE;
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	ui_widget::InputField(Ctx, &m_Text, m_Rect, Options);
	m_Ui.EndBackgroundRender();
	EXPECT_TRUE(m_Text.WasChanged());
	EXPECT_TRUE(m_Text.WasCursorChanged());
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsUncommittedNumberInsteadOfMaximumLabel)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	ui_widget::SNumericFieldOptions Options;
	Options.m_pMaxText = "Maximum";
	m_Value = 198;
	ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
	m_Recording.m_vQuads.clear();
	ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
	const auto Baseline = m_Recording.m_vQuads;
	ASSERT_FALSE(Baseline.empty());
	// 模拟最大提交值为 200、用户已输入 198，但控制台接管后尚未提交。
	m_Value = 200;
	m_Number.m_LastSyncedStoredValue = 200;
	m_Recording.m_vQuads.clear();
	m_Ui.BeginBackgroundRender();
	ui_widget::NumericField(Ctx, &m_Number, &m_Button, &m_Value, 0, 200, m_Rect, Options);
	m_Ui.EndBackgroundRender();
	ASSERT_EQ(m_Recording.m_vQuads.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
	{
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.x, Baseline[i].m_Rect.x);
		EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Rect.y, Baseline[i].m_Rect.y);
		EXPECT_EQ(m_Recording.m_vQuads[i].m_Color, Baseline[i].m_Color);
	}
	EXPECT_EQ(m_Value, 200);
	EXPECT_STREQ(m_Number.m_Input.GetString(), "198");
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayPreservesClearedIntegerInput)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Integer.Clear();
	m_Value = 123;
	m_Recording.m_vQuads.clear();
	m_Ui.BeginBackgroundRender();
	ui_widget::IntegerField(Ctx, &m_Integer, &m_Value, 0, 200, m_Rect, {});
	m_Ui.EndBackgroundRender();
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	EXPECT_TRUE(m_Integer.IsEmpty());
	EXPECT_EQ(m_Value, 123);
}

TEST_F(QmBackgroundControlsIntegration, MeasurementDoesNotRecalculateActiveTextSelection)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	m_Text.Activate(EInputPriority::UI);
	m_Text.GetMouseSelection()->m_Selecting = true;
	m_Text.SetSelection(2, 5);
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Ui.BeginRenderOnly();
	ui_widget::InputField(Ctx, &m_Text, m_Rect, ui_widget::SInputFieldOptions{});
	ui_widget::ReadOnlyTextField(Ctx, &m_Text, m_Rect, nullptr, 12.0f);
	m_Ui.EndRenderOnly();
	EXPECT_EQ(m_Text.GetSelectionStart(), 2u);
	EXPECT_EQ(m_Text.GetSelectionEnd(), 5u);
	EXPECT_TRUE(m_Text.GetMouseSelection()->m_Selecting);
	EXPECT_TRUE(m_Text.IsActive());
	m_Text.Deactivate();
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayKeepsCapsulePlateAndIndicatorWhileMeasurementDrawsNeither)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const CUIRect aSlots[] = {{20, 20, 80, 24}, {108, 20, 80, 24}, {196, 20, 80, 24}};
	ui_widget::SCapsuleTabBarStyle Style;
	ui_widget::CapsuleTabBarChrome(Ctx, 1234, aSlots, 3, 1, Style);
	const auto Baseline = m_Recording.m_vSurfaces;
	ASSERT_EQ(Baseline.size(), 2u);
	EXPECT_EQ(Baseline[0].m_FillColor, vec4(Style.m_CapsuleColor.r, Style.m_CapsuleColor.g, Style.m_CapsuleColor.b, Style.m_CapsuleColor.a));
	EXPECT_EQ(Baseline[1].m_FillColor, vec4(Style.m_IndicatorColor.r, Style.m_IndicatorColor.g, Style.m_IndicatorColor.b, Style.m_IndicatorColor.a));
	m_Recording.m_vSurfaces.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	ui_widget::CapsuleTabBarChrome(Ctx, 1234, aSlots, 3, 1, Style);
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
	{
		EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_Rect.x, Baseline[i].m_Rect.x);
		EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_Rect.y, Baseline[i].m_Rect.y);
		EXPECT_EQ(m_Recording.m_vSurfaces[i].m_FillColor, Baseline[i].m_FillColor);
	}
	m_Recording.m_vSurfaces.clear();
	m_Ui.BeginRenderOnly();
	ui_widget::CapsuleTabBarChrome(Ctx, 1234, aSlots, 3, 1, Style);
	m_Ui.EndRenderOnly();
	m_Ui.EndBackgroundRender();
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
}

TEST_F(QmBackgroundControlsIntegration, PieOptionButtonAndColorHitAreasRemainSeparateWhenNarrowOrScaled)
{
	for(const float Scale : {0.75f, 1.0f, 2.0f})
	{
		for(const float Width : {0.0f, 8.0f, 24.0f, 100.0f})
		{
			SCOPED_TRACE(Scale);
			SCOPED_TRACE(Width);
			const CUIRect Cell{20, 20, Width * Scale, 32 * Scale};
			const CUIRect Button = QmPieOptionButtonRect(Cell, 24 * Scale, 4 * Scale);
			const CUIRect Swatch = QmPieOptionSwatchRect(Cell, 24 * Scale);
			EXPECT_GE(Button.w, 0.0f);
			EXPECT_GE(Swatch.w, 0.0f);
			EXPECT_LE(Button.x + Button.w, Swatch.x);
			EXPECT_LE(Swatch.x + Swatch.w, Cell.x + Cell.w);
			EXPECT_GE(Swatch.y, Cell.y);
			EXPECT_LE(Swatch.y + Swatch.h, Cell.y + Cell.h);
		}
	}
}

TEST_F(QmBackgroundControlsIntegration, PieOptionSwatchRemainsVisibleBehindConsole)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const ColorRGBA Color(0.8f, 0.2f, 0.3f, 0.75f);
	m_Recording.m_vSurfaces.clear();
	QmDrawPieOptionColorControl(Ctx, &m_Button, Color, QmPieOptionSwatchRect(m_Rect, 24.0f), 6.0f, true);
	const auto Baseline = m_Recording.m_vSurfaces;
	ASSERT_FALSE(Baseline.empty());
	m_Recording.m_vSurfaces.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	const auto Result = QmDrawPieOptionColorControl(Ctx, &m_Button, Color, QmPieOptionSwatchRect(m_Rect, 24.0f), 6.0f, true);
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(Result);
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
	{
		EXPECT_EQ(m_Recording.m_vSurfaces[i].m_FillColor, Baseline[i].m_FillColor);
		EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_Rect.x, Baseline[i].m_Rect.x);
		EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_Rect.y, Baseline[i].m_Rect.y);
		EXPECT_TRUE(m_Recording.m_vSurfaceVisibility[i]);
	}
	m_Recording.m_vSurfaces.clear();
	m_Ui.BeginRenderOnly();
	QmDrawPieOptionColorControl(Ctx, &m_Button, Color, QmPieOptionSwatchRect(m_Rect, 24.0f), 6.0f, false);
	m_Ui.EndRenderOnly();
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
	m_Ui.SetEnabled(true);
}

TEST_F(QmBackgroundControlsIntegration, PieOptionPointerInputResumesOnlyAfterConsoleCloses)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const ColorRGBA Color(0.8f, 0.2f, 0.3f, 0.75f);
	const CUIRect Cell{20, 20, 100, 24};
	const CUIRect Swatch = QmPieOptionSwatchRect(Cell, 24.0f);
	const auto Frame = [&](vec2 Position, bool Pressed, bool Background) {
		const vec2 Pixels = Position * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
		const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
		m_Ui.OnCursorMove(Delta.x, Delta.y);
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		if(Background)
			m_Ui.BeginBackgroundRender();
		const auto Result = QmDrawPieOptionColorControl(Ctx, &m_Button, Color, QmPieOptionSwatchRect(Cell, 24.0f), 6.0f, true);
		if(Background)
			m_Ui.EndBackgroundRender();
		return Result;
	};
	for(const auto Position : {Swatch.Center()})
	{
		for(const bool Pressed : {false, true, false})
		{
			const auto Result = Frame(Position, Pressed, true);
			EXPECT_FALSE(Result);
		}
	}
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Frame(Swatch.Center(), false, false);
	Frame(Swatch.Center(), true, false);
	EXPECT_TRUE(Frame(Swatch.Center(), false, false));
}

TEST_F(QmBackgroundControlsIntegration, PieOptionPreLayoutInputCommitsWithoutDrawing)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const ColorRGBA Color(0.8f, 0.2f, 0.3f, 0.75f);
	const CUIRect Cell{20, 20, 100, 24};
	const CUIRect Swatch = QmPieOptionSwatchRect(Cell, 24.0f);
	const auto Frame = [&](vec2 Position, bool Pressed) {
		const vec2 Pixels = Position * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
		const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
		m_Ui.OnCursorMove(Delta.x, Delta.y);
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		m_Recording.m_vSurfaces.clear();
		m_Recording.m_vQuads.clear();
		m_Ui.BeginPreLayoutInput();
		const auto Result = QmDrawPieOptionColorControl(Ctx, &m_Button, Color, QmPieOptionSwatchRect(Cell, 24.0f), 6.0f, true);
		m_Ui.EndPreLayoutInput();
		EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
		EXPECT_TRUE(m_Recording.m_vQuads.empty());
		return Result;
	};
	Frame(Swatch.Center(), false);
	Frame(Swatch.Center(), true);
	EXPECT_TRUE(Frame(Swatch.Center(), false));
}

TEST_F(QmBackgroundControlsIntegration, BackgroundActionIconsRemainVisibleWithoutClicks)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const EQmIcon aIcons[] = {EQmIcon::ARROW_ROTATE_RIGHT, EQmIcon::ARROW_UP_RIGHT_FROM_SQUARE, EQmIcon::PLUS, EQmIcon::CLOSE};
	const char *apFallbacks[] = {FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, FontIcons::FONT_ICON_PLUS, FontIcons::FONT_ICON_XMARK};
	for(size_t i = 0; i < std::size(aIcons); ++i)
	{
		SCOPED_TRACE(static_cast<int>(aIcons[i]));
		m_Recording.m_vSurfaces.clear();
		m_Recording.m_vQuads.clear();
		const CUIRect Rect{20, 20, 24, 24};
		m_Ui.BeginBackgroundRender();
		EXPECT_EQ(m_Ui.DoButton_QmIcon(&m_Button, aIcons[i], apFallbacks[i], 0, &Rect, BUTTONFLAG_LEFT), 0);
		m_Ui.EndBackgroundRender();
		ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
		EXPECT_TRUE(m_Recording.m_vSurfaceVisibility.back());
		EXPECT_TRUE(std::any_of(m_Recording.m_vQuads.begin(), m_Recording.m_vQuads.end(), [](const auto &Quad) { return Quad.m_Visible; }));
	}
}

TEST_F(QmBackgroundControlsIntegration, ConsoleOverlayDoesNotDimDiscreteServerFilter)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	ui_widget::SDiscreteSliderState State;
	int Value = 2;
	ui_widget::DiscreteSlider(Ctx, &m_Button, State, &Value, 0, 3, m_Rect);
	const auto Baseline = m_Recording.m_vSurfaces;
	ASSERT_FALSE(Baseline.empty());
	m_Recording.m_vSurfaces.clear();
	m_Ui.SetEnabled(false);
	m_Ui.BeginBackgroundRender();
	EXPECT_FALSE(ui_widget::DiscreteSlider(Ctx, &m_Button, State, &Value, 0, 3, m_Rect));
	m_Ui.EndBackgroundRender();
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
	for(size_t i = 0; i < Baseline.size(); ++i)
		EXPECT_EQ(m_Recording.m_vSurfaces[i].m_FillColor, Baseline[i].m_FillColor);
	EXPECT_EQ(Value, 2);
}

TEST_F(QmBackgroundControlsIntegration, BackgroundSubmitsFillAndBorderWithoutChangingControls)
{
	m_Ui.SetActiveItem(&m_Text);
	const void *pActive = m_Ui.ActiveItem();
	m_Ui.BeginBackgroundRender();
	DrawControls();
	m_Ui.EndBackgroundRender();
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), 9u);
	EXPECT_GT(m_Recording.m_vSurfaces.front().m_FillColor.a, 0.0f);
	EXPECT_GT(m_Recording.m_vSurfaces[1].m_BorderColor.a, 0.0f);
	EXPECT_EQ(m_Ui.ActiveItem(), pActive);
	ExpectUnchanged();
}

TEST_F(QmBackgroundControlsIntegration, MeasurementSuppressesChromeAndNestedExitRestoresBackgroundDrawing)
{
	m_Ui.BeginBackgroundRender();
	m_Ui.BeginBackgroundRender();
	m_Ui.BeginRenderOnly();
	m_Ui.BeginRenderOnly();
	DrawControls();
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
	m_Ui.EndRenderOnly();
	DrawControls();
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
	m_Ui.EndRenderOnly();
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_TRUE(m_Ui.RenderOnly());
	DrawControls();
	EXPECT_FALSE(m_Recording.m_vSurfaces.empty());
	m_Ui.EndBackgroundRender();
	EXPECT_TRUE(m_Ui.RenderOnly());
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Ui.RenderOnly());
	ExpectUnchanged();
}

TEST_F(QmBackgroundControlsIntegration, SelectedSecondaryButtonUsesPublicRadiusAndRetainsSelectionBehindConsole)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Recording.m_vSurfaces.clear();
	ui_widget::PrimaryButton(Ctx, &m_Button, "Vote", m_Rect);
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	const auto Primary = m_Recording.m_vSurfaces.back();
	m_Recording.m_vSurfaces.clear();
	ui_widget::SecondaryButton(Ctx, &m_Button, "All", m_Rect, false, EUiButtonGeometry::CONTROL, true);
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	const auto Selected = m_Recording.m_vSurfaces.back();
	EXPECT_EQ(Selected.m_CornerRadii, Primary.m_CornerRadii);
	m_Recording.m_vSurfaces.clear();
	m_Ui.BeginBackgroundRender();
	EXPECT_FALSE(ui_widget::SecondaryButton(Ctx, &m_Button, "All", m_Rect, false, EUiButtonGeometry::CONTROL, true));
	m_Ui.EndBackgroundRender();
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	EXPECT_EQ(m_Recording.m_vSurfaces.back().m_CornerRadii, Selected.m_CornerRadii);
	EXPECT_EQ(m_Recording.m_vSurfaces.back().m_FillColor, Selected.m_FillColor);
}

TEST_F(QmBackgroundControlsIntegration, TransparentSelectedButtonKeepsBorderUntilPointerEnters)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	ui_widget::SButtonSurfaceOptions Options;
	Options.m_TransparentInactive = true;
	Options.m_Selected = true;
	const auto Draw = [&]() {
		m_Recording.m_vSurfaces.clear();
		ui_widget::DrawButtonSurface(Ctx, &m_Button, m_Rect, Options);
		EXPECT_FALSE(m_Recording.m_vSurfaces.empty());
		return m_Recording.m_vSurfaces.empty() ? 0.0f : m_Recording.m_vSurfaces.back().m_BorderColor.a;
	};
	const float SelectedBorder = Draw();
	EXPECT_GT(SelectedBorder, 0.0f);
	const vec2 Pixels = m_Rect.Center() * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	m_Client.AdvanceFrame();
	m_Ui.Update();
	EXPECT_LT(Draw(), SelectedBorder);
	Options.m_Selected = false;
	EXPECT_FLOAT_EQ(Draw(), SelectedBorder);
}

TEST_F(QmBackgroundControlsIntegration, DisabledSecondaryButtonCancelsCaptureAndRecoversOnNewClick)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const vec2 Pixels = m_Rect.Center() * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	const auto Frame = [&](bool Pressed, bool Disabled) {
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		return ui_widget::SecondaryButton(Ctx, &m_Button, "Random", m_Rect, Disabled);
	};
	EXPECT_FALSE(Frame(false, false));
	EXPECT_FALSE(Frame(true, false));
	EXPECT_EQ(m_Ui.ActiveItem(), &m_Button);
	EXPECT_FALSE(Frame(true, true));
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	EXPECT_FALSE(Frame(false, true));
	EXPECT_FALSE(Frame(false, false));
	EXPECT_FALSE(Frame(true, false));
	EXPECT_TRUE(Frame(false, false));
}

TEST_F(QmBackgroundControlsIntegration, IconButtonCanKeepFullRowHitAreaWithoutChangingSquareSurface)
{
	const CUIRect Row{20, 20, 20, 30};
	const vec2 Position(Row.x + 10, Row.y + 1);
	const vec2 Pixels = Position * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	const auto Frame = [&](bool Pressed) {
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		return ui_widget::DoIconButton(ui_widget::ControlContext(&m_Ui), &m_Button, EQmIcon::STAR, "*", 0, Row, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true, std::nullopt, false, true, &Row);
	};
	EXPECT_EQ(Frame(false), 0);
	EXPECT_EQ(Frame(true), 0);
	EXPECT_NE(Frame(false), 0);
}

TEST_F(QmBackgroundControlsIntegration, BackgroundIgnoresPointerPressAndReleaseAndThenRestoresButtonInput)
{
	const vec2 Position = m_Rect.Center();
	const vec2 Pixels = Position * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	const auto Frame = [&](bool Pressed, bool Background) {
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		m_Client.AdvanceFrame();
		m_Ui.Update();
		if(Background)
			m_Ui.BeginBackgroundRender();
		const bool Clicked = ui_widget::PrimaryButton(ui_widget::ControlContext(&m_Ui), &m_Button, "Connect", m_Rect);
		if(Background)
			m_Ui.EndBackgroundRender();
		return Clicked;
	};
	EXPECT_FALSE(Frame(false, true));
	EXPECT_FALSE(Frame(true, true));
	EXPECT_FALSE(Frame(false, true));
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	EXPECT_FALSE(Frame(false, false));
	EXPECT_FALSE(Frame(true, false));
	EXPECT_TRUE(Frame(false, false));
}

TEST_F(QmBackgroundControlsIntegration, BackgroundDoesNotCreateToggleAnimationState)
{
	CUiV2AnimationRuntime Animation;
	m_Ui.SetQmAnimationRuntime(&Animation);
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const uint64_t KnobKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0x5A5Aull, reinterpret_cast<uint64_t>(&m_Toggle));
	m_Ui.BeginBackgroundRender();
	ui_widget::DrawToggle(Ctx, &m_Toggle, true, m_Rect, true, true);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Animation.GetValue(KnobKey, EUiAnimProperty::COLOR_MIX, -17.0f), -17.0f);
	m_Ui.BeginRenderOnly();
	ui_widget::DrawToggle(Ctx, &m_Toggle, true, m_Rect, true, true);
	m_Ui.EndRenderOnly();
	EXPECT_FLOAT_EQ(Animation.GetValue(KnobKey, EUiAnimProperty::COLOR_MIX, -17.0f), -17.0f);
	ui_widget::DrawToggle(Ctx, &m_Toggle, true, m_Rect, true, true);
	EXPECT_NE(Animation.GetValue(KnobKey, EUiAnimProperty::COLOR_MIX, -17.0f), -17.0f);
	m_Ui.SetQmAnimationRuntime(nullptr);
}

TEST_F(QmBackgroundControlsIntegration, PageBackgroundOpacityDoesNotMultiplyControlFillOrBorder)
{
	for(bool On : {false, true})
	{
		SCOPED_TRACE(On);
		m_Ui.SetBackgroundAlphaScale(1.0f);
		DrawOpacityControls(On);
		ASSERT_EQ(m_Recording.m_vSurfaces.size(), 4u);
		const auto Baseline = m_Recording.m_vSurfaces;
		EXPECT_FLOAT_EQ(Baseline[0].m_FillColor.a, 0.4f);
		EXPECT_FLOAT_EQ(Baseline[1].m_FillColor.a, 0.6f);
		if(!On)
			EXPECT_FLOAT_EQ(Baseline[2].m_FillColor.a, 0.4f);
		else
			EXPECT_GT(Baseline[2].m_FillColor.a, 0.0f);
		for(float PageAlpha : {0.0f, 0.2f, 0.6f, 1.0f})
		{
			SCOPED_TRACE(PageAlpha);
			m_Ui.SetBackgroundAlphaScale(PageAlpha);
			DrawOpacityControls(On);
			ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
			for(size_t i = 0; i < Baseline.size(); ++i)
			{
				SCOPED_TRACE(i);
				EXPECT_EQ(m_Recording.m_vSurfaces[i].m_FillColor, Baseline[i].m_FillColor);
				EXPECT_EQ(m_Recording.m_vSurfaces[i].m_BorderColor, Baseline[i].m_BorderColor);
			}
		}
	}
}

// 沿真实文字引擎和 GPU 提交验证显隐，避免参数测试漏掉主题绘制路径。
TEST_F(QmBackgroundControlsIntegration, PopupHeaderTextAndBorderFadeWithParentAndRecover)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	m_Ui.SetBackgroundAlphaScale(0.2f);
	const ColorRGBA Base(0.25f, 0.55f, 0.8f, 0.6f);
	const auto Draw = [&](float Alpha) {
		m_Recording.m_vSurfaces.clear();
		m_Recording.m_vQuads.clear();
		CUiScopedControlAlphaScale Fade(&m_Ui, Alpha);
		m_Ui.DoButton_PopupMenu(&m_Button, "Scroll mode: On", &m_Rect, 10.0f, TEXTALIGN_MC, 0.0f, false, false, Base);
	};
	Draw(1.0f);
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), 1u);
	const auto BaselineSurface = m_Recording.m_vSurfaces.front();
	const auto BaselineText = m_Recording.m_vQuads;
	ASSERT_FALSE(BaselineText.empty());
	for(float Alpha : {0.5f, 0.0f, 1.0f})
	{
		SCOPED_TRACE(Alpha);
		Draw(Alpha);
		ASSERT_EQ(m_Recording.m_vSurfaces.size(), 1u);
		EXPECT_NEAR(m_Recording.m_vSurfaces.front().m_FillColor.a, BaselineSurface.m_FillColor.a * Alpha, 0.0001f);
		EXPECT_NEAR(m_Recording.m_vSurfaces.front().m_BorderColor.a, BaselineSurface.m_BorderColor.a * Alpha, 0.0001f);
		if(Alpha == 0.0f)
		{
			// 文字引擎也可直接跳过全透明字形提交，二者都不能留下可见内容。
			for(const auto &Quad : m_Recording.m_vQuads)
				EXPECT_FLOAT_EQ(Quad.m_Color.a, 0.0f);
			EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
			continue;
		}
		ASSERT_EQ(m_Recording.m_vQuads.size(), BaselineText.size());
		for(size_t i = 0; i < BaselineText.size(); ++i)
		{
			// 字形顶点颜色量化为8位；允许一个颜色步长，关闭时仍严格为零。
			EXPECT_NEAR(m_Recording.m_vQuads[i].m_Color.a, BaselineText[i].m_Color.a * Alpha, 1.0f / 255.0f);
			if(Alpha == 0.0f)
				EXPECT_FLOAT_EQ(m_Recording.m_vQuads[i].m_Color.a, 0.0f);
		}
		EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
	}
}

TEST_F(QmBackgroundControlsIntegration, ScoreboardThemeScrollbarFadesAtBothOverflowStatesAndRecovers)
{
	CUiV2AnimationRuntime Animation;
	m_Ui.SetQmAnimationRuntime(&Animation);
	m_Ui.SetBackgroundAlphaScale(0.2f);
	for(float ContentHeight : {50.0f, 900.0f})
	{
		SCOPED_TRACE(ContentHeight);
		CScrollRegion Region;
		const auto Draw = [&](float Alpha) {
			m_Recording.m_vSurfaces.clear();
			CScrollRegionParams Params;
			Params.m_ScrollbarAlwaysReserved = true;
			Params.m_Interactive = false;
			QmScoreboardScrollAlpha(Params, Alpha);
			Region.SetContentHeightForNextFrame(ContentHeight);
			CUIRect View{20, 20, 280, 140};
			vec2 Offset;
			Region.Begin(&View, &Offset, &Params);
			Region.AddRect({View.x, View.y + Offset.y, View.w, ContentHeight});
			Region.End();
		};
		Draw(1.0f);
		if(ContentHeight <= 140.0f)
		{
			for(float Alpha : {0.5f, 0.0f, 1.0f})
			{
				Draw(Alpha);
				EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
				EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
			}
			continue;
		}
		ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
		const auto BaselineHandle = m_Recording.m_vSurfaces.back();
		ASSERT_GT(BaselineHandle.m_FillColor.a, 0.0f);
		for(float Alpha : {0.5f, 0.0f, 1.0f})
		{
			SCOPED_TRACE(Alpha);
			Draw(Alpha);
			ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
			EXPECT_NEAR(m_Recording.m_vSurfaces.back().m_FillColor.a, BaselineHandle.m_FillColor.a * Alpha, 0.0001f);
			EXPECT_NEAR(m_Recording.m_vSurfaces.back().m_BorderColor.a, BaselineHandle.m_BorderColor.a * Alpha, 0.0001f);
			if(Alpha == 0.0f)
				for(const auto &Surface : m_Recording.m_vSurfaces)
				{
					EXPECT_FLOAT_EQ(Surface.m_FillColor.a, 0.0f);
					EXPECT_FLOAT_EQ(Surface.m_BorderColor.a, 0.0f);
				}
			EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
		}
	}
	m_Ui.SetQmAnimationRuntime(nullptr);
}

TEST_F(QmBackgroundControlsIntegration, ParentTransitionFadesControlsExactlyOnceAndRestoresAfterScope)
{
	m_Ui.SetBackgroundAlphaScale(0.2f);
	DrawOpacityControls();
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), 4u);
	const auto Baseline = m_Recording.m_vSurfaces;
	const auto Text = m_Ui.TextRender()->GetTextColor();
	const auto Outline = m_Ui.TextRender()->GetTextOutlineColor();
	const auto Selection = m_Ui.TextRender()->GetTextSelectionColor();
	for(float TransitionAlpha : {0.0f, 0.25f, 0.5f, 1.0f})
	{
		SCOPED_TRACE(TransitionAlpha);
		{
			CUiScopedControlAlphaScale Transition(&m_Ui, TransitionAlpha);
			DrawOpacityControls();
			EXPECT_FLOAT_EQ(m_Ui.TextRender()->GetTextColor().a, Text.a * TransitionAlpha);
			EXPECT_FLOAT_EQ(m_Ui.TextRender()->GetTextOutlineColor().a, Outline.a * TransitionAlpha);
			EXPECT_FLOAT_EQ(m_Ui.TextRender()->GetTextSelectionColor().a, Selection.a * TransitionAlpha);
			ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
			for(size_t i = 0; i < Baseline.size(); ++i)
			{
				SCOPED_TRACE(i);
				EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_FillColor.a, Baseline[i].m_FillColor.a * TransitionAlpha);
				EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[i].m_BorderColor.a, Baseline[i].m_BorderColor.a * TransitionAlpha);
			}
		}
		EXPECT_EQ(m_Ui.TextRender()->GetTextColor(), Text);
		EXPECT_EQ(m_Ui.TextRender()->GetTextOutlineColor(), Outline);
		EXPECT_EQ(m_Ui.TextRender()->GetTextSelectionColor(), Selection);
		EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
		EXPECT_FLOAT_EQ(m_Ui.BackgroundAlphaScale(), 0.2f);
		DrawOpacityControls();
		ASSERT_EQ(m_Recording.m_vSurfaces.size(), Baseline.size());
		for(size_t i = 0; i < Baseline.size(); ++i)
			EXPECT_EQ(m_Recording.m_vSurfaces[i].m_FillColor, Baseline[i].m_FillColor);
	}
}

TEST_F(QmBackgroundControlsIntegration, NestedParentTransitionsMultiplyEachLayerAndRestoreIndependently)
{
	m_Ui.SetBackgroundAlphaScale(0.2f);
	{
		CUiScopedControlAlphaScale Parent(&m_Ui, 0.5f);
		{
			CUiScopedControlAlphaScale Child(&m_Ui, 0.4f);
			DrawOpacityControls();
			ASSERT_EQ(m_Recording.m_vSurfaces.size(), 4u);
			EXPECT_NEAR(m_Recording.m_vSurfaces[0].m_FillColor.a, 0.4f * 0.5f * 0.4f, 0.0001f);
			EXPECT_NEAR(m_Recording.m_vSurfaces[1].m_FillColor.a, 0.6f * 0.5f * 0.4f, 0.0001f);
		}
		DrawOpacityControls();
		ASSERT_EQ(m_Recording.m_vSurfaces.size(), 4u);
		EXPECT_FLOAT_EQ(m_Recording.m_vSurfaces[0].m_FillColor.a, 0.4f * 0.5f);
	}
	EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
}

TEST_F(QmBackgroundControlsIntegration, AnimatedPopupAppliesTransitionToItsControlsAndRestoresOutside)
{
	CScopedQmFullMotion FullMotion;
	// 弹窗转场由引擎 ALPHA 轨道驱动：显式挂运行时并逐帧 Advance 驱动时钟。
	CUiV2AnimationRuntime Animation;
	m_Ui.SetQmAnimationRuntime(&Animation);
	SPopupMenuId Id;
	struct SProbe
	{
		CUi *m_pUi;
		CButtonContainer m_Button;
		float m_Alpha = -1.0f;
	} Probe{&m_Ui};
	SPopupMenuProperties Props;
	Props.m_Animate = true;
	Props.m_BlockUnderlyingPointerInput = false;
	g_Config.m_QmGaussianBlur = 0;
	g_Config.m_QmUiDropdownOpacity = 40;
	m_Ui.SetBackgroundAlphaScale(0.2f);
	m_Ui.DoPopupMenu(&Id, 20, 20, 240, 100, &Probe, [](void *pContext, CUIRect View, bool) {
		auto &State = *static_cast<SProbe *>(pContext);
		State.m_Alpha = State.m_pUi->ControlAlphaScale();
		ui_widget::DrawButtonSurface(ui_widget::ControlContext(State.m_pUi), &State.m_Button, View, {});
		return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.RenderPopupMenus();
	EXPECT_FLOAT_EQ(Probe.m_Alpha, 0.0f);
	EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
	m_Client.AdvanceFrame();
	Animation.Advance(1.0f / 60.0f);
	m_Recording.m_vSurfaces.clear();
	m_Ui.RenderPopupMenus();
	EXPECT_GT(Probe.m_Alpha, 0.0f);
	EXPECT_LT(Probe.m_Alpha, 1.0f);
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	EXPECT_NEAR(m_Recording.m_vSurfaces.back().m_FillColor.a, 0.4f * Probe.m_Alpha, 0.0001f);
	EXPECT_FLOAT_EQ(m_Ui.ControlAlphaScale(), 1.0f);
	for(int Frame = 0; Frame < 20; ++Frame)
	{
		m_Client.AdvanceFrame();
		Animation.Advance(1.0f / 60.0f);
	}
	m_Ui.RenderPopupMenus();
	EXPECT_FLOAT_EQ(Probe.m_Alpha, 1.0f);
	m_Ui.ClosePopupMenus();
	m_Ui.SetQmAnimationRuntime(nullptr);
}

TEST_F(QmBackgroundControlsIntegration, SliderAccentFillAndHandleKeepOwnOpacityAcrossPageBackgrounds)
{
	m_Ui.BeginBackgroundRender();
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	m_Ui.SetBackgroundAlphaScale(1.0f);
	ui_widget::RenderHorizontalSlider(Ctx, &m_Button, m_Rect, 0.5f);
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), 3u);
	const auto Baseline = m_Recording.m_vSurfaces;
	m_Recording.m_vSurfaces.clear();
	m_Ui.SetBackgroundAlphaScale(0.2f);
	ui_widget::RenderHorizontalSlider(Ctx, &m_Button, m_Rect, 0.5f);
	ASSERT_EQ(m_Recording.m_vSurfaces.size(), 3u);
	EXPECT_EQ(m_Recording.m_vSurfaces[1].m_FillColor, Baseline[1].m_FillColor);
	EXPECT_EQ(m_Recording.m_vSurfaces[2].m_FillColor, Baseline[2].m_FillColor);
	m_Ui.EndBackgroundRender();
}

TEST_F(QmBackgroundControlsIntegration, BackgroundMeasurementConsumesRowsAndRestoresPixelClipAndVisibleControls)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	const CUIRect ParentClip{10, 10, 320, 160};
	m_Ui.ClipEnable(&ParentClip);
	const CUIRect OriginalPixelClip = m_Recording.m_PixelClip;
	m_Ui.BeginBackgroundRender();
	bool Measured = false;
	const float Height = MeasureSettingsCardContent(&m_Ui, [&](CUIRect &Content) {
		Measured = true;
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(m_Ui.RenderOnly());
		EXPECT_TRUE(m_Recording.m_Clipped);
		EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.w, 0.0f);
		EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.h, 0.0f);
		EXPECT_FLOAT_EQ(Content.w, 280.0f);
		CUIRect Row;
		Content.HSplitTop(24.0f, &Row, &Content);
		ui_widget::PrimaryButton(ui_widget::ControlContext(&m_Ui), &m_Button, "Measure", Row);
		Content.HSplitTop(6.0f, nullptr, &Content);
		Content.HSplitTop(32.0f, &Row, &Content);
		m_Ui.DoLabel(&Row, "Second row", 14.0f, TEXTALIGN_ML); }, 280.0f);
	EXPECT_TRUE(Measured);
	EXPECT_FLOAT_EQ(Height, 62.0f);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_TRUE(m_Ui.RenderOnly());
	EXPECT_TRUE(m_Recording.m_Clipped);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.x, OriginalPixelClip.x);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.y, OriginalPixelClip.y);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.w, OriginalPixelClip.w);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.h, OriginalPixelClip.h);
	// 测量直接抑制提交，不能仅靠零面积裁剪隐藏已经绘制的文字。
	EXPECT_TRUE(m_Recording.m_vQuads.empty());
	EXPECT_TRUE(m_Recording.m_vSurfaces.empty());
	m_Recording.m_vQuads.clear();
	EXPECT_FALSE(ui_widget::PrimaryButton(ui_widget::ControlContext(&m_Ui), &m_Button, "Visible again", m_Rect));
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	EXPECT_TRUE(m_Recording.m_vSurfaceVisibility.back());
	EXPECT_GT(m_Recording.m_vSurfaces.back().m_FillColor.a, 0.0f);
	EXPECT_TRUE(std::any_of(m_Recording.m_vQuads.begin(), m_Recording.m_vQuads.end(), [](const auto &Quad) { return Quad.m_Visible; }));
	m_Ui.EndBackgroundRender();
	m_Ui.ClipDisable();
	EXPECT_FALSE(m_Recording.m_Clipped);
	ExpectUnchanged();
}

TEST_F(QmBackgroundControlsIntegration, EmptyMeasurementPreservesBackgroundAndParentClip)
{
	m_Ui.ClipEnable(&m_Rect);
	const CUIRect OriginalPixelClip = m_Recording.m_PixelClip;
	m_Ui.BeginBackgroundRender();
	EXPECT_FLOAT_EQ(MeasureSettingsCardContent(&m_Ui, FSettingsCardRenderMeasured{}, m_Rect.w), 0.0f);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_TRUE(m_Ui.RenderOnly());
	EXPECT_TRUE(m_Recording.m_Clipped);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.x, OriginalPixelClip.x);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.y, OriginalPixelClip.y);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.w, OriginalPixelClip.w);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.h, OriginalPixelClip.h);
	m_Ui.EndBackgroundRender();
	m_Ui.ClipDisable();
}

TEST_F(QmBackgroundControlsIntegration, BackgroundDropdownKeepsVisibleFillBorderAndArrowWithoutOpeningPopup)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	CUi::SDropDownState State;
	// 空标签让真实字形提交只来自下拉箭头，避免普通文字掩盖箭头缺失。
	const char *apEntries[] = {"", "Other", "Last"};
	m_Ui.ClipEnable(&m_Rect);
	const vec2 Pixels = m_Rect.Center() * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	m_Ui.BeginBackgroundRender();
	m_Input.m_HeldKey = KEY_MOUSE_1;
	m_Client.AdvanceFrame();
	m_Ui.Update();
	EXPECT_EQ(m_Ui.DoDropDown(&m_Rect, 0, apEntries, 3, State), 0);
	m_Input.m_HeldKey = 0;
	m_Client.AdvanceFrame();
	m_Ui.Update();
	EXPECT_EQ(m_Ui.DoDropDown(&m_Rect, 0, apEntries, 3, State), 0);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_FALSE(State.m_DropDownState.IsOpen());
	EXPECT_EQ(State.m_SelectionPopupContext.m_SelectionIndex, -1);
	ExpectVisibleDropdown();
	m_Ui.EndBackgroundRender();
	m_Ui.ClipDisable();
}

TEST_F(QmBackgroundControlsIntegration, BackgroundDropdownPreservesOpenPopupAndPendingSelection)
{
	UseRecordingTextEngine();
	ASSERT_FALSE(HasFatalFailure());
	CUi::SDropDownState State;
	const char *apEntries[] = {"", "Other", "Last"};
	auto &Popup = State.m_SelectionPopupContext;
	Popup.m_SelectionIndex = 2;
	Popup.m_ActiveIndex = 1;
	Popup.m_vEntries.assign(std::begin(apEntries), std::end(apEntries));
	SQmDropdownInput Open;
	Open.m_TogglePressed = true;
	Open.m_InitialIndex = 0;
	ASSERT_TRUE(State.m_DropDownState.Update(Open, 3).m_Opened);
	const auto KeepOpen = [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; };
	m_Ui.DoPopupMenu(&Popup, 20, 60, 280, 100, nullptr, KeepOpen);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Popup));
	const CUIRect OriginalPopup = *m_Ui.GetPopupMenuRect(&Popup);
	m_Ui.BeginBackgroundRender();
	EXPECT_EQ(m_Ui.DoDropDown(&m_Rect, 0, apEntries, 3, State), 0);
	EXPECT_TRUE(State.m_DropDownState.IsOpen());
	EXPECT_EQ(Popup.m_SelectionIndex, 2);
	EXPECT_EQ(Popup.m_ActiveIndex, 1);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Popup));
	const CUIRect *pPopupRect = m_Ui.GetPopupMenuRect(&Popup);
	ASSERT_NE(pPopupRect, nullptr);
	EXPECT_FLOAT_EQ(pPopupRect->x, OriginalPopup.x);
	EXPECT_FLOAT_EQ(pPopupRect->y, OriginalPopup.y);
	EXPECT_FLOAT_EQ(pPopupRect->w, OriginalPopup.w);
	EXPECT_FLOAT_EQ(pPopupRect->h, OriginalPopup.h);
	ExpectVisibleDropdown();
	m_Ui.EndBackgroundRender();
	m_Ui.ClosePopupMenus();
}

TEST_F(QmBackgroundControlsIntegration, BackgroundControlSubmissionOutsideNonEmptyPixelClipIsInvisible)
{
	const CUIRect Clip{10, 10, 40, 40};
	const CUIRect Outside{100, 100, 100, 24};
	m_Ui.ClipEnable(&Clip);
	ASSERT_GT(m_Recording.m_PixelClip.w, 0.0f);
	ASSERT_GT(m_Recording.m_PixelClip.h, 0.0f);
	m_Ui.BeginBackgroundRender();
	ui_widget::DrawButtonSurface(ui_widget::ControlContext(&m_Ui), &m_Button, Outside, {});
	ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
	EXPECT_GT(m_Recording.m_vSurfaces.back().m_FillColor.a, 0.0f);
	EXPECT_FALSE(m_Recording.m_vSurfaceVisibility.back());
	const CUIRect Inside{15, 15, 24, 24};
	ui_widget::DrawButtonSurface(ui_widget::ControlContext(&m_Ui), &m_Button, Inside, {});
	EXPECT_TRUE(m_Recording.m_vSurfaceVisibility.back());
	m_Ui.EndBackgroundRender();
	m_Ui.ClipDisable();
}

TEST_F(QmBackgroundControlsIntegration, BackgroundScrollKeepsOffsetIgnoresRevealAndWheelAndDrawsScrollbar)
{
	g_Config.m_UiSmoothScrollTime = 0;
	CScrollRegion Region;
	const CUIRect ParentClip{10, 10, 340, 200};
	const CUIRect Viewport{20, 20, 280, 140};
	CScrollRegionParams Params;
	const auto DrawContent = [&](bool RevealLastRow) {
		CUIRect View = Viewport;
		vec2 Offset;
		Region.Begin(&View, &Offset, &Params);
		EXPECT_TRUE(m_Recording.m_Clipped);
		EXPECT_GT(m_Recording.m_PixelClip.w, 0.0f);
		EXPECT_GT(m_Recording.m_PixelClip.h, 0.0f);
		CUIRect Content{View.x, View.y + Offset.y, View.w, 900.0f};
		Region.AddRect(Content);
		const CUIRect LastRow{Content.x, Content.y + Content.h - 24.0f, Content.w, 24.0f};
		Region.AddRect(LastRow, RevealLastRow);
		Region.End();
		return Offset;
	};
	m_Ui.ClipEnable(&ParentClip);
	const CUIRect OriginalPixelClip = m_Recording.m_PixelClip;
	DrawContent(false);
	ASSERT_TRUE(Region.ContentOverflows());
	Region.SetScrollOffsetY(-120.0f);
	const float OriginalOffset = Region.State().Offset();
	ASSERT_GT(OriginalOffset, 0.0f);
	const vec2 Pixels = Viewport.Center() * vec2(m_Recording.WindowWidth(), m_Recording.WindowHeight()) / m_Ui.Screen()->Size();
	m_Ui.OnCursorMove(Pixels.x, Pixels.y);
	m_Client.AdvanceFrame();
	m_Ui.Update();
	IInput::CEvent Wheel{};
	Wheel.m_Key = KEY_MOUSE_WHEEL_DOWN;
	Wheel.m_Flags = IInput::FLAG_PRESS;
	ASSERT_TRUE(m_Ui.OnInput(Wheel));
	m_Client.AdvanceFrame();
	m_Ui.Update();
	m_Recording.m_vSurfaces.clear();
	m_Recording.m_vSurfaceVisibility.clear();
	m_Ui.BeginBackgroundRender();
	const vec2 BackgroundOffset = DrawContent(true);
	EXPECT_FLOAT_EQ(BackgroundOffset.y, -OriginalOffset);
	EXPECT_FLOAT_EQ(Region.State().Offset(), OriginalOffset);
	EXPECT_FALSE(Region.WheelConsumedThisFrame());
	EXPECT_TRUE(m_Recording.m_Clipped);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.x, OriginalPixelClip.x);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.y, OriginalPixelClip.y);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.w, OriginalPixelClip.w);
	EXPECT_FLOAT_EQ(m_Recording.m_PixelClip.h, OriginalPixelClip.h);
	// 本帧内容没有绘制表面，记录中的可见细长表面来自真实滚动条轨道和滑块。
	bool VisibleRail = false;
	bool VisibleHandle = false;
	for(size_t i = 0; i < m_Recording.m_vSurfaces.size(); ++i)
	{
		const auto &Surface = m_Recording.m_vSurfaces[i];
		if(!m_Recording.m_vSurfaceVisibility[i] || Surface.m_Rect.z >= Viewport.w / 2.0f)
			continue;
		VisibleRail = VisibleRail || Surface.m_Rect.w > Viewport.h / 2.0f;
		VisibleHandle = VisibleHandle || Surface.m_Rect.w < Viewport.h / 2.0f;
	}
	EXPECT_TRUE(VisibleRail);
	EXPECT_TRUE(VisibleHandle);
	m_Ui.EndBackgroundRender();
	m_Client.AdvanceFrame();
	m_Ui.Update();
	const vec2 ForegroundOffset = DrawContent(false);
	EXPECT_FLOAT_EQ(ForegroundOffset.y, -OriginalOffset);
	EXPECT_FLOAT_EQ(Region.State().Offset(), OriginalOffset);
	EXPECT_FALSE(Region.WheelConsumedThisFrame());
	m_Ui.ClipDisable();
	EXPECT_FALSE(m_Recording.m_Clipped);
}

TEST_F(QmBackgroundControlsIntegration, ServerFilterDrawsVisibleKnobAtEachSelectedStop)
{
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry(m_Rect, Ctx.m_UiScale);
	ui_widget::SDiscreteSliderState State;
	for(int Value : {0, 3, 6})
	{
		SCOPED_TRACE(Value);
		int Selected = Value;
		m_Recording.m_vSurfaces.clear();
		ui_widget::DiscreteSlider(Ctx, &m_Button, State, &Selected, 0, 6, m_Rect);
		ASSERT_FALSE(m_Recording.m_vSurfaces.empty());
		const auto &Knob = m_Recording.m_vSurfaces.back();
		EXPECT_FLOAT_EQ(Knob.m_FillColor.r, 1.0f);
		EXPECT_GT(Knob.m_FillColor.a, 0.0f);
		EXPECT_NEAR(Knob.m_Rect.z, Knob.m_Rect.w, 0.001f);
		// 公共 GPU 提交会对齐设备像素，允许至多一个逻辑像素的圆心偏移。
		const float PixelSize = m_Ui.Screen()->w / m_Recording.ScreenWidth();
		EXPECT_NEAR(Knob.m_Rect.x + Knob.m_Rect.z * 0.5f, Geometry.Position(Value / 6.0f), PixelSize);
	}
}
