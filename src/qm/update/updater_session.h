#ifndef QM_UPDATE_UPDATER_SESSION_H
#define QM_UPDATE_UPDATER_SESSION_H

#include "updater_arguments.h"

#if defined(_WIN32)
#include <windows.h>

#include <cwchar>
#else
#include <unistd.h>

#include <cerrno>
#endif

namespace QmUpdate
{
#if defined(_WIN32)
	class CInstallLock
	{
	public:
		enum class EResult
		{
			ACQUIRED,
			BUSY,
			FAILED,
		};

	private:
		HANDLE m_Handle = nullptr;
		EResult m_Result = EResult::FAILED;

	public:
		explicit CInstallLock(const std::filesystem::path &Directory)
		{
			const HANDLE DirectoryHandle = CreateFileW(Directory.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if(DirectoryHandle == INVALID_HANDLE_VALUE)
				return;
			BY_HANDLE_FILE_INFORMATION Info{};
			const BOOL ReadInfo = GetFileInformationByHandle(DirectoryHandle, &Info);
			CloseHandle(DirectoryHandle);
			if(!ReadInfo || !(Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				return;

			// 按目录身份互斥，路径别名和不同登录会话也使用同一把锁。
			wchar_t aName[64];
			if(std::swprintf(aName, std::size(aName), L"Global\\QmClientUpdate-%08lX-%08lX-%08lX",
				   Info.dwVolumeSerialNumber, Info.nFileIndexHigh, Info.nFileIndexLow) <= 0)
				return;
			const HANDLE Mutex = CreateMutexExW(nullptr, aName, 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
			if(!Mutex)
				return;
			const DWORD Wait = WaitForSingleObject(Mutex, 0);
			if(Wait == WAIT_OBJECT_0 || Wait == WAIT_ABANDONED)
			{
				m_Handle = Mutex;
				m_Result = EResult::ACQUIRED;
			}
			else
			{
				CloseHandle(Mutex);
				if(Wait == WAIT_TIMEOUT)
					m_Result = EResult::BUSY;
			}
		}

		CInstallLock(const CInstallLock &) = delete;
		CInstallLock &operator=(const CInstallLock &) = delete;
		~CInstallLock() { Release(); }
		EResult Result() const { return m_Result; }
		void Release()
		{
			if(!m_Handle)
				return;
			ReleaseMutex(m_Handle);
			CloseHandle(m_Handle);
			m_Handle = nullptr;
		}
	};
#endif

	// 只删除下载文件。单个附件清理失败后继续处理其余附件。
	inline bool CleanupDownloadedFiles(const SArguments &Arguments)
	{
		bool Success = true;
		for(const auto *pPath : {&Arguments.m_Package, &Arguments.m_PackageSignature, &Arguments.m_Manifest, &Arguments.m_ManifestSignature})
		{
			if(pPath->empty())
				continue;
#if defined(_WIN32)
			if(!DeleteFileW(pPath->c_str()))
			{
				const DWORD Error = GetLastError();
				Success &= Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND;
			}
#else
			const std::filesystem::path Path(*pPath);
			if(unlink(Path.c_str()) != 0 && errno != ENOENT)
				Success = false;
#endif
		}
		return Success;
	}

	enum class ESetupResult
	{
		SUCCEEDED,
		FAILED,
		STILL_RUNNING,
	};

	// 只有持有安装器的会话负责清理；等待异常时保留运行中安装器的文件。
	template<typename TVerify, typename TRun, typename TCleanup>
	ESetupResult RunSetupSession(TVerify Verify, TRun Run, TCleanup Cleanup)
	{
		const ESetupResult Result = Verify() ? Run() : ESetupResult::FAILED;
		if(Result != ESetupResult::STILL_RUNNING)
			Cleanup();
		return Result;
	}
}

#endif
