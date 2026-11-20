#include <game/client/QmUi/QmDemoEncoderChoices.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>
#include <test/support/video_encoder_probe.h>

namespace
{
	std::vector<video_encoder::SProbeResult> AvailableVideoEncoders()
	{
		return {
			MakeVideoEncoderProbe("libx264", "software"),
			MakeVideoEncoderProbe("av1_nvenc", "unavailable", false),
			MakeVideoEncoderProbe("av1_qsv", "AV1 hardware with a description longer than small-string storage"),
			MakeVideoEncoderProbe("libsvtav1", "AV1 software with a description longer than small-string storage")};
	}
}

TEST(QmDemoEncoderChoices, ListsDefaultThenAvailableEncodersInProbeOrder)
{
	CQmDemoEncoderChoices Choices;
	ASSERT_TRUE(Choices.TryBuild(AvailableVideoEncoders(), "Default"));
	ASSERT_EQ(Choices.Num(), 3);
	EXPECT_STREQ(Choices.Names()[0], "Default");
	EXPECT_STREQ(Choices.Encoder(0), "");
	EXPECT_STREQ(Choices.Encoder(1), "av1_qsv");
	EXPECT_STREQ(Choices.Encoder(2), "libsvtav1");
	EXPECT_EQ(Choices.FindSelection("libsvtav1"), 2);
	EXPECT_EQ(Choices.FindSelection("av1_nvenc"), 0);
}

TEST(QmDemoEncoderChoices, EmptyProbeSnapshotDoesNotPermanentlyCacheDefaultOnly)
{
	CQmDemoEncoderChoices Choices;
	EXPECT_FALSE(Choices.TryBuild({}, "Default"));
	EXPECT_EQ(Choices.Num(), 0);
	EXPECT_EQ(Choices.Encoder(0), nullptr);
	ASSERT_TRUE(Choices.TryBuild(AvailableVideoEncoders(), "Default"));
	EXPECT_EQ(Choices.Num(), 3);
}

TEST(QmDemoEncoderChoices, LabelsAndEncoderValuesOutliveTheInputSnapshot)
{
	CQmDemoEncoderChoices Choices;
	{
		auto vProbes = AvailableVideoEncoders();
		std::string Default(200, 'D');
		ASSERT_TRUE(Choices.TryBuild(vProbes, Default.c_str()));
		Default.assign("changed");
		str_copy(vProbes[2].m_aName, "changed");
		str_copy(vProbes[2].m_aDescription, "changed");
	}
	ASSERT_EQ(Choices.Num(), 3);
	EXPECT_EQ(std::string(Choices.Names()[0]), std::string(200, 'D'));
	EXPECT_STREQ(Choices.Names()[1], "AV1 hardware with a description longer than small-string storage");
	EXPECT_STREQ(Choices.Encoder(1), "av1_qsv");
}

TEST(QmDemoEncoderChoices, EachCandidateAllocationFailureLeavesNoPartialChoicesAndAllowsRetry)
{
	const auto vProbes = AvailableVideoEncoders();
	const std::string Default(200, 'D');
	CTrackedAllocationResource Measured;
	int Allocations;
	{
		CQmDemoEncoderChoices Choices(&Measured);
		ASSERT_TRUE(Choices.TryBuild(vProbes, Default.c_str()));
		Allocations = Measured.m_Allocations;
	}
	ASSERT_GT(Allocations, 0);
	EXPECT_EQ(Measured.m_Live, 0);
	for(const bool ReturnNull : {false, true})
		for(int FailAt = 1; FailAt <= Allocations; ++FailAt)
		{
			SCOPED_TRACE(testing::Message() << "allocation=" << FailAt << ", null=" << ReturnNull);
			CTrackedAllocationResource Resource;
			{
				CQmDemoEncoderChoices Choices(&Resource);
				Resource.m_ReturnNull = ReturnNull;
				Resource.m_FailAt = FailAt;
				EXPECT_FALSE(Choices.TryBuild(vProbes, Default.c_str()));
				EXPECT_EQ(Choices.Num(), 0);
				EXPECT_EQ(Choices.Encoder(0), nullptr);
				EXPECT_EQ(Resource.m_Live, 0);
				ASSERT_TRUE(Choices.TryBuild(vProbes, Default.c_str()));
				ASSERT_EQ(Choices.Num(), 3);
				EXPECT_STREQ(Choices.Encoder(1), "av1_qsv");
				EXPECT_STREQ(Choices.Encoder(2), "libsvtav1");
			}
			EXPECT_EQ(Resource.m_Live, 0);
		}
}

TEST(QmDemoEncoderChoices, UnknownAllocatorExceptionPropagatesAndAllowsCleanRetry)
{
	CTrackedAllocationResource Resource;
	CQmDemoEncoderChoices Choices(&Resource);
	Resource.m_FailAt = 1;
	Resource.m_ThrowUnknown = true;
	EXPECT_THROW(Choices.TryBuild(AvailableVideoEncoders(), "Default"), int);
	EXPECT_EQ(Choices.Num(), 0);
	EXPECT_EQ(Resource.m_Live, 0);
	ASSERT_TRUE(Choices.TryBuild(AvailableVideoEncoders(), "Default"));
	EXPECT_EQ(Choices.Num(), 3);
}

TEST(QmDemoEncoderChoices, CompletedChoicesAreReusedWithoutFurtherAllocations)
{
	CTrackedAllocationResource Resource;
	CQmDemoEncoderChoices Choices(&Resource);
	ASSERT_TRUE(Choices.TryBuild(AvailableVideoEncoders(), "Default"));
	const int Allocations = Resource.m_Allocations;
	const char *const *ppNames = Choices.Names();
	Resource.m_FailAt = Allocations + 1;
	EXPECT_TRUE(Choices.TryBuild({}, "changed"));
	EXPECT_EQ(Resource.m_Allocations, Allocations);
	EXPECT_EQ(Choices.Names(), ppNames);
	EXPECT_STREQ(Choices.Names()[0], "Default");
	EXPECT_STREQ(Choices.Encoder(2), "libsvtav1");
}

TEST(QmDemoEncoderChoices, InvalidSelectionCannotReturnAnEncoderValue)
{
	CQmDemoEncoderChoices Choices;
	EXPECT_EQ(Choices.Encoder(0), nullptr);
	ASSERT_TRUE(Choices.TryBuild(AvailableVideoEncoders(), "Default"));
	EXPECT_EQ(Choices.Encoder(-1), nullptr);
	EXPECT_EQ(Choices.Encoder(Choices.Num()), nullptr);
	EXPECT_EQ(Choices.FindSelection(nullptr), 0);
}

TEST(QmDemoEncoderChoices, CompleteUnavailableProbeStillProvidesDefaultSoftwareChoice)
{
	CQmDemoEncoderChoices Choices;
	const std::vector<video_encoder::SProbeResult> vProbes = {
		MakeVideoEncoderProbe("libx264", "not built", false),
		MakeVideoEncoderProbe("av1_qsv", "unavailable", false)};
	ASSERT_TRUE(Choices.TryBuild(vProbes, "Default"));
	ASSERT_EQ(Choices.Num(), 1);
	EXPECT_STREQ(Choices.Names()[0], "Default");
	EXPECT_STREQ(Choices.Encoder(0), "");
}
