#include <game/client/components/tclient/tclient.h>
#include <test/test.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

namespace
{
	void ExpectSavedNote(IStorage &Storage, const char *pExpected)
	{
		std::unique_ptr<char, decltype(&free)> pText(Storage.ReadFileStr(qm_tclient_map_data::NOTES.m_pPath, IStorage::TYPE_SAVE), free);
		ASSERT_NE(pText, nullptr);
		std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(pText.get(), str_length(pText.get())), json_value_free);
		ASSERT_NE(pRoot, nullptr);
		const json_value *pVersion = json_object_get(pRoot.get(), "version");
		ASSERT_EQ(pVersion->type, json_integer);
		EXPECT_EQ(json_int_get(pVersion), 1);
		const json_value *pNote = json_object_get(json_object_get(pRoot.get(), "notes"), "Kobra");
		ASSERT_EQ(pNote->type, json_string);
		EXPECT_STREQ(json_string_get(pNote), pExpected);
	}
}

TEST(QmTClientMapDataSave, OccupiedCandidateKeepsSavedNotesAndAllowsRetry)
{
	using namespace qm_tclient_map_data;
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	bool Dirty = true;
	ASSERT_TRUE(Save(*pStorage, NOTES, {{"Kobra", "old note"}}, Dirty));
	EXPECT_FALSE(Dirty);
	char aCandidate[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aCandidate, sizeof(aCandidate), NOTES.m_pPath);
	ASSERT_TRUE(pStorage->CreateFolder(aCandidate, IStorage::TYPE_SAVE));

	Dirty = true;
	EXPECT_FALSE(Save(*pStorage, NOTES, {{"Kobra", "new note"}}, Dirty));
	EXPECT_TRUE(Dirty);
	EXPECT_TRUE(pStorage->FolderExists(aCandidate, IStorage::TYPE_SAVE));
	ExpectSavedNote(*pStorage, "old note");

	ASSERT_TRUE(pStorage->RemoveFolder(aCandidate, IStorage::TYPE_SAVE));
	ASSERT_TRUE(Save(*pStorage, NOTES, {{"Kobra", "new note"}}, Dirty));
	EXPECT_FALSE(Dirty);
	ExpectSavedNote(*pStorage, "new note");
}

TEST(QmTClientMapDataSave, FailedPublishCleansCandidateAndKeepsPendingCategories)
{
	using namespace qm_tclient_map_data;
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder(CATEGORIES.m_pPath, IStorage::TYPE_SAVE));

	bool Dirty = true;
	EXPECT_FALSE(Save(*pStorage, CATEGORIES, {{"Kobra", "novice"}}, Dirty));
	EXPECT_TRUE(Dirty);
	EXPECT_TRUE(pStorage->FolderExists(CATEGORIES.m_pPath, IStorage::TYPE_SAVE));
	char aCandidate[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aCandidate, sizeof(aCandidate), CATEGORIES.m_pPath);
	EXPECT_FALSE(pStorage->FileExists(aCandidate, IStorage::TYPE_SAVE));
}
