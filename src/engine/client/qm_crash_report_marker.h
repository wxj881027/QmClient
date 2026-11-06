#ifndef ENGINE_CLIENT_QM_CRASH_REPORT_MARKER_H
#define ENGINE_CLIENT_QM_CRASH_REPORT_MARKER_H

#include "qm_crash_report_file.h"

#include <base/detect.h>
#include <base/str.h>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace QmCrashReportMarker
{
	constexpr unsigned MAX_TIMESTAMP_BYTES = 64;

	inline std::optional<int64_t> ParseStartedAt(std::string_view Text) noexcept
	{
		const auto IsSpace = [](char Character) { return str_isspace(Character) || Character == '\v' || Character == '\f'; };
		while(!Text.empty() && IsSpace(Text.front()))
			Text.remove_prefix(1);
		while(!Text.empty() && IsSpace(Text.back()))
			Text.remove_suffix(1);
		if(!Text.empty() && Text.front() == '+')
			Text.remove_prefix(1);
		if(Text.empty())
			return std::nullopt;
		int64_t Timestamp = 0;
		const auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Timestamp, 10);
		if(Parsed.ec != std::errc{} || Parsed.ptr != Text.data() + Text.size() || Timestamp <= 0)
			return std::nullopt;
		return Timestamp;
	}

	// 标记沿用正十进制时间戳格式，读取额度不依赖磁盘声明长度。
	inline std::optional<int64_t> ReadStartedAt(const char *pPath, IAllocationResource *pResource = nullptr) noexcept
	{
		const auto Text = QmCrashReportFile::ReadFile(pPath, pResource, MAX_TIMESTAMP_BYTES);
		return Text ? ParseStartedAt(std::string_view(Text->data(), Text->size())) : std::nullopt;
	}
}

#if defined(CONF_FAMILY_WINDOWS)
#include <base/crash_output.h>
#include <base/crash_output_windows.h>
#include <base/system.h>

#include <atomic>
#include <iterator>

namespace QmCrashReportMarker
{
	inline std::atomic<uint64_t> gs_Sequence{0};

	// 健康报告进程/启动线程使用；候选独占创建，失败保留正式标记和候选证据。
	inline bool Save(const char *pPath, int64_t Timestamp, const crash_output::SWindowsFileOperations *pOperations = nullptr) noexcept
	{
		if(!pPath || !pPath[0] || !str_utf8_check(pPath))
			return false;
		wchar_t aTarget[IO_MAX_PATH_LENGTH + 128];
		if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pPath, -1, aTarget, std::size(aTarget)) <= 0)
			return false;
		for(int Attempt = 0; Attempt < 16; ++Attempt)
		{
			char aSuffix[64], aCandidate[IO_MAX_PATH_LENGTH + 128];
			str_format(aSuffix, sizeof(aSuffix), ".%d.%llx.tmp", pid(), static_cast<unsigned long long>(gs_Sequence.fetch_add(1, std::memory_order_relaxed)));
			if(!crash_output::AppendPathSuffix(pPath, aSuffix, aCandidate, sizeof(aCandidate)))
				return false;
			wchar_t aWideCandidate[IO_MAX_PATH_LENGTH + 128];
			if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, aCandidate, -1, aWideCandidate, std::size(aWideCandidate)) <= 0)
				return false;
			const HANDLE Handle = CreateFileW(aWideCandidate, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(Handle == INVALID_HANDLE_VALUE)
			{
				const DWORD Error = GetLastError();
				if(Error == ERROR_FILE_EXISTS || Error == ERROR_ALREADY_EXISTS)
					continue;
				return false;
			}
			crash_output::CWindowsFile File(Handle, pOperations);
			crash_output::CWriter Output(File);
			char aTimestamp[64];
			str_format(aTimestamp, sizeof(aTimestamp), "%lld\n", static_cast<long long>(Timestamp));
			Output.WriteText(aTimestamp);
			return Output.Finish() && crash_output::PublishWindowsOutput(aWideCandidate, aTarget);
		}
		return false;
	}
}
#endif
#endif
