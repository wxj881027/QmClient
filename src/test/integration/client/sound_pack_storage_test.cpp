#include <base/system.h>

#include <engine/storage.h>

#include <game/client/sound_pack.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CSoundPackStorage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			for(const char *pDirectory : {"audio", "audio/first", "audio/first/audio", "audio/second"})
				ASSERT_TRUE(m_pStorage->CreateFolder(pDirectory, IStorage::TYPE_SAVE));
		}

		void Store(const char *pFilename)
		{
			IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CSoundPackStorage, SourceMutationDoesNotChangeAnExistingBatch)
{
	Store("audio/first/hit.wv");
	Store("audio/second/hit.wv");
	char aName[64] = "first";
	const CSoundPack First(aName);
	str_copy(aName, "second");
	const CSoundPack Second(aName);
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_STREQ(First.Resolve("audio/hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/hit.wv");
	EXPECT_STREQ(Second.Resolve("audio/hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/second/hit.wv");
}

TEST_F(CSoundPackStorage, BatchRetainsTheNameAfterItsSourceIsDestroyed)
{
	Store("audio/first/hit.wv");
	Store("audio/first/jump.wv");
	std::unique_ptr<CSoundPack> pPack;
	{
		std::string Name = "first";
		pPack = std::make_unique<CSoundPack>(Name.c_str());
	}
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_STREQ(pPack->Resolve("audio/hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/hit.wv");
	EXPECT_STREQ(pPack->Resolve("audio/jump.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/jump.wv");
}

TEST_F(CSoundPackStorage, ShortOverrideLayoutHasPriorityOverLegacyLayout)
{
	Store("audio/first/hit.wv");
	Store("audio/first/audio/hit.wv");
	const CSoundPack Pack("first");
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_STREQ(Pack.Resolve("audio/hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/hit.wv");
}

TEST_F(CSoundPackStorage, MissingShortLayoutFallsBackToLegacyLayout)
{
	Store("audio/first/audio/hit.wv");
	const CSoundPack Pack("first");
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_STREQ(Pack.Resolve("audio/hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/audio/hit.wv");
}

TEST_F(CSoundPackStorage, MissingOverrideReturnsTheOriginalBorrowedFilename)
{
	const CSoundPack Pack("first");
	const char aFilename[] = "audio/missing.wv";
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_EQ(Pack.Resolve(aFilename, m_pStorage.get(), aPath, sizeof(aPath)), aFilename);
}

TEST_F(CSoundPackStorage, EmptyPackKeepsTheOriginalFilenameDespiteAnOverride)
{
	Store("audio/hit.wv");
	const CSoundPack Pack("");
	const char aFilename[] = "audio/hit.wv";
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_EQ(Pack.Resolve(aFilename, m_pStorage.get(), aPath, sizeof(aPath)), aFilename);
}

TEST_F(CSoundPackStorage, FilenameWithoutAudioPrefixUsesThePackDirectory)
{
	Store("audio/first/hit.wv");
	const CSoundPack Pack("first");
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_STREQ(Pack.Resolve("hit.wv", m_pStorage.get(), aPath, sizeof(aPath)), "audio/first/hit.wv");
}

TEST_F(CSoundPackStorage, UnavailableLookupInputsPreserveTheOriginalPointer)
{
	const CSoundPack Pack("first");
	const char aFilename[] = "audio/hit.wv";
	const char aEmpty[] = "";
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_EQ(Pack.Resolve(aFilename, nullptr, aPath, sizeof(aPath)), aFilename);
	EXPECT_EQ(Pack.Resolve(nullptr, m_pStorage.get(), aPath, sizeof(aPath)), nullptr);
	EXPECT_EQ(Pack.Resolve(aEmpty, m_pStorage.get(), aPath, sizeof(aPath)), aEmpty);
}
