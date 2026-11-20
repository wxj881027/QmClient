#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>
#include <test/support/tracked_allocation_resource.h>

#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>

namespace
{
	struct SCopyState
	{
		int m_Copies = 0;
		int m_Calls = 0;
		bool m_FailCopy = false;
		int m_FailAtCopy = -1;
	};
	struct CCopySensitiveCallback
	{
		std::shared_ptr<SCopyState> m_pState;
		std::array<int, 32> m_Payload{};
		explicit CCopySensitiveCallback(std::shared_ptr<SCopyState> pState) :
			m_pState(std::move(pState)) {}
		CCopySensitiveCallback(const CCopySensitiveCallback &Other) :
			m_pState(Other.m_pState), m_Payload(Other.m_Payload)
		{
			++m_pState->m_Copies;
			if(m_pState->m_FailCopy || m_pState->m_Copies == m_pState->m_FailAtCopy)
				throw std::bad_alloc();
		}
		CCopySensitiveCallback(CCopySensitiveCallback &&) noexcept = default;
		void operator()() const { ++m_pState->m_Calls; }
	};
	struct SLifetimeToken
	{
		std::function<void()> m_OnDestroy;
		~SLifetimeToken()
		{
			if(m_OnDestroy)
				m_OnDestroy();
		}
	};
	// 先构造、最后销毁内存域，覆盖整个真实UI fixture及其队列的寿命。
	class CPopupMemoryOwner
	{
	protected:
		CTrackedAllocationResource m_Memory;
		~CPopupMemoryOwner() { EXPECT_EQ(m_Memory.m_Live, 0); }
	};
}

class QmPopupCallbackLifetime : public CPopupMemoryOwner, public qm_ui_test::CRealUiFixture
{
protected:
	qm_ui_test::CTestInput m_Input;
	qm_ui_test::CTestUiClient m_Client;
	SPopupMenuId m_A, m_B, m_C;
	CButtonContainer m_Button;
	int m_Closed = 0, m_Global = 0, m_EventCount = 0, m_ContentCalls = 0;
	std::array<int, 32> m_Events{};
	bool m_AllRemoved = true, m_AliveInChild = false, m_AliveInGlobal = false;
	std::weak_ptr<SLifetimeToken> m_Weak;
	IInput *UiInput() override { return &m_Input; }
	IClient *UiClient() override { return &m_Client; }
	void Event(int Value) { m_Events.at(m_EventCount++) = Value; }
	void ExpectEvents(std::initializer_list<int> Values)
	{
		ASSERT_EQ(m_EventCount, static_cast<int>(Values.size()));
		int Index = 0;
		for(int Value : Values)
			EXPECT_EQ(m_Events[Index++], Value);
	}
	static CUi::EPopupMenuFunctionResult Content(void *pContext, CUIRect, bool)
	{
		++static_cast<QmPopupCallbackLifetime *>(pContext)->m_ContentCalls;
		return CUi::POPUP_KEEP_OPEN;
	}
	SPopupMenuProperties Properties()
	{
		SPopupMenuProperties Props;
		Props.m_AutoReposition = false;
		Props.m_Animate = false;
		return Props;
	}
	void Open(const SPopupMenuId *pId, std::function<void()> Callback, bool Animated = false)
	{
		auto Props = Properties();
		Props.m_Animate = Animated;
		Props.m_OnClosed = std::move(Callback);
		m_Ui.DoPopupMenu(pId, 40, 40, 240, 120, this, Content, Props);
	}
	void TearDown() override
	{
		m_Memory.m_FailAt = -1;
		m_Ui.ClosePopupMenus(false);
		m_Ui.SetPopupMenuClosedCallback({});
		CRealUiFixture::TearDown();
	}

public:
	QmPopupCallbackLifetime() :
		CRealUiFixture(&m_Memory) {}
};

TEST_F(QmPopupCallbackLifetime, SingleCloseDoesNotCopyTheCallbackOrGrowItsStorage)
{
	auto pState = std::make_shared<SCopyState>();
	Open(&m_A, CCopySensitiveCallback(pState));
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	const int Copies = pState->m_Copies, Allocations = m_Memory.m_Allocations;
	pState->m_FailCopy = true;
	m_Memory.m_FailAt = Allocations + 1;
	EXPECT_NO_THROW(m_Ui.ClosePopupMenu(&m_A, true));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_A));
	EXPECT_EQ(pState->m_Calls, 1);
	EXPECT_EQ(pState->m_Copies, Copies);
	EXPECT_EQ(m_Memory.m_Allocations, Allocations);
}

TEST_F(QmPopupCallbackLifetime, CloseAllDoesNotCopyCallbacksOrGrowTheirStorage)
{
	auto pState = std::make_shared<SCopyState>();
	Open(&m_A, CCopySensitiveCallback(pState));
	Open(&m_B, CCopySensitiveCallback(pState));
	const int Copies = pState->m_Copies, Allocations = m_Memory.m_Allocations;
	pState->m_FailCopy = true;
	m_Memory.m_FailAt = Allocations + 1;
	EXPECT_NO_THROW(m_Ui.ClosePopupMenus(false));
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_EQ(pState->m_Calls, 2);
	EXPECT_EQ(pState->m_Copies, Copies);
	EXPECT_EQ(m_Memory.m_Allocations, Allocations);
}

TEST_F(QmPopupCallbackLifetime, ParentAndChildAreRemovedBeforeForwardNotificationsAndGlobalNotice)
{
	Open(&m_A, [this] { Event(1); m_AllRemoved &= !m_Ui.IsPopupOpen(&m_A) && !m_Ui.IsPopupOpen(&m_B); });
	Open(&m_B, [this] { Event(2); m_AllRemoved &= !m_Ui.IsPopupOpen(); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_TRUE(m_AllRemoved);
	ExpectEvents({1, 2, 3});
}

TEST_F(QmPopupCallbackLifetime, AnimatedParentNotifiesEachOwnerOnlyAtLogicalClose)
{
	g_Config.m_QmUiMotionLevel = 2;
	Open(&m_A, [this] { ++m_Closed; }, true);
	Open(&m_B, [this] { ++m_Closed; }, true);
	m_Ui.ClosePopupMenu(&m_A);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_TRUE(m_Ui.IsPopupVisible(&m_A));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_B));
	EXPECT_EQ(m_Closed, 2);
	m_Ui.ClosePopupMenu(&m_A);
	m_Ui.RenderPopupMenus();
	EXPECT_EQ(m_Closed, 2);
	EXPECT_EQ(m_ContentCalls, 0);
}

TEST_F(QmPopupCallbackLifetime, ReopeningTheClosingParentDoesNotReplacePendingChildNotifications)
{
	g_Config.m_QmUiMotionLevel = 2;
	Open(&m_A, [this] { Event(1); Open(&m_A, [this] { Event(4); }, true); }, true);
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	m_Ui.ClosePopupMenu(&m_A);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_B));
	ExpectEvents({1, 2, 3});
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2, 3, 4, 3});
}

TEST_F(QmPopupCallbackLifetime, FailedReopenInsideNotificationDoesNotAbortTheOldBatch)
{
	g_Config.m_QmUiMotionLevel = 2;
	Open(&m_A, [this] {
		Event(1);
		m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
		Open(&m_A, [this] { Event(4); }, true); }, true);
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	m_Ui.ClosePopupMenu(&m_A);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	ExpectEvents({1, 2, 3});
	m_Memory.m_FailAt = -1;
	Open(&m_A, [this] { Event(4); }, true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2, 3, 4, 3});
}

TEST_F(QmPopupCallbackLifetime, NestedCloseKeepsItsCallbacksAndGlobalNoticeInItsOwnBatch)
{
	Open(&m_A, [this] { Event(4); });
	Open(&m_B, [this] {
		Event(1);
		Open(&m_C, [this] { Event(2); });
		m_Ui.ClosePopupMenu(&m_C, true);
		Event(3);
	});
	m_Ui.SetPopupMenuClosedCallback([this] { Event(9); });
	m_Ui.ClosePopupMenu(&m_B, true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	ExpectEvents({1, 2, 9, 3, 9});
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2, 9, 3, 9, 4, 9});
}

TEST_F(QmPopupCallbackLifetime, ClosingAllFromANotificationKeepsTheRemainingOuterCallbacks)
{
	Open(&m_A, [this] {
		Event(1);
		Open(&m_C, [this] { Event(4); });
		m_Ui.ClosePopupMenus(false);
	});
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 4, 3, 2, 3});
	EXPECT_FALSE(m_Ui.IsPopupOpen());
}

TEST_F(QmPopupCallbackLifetime, CallbackAllocationExceptionStillNotifiesChildAndGlobalOnce)
{
	Open(&m_A, [this] { Event(1); throw std::bad_alloc(); });
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	EXPECT_NO_THROW(m_Ui.ClosePopupMenu(&m_A, true));
	ExpectEvents({1, 2, 3});
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2, 3});
}

TEST_F(QmPopupCallbackLifetime, UnknownCallbackExceptionDoesNotLeaveTheRestOfTheBatchPending)
{
	Open(&m_A, [this] { Event(1); throw 7; });
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	EXPECT_NO_THROW(m_Ui.ClosePopupMenus(false));
	ExpectEvents({1, 2, 3});
	Open(&m_A, [this] { Event(4); });
	m_Ui.ClosePopupMenus(false);
	ExpectEvents({1, 2, 3, 4, 3});
}

TEST_F(QmPopupCallbackLifetime, GlobalNotificationExceptionIsContainedAndIsNotReplayed)
{
	Open(&m_A, [this] { ++m_Closed; });
	m_Ui.SetPopupMenuClosedCallback([this] { ++m_Global; throw std::bad_alloc(); });
	EXPECT_NO_THROW(m_Ui.ClosePopupMenu(&m_A, true));
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(m_Closed, 1);
	EXPECT_EQ(m_Global, 1);
}

TEST_F(QmPopupCallbackLifetime, GlobalCallbackCanReplaceItselfWithoutDestroyingItsCurrentCapture)
{
	auto pToken = std::make_shared<SLifetimeToken>();
	m_Weak = pToken;
	m_Ui.SetPopupMenuClosedCallback([this, pToken] {
		(void)pToken;
		m_Ui.SetPopupMenuClosedCallback([this] { Event(9); });
		m_AliveInGlobal = !m_Weak.expired();
		Event(3);
	});
	pToken.reset();
	Open(&m_A, [this] { Event(1); });
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_TRUE(m_AliveInGlobal);
	EXPECT_TRUE(m_Weak.expired());
	Open(&m_A, [this] { Event(2); });
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 3, 2, 9});
}

TEST_F(QmPopupCallbackLifetime, QueuePreparationFailureDoesNotPublishOrStealFocusAndCanRetry)
{
	auto Props = Properties();
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_OnClosed = [this] { ++m_Closed; };
	m_Ui.SetKeyboardFocus(&m_Button);
	m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
	EXPECT_NO_THROW(m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_A));
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Button);
	EXPECT_EQ(m_Closed, 0);
	m_Memory.m_FailAt = -1;
	m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	m_Ui.ClosePopupMenu(&m_A, true, false);
	EXPECT_EQ(m_Closed, 1);
}

TEST_F(QmPopupCallbackLifetime, FailedChildAdmissionLeavesTheParentIntactAndCanRetry)
{
	Open(&m_A, [this] { Event(1); });
	m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
	Open(&m_B, [this] { Event(2); });
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_A));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_B));
	EXPECT_EQ(m_EventCount, 0);
	m_Memory.m_FailAt = -1;
	Open(&m_B, [this] { Event(2); });
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_B));
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2});
}

TEST_F(QmPopupCallbackLifetime, NullCallbackOwnerAllocationDoesNotPublishAndCanRetry)
{
	auto Props = Properties();
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_OnClosed = [this] { ++m_Closed; };
	m_Ui.SetKeyboardFocus(&m_Button);
	m_Memory.m_ReturnNull = true;
	m_Memory.m_FailAt = m_Memory.m_Allocations + 2;
	EXPECT_NO_THROW(m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_A));
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Button);
	m_Memory.m_FailAt = -1;
	m_Memory.m_ReturnNull = false;
	m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	m_Ui.ClosePopupMenu(&m_A, true, false);
	EXPECT_EQ(m_Closed, 1);
}

TEST_F(QmPopupCallbackLifetime, UnknownAdmissionExceptionLeavesNoPartialPopupAndCanRetry)
{
	m_Memory.m_ThrowUnknown = true;
	m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
	EXPECT_NO_THROW(Open(&m_A, [this] { ++m_Closed; }));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_A));
	m_Memory.m_FailAt = -1;
	m_Memory.m_ThrowUnknown = false;
	Open(&m_A, [this] { ++m_Closed; });
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(m_Closed, 1);
}

TEST_F(QmPopupCallbackLifetime, CallbackCopyFailureDoesNotAdmitAPartialRecord)
{
	auto pState = std::make_shared<SCopyState>();
	auto Props = Properties();
	Props.m_OnClosed = CCopySensitiveCallback(pState);
	pState->m_FailCopy = true;
	EXPECT_NO_THROW(m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_A));
	EXPECT_EQ(pState->m_Calls, 0);
	pState->m_FailCopy = false;
	m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(pState->m_Calls, 1);
}

TEST_F(QmPopupCallbackLifetime, FailedRefreshPreservesTheOldRectangleAndCallback)
{
	Open(&m_A, [this] { Event(1); });
	auto pState = std::make_shared<SCopyState>();
	auto Props = Properties();
	Props.m_OnClosed = CCopySensitiveCallback(pState);
	pState->m_FailCopy = true;
	EXPECT_NO_THROW(m_Ui.DoPopupMenu(&m_A, 90, 90, 500, 400, this, Content, Props));
	const auto *pRect = m_Ui.GetPopupMenuRect(&m_A);
	ASSERT_NE(pRect, nullptr);
	EXPECT_FLOAT_EQ(pRect->w, 240);
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1});
	EXPECT_EQ(pState->m_Calls, 0);
}

TEST_F(QmPopupCallbackLifetime, AdmissionMovesThePreparedCallbackInsteadOfCopyingAfterPublication)
{
	auto pState = std::make_shared<SCopyState>();
	auto Props = Properties();
	Props.m_OnClosed = CCopySensitiveCallback(pState);
	const int Copies = pState->m_Copies;
	pState->m_FailAtCopy = Copies + 2;
	EXPECT_NO_THROW(m_Ui.DoPopupMenu(&m_A, 40, 40, 240, 120, this, Content, Props));
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_A));
	EXPECT_EQ(pState->m_Copies, Copies + 1);
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(pState->m_Calls, 1);
}

TEST_F(QmPopupCallbackLifetime, RefreshingAnExistingCallbackReusesItsReservedStorage)
{
	Open(&m_A, [this] { ++m_Closed; });
	const int Allocations = m_Memory.m_Allocations;
	m_Memory.m_FailAt = Allocations + 1;
	Open(&m_A, [this] { m_Closed += 10; });
	EXPECT_EQ(m_Memory.m_Allocations, Allocations);
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(m_Closed, 10);
}

TEST_F(QmPopupCallbackLifetime, ClearingACallbackReleasesItsCaptureAndDoesNotNotifyIt)
{
	auto pToken = std::make_shared<SLifetimeToken>();
	m_Weak = pToken;
	Open(&m_A, [this, pToken] { (void)pToken; ++m_Closed; });
	pToken.reset();
	ASSERT_FALSE(m_Weak.expired());
	Open(&m_A, {});
	EXPECT_TRUE(m_Weak.expired());
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(m_Closed, 0);
}

TEST_F(QmPopupCallbackLifetime, CapturesStayAliveUntilChildAndGlobalNotificationsHaveFinished)
{
	auto pToken = std::make_shared<SLifetimeToken>();
	m_Weak = pToken;
	Open(&m_A, [this, pToken] { (void)pToken; Event(1); });
	pToken.reset();
	Open(&m_B, [this] { m_AliveInChild = !m_Weak.expired(); Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { m_AliveInGlobal = !m_Weak.expired(); Event(3); });
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_TRUE(m_AliveInChild);
	EXPECT_TRUE(m_AliveInGlobal);
	EXPECT_TRUE(m_Weak.expired());
	ExpectEvents({1, 2, 3});
}

TEST_F(QmPopupCallbackLifetime, CaptureDestructorCanReenterAfterBatchRemoval)
{
	auto pToken = std::make_shared<SLifetimeToken>();
	pToken->m_OnDestroy = [this] { Open(&m_C, [this] { Event(4); }); m_Ui.ClosePopupMenu(&m_C, true); };
	Open(&m_A, [this, pToken] { (void)pToken; Event(1); });
	pToken.reset();
	Open(&m_B, [this] { Event(2); });
	m_Ui.SetPopupMenuClosedCallback([this] { Event(3); });
	m_Ui.ClosePopupMenu(&m_A, true);
	ExpectEvents({1, 2, 3, 4, 3});
	EXPECT_FALSE(m_Ui.IsPopupOpen());
}

TEST_F(QmPopupCallbackLifetime, FailedGlobalCallbackReplacementKeepsThePreviousCallback)
{
	m_Ui.SetPopupMenuClosedCallback([this] { ++m_Global; });
	m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
	EXPECT_THROW(m_Ui.SetPopupMenuClosedCallback([this] { m_Global += 10; }), std::bad_alloc);
	m_Memory.m_FailAt = -1;
	Open(&m_A, [this] { ++m_Closed; });
	m_Ui.ClosePopupMenu(&m_A, true);
	EXPECT_EQ(m_Global, 1);
	EXPECT_EQ(m_Closed, 1);
}

TEST_F(QmPopupCallbackLifetime, ReplacedCaptureCanCloseTheFullyUpdatedPopupFromItsDestructor)
{
	auto pToken = std::make_shared<SLifetimeToken>();
	pToken->m_OnDestroy = [this] { m_Ui.ClosePopupMenu(&m_A, true); };
	Open(&m_A, [this, pToken] { (void)pToken; Event(1); });
	pToken.reset();
	Open(&m_A, [this] { Event(2); });
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_A));
	ExpectEvents({2});
}
