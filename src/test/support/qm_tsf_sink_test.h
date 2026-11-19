#ifndef TEST_SUPPORT_QM_TSF_SINK_TEST_H
#define TEST_SUPPORT_QM_TSF_SINK_TEST_H

#include <engine/client/input_tsf_sink.h>
#include <engine/shared/qm_ime_policy.h>

#include <gtest/gtest.h>
#include <test/support/qm_tsf_test.h>

#include <functional>
#include <map>
#include <utility>

namespace qm_tsf_test
{
	using Microsoft::WRL::ComPtr;

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

#endif
