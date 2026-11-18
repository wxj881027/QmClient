#include <base/io.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	constexpr char OLD_MARKER[] = "session=old\nstarted_at=100\nlast_seen_at=120\nclient_id=qmtest\n";
	constexpr char NEW_MARKER[] = "session=old\nstarted_at=100\nlast_seen_at=130\nclient_id=qmtest\n";

	class CQmLifecycleMarkerWriteTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
	};
}

TEST_F(CQmLifecycleMarkerWriteTest, ReplacementLeavesExistingReaderOnPreviousCompleteMarker)
{
	ASSERT_TRUE(QmWriteLifecycleMarker(*m_pStorage, OLD_MARKER));
	std::unique_ptr<void, decltype(&io_close)> Previous(
		m_pStorage->OpenFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IOFLAG_READ, IStorage::TYPE_SAVE), io_close);
	ASSERT_NE(Previous, nullptr);

	// 部分 Windows 文件系统会在旧句柄关闭前拒绝替换，失败时同样保留旧标记。
	const bool Replaced = QmWriteLifecycleMarker(*m_pStorage, NEW_MARKER);
#if !defined(CONF_FAMILY_WINDOWS)
	EXPECT_TRUE(Replaced);
#endif
	char aPrevious[sizeof(OLD_MARKER)] = {};
	ASSERT_EQ(io_read(Previous.get(), aPrevious, sizeof(aPrevious) - 1), sizeof(aPrevious) - 1);
	EXPECT_STREQ(aPrevious, OLD_MARKER);
	Previous.reset();

	const std::unique_ptr<char, decltype(&free)> Visible(
		m_pStorage->ReadFileStr(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE), free);
	ASSERT_NE(Visible, nullptr);
	EXPECT_STREQ(Visible.get(), Replaced ? NEW_MARKER : OLD_MARKER);
	if(!Replaced)
	{
		ASSERT_TRUE(QmWriteLifecycleMarker(*m_pStorage, NEW_MARKER));
		int64_t StartedAt = 0;
		int64_t LastSeenAt = 0;
		ASSERT_TRUE(QmReadLifecycleMarker(*m_pStorage, StartedAt, LastSeenAt));
		EXPECT_EQ(LastSeenAt, 130);
	}
}

TEST_F(CQmLifecycleMarkerWriteTest, FailedPublishCleansCandidateAndCanRetry)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE));

	EXPECT_FALSE(QmWriteLifecycleMarker(*m_pStorage, OLD_MARKER));
	EXPECT_TRUE(m_pStorage->FolderExists(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE));
	char aTemporary[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), QMCLIENT_LIFECYCLE_MARKER_FILE);
	EXPECT_FALSE(m_pStorage->FileExists(aTemporary, IStorage::TYPE_SAVE));

	ASSERT_TRUE(m_pStorage->RemoveFolder(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE));
	ASSERT_TRUE(QmWriteLifecycleMarker(*m_pStorage, NEW_MARKER));
	const std::unique_ptr<char, decltype(&free)> Published(
		m_pStorage->ReadFileStr(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE), free);
	ASSERT_NE(Published, nullptr);
	EXPECT_STREQ(Published.get(), NEW_MARKER);
}

TEST_F(CQmLifecycleMarkerWriteTest, ReadsTimesFromPublishedMarker)
{
	ASSERT_TRUE(QmWriteLifecycleMarker(*m_pStorage, NEW_MARKER));

	int64_t StartedAt = 0;
	int64_t LastSeenAt = 0;
	ASSERT_TRUE(QmReadLifecycleMarker(*m_pStorage, StartedAt, LastSeenAt));
	EXPECT_EQ(StartedAt, 100);
	EXPECT_EQ(LastSeenAt, 130);
}

TEST_F(CQmLifecycleMarkerWriteTest, EmptyMarkerTriggersRecoveryWithoutKnownTimes)
{
	ASSERT_TRUE(QmWriteLifecycleMarker(*m_pStorage, ""));

	int64_t StartedAt = -1;
	int64_t LastSeenAt = -1;
	EXPECT_TRUE(QmReadLifecycleMarker(*m_pStorage, StartedAt, LastSeenAt));
	EXPECT_EQ(StartedAt, 0);
	EXPECT_EQ(LastSeenAt, 0);
}

TEST_F(CQmLifecycleMarkerWriteTest, OversizedMarkerTriggersRecoveryWithoutReadingPartialTimes)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	constexpr unsigned Size = QMCLIENT_LIFECYCLE_MARKER_MAX_BYTES + 1;
	std::string Content = NEW_MARKER;
	Content.resize(Size, ' ');
	IOHANDLE File = m_pStorage->OpenFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, Content.data(), Size), Size);
	ASSERT_EQ(io_close(File), 0);

	int64_t StartedAt = -1;
	int64_t LastSeenAt = -1;
	EXPECT_TRUE(QmReadLifecycleMarker(*m_pStorage, StartedAt, LastSeenAt));
	EXPECT_EQ(StartedAt, 0);
	EXPECT_EQ(LastSeenAt, 0);
}

TEST_F(CQmLifecycleMarkerWriteTest, MissingMarkerDoesNotTriggerRecovery)
{
	int64_t StartedAt = -1;
	int64_t LastSeenAt = -1;
	EXPECT_FALSE(QmReadLifecycleMarker(*m_pStorage, StartedAt, LastSeenAt));
	EXPECT_EQ(StartedAt, 0);
	EXPECT_EQ(LastSeenAt, 0);
}
