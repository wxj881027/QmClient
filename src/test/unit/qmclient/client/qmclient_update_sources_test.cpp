#include <game/client/components/qmclient/update_sources.h>

#include <gtest/gtest.h>

TEST(QmUpdateSpeed, ContinuousSlowProgressSwitchesOnlyWhenAnAlternativeExists)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(4.9, 4.9 * 40 * 1024, 100 * 1024 * 1024, 1024 * 1024));
	EXPECT_TRUE(Monitor.ShouldSwitch(5, 5 * 40 * 1024, 100 * 1024 * 1024, 1024 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(10, 10 * 40 * 1024, 100 * 1024 * 1024, 0));
}

TEST(QmUpdateSpeed, HealthyLongDownloadsHaveNoTotalTimeLimit)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	for(int Second = 1; Second <= 2000; ++Second)
		EXPECT_FALSE(Monitor.ShouldSwitch(Second, Second * 512.0 * 1024, 0, 1024 * 1024));
}

TEST(QmUpdateSpeed, ShortPauseAndRecoveryDoNotSwitchSources)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(2, 1024 * 1024, 100 * 1024 * 1024, 1024 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(4, 1024 * 1024, 100 * 1024 * 1024, 1024 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 2 * 1024 * 1024, 100 * 1024 * 1024, 1024 * 1024));
	EXPECT_TRUE(Monitor.ShouldSwitch(10, 2.1 * 1024 * 1024, 100 * 1024 * 1024, 1024 * 1024));
}

TEST(QmUpdateSpeed, NearCompletionDoesNotDiscardTheWholePackage)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 100 * 1024, 200 * 1024, 1024 * 1024, true));
}

TEST(QmUpdateSpeed, SlowerAlternativeDoesNotDiscardTheCurrentPackage)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 5 * 40 * 1024, 100 * 1024 * 1024, 20 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(10, 10 * 40 * 1024, 100 * 1024 * 1024, 80 * 1024));
}

TEST(QmUpdateSpeed, NewTransferStartsWithAFreshWindow)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_TRUE(Monitor.ShouldSwitch(5, 1024, 0, 1024 * 1024));
	Monitor.Begin(25);
	EXPECT_FALSE(Monitor.ShouldSwitch(29.9, 1024, 0, 1024 * 1024));
	EXPECT_TRUE(Monitor.ShouldSwitch(30, 1024, 0, 1024 * 1024));
}

TEST(QmUpdateSpeed, OfficialBelow100KiBSwitchesWithoutRequiringTwiceTheSpeed)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_TRUE(Monitor.ShouldSwitch(5, 5 * 99 * 1024, 100 * 1024 * 1024, 100 * 1024, true));
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 5 * 100 * 1024, 100 * 1024 * 1024, 1024 * 1024, true));
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 5 * 99 * 1024, 100 * 1024 * 1024, 0, true));
}

TEST(QmUpdateSpeed, SegmentRetryProgressRegressionStartsANewWindow)
{
	qm_update::CDownloadSpeedMonitor Monitor;
	Monitor.Begin(0);
	EXPECT_FALSE(Monitor.ShouldSwitch(5, 5 * 512 * 1024, 0, 1024 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(6, 1024, 0, 1024 * 1024));
	EXPECT_FALSE(Monitor.ShouldSwitch(10.9, 1024, 0, 1024 * 1024));
	EXPECT_TRUE(Monitor.ShouldSwitch(11, 2048, 0, 1024 * 1024));
}

TEST(QmUpdateSources, OnlyOfficialResourcesMayUseSystemOrEnvironmentProxy)
{
	EXPECT_TRUE(qm_update::UpdateUsesSystemProxy("https://github.com/wxj881027/QmClient/releases/download/v3.4/file.exe"));
	EXPECT_TRUE(qm_update::UpdateUsesSystemProxy("https://api.github.com/repos/wxj881027/QmClient/releases/latest"));
	EXPECT_TRUE(qm_update::UpdateUsesSystemProxy("https://raw.githubusercontent.com/wxj881027/QmClient/master/file"));
	EXPECT_FALSE(qm_update::UpdateUsesSystemProxy("https://gh-proxy.com/https://github.com/wxj881027/QmClient/releases/download/v3.4/file.exe"));
}

namespace
{
	const std::string s_Asset = "https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.7z";
	const std::string s_Api = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
}

TEST(QmUpdateSources, GroupsAndDuplicateUrlsAreDeduplicated)
{
	qm_update::CSourceRegistry Sources({{"https://mirror/", "one", 10, qm_update::RELEASE}, {"https://node/", "one", 20, qm_update::RELEASE}, {"https://mirror/", "two", 30, qm_update::RELEASE}});
	const auto Candidates = Sources.Candidates(s_Asset, qm_update::RELEASE, 0);
	ASSERT_EQ(Candidates.size(), 2U);
	EXPECT_EQ(Candidates.front().m_Prefix, "");
	EXPECT_EQ(Candidates.back().m_Prefix, "https://mirror/");
}

TEST(QmUpdateSources, RecentApprovedMirrorCannotDisplaceOfficial)
{
	qm_update::CSourceRegistry Sources;
	Sources.SetRecent("https://gh-proxy.org/");
	const auto Candidates = Sources.Candidates(s_Api, qm_update::API, 0);
	ASSERT_EQ(Candidates.size(), 2U);
	EXPECT_EQ(Candidates.front().m_Group, "github");
	EXPECT_EQ(Candidates[1].m_Prefix, "https://gh-proxy.org/");
}

TEST(QmUpdateSources, DisabledAndUnapprovedRecentSourcesAreNeverUsed)
{
	qm_update::CSourceRegistry Sources;
	for(const auto &Recent : {"https://cdn.gh-proxy.org/", "https://attacker/"})
	{
		Sources.SetRecent(Recent);
		const auto Candidates = Sources.Candidates(s_Api, qm_update::API, 0);
		ASSERT_FALSE(Candidates.empty());
		for(const auto &Candidate : Candidates)
			EXPECT_NE(Candidate.m_Prefix, Recent);
	}
}

TEST(QmUpdateSources, ReleaseOnlyServicesAreNotApiMirrors)
{
	qm_update::CSourceRegistry Sources;
	const auto Candidates = Sources.Candidates(s_Api, qm_update::API, 0);
	ASSERT_EQ(Candidates.size(), 2U);
	EXPECT_EQ(Candidates[0].m_Group, "github");
	EXPECT_EQ(Candidates[1].m_Group, "gh-proxy");
}

TEST(QmUpdateSources, FailureDegradesWholeServiceAndRecoveryRestoresIt)
{
	qm_update::CSourceRegistry Sources;
	const auto Initial = Sources.Candidates(s_Asset, qm_update::RELEASE, 0);
	Sources.Failed(Initial[1], 10, 600);
	const auto Degraded = Sources.Candidates(s_Asset, qm_update::RELEASE, 609);
	ASSERT_EQ(Degraded.size(), 2U);
	for(const auto &Candidate : Degraded)
		EXPECT_NE(Candidate.m_Group, Initial[1].m_Group);
	EXPECT_EQ(Sources.Candidates(s_Asset, qm_update::RELEASE, 610).size(), 3U);
	Sources.Succeeded(Initial[1]);
	EXPECT_EQ(Sources.Candidates(s_Asset, qm_update::RELEASE, 20).size(), 3U);
}

TEST(QmUpdateSources, ProxyNestingCredentialsAndForeignReposAreRejected)
{
	qm_update::CSourceRegistry Sources;
	for(const auto &Url : std::vector<std::string>{"https://gh-proxy.com/" + s_Asset, "https://user:token@github.com/wxj881027/QmClient/releases/download/v3.4/file", "https://api.github.com/repos/wxj881027/QmClient/releases-evil/latest", s_Asset + "\n", s_Asset + "/https://evil/"})
		EXPECT_TRUE(Sources.Candidates(Url, qm_update::RELEASE, 0).empty()) << Url;
}

TEST(QmUpdateSources, CancelledAndExhaustedAttemptsCannotAdvance)
{
	qm_update::CSourceAttempt Attempt;
	qm_update::CSourceRegistry Sources;
	Attempt.Begin(Sources.Candidates(s_Api, qm_update::API, 0));
	Attempt.Cancel();
	EXPECT_FALSE(Attempt.Next());
	EXPECT_EQ(Attempt.Current(), nullptr);
	Attempt.Begin(Sources.Candidates(s_Api, qm_update::API, 0));
	ASSERT_TRUE(Attempt.Next());
	EXPECT_FALSE(Attempt.Next());
	EXPECT_FALSE(Attempt.Next());
}

TEST(QmUpdateSources, ProgressExtendsIdleDeadlineWithoutTotalDownloadLimit)
{
	qm_update::CProgressDeadline Deadline;
	Deadline.Begin(0);
	EXPECT_FALSE(Deadline.Expired(9, 0));
	EXPECT_TRUE(Deadline.Expired(10, 0));
	Deadline.Begin(0);
	for(int Second = 1; Second <= 3600; ++Second)
		ASSERT_FALSE(Deadline.Expired(Second, Second));
	EXPECT_FALSE(Deadline.Expired(3619, 3600));
	EXPECT_TRUE(Deadline.Expired(3620, 3600));
}

TEST(QmUpdateSources, OfficialRateLimitIsHonoredUntilRetryAfterExpires)
{
	qm_update::CSourceRegistry Sources({});
	const auto Initial = Sources.Candidates(s_Api, qm_update::API, 0);
	ASSERT_EQ(Initial.size(), 1U);
	Sources.Failed(Initial.front(), 0, 900);
	EXPECT_TRUE(Sources.Candidates(s_Api, qm_update::API, 899).empty());
	EXPECT_EQ(Sources.Candidates(s_Api, qm_update::API, 900).size(), 1U);
}
