#ifndef BASE_CRASH_OUTPUT_WINDOWS_H
#define BASE_CRASH_OUTPUT_WINDOWS_H

#include "detect.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <cstddef>

namespace crash_output
{
	// 可选外部I/O操作须存活到writer关闭完成，不得抛C++异常。
	struct SWindowsFileOperations
	{
		decltype(&FlushFileBuffers) m_pfnFlush = FlushFileBuffers;
		decltype(&CloseHandle) m_pfnClose = CloseHandle;
	};

	class CWindowsFile
	{
	public:
		enum class EOperation
		{
			WRITE,
			FLUSH,
			CLOSE,
			PUBLISH
		};

	private:
		HANDLE m_Handle;
		const SWindowsFileOperations *m_pOperations;
		DWORD m_Error = ERROR_SUCCESS;
		EOperation m_Operation = EOperation::WRITE;

	public:
		// 关闭责任交给CWriter；本适配器不在析构中重试原生关闭。
		explicit CWindowsFile(HANDLE Handle, const SWindowsFileOperations *pOperations = nullptr) noexcept :
			m_Handle(Handle), m_pOperations(pOperations) {}
		CWindowsFile(const CWindowsFile &) = delete;
		CWindowsFile &operator=(const CWindowsFile &) = delete;
		void Fail(DWORD Error, EOperation Operation = EOperation::WRITE) noexcept
		{
			if(m_Error == ERROR_SUCCESS)
			{
				m_Error = Error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : Error;
				m_Operation = Operation;
			}
		}
		DWORD Error() const noexcept { return m_Error; }
		EOperation FailedOperation() const noexcept { return m_Operation; }
		bool Write(const void *pData, std::size_t Size, std::size_t &Written) noexcept
		{
			const DWORD Requested = Size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(Size);
			DWORD NativeWritten = 0;
			if(!WriteFile(m_Handle, pData, Requested, &NativeWritten, nullptr))
			{
				Fail(GetLastError());
				return false;
			}
			if(NativeWritten == 0 || NativeWritten > Requested)
			{
				Fail(ERROR_WRITE_FAULT);
				return false;
			}
			Written = NativeWritten;
			return true;
		}
		bool Flush() noexcept
		{
			if(m_pOperations ? m_pOperations->m_pfnFlush(m_Handle) : FlushFileBuffers(m_Handle))
				return true;
			Fail(GetLastError(), EOperation::FLUSH);
			return false;
		}
		bool Close() noexcept
		{
			const HANDLE Handle = m_Handle;
			m_Handle = INVALID_HANDLE_VALUE;
			if(m_pOperations ? m_pOperations->m_pfnClose(Handle) : CloseHandle(Handle))
				return true;
			Fail(GetLastError(), EOperation::CLOSE);
			return false;
		}
	};

	inline bool PublishWindowsOutput(const wchar_t *pCandidate, const wchar_t *pTarget) noexcept
	{
		return MoveFileExW(pCandidate, pTarget, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}
}

#endif
#endif
