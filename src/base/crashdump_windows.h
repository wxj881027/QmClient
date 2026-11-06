#ifndef BASE_CRASHDUMP_WINDOWS_H
#define BASE_CRASHDUMP_WINDOWS_H

#include "detect.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <array>
#include <cstddef>

namespace crashdump_windows
{
	// 仅供一次诊断串行使用；不借用故障内存，不分配堆或在原始指针上解引用。
	class CExceptionSnapshot
	{
	public:
		static constexpr std::size_t MAX_RECORDS = 16;

	private:
		EXCEPTION_POINTERS m_Pointers{};
		CONTEXT m_Context{};
		std::array<EXCEPTION_RECORD, MAX_RECORDS> m_aRecords{};
		std::array<const EXCEPTION_RECORD *, MAX_RECORDS> m_apSources{};
		std::size_t m_RecordCount = 0;

		template<typename T>
		static bool ReadObject(const T *pSource, T &Output) noexcept
		{
			SIZE_T BytesRead = 0;
			return pSource && ReadProcessMemory(GetCurrentProcess(), pSource, &Output, sizeof(Output), &BytesRead) && BytesRead == sizeof(Output);
		}

	public:
		CExceptionSnapshot() = default;
		// 内部指针指向本对象，复制或移动会使它们指向错误的所有者。
		CExceptionSnapshot(const CExceptionSnapshot &) = delete;
		CExceptionSnapshot &operator=(const CExceptionSnapshot &) = delete;

		void Capture(const EXCEPTION_POINTERS *pSource) noexcept
		{
			m_Pointers = {};
			m_RecordCount = 0;
			EXCEPTION_POINTERS Source{};
			if(!ReadObject(pSource, Source))
				return;
			if(ReadObject(Source.ContextRecord, m_Context))
			{
				// 固定CONTEXT不含可变长XSTATE区域，不能让DbgHelp沿扩展标志越界。
#if defined(CONTEXT_XSTATE)
				m_Context.ContextFlags &= static_cast<DWORD>(~(CONTEXT_XSTATE & ~CONTEXT_ALL));
#endif
				m_Pointers.ContextRecord = &m_Context;
			}
			const EXCEPTION_RECORD *pRecord = Source.ExceptionRecord;
			while(pRecord && m_RecordCount < MAX_RECORDS)
			{
				for(std::size_t Index = 0; Index < m_RecordCount; ++Index)
					if(m_apSources[Index] == pRecord)
						return;
				auto &Record = m_aRecords[m_RecordCount];
				if(!ReadObject(pRecord, Record))
					break;
				m_apSources[m_RecordCount] = pRecord;
				pRecord = Record.ExceptionRecord;
				Record.ExceptionRecord = nullptr;
				if(Record.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS)
					Record.NumberParameters = EXCEPTION_MAXIMUM_PARAMETERS;
				if(m_RecordCount == 0)
					m_Pointers.ExceptionRecord = &Record;
				else
					m_aRecords[m_RecordCount - 1].ExceptionRecord = &Record;
				++m_RecordCount;
			}
		}

		const EXCEPTION_RECORD *Record() const noexcept { return m_Pointers.ExceptionRecord; }
		const CONTEXT *Context() const noexcept { return m_Pointers.ContextRecord; }
		// DbgHelp只接收完整的本地记录/上下文对；否则省略异常流，仍可生成普通转储。
		EXCEPTION_POINTERS *PointersForDump() noexcept
		{
			return Record() && Context() ? &m_Pointers : nullptr;
		}
	};
}

#endif
#endif
