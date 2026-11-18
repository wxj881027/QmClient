#include "input_tsf.h"

#include "input_tsf_sink.h"

#include <base/system.h>
#include <base/windows.h>

#include <engine/shared/qm_ime_policy.h>

#if defined(CONF_FAMILY_WINDOWS) && defined(_MSC_VER)
#include <limits>
#include <memory>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace
{
	const GUID s_ClsidTfThreadMgr = {0x529A9E6B, 0x6587, 0x4F23, {0xAB, 0x9E, 0x9C, 0x7D, 0x68, 0x3E, 0x3C, 0x50}};
}

std::optional<SQmImeCandidateList> QmReadTsfCandidates(ITfCandidateListUIElement *pElement)
{
	UINT Count = 0;
	if(FAILED(pElement->GetCount(&Count)) || Count > std::numeric_limits<int>::max())
		return std::nullopt;
	SQmImeCandidateList List;
	List.m_TotalCount = (int)Count;
	if(Count == 0)
		return List;

	UINT Selection = 0;
	const HRESULT SelectionResult = pElement->GetSelection(&Selection);
	if(FAILED(SelectionResult) || (SelectionResult == S_OK && Selection >= Count))
		return std::nullopt;

	UINT PageCount = 0;
	// S_FALSE 也可能返回完整页表；以返回数量和边界判断数据是否完整。
	if(FAILED(pElement->GetPageIndex(nullptr, 0, &PageCount)) || PageCount == 0 || PageCount > Count + 1)
		return std::nullopt;
	std::vector<UINT> vPages(PageCount);
	UINT ReturnedPages = 0;
	if(FAILED(pElement->GetPageIndex(vPages.data(), PageCount, &ReturnedPages)) || ReturnedPages == 0 || ReturnedPages > PageCount)
		return std::nullopt;
	vPages.resize(ReturnedPages);
	UINT CurrentPage = 0;
	if(FAILED(pElement->GetCurrentPage(&CurrentPage)) || vPages.front() != 0)
		return std::nullopt;
	for(size_t i = 0; i < vPages.size(); ++i)
	{
		if(vPages[i] > Count || (i > 0 && vPages[i] <= vPages[i - 1]))
			return std::nullopt;
	}
	// 有些 TIP 在整页末尾附带 Count 哨兵，不把它当成实际候选页。
	if(vPages.back() == Count)
		vPages.pop_back();
	if(CurrentPage >= vPages.size())
		return std::nullopt;
	const UINT Start = vPages[CurrentPage];
	const UINT End = CurrentPage + 1 < vPages.size() ? vPages[CurrentPage + 1] : Count;
	List.m_PageStart = (int)Start;
	List.m_PageSize = (int)(End - Start);
	List.m_PageIndex = (int)CurrentPage;
	List.m_PageCount = (int)vPages.size();
	// S_FALSE 表示没有选中项，此时 Selection 的值没有意义。
	if(SelectionResult == S_OK && Selection >= Start && Selection < End)
		List.m_SelectedIndex = (int)(Selection - Start);
	List.m_vCandidates.reserve(End - Start);
	for(UINT i = Start; i < End; ++i)
	{
		BSTR pText = nullptr;
		const HRESULT Result = pElement->GetString(i, &pText);
		const std::unique_ptr<wchar_t, decltype(&SysFreeString)> Text(pText, SysFreeString);
		if(FAILED(Result) || pText == nullptr)
			return std::nullopt;
		auto Candidate = windows_wide_to_utf8_bounded(pText, pText + SysStringLen(pText) + 1);
		if(!Candidate.has_value())
			return std::nullopt;
		List.m_vCandidates.push_back(std::move(*Candidate));
	}
	return List;
}

CQmTsfUiElementSink::CQmTsfUiElementSink(ITfUIElementMgr *pManager) :
	m_pManager(pManager)
{
}

void CQmTsfUiElementSink::Enable(CQmImeCandidateState &Candidates)
{
	if(m_pManager == nullptr || m_pCandidates == &Candidates)
		return;
	Disable();
	m_pCandidates = &Candidates;
}

void CQmTsfUiElementSink::Disable()
{
	// Show 可能同步重入回调，先让旧会话的所有读取失效。
	++m_Session;
	if(auto *pCandidates = std::exchange(m_pCandidates, nullptr))
		pCandidates->ResetTsfCandidates();
	auto Elements = std::move(m_Elements);
	m_Elements.clear();
	for(auto &Entry : Elements)
	{
		if(Entry.second.m_DisplayMode == EDisplayMode::CUSTOM)
		{
			const HRESULT Result = Entry.second.m_pCandidates->Show(TRUE);
			if(FAILED(Result))
				dbg_msg("input/ime", "event=tsf_candidate_restore_failed element=%lu hr=0x%08lx", (unsigned long)Entry.first, (unsigned long)Result);
		}
	}
}

void CQmTsfUiElementSink::Shutdown()
{
	// 注销失败时 TSF 仍可能持有 sink，断开反向引用以免形成 COM 引用环。
	const auto pManager = std::move(m_pManager);
	Disable();
}

HRESULT STDMETHODCALLTYPE CQmTsfUiElementSink::QueryInterface(REFIID Riid, void **ppObject)
{
	if(ppObject == nullptr)
		return E_POINTER;
	*ppObject = nullptr;
	if(!IsEqualIID(Riid, __uuidof(IUnknown)) && !IsEqualIID(Riid, __uuidof(ITfUIElementSink)))
		return E_NOINTERFACE;
	*ppObject = static_cast<ITfUIElementSink *>(this);
	AddRef();
	return S_OK;
}

ULONG STDMETHODCALLTYPE CQmTsfUiElementSink::AddRef()
{
	return InterlockedIncrement(&m_RefCount);
}

ULONG STDMETHODCALLTYPE CQmTsfUiElementSink::Release()
{
	const ULONG RefCount = InterlockedDecrement(&m_RefCount);
	if(RefCount == 0)
		delete this;
	return RefCount;
}

HRESULT STDMETHODCALLTYPE CQmTsfUiElementSink::BeginUIElement(DWORD ElementId, BOOL *pShow)
{
	if(pShow == nullptr)
		return E_POINTER;
	*pShow = TRUE;
	if(m_pCandidates == nullptr)
		return S_OK;
	const uint64_t Session = m_Session;
	ComPtr<ITfUIElement> pElement;
	ComPtr<ITfCandidateListUIElement> pCandidates;
	if(FAILED(m_pManager->GetUIElement(ElementId, &pElement)) || pElement == nullptr ||
		FAILED(pElement.As(&pCandidates)) || m_pCandidates == nullptr || m_Session != Session)
		return S_OK;
	// 未知 UI 保留系统显示；已识别候选的内容到 Update 时才保证就绪。
	const auto [It, Inserted] = m_Elements.emplace(ElementId, SElement{pCandidates, ++m_NextRevision});
	if(Inserted)
		m_pCandidates->BeginTsfCandidates(ElementId);
	*pShow = It->second.m_DisplayMode == EDisplayMode::CUSTOM ? FALSE : TRUE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CQmTsfUiElementSink::UpdateUIElement(DWORD ElementId)
{
	auto It = m_Elements.find(ElementId);
	if(m_pCandidates == nullptr || It == m_Elements.end() || It->second.m_DisplayMode != EDisplayMode::CUSTOM)
		return S_OK;
	const uint64_t Session = m_Session;
	const uint64_t Revision = ++m_NextRevision;
	It->second.m_Revision = Revision;
	const auto pElement = It->second.m_pCandidates;
	auto Candidates = QmReadTsfCandidates(pElement.Get());
	// COM 可关闭并重开输入框、复用元素 ID，或嵌套发布更新的快照。
	It = m_Elements.find(ElementId);
	if(m_pCandidates == nullptr || m_Session != Session || It == m_Elements.end() || It->second.m_Revision != Revision)
		return S_OK;
	if(Candidates.has_value())
	{
		m_pCandidates->UpdateTsfCandidates(ElementId, std::move(Candidates));
		return S_OK;
	}
	// 恢复窗口时避免重入更新再次显示自绘候选，失败后允许下一次更新重试。
	It->second.m_DisplayMode = EDisplayMode::RESTORING_SYSTEM;
	m_pCandidates->UpdateTsfCandidates(ElementId, std::nullopt);
	const HRESULT Result = pElement->Show(TRUE);
	It = m_Elements.find(ElementId);
	if(m_pCandidates != nullptr && m_Session == Session && It != m_Elements.end() && It->second.m_Revision == Revision)
		It->second.m_DisplayMode = SUCCEEDED(Result) ? EDisplayMode::SYSTEM : EDisplayMode::CUSTOM;
	dbg_msg("input/ime", "event=tsf_candidate_fallback element=%lu hr=0x%08lx", (unsigned long)ElementId, (unsigned long)Result);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CQmTsfUiElementSink::EndUIElement(DWORD ElementId)
{
	// 延后释放 COM 引用，避免析构重入时把同 ID 的新候选一并清除。
	auto Element = m_Elements.extract(ElementId);
	if(m_pCandidates != nullptr && !Element.empty())
		m_pCandidates->EndTsfCandidates(ElementId);
	return S_OK;
}

CQmTsfUiLess::~CQmTsfUiLess()
{
	Deactivate();
}

bool CQmTsfUiLess::Activate(CQmImeCandidateState &Candidates)
{
	if(IsActive())
		return true;
	ComPtr<ITfThreadMgrEx> pThreadMgr;
	HRESULT Result = CoCreateInstance(s_ClsidTfThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pThreadMgr));
	if(FAILED(Result))
	{
		dbg_msg("input/ime", "event=tsf_uiless_failed step=create hr=0x%08lx", (unsigned long)Result);
		return false;
	}
	TfClientId ClientId = TF_CLIENTID_NULL;
	Result = pThreadMgr->ActivateEx(&ClientId, TF_TMAE_UIELEMENTENABLEDONLY);
	if(FAILED(Result))
	{
		dbg_msg("input/ime", "event=tsf_uiless_failed step=activate hr=0x%08lx", (unsigned long)Result);
		return false;
	}
	ComPtr<ITfUIElementMgr> pManager;
	Result = pThreadMgr.As(&pManager);
	if(FAILED(Result))
	{
		pThreadMgr->Deactivate();
		dbg_msg("input/ime", "event=tsf_uiless_failed step=ui_manager hr=0x%08lx", (unsigned long)Result);
		return false;
	}
	m_pSink = new CQmTsfUiElementSink(pManager.Get());
	m_pThreadMgr = pThreadMgr.Detach();
	m_pCandidates = &Candidates;
	dbg_msg("input/ime", "event=tsf_uiless_enabled");
	return true;
}

void CQmTsfUiLess::SetTextInputActive(bool Active)
{
	if(!IsActive())
		return;
	if(!Active)
		m_pSink->Disable();
	else
		m_pSink->Enable(*m_pCandidates);
	if(Active == (m_SinkCookie != TF_INVALID_COOKIE))
		return;
	ComPtr<ITfSource> pSource;
	HRESULT Result = m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pSource));
	if(SUCCEEDED(Result))
	{
		if(Active)
			Result = pSource->AdviseSink(__uuidof(ITfUIElementSink), m_pSink, &m_SinkCookie);
		else
		{
			Result = pSource->UnadviseSink(m_SinkCookie);
			if(SUCCEEDED(Result))
				m_SinkCookie = TF_INVALID_COOKIE;
		}
	}
	if(FAILED(Result))
	{
		m_pSink->Disable();
		dbg_msg("input/ime", "event=tsf_uiless_failed step=%s hr=0x%08lx", Active ? "advise" : "unadvise", (unsigned long)Result);
	}
}

void CQmTsfUiLess::Deactivate()
{
	if(!IsActive())
		return;
	SetTextInputActive(false);
	m_pSink->Shutdown();
	m_pThreadMgr->Deactivate();
	m_pSink->Release();
	m_pThreadMgr->Release();
	m_pThreadMgr = nullptr;
	m_pSink = nullptr;
	m_pCandidates = nullptr;
	m_SinkCookie = TF_INVALID_COOKIE;
}

#elif defined(CONF_FAMILY_WINDOWS)

CQmTsfUiLess::~CQmTsfUiLess() = default;
bool CQmTsfUiLess::Activate(CQmImeCandidateState &) { return false; }
void CQmTsfUiLess::SetTextInputActive(bool) {}
void CQmTsfUiLess::Deactivate() {}

#endif
