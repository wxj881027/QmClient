#include <engine/storage.h>
#include <game/client/components/qmclient/local_map_tags.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr char TAGS_FILE[] = "qm_local_map_tags.txt";
	constexpr char SAVED_TAGS[] = "community\tmap\tfirst\ncommunity\tmap\tsecond\n";
	constexpr size_t FILE_LIMIT = 256 * 1024;
	constexpr size_t ROW_SIZE = 64;

	std::string MapTagRows(size_t Count)
	{
		const std::string Community(24, 'c');
		std::string Rows;
		for(size_t Index = 0; Index < Count; ++Index)
		{
			std::string Map = "map" + std::to_string(Index);
			Map.resize(ROW_SIZE - Community.size() - 3 - 5, 'a');
			Rows += Community + "\t" + Map + "\tfirst\n";
		}
		return Rows;
	}

	class CQmLocalMapTagsStorage : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		QmMapTags::CLocalTags m_Tags;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void Write(const char *pPath, std::string_view Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), static_cast<unsigned>(Content.size())), Content.size());
			ASSERT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pPath)
		{
			std::unique_ptr<char, decltype(&std::free)> pText(m_pStorage->ReadFileStr(pPath, IStorage::TYPE_SAVE), std::free);
			EXPECT_NE(pText, nullptr);
			return pText ? std::string(pText.get()) : std::string();
		}
	};
} // namespace

TEST_F(CQmLocalMapTagsStorage, OversizedExistingFileCannotBeOverwrittenByAnEdit)
{
	std::string Existing(SAVED_TAGS);
	Existing.resize(FILE_LIMIT + 1, ' ');
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, Existing));
	m_Tags.Load(m_pStorage.get());

	EXPECT_FALSE(m_Tags.Loaded());
	EXPECT_FALSE(m_Tags.Add(m_pStorage.get(), "community", "map", "extra"));
	EXPECT_TRUE(Read(TAGS_FILE) == Existing);
}

TEST_F(CQmLocalMapTagsStorage, AnEditRetriesTheReadAfterAStorageFailure)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(TAGS_FILE, IStorage::TYPE_SAVE));
	m_Tags.Load(m_pStorage.get());
	EXPECT_FALSE(m_Tags.Loaded());
	ASSERT_TRUE(m_pStorage->RemoveFolder(TAGS_FILE, IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, SAVED_TAGS));

	ASSERT_TRUE(m_Tags.Add(m_pStorage.get(), "community", "map", "extra"));
	QmMapTags::CLocalTags Reloaded;
	Reloaded.Load(m_pStorage.get());
	EXPECT_EQ(Reloaded.Get("community", "map"), (std::vector<std::string>{"first", "second", "extra"}));
}

TEST_F(CQmLocalMapTagsStorage, OccupiedTemporaryPathDoesNotBlockSaving)
{
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, SAVED_TAGS));
	m_Tags.Load(m_pStorage.get());
	ASSERT_TRUE(m_Tags.Loaded());
	char aTemporary[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), TAGS_FILE);
	ASSERT_NO_FATAL_FAILURE(Write(aTemporary, "existing temporary file"));

	ASSERT_TRUE(m_Tags.Remove(m_pStorage.get(), "community", "map", "first"));
	EXPECT_FALSE(m_Tags.Has("community", "map", "first"));
	EXPECT_EQ(Read(TAGS_FILE), "community\tmap\tsecond\n");
	EXPECT_EQ(Read(aTemporary), "existing temporary file");
}

TEST_F(CQmLocalMapTagsStorage, FailedReplacementDiscardsTheCandidateAndKeepsTheTagOrder)
{
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, SAVED_TAGS));
	m_Tags.Load(m_pStorage.get());
	ASSERT_TRUE(m_Tags.Loaded());
	ASSERT_TRUE(m_pStorage->RenameFile(TAGS_FILE, "previous.txt", IStorage::TYPE_SAVE, false));
	ASSERT_TRUE(m_pStorage->CreateFolder(TAGS_FILE, IStorage::TYPE_SAVE));

	EXPECT_FALSE(m_Tags.Move(m_pStorage.get(), "community", "map", 0, 1));
	EXPECT_EQ(m_Tags.Get("community", "map"), (std::vector<std::string>{"first", "second"}));
	EXPECT_TRUE(m_pStorage->FolderExists(TAGS_FILE, IStorage::TYPE_SAVE));
	EXPECT_EQ(Read("previous.txt"), SAVED_TAGS);
	char aTemporary[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), TAGS_FILE);
	EXPECT_FALSE(m_pStorage->FileExists(aTemporary, IStorage::TYPE_SAVE));
}

TEST_F(CQmLocalMapTagsStorage, TagTrailingSpacesSurviveReload)
{
	ASSERT_TRUE(m_Tags.Add(m_pStorage.get(), "", "map", "first "));
	ASSERT_TRUE(m_Tags.Add(m_pStorage.get(), "", "map", "first"));

	QmMapTags::CLocalTags Reloaded;
	Reloaded.Load(m_pStorage.get());
	EXPECT_EQ(Reloaded.Get("", "map"), (std::vector<std::string>{"first ", "first"}));
}

TEST_F(CQmLocalMapTagsStorage, AnEditCannotSaveMoreBytesThanTheLoaderCanRead)
{
	const std::string Existing = MapTagRows(FILE_LIMIT / ROW_SIZE);
	ASSERT_EQ(Existing.size(), FILE_LIMIT);
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, Existing));
	m_Tags.Load(m_pStorage.get());
	ASSERT_TRUE(m_Tags.Loaded());

	EXPECT_FALSE(m_Tags.Add(m_pStorage.get(), "community", "other", "new"));
	EXPECT_FALSE(m_Tags.Has("community", "other", "new"));
	EXPECT_TRUE(Read(TAGS_FILE) == Existing);
}

TEST_F(CQmLocalMapTagsStorage, MoveStillSavesAFullFileWithoutFinalNewline)
{
	const std::string Existing = SAVED_TAGS + MapTagRows(FILE_LIMIT / ROW_SIZE - 1) + "community\tlast-map\tlast";
	ASSERT_EQ(Existing.size(), FILE_LIMIT);
	ASSERT_NO_FATAL_FAILURE(Write(TAGS_FILE, Existing));
	m_Tags.Load(m_pStorage.get());
	ASSERT_TRUE(m_Tags.Loaded());

	ASSERT_TRUE(m_Tags.Move(m_pStorage.get(), "community", "map", 0, 1));
	EXPECT_EQ(Read(TAGS_FILE).size(), FILE_LIMIT);
	QmMapTags::CLocalTags Reloaded;
	Reloaded.Load(m_pStorage.get());
	EXPECT_EQ(Reloaded.Get("community", "map"), (std::vector<std::string>{"second", "first"}));
	EXPECT_TRUE(Reloaded.Has("community", "last-map", "last"));
}
