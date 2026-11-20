#include <base/str.h>

#include <engine/client/updater_data.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	using namespace QmLegacyUpdater;
	using TJobs = std::vector<std::pair<std::string, bool>>;
	TJobs Jobs(const SUpdatePlan &Plan) { return {Plan.m_FileJobs.begin(), Plan.m_FileJobs.end()}; }
	SUpdatePlan PreviousPlan()
	{
		SUpdatePlan Plan;
		Plan.m_ServerUpdate = true;
		Plan.m_FileJobs.emplace_front("previous.bin", false);
		return Plan;
	}
}

TEST(LegacyUpdaterData, DirectClientUrlAndExplicitDestinationRemainComplete)
{
	SRequestPaths Paths;
	ASSERT_TRUE(PrepareRequestPaths("DDNet.exe", "DDNet.123.tmp", "client/", 8, Paths));
	EXPECT_STREQ(Paths.m_aUrl, "https://github.com/wxj881027/Q1menG_Client/releases/latest/download/DDNet.exe");
	EXPECT_STREQ(Paths.m_aDestination, "update/DDNet.123.tmp");
}

TEST(LegacyUpdaterData, EncodesTheWholeUnicodePathWithoutChangingDirectorySeparators)
{
	SRequestPaths Paths;
	ASSERT_TRUE(PrepareRequestPaths("data/测试 file.bin", {}, {}, 8, Paths));
	EXPECT_STREQ(Paths.m_aUrl, "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/data/%E6%B5%8B%E8%AF%95%20file.bin");
	EXPECT_STREQ(Paths.m_aDestination, "update/data/测试 file.bin");
}

TEST(LegacyUpdaterData, AUrlLongerThanTheOldBufferIsPreservedCompletely)
{
	const std::string Name = "data/" + std::string(240, 'a') + ".bin";
	SRequestPaths Paths;
	ASSERT_TRUE(PrepareRequestPaths(Name, {}, {}, 8, Paths));
	EXPECT_EQ(std::string(Paths.m_aUrl), "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/" + Name);
	EXPECT_GT(std::strlen(Paths.m_aUrl), 256u);
}

TEST(LegacyUpdaterData, PercentAndHashRemainFilenameBytesInTheEncodedUrl)
{
	SRequestPaths Paths;
	ASSERT_TRUE(PrepareRequestPaths("data/a%#b.bin", {}, {}, 8, Paths));
	EXPECT_STREQ(Paths.m_aUrl, "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/data/a%25%23b.bin");
}

TEST(LegacyUpdaterData, InvalidPathsNeverReplaceThePreviousRequestIdentity)
{
	SRequestPaths Paths;
	ASSERT_TRUE(PrepareRequestPaths("DDNet.exe", {}, {}, 8, Paths));
	const SRequestPaths Previous = Paths;
	for(std::string_view Invalid : {"../wrong", "/absolute", "C:/absolute", "dir\\file", "dir//file", "dir/", "NUL", ""})
	{
		SCOPED_TRACE(Invalid);
		EXPECT_FALSE(PrepareRequestPaths(Invalid, {}, {}, 8, Paths));
		EXPECT_STREQ(Paths.m_aUrl, Previous.m_aUrl);
		EXPECT_STREQ(Paths.m_aDestination, Previous.m_aDestination);
	}
}

TEST(LegacyUpdaterData, EmbeddedNullAndInvalidUtf8AreRejectedBeforeEncoding)
{
	SRequestPaths Paths;
	const std::string NullPath("good\0bad", 8);
	EXPECT_FALSE(PrepareRequestPaths(NullPath, {}, {}, 8, Paths));
	EXPECT_FALSE(PrepareRequestPaths(std::string("\xff.bin"), {}, {}, 8, Paths));
	EXPECT_FALSE(PrepareRequestPaths("good.bin", NullPath, {}, 8, Paths));
}

TEST(LegacyUpdaterData, CompleteTemporaryPathAcceptsExactCapacityAndRejectsOneMoreByte)
{
	SRequestPaths Paths;
	constexpr size_t SuffixLength = 12;
	constexpr size_t DestinationLength = sizeof("update/a") - 1;
	const std::string Root(IO_MAX_PATH_LENGTH - 1 - SuffixLength - DestinationLength, 'r');
	ASSERT_TRUE(PrepareRequestPaths("a", {}, Root, SuffixLength, Paths));
	const SRequestPaths Previous = Paths;
	EXPECT_FALSE(PrepareRequestPaths("a", {}, Root + "r", SuffixLength, Paths));
	EXPECT_STREQ(Paths.m_aUrl, Previous.m_aUrl);
	EXPECT_STREQ(Paths.m_aDestination, Previous.m_aDestination);
}

TEST(LegacyUpdaterData, RelativeDestinationMustFitThePrefixAndTerminator)
{
	SRequestPaths Paths;
	const std::string TooLong(IO_MAX_PATH_LENGTH - sizeof("update/") + 1, 'a');
	EXPECT_FALSE(PrepareRequestPaths("a", TooLong, {}, 0, Paths));
	EXPECT_FALSE(PrepareRequestPaths("a", {}, {}, IO_MAX_PATH_LENGTH, Paths));
}

TEST(LegacyUpdaterData, BinaryPathResolutionDoesNotPublishATruncatedPrefix)
{
	char aPath[sizeof("root/file")] = "previous";
	ASSERT_TRUE(ResolveBinaryPath("root/", "file", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "root/file");
	EXPECT_FALSE(ResolveBinaryPath("root/", "file", aPath, sizeof(aPath) - 1));
	EXPECT_STREQ(aPath, "root/file");
}

TEST(LegacyUpdaterData, ManifestKeepsNewestDecisionAndOriginalReverseExecutionOrder)
{
	SUpdatePlan Plan;
	ASSERT_TRUE(ParseManifest(R"([{"version":"3","client":true,"download":["a.bin"],"remove":["b.bin"]},{"version":"2","server":true,"download":["b.bin","c.bin"],"remove":["a.bin"]},{"version":"1","download":42}])", "1", Plan));
	EXPECT_TRUE(Plan.m_ClientUpdate);
	EXPECT_TRUE(Plan.m_ServerUpdate);
	EXPECT_EQ(Jobs(Plan), (TJobs{{"c.bin", true}, {"b.bin", false}, {"a.bin", true}}));
}

TEST(LegacyUpdaterData, CurrentVersionStopsBeforeOlderOperationsAreConsumed)
{
	SUpdatePlan Plan = PreviousPlan();
	ASSERT_TRUE(ParseManifest(R"([{"version":"1"},{"version":42,"download":[7]}])", "1", Plan));
	EXPECT_FALSE(Plan.m_ClientUpdate);
	EXPECT_FALSE(Plan.m_ServerUpdate);
	EXPECT_TRUE(Plan.m_FileJobs.empty());
}

TEST(LegacyUpdaterData, WrongRequiredFieldTypesPreserveThePreviousPlan)
{
	for(std::string_view Content : {R"([{"version":42}])", R"([{"version":"2","download":[7]}])", R"([{"version":"2","download":[4294967296]}])", R"([{"version":"2","remove":true}])", R"([{"version":"2","client":"true"}])", R"([7])"})
	{
		SCOPED_TRACE(Content);
		SUpdatePlan Plan = PreviousPlan();
		EXPECT_FALSE(ParseManifest(Content, "1", Plan));
		EXPECT_TRUE(Plan.m_ServerUpdate);
		EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
	}
}

TEST(LegacyUpdaterData, InvalidLaterVersionCannotPublishTheValidPrefix)
{
	SUpdatePlan Plan = PreviousPlan();
	EXPECT_FALSE(ParseManifest(R"([{"version":"3","client":true,"download":["new.bin"]},{"version":"2","remove":[9]}])", "1", Plan));
	EXPECT_FALSE(Plan.m_ClientUpdate);
	EXPECT_TRUE(Plan.m_ServerUpdate);
	EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
}

TEST(LegacyUpdaterData, ConsumedStringsWithEmbeddedNullAreNotTruncated)
{
	for(std::string_view Content : {R"([{"version":"1\u0000extra","download":["a.bin"]}])", R"([{"version":"2","download":["safe.bin\u0000other"]}])", R"([{"version":"2","remove":["safe.bin\u0000other"]}])"})
	{
		SUpdatePlan Plan = PreviousPlan();
		EXPECT_FALSE(ParseManifest(Content, "1", Plan));
		EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
	}
}

TEST(LegacyUpdaterData, FullMemberNamesPreventNullPrefixAliases)
{
	SUpdatePlan Plan;
	ASSERT_TRUE(ParseManifest(R"([{"version":"2","download\u0000ignored":[7],"download":["a.bin"]}])", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"a.bin", true}}));
	EXPECT_FALSE(ParseManifest(R"([{"version\u0000ignored":"1","download":["a.bin"]}])", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"a.bin", true}}));
}

TEST(LegacyUpdaterData, DuplicateConsumedFieldsAreRejectedWithoutChoosingOne)
{
	for(std::string_view Content : {R"([{"version":"2","version":"1"}])", R"([{"version":"2","download":[],"download":["a.bin"]}])", R"([{"version":"2","client":false,"client":true}])"})
	{
		SUpdatePlan Plan = PreviousPlan();
		EXPECT_FALSE(ParseManifest(Content, "1", Plan));
		EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
	}
}

TEST(LegacyUpdaterData, UnsafePathsRejectTheWholeManifest)
{
	SUpdatePlan Plan = PreviousPlan();
	EXPECT_FALSE(ParseManifest(R"([{"version":"2","download":["safe.bin","../bad"]}])", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
}

TEST(LegacyUpdaterData, InputByteBudgetAcceptsExactSizeAndRejectsOneMore)
{
	SUpdatePlan Plan;
	const std::string Content = "[]" + std::string(MAX_MANIFEST_BYTES - 2, ' ');
	ASSERT_TRUE(ParseManifest(Content, "1", Plan));
	Plan = PreviousPlan();
	EXPECT_FALSE(ParseManifest(Content + " ", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
}

TEST(LegacyUpdaterData, OperationBudgetCountsDuplicateEntriesBeforeDeduplication)
{
	std::string Content = R"([{"version":"2","download":[)";
	for(size_t Index = 0; Index < MAX_MANIFEST_OPERATIONS; ++Index)
	{
		if(Index)
			Content += ',';
		Content += R"("a.bin")";
	}
	SUpdatePlan Plan;
	ASSERT_TRUE(ParseManifest(Content + "]}]", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"a.bin", true}}));
	Plan = PreviousPlan();
	EXPECT_FALSE(ParseManifest(Content + R"(,"a.bin"]}])", "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
}

TEST(LegacyUpdaterData, JsonAllocationBudgetRejectsDenseMetadataWithoutPublishing)
{
	std::string Content = R"([{"version":"2","unknown":[)";
	for(size_t Index = 0; Index < 500000; ++Index)
	{
		if(Index)
			Content += ',';
		Content += '0';
	}
	Content += "]}]";
	ASSERT_LT(Content.size(), MAX_MANIFEST_BYTES);
	SUpdatePlan Plan = PreviousPlan();
	EXPECT_FALSE(ParseManifest(Content, "1", Plan));
	EXPECT_EQ(Jobs(Plan), (TJobs{{"previous.bin", false}}));
}
