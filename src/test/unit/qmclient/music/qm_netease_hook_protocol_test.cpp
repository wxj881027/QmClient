#include <game/client/components/qmclient/netease_hook/qm_netease_hook_metadata.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
	QmNeteaseHook::SSnapshot ValidSnapshot()
	{
		QmNeteaseHook::SSnapshot Snapshot;
		Snapshot.m_Sequence = 2;
		Snapshot.m_Status = QmNeteaseHook::STATUS_HAS_MEDIA | QmNeteaseHook::STATUS_PLAYING | QmNeteaseHook::STATUS_HAS_CURRENT_LINE | QmNeteaseHook::STATUS_HAS_COVER;
		Snapshot.m_DurationMs = 180000;
		Snapshot.m_PositionMs = 12000;
		Snapshot.m_CurrentLineStartMs = 10000;
		Snapshot.m_CurrentLineEndMs = 15000;
		std::strcpy(Snapshot.m_aTitle, "title");
		std::strcpy(Snapshot.m_aCurrentLine, "line");
		std::strcpy(Snapshot.m_aCoverPath, "C:/Users/test/AppData/Local/QmClient/netease-hook/cover-1.png");
		QmNeteaseHook::FinalizeSnapshot(&Snapshot);
		return Snapshot;
	}
}

TEST(QmNeteaseHookProtocol, RejectsInvalidMagic)
{
	auto Snapshot = ValidSnapshot();
	Snapshot.m_Magic ^= 1;
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, RejectsUnsupportedSchema)
{
	auto Snapshot = ValidSnapshot();
	Snapshot.m_SchemaVersion++;
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, RejectsInvalidChecksum)
{
	auto Snapshot = ValidSnapshot();
	Snapshot.m_aTitle[0] = 'X';
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, RejectsTornSequence)
{
	EXPECT_FALSE(QmNeteaseHook::IsStableSequence(3, 3));
	EXPECT_FALSE(QmNeteaseHook::IsStableSequence(2, 4));
	EXPECT_TRUE(QmNeteaseHook::IsStableSequence(2, 2));
}

TEST(QmNeteaseHookProtocol, SharedControlFlagsDoNotOverlapSnapshots)
{
	QmNeteaseHook::SSharedBlock Shared{};
	Shared.m_ControlFlags = QmNeteaseHook::CONTROL_STOP_REQUESTED | QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED;

	EXPECT_EQ(Shared.m_ActiveSequence, 0);
	EXPECT_EQ(Shared.m_aSnapshots[0].m_Sequence, 0u);
	EXPECT_EQ(Shared.m_aSnapshots[1].m_Sequence, 0u);
	EXPECT_EQ(Shared.m_ControlFlags & QmNeteaseHook::CONTROL_STOP_REQUESTED, QmNeteaseHook::CONTROL_STOP_REQUESTED);
	EXPECT_EQ(Shared.m_ControlFlags & QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED, QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED);
}

TEST(QmNeteaseHookProtocol, RejectsCurrentLineWithoutBoundaries)
{
	auto Snapshot = ValidSnapshot();
	Snapshot.m_CurrentLineEndMs = Snapshot.m_CurrentLineStartMs;
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, AcceptsCurrentLineWithUnknownBoundaries)
{
	auto Snapshot = ValidSnapshot();
	Snapshot.m_CurrentLineStartMs = -1;
	Snapshot.m_CurrentLineEndMs = -1;
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, RejectsCoverFlagWithoutPathOrUrl)
{
	QmNeteaseHook::SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_aCoverPath[0] = '\0';
	Snapshot.m_aCoverUrl[0] = '\0';
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookProtocol, AcceptsCoverUrlWithoutLocalPath)
{
	QmNeteaseHook::SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_aCoverPath[0] = '\0';
	std::strcpy(Snapshot.m_aCoverUrl, "https://example.invalid/cover.png");
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_TRUE(QmNeteaseHook::HasCover(Snapshot));
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookMetadata, MatchesNeteaseMainWindowTitleWithoutPlaybackHistory)
{
	std::vector<QmNeteaseHook::SLocalTrackMetadata> Tracks = {
		{"100", "What Lovers Do", "Maroon 5, SZA", "Red Pill Blues", "https://example.invalid/old.png", 180000},
		{"200", "What Do You Mean?", "Justin Bieber", "Purpose", "https://example.invalid/current.png", 210000},
		{"300", "I Will Be OK", "FlyBoy, Coby Grant, The Onyx Twins", "I Will Be OK", "https://example.invalid/next.png", 190000},
	};

	const QmNeteaseHook::SLocalTrackMetadata *pTrack = QmNeteaseHook::FindLocalTrackByWindowTitle(Tracks, "What Do You Mean? - Justin Bieber");

	ASSERT_NE(pTrack, nullptr);
	EXPECT_EQ(pTrack->m_SongId, "200");
	EXPECT_EQ(pTrack->m_Title, "What Do You Mean?");
}

TEST(QmNeteaseHookMetadata, MatchesArtistListWithWindowSlashSeparators)
{
	std::vector<QmNeteaseHook::SLocalTrackMetadata> Tracks = {
		{"100", "What Lovers Do", "Maroon 5, SZA", "Red Pill Blues", "", 180000},
		{"200", "I Will Be OK", "FlyBoy, Coby Grant, The Onyx Twins", "I Will Be OK", "", 210000},
	};

	const QmNeteaseHook::SLocalTrackMetadata *pTrack = QmNeteaseHook::FindLocalTrackByWindowTitle(Tracks, "I Will Be OK - FlyBoy / Coby Grant / The Onyx Twins");

	ASSERT_NE(pTrack, nullptr);
	EXPECT_EQ(pTrack->m_SongId, "200");
}

TEST(QmNeteaseHookMetadata, MatchesTitleContainingTheWindowSeparator)
{
	std::vector<QmNeteaseHook::SLocalTrackMetadata> Tracks = {
		{"100", "Live - From Somewhere", "Example Artist", "Album", "", 180000},
	};

	const QmNeteaseHook::SLocalTrackMetadata *pTrack = QmNeteaseHook::FindLocalTrackByWindowTitle(Tracks, "Live - From Somewhere - Example Artist");

	ASSERT_NE(pTrack, nullptr);
	EXPECT_EQ(pTrack->m_SongId, "100");
}

TEST(QmNeteaseHookMetadata, RejectsAmbiguousWindowTitleMatchingDifferentSongIds)
{
	std::vector<QmNeteaseHook::SLocalTrackMetadata> Tracks = {
		{"100", "Same Title", "Same Artist", "Album A", "", 180000},
		{"200", "Same Title", "Same Artist", "Album B", "", 180000},
	};

	EXPECT_EQ(QmNeteaseHook::FindLocalTrackByWindowTitle(Tracks, "Same Title - Same Artist"), nullptr);
}

TEST(QmNeteaseHookMetadata, DoesNotInventMediaForAnIncompleteWindowTitle)
{
	std::vector<QmNeteaseHook::SLocalTrackMetadata> Tracks = {
		{"100", "Known Song", "Known Artist", "Album", "", 180000},
	};

	EXPECT_EQ(QmNeteaseHook::FindLocalTrackByWindowTitle(Tracks, "Known Song"), nullptr);
}

TEST(QmNeteaseHookMetadata, FillsIndependentMediaSnapshotFromMatchedTrack)
{
	const QmNeteaseHook::SLocalTrackMetadata Track{
		"502448541", "What Lovers Do", "Maroon 5, SZA", "Red Pill Blues (Deluxe)",
		"https://example.invalid/cover.jpg", 199923};
	QmNeteaseHook::SSnapshot Snapshot{};

	ASSERT_TRUE(QmNeteaseHook::PopulateSnapshotFromLocalTrack(&Snapshot, Track));

	EXPECT_TRUE(QmNeteaseHook::HasMedia(Snapshot));
	EXPECT_TRUE(QmNeteaseHook::HasCover(Snapshot));
	EXPECT_EQ(Snapshot.m_SongId, 502448541u);
	EXPECT_STREQ(Snapshot.m_aSongId, "502448541");
	EXPECT_STREQ(Snapshot.m_aTitle, "What Lovers Do");
	EXPECT_STREQ(Snapshot.m_aArtist, "Maroon 5, SZA");
	EXPECT_STREQ(Snapshot.m_aAlbum, "Red Pill Blues (Deluxe)");
	EXPECT_STREQ(Snapshot.m_aCoverUrl, "https://example.invalid/cover.jpg");
	EXPECT_EQ(Snapshot.m_DurationMs, 199923);
	EXPECT_EQ(Snapshot.m_PositionMs, 0);
	EXPECT_EQ(Snapshot.m_Status & QmNeteaseHook::STATUS_PLAYING, 0u);
}

TEST(QmNeteaseHookMetadata, MediaKeyChangesWhenTheTrackChanges)
{
	const QmNeteaseHook::SLocalTrackMetadata First{"100", "same title", "artist", "album", "", 180000};
	const QmNeteaseHook::SLocalTrackMetadata Second{"200", "same title", "artist", "album", "", 180000};

	EXPECT_NE(QmNeteaseHook::LocalTrackMediaKey(First), QmNeteaseHook::LocalTrackMediaKey(Second));
}

TEST(QmNeteaseHookWaveOutTimeline, ConvertsOnlyReliableWaveOutPositionUnits)
{
	int64_t PositionMs = -1;
	EXPECT_TRUE(QmNeteaseHook::ConvertWaveOutPositionToMs(QmNeteaseHook::WAVE_OUT_TIME_MS, 1234, 0, 0, &PositionMs));
	EXPECT_EQ(PositionMs, 1234);
	EXPECT_TRUE(QmNeteaseHook::ConvertWaveOutPositionToMs(QmNeteaseHook::WAVE_OUT_TIME_SAMPLES, 88200, 44100, 0, &PositionMs));
	EXPECT_EQ(PositionMs, 2000);
	EXPECT_TRUE(QmNeteaseHook::ConvertWaveOutPositionToMs(QmNeteaseHook::WAVE_OUT_TIME_BYTES, 352800, 0, 176400, &PositionMs));
	EXPECT_EQ(PositionMs, 2000);
	EXPECT_FALSE(QmNeteaseHook::ConvertWaveOutPositionToMs(0x0008, 1, 44100, 176400, &PositionMs));
}
