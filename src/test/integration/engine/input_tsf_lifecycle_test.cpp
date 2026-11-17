#include <engine/client/input_tsf_sink.h>
#include <engine/shared/qm_ime_policy.h>

#include <gtest/gtest.h>
#include <test/support/qm_tsf_test.h>

#include <functional>
#include <map>
#include <utility>

namespace
{
	using Microsoft::WRL::ComPtr;
	using qm_tsf_test::CFakeCandidateList;
	using qm_tsf_test::CreateCandidates;

	// 管理器仅提供 COM 元素和重入时机，候选所有权与回退由生产 sink 处理。
	class CFakeUiElementManager final : public ITfUIElementMgr
	{
		ULONG m_RefCount = 1;

	public:
		std::map<DWORD, ComPtr<ITfUIElement>> m_Elements;
		std::function<void()> m_OnGetUiElement;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppObject) override
		{
			if(ppObject == nullptr)
				return E_POINTER;
			*ppObject = nullptr;
			if(!IsEqualIID(Riid, __uuidof(IUnknown)) && !IsEqualIID(Riid, __uuidof(ITfUIElementMgr)))
				return E_NOINTERFACE;
			*ppObject = static_cast<ITfUIElementMgr *>(this);
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG RefCount = --m_RefCount;
			if(RefCount == 0)
				delete this;
			return RefCount;
		}
		HRESULT STDMETHODCALLTYPE BeginUIElement(ITfUIElement *, BOOL *, DWORD *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE EndUIElement(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetUIElement(DWORD ElementId, ITfUIElement **ppElement) override
		{
			*ppElement = nullptr;
			const auto It = m_Elements.find(ElementId);
			if(It == m_Elements.end())
				return E_INVALIDARG;
			const HRESULT Result = It->second.CopyTo(ppElement);
			if(auto Callback = std::exchange(m_OnGetUiElement, {}))
				Callback();
			return Result;
		}
		HRESULT STDMETHODCALLTYPE EnumUIElements(IEnumTfUIElements **ppElements) override
		{
			*ppElements = nullptr;
			return E_NOTIMPL;
		}
	};

	class CQmTsfLifecycleTest : public testing::Test
	{
	protected:
		static constexpr DWORD ELEMENT_ID = 17;
		CQmImeCandidateState m_State;
		ComPtr<CFakeUiElementManager> m_pManager;
		ComPtr<CFakeCandidateList> m_pElement;
		ComPtr<CQmTsfUiElementSink> m_pSink;

		void SetUp() override
		{
			m_pManager.Attach(new CFakeUiElementManager());
			m_pElement = CreateCandidates();
			m_pManager->m_Elements.emplace(ELEMENT_ID, m_pElement);
			m_pSink.Attach(new CQmTsfUiElementSink(m_pManager.Get()));
			m_pSink->Enable(m_State);
		}

		void TearDown() override
		{
			m_pSink->Disable();
		}

		void BeginCandidates()
		{
			BOOL Show = TRUE;
			ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
			ASSERT_EQ(Show, FALSE);
			// 模拟 TIP 接收 Begin 返回值后决定其窗口是否可见。
			m_pElement->m_Shown = Show;
		}
	};
}

TEST_F(CQmTsfLifecycleTest, CandidateUpdatesPopulateCustomUiUntilElementEnds)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_FALSE(m_State.AcceptsSdlCandidates());

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, FailedSystemFallbackAllowsNextSuccessfulCandidateRead)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_ShowResult = E_FAIL;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_EQ(m_pElement->m_ShowCalls, 1);
	ASSERT_FALSE(m_pElement->m_Shown);
	ASSERT_TRUE(m_State.Candidates().m_vCandidates.empty());

	m_pElement->m_FailedStringIndex = -1;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_FALSE(m_pElement->m_Shown);
}

TEST_F(CQmTsfLifecycleTest, FailedSystemFallbackRetriesWhenCandidateReadStillFails)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_ShowResult = E_FAIL;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_FALSE(m_pElement->m_Shown);

	m_pElement->m_ShowResult = S_OK;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_pElement->m_ShowCalls, 2);
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, SuccessfulSystemFallbackSuppressesNativeAndSdlCandidatesUntilEnd)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_TRUE(m_pElement->m_Shown);

	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"旧候选"};
	NativeCandidates.m_PageSize = NativeCandidates.m_TotalCount = 1;
	m_State.UpdateFromNative(NativeCandidates, true);
	const char *apSdlCandidates[] = {"1 旧候选"};
	m_State.UpdateFromSdl(apSdlCandidates, 1, 0, true);
	m_pElement->m_FailedStringIndex = -1;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_TRUE(m_pElement->m_Shown);

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	m_State.UpdateFromNative(NativeCandidates, true);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, NativeCandidates.m_vCandidates);
}

TEST_F(CQmTsfLifecycleTest, BeginInterruptedBySessionRestartLeavesOldElementNative)
{
	m_pManager->m_OnGetUiElement = [this] {
		m_pSink->Disable();
		m_pSink->Enable(m_State);
	};
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	EXPECT_EQ(Show, TRUE);

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_pElement->m_StringCalls, 0);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());

	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
}

TEST_F(CQmTsfLifecycleTest, OldReadCannotOverwriteRestartedElementWithSameIdAndPointer)
{
	BeginCandidates();
	m_pElement->m_vStrings = {L"旧"};
	m_pElement->m_OnGetString = [this] {
		EXPECT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
		BeginCandidates();
		m_pElement->m_vStrings = {L"新"};
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_FALSE(m_pElement->m_Shown);
}

TEST_F(CQmTsfLifecycleTest, NestedUpdateKeepsTheNewerCandidateSnapshot)
{
	BeginCandidates();
	m_pElement->m_vStrings = {L"旧"};
	m_pElement->m_OnGetString = [this] {
		m_pElement->m_vStrings = {L"新"};
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);
}

TEST_F(CQmTsfLifecycleTest, DisableClearsCandidatesAndRejectsLateCallbacks)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_FALSE(m_State.Candidates().m_vCandidates.empty());
	m_pSink->Disable();
	ASSERT_TRUE(m_State.Candidates().m_vCandidates.empty());
	ASSERT_TRUE(m_pElement->m_Shown);
	const int PreviousReadCount = m_pElement->m_StringCalls;

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);

	EXPECT_EQ(Show, TRUE);
	EXPECT_EQ(m_pElement->m_StringCalls, PreviousReadCount);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, RepeatedEnablePreservesCurrentCandidateOwnership)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pSink->Enable(m_State);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_FALSE(m_State.AcceptsSdlCandidates());
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);

	m_pElement->m_vStrings = {L"新"};
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
}

TEST_F(CQmTsfLifecycleTest, ShutdownPreventsLateCallbacksFromReactivatingTheSink)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pElement->m_OnShow = [this] {
		m_pSink->Enable(m_State);
	};
	m_pSink->Shutdown();
	const int PreviousReadCount = m_pElement->m_StringCalls;
	m_pSink->Enable(m_State);
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(Show, TRUE);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_EQ(m_pElement->m_StringCalls, PreviousReadCount);
}

TEST_F(CQmTsfLifecycleTest, UnknownUiElementKeepsItsNativeWindow)
{
	m_pElement->m_IsCandidateList = false;
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	EXPECT_EQ(Show, TRUE);

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_pElement->m_StringCalls, 0);
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, SystemFallbackIgnoresSynchronousUpdateFromShow)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_OnShow = [this] {
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, ElementReleaseCannotClearNewCandidatesWithTheSameId)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pElement->m_OnFinalRelease = [this] {
		m_pElement = CreateCandidates();
		m_pElement->m_vStrings = {L"新"};
		m_pManager->m_Elements.emplace(ELEMENT_ID, m_pElement);
		BeginCandidates();
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};
	// 只让 sink 持有旧元素，使 End 释放其 COM 引用时触发新一轮通知。
	m_pManager->m_Elements.erase(ELEMENT_ID);
	m_pElement.Reset();

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);

	ASSERT_NE(m_pElement.Get(), nullptr);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_FALSE(m_pElement->m_Shown);
}
