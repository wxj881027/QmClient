#ifndef TEST_SUPPORT_QM_TSF_TEST_H
#define TEST_SUPPORT_QM_TSF_TEST_H

#include <windows.h>
// Windows 接口声明必须先于 TSF 和 WRL 头文件。
#include <msctf.h>
#include <wrl/client.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace qm_tsf_test
{
	using Microsoft::WRL::ComPtr;

	// 仅模拟 TIP 的 COM 输出，分页切片和选中项换算由生产函数完成。
	class CFakeCandidateList final : public ITfCandidateListUIElement
	{
		ULONG m_RefCount = 1;

	public:
		std::vector<std::wstring> m_vStrings{L"你", L"好", L"吗"};
		std::vector<UINT> m_vPageStarts{0};
		UINT m_CurrentPage = 0;
		UINT m_Selection = 0;
		HRESULT m_SelectionResult = S_OK;
		HRESULT m_PageResult = S_OK;
		UINT m_ExtraReturnedPages = 0;
		int m_FailedStringIndex = -1;
		int m_MetadataCalls = 0;
		int m_StringCalls = 0;
		HRESULT m_ShowResult = S_OK;
		BOOL m_Shown = TRUE;
		int m_ShowCalls = 0;
		bool m_IsCandidateList = true;
		std::function<void()> m_OnGetString;
		std::function<void()> m_OnShow;
		std::function<void()> m_OnFinalRelease;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppObject) override
		{
			if(ppObject == nullptr)
				return E_POINTER;
			*ppObject = nullptr;
			if(!IsEqualIID(Riid, __uuidof(IUnknown)) && !IsEqualIID(Riid, __uuidof(ITfUIElement)) && !IsEqualIID(Riid, __uuidof(ITfCandidateListUIElement)))
				return E_NOINTERFACE;
			if(IsEqualIID(Riid, __uuidof(ITfCandidateListUIElement)) && !m_IsCandidateList)
				return E_NOINTERFACE;
			*ppObject = static_cast<ITfCandidateListUIElement *>(this);
			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG RefCount = --m_RefCount;
			if(RefCount == 0)
			{
				if(auto Callback = std::exchange(m_OnFinalRelease, {}))
					Callback();
				delete this;
			}
			return RefCount;
		}

		HRESULT STDMETHODCALLTYPE GetDescription(BSTR *pDescription) override
		{
			*pDescription = nullptr;
			return E_NOTIMPL;
		}
		HRESULT STDMETHODCALLTYPE GetGUID(GUID *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE Show(BOOL Show) override
		{
			++m_ShowCalls;
			if(SUCCEEDED(m_ShowResult))
				m_Shown = Show;
			if(auto Callback = std::exchange(m_OnShow, {}))
				Callback();
			return m_ShowResult;
		}
		HRESULT STDMETHODCALLTYPE IsShown(BOOL *pShown) override
		{
			*pShown = m_Shown;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **ppDocument) override
		{
			*ppDocument = nullptr;
			return E_NOTIMPL;
		}
		HRESULT STDMETHODCALLTYPE GetCount(UINT *pCount) override
		{
			*pCount = (UINT)m_vStrings.size();
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetSelection(UINT *pSelection) override
		{
			++m_MetadataCalls;
			*pSelection = m_Selection;
			return m_SelectionResult;
		}
		HRESULT STDMETHODCALLTYPE GetString(UINT Index, BSTR *pString) override
		{
			++m_StringCalls;
			*pString = nullptr;
			if(Index >= m_vStrings.size() || (int)Index == m_FailedStringIndex)
				return E_FAIL;
			*pString = SysAllocStringLen(m_vStrings[Index].data(), (UINT)m_vStrings[Index].size());
			// 先保存本次输出，再触发同步通知，以检验旧读取覆盖新快照的情况。
			if(auto Callback = std::exchange(m_OnGetString, {}))
				Callback();
			return *pString != nullptr ? S_OK : E_OUTOFMEMORY;
		}
		HRESULT STDMETHODCALLTYPE GetPageIndex(UINT *pIndices, UINT Size, UINT *pPageCount) override
		{
			++m_MetadataCalls;
			*pPageCount = (UINT)m_vPageStarts.size();
			if(pIndices != nullptr)
			{
				std::copy_n(m_vPageStarts.begin(), std::min((size_t)Size, m_vPageStarts.size()), pIndices);
				*pPageCount += m_ExtraReturnedPages;
			}
			return m_PageResult;
		}
		HRESULT STDMETHODCALLTYPE SetPageIndex(UINT *, UINT) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT *pPage) override
		{
			++m_MetadataCalls;
			*pPage = m_CurrentPage;
			return S_OK;
		}
	};

	inline ComPtr<CFakeCandidateList> CreateCandidates()
	{
		ComPtr<CFakeCandidateList> pCandidates;
		pCandidates.Attach(new CFakeCandidateList());
		return pCandidates;
	}
}

#endif
