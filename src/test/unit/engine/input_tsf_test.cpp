#include <engine/client/input_tsf.h>
#include <engine/shared/qm_ime_policy.h>

#include <windows.h>

#include <gtest/gtest.h>
// Windows 接口声明必须先于 TSF 和 WRL 头文件。
#include <msctf.h>
#include <wrl/client.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace
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

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppObject) override
		{
			if(ppObject == nullptr)
				return E_POINTER;
			*ppObject = nullptr;
			if(!IsEqualIID(Riid, __uuidof(IUnknown)) && !IsEqualIID(Riid, __uuidof(ITfUIElement)) && !IsEqualIID(Riid, __uuidof(ITfCandidateListUIElement)))
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
				delete this;
			return RefCount;
		}

		HRESULT STDMETHODCALLTYPE GetDescription(BSTR *pDescription) override
		{
			*pDescription = nullptr;
			return E_NOTIMPL;
		}
		HRESULT STDMETHODCALLTYPE GetGUID(GUID *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE Show(BOOL) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE IsShown(BOOL *) override { return E_NOTIMPL; }
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

	ComPtr<CFakeCandidateList> CreateCandidates()
	{
		ComPtr<CFakeCandidateList> pCandidates;
		pCandidates.Attach(new CFakeCandidateList());
		return pCandidates;
	}
}

TEST(QmTsfCandidates, UnequalLastPageAcceptsCompleteSFalsePageTable)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings = {L"零", L"一", L"二", L"三", L"四", L"五", L"六"};
	pElement->m_vPageStarts = {0, 2, 5};
	pElement->m_CurrentPage = 2;
	pElement->m_Selection = 6;
	// 微软示例在页表缓冲区恰好够用时仍返回 S_FALSE。
	pElement->m_PageResult = S_FALSE;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"五", "六"}));
	EXPECT_EQ(Result->m_SelectedIndex, 1);
	EXPECT_EQ(Result->m_PageStart, 5);
	EXPECT_EQ(Result->m_PageSize, 2);
	EXPECT_EQ(Result->m_PageIndex, 2);
	EXPECT_EQ(Result->m_PageCount, 3);
	EXPECT_EQ(Result->m_TotalCount, 7);
	EXPECT_EQ(pElement->m_StringCalls, 2);
}

TEST(QmTsfCandidates, NoSelectionIgnoresUndefinedOutputIndex)
{
	const auto pElement = CreateCandidates();
	pElement->m_SelectionResult = S_FALSE;
	pElement->m_Selection = std::numeric_limits<UINT>::max();

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_SelectedIndex, -1);
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
}

TEST(QmTsfCandidates, CountSentinelDoesNotCreateAnEmptyLastPage)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings = {L"甲", L"乙", L"丙", L"丁"};
	pElement->m_vPageStarts = {0, 2, 4};
	pElement->m_CurrentPage = 1;
	pElement->m_Selection = 3;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"丙", "丁"}));
	EXPECT_EQ(Result->m_PageStart, 2);
	EXPECT_EQ(Result->m_PageSize, 2);
	EXPECT_EQ(Result->m_PageIndex, 1);
	EXPECT_EQ(Result->m_PageCount, 2);
	EXPECT_EQ(Result->m_SelectedIndex, 1);
}

TEST(QmTsfCandidates, EmptyListDoesNotReadUnavailablePageMetadata)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings.clear();
	pElement->m_SelectionResult = E_FAIL;
	pElement->m_PageResult = E_FAIL;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(Result->m_vCandidates.empty());
	EXPECT_EQ(Result->m_TotalCount, 0);
	EXPECT_EQ(Result->m_SelectedIndex, -1);
	EXPECT_EQ(pElement->m_MetadataCalls, 0);
	EXPECT_EQ(pElement->m_StringCalls, 0);
}

TEST(QmTsfCandidates, FailedStringDiscardsAlreadyReadCandidates)
{
	const auto pElement = CreateCandidates();
	pElement->m_FailedStringIndex = 1;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	EXPECT_FALSE(Result.has_value());
	EXPECT_EQ(pElement->m_StringCalls, 2);
}

TEST(QmTsfCandidates, InvalidPageStartsRejectTheSnapshot)
{
	const std::vector<std::vector<UINT>> vvInvalidPages{{1}, {0, 0}, {0, 4}};
	for(size_t i = 0; i < vvInvalidPages.size(); ++i)
	{
		SCOPED_TRACE(i);
		const auto pElement = CreateCandidates();
		pElement->m_vPageStarts = vvInvalidPages[i];

		EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
		EXPECT_EQ(pElement->m_StringCalls, 0);
	}
}

TEST(QmTsfCandidates, PageCountGrowthRejectsIncompletePageTable)
{
	const auto pElement = CreateCandidates();
	pElement->m_ExtraReturnedPages = 1;
	pElement->m_PageResult = S_FALSE;

	EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
	EXPECT_EQ(pElement->m_StringCalls, 0);
}

TEST(QmTsfCandidates, CurrentPageOutsidePageTableRejectsTheSnapshot)
{
	const auto pElement = CreateCandidates();
	pElement->m_CurrentPage = 1;

	EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
	EXPECT_EQ(pElement->m_StringCalls, 0);
}
