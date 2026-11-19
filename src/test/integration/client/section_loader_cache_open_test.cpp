#include <engine/storage.h>

#include <game/client/components/section_loader.h>

#include <gtest/gtest.h>
#include <test/support/regular_file_open_timeout.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>

#if defined(CONF_FAMILY_UNIX) && GTEST_HAS_DEATH_TEST && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <sys/stat.h>
#include <unistd.h>

TEST(SectionLoaderCacheOpen, RejectsNonRegularCacheAtStartup)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	constexpr char CACHE[] = "section-metadata.cfg";
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, CACHE, aPath, sizeof(aPath));
	ASSERT_EQ(mkfifo(aPath, 0600), 0);

	ASSERT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			SSessionUiCache Cache;
			Cache.m_Valid = true;
			Cache.m_LastSettingsPage = 3;
			const bool Loaded = CSectionLoader::LoadSessionCache(Cache, CACHE, pStorage.get());
			_exit(!Loaded && Cache.m_Valid && Cache.m_LastSettingsPage == 3 ? 0 : 1);
		},
		testing::ExitedWithCode(0), "");
}
#endif

TEST(SectionLoaderCacheOpen, CandidateOpenDoesNotTruncateAnAlreadyCreatedFile)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	constexpr char PATH[] = "section-metadata.tmp";
	constexpr char CONTENTS[] = "existing candidate";
	std::unique_ptr<void, decltype(&io_close)> File(pStorage->OpenFile(PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File.get(), CONTENTS, sizeof(CONTENTS) - 1), sizeof(CONTENTS) - 1);
	ASSERT_EQ(io_close(File.release()), 0);

	CSectionLoader::CSessionCacheFileOperations Operations;
	std::unique_ptr<void, decltype(&io_close)> Candidate(Operations.Open(*pStorage, PATH, IOFLAG_WRITE), io_close);
	ASSERT_EQ(Candidate, nullptr);
	std::unique_ptr<char, decltype(&free)> pContents(pStorage->ReadFileStr(PATH, IStorage::TYPE_SAVE), free);
	ASSERT_NE(pContents, nullptr);
	EXPECT_STREQ(pContents.get(), CONTENTS);
}
