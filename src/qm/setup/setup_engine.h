#ifndef QM_SETUP_SETUP_ENGINE_H
#define QM_SETUP_SETUP_ENGINE_H

#include <windows.h>

#include <string>
#include <thread>
#include <vector>

namespace QmSetup
{
	// 与 native_bridge.iss 共用；消息仅携带进度和窗口句柄，不跨进程传指针。
	constexpr UINT WM_ENGINE_READY = WM_APP + 31;
	constexpr UINT WM_ENGINE_PROGRESS = WM_APP + 32;
	constexpr UINT WM_ENGINE_DONE = WM_APP + 33;

	class CSetupEngine
	{
		std::thread m_Worker;
		std::wstring m_Directory;
		std::wstring m_Executable;
		std::wstring m_Log;
		std::wstring m_Error;
		bool m_RetainExecutable = false;

		bool Extract();

	public:
		~CSetupEngine();
		DWORD Run(std::vector<std::wstring> vArguments, HWND Window = nullptr);
		void Start(std::vector<std::wstring> vArguments, HWND Window);
		void Join();
		const std::wstring &Error() const { return m_Error; }
		const std::wstring &Log() const { return m_Log; }
	};

	std::wstring WindowsError(DWORD Code);
}

#endif // QM_SETUP_SETUP_ENGINE_H
