#include "input_tsf.h"

#include <base/system.h>

#if defined(CONF_FAMILY_WINDOWS) && defined(_MSC_VER)
#include <windows.h>
// windows.h must be included before msctf.h, but clang-format requires includes to be sorted alphabetically, hence this comment.
#include <msctf.h>

namespace
{
	// 本地定义 CLSID，不依赖 uuid.lib 是否导出该符号。
	const GUID s_ClsidTfThreadMgr = {0x529A9E6B, 0x6587, 0x4F23, {0xAB, 0x9E, 0x9C, 0x7D, 0x68, 0x3E, 0x3C, 0x50}};

	class CQmTsfUiElementSink final : public ITfUIElementSink
	{
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppObject) override
		{
			if(ppObject == nullptr)
				return E_POINTER;
			if(IsEqualIID(Riid, __uuidof(IUnknown)) || IsEqualIID(Riid, __uuidof(ITfUIElementSink)))
			{
				*ppObject = static_cast<ITfUIElementSink *>(this);
				AddRef();
				return S_OK;
			}
			*ppObject = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return InterlockedIncrement(&m_RefCount);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG RefCount = InterlockedDecrement(&m_RefCount);
			if(RefCount == 0)
				delete this;
			return RefCount;
		}

		// 候选窗、读音窗等界面一律由客户端绘制，输入法不再显示自己的窗口。
		HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD ElementId, BOOL *pShow) override
		{
			if(pShow == nullptr)
				return E_INVALIDARG;
			*pShow = FALSE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD ElementId) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE EndUIElement(DWORD ElementId) override
		{
			return S_OK;
		}

	private:
		LONG m_RefCount = 1;
	};
}

CQmTsfUiLess::~CQmTsfUiLess()
{
	Deactivate();
}

bool CQmTsfUiLess::Activate()
{
	if(IsActive())
		return true;

	ITfThreadMgrEx *pThreadMgr = nullptr;
	HRESULT Result = CoCreateInstance(s_ClsidTfThreadMgr, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITfThreadMgrEx), reinterpret_cast<void **>(&pThreadMgr));
	if(FAILED(Result))
	{
		dbg_msg("input/ime", "event=tsf_uiless_failed step=create hr=0x%08lx", (unsigned long)Result);
		return false;
	}

	// 线程管理器按线程共享，这里只增加一次激活计数，Deactivate 时对称减少。
	TfClientId ClientId = TF_CLIENTID_NULL;
	Result = pThreadMgr->ActivateEx(&ClientId, TF_TMAE_UIELEMENTENABLEDONLY);
	if(FAILED(Result))
	{
		dbg_msg("input/ime", "event=tsf_uiless_failed step=activate hr=0x%08lx", (unsigned long)Result);
		pThreadMgr->Release();
		return false;
	}

	ITfSource *pSource = nullptr;
	ITfUIElementSink *pSink = new CQmTsfUiElementSink();
	DWORD Cookie = TF_INVALID_COOKIE;
	Result = pThreadMgr->QueryInterface(__uuidof(ITfSource), reinterpret_cast<void **>(&pSource));
	if(SUCCEEDED(Result))
	{
		Result = pSource->AdviseSink(__uuidof(ITfUIElementSink), pSink, &Cookie);
		pSource->Release();
	}
	if(FAILED(Result))
	{
		dbg_msg("input/ime", "event=tsf_uiless_failed step=advise hr=0x%08lx", (unsigned long)Result);
		pSink->Release();
		pThreadMgr->Deactivate();
		pThreadMgr->Release();
		return false;
	}

	m_pThreadMgr = pThreadMgr;
	m_pSink = pSink;
	m_SinkCookie = Cookie;
	dbg_msg("input/ime", "event=tsf_uiless_enabled");
	return true;
}

void CQmTsfUiLess::Deactivate()
{
	if(!IsActive())
		return;

	ITfSource *pSource = nullptr;
	if(SUCCEEDED(m_pThreadMgr->QueryInterface(__uuidof(ITfSource), reinterpret_cast<void **>(&pSource))))
	{
		pSource->UnadviseSink(m_SinkCookie);
		pSource->Release();
	}
	m_pThreadMgr->Deactivate();
	m_pThreadMgr->Release();
	m_pSink->Release();
	m_pThreadMgr = nullptr;
	m_pSink = nullptr;
	m_SinkCookie = 0;
}

#elif defined(CONF_FAMILY_WINDOWS)

CQmTsfUiLess::~CQmTsfUiLess() = default;

bool CQmTsfUiLess::Activate()
{
	return false;
}

void CQmTsfUiLess::Deactivate()
{
}

#endif
