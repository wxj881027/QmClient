#include <base/str.h>

#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
	class CQmStorageLayout : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
		}

		void EnsureFolder(const char *pPath)
		{
			std::string Path = pPath;
			for(size_t Slash = Path.find('/'); Slash != std::string::npos; Slash = Path.find('/', Slash + 1))
			{
				const std::string Parent = Path.substr(0, Slash);
				ASSERT_TRUE(m_pStorage->CreateFolder(Parent.c_str(), IStorage::TYPE_SAVE) ||
					    m_pStorage->FolderExists(Parent.c_str(), IStorage::TYPE_SAVE))
					<< Parent;
			}
			ASSERT_TRUE(m_pStorage->CreateFolder(Path.c_str(), IStorage::TYPE_SAVE) ||
				    m_pStorage->FolderExists(Path.c_str(), IStorage::TYPE_SAVE))
				<< Path;
		}

		void Write(const char *pPath, const std::string &Text)
		{
			const char *pSlash = str_rchr(pPath, '/');
			if(pSlash != nullptr)
				EnsureFolder(std::string(pPath, pSlash - pPath).c_str());
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr) << pPath;
			ASSERT_EQ(io_write(File, Text.data(), Text.size()), Text.size()) << pPath;
			ASSERT_EQ(io_close(File), 0) << pPath;
		}

		std::string Read(const char *pPath)
		{
			char *pText = m_pStorage->ReadFileStr(pPath, IStorage::TYPE_SAVE);
			if(pText == nullptr)
				return {};
			std::string Text(pText);
			free(pText);
			return Text;
		}

		void SetModifiedAge(const char *pPath, std::chrono::seconds Age)
		{
			char aAbsolute[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aAbsolute, sizeof(aAbsolute));
			ASSERT_NE(aAbsolute[0], '\0') << pPath;
			std::error_code Error;
			std::filesystem::last_write_time(std::filesystem::u8path(aAbsolute), std::filesystem::file_time_type::clock::now() - Age, Error);
			ASSERT_FALSE(Error) << pPath << ": " << Error.message();
		}
	};
}

TEST_F(CQmStorageLayout, MigratesKnownFilesAndTreesOnce)
{
	Write("qmclient/qmclient_profiles.cfg", "profiles");
	Write("qmclient/statistics.json", "statistics");
	Write("qmclient/rank1/demos/run.demo", "demo");
	Write("qmclient/workshop/thumbs/asset.webp", "thumb");
	Write("qmclient/fontcache/preview.ttf", "preview-font");
	Write("qmclient/mapcovers/cover.png", "cover");
	Write("qmclient/chat_log/chat.txt", "chat");
	Write("qmclient/lyrics/netease/1.yrc", "lyrics");
	Write("fonts/fonts_store/installed.ttf", "installed-font");
	Write("fonts/downloaded_fonts/obsolete.otf", "obsolete-font");
	Write("map_preview_cache/preview.png", "map-preview");
	Write("cache/qm-serverlist.json", "servers");
	Write("cache/qm-serverlist.json.47756.tmp", "stale-server-cache");
	Write("qmclient/cache/qm-serverlist.json.82956.tmp", "stale-qm-server-cache");
	Write("custom-communities-ddnet-info.json", "communities");
	Write("qm_friend_last_seen.json", "friends");
	Write("qm_local_map_tags.txt", "tags");

	ASSERT_TRUE(qm_storage::MigrateStorageLayout(m_pStorage.get()));
	EXPECT_TRUE(m_pStorage->FileExists(qm_storage::LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(qm_storage::PROFILES_FILE), "profiles");
	EXPECT_EQ(Read(qm_storage::STATISTICS_FILE), "statistics");
	EXPECT_EQ(Read("qmclient/content/rank1/demos/run.demo"), "demo");
	EXPECT_EQ(Read("qmclient/cache/workshop/thumbs/asset.webp"), "thumb");
	EXPECT_EQ(Read("qmclient/cache/fonts/previews/preview.ttf"), "preview-font");
	EXPECT_EQ(Read("qmclient/cache/maps/covers/cover.png"), "cover");
	EXPECT_EQ(Read("qmclient/content/chat_logs/chat.txt"), "chat");
	EXPECT_EQ(Read("qmclient/content/lyrics/netease/1.yrc"), "lyrics");
	EXPECT_EQ(Read("qmclient/content/fonts/installed.ttf"), "installed-font");
	EXPECT_EQ(Read("qmclient/state/legacy/old_downloaded_fonts/obsolete.otf"), "obsolete-font");
	EXPECT_EQ(Read("qmclient/cache/maps/previews/preview.png"), "map-preview");
	EXPECT_EQ(Read(qm_storage::SERVERLIST_CACHE_FILE), "servers");
	EXPECT_EQ(Read(qm_storage::CUSTOM_COMMUNITIES_CACHE_FILE), "communities");
	EXPECT_EQ(Read(qm_storage::FRIEND_LAST_SEEN_CACHE_FILE), "friends");
	EXPECT_EQ(Read(qm_storage::LOCAL_MAP_TAGS_FILE), "tags");

	EXPECT_FALSE(m_pStorage->FileExists("qmclient/qmclient_profiles.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("cache/qm-serverlist.json.47756.tmp", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("qmclient/cache/qm-serverlist.json.82956.tmp", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FolderExists("qmclient/rank1", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FolderExists("qmclient/workshop/thumbs", IStorage::TYPE_SAVE));

	ASSERT_TRUE(qm_storage::MigrateStorageLayout(m_pStorage.get()));
	EXPECT_EQ(Read(qm_storage::PROFILES_FILE), "profiles");
	EXPECT_EQ(Read("qmclient/content/rank1/demos/run.demo"), "demo");
}

TEST_F(CQmStorageLayout, ConflictingTargetPreservesBothFilesAndSkipsMarker)
{
	Write("qmclient/statistics.json", "legacy-statistics");
	Write(qm_storage::STATISTICS_FILE, "current-statistics");

	EXPECT_FALSE(qm_storage::MigrateStorageLayout(m_pStorage.get()));
	EXPECT_EQ(Read("qmclient/statistics.json"), "legacy-statistics");
	EXPECT_EQ(Read(qm_storage::STATISTICS_FILE), "current-statistics");
	EXPECT_FALSE(m_pStorage->FileExists(qm_storage::LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE));
}

TEST_F(CQmStorageLayout, IdenticalTargetCompletesMigrationWithoutOverwrite)
{
	Write("qmclient/statistics.json", "same-statistics");
	Write(qm_storage::STATISTICS_FILE, "same-statistics");

	ASSERT_TRUE(qm_storage::MigrateStorageLayout(m_pStorage.get()));
	EXPECT_FALSE(m_pStorage->FileExists("qmclient/statistics.json", IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(qm_storage::STATISTICS_FILE), "same-statistics");
	EXPECT_TRUE(m_pStorage->FileExists(qm_storage::LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE));
}

TEST_F(CQmStorageLayout, NewestDuplicateCacheWinsAndOlderCopyIsPreserved)
{
	Write("cache/qm-serverlist.json", "older-serverlist");
	Write("qmclient/cache/qm-serverlist.json", "newer-serverlist");
	SetModifiedAge("cache/qm-serverlist.json", std::chrono::hours(2));
	SetModifiedAge("qmclient/cache/qm-serverlist.json", std::chrono::hours(1));

	ASSERT_TRUE(qm_storage::MigrateStorageLayout(m_pStorage.get()));
	EXPECT_EQ(Read(qm_storage::SERVERLIST_CACHE_FILE), "newer-serverlist");
	EXPECT_FALSE(m_pStorage->FileExists("cache/qm-serverlist.json", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("qmclient/cache/qm-serverlist.json", IStorage::TYPE_SAVE));
	EXPECT_EQ(Read("qmclient/state/legacy/cache/serverlist.json"), "older-serverlist");
	EXPECT_TRUE(m_pStorage->FileExists(qm_storage::LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE));
}
