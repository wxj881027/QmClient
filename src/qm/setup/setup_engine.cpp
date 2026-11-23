#include "setup_engine.h"

#include "setup_options.h"

#include <objbase.h>

#include <algorithm>
#include <array>
#include <exception>
#include <memory>
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

		void CleanupStaging(std::wstring &Executable, std::wstring &Directory)
		{
			if(!Executable.empty())
			{
				if(!DeleteFileW(Executable.c_str()))
				{
					const DWORD Error = GetLastError();
					if(Error != ERROR_FILE_NOT_FOUND && Error != ERROR_PATH_NOT_FOUND)
						return;
				}
				Executable.clear();
			}
			// 日志和其他文件保留，空目录才会被移除。
			if(!Directory.empty() && RemoveDirectoryW(Directory.c_str()))
				Directory.clear();
		}

		struct SStagingCleanup
		{
			std::wstring &m_Executable;
			std::wstring &m_Directory;
			const bool &m_RetainExecutable;

			~SStagingCleanup()
			{
				if(!m_RetainExecutable)
					CleanupStaging(m_Executable, m_Directory);
			}
		};
	}

	std::wstring WindowsError(DWORD Code)
	{
		wchar_t *pMessage = nullptr;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Code, 0, reinterpret_cast<wchar_t *>(&pMessage), 0, nullptr);
		const std::unique_ptr<wchar_t, decltype(&LocalFree)> Message(pMessage, LocalFree);
		const std::wstring Result = Message ? Message.get() : L"Windows 操作失败";
		return Result + L"（" + std::to_wstring(Code) + L"）";
	}

	CSetupEngine::~CSetupEngine()
	{
		Join();
		// 退出时再尝试清理曾被占用的暂存文件。
		if(!m_RetainExecutable)
			CleanupStaging(m_Executable, m_Directory);
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
		std::wstring Directory = std::wstring(aTemporary.data()) + L"QmClient-Setup-" + aId;
		if(!CreateDirectoryW(Directory.c_str(), nullptr))
		{
			m_Error = L"无法创建安装临时目录：" + WindowsError(GetLastError());
			return false;
		}
		m_Directory = std::move(Directory);
		std::wstring Executable = m_Directory + L"\\engine.exe";
		CHandle File(CreateFileW(Executable.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
		if(File.Get() == INVALID_HANDLE_VALUE)
		{
			m_Error = L"无法写入安装文件：" + WindowsError(GetLastError());
			return false;
		}
		m_Executable = std::move(Executable);
		for(DWORD Offset = 0; Offset < Size;)
		{
			const DWORD Count = std::min<DWORD>(1024 * 1024, Size - Offset);
			DWORD Written = 0;
			const BOOL Success = WriteFile(File.Get(), pData + Offset, Count, &Written, nullptr);
			if(!Success || Written != Count)
			{
				const DWORD Error = Success ? ERROR_WRITE_FAULT : GetLastError();
				m_Error = L"无法准备安装文件：" + WindowsError(Error);
				return false;
			}
			Offset += Written;
		}
		return true;
	}

	DWORD CSetupEngine::Run(std::vector<std::wstring> vArguments, HWND Window)
	try
	{
		const SStagingCleanup StagingCleanup{m_Executable, m_Directory, m_RetainExecutable};
		bool HasLog = false;
		std::wstring Log;
		for(const auto &Argument : vArguments)
		{
			// Inno 将双引号当作分隔符，参数内容里的双引号会改变后续参数的边界。
			if(Argument.find(L'"') != std::wstring::npos)
			{
				m_Error = L"安装参数包含不支持的双引号。";
				return 1;
			}
			const auto Name = Lower(Argument);
			if(Name.rfind(L"/log=", 0) == 0)
			{
				HasLog = true;
				Log = Argument.substr(5);
			}
			else if(Name == L"/log")
			{
				HasLog = true;
				Log.clear();
			}
		}
		if(!Extract())
			return 1;
		if(!HasLog)
		{
			Log = m_Directory + L"\\setup.log";
			vArguments.push_back(L"/LOG=" + Log);
		}
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
		if(WaitForSingleObject(ProcessHandle.Get(), INFINITE) != WAIT_OBJECT_0)
		{
			m_RetainExecutable = true;
			m_Error = L"无法确认安装程序是否退出。";
		}
		else if(!GetExitCodeProcess(ProcessHandle.Get(), &ExitCode))
		{
			ExitCode = 1;
			m_Error = L"无法读取安装程序的退出状态：" + WindowsError(GetLastError());
		}
		else if(ExitCode != 0)
			m_Error = ExitCode == 2 ? L"安装已取消。" : L"安装未完成。请检查目录权限、可用空间，以及客户端是否仍在运行。\n错误代码：" + std::to_wstring(ExitCode);
		if(Window && ExitCode != 0 && !Log.empty())
		{
			const DWORD Attributes = GetFileAttributesW(Log.c_str());
			if(Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY))
				m_Log = std::move(Log);
		}
		return ExitCode;
	}
	catch(const std::exception &)
	{
		const wchar_t *pMessage = L"准备安装时发生错误，请重新运行安装程序。";
		try
		{
			m_Error = pMessage;
		}
		catch(const std::exception &)
		{
			// 资源耗尽时，错误文本分配失败也要返回失败结果。
			OutputDebugStringW(pMessage);
		}
		return 1;
	}

	void CSetupEngine::Start(std::vector<std::wstring> vArguments, HWND Window)
	{
		m_Worker = std::thread([this, vArguments = std::move(vArguments), Window]() mutable {
			const DWORD Result = Run(std::move(vArguments), Window);
			// 消息队列达到配额时走跨线程异步通知，界面收到后仍须 Join。
			if(!PostMessageW(Window, WM_ENGINE_DONE, Result, 0) && GetLastError() == ERROR_NOT_ENOUGH_QUOTA)
				SendNotifyMessageW(Window, WM_ENGINE_DONE, Result, 0);
		});
	}

	void CSetupEngine::Join()
	{
		if(m_Worker.joinable())
			m_Worker.join();
	}
}
