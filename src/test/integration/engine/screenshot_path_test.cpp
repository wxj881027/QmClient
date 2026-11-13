#include <engine/client/screenshot_path.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <string>

namespace
{
	class ScreenshotPath : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		CConfig m_Config{};
		char m_aPath[IO_MAX_PATH_LENGTH] = "screenshots/pending_2026-01-01_00-00-00.png";
		static constexpr const char *TIMESTAMP = "2026-11-12_12-34-56";
		void SetUp() override { ASSERT_NE(m_pStorage, nullptr); }

		std::string Absolute(const char *pName)
		{
			const auto Path = (std::filesystem::absolute(std::filesystem::u8path(m_Info.StoragePath())) / std::filesystem::u8path(pName)).generic_u8string();
			return {reinterpret_cast<const char *>(Path.c_str()), Path.size()};
		}
		bool Prepare(const char *pName)
		{
			return screenshot_path::Prepare(m_pStorage.get(), m_Config, pName, TIMESTAMP, m_aPath);
		}
	};
}

TEST_F(ScreenshotPath, MissingNameKeepsTheExistingDefaultAndTimestamp)
{
	for(const char *pName : {static_cast<const char *>(nullptr), ""})
	{
		ASSERT_TRUE(Prepare(pName));
		EXPECT_STREQ(m_aPath, "screenshots/screenshot_2026-11-12_12-34-56.png");
	}
}

TEST_F(ScreenshotPath, SupportedSuffixesAndNestedNamesKeepTheirNamingRules)
{
	struct SCase
	{
		const char *m_pName;
		const char *m_pExpected;
	};
	const SCase aCases[] = {
		{"manual", "screenshots/manual_2026-11-12_12-34-56.png"},
		{"manual.PNG", "screenshots/manual_2026-11-12_12-34-56.png"},
		{"auto/stats/manual.WeBp", "screenshots/auto/stats/manual_2026-11-12_12-34-56.webp"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pName);
		ASSERT_TRUE(Prepare(Case.m_pName));
		EXPECT_STREQ(m_aPath, Case.m_pExpected);
	}
}

TEST_F(ScreenshotPath, RelativeSettingCreatesParentsBelowTheIsolatedSaveRoot)
{
	str_copy(m_Config.m_QmScreenshotDirectory, "captures/images");
	ASSERT_TRUE(Prepare("auto/manual.webp"));
	EXPECT_EQ(std::string(m_aPath), Absolute("captures/images/auto/manual_2026-11-12_12-34-56.webp"));
	EXPECT_TRUE(fs_is_dir(Absolute("captures/images/auto").c_str()));
}

TEST_F(ScreenshotPath, AbsoluteSettingKeepsItsDestination)
{
	const std::string Root = Absolute("custom screenshots");
	str_copy(m_Config.m_QmScreenshotDirectory, Root.c_str());
	ASSERT_TRUE(Prepare("manual"));
	EXPECT_EQ(std::string(m_aPath), Root + "/manual_2026-11-12_12-34-56.png");
}

TEST_F(ScreenshotPath, DirectoryFailureKeepsThePendingPathAndTheNextRequestCanRetry)
{
	const std::string Pending = m_aPath;
	IOHANDLE File = m_pStorage->OpenFile("blocked", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_close(File), 0);
	str_copy(m_Config.m_QmScreenshotDirectory, "blocked/inside");
	EXPECT_FALSE(Prepare("replacement"));
	EXPECT_STREQ(m_aPath, Pending.c_str());
	m_Config.m_QmScreenshotDirectory[0] = '\0';
	ASSERT_TRUE(Prepare("retry"));
	EXPECT_STREQ(m_aPath, "screenshots/retry_2026-11-12_12-34-56.png");
}

TEST_F(ScreenshotPath, RejectedParentTraversalDoesNotRedirectThePendingRequest)
{
	const std::string Pending = m_aPath;
	EXPECT_FALSE(Prepare("../replacement"));
	EXPECT_STREQ(m_aPath, Pending.c_str());
}

TEST_F(ScreenshotPath, EveryNativePreparationAllocationFailureKeepsThePendingPath)
{
	str_copy(m_Config.m_QmScreenshotDirectory, "captures/native-allocation/images");
	ASSERT_TRUE(Prepare("warmup"));
	std::size_t Allocations;
	bool Prepared;
	{
		CPredictionAllocationFailure Count(std::numeric_limits<std::size_t>::max());
		Prepared = Prepare("allocation-check");
		Allocations = Count.Attempts();
	}
	ASSERT_TRUE(Prepared);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	for(std::size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		str_copy(m_aPath, "screenshots/pending.png");
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Prepared = Prepare("allocation-check");
			Triggered = Fault.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Prepared);
		EXPECT_STREQ(m_aPath, "screenshots/pending.png");
		ASSERT_TRUE(Prepare("retry"));
		EXPECT_EQ(std::string(m_aPath), Absolute("captures/native-allocation/images/retry_2026-11-12_12-34-56.png"));
	}
}
