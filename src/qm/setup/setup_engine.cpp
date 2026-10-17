#include "setup_engine.h"

#include "setup_options.h"

#include <objbase.h>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace QmSetup
{
	namespace
	{
		class CHandle
		{
			HANDLE m_Value;

		public:
			explicit CHandle(HANDLE Value) :
				m_Value(Value) {}
			~CHandle()
			{
				if(m_Value && m_Value != INVALID_HANDLE_VALUE)
					CloseHandle(m_Value);
			}
			CHandle(const CHandle &) = delete;
			CHandle &operator=(const CHandle &) = delete;
			HANDLE Get() const { return m_Value; }
		};
	}

	std::wstring WindowsError(DWORD Code)
	{
		wchar_t *pMessage = nullptr;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Code, 0, reinterpret_cast<wchar_t *>(&pMessage), 0, nullptr);
		std::wstring Result = pMessage ? pMessage : L"Windows 操作失败";
		LocalFree(pMessage);
		return Result + L"（" + std::to_wstring(Code) + L"）";
	}

	CSetupEngine::~CSetupEngine()
	{
		Join();
		// 只清理本次创建的内核；日志保留供失败诊断，不递归清理临时目录。
		if(!m_RetainExecutable && !m_Executable.empty())
			DeleteFileW(m_Executable.c_str());
		if(!m_Directory.empty())
			RemoveDirectoryW(m_Directory.c_str());
	}

	bool CSetupEngine::Extract()
	{
		const HRSRC Resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
		const DWORD Size = Resource ? SizeofResource(nullptr, Resource) : 0;
		const HGLOBAL Loaded = Resource ? LoadResource(nullptr, Resource) : nullptr;
		const auto *pData = Loaded ? static_cast<const unsigned char *>(LockResource(Loaded)) : nullptr;
		if(!pData || Size < 2 || pData[0] != 'M' || pData[1] != 'Z')
		{
			m_Error = L"安装包不完整，请重新下载安装包。";
			return false;
		}
		std::array<wchar_t, 32768> aTemporary{};
		const DWORD Length = GetTempPathW(static_cast<DWORD>(aTemporary.size()), aTemporary.data());
		GUID Id{};
		wchar_t aId[40]{};
		if(Length == 0 || Length >= aTemporary.size() || FAILED(CoCreateGuid(&Id)) || !StringFromGUID2(Id, aId, 40))
		{
			m_Error = L"无法准备安装临时目录。";
			return false;
		}
		const std::wstring Directory = std::wstring(aTemporary.data()) + L"QmClient-Setup-" + aId;
		if(!CreateDirectoryW(Directory.c_str(), nullptr))
		{
			m_Error = L"无法创建安装临时目录：" + WindowsError(GetLastError());
			return false;
		}
		m_Directory = Directory;
		const std::wstring Executable = m_Directory + L"\\engine.exe";
		m_Log = m_Directory + L"\\setup.log";
		CHandle File(CreateFileW(Executable.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
		if(File.Get() == INVALID_HANDLE_VALUE)
		{
			m_Error = L"无法写入安装文件：" + WindowsError(GetLastError());
			return false;
		}
		m_Executable = Executable;
		for(DWORD Offset = 0; Offset < Size;)
		{
			const DWORD Count = std::min<DWORD>(1024 * 1024, Size - Offset);
			DWORD Written = 0;
			if(!WriteFile(File.Get(), pData + Offset, Count, &Written, nullptr) || Written != Count)
			{
				m_Error = L"无法准备安装文件：" + WindowsError(GetLastError());
				return false;
			}
			Offset += Written;
		}
		return true;
	}

	DWORD CSetupEngine::Run(std::vector<std::wstring> vArguments, HWND Window)
	{
		if(!Extract())
			return 1;
		bool HasLog = false;
		for(const auto &Argument : vArguments)
		{
			if(Lower(Argument).rfind(L"/log=", 0) == 0)
			{
				HasLog = true;
				m_Log = Argument.substr(5);
			}
			else if(Lower(Argument) == L"/log")
				HasLog = true;
		}
		if(!HasLog)
			vArguments.push_back(L"/LOG=" + m_Log);
		if(Window)
			vArguments.push_back(L"/QMNATIVEHWND=" + std::to_wstring(reinterpret_cast<UINT_PTR>(Window)));
		std::wstring Command = QuoteArgument(m_Executable) + L" " + CommandLine(vArguments);
		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		PROCESS_INFORMATION Process{};
		// 保留调用者工作目录，使原有 /LOG、/LOADINF 等相对路径含义不变。
		if(!CreateProcessW(m_Executable.c_str(), Command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process))
		{
			m_Error = L"无法启动安装任务：" + WindowsError(GetLastError());
			return 1;
		}
		CHandle ProcessHandle(Process.hProcess);
		CHandle ThreadHandle(Process.hThread);
		DWORD ExitCode = 1;
		if(WaitForSingleObject(ProcessHandle.Get(), INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(ProcessHandle.Get(), &ExitCode))
		{
			m_RetainExecutable = true;
			m_Error = L"无法确认安装程序是否退出，请查看安装日志。";
			return 1;
		}
		if(ExitCode != 0)
			m_Error = ExitCode == 2 ? L"安装已取消。" : L"安装未完成。请检查目录权限、可用空间，以及客户端是否仍在运行。\n错误代码：" + std::to_wstring(ExitCode);
		return ExitCode;
	}

	void CSetupEngine::Start(std::vector<std::wstring> vArguments, HWND Window)
	{
		m_Worker = std::thread([this, vArguments = std::move(vArguments), Window]() mutable {
			DWORD Result = 1;
			try
			{
				Result = Run(std::move(vArguments), Window);
			}
			catch(const std::exception &)
			{
				m_Error = L"准备安装时发生错误，请重新运行安装程序。";
			}
			PostMessageW(Window, WM_ENGINE_DONE, Result, 0);
		});
	}

	void CSetupEngine::Join()
	{
		if(m_Worker.joinable())
			m_Worker.join();
	}
}
