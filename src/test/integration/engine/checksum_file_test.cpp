#include <engine/client/checksum.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <memory>

namespace
{
	class CChecksumFileTest : public testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		std::unique_ptr<void, decltype(&io_close)> m_pFile{nullptr, io_close};
		std::array<char, 4099> m_aContents{};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_aContents.fill('a');
			m_aContents.front() = 'b';
			m_aContents.back() = 'c';
			const char *pPath = "checksum-input.bin";
			m_pFile.reset(m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE));
			ASSERT_NE(m_pFile, nullptr);
			ASSERT_EQ(io_write(m_pFile.get(), m_aContents.data(), m_aContents.size()), m_aContents.size());
			ASSERT_EQ(io_close(m_pFile.release()), 0);
			m_pFile.reset(m_pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE));
			ASSERT_NE(m_pFile, nullptr);
		}
	};
}

TEST_F(CChecksumFileTest, AppendsExactlyTheRequestedRange)
{
	SHA256_CTX Context;
	sha256_init(&Context);
	ASSERT_TRUE(ChecksumAppendFileRange(Context, m_pFile.get(), 1, m_aContents.size() - 2));
	EXPECT_EQ(sha256_finish(&Context), sha256(m_aContents.data() + 1, m_aContents.size() - 2));
}

TEST_F(CChecksumFileTest, ShortReadFailsAndAllowsASeparateRetry)
{
	SHA256_CTX Context;
	sha256_init(&Context);
	EXPECT_FALSE(ChecksumAppendFileRange(Context, m_pFile.get(), 1, m_aContents.size()));

	sha256_init(&Context);
	ASSERT_TRUE(ChecksumAppendFileRange(Context, m_pFile.get(), 0, m_aContents.size()));
	EXPECT_EQ(sha256_finish(&Context), sha256(m_aContents.data(), m_aContents.size()));
}
