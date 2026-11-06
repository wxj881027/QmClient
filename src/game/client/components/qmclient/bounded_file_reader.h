#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BOUNDED_FILE_READER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BOUNDED_FILE_READER_H

#include <base/system.h>

#include <engine/storage.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

// 只接收普通文件的完整限长结果，直接持有存储接口交出的 malloc 缓冲。
class CQmCheckedFileData
{
	void *m_pData = nullptr;
	unsigned m_Size = 0;

	void Reset() noexcept
	{
		free(std::exchange(m_pData, nullptr));
		m_Size = 0;
	}

public:
	CQmCheckedFileData() = default;
	CQmCheckedFileData(const CQmCheckedFileData &) = delete;
	CQmCheckedFileData &operator=(const CQmCheckedFileData &) = delete;
	CQmCheckedFileData(CQmCheckedFileData &&Other) noexcept :
		m_pData(std::exchange(Other.m_pData, nullptr)),
		m_Size(std::exchange(Other.m_Size, 0)) {}
	CQmCheckedFileData &operator=(CQmCheckedFileData &&Other) noexcept
	{
		if(this != &Other)
		{
			Reset();
			m_pData = std::exchange(Other.m_pData, nullptr);
			m_Size = std::exchange(Other.m_Size, 0);
		}
		return *this;
	}
	~CQmCheckedFileData() { Reset(); }

	bool Read(IStorage &Storage, const char *pFilename, int Type, size_t MaxBytes)
	{
		Reset();
		CQmCheckedFileData Candidate;
		const unsigned Limit = MaxBytes > std::numeric_limits<unsigned>::max() ? std::numeric_limits<unsigned>::max() : static_cast<unsigned>(MaxBytes);
		if(Storage.ReadFileCheckedBounded(pFilename, Type, &Candidate.m_pData, &Candidate.m_Size, Limit) != IStorage::EReadFileResult::SUCCESS ||
			Candidate.m_pData == nullptr || Candidate.m_Size == 0 || Candidate.m_Size > MaxBytes)
			return false;
		*this = std::move(Candidate);
		return true;
	}

	const void *Data() const { return m_pData; }
	unsigned Size() const { return m_Size; }
};

namespace QmBoundedFileDetail
{
	// 接管文件句柄；在分配前检查长度，失败时不发布残缺数据。
	template<typename TBuffer>
	bool Read(IOHANDLE File, size_t MaxBytes, TBuffer &Output)
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
			TBuffer Candidate(Output.get_allocator());
			Candidate.resize((size_t)Length);
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
}

inline bool QmReadFileBounded(IOHANDLE File, size_t MaxBytes, std::string &Output)
{
	return QmBoundedFileDetail::Read(File, MaxBytes, Output);
}

template<typename TAllocator>
bool QmReadFileBounded(IOHANDLE File, size_t MaxBytes, std::vector<uint8_t, TAllocator> &Output)
{
	return QmBoundedFileDetail::Read(File, MaxBytes, Output);
}

#endif
