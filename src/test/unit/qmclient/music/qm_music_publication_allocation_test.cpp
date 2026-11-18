#include <gtest/gtest.h>
#include <qm/music/qm_music_publication.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <new>
#include <string>

namespace
{
	struct SInputs
	{
		QmMusicHook::SPlayback m_Old;
		QmMusicHook::SPlayback m_Next;

		SInputs()
		{
			m_Old.m_ProcessId = 123;
			m_Old.m_HasSong = true;
			m_Old.m_Playing = true;
			m_Old.m_PositionValid = true;
			m_Old.m_PositionMs = 1000;
			m_Old.m_DurationMs = 180000;
			m_Old.m_MediaId = "qqmusic:11";
			m_Old.m_Title = "old title";
			m_Old.m_Artist = "old artist";
			m_Old.m_Album = "old album";
			m_Old.m_CoverUrl = "old-cover";
			m_Old.m_LyricType = "lrc";
			m_Old.m_LyricContent = "[00:01]old";
			m_Old.m_TranslationLrc = "[00:01]old-t";
			m_Old.m_Error = "old error";
			m_Next = m_Old;
			m_Next.m_ProcessId = 456;
			m_Next.m_Playing = false;
			m_Next.m_Loading = true;
			m_Next.m_PositionValid = false;
			m_Next.m_PositionMs = 5000;
			m_Next.m_DurationMs = 240000;
			m_Next.m_MediaId = "qqmusic:" + std::string(18, '7');
			m_Next.m_Title = std::string(96, 't');
			m_Next.m_Artist = std::string(80, 'a');
			m_Next.m_Album = std::string(64, 'b');
			m_Next.m_CoverUrl = "https://covers.example/" + std::string(80, 'c');
			m_Next.m_LyricType = "qrc";
			m_Next.m_LyricContent = "[1000,1000]" + std::string(768, 'w');
			m_Next.m_TranslationLrc = "[00:01]" + std::string(256, 'x');
			m_Next.m_Error = std::string(96, 'e');
		}
	};

	struct SObserved
	{
		uint64_t m_Generation;
		bool m_HasLyrics;
		std::string m_Json;
		QmSodaHook::SSnapshot m_Snapshot;
	};

	SObserved Observe(const QmMusicHook::CPublication &Publication)
	{
		return {Publication.Generation(), Publication.HasLyrics(), Publication.LyricJson(), Publication.Snapshot(1234, "lyrics.json")};
	}

	void ExpectSame(const QmMusicHook::CPublication &Publication, const SObserved &Expected)
	{
		const auto Actual = Observe(Publication);
		EXPECT_EQ(Actual.m_Generation, Expected.m_Generation);
		EXPECT_EQ(Actual.m_HasLyrics, Expected.m_HasLyrics);
		EXPECT_EQ(Actual.m_Json, Expected.m_Json);
		const auto &A = Actual.m_Snapshot;
		const auto &E = Expected.m_Snapshot;
		EXPECT_EQ(A.m_Magic, E.m_Magic);
		EXPECT_EQ(A.m_SchemaVersion, E.m_SchemaVersion);
		EXPECT_EQ(A.m_SnapshotSize, E.m_SnapshotSize);
		EXPECT_EQ(A.m_Sequence, E.m_Sequence);
		EXPECT_EQ(A.m_SodaMusicPid, E.m_SodaMusicPid);
		EXPECT_EQ(A.m_Flags, E.m_Flags);
		EXPECT_EQ(A.m_Generation, E.m_Generation);
		EXPECT_EQ(A.m_PositionMs, E.m_PositionMs);
		EXPECT_EQ(A.m_DurationMs, E.m_DurationMs);
		EXPECT_EQ(A.m_UpdatedAtTick, E.m_UpdatedAtTick);
		EXPECT_STREQ(A.m_aMediaId, E.m_aMediaId);
		EXPECT_STREQ(A.m_aTitle, E.m_aTitle);
		EXPECT_STREQ(A.m_aArtist, E.m_aArtist);
		EXPECT_STREQ(A.m_aAlbum, E.m_aAlbum);
		EXPECT_STREQ(A.m_aCoverUrl, E.m_aCoverUrl);
		EXPECT_STREQ(A.m_aLyricFilePath, E.m_aLyricFilePath);
		EXPECT_STREQ(A.m_aError, E.m_aError);
		EXPECT_EQ(A.m_Checksum, E.m_Checksum);
		EXPECT_EQ(A.m_Reserved, E.m_Reserved);
	}

	struct SAttempt
	{
		bool m_Changed = false;
		bool m_Threw = false;
		bool m_Triggered = false;
		size_t m_Allocations = 0;
	};

	SAttempt UpdateWithFailure(QmMusicHook::CPublication &Publication, const QmMusicHook::SPlayback &Playback, size_t Budget)
	{
		SAttempt Result;
		CPredictionAllocationFailure Failure(Budget);
		try
		{
			Result.m_Changed = Publication.Update(Playback);
		}
		catch(const std::bad_alloc &)
		{
			Result.m_Threw = true;
		}
		Result.m_Triggered = Failure.Triggered();
		Result.m_Allocations = Failure.Attempts();
		return Result;
	}
}

TEST(QmMusicPublicationAllocation, InitialAndReplacementFailuresKeepTheWholePublicationUntilRetry)
{
	const SInputs Input;
	for(const bool HasOldSong : {false, true})
	{
		SCOPED_TRACE(HasOldSong);
		QmMusicHook::CPublication Baseline(41);
		if(HasOldSong)
			ASSERT_TRUE(Baseline.Update(Input.m_Old));
		const auto OldGeneration = Baseline.Generation();
		const auto Trace = UpdateWithFailure(Baseline, Input.m_Next, std::numeric_limits<size_t>::max());
		ASSERT_TRUE(Trace.m_Changed);
		ASSERT_FALSE(Trace.m_Threw);
		ASSERT_FALSE(Trace.m_Triggered);
		ASSERT_GT(Trace.m_Allocations, 0u);
		ASSERT_LE(Trace.m_Allocations, 32u);
		EXPECT_EQ(Baseline.Generation(), OldGeneration + 1);
		const auto Expected = Observe(Baseline);
		for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
		{
			SCOPED_TRACE(Budget);
			QmMusicHook::CPublication State(41);
			if(HasOldSong)
				ASSERT_TRUE(State.Update(Input.m_Old));
			const auto Before = Observe(State);
			const auto Failed = UpdateWithFailure(State, Input.m_Next, Budget);
			EXPECT_TRUE(Failed.m_Triggered);
			EXPECT_TRUE(Failed.m_Threw);
			EXPECT_FALSE(Failed.m_Changed);
			ExpectSame(State, Before);
			ASSERT_TRUE(State.Update(Input.m_Next));
			ExpectSame(State, Expected);
		}
	}
}

TEST(QmMusicPublicationAllocation, AuxiliaryChangesAreAtomicAndStableOrProgressFramesDoNotAllocate)
{
	const SInputs Input;
	auto Previous = Input.m_Old;
	Previous.m_LyricType = Input.m_Next.m_LyricType;
	Previous.m_LyricContent = Input.m_Next.m_LyricContent;
	Previous.m_TranslationLrc = Input.m_Next.m_TranslationLrc;
	auto Auxiliary = Previous;
	Auxiliary.m_CoverUrl = Input.m_Next.m_CoverUrl;
	Auxiliary.m_Error = Input.m_Next.m_Error;
	Auxiliary.m_Playing = false;
	Auxiliary.m_Loading = true;
	Auxiliary.m_PositionValid = false;
	Auxiliary.m_PositionMs = 5000;
	Auxiliary.m_DurationMs = 240000;
	QmMusicHook::CPublication Baseline(41);
	ASSERT_TRUE(Baseline.Update(Previous));
	const auto OldGeneration = Baseline.Generation();
	const auto Trace = UpdateWithFailure(Baseline, Auxiliary, std::numeric_limits<size_t>::max());
	ASSERT_FALSE(Trace.m_Changed);
	ASSERT_FALSE(Trace.m_Threw);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 32u);
	EXPECT_EQ(Baseline.Generation(), OldGeneration);
	const auto Expected = Observe(Baseline);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		QmMusicHook::CPublication State(41);
		ASSERT_TRUE(State.Update(Previous));
		const auto Before = Observe(State);
		const auto Failed = UpdateWithFailure(State, Auxiliary, Budget);
		EXPECT_TRUE(Failed.m_Triggered);
		EXPECT_TRUE(Failed.m_Threw);
		ExpectSame(State, Before);
		EXPECT_FALSE(State.Update(Auxiliary));
		ExpectSame(State, Expected);
	}
	const auto Stable = UpdateWithFailure(Baseline, Auxiliary, 0);
	EXPECT_FALSE(Stable.m_Changed);
	EXPECT_FALSE(Stable.m_Threw);
	EXPECT_FALSE(Stable.m_Triggered);
	EXPECT_EQ(Stable.m_Allocations, 0u);
	ExpectSame(Baseline, Expected);
	Auxiliary.m_PositionMs = 6000;
	Auxiliary.m_Playing = true;
	Auxiliary.m_PositionValid = true;
	const auto Progress = UpdateWithFailure(Baseline, Auxiliary, 0);
	EXPECT_FALSE(Progress.m_Changed);
	EXPECT_FALSE(Progress.m_Threw);
	EXPECT_FALSE(Progress.m_Triggered);
	EXPECT_EQ(Progress.m_Allocations, 0u);
	const auto Snapshot = Baseline.Snapshot(1234, "lyrics.json");
	EXPECT_EQ(Snapshot.m_Generation, OldGeneration);
	EXPECT_EQ(Snapshot.m_PositionMs, 6000);
	EXPECT_NE(Snapshot.m_Flags & QmSodaHook::FLAG_PLAYING, 0u);
	EXPECT_NE(Snapshot.m_Flags & QmSodaHook::FLAG_POSITION_VALID, 0u);
}

TEST(QmMusicPublicationAllocation, EmptyStalePublicationClearsLyricsWithoutHeapPreparationAndCanRecover)
{
	const SInputs Input;
	QmMusicHook::CPublication State(41);
	ASSERT_TRUE(State.Update(Input.m_Next));
	const auto Before = Observe(State);
	QmMusicHook::SPlayback Stale;
	Stale.m_Loading = true;
	const auto Cleared = UpdateWithFailure(State, Stale, 0);
	EXPECT_TRUE(Cleared.m_Changed);
	EXPECT_FALSE(Cleared.m_Threw);
	EXPECT_FALSE(Cleared.m_Triggered);
	EXPECT_EQ(Cleared.m_Allocations, 0u);
	EXPECT_EQ(State.Generation(), Before.m_Generation + 1);
	EXPECT_FALSE(State.HasLyrics());
	const auto Snapshot = State.Snapshot(1234, "");
	EXPECT_EQ(Snapshot.m_Flags, QmSodaHook::FLAG_LOADING);
	EXPECT_EQ(Snapshot.m_PositionMs, 0);
	EXPECT_EQ(Snapshot.m_DurationMs, 0);
	EXPECT_STREQ(Snapshot.m_aMediaId, "");
	EXPECT_STREQ(Snapshot.m_aTitle, "");
	EXPECT_STREQ(Snapshot.m_aArtist, "");
	EXPECT_STREQ(Snapshot.m_aAlbum, "");
	EXPECT_STREQ(Snapshot.m_aCoverUrl, "");
	EXPECT_STREQ(Snapshot.m_aLyricFilePath, "");
	EXPECT_STREQ(Snapshot.m_aError, "");
	const auto Repeated = UpdateWithFailure(State, Stale, 0);
	EXPECT_FALSE(Repeated.m_Changed);
	EXPECT_FALSE(Repeated.m_Threw);
	EXPECT_FALSE(Repeated.m_Triggered);
	EXPECT_EQ(Repeated.m_Allocations, 0u);
	EXPECT_EQ(State.Generation(), Before.m_Generation + 1);
	ASSERT_TRUE(State.Update(Input.m_Next));
	EXPECT_EQ(State.Generation(), Before.m_Generation + 2);
	EXPECT_EQ(State.LyricJson(), Before.m_Json);
}
