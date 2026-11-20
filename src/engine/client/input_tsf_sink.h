#ifndef ENGINE_CLIENT_INPUT_TSF_SINK_H
#define ENGINE_CLIENT_INPUT_TSF_SINK_H

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS) && defined(_MSC_VER)
#include <engine/shared/qm_ime_policy.h>

#include <windows.h>
// Windows 接口声明必须先于 TSF 和 WRL 头文件。
#include <msctf.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>

class CQmTsfUiElementSink final : public ITfUIElementSink
{
	friend class CQmTsfUiLess;

	enum class EDisplayMode
	{
		CUSTOM,
		RESTORING_SYSTEM,
		SYSTEM,
	};
	struct SElement
	{
		Microsoft::WRL::ComPtr<ITfCandidateListUIElement> m_pCandidates;
		uint64_t m_Revision;
		EDisplayMode m_DisplayMode = EDisplayMode::CUSTOM;
	};
	LONG m_RefCount = 1;
	Microsoft::WRL::ComPtr<ITfUIElementMgr> m_pManager;
	CQmImeCandidateState *m_pCandidates = nullptr;
	using TElements = std::map<DWORD, SElement>;
	std::unique_ptr<TElements> m_pElements;
	uint64_t m_Session = 0;
	uint64_t m_NextRevision = 0;

	SElement *FindElement(DWORD ElementId);

public:
	explicit CQmTsfUiElementSink(ITfUIElementMgr *pManager) noexcept;
	void Enable(CQmImeCandidateState &Candidates);
	void Disable();
	void Shutdown();

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD ElementId, BOOL *pShow) override;
	HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD ElementId) override;
	HRESULT STDMETHODCALLTYPE EndUIElement(DWORD ElementId) override;
};
#endif

#endif
