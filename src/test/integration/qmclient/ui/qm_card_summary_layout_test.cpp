// 卡片悬浮说明、真实控件输入与不可变内容布局的协作；fake 只替换设备边界。
#include <engine/keys.h>

#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/SettingsCardSubtitle.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmCardSummaryLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}
		void Frame(vec2 Position, bool Pressed = false)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
	};
}

TEST_F(QmCardSummaryLayout, ContentDrawKeepsButtonFeedbackWhileBlockingClicksAndUnderlyingBackgroundFeedback)
{
	CButtonContainer Button;
	const CUIRect Row{40, 40, 240, 24};
	Frame({80, 50});
	ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Action", Row);
	Frame({80, 50}, true);
	EXPECT_FALSE(ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Action", Row));
	ASSERT_EQ(m_Ui.ActiveItem(), &Button);
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const auto Draw = [&] {
		Graphics.m_vRoundedSubmissions.clear();
		EXPECT_FALSE(ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Action", Row));
		return Graphics.m_vRoundedSubmissions.back().m_FillColor;
	};
	const auto InputColor = Draw();
	{
		CUiScopedBackgroundRender Content(&m_Ui, true);
		EXPECT_FALSE(m_Ui.MouseHovered(&Row));
		EXPECT_TRUE(m_Ui.VisualMouseHovered(&Row));
		const auto DrawColor = Draw();
		EXPECT_EQ(DrawColor, InputColor);
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_FALSE(m_Ui.VisualMouseHovered(&Row));
		EXPECT_NE(Draw(), InputColor);
	}
	EXPECT_TRUE(m_Ui.VisualMouseHovered(&Row));
}

TEST_F(QmCardSummaryLayout, DeclaredContentKeepsBackgroundDrawingAndCacheThroughMeasurement)
{
	CButtonContainer Button;
	// 先准备真实按钮字形，避免首次绘制的 atlas 更新成为另一种缓存失效原因。
	Frame({10, 10});
	ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Action", {40, 40, 240, 24});
	int Inputs = 0;
	SSettingsCardDefinition Definition;
	Definition.m_RenderMeasured = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		if(!Flow.Control("action", 24, Row))
			return;
		if(Flow.Input())
			++Inputs;
		ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Action", Row);
	};
	qm_settings_content::UseDeclaredContent(Definition, &m_Ui, m_Ui.TextRender());
	CSettingsContentLayoutCache Cache;
	const auto pInitial = Definition.m_ResolveContentLayout(Cache, 240);
	ASSERT_NE(pInitial, nullptr);
	Frame({80, 50}, true);
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		auto pLayout = Definition.m_ResolveContentLayout(Cache, 240);
		EXPECT_EQ(pLayout, pInitial);
		EXPECT_FALSE(m_Ui.MeasurementOnly());
		EXPECT_FALSE(qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 40, 240, 24}, true, false, false, &m_Ui));
		Graphics.m_vRoundedSubmissions.clear();
		Definition.m_ContentLayoutDraw(*pLayout, {40, 40, 240, 24});
		EXPECT_FALSE(Graphics.m_vRoundedSubmissions.empty());
		EXPECT_EQ(Inputs, 0);
		{
			qm_settings_content::CContentDeclarationUiGuard Measurement(&m_Ui);
			EXPECT_EQ(Definition.m_ResolveContentLayout(Cache, 240), pInitial);
			Graphics.m_vRoundedSubmissions.clear();
			Definition.m_ContentLayoutDraw(*pLayout, {40, 40, 240, 24});
			EXPECT_TRUE(Graphics.m_vRoundedSubmissions.empty());
		}
		EXPECT_FALSE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(m_Ui.BackgroundRender());
	}
	EXPECT_FALSE(m_Ui.RenderOnly());
	auto pLayout = Definition.m_ResolveContentLayout(Cache, 240);
	EXPECT_EQ(pLayout, pInitial);
	m_Ui.BeginPreLayoutInput();
	qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 40, 240, 24}, true, false, false, &m_Ui);
	m_Ui.EndPreLayoutInput();
	EXPECT_EQ(Inputs, 1);
}

TEST_F(QmCardSummaryLayout, BackgroundDropdownKeepsItsSurfaceWithoutConsumingPendingSelection)
{
	CUi::SDropDownState Dropdown;
	const char *apEntries[] = {"Basic", "Advanced"};
	CUIRect Row{40, 40, 240, 24};
	Frame({10, 10});
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	m_Ui.DoDropDown(&Row, 0, apEntries, 2, Dropdown);
	ASSERT_FALSE(Graphics.m_vRoundedSubmissions.empty());
	const size_t SurfaceCount = Graphics.m_vRoundedSubmissions.size();
	Graphics.m_vRoundedSubmissions.clear();
	Dropdown.m_SelectionPopupContext.m_vEntries = {"Basic", "Advanced"};
	Dropdown.m_SelectionPopupContext.m_SelectionIndex = 1;
	{
		CUiScopedBackgroundRender Guard(&m_Ui);
		EXPECT_EQ(m_Ui.DoDropDown(&Row, 0, apEntries, 2, Dropdown), 0);
	}
	EXPECT_EQ(Graphics.m_vRoundedSubmissions.size(), SurfaceCount);
	EXPECT_EQ(Dropdown.m_SelectionPopupContext.m_SelectionIndex, 1);
	EXPECT_EQ(m_Ui.DoDropDown(&Row, 0, apEntries, 2, Dropdown), 1);
	EXPECT_EQ(Dropdown.m_SelectionPopupContext.m_SelectionIndex, -1);
}

TEST_F(QmCardSummaryLayout, OffscreenDropdownSubmissionSurvivesBridgeAndUpdatesFinalLayoutOnce)
{
	int Owner;
	CUiScopedInteractionOwner Source(&m_Ui, &Owner, "mode-card");
	CUi::SDropDownState Dropdown;
	const char *apEntries[] = {"Basic", "Advanced"};
	int Selection = 0, Commits = 0, LegacyInputs = 0;
	SSettingsCardDefinition Definition;
	Definition.m_PreLayoutInput = [&](CUIRect) { ++LegacyInputs; return false; };
	Definition.m_RenderMeasured = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		EXPECT_EQ(m_Ui.RenderOnly(), !Flow.Input());
		CUIRect Row;
		if(Flow.Control("mode", 24, Row))
		{
			const int Next = m_Ui.DoDropDown(&Row, Selection, apEntries, 2, Dropdown);
			if(Flow.Input() && Next != Selection)
			{
				Selection = Next;
				++Commits;
			}
		}
		if(Selection == 1)
			Flow.Text("advanced", "The newly selected mode declares more settings in this same frame.", 0, 12, 4, {}, [](const CUIRect &, const char *) {});
	};
	qm_settings_content::UseDeclaredContent(Definition, &m_Ui, m_Ui.TextRender());
	EXPECT_FALSE(Definition.m_HasPendingPreLayoutInput);
	EXPECT_FALSE(Definition.m_PreLayoutInput);
	CSettingsContentLayoutCache Cache;
	auto pLayout = Definition.m_ResolveContentLayout(Cache, 240);
	const float InitialHeight = pLayout->Height();
	Frame({10, 10});
	m_Ui.BeginPreLayoutInput();
	qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 40, 240, InitialHeight}, true, false, false, &m_Ui);
	m_Ui.EndPreLayoutInput();
	auto &Popup = Dropdown.m_SelectionPopupContext;
	Popup.m_vEntries = {"Basic", "Advanced"};
	Popup.m_ActiveIndex = 1;
	Popup.m_Width = 180;
	Popup.m_Props.m_Animate = false;
	Popup.m_Props.m_RequireSourceRefresh = false;
	m_Ui.ShowPopupSelection(40, 40, &Popup);
	Key(KEY_RETURN);
	m_Ui.RenderPopupMenus();
	ASSERT_EQ(Popup.m_SelectionIndex, 1);
	ASSERT_FALSE(m_Ui.IsPopupOpen(&Popup));
	EXPECT_TRUE(m_Ui.HasPendingInteractionSelection());
	{
		CUiScopedInteractionOwner Other(&m_Ui, &Owner, "other-card");
		EXPECT_FALSE(m_Ui.HasPendingInteractionSelection());
	}

	// 源卡离屏且没有 active 控件；DECLARE/DRAW 必须保留待提交选择。
	pLayout = Definition.m_ResolveContentLayout(Cache, 240);
	Definition.m_ContentLayoutDraw(*pLayout, {40, 1000, 240, pLayout->Height()});
	EXPECT_EQ(Selection, 0);
	EXPECT_EQ(Popup.m_SelectionIndex, 1);
	m_Ui.BeginPreLayoutInput();
	const bool Changed = qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 1000, 240, pLayout->Height()}, false, false, false, &m_Ui);
	m_Ui.EndPreLayoutInput();
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Selection, 1);
	EXPECT_EQ(Commits, 1);
	EXPECT_GT(pLayout->Height(), InitialHeight);
	EXPECT_NE(pLayout->Find("advanced"), nullptr);
	EXPECT_FALSE(m_Ui.HasPendingInteractionSelection());
	EXPECT_FALSE(qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 1000, 240, pLayout->Height()}, false, false, false, &m_Ui));
	Definition.m_ContentLayoutDraw(*pLayout, {40, 1000, 240, pLayout->Height()});
	EXPECT_EQ(Commits, 1);
	EXPECT_EQ(LegacyInputs, 0);
}

TEST_F(QmCardSummaryLayout, ClosingSourceDiscardsCommittedSelectionBeforeSourceReturns)
{
	int Owner;
	CUiScopedInteractionOwner Source(&m_Ui, &Owner, "mode-card");
	CUi::SDropDownState Dropdown;
	const char *apEntries[] = {"Basic", "Advanced"};
	CUIRect Row{40, 40, 240, 24};
	Frame({10, 10});
	m_Ui.DoDropDown(&Row, 0, apEntries, 2, Dropdown);
	auto &Popup = Dropdown.m_SelectionPopupContext;
	Popup.m_vEntries = {"Basic", "Advanced"};
	Popup.m_ActiveIndex = 1;
	Popup.m_Width = 180;
	Popup.m_Props.m_Animate = false;
	Popup.m_Props.m_RequireSourceRefresh = false;
	m_Ui.ShowPopupSelection(40, 40, &Popup);
	Key(KEY_RETURN);
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_Ui.HasPendingInteractionSelection());
	m_Ui.CloseInteractionSource(&Owner, "mode-card");
	EXPECT_FALSE(m_Ui.HasPendingInteractionSelection());
	EXPECT_EQ(m_Ui.DoDropDown(&Row, 0, apEntries, 2, Dropdown), 0);
	EXPECT_EQ(Popup.m_SelectionIndex, -1);
	EXPECT_FALSE(Popup.m_pCommit);
}

TEST_F(QmCardSummaryLayout, ChangedDropdownEntriesReleasePendingInputBeforeNextSelection)
{
	int Owner;
	CUiScopedInteractionOwner Source(&m_Ui, &Owner, "mode-card");
	CUi::SDropDownState Dropdown;
	const char *apOldEntries[] = {"Basic", "Advanced"};
	const char *apNewEntries[] = {"Basic", "Custom"};
	CUIRect Row{40, 40, 240, 24};
	Frame({10, 10});
	ASSERT_EQ(m_Ui.DoDropDown(&Row, 0, apOldEntries, 2, Dropdown), 0);

	auto &Popup = Dropdown.m_SelectionPopupContext;
	const auto ConfirmSecondEntry = [&](const char *const *ppEntries) {
		Frame({10, 10});
		Popup.m_vEntries = {ppEntries[0], ppEntries[1]};
		Popup.m_ActiveIndex = 1;
		Popup.m_Width = 180;
		Popup.m_Props.m_Animate = false;
		Popup.m_Props.m_RequireSourceRefresh = false;
		m_Ui.ShowPopupSelection(40, 40, &Popup);
		ASSERT_TRUE(m_Ui.IsPopupOpen(&Popup));
		Key(KEY_RETURN);
		m_Ui.RenderPopupMenus();
		ASSERT_EQ(Popup.m_SelectionIndex, 1);
		ASSERT_FALSE(m_Ui.IsPopupOpen(&Popup));
		ASSERT_TRUE(m_Ui.HasPendingInteractionSelection());
	};

	ASSERT_NO_FATAL_FAILURE(ConfirmSecondEntry(apOldEntries));
	EXPECT_EQ(m_Ui.DoDropDown(&Row, 0, apNewEntries, 2, Dropdown), 0);
	EXPECT_EQ(Popup.m_SelectionIndex, -1);
	EXPECT_FALSE(m_Ui.HasPendingInteractionSelection());

	ASSERT_NO_FATAL_FAILURE(ConfirmSecondEntry(apNewEntries));
	EXPECT_EQ(m_Ui.DoDropDown(&Row, 0, apNewEntries, 2, Dropdown), 1);
	EXPECT_FALSE(m_Ui.HasPendingInteractionSelection());
}

TEST_F(QmCardSummaryLayout, CanceledDropdownDoesNotCommitAfterOffscreenSourceReturns)
{
	CUi::SDropDownState Dropdown;
	const char *apEntries[] = {"Basic", "Advanced"};
	int Selection = 0, Inputs = 0;
	SSettingsCardDefinition Definition;
	Definition.m_HasPendingPreLayoutInput = [&] { return Dropdown.m_SelectionPopupContext.m_SelectionIndex >= 0; };
	Definition.m_RenderMeasured = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		if(Flow.Control("mode", 24, Row))
		{
			const int Next = m_Ui.DoDropDown(&Row, Selection, apEntries, 2, Dropdown);
			if(Flow.Input())
			{
				++Inputs;
				Selection = Next;
			}
		}
	};
	qm_settings_content::UseDeclaredContent(Definition, &m_Ui, m_Ui.TextRender());
	CSettingsContentLayoutCache Cache;
	auto pLayout = Definition.m_ResolveContentLayout(Cache, 240);
	Frame({10, 10});
	m_Ui.BeginPreLayoutInput();
	qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 40, 240, 24}, true, false, false, &m_Ui);
	m_Ui.EndPreLayoutInput();
	auto &Popup = Dropdown.m_SelectionPopupContext;
	Popup.m_vEntries = {"Basic", "Advanced"};
	Popup.m_ActiveIndex = 1;
	Popup.m_Width = 180;
	Popup.m_Props.m_Animate = false;
	Popup.m_Props.m_RequireSourceRefresh = false;
	m_Ui.ShowPopupSelection(40, 40, &Popup);
	Key(KEY_ESCAPE);
	m_Ui.RenderPopupMenus();
	ASSERT_FALSE(m_Ui.IsPopupOpen(&Popup));
	EXPECT_FALSE(Definition.m_HasPendingPreLayoutInput());
	EXPECT_FALSE(qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 1000, 240, 24}, false, false, false, &m_Ui));
	EXPECT_EQ(Inputs, 1);
	m_Ui.BeginPreLayoutInput();
	qm_settings_content::ExecuteInput(Definition, pLayout, Cache, {40, 40, 240, 24}, true, false, false, &m_Ui);
	m_Ui.EndPreLayoutInput();
	EXPECT_EQ(Selection, 0);
	EXPECT_EQ(Inputs, 2);
}

TEST_F(QmCardSummaryLayout, SubtitleOnlyAppearsOnCardHoverAndNeverChangesBodyGeometry)
{
	const SSettingsCardSpec Spec{"translate", "Translation", "Description shown only on hover"};
	const auto FrameGeometry = BuildSettingsCardFrame({40, 40, 320, 0}, Spec, 100, 1);
	ASSERT_GT(FrameGeometry.m_SubtitleRect.h, 0);
	IUiContext Context;
	Context.m_pUi = &m_Ui;
	Context.m_pTextRender = m_Ui.TextRender();
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const auto Draw = [&] {
		const auto Before = Graphics.m_Quads;
		const CUIRect PreviousContent = FrameGeometry.m_ContentRect;
		RenderSettingsCardSubtitle(Context, FrameGeometry, Spec, m_Ui.MouseHovered(&FrameGeometry.m_Rect));
		EXPECT_FLOAT_EQ(FrameGeometry.m_ContentRect.y, PreviousContent.y);
		EXPECT_FLOAT_EQ(FrameGeometry.m_ContentRect.h, PreviousContent.h);
		return Graphics.m_Quads - Before;
	};
	Frame({10, 10});
	const auto OutsideQuads = Draw();
	// 调用卡片实际使用的文字绘制入口；正文悬浮时提交副标题字形，不依赖 tooltip 服务。
	Frame({80, FrameGeometry.m_ContentRect.y + 20});
	EXPECT_GT(Draw(), OutsideQuads);
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_LE(Draw(), OutsideQuads);
	}
	Frame({10, 10});
	EXPECT_EQ(Draw(), OutsideQuads);
}

TEST_F(QmCardSummaryLayout, SubtitleHoverRemainsBoundedAtNarrowWidthsAndUiScales)
{
	IUiContext Context;
	Context.m_pUi = &m_Ui;
	Context.m_pTextRender = m_Ui.TextRender();
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	for(const float Scale : {0.78f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		Context.m_UiScale = Scale;
		const SSettingsCardSpec Spec{"narrow", "Title", "Long subtitle that must remain inside the narrow card"};
		const auto Geometry = BuildSettingsCardFrame({40, 40, 120, 0}, Spec, 40, Scale);
		ASSERT_GT(Geometry.m_SubtitleRect.w, 0);
		EXPECT_LE(Geometry.m_SubtitleRect.x + Geometry.m_SubtitleRect.w, Geometry.m_Rect.x + Geometry.m_Rect.w);
		const auto Draw = [&] {
			const auto Before = Graphics.m_Quads;
			RenderSettingsCardSubtitle(Context, Geometry, Spec, m_Ui.MouseHovered(&Geometry.m_Rect));
			return Graphics.m_Quads - Before;
		};
		Frame({10, 10});
		const auto Outside = Draw();
		Frame({80, Geometry.m_ContentRect.y + 10});
		EXPECT_GT(Draw(), Outside);
	}
}

TEST_F(QmCardSummaryLayout, EmptySubtitleDoesNotAddHoverText)
{
	IUiContext Context;
	Context.m_pUi = &m_Ui;
	Context.m_pTextRender = m_Ui.TextRender();
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const SSettingsCardSpec Spec{"empty", "Title", ""};
	const auto Geometry = BuildSettingsCardFrame({40, 40, 240, 0}, Spec, 40, 1);
	const auto Draw = [&] {
		const auto Before = Graphics.m_Quads;
		RenderSettingsCardSubtitle(Context, Geometry, Spec, m_Ui.MouseHovered(&Geometry.m_Rect));
		return Graphics.m_Quads - Before;
	};
	Frame({10, 10});
	const auto Outside = Draw();
	Frame({80, Geometry.m_ContentRect.y + 10});
	EXPECT_EQ(Draw(), Outside);
}

TEST_F(QmCardSummaryLayout, BodyDescriptionWrapsUsingRealTextAndMovesTheFollowingCard)
{
	CSettingsContentLayoutCache Cache;
	const std::vector<SSettingsContentRowDeclaration> vRows{{"result", "This translation result is a long description that must wrap onto more lines when the card becomes narrow.", 10, 10, 20, 4, true}};
	const auto Measure = [&](const char *pText, float Width, float FontSize) { return m_Ui.TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; };
	const auto pWide = Cache.Resolve(vRows, 500, m_Ui.TextRender()->GlyphAtlasRevision(), Measure);
	const auto pNarrow = Cache.Resolve(vRows, 120, m_Ui.TextRender()->GlyphAtlasRevision(), Measure);
	ASSERT_GT(pNarrow->Height(), pWide->Height());
	const auto WideCard = BuildSettingsCardFrame({0, 0, 528, 0}, {"wide", "Translation", "Tooltip"}, pWide->Height(), 1);
	const auto NarrowCard = BuildSettingsCardFrame({0, 0, 148, 0}, {"narrow", "Translation", "Tooltip"}, pNarrow->Height(), 1);
	CSettingsCardColumnFramePlan Wide(0, 16), Narrow(0, 16);
	Wide.Append(WideCard.m_Rect.h);
	Narrow.Append(NarrowCard.m_Rect.h);
	EXPECT_GT(Narrow.Append(100).m_Y, Wide.Append(100).m_Y);
	CUIRect Content = NarrowCard.m_ContentRect;
	CSettingsContentLayoutScope Scope(*pNarrow, ESettingsContentPass::DRAW);
	CSettingsContentLayoutFlow Flow(Content);
	Flow.Text("result", "", 0, 10, 0, {},
		[&](const CUIRect &Row, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Row.w;
			Props.m_EnableWidthCheck = false;
			EXPECT_FALSE(m_Ui.DoLabel(&Row, pText, 10, TEXTALIGN_ML, Props).m_Truncated);
		});
}

TEST_F(QmCardSummaryLayout, RealReleaseChangesFinalHeightBeforeDrawingWithoutRepeatingInput)
{
	bool Expanded = false;
	CButtonContainer Button;
	CSettingsContentLayoutCache Cache;
	const auto Program = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		if(Flow.Control("advanced", 20, Row))
		{
			const bool Clicked = ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &Button, "Advanced options", Row);
			if(Flow.Input() && Clicked)
				Expanded = !Expanded;
		}
		Flow.Space(4);
		if(Expanded)
			Flow.Text("details", "The expanded description adds a visible row measured by the real text renderer.", 20, 10, 4,
				{}, [](const CUIRect &, const char *) {});
	};
	const auto Resolve = [&](float Width) {
		std::vector<SSettingsContentRowDeclaration> vRows;
		{
			CSettingsContentLayoutScope Scope(vRows);
			CUIRect Content{0, 0, Width, 0};
			Program(Content);
		}
		return Cache.Resolve(vRows, Width, m_Ui.TextRender()->GlyphAtlasRevision(),
			[&](const char *pText, float TextWidth, float FontSize) { return m_Ui.TextRender()->TextBoundingBox(FontSize, pText, -1, TextWidth).m_H; });
	};
	auto pLayout = Resolve(240);
	const float InitialHeight = pLayout->Height();
	for(const bool Pressed : {false, true, false})
	{
		Frame({80, 50}, Pressed);
		m_Ui.BeginPreLayoutInput();
		const bool Changed = ExecuteSettingsContentInputThenResolve(pLayout, {40, 40, 240, pLayout->Height()}, [&](const CSettingsContentLayout &Layout, CUIRect Content) {
				CSettingsContentLayoutScope Scope(Layout, ESettingsContentPass::INPUT);
				Program(Content); }, Resolve);
		m_Ui.EndPreLayoutInput();
		pLayout = Resolve(240);
		if(!Pressed && Expanded)
		{
			EXPECT_TRUE(Changed);
			EXPECT_GT(pLayout->Height(), InitialHeight);
			EXPECT_NE(pLayout->Find("details"), nullptr);
		}
		CUiScopedBackgroundRender ReadOnlyDraw(&m_Ui);
		CSettingsContentLayoutScope Scope(*pLayout, ESettingsContentPass::DRAW);
		CUIRect Content{40, 40, 240, pLayout->Height()};
		Program(Content);
	}
	EXPECT_TRUE(Expanded);
	EXPECT_GT(pLayout->Height(), InitialHeight);
}

TEST_F(QmCardSummaryLayout, MultilineInputUsesActualControlWidthAndReusesStableMeasurements)
{
	const char *pWords = "first long word, second long word, third long word, fourth long word, fifth long word";
	CSettingsContentLayoutCache Cache;
	int Measures = 0;
	const auto Measure = [&](const SSettingsContentRowDeclaration &Row, float Width) {
		++Measures;
		return qm_card_catalog::CalcQiaFenInputHeight(m_Ui.TextRender(), Row.m_Text.c_str(), Width, Row.m_FontSize, Row.m_TextLineSpacing, Row.m_MinimumHeight);
	};
	const auto Resolve = [&](float Width) {
		CUIRect Content{0, 0, Width, 0}, Row{};
		const auto Metrics = ResolveSettingsContentMetrics(Width);
		const auto Columns = ResolveSettingsControlRowLayout(Content, Metrics);
		std::vector<SSettingsContentRowDeclaration> vRows;
		{
			CSettingsContentLayoutScope Scope(vRows);
			CSettingsContentLayoutFlow Flow(Content);
			EXPECT_FALSE(Flow.MeasuredControl("word-list", pWords, Metrics.m_LineHeight, Metrics.m_BodySize, Columns.m_ControlRect.x, 2, 2, Row, Measure));
		}
		return Cache.Resolve(vRows, Width, m_Ui.TextRender()->GlyphAtlasRevision(), {}, Measure);
	};
	const auto pWide = Resolve(500);
	EXPECT_EQ(Resolve(500), pWide);
	EXPECT_EQ(Measures, 1);
	const auto pNarrow = Resolve(160);
	ASSERT_GT(pNarrow->Height(), pWide->Height());
	const auto Metrics = ResolveSettingsContentMetrics(160);
	const auto Columns = ResolveSettingsControlRowLayout({0, 0, 160, 0}, Metrics);
	EXPECT_FLOAT_EQ(pNarrow->Height(), qm_card_catalog::CalcQiaFenInputHeight(m_Ui.TextRender(), pWords, Columns.m_ControlRect.w, Metrics.m_BodySize, 2, Metrics.m_LineHeight));
	EXPECT_FLOAT_EQ(pNarrow->Find("word-list")->m_Rect.w, 160);
	EXPECT_EQ(Resolve(160), pNarrow);
	EXPECT_EQ(Measures, 2);
}
