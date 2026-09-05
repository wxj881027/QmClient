#include "test.h"

#include <game/client/components/qmclient/core/qm_diagnostics.h>
#include <game/client/components/qmclient/core/qm_diagnostics_json.h>
#include <game/client/components/qmclient/core/qm_diagnostics_metrics.h>
#include <game/client/components/qmclient/core/qm_diagnostics_retention.h>

#include <base/fs.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
int CollectDiagnosticFiles(const CFsFileInfo *pInfo, int IsDir, int, void *pUser)
{
	if(!IsDir && pInfo != nullptr && pInfo->m_pName != nullptr)
		static_cast<std::vector<std::string> *>(pUser)->emplace_back(pInfo->m_pName);
	return 0;
}

std::string ReadDiagnosticFile(IStorage *pStorage, const std::string &Name)
{
	const std::string Path = "qmclient/diagnostics/" + Name;
	char *pContents = pStorage->ReadFileStr(Path.c_str(), IStorage::TYPE_SAVE);
	if(!pContents)
		return {};
	std::string Contents = pContents;
	std::free(pContents);
	return Contents;
}
}

TEST(QmDiagnostics, CalculatesFrameMetrics)
{
	const std::vector<int64_t> Samples{1000000, 2000000, 3000000, 4000000, 5000000};
	EXPECT_DOUBLE_EQ(QmDiagnostics::Average(Samples), 3.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 0.0), 1.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 0.95), 5.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 1.0), 5.0);
	EXPECT_NEAR(QmDiagnostics::OnePercentLow(Samples), 200.0, 0.0001);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Average(std::vector<int64_t>{}), 0.0);
}

TEST(QmDiagnostics, SampleWindowKeepsRecentSamplesWithoutShifting)
{
	QmDiagnostics::CSampleWindow Samples;
	Samples.Prepare(3);
	Samples.Push(1000000, 3);
	Samples.Push(2000000, 3);
	Samples.Push(3000000, 3);
	Samples.Push(4000000, 3);

	const std::vector<int64_t> Ordered = Samples.Ordered();
	ASSERT_EQ(Ordered.size(), 3U);
	EXPECT_EQ(Ordered[0], 2000000);
	EXPECT_EQ(Ordered[1], 3000000);
	EXPECT_EQ(Ordered[2], 4000000);
}

TEST(QmDiagnostics, EscapesInvalidUtf8AndTruncatesSafely)
{
	const char InvalidUtf8[] = {'a', static_cast<char>(0xff), '"', '\n', 'b', '\0'};
	char aBuffer[64];
	QmDiagnostics::EscapeJson(aBuffer, sizeof(aBuffer), InvalidUtf8);
	EXPECT_STREQ(aBuffer, "a\\u00ff\\\"\\nb");

	char aSmallBuffer[4];
	EXPECT_FALSE(QmDiagnostics::EscapeJson(aSmallBuffer, sizeof(aSmallBuffer), "a\\\"b"));
	EXPECT_STREQ(aSmallBuffer, "a\\\\");

	char aZeroBuffer[1] = {'x'};
	EXPECT_TRUE(QmDiagnostics::EscapeJson(aZeroBuffer, sizeof(aZeroBuffer), ""));
	EXPECT_EQ(aZeroBuffer[0], '\0');

	char aFullBuffer[32];
	EXPECT_TRUE(QmDiagnostics::EscapeJson(aFullBuffer, sizeof(aFullBuffer), "a\\\"b"));
	EXPECT_STREQ(aFullBuffer, "a\\\\\\\"b");
}

TEST(QmDiagnostics, RetentionKeepsNewestFilesAndUsesStableTieBreak)
{
	std::vector<QmDiagnostics::SDiagnosticFileEntry> vEntries{
		{"report-old.tmp", 10},
		{"report-tie-a.tmp", 20},
		{"report-new.tmp", 30},
		{"report-tie-b.tmp", 20},
	};

	QmDiagnostics::SortDiagnosticFileEntries(vEntries);
	ASSERT_EQ(vEntries.size(), 4U);
	EXPECT_EQ(vEntries[0].m_Name, "report-new.tmp");
	EXPECT_EQ(vEntries[1].m_Name, "report-tie-b.tmp");
	EXPECT_EQ(vEntries[2].m_Name, "report-tie-a.tmp");
	EXPECT_EQ(vEntries[3].m_Name, "report-old.tmp");
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(vEntries.size(), 3), 3U);
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(vEntries.size(), 8), 4U);
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(0, 3), 0U);
}

TEST(QmDiagnostics, WritesLifecycleAndRejectsStaleGenerationEvents)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	CQmDiagnostics Diagnostics;
	Diagnostics.Init(pStorage.get(), nullptr);
	ASSERT_TRUE(Diagnostics.IsActive());
	const uint32_t FirstGeneration = Diagnostics.SessionGeneration();
	Diagnostics.RecordEvent("qm.test.first", "first");
	Diagnostics.Shutdown();
	EXPECT_FALSE(Diagnostics.IsActive());
	Diagnostics.Shutdown();

	Diagnostics.Init(pStorage.get(), nullptr);
	ASSERT_TRUE(Diagnostics.IsActive());
	const uint32_t SecondGeneration = Diagnostics.SessionGeneration();
	EXPECT_NE(FirstGeneration, SecondGeneration);
	Diagnostics.RecordEventNonBlocking(FirstGeneration, "qm.test.stale", "must_not_be_written");
	Diagnostics.RecordEvent("qm.test.second", "second");
	Diagnostics.RecordEventNonBlocking(SecondGeneration, "qm.test.current", "written");
	Diagnostics.Shutdown();

	std::vector<std::string> vDiagnosticFiles;
	pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, "qmclient/diagnostics", CollectDiagnosticFiles, &vDiagnosticFiles);
	size_t SessionCount = 0;
	size_t ReportCount = 0;
	size_t FirstEventCount = 0;
	size_t SecondEventCount = 0;
	size_t CurrentEventCount = 0;
	for(const std::string &Name : vDiagnosticFiles)
	{
		if(Name.rfind("session-", 0) == 0)
		{
			++SessionCount;
			const std::string Contents = ReadDiagnosticFile(pStorage.get(), Name);
			EXPECT_NE(Contents.find("\"type\":\"session_start\""), std::string::npos);
			EXPECT_NE(Contents.find("\"type\":\"session_end\""), std::string::npos);
			if(Contents.find("qm.test.first") != std::string::npos)
				++FirstEventCount;
			if(Contents.find("qm.test.second") != std::string::npos)
				++SecondEventCount;
			if(Contents.find("qm.test.current") != std::string::npos)
				++CurrentEventCount;
		EXPECT_EQ(Contents.find("qm.test.stale"), std::string::npos);
			continue;
		}
		const bool IsReport = Name.rfind("report-", 0) == 0;
		const bool IsTemporaryReport = Name.size() >= 4 && Name.compare(Name.size() - 4, 4, ".tmp") == 0;
		if(IsReport && !IsTemporaryReport)
		{
			++ReportCount;
			const std::string Contents = ReadDiagnosticFile(pStorage.get(), Name);
			EXPECT_NE(Contents.find("\"type\":\"report\""), std::string::npos);
			EXPECT_NE(Contents.find("\"write_failed\":false"), std::string::npos);
		}
	}
	EXPECT_EQ(SessionCount, 2U);
	EXPECT_EQ(ReportCount, 2U);
	EXPECT_EQ(FirstEventCount, 1U);
	EXPECT_EQ(SecondEventCount, 1U);
	EXPECT_EQ(CurrentEventCount, 1U);
}
