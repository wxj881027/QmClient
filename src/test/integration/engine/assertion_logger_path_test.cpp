#include <base/dbg.h>
#include <base/logger.h>

#include <engine/shared/assertion_logger.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#if defined(CONF_FAMILY_UNIX) && GTEST_HAS_DEATH_TEST && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
namespace
{
	void DumpWithLongSavePath()
	{
		auto pInfo = std::make_shared<CTestInfo>();
		ASSERT_NE(pInfo->CreateTestStorage(), nullptr);

		constexpr char aCrashLogDirectory[] = "/dumps/QmClient_Crash/";
		constexpr char aGameName[] = "TestGame";
		// 让旧缓冲刚好装下游戏名，时间戳和进程号会被截掉。
		constexpr size_t SavePathLength = IO_MAX_PATH_LENGTH - (sizeof(aCrashLogDirectory) - 1) - sizeof(aGameName);
		std::string SavePath = std::string(pInfo->StoragePath()) + "/" + std::string(200, 'a') + "/";
		ASSERT_LT(SavePath.size(), SavePathLength);
		SavePath.resize(SavePathLength, 'b');
		const std::string TruncatedPath = SavePath + aCrashLogDirectory + aGameName;
		ASSERT_TRUE(std::filesystem::create_directories(std::filesystem::path(TruncatedPath).parent_path()));

		constexpr const char *pOldContent = "previous assertion log";
		std::ofstream ExistingReport(TruncatedPath, std::ios::binary);
		ExistingReport << pOldContent;
		ExistingReport.close();
		ASSERT_TRUE(ExistingReport);

		const char *apArgs[] = {::testing::internal::GetArgvs().front().c_str()};
		auto pStorage = CreateTempStorage(SavePath.c_str(), std::size(apArgs), apArgs);
		ASSERT_NE(pStorage, nullptr);
		const std::shared_ptr<ILogger> pLogger = CreateAssertionLogger(pStorage.get(), aGameName);
		dbg_assert_set_handler([pInfo = std::move(pInfo), pLogger, TruncatedPath, pOldContent](const char *) mutable {
			pLogger->GlobalFinish();
			std::ifstream File(TruncatedPath, std::ios::binary);
			std::string Contents;
			const bool Preserved = std::getline(File, Contents) && Contents == pOldContent;
			File.close();
			pInfo.reset();
			std::_Exit(Preserved && !::testing::Test::HasFailure() ? EXIT_SUCCESS : EXIT_FAILURE);
		});
		dbg_assert_failed("intentional assertion for the log path test");
	}
}

TEST(AssertionLoggerDeathTest, LongSavePathPreservesTheExistingLog)
{
	EXPECT_EXIT(
		{
			DumpWithLongSavePath();
			std::_Exit(EXIT_FAILURE);
		},
		::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif
