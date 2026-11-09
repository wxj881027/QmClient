#ifndef ENGINE_CLIENT_INPUT_WINDOWS_IME_H
#define ENGINE_CLIENT_INPUT_WINDOWS_IME_H

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
// commctrl.h 依赖 windows.h 中的窗口类型。
#include <commctrl.h>
#include <imm.h>

// 对齐 SDL2 的 SYSWMEVENT 接入位置：窗口过程也能收到 SendMessage 同步通知。
// 只观察候选通知，所有消息继续交给原窗口过程；不接管 SDL 的文字提交。
class CWindowsImeHook
{
public:
	using FNotify = void (*)(void *, HWND, WPARAM, LPARAM);

	CWindowsImeHook() = default;
	CWindowsImeHook(const CWindowsImeHook &) = delete;
	CWindowsImeHook &operator=(const CWindowsImeHook &) = delete;
	~CWindowsImeHook() { Detach(); }

	bool Attach(HWND Window, FNotify pfnNotify, void *pUser)
	{
		if(Window == m_Window && Window != nullptr)
			return true;
		Detach();
		if(Window == nullptr || pfnNotify == nullptr)
			return false;
		m_Window = Window;
		m_pfnNotify = pfnNotify;
		m_pUser = pUser;
		if(SetWindowSubclass(Window, WindowProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
			return true;
		m_Window = nullptr;
		m_pfnNotify = nullptr;
		m_pUser = nullptr;
		return false;
	}

	void Detach()
	{
		if(m_Window != nullptr)
			RemoveWindowSubclass(m_Window, WindowProc, reinterpret_cast<UINT_PTR>(this));
		m_Window = nullptr;
		m_pfnNotify = nullptr;
		m_pUser = nullptr;
	}

private:
	static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR, DWORD_PTR RefData)
	{
		auto *pHook = reinterpret_cast<CWindowsImeHook *>(RefData);
		if(Message == WM_IME_NOTIFY && (WParam == IMN_OPENCANDIDATE || WParam == IMN_CHANGECANDIDATE || WParam == IMN_CLOSECANDIDATE))
			pHook->m_pfnNotify(pHook->m_pUser, Window, WParam, LParam);
		else if(Message == WM_NCDESTROY)
			pHook->Detach();
		return DefSubclassProc(Window, Message, WParam, LParam);
	}

	HWND m_Window = nullptr;
	FNotify m_pfnNotify = nullptr;
	void *m_pUser = nullptr;
};
#endif

#endif
