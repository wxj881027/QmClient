// 执行真实 Deck、卡片壳、模型和 CUi；仅由共享 fixture 替换设备边界。
#include <engine/keys.h>

#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardInfo.h>
#include <game/client/QmUi/SettingsMenuPrewarmScope.h>
#include <game/client/QmUi/UiContext.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{
	constexpr const char *CARD_A = "deck:appearance-name-plate-hook-strength";
	constexpr const char *CARD_B = "deck:appearance-name-plate-key-presses";
	constexpr const char *TAB = "deck-consumer-test";

	struct SConsumerFailure
	{
		std::optional<CPredictionAllocationFailure> m_Active;
		bool m_Triggered = false;
		size_t m_Attempts = 0;

		void Start(size_t BeforeFailure)
		{
			m_Triggered = false;
			m_Attempts = 0;
			m_Active.emplace(BeforeFailure);
		}
		void Stop() noexcept
		{
			if(!m_Active)
				return;
			m_Triggered = m_Active->Triggered();
			m_Attempts = m_Active->Attempts();
			m_Active.reset();
		}
	};

	struct SCardOwner
	{
		std::string m_Id;
		float m_Height;
		uint64_t m_MeasureRevision = 1;
		int m_Measures = 0;
		int m_Draws = 0;
		int m_HeaderInputs = 0;

		SCardOwner(const char *pId, float Height) :
			m_Id(pId), m_Height(Height) {}
	};

	void AddCard(std::vector<SSettingsCardDefinition> &vCards, const std::shared_ptr<SCardOwner> &pOwner, bool ReadOnly = false, SConsumerFailure *pFailure = nullptr)
	{
		SSettingsCardDefinition Card;
		Card.m_Spec = {pOwner->m_Id.c_str(), "Card", nullptr};
		Card.m_MeasureRevision = pOwner->m_MeasureRevision;
		Card.m_IsCollapsed = [] { return false; };
		Card.m_IsVisible = [pOwner, pFailure] {
			// 绑定准备已经结束，后面的真实测量/绘制不属于本次拒收窗口。
			if(pFailure != nullptr)
				pFailure->Stop();
			return pOwner != nullptr;
		};
		Card.m_Measure = [pOwner](float) { ++pOwner->m_Measures; return pOwner->m_Height; };
		Card.m_Render = [pOwner](CUIRect) { ++pOwner->m_Draws; };
		Card.m_PreLayoutHeaderInput = [pOwner, ReadOnly](const SSettingsCardFrame &, bool) {
			if(!ReadOnly)
				++pOwner->m_HeaderInputs;
			return false;
		};
		vCards.push_back(std::move(Card));
	}

	class QmSettingsCardDeckConsumer : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		qm_card_order::CModel m_Model;
		SSettingsPageLayoutFrame m_Layout;
		SSettingsCardDeckFrameDiagnostics m_Diagnostics;
		SCardMotionSpec m_Motion;
		SSettingsCardDeckVisualOptions m_VisualOptions;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			m_Model.SetEntries({{CARD_A, TAB, 1, 0}, {CARD_B, TAB, 2, 0}});
			m_Layout = SettingsPageLayout({20, 20, 800, 560}, 1.0f);
		}
		void Frame(vec2 Position = {900, 600}, bool Pressed = false)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
		template<typename FBuild>
		SSettingsCardDeckResult Draw(CSettingsCardDeck &Deck, uint64_t Revision, FBuild &&Build)
		{
			m_Diagnostics = {};
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pTextRender = m_Ui.TextRender();
			Ctx.m_ScopeHash = MakeUiScopeHash("deck-consumer-test");
			SSettingsCardDeckInput Input;
			Input.m_MouseX = m_Ui.MouseX();
			Input.m_MouseY = m_Ui.MouseY();
			Input.m_MousePressed = m_Ui.MouseButtonClicked(0);
			Input.m_MouseDown = m_Ui.MouseButton(0);
			Input.m_MouseReleased = !Input.m_MouseDown && m_Ui.LastMouseButton(0);
			Input.m_AllowHeaderDrag = false;
			Input.m_pDiagnostics = &m_Diagnostics;
			return Deck.RenderCached(Ctx, m_Layout, TAB, Revision, std::forward<FBuild>(Build), m_Model, nullptr, Input, m_Motion, m_VisualOptions, 1);
		}
		SSettingsCardDeckResult DrawDirect(CSettingsCardDeck &Deck, const std::vector<SSettingsCardDefinition> &vCards)
		{
			m_Diagnostics = {};
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pTextRender = m_Ui.TextRender();
			Ctx.m_ScopeHash = MakeUiScopeHash("deck-consumer-test");
			SSettingsCardDeckInput Input;
			Input.m_MouseX = m_Ui.MouseX();
			Input.m_MouseY = m_Ui.MouseY();
			Input.m_MousePressed = m_Ui.MouseButtonClicked(0);
			Input.m_MouseDown = m_Ui.MouseButton(0);
			Input.m_MouseReleased = !Input.m_MouseDown && m_Ui.LastMouseButton(0);
			Input.m_AllowHeaderDrag = false;
			Input.m_pDiagnostics = &m_Diagnostics;
			return Deck.Render(Ctx, m_Layout, TAB, vCards, m_Model, nullptr, Input, m_Motion, m_VisualOptions);
		}
		template<typename FDraw>
		void PressWidthButton(FDraw &&DrawCards)
		{
			Frame();
			DrawCards();
			ASSERT_EQ(m_Diagnostics.m_GeometryCount, 1u);
			const auto &Geometry = m_Diagnostics.m_aGeometry[0];
			const SSettingsCardSpec Spec{CARD_A, "Card", nullptr};
			const auto Shell = ResolveSettingsCardHeaderActions(BuildSettingsCardFrame({Geometry.m_Rect.x, Geometry.m_Rect.y, Geometry.m_Rect.w, 0}, Spec, 40, 1), 0, HasSettingsCardInfo(Spec));
			const CUIRect Button = SettingsCardWidthButtonRect(Shell);
			ASSERT_GT(Button.w, 0.0f);
			const vec2 Pointer{Button.x + Button.w * 0.5f, Button.y + Button.h * 0.5f};
			Frame(Pointer);
			DrawCards();
			Frame(Pointer, true);
			DrawCards();
		}
		void ExpectGeometry(const char *pFirst, float FirstHeight, const char *pSecond = nullptr, float SecondHeight = 0.0f)
		{
			ASSERT_EQ(m_Diagnostics.m_GeometryCount, pSecond != nullptr ? 2u : 1u);
			EXPECT_STREQ(m_Diagnostics.m_aGeometry[0].m_pStableId, pFirst);
			EXPECT_FLOAT_EQ(m_Diagnostics.m_aGeometry[0].m_TargetContentHeight, FirstHeight);
			if(pSecond != nullptr)
			{
				EXPECT_STREQ(m_Diagnostics.m_aGeometry[1].m_pStableId, pSecond);
				EXPECT_FLOAT_EQ(m_Diagnostics.m_aGeometry[1].m_TargetContentHeight, SecondHeight);
			}
		}
	};
}

TEST_F(QmSettingsCardDeckConsumer, EveryConsumerAllocationRejectionKeepsTheOldOwnerAndRetriesTheSameKey)
{
	CSettingsCardDeck Trace;
	SConsumerFailure TraceFailure;
	const auto pTraceA = std::make_shared<SCardOwner>(CARD_A, 80);
	const auto pTraceB = std::make_shared<SCardOwner>(CARD_B, 30);
	Frame();
	Draw(Trace, 1, [&](auto &vCards) { AddCard(vCards, pTraceA); });
	Draw(Trace, 2, [&](auto &vCards) {
		AddCard(vCards, pTraceA, false, &TraceFailure);
		AddCard(vCards, pTraceB, false, &TraceFailure);
		TraceFailure.Start(std::numeric_limits<size_t>::max());
	});
	TraceFailure.Stop();
	ASSERT_FALSE(TraceFailure.m_Triggered);
	ASSERT_GT(TraceFailure.m_Attempts, 0u);
	ASSERT_LE(TraceFailure.m_Attempts, 4096u);
	for(size_t Allocation = 0; Allocation < TraceFailure.m_Attempts; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsCardDeck Deck;
		SConsumerFailure Failure;
		auto pOld = std::make_shared<SCardOwner>(CARD_A, 20);
		const std::weak_ptr<SCardOwner> OldOwner = pOld;
		Draw(Deck, 1, [&](auto &vCards) { AddCard(vCards, pOld); });
		pOld.reset();
		const auto pNewA = std::make_shared<SCardOwner>(CARD_A, 80);
		const auto pNewB = std::make_shared<SCardOwner>(CARD_B, 30);
		int Builds = 0;
		const auto Build = [&](auto &vCards) {
			++Builds;
			AddCard(vCards, pNewA, false, &Failure);
			AddCard(vCards, pNewB, false, &Failure);
		};
		bool Threw = false;
		try
		{
			Draw(Deck, 2, [&](auto &vCards) { Build(vCards); Failure.Start(Allocation); });
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Failure.Stop();
		ASSERT_FALSE(Threw);
		ASSERT_TRUE(Failure.m_Triggered);
		EXPECT_FALSE(OldOwner.expired());
		EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
		EXPECT_EQ(pNewA->m_Measures, 0);
		EXPECT_EQ(pNewB->m_Measures, 0);
		Draw(Deck, 2, Build);
		EXPECT_EQ(Builds, 2);
		EXPECT_TRUE(OldOwner.expired());
		EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 1u);
		EXPECT_EQ(pNewA->m_Measures, 1);
		EXPECT_EQ(pNewB->m_Measures, 1);
		ExpectGeometry(CARD_A, 80, CARD_B, 30);
	}
}

TEST_F(QmSettingsCardDeckConsumer, UnchangedKeyAndDisplayReentryReuseMeasurements)
{
	CSettingsCardDeck Deck;
	const auto pOwner = std::make_shared<SCardOwner>(CARD_A, 40);
	int Builds = 0;
	const auto Build = [&](auto &vCards) { ++Builds; AddCard(vCards, pOwner); };
	Frame();
	Draw(Deck, 1, Build);
	ASSERT_EQ(pOwner->m_Measures, 1);
	Frame();
	Draw(Deck, 1, Build);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	EXPECT_EQ(m_Diagnostics.m_MeasureCount, 0u);
	Deck.BeginDisplayCycle(2, false);
	Frame();
	Draw(Deck, 1, Build);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(pOwner->m_Measures, 1);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	ExpectGeometry(CARD_A, 40);
}

TEST_F(QmSettingsCardDeckConsumer, SameAddressModelReplacementCanDeferAndRebindTheNewIndices)
{
	CSettingsCardDeck Deck;
	const auto pA = std::make_shared<SCardOwner>(CARD_A, 80);
	const auto pB = std::make_shared<SCardOwner>(CARD_B, 30);
	int Builds = 0;
	const auto Build = [&](auto &vCards) { ++Builds; AddCard(vCards, pA); AddCard(vCards, pB); };
	Frame();
	Draw(Deck, 1, Build);
	ExpectGeometry(CARD_A, 80, CARD_B, 30);
	const auto *pModel = &m_Model;
	qm_card_order::CModel Candidate;
	Candidate.SetEntries({{CARD_B, TAB, 1, 0}, {CARD_A, TAB, 2, 0}});
	m_Model.SwapContent(Candidate);
	SConsumerFailure Failure;
	bool Threw = false;
	Failure.Start(0);
	try
	{
		Draw(Deck, 1, Build);
	}
	catch(const std::bad_alloc &)
	{
		Threw = true;
	}
	Failure.Stop();
	EXPECT_FALSE(Threw);
	EXPECT_TRUE(Failure.m_Triggered);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	Frame();
	Draw(Deck, 1, Build);
	EXPECT_EQ(&m_Model, pModel);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 1u);
	ExpectGeometry(CARD_B, 30, CARD_A, 80);
}

TEST_F(QmSettingsCardDeckConsumer, DefinitionRetirementKeepsOwnedIdsAndUnchangedNeighbourMeasurements)
{
	CSettingsCardDeck Deck;
	auto pOldA = std::make_shared<SCardOwner>(CARD_A, 20);
	auto pOldB = std::make_shared<SCardOwner>(CARD_B, 30);
	const std::weak_ptr<SCardOwner> OldA = pOldA, OldB = pOldB;
	Frame();
	Draw(Deck, 1, [&](auto &vCards) { AddCard(vCards, pOldA); AddCard(vCards, pOldB); });
	pOldA.reset();
	pOldB.reset();
	const auto pA = std::make_shared<SCardOwner>(CARD_A, 80);
	const auto pB = std::make_shared<SCardOwner>(CARD_B, 30);
	pA->m_MeasureRevision = 2;
	const auto Build = [&](auto &vCards) { AddCard(vCards, pA); AddCard(vCards, pB); };
	Frame();
	Draw(Deck, 2, Build);
	EXPECT_TRUE(OldA.expired());
	EXPECT_TRUE(OldB.expired());
	EXPECT_EQ(pA->m_Measures, 1);
	EXPECT_EQ(pB->m_Measures, 0);
	EXPECT_EQ(m_Diagnostics.m_MeasureCount, 1u);
	ExpectGeometry(CARD_A, 80, CARD_B, 30);
	Frame();
	Draw(Deck, 2, Build);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	EXPECT_EQ(m_Diagnostics.m_MeasureCount, 0u);
	ExpectGeometry(CARD_A, 80, CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, BackgroundAndMeasurementPassesRebindBeforeReturningToInteractiveInput)
{
	CSettingsCardDeck Deck;
	const auto pOwner = std::make_shared<SCardOwner>(CARD_A, 40);
	int Builds = 0;
	const auto Build = [&](auto &vCards) { ++Builds; AddCard(vCards, pOwner, m_Ui.RenderOnly()); };
	Frame();
	Draw(Deck, 1, Build);
	ASSERT_EQ(pOwner->m_HeaderInputs, 1);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw(Deck, 1, Build);
		EXPECT_EQ(pOwner->m_HeaderInputs, 1);
	}
	Draw(Deck, 1, Build);
	EXPECT_EQ(pOwner->m_HeaderInputs, 2);
	RunSettingsTextPlanMeasurement(&m_Ui, [&] {
		Draw(Deck, 1, Build);
		EXPECT_EQ(pOwner->m_HeaderInputs, 2);
	});
	Draw(Deck, 1, Build);
	EXPECT_EQ(Builds, 5);
	EXPECT_EQ(pOwner->m_HeaderInputs, 3);
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	ExpectGeometry(CARD_A, 40);
}

TEST_F(QmSettingsCardDeckConsumer, ViewChangeClosesItsPopupBeforeRetiringTheOldDefinitionOwner)
{
	CSettingsCardDeck Deck;
	auto pOld = std::make_shared<SCardOwner>(CARD_A, 40);
	const std::weak_ptr<SCardOwner> OldOwner = pOld;
	Frame();
	Draw(Deck, 1, [&](auto &vCards) { AddCard(vCards, pOld); });
	pOld.reset();
	struct SClosed
	{
		int m_Count = 0;
		bool m_OwnerAlive = false;
	};
	const auto pClosed = std::make_shared<SClosed>();
	SPopupMenuId Popup;
	SPopupMenuProperties Props;
	Props.m_AutoReposition = false;
	Props.m_Animate = false;
	Props.m_OnClosed = [pClosed, OldOwner] { ++pClosed->m_Count; pClosed->m_OwnerAlive = !OldOwner.expired(); };
	{
		CUiScopedInteractionOwner Source(&m_Ui, &Deck, CARD_A);
		m_Ui.DoPopupMenu(&Popup, 40, 40, 240, 120, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	}
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Popup));
	const auto pNew = std::make_shared<SCardOwner>(CARD_B, 30);
	Draw(Deck, 2, [&](auto &vCards) { AddCard(vCards, pNew); });
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Popup));
	EXPECT_EQ(pClosed->m_Count, 1);
	EXPECT_TRUE(pClosed->m_OwnerAlive);
	EXPECT_TRUE(OldOwner.expired());
	ExpectGeometry(CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, CloseCallbackModelChangeCanRejectPreparationWithoutPublishingDefinitions)
{
	CSettingsCardDeck Deck;
	auto pOld = std::make_shared<SCardOwner>(CARD_A, 40);
	const std::weak_ptr<SCardOwner> OldOwner = pOld;
	Frame();
	Draw(Deck, 1, [&](auto &vCards) { AddCard(vCards, pOld); });
	pOld.reset();
	const auto pFailure = std::make_shared<SConsumerFailure>();
	SPopupMenuId Popup;
	SPopupMenuProperties Props;
	Props.m_AutoReposition = false;
	Props.m_Animate = false;
	Props.m_OnClosed = [this, pFailure] {
		m_Model.SetEntries({{CARD_B, TAB, 1, 0}, {CARD_A, TAB, 2, 0}});
		pFailure->Start(0);
	};
	{
		CUiScopedInteractionOwner Source(&m_Ui, &Deck, CARD_A);
		m_Ui.DoPopupMenu(&Popup, 40, 40, 240, 120, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	}
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Popup));
	const auto pNew = std::make_shared<SCardOwner>(CARD_B, 30);
	int Builds = 0;
	const auto Build = [&](auto &vCards) { ++Builds; AddCard(vCards, pNew, false, pFailure.get()); };
	bool Threw = false;
	try
	{
		Draw(Deck, 2, Build);
	}
	catch(const std::bad_alloc &)
	{
		Threw = true;
	}
	pFailure->Stop();
	EXPECT_FALSE(Threw);
	ASSERT_TRUE(pFailure->m_Triggered);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Popup));
	EXPECT_FALSE(OldOwner.expired());
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	EXPECT_EQ(pNew->m_Measures, 0);
	Draw(Deck, 2, Build);
	EXPECT_EQ(Builds, 2);
	EXPECT_TRUE(OldOwner.expired());
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 1u);
	ExpectGeometry(CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, ReplacingRuntimeStorageReleasesThePressedWidthButton)
{
	CSettingsCardDeck Deck;
	const auto pA = std::make_shared<SCardOwner>(CARD_A, 40);
	const auto pB = std::make_shared<SCardOwner>(CARD_B, 30);
	const auto BuildA = [&](auto &vCards) { AddCard(vCards, pA); };
	Frame();
	Draw(Deck, 1, BuildA);
	ASSERT_EQ(m_Diagnostics.m_GeometryCount, 1u);
	const auto &Geometry = m_Diagnostics.m_aGeometry[0];
	const SSettingsCardSpec Spec{CARD_A, "Card", nullptr};
	const auto Shell = ResolveSettingsCardHeaderActions(BuildSettingsCardFrame({Geometry.m_Rect.x, Geometry.m_Rect.y, Geometry.m_Rect.w, 0}, Spec, 40, 1), 0, HasSettingsCardInfo(Spec));
	const CUIRect Button = SettingsCardWidthButtonRect(Shell);
	ASSERT_GT(Button.w, 0.0f);
	const vec2 Pointer{Button.x + Button.w * 0.5f, Button.y + Button.h * 0.5f};
	Frame(Pointer);
	Draw(Deck, 1, BuildA);
	Frame(Pointer, true);
	Draw(Deck, 1, BuildA);
	ASSERT_NE(m_Ui.ActiveItem(), nullptr);
	Frame(Pointer, true);
	Draw(Deck, 2, [&](auto &vCards) { AddCard(vCards, pB); });
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	ExpectGeometry(CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, CachedBackgroundViewChangeDefersWithItsOldOwnerAndPressedWidthButton)
{
	CSettingsCardDeck Deck;
	auto pOld = std::make_shared<SCardOwner>(CARD_A, 40);
	const std::weak_ptr<SCardOwner> OldOwner = pOld;
	const auto BuildOld = [&](auto &vCards) { AddCard(vCards, pOld); };
	PressWidthButton([&] { Draw(Deck, 1, BuildOld); });
	ASSERT_NE(m_Ui.ActiveItem(), nullptr);
	const void *pPressed = m_Ui.ActiveItem();
	pOld.reset();
	const auto pNew = std::make_shared<SCardOwner>(CARD_B, 30);
	int Builds = 0;
	const auto BuildNew = [&](auto &vCards) { ++Builds; AddCard(vCards, pNew, m_Ui.RenderOnly()); };
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw(Deck, 2, BuildNew);
		EXPECT_EQ(m_Ui.ActiveItem(), pPressed);
		EXPECT_FALSE(OldOwner.expired());
		EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
		EXPECT_EQ(m_Diagnostics.m_MeasureCount, 0u);
		EXPECT_EQ(m_Diagnostics.m_GeometryCount, 0u);
		EXPECT_EQ(pNew->m_Measures, 0);
	}
	Frame({900, 600}, true);
	Draw(Deck, 2, BuildNew);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	EXPECT_TRUE(OldOwner.expired());
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 1u);
	EXPECT_EQ(pNew->m_Measures, 1);
	ExpectGeometry(CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, DirectBackgroundViewChangeDefersUntilItsPressedRuntimeCanBeRetired)
{
	CSettingsCardDeck Deck;
	auto pOld = std::make_shared<SCardOwner>(CARD_A, 40);
	const std::weak_ptr<SCardOwner> OldOwner = pOld;
	std::vector<SSettingsCardDefinition> vOld;
	AddCard(vOld, pOld);
	pOld.reset();
	PressWidthButton([&] { DrawDirect(Deck, vOld); });
	ASSERT_NE(m_Ui.ActiveItem(), nullptr);
	const void *pPressed = m_Ui.ActiveItem();
	const auto pNew = std::make_shared<SCardOwner>(CARD_B, 30);
	std::vector<SSettingsCardDefinition> vNew;
	AddCard(vNew, pNew);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		DrawDirect(Deck, vNew);
		EXPECT_EQ(m_Ui.ActiveItem(), pPressed);
		EXPECT_FALSE(OldOwner.expired());
		EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
		EXPECT_EQ(m_Diagnostics.m_MeasureCount, 0u);
		EXPECT_EQ(m_Diagnostics.m_GeometryCount, 0u);
		EXPECT_EQ(pNew->m_Measures, 0);
	}
	Frame({900, 600}, true);
	DrawDirect(Deck, vNew);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 1u);
	ExpectGeometry(CARD_B, 30);
	// 直传定义由调用方持有，绑定成功后才允许其释放上一份 owner。
	vOld.clear();
	EXPECT_TRUE(OldOwner.expired());
	DrawDirect(Deck, vNew);
	EXPECT_EQ(m_Diagnostics.m_DefinitionsPrepareCount, 0u);
	EXPECT_EQ(m_Diagnostics.m_MeasureCount, 0u);
	ExpectGeometry(CARD_B, 30);
}

TEST_F(QmSettingsCardDeckConsumer, PreparedRevealStringCommitsWithoutAllocationAndIsConsumedOnce)
{
	CSettingsCardDeck Deck;
	const auto pOwner = std::make_shared<SCardOwner>(CARD_A, 40);
	const auto Build = [&](auto &vCards) { AddCard(vCards, pOwner); };
	std::string StableId = CARD_A;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Deck.RequestReveal(std::move(StableId));
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	Frame();
	EXPECT_STREQ(Draw(Deck, 1, Build).m_pRevealedStableId, CARD_A);
	EXPECT_EQ(Draw(Deck, 1, Build).m_pRevealedStableId, nullptr);
	Deck.RequestReveal(CARD_A);
	Deck.RequestReveal(nullptr);
	EXPECT_EQ(Draw(Deck, 1, Build).m_pRevealedStableId, nullptr);
}
