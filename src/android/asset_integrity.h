#ifndef ANDROID_ASSET_INTEGRITY_H
#define ANDROID_ASSET_INTEGRITY_H

#include <base/borrowed_allocator.h>
#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/shared/linereader.h>

#include <cstring>
#include <string_view>
#include <vector>

namespace android_assets
{
	constexpr unsigned MAX_INDEX_BUFFER_BYTES = 4 * 1024 * 1024 + 1;
	constexpr size_t MAX_INDEX_RECORDS = 32768;

	struct CIntegrityFileLine
	{
		char m_aFilename[IO_MAX_PATH_LENGTH] = {};
		SHA256_DIGEST m_Sha256 = {};
	};
	using CIntegrityLines = std::vector<CIntegrityFileLine, CBorrowedAllocator<CIntegrityFileLine>>;

	// 索引生成器只枚举data子树；这里检查词法边界，不解析文件系统符号链接。
	inline bool IsAssetPath(std::string_view Path) noexcept
	{
		if(Path.size() >= IO_MAX_PATH_LENGTH || Path.substr(0, 5) != "data/" || Path.find('\0') != std::string_view::npos || Path.find_first_of("\r\n") != std::string_view::npos)
			return false;
		for(size_t Start = 5;;)
		{
			const auto End = Path.find('/', Start);
			const auto Part = Path.substr(Start, End == std::string_view::npos ? End : End - Start);
			if(Part.empty() || Part == "." || Part == "..")
				return false;
			if(End == std::string_view::npos)
				return true;
			Start = End + 1;
		}
	}

	inline bool ParseIntegrityLine(std::string_view Text, bool Header, CIntegrityFileLine &Out) noexcept
	{
		CIntegrityFileLine Candidate;
		std::string_view Hash = Text;
		if(!Header)
		{
			const auto Separator = Text.find_last_of(' ');
			if(Separator == std::string_view::npos)
				return false;
			const auto Path = Text.substr(0, Separator);
			if(!IsAssetPath(Path))
				return false;
			std::memcpy(Candidate.m_aFilename, Path.data(), Path.size());
			if(!str_utf8_check(Candidate.m_aFilename))
				return false;
			Hash = Text.substr(Separator + 1);
		}
		if(Hash.size() != SHA256_MAXSTRSIZE - 1)
			return false;
		char aHash[SHA256_MAXSTRSIZE];
		std::memcpy(aHash, Hash.data(), Hash.size());
		aHash[Hash.size()] = '\0';
		if(sha256_from_str(&Candidate.m_Sha256, aHash) != 0)
			return false;
		Out = Candidate;
		return true;
	}

	// 可选内存域仅用于条目容器，须活到返回容器销毁；读取/解码使用有界C缓冲。
	inline CIntegrityLines ReadIntegrityFile(const char *pFilename, IAllocationResource *pResource = nullptr) noexcept
	{
		try
		{
			CLineReader Reader;
			if(!Reader.OpenFileWithBudget(io_open(pFilename, IOFLAG_READ), MAX_INDEX_BUFFER_BYTES, true))
				return {};
			CIntegrityLines vLines{CBorrowedAllocator<CIntegrityFileLine>(pResource)};
			while(const char *pLine = Reader.Get())
			{
				CIntegrityFileLine Line;
				if(vLines.size() >= MAX_INDEX_RECORDS || !ParseIntegrityLine(pLine, vLines.empty(), Line))
				{
					log_error("android", "Invalid or oversized integrity index '%s' at line %zu", pFilename, vLines.size() + 1);
					return {};
				}
				vLines.push_back(Line);
			}
			return vLines;
		}
		catch(...)
		{
			return {};
		}
	}
}

#endif
