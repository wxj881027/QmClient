#ifndef ENGINE_CLIENT_QM_CRASH_REPORT_FILE_H
#define ENGINE_CLIENT_QM_CRASH_REPORT_FILE_H

#include <base/borrowed_allocator.h>
#include <base/system.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace QmCrashReportFile
{
	constexpr unsigned MAX_REPORT_BYTES = 2 * 1024 * 1024;
	using CText = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;

	// Input借用到返回；Read返回实际字节数，0为EOF，Failed报告读取错误。
	// 可选内存域须存活到返回文本销毁；失败不交付部分内容，不校验UTF-8。
	template<typename TInput>
	std::optional<CText> ReadText(TInput &Input, unsigned Limit = MAX_REPORT_BYTES, IAllocationResource *pResource = nullptr) noexcept
	{
		if(Limit > MAX_REPORT_BYTES)
			return std::nullopt;
		try
		{
			CText Text{CBorrowedAllocator<char>(pResource)};
			char aBuffer[4096];
			for(;;)
			{
				const size_t Remaining = Limit - Text.size();
				// 多读一个探测字节，精确上限仍须确认EOF；文件增长不放宽额度。
				const unsigned Requested = static_cast<unsigned>(std::min(sizeof(aBuffer), Remaining + 1));
				const unsigned Read = Input.Read(aBuffer, Requested);
				if(Read > Requested || Input.Failed() || Read > Remaining)
					return std::nullopt;
				if(Read == 0)
					return std::optional<CText>(std::move(Text));
				if(std::memchr(aBuffer, '\0', Read))
					return std::nullopt;
				Text.append(aBuffer, Read);
			}
		}
		catch(...)
		{
			return std::nullopt;
		}
	}

	namespace detail
	{
		class CFileInput
		{
			IOHANDLE m_File;

		public:
			explicit CFileInput(IOHANDLE File) :
				m_File(File) {}
			unsigned Read(char *pBuffer, unsigned Size) { return io_read(m_File, pBuffer, Size); }
			bool Failed() const { return io_error(m_File) != 0; }
		};
	}

	// 仅消费可取长度的报告文件；长度是快速拒绝条件，实际读取仍独立受限。
	inline std::optional<CText> ReadFile(const char *pPath, IAllocationResource *pResource = nullptr) noexcept
	{
		if(!pPath || !pPath[0])
			return std::nullopt;
		try
		{
			bool NotFound = false;
			std::unique_ptr<void, decltype(&io_close)> File(io_open_read_checked(pPath, &NotFound), &io_close);
			if(!File)
				return std::nullopt;
			const int64_t Length = io_length(File.get());
			if(Length < 0 || Length > MAX_REPORT_BYTES)
				return std::nullopt;
			detail::CFileInput Input(File.get());
			auto Text = ReadText(Input, MAX_REPORT_BYTES, pResource);
			const bool Closed = io_close(File.release()) == 0;
			if(!Closed)
				return std::nullopt;
			return Text;
		}
		catch(...)
		{
			return std::nullopt;
		}
	}
}

#endif
