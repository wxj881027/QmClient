#ifndef TEST_SUPPORT_DATAFILE_WIRE_FIXTURE_H
#define TEST_SUPPORT_DATAFILE_WIRE_FIXTURE_H

#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

// 只编辑小型隔离 fixture 的线格式字段，不调用待测读取器解析它们。
inline void ReadDatafileFixtureHeader(IStorage *pStorage, const char *pName, std::array<int, 9> &aHeader)
{
	IOHANDLE File = pStorage->OpenFile(pName, IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Read = io_read(File, aHeader.data(), sizeof(aHeader));
	const int Closed = io_close(File);
	ASSERT_EQ(Read, sizeof(aHeader));
	ASSERT_EQ(Closed, 0);
#if defined(CONF_ARCH_ENDIAN_BIG)
	swap_endian(aHeader.data(), sizeof(int), aHeader.size());
#endif
}

inline void PatchDatafileFixtureWord(IStorage *pStorage, const char *pName, int Offset, int Value)
{
	IOHANDLE File = pStorage->OpenFile(pName, IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const int64_t Length = io_length(File);
	if(Length < (int64_t)Offset + (int64_t)sizeof(Value) || Length > 1024 * 1024 || Offset < 0)
	{
		io_close(File);
		FAIL() << "invalid bounded fixture size or offset";
	}
	std::vector<unsigned char> vBytes((size_t)Length);
	const unsigned Read = io_read(File, vBytes.data(), vBytes.size());
	const int ReadClosed = io_close(File);
	ASSERT_EQ(Read, vBytes.size());
	ASSERT_EQ(ReadClosed, 0);
#if defined(CONF_ARCH_ENDIAN_BIG)
	swap_endian(&Value, sizeof(int), 1);
#endif
	mem_copy(vBytes.data() + Offset, &Value, sizeof(Value));
	File = pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Written = io_write(File, vBytes.data(), vBytes.size());
	const int Closed = io_close(File);
	ASSERT_EQ(Written, vBytes.size());
	ASSERT_EQ(Closed, 0);
}

#endif
