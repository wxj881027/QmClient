#include <engine/client/video_encoder_probe_cache.h>

#include <game/client/QmUi/QmDemoEncoderChoices.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>
#include <test/support/video_encoder_probe.h>

TEST(QmDemoEncoderRecovery, ProbeAllocationFailureLeavesChoicesPendingUntilCompleteRetry)
{
	CTrackedAllocationResource Resource;
	CVideoEncoderProbeCache<CBorrowedAllocator<video_encoder::SProbeResult>> Cache{&Resource};
	CQmDemoEncoderChoices Choices;
	int Calls = 0;
	const auto Probe = [&](auto &vResults) {
		++Calls;
		vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "AV1 hardware"));
	};
	Resource.m_FailAt = 1;
	const auto &Unavailable = Cache.Get(Probe);
	EXPECT_FALSE(Choices.TryBuild(Unavailable, "Default"));
	EXPECT_EQ(Choices.Num(), 0);
	ASSERT_TRUE(Choices.TryBuild(Cache.Get(Probe), "Default"));
	EXPECT_TRUE(Unavailable.empty());
	EXPECT_EQ(Calls, 2);
	ASSERT_EQ(Choices.Num(), 2);
	EXPECT_STREQ(Choices.Encoder(1), "av1_qsv");
}

TEST(QmDemoEncoderRecovery, ChoicesAllocationFailureReusesSuccessfulProbeSnapshot)
{
	CVideoEncoderProbeCache<> Cache;
	CTrackedAllocationResource Resource;
	CQmDemoEncoderChoices Choices(&Resource);
	int Calls = 0;
	const auto Probe = [&](auto &vResults) {
		++Calls;
		vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "AV1 hardware"));
	};
	Resource.m_FailAt = 1;
	const auto &Snapshot = Cache.Get(Probe);
	EXPECT_FALSE(Choices.TryBuild(Snapshot, "Default"));
	EXPECT_EQ(Choices.Num(), 0);
	ASSERT_TRUE(Choices.TryBuild(Cache.Get(Probe), "Default"));
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(&Cache.Get(Probe), &Snapshot);
	ASSERT_EQ(Choices.Num(), 2);
	EXPECT_STREQ(Choices.Encoder(1), Snapshot[0].m_aName);
}

TEST(QmDemoEncoderRecovery, PartialProbeCannotPublishADefaultOnlyMenu)
{
	CVideoEncoderProbeCache<> Cache;
	CQmDemoEncoderChoices Choices;
	const auto &Partial = Cache.Get([](auto &vResults) {
		vResults.push_back(MakeVideoEncoderProbe("libx264", "partial"));
		throw std::bad_alloc();
	});
	EXPECT_FALSE(Choices.TryBuild(Partial, "Default"));
	const auto &Complete = Cache.Get([](auto &vResults) {
		vResults.push_back(MakeVideoEncoderProbe("libx264", "software"));
		vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "hardware"));
	});
	ASSERT_TRUE(Choices.TryBuild(Complete, "Default"));
	ASSERT_EQ(Choices.Num(), 2);
	EXPECT_STREQ(Choices.Encoder(1), "av1_qsv");
}

TEST(QmDemoEncoderRecovery, FamilyAliasSelectsTheSameValueShownInCompleteChoices)
{
	CVideoEncoderProbeCache<> Cache;
	CQmDemoEncoderChoices Choices;
	const auto &Snapshot = Cache.Get([](auto &vResults) {
		vResults.push_back(MakeVideoEncoderProbe("libsvtav1", "AV1 software"));
		vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "AV1 hardware"));
	});
	ASSERT_TRUE(Choices.TryBuild(Snapshot, "Default"));
	const char *pSelected = video_encoder::PreferredEncoderForFamily("av1", Snapshot);
	EXPECT_STREQ(pSelected, "av1_qsv");
	const int Selection = Choices.FindSelection(pSelected);
	EXPECT_EQ(Selection, 2);
	EXPECT_STREQ(Choices.Encoder(Selection), pSelected);
}
