#ifndef TEST_SUPPORT_DATAFILE_READER_OPERATIONS_H
#define TEST_SUPPORT_DATAFILE_READER_OPERATIONS_H

#include <engine/shared/datafile.h>

#include <algorithm>
#include <limits>

class CTestDatafileReaderOperations : public CDataFileReader::IOperations
{
public:
	bool m_OverrideLength = false;
	int64_t m_Length = 0;
	bool m_InvalidReadCount = false;
	unsigned m_MaxRead = std::numeric_limits<unsigned>::max();
	size_t m_MaxAllocation = 1024 * 1024;
	int m_Reads = 0;
	int m_Allocations = 0;
	int m_RejectedAllocations = 0;

	int64_t Length(IOHANDLE File) noexcept override
	{
		const auto Actual = IOperations::Length(File);
		return m_OverrideLength ? m_Length : Actual;
	}
	unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept override
	{
		++m_Reads;
		if(m_InvalidReadCount)
			return Size + 1;
		return IOperations::Read(File, pData, std::min(Size, m_MaxRead));
	}
	void *Allocate(size_t Size) noexcept override
	{
		++m_Allocations;
		if(Size > m_MaxAllocation)
		{
			++m_RejectedAllocations;
			return nullptr;
		}
		return IOperations::Allocate(Size);
	}
};

#endif
