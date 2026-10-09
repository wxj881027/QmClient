#include <base/io_read_all.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <functional>
#include <string>

namespace
{
	struct SAllocationState
	{
		int m_Calls = 0;
		int m_FailOnCall = 0;
		int m_Frees = 0;
		void *m_pLive = nullptr;
		std::function<void()> m_OnInitialAllocation;

		static void *Reallocate(void *pBuffer, size_t Size, void *pUser)
		{
			auto &State = *static_cast<SAllocationState *>(pUser);
			++State.m_Calls;
			if(State.m_Calls == State.m_FailOnCall)
				return nullptr;
			if(State.m_Calls == 1 && State.m_OnInitialAllocation)
				State.m_OnInitialAllocation();
			EXPECT_EQ(pBuffer, State.m_pLive);
			void *pCandidate = std::realloc(pBuffer, Size);
			if(pCandidate != nullptr)
				State.m_pLive = pCandidate;
			return pCandidate;
		}

		static void Free(void *pBuffer, void *pUser)
		{
			auto &State = *static_cast<SAllocationState *>(pUser);
			EXPECT_EQ(pBuffer, State.m_pLive);
			std::free(pBuffer);
			State.m_pLive = nullptr;
			++State.m_Frees;
		}

		SIOReadAllAllocator Allocator() { return {Reallocate, Free, this}; }
		~SAllocationState() { std::free(m_pLive); }
	};

	class CIoReadAll : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::string m_Path;
		IOHANDLE m_File = nullptr;
		SAllocationState m_Allocation;

		void SetUp() override
		{
			ASSERT_EQ(fs_makedir_rec_for(m_Info.StoragePath()), 0);
			ASSERT_EQ(fs_makedir(m_Info.StoragePath()), 0);
			m_Info.m_HasCreatedStoragePath = true;
			m_Path = std::string(m_Info.StoragePath()) + "/read.bin";
		}

		bool Write(const std::string &Text, int Flags = IOFLAG_WRITE)
		{
			IOHANDLE File = io_open(m_Path.c_str(), Flags);
			if(File == nullptr)
				return false;
			const bool Written = io_write(File, Text.data(), Text.size()) == Text.size();
			const bool Closed = io_close(File) == 0;
			return Written && Closed;
		}

		bool Open(int Flags = IOFLAG_READ)
		{
			m_File = io_open(m_Path.c_str(), Flags);
			return m_File != nullptr;
		}

		bool Read(void **ppResult, unsigned *pLength)
		{
			return io_read_all_with_allocator(m_File, ppResult, pLength, m_Allocation.Allocator());
		}

		void ExpectContents(void *pResult, unsigned Length, const std::string &Expected)
		{
			ASSERT_NE(pResult, nullptr);
			EXPECT_EQ(Length, Expected.size());
			EXPECT_EQ(std::string(static_cast<char *>(pResult), Length), Expected);
			EXPECT_EQ(static_cast<char *>(pResult)[Length], '\0');
			SAllocationState::Free(pResult, &m_Allocation);
		}

		void TearDown() override
		{
			if(m_File != nullptr)
				EXPECT_EQ(io_close(m_File), 0);
		}
	};
}

TEST_F(CIoReadAll, InitialAllocationFailureClearsOutputAndCanRetry)
{
	ASSERT_TRUE(Write("contents"));
	ASSERT_TRUE(Open());
	m_Allocation.m_FailOnCall = 1;
	void *pResult = &m_Allocation;
	unsigned Length = 99;
	EXPECT_FALSE(Read(&pResult, &Length));
	EXPECT_EQ(pResult, nullptr);
	EXPECT_EQ(Length, 0u);
	EXPECT_EQ(m_Allocation.m_Frees, 0);

	m_Allocation.m_FailOnCall = 0;
	ASSERT_TRUE(Read(&pResult, &Length));
	ExpectContents(pResult, Length, "contents");
}

TEST_F(CIoReadAll, FirstGrowthFailureReleasesOriginalBuffer)
{
	ASSERT_TRUE(Write("abcd"));
	ASSERT_TRUE(Open());
	// 长度查询后文件增长，驱动真实读取接口进入扩容路径。
	m_Allocation.m_OnInitialAllocation = [&]() { EXPECT_TRUE(Write(std::string(64, 'x'), IOFLAG_APPEND)); };
	m_Allocation.m_FailOnCall = 2;
	void *pResult = &m_Allocation;
	unsigned Length = 99;
	EXPECT_FALSE(Read(&pResult, &Length));
	EXPECT_EQ(pResult, nullptr);
	EXPECT_EQ(Length, 0u);
	EXPECT_EQ(m_Allocation.m_pLive, nullptr);
	EXPECT_EQ(m_Allocation.m_Frees, 1);
}

TEST_F(CIoReadAll, LaterGrowthFailureClearsPartialDataAndCanRetry)
{
	ASSERT_TRUE(Write("a"));
	ASSERT_TRUE(Open());
	const std::string Extra(64, 'x');
	m_Allocation.m_OnInitialAllocation = [&]() { EXPECT_TRUE(Write(Extra, IOFLAG_APPEND)); };
	m_Allocation.m_FailOnCall = 3;
	void *pResult = &m_Allocation;
	unsigned Length = 99;
	EXPECT_FALSE(Read(&pResult, &Length));
	EXPECT_EQ(pResult, nullptr);
	EXPECT_EQ(Length, 0u);
	EXPECT_EQ(m_Allocation.m_pLive, nullptr);
	EXPECT_EQ(m_Allocation.m_Frees, 1);

	m_Allocation.m_FailOnCall = 0;
	ASSERT_TRUE(Read(&pResult, &Length));
	ExpectContents(pResult, Length, "a" + Extra);
}

TEST_F(CIoReadAll, ShrinkFailureKeepsValidContents)
{
	ASSERT_TRUE(Write("longer"));
	ASSERT_TRUE(Open());
	m_Allocation.m_OnInitialAllocation = [&]() { EXPECT_TRUE(Write("x")); };
	m_Allocation.m_FailOnCall = 2;
	void *pResult = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Read(&pResult, &Length));
	ExpectContents(pResult, Length, "x");
}

TEST_F(CIoReadAll, FileGrowthPreservesAllContents)
{
	ASSERT_TRUE(Write("a"));
	ASSERT_TRUE(Open());
	const std::string Extra(64, 'x');
	m_Allocation.m_OnInitialAllocation = [&]() { EXPECT_TRUE(Write(Extra, IOFLAG_APPEND)); };
	void *pResult = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Read(&pResult, &Length));
	ExpectContents(pResult, Length, "a" + Extra);
}

TEST_F(CIoReadAll, FinalShrinkFailureKeepsGrownContents)
{
	ASSERT_TRUE(Write("a"));
	ASSERT_TRUE(Open());
	m_Allocation.m_OnInitialAllocation = [&]() { EXPECT_TRUE(Write("bc", IOFLAG_APPEND)); };
	m_Allocation.m_FailOnCall = 3;
	void *pResult = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Read(&pResult, &Length));
	ExpectContents(pResult, Length, "abc");
}

TEST_F(CIoReadAll, ReadErrorReleasesBufferAndClearsOutput)
{
	ASSERT_TRUE(Open(IOFLAG_WRITE));
	void *pResult = &m_Allocation;
	unsigned Length = 99;
	EXPECT_FALSE(Read(&pResult, &Length));
	EXPECT_EQ(pResult, nullptr);
	EXPECT_EQ(Length, 0u);
	EXPECT_EQ(m_Allocation.m_pLive, nullptr);
	EXPECT_EQ(m_Allocation.m_Frees, 1);
}

TEST_F(CIoReadAll, PublicInterfaceReadsEmptyAndBinaryFiles)
{
	const std::string aInputs[] = {"", std::string("a\0b", 3)};
	for(const auto &Input : aInputs)
	{
		ASSERT_TRUE(Write(Input));
		ASSERT_TRUE(Open());
		void *pResult = nullptr;
		unsigned Length = 99;
		ASSERT_TRUE(io_read_all(m_File, &pResult, &Length));
		ASSERT_NE(pResult, nullptr);
		EXPECT_EQ(Length, Input.size());
		EXPECT_EQ(std::string(static_cast<char *>(pResult), Length), Input);
		EXPECT_EQ(static_cast<char *>(pResult)[Length], '\0');
		std::free(pResult);
		EXPECT_EQ(io_close(m_File), 0);
		m_File = nullptr;
	}
}
