#ifndef TEST_SUPPORT_DATAFILE_WRITER_OPERATIONS_H
#define TEST_SUPPORT_DATAFILE_WRITER_OPERATIONS_H

#include <engine/shared/datafile.h>

#include <new>

class CTestDatafileWriterOperations : public CDataFileWriter::IOperations
{
public:
	int m_Allocations = 0;
	int m_FailAllocationAt = -1;
	int m_Compressions = 0;
	bool m_CompressionFails = false;
	bool m_CompressionThrows = false;
	int m_InvalidCompressedSize = -1;
	int m_Writes = 0;
	int m_ShortWriteAt = -1;
	int m_ThrowWriteAt = -1;
	int m_Errors = 0;
	int m_ErrorAt = -1;
	int m_ThrowErrorAt = -1;
	int m_Closes = 0;
	bool m_CloseFails = false;

	void *Allocate(size_t Size) noexcept override
	{
		return ++m_Allocations == m_FailAllocationAt ? nullptr : IOperations::Allocate(Size);
	}
	bool Compress(void *pOutput, unsigned long &OutputSize, const void *pInput, unsigned long InputSize, int Level) override
	{
		++m_Compressions;
		if(m_CompressionThrows)
			throw std::bad_alloc();
		if(m_CompressionFails)
			return false;
		if(m_InvalidCompressedSize >= 0)
		{
			OutputSize = m_InvalidCompressedSize == 0 ? 0 : OutputSize + 1;
			return true;
		}
		return IOperations::Compress(pOutput, OutputSize, pInput, InputSize, Level);
	}
	unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
	{
		++m_Writes;
		if(m_Writes == m_ThrowWriteAt)
			throw std::bad_alloc();
		return IOperations::Write(File, pData, m_Writes == m_ShortWriteAt && Size > 0 ? Size - 1 : Size);
	}
	int Error(IOHANDLE File) override
	{
		++m_Errors;
		if(m_Errors == m_ThrowErrorAt)
			throw std::bad_alloc();
		return m_Errors == m_ErrorAt ? 1 : IOperations::Error(File);
	}
	int Close(IOHANDLE File) noexcept override
	{
		++m_Closes;
		const int Result = IOperations::Close(File);
		return m_CloseFails ? -1 : Result;
	}
};

#endif
