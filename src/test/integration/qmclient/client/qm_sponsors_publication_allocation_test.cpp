#include <engine/storage.h>

#include <game/client/components/qmclient/qm_sponsors.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <vector>

TEST(QmSponsorsPublication, FailedNamePreparationKeepsThePreviousPublishedSnapshot)
{
	static constexpr char aInitial[] = R"({"version":3,"markdown":"- A long published name that is longer than the replacement"})";
	static constexpr char aReplacement[] = R"({"version":4,"markdown":"- A long replacement name"})";
	const std::unique_ptr<json_value, decltype(&json_value_free)> pInitial(
		JsonParse(aInitial, sizeof(aInitial) - 1), json_value_free);
	const std::unique_ptr<json_value, decltype(&json_value_free)> pReplacement(
		JsonParse(aReplacement, sizeof(aReplacement) - 1), json_value_free);
	ASSERT_NE(pInitial, nullptr);
	ASSERT_NE(pReplacement, nullptr);

	qm_sponsors::CSnapshot Snapshot;
	bool Changed = false;
	ASSERT_TRUE(Snapshot.Apply(pInitial.get(), Changed));
	ASSERT_TRUE(Changed);
	const std::string InitialMarkdown = Snapshot.Markdown();
	const auto vInitialNames = Snapshot.Names();
	const int InitialRevision = Snapshot.Revision();

	bool Applied;
	bool FailedAllocation;
	{
		// 正文先准备成功，随后在构建名单时注入一次失败。
		CPredictionAllocationFailure Fault(1);
		Applied = Snapshot.Apply(pReplacement.get(), Changed);
		FailedAllocation = Fault.Triggered();
	}
	EXPECT_TRUE(FailedAllocation);
	EXPECT_FALSE(Applied);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(Snapshot.Version(), 3);
	EXPECT_EQ(Snapshot.Revision(), InitialRevision);
	EXPECT_EQ(Snapshot.Markdown(), InitialMarkdown);
	EXPECT_EQ(Snapshot.Names(), vInitialNames);

	ASSERT_TRUE(Snapshot.Apply(pReplacement.get(), Changed));
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Snapshot.Version(), 4);
	EXPECT_EQ(Snapshot.Revision(), InitialRevision + 1);
	EXPECT_EQ(Snapshot.Names(), (std::vector<std::string>{"A long replacement name"}));
}

TEST(QmSponsorsPublication, FailedDraftNamePreparationClearsPreviewAndAllowsReload)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const char *pPath = "sponsors_draft.md";
	const std::string Text = "- A long replacement name";
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size()));
	ASSERT_EQ(io_close(File), 0);
	ASSERT_EQ(Written, Text.size());

	std::string Draft = "- Old preview";
	std::vector<std::string> vNames = {"Old preview"};
	File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	bool Loaded;
	bool FailedAllocation;
	{
		CPredictionAllocationFailure Fault(1);
		Loaded = qm_sponsors::LoadDraft(File, Draft, vNames);
		FailedAllocation = Fault.Triggered();
	}
	EXPECT_TRUE(FailedAllocation);
	EXPECT_FALSE(Loaded);
	EXPECT_TRUE(Draft.empty());
	EXPECT_TRUE(vNames.empty());

	ASSERT_TRUE(qm_sponsors::LoadDraft(pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE), Draft, vNames));
	EXPECT_EQ(Draft, Text);
	EXPECT_EQ(vNames, (std::vector<std::string>{"A long replacement name"}));
}
