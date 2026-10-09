#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BOUNDED_FILE_READER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BOUNDED_FILE_READER_H

#include <base/system.h>

#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

// 接管文件句柄；在分配前检查长度，失败时不发布残缺数据。
inline bool QmReadFileBounded(IOHANDLE File, size_t MaxBytes, std::string &Output)
{
	Output.clear();
	if(File == nullptr)
		return false;
	struct SCloseFile
	{
		IOHANDLE m_File;
		~SCloseFile() { io_close(m_File); }
	} Close{File};
	const int64_t Length = io_length(File);
	if(Length <= 0 || (uint64_t)Length > MaxBytes || (uint64_t)Length > std::numeric_limits<unsigned>::max())
		return false;
	try
	{
		std::string Candidate((size_t)Length, '\0');
		if(io_read(File, Candidate.data(), (unsigned)Candidate.size()) != Candidate.size())
			return false;
		Output = std::move(Candidate);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}

#endif
