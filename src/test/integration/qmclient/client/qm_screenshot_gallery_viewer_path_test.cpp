#include <game/client/components/menus.h>

#include <gtest/gtest.h>
#include <test/test.h>

TEST(QmScreenshotGalleryViewerPath, UsesTheCompletePathInTheSelectedStorage)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aCompletePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "screenshots/round.png", aCompletePath, sizeof(aCompletePath));

	EXPECT_EQ(qmclient::demo_browser::BuildScreenshotViewerPath(*pStorage, IStorage::TYPE_SAVE, "screenshots", "round.png"), aCompletePath);
}

TEST(QmScreenshotGalleryViewerPath, RefusesAPathThatWouldBeTruncatedAfterAddingTheStorageRoot)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aPrefix[IO_MAX_PATH_LENGTH + 1];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aPrefix, sizeof(aPrefix));
	const size_t PrefixLength = str_length(aPrefix);
	const std::string Name = "round.png";
	ASSERT_GT(PrefixLength, 0u);
	ASSERT_GT(IO_MAX_PATH_LENGTH, PrefixLength + Name.size() + 1);
	const size_t FolderLength = IO_MAX_PATH_LENGTH - PrefixLength - Name.size() - 1;
	std::string Folder(FolderLength, 'd');
	for(size_t Separator = 64; Separator < Folder.size(); Separator += 65)
		Folder[Separator] = '/';
	if(Folder.back() == '/')
		Folder.back() = 'd';

	EXPECT_TRUE(qmclient::demo_browser::BuildScreenshotViewerPath(*pStorage, IStorage::TYPE_SAVE, Folder.c_str(), Name.c_str()).empty());
	Folder.erase(0, 1);
	EXPECT_EQ(qmclient::demo_browser::BuildScreenshotViewerPath(*pStorage, IStorage::TYPE_SAVE, Folder.c_str(), Name.c_str()).size(), IO_MAX_PATH_LENGTH - 1);
}
