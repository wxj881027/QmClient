#include <game/client/components/qmclient/skin_download_session.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(SkinDownloadRegistration, FactoryAllocationFailurePreservesExistingNames)
{
	std::vector<std::string> Names = {"first", "second"};
	int Creates = 0;
	auto pSession = QmCreateTrackedSkinDownloadSession(Names, "new", [&]() -> std::unique_ptr<CQmSkinDownloadSession> {
		++Creates;
		throw std::bad_alloc();
	});
	EXPECT_EQ(pSession, nullptr);
	EXPECT_EQ(Creates, 1);
	ASSERT_EQ(Names.size(), 2u);
	EXPECT_EQ(Names[0], "first");
	EXPECT_EQ(Names[1], "second");
}

TEST(SkinDownloadRegistration, AbsentCandidateDoesNotPublishTrackingEntry)
{
	std::vector<std::string> Names = {"existing"};
	auto pSession = QmCreateTrackedSkinDownloadSession(Names, "new", [] { return std::unique_ptr<CQmSkinDownloadSession>(); });
	EXPECT_EQ(pSession, nullptr);
	ASSERT_EQ(Names.size(), 1u);
	EXPECT_EQ(Names[0], "existing");
}
