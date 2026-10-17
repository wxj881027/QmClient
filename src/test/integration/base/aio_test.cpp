// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/test.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

static const int BUF_SIZE = 64 * 1024;

class Async : public ::testing::Test
{
protected:
	ASYNCIO *m_pAio = nullptr;
	CTestInfo m_Info;
	std::string m_Path;
	bool Delete = false;
	int m_ErrorAfterDrain = 0;

	void SetUp() override
	{
		m_Path = std::string("tmp/tests/") + m_Info.m_aFilename;
		ASSERT_EQ(fs_makedir_rec_for(m_Path.c_str()), 0);
		IOHANDLE File = io_open(m_Path.c_str(), IOFLAG_WRITE);
		ASSERT_TRUE(File);
		m_pAio = aio_new(File);
		if(!m_pAio)
			io_close(File);
		ASSERT_NE(m_pAio, nullptr);
	}

	void TearDown() override
	{
		if(m_pAio)
		{
			// 断言提前退出时把收尾留给仍持有文件的工作线程。
			aio_close(m_pAio);
			aio_free(m_pAio);
		}
		if(Delete)
		{
			EXPECT_FALSE(fs_remove(m_Path.c_str()));
		}
	}

	void Write(const char *pText)
	{
		aio_write(m_pAio, pText, str_length(pText));
	}

	void Expect(const char *pOutput)
	{
		ASYNCIO *pAio = std::exchange(m_pAio, nullptr);
		auto pError = std::make_shared<int>(0);
		ASSERT_TRUE(WaitForAsyncIo([pAio, pError] {
			aio_close(pAio);
			aio_wait(pAio);
			*pError = aio_error(pAio);
			aio_free(pAio);
		}));
		m_ErrorAfterDrain = *pError;

		char aBuf[BUF_SIZE];
		IOHANDLE File = io_open(m_Path.c_str(), IOFLAG_READ);
		ASSERT_TRUE(File);
		int Read = io_read(File, aBuf, sizeof(aBuf));
		io_close(File);

		ASSERT_EQ(str_length(pOutput), Read);
		ASSERT_TRUE(mem_comp(aBuf, pOutput, Read) == 0);
		Delete = true;
	}
};

TEST_F(Async, Empty)
{
	Expect("");
}

TEST_F(Async, Simple)
{
	static const char TEXT[] = "a\n";
	Write(TEXT);
	Expect(TEXT);
}

TEST_F(Async, Long)
{
	char aText[BUF_SIZE + 1];
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		aText[i] = 'a';
	}
	aText[sizeof(aText) - 1] = 0;
	Write(aText);
	Expect(aText);
}

TEST_F(Async, Pieces)
{
	char aText[BUF_SIZE + 1];
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		aText[i] = 'a';
	}
	aText[sizeof(aText) - 1] = 0;
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		Write("a");
	}
	Expect(aText);
}

TEST_F(Async, Mixed)
{
	char aText[BUF_SIZE + 1];
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		aText[i] = 'a' + i % 26;
	}
	aText[sizeof(aText) - 1] = 0;
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		char w = 'a' + i % 26;
		aio_write(m_pAio, &w, 1);
	}
	Expect(aText);
}

TEST_F(Async, NonDivisor)
{
	static const int NUM_LETTERS = 13;
	static const int SIZE = BUF_SIZE / NUM_LETTERS * NUM_LETTERS;
	char aText[SIZE + 1];
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		aText[i] = 'a' + i % NUM_LETTERS;
	}
	aText[sizeof(aText) - 1] = 0;
	for(unsigned i = 0; i < (sizeof(aText) - 1) / NUM_LETTERS; i++)
	{
		Write("abcdefghijklm");
	}
	Expect(aText);
}

TEST_F(Async, Transaction)
{
	static const int NUM_LETTERS = 13;
	static const int SIZE = BUF_SIZE / NUM_LETTERS * NUM_LETTERS;
	char aText[SIZE + 1];
	for(unsigned i = 0; i < sizeof(aText) - 1; i++)
	{
		aText[i] = 'a' + i % NUM_LETTERS;
	}
	aText[sizeof(aText) - 1] = 0;
	for(unsigned i = 0; i < (sizeof(aText) - 1) / NUM_LETTERS; i++)
	{
		aio_lock(m_pAio);
		for(char c = 'a'; c < 'a' + NUM_LETTERS; c++)
		{
			aio_write_unlocked(m_pAio, &c, 1);
		}
		aio_unlock(m_pAio);
	}
	Expect(aText);
}

TEST_F(Async, OversizedWritePreservesQueueAndErrorAfterLaterFlush)
{
	Write("before");
	aio_write(m_pAio, nullptr, std::numeric_limits<unsigned>::max());
	EXPECT_NE(aio_error(m_pAio), 0);
	Write("after");
	ASSERT_NO_FATAL_FAILURE(Expect("beforeafter"));
	EXPECT_NE(m_ErrorAfterDrain, 0);
}

TEST_F(Async, CloseDrainsAcceptedWritesAndIgnoresLaterWrites)
{
	Write("accepted");
	aio_close(m_pAio);
	Write("late");
	ASSERT_NO_FATAL_FAILURE(Expect("accepted"));
	EXPECT_EQ(m_ErrorAfterDrain, 0);
}
