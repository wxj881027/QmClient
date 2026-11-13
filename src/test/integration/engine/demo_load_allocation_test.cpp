#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <algorithm>
#include <chrono>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
	class CObservedKeyFrameMemory : public CDemoPlayer::CKeyFrameMemory
	{
	public:
		enum class EFailure
		{
			NONE,
			NULL_RESULT,
			ALLOCATION,
			LENGTH,
			UNKNOWN,
		};
		EFailure m_Failure = EFailure::NONE;
		bool m_OnlyWithLiveMemory = false;
		bool m_FailedWithLiveMemory = false;
		bool m_FileWasOpenOnFailure = false;
		bool m_ClosedBeforeRelease = true;
		bool m_Triggered = false;
		size_t m_Requests = 0;
		size_t m_FirstRequestBytes = 0;
		size_t m_LiveBytes = 0;
		CDemoPlayer *m_pOwner = nullptr;

		void *Allocate(size_t Bytes) override
		{
			++m_Requests;
			if(m_FirstRequestBytes == 0)
				m_FirstRequestBytes = Bytes;
			if(m_Failure != EFailure::NONE && (!m_OnlyWithLiveMemory || m_LiveBytes != 0))
			{
				m_Triggered = true;
				m_FailedWithLiveMemory = m_LiveBytes != 0;
				m_FileWasOpenOnFailure = m_pOwner && m_pOwner->IsPlaying();
				switch(m_Failure)
				{
				case EFailure::NULL_RESULT: return nullptr;
				case EFailure::ALLOCATION: throw std::bad_alloc();
				case EFailure::LENGTH: throw std::length_error("index capacity");
				case EFailure::UNKNOWN: throw 7;
				case EFailure::NONE: break;
				}
			}
			void *pMemory = CKeyFrameMemory::Allocate(Bytes);
			m_LiveBytes += Bytes;
			return pMemory;
		}
		void Free(void *pMemory, size_t Bytes) noexcept override
		{
			if(m_Triggered && m_pOwner && m_pOwner->IsPlaying())
				m_ClosedBeforeRelease = false;
			m_LiveBytes -= Bytes;
			CKeyFrameMemory::Free(pMemory, Bytes);
		}
		void Recover()
		{
			m_Failure = EFailure::NONE;
			m_Triggered = false;
		}
	};

	class DemoLoadAllocationIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CObservedKeyFrameMemory m_Memory;
		CDemoPlayer m_Player{&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, nullptr, CDemoPlayer::ETimeMode::UNCACHED, &m_Memory};
		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
			m_Memory.m_pOwner = &m_Player;
			ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
		}
		void TearDown() override { m_Player.Stop(); }
		int Load()
		{
			return m_Player.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false);
		}
		void ExpectFailed(const char *pReason)
		{
			EXPECT_FALSE(m_Player.IsPlaying());
			EXPECT_STREQ(m_Player.Filename(), "");
			EXPECT_STREQ(m_Player.ErrorMessage(), pReason);
			EXPECT_EQ(m_Memory.m_LiveBytes, 0U);
			EXPECT_TRUE(m_Memory.m_FileWasOpenOnFailure);
			EXPECT_TRUE(m_Memory.m_ClosedBeforeRelease);
			EXPECT_FALSE(m_Player.Play());
			EXPECT_EQ(m_Player.GetMapData(m_State.m_pStorage.get()), nullptr);
		}
		void RecordTicks(const char *pName, int Count)
		{
			unsigned char aMap[] = {'m', 'a', 'p', 'b', 'y', 't', 'e', 's'};
			ASSERT_EQ(m_State.m_Recorder.Start(m_State.m_pStorage.get(), nullptr, pName, "test", "map", SHA256_DIGEST{}, 0, "client", sizeof(aMap), aMap, nullptr, nullptr, nullptr), 0);
			const CSnapshot *pSnapshot = CSnapshot::EmptySnapshot();
			const int Size = static_cast<int>(pSnapshot->AsSlice().size() * sizeof(int32_t));
			for(int i = 0; i < Count; ++i)
				m_State.m_Recorder.RecordSnapshot(300 + i, pSnapshot, Size, true);
			ASSERT_EQ(m_State.m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
		}
	};
}

TEST_F(DemoLoadAllocationIntegration, AllocationExceptionClosesTheLoadedFile)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	EXPECT_EQ(Load(), -1);
	ExpectFailed("Not enough memory to load demo");
}

TEST_F(DemoLoadAllocationIntegration, NullAllocationIsConvertedToAClosedLoadFailure)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::NULL_RESULT;
	EXPECT_EQ(Load(), -1);
	ExpectFailed("Not enough memory to load demo");
}

TEST_F(DemoLoadAllocationIntegration, LengthExceptionPreservesACapacityFailureReason)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::LENGTH;
	EXPECT_EQ(Load(), -1);
	ExpectFailed("Demo keyframe index is too large");
}

TEST_F(DemoLoadAllocationIntegration, UnknownAllocationExceptionReturnsFailureWithoutEscaping)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::UNKNOWN;
	EXPECT_EQ(Load(), -1);
	ExpectFailed("Error loading demo");
}

TEST_F(DemoLoadAllocationIntegration, GrowthFailureReleasesThePartiallyPreparedIndex)
{
	ASSERT_EQ(Load(), 0);
	m_Player.Stop();
	const size_t Bytes = std::max(m_Memory.m_LiveBytes, m_Memory.m_FirstRequestBytes);
	ASSERT_LT(Bytes, 4096U);
	ASSERT_NO_FATAL_FAILURE(RecordTicks("source.demo", static_cast<int>(Bytes) + 1));
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	m_Memory.m_OnlyWithLiveMemory = true;
	EXPECT_EQ(Load(), -1);
	EXPECT_TRUE(m_Memory.m_FailedWithLiveMemory);
	ExpectFailed("Not enough memory to load demo");
}

TEST_F(DemoLoadAllocationIntegration, FailedLoadCanRetryOnTheSamePlayer)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	ASSERT_EQ(Load(), -1);
	m_Memory.Recover();
	ASSERT_EQ(Load(), 0);
	EXPECT_TRUE(m_Player.Play());
	EXPECT_EQ(m_Player.ErrorMessage()[0], '\0');
}

TEST_F(DemoLoadAllocationIntegration, RepeatedFailureDoesNotRetainIndexAllocations)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	EXPECT_EQ(Load(), -1);
	EXPECT_EQ(Load(), -1);
	ExpectFailed("Not enough memory to load demo");
}

TEST_F(DemoLoadAllocationIntegration, FailureOnOnePlayerDoesNotAffectAnotherPlayersMemory)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	ASSERT_EQ(Load(), -1);
	CObservedKeyFrameMemory OtherMemory;
	CDemoPlayer Other(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, nullptr, CDemoPlayer::ETimeMode::UNCACHED, &OtherMemory);
	EXPECT_EQ(Other.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false), 0);
	EXPECT_TRUE(Other.Play());
	EXPECT_EQ(m_Memory.m_LiveBytes, 0U);
	Other.Stop();
}

TEST_F(DemoLoadAllocationIntegration, NormalInjectedMemoryKeepsTheRecordedBoundsAndPlayback)
{
	ASSERT_EQ(Load(), 0);
	EXPECT_GT(m_Memory.m_Requests, 0U);
	EXPECT_GT(m_Memory.m_LiveBytes, 0U);
	EXPECT_EQ(m_Player.BaseInfo()->m_FirstTick, 100);
	EXPECT_EQ(m_Player.BaseInfo()->m_LastTick, 200);
	EXPECT_TRUE(m_Player.Play());
}

TEST_F(DemoLoadAllocationIntegration, DestructionReleasesIndexThroughTheBorrowedMemoryOwner)
{
	CObservedKeyFrameMemory Memory;
	{
		CDemoPlayer Player(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, nullptr, CDemoPlayer::ETimeMode::UNCACHED, &Memory);
		EXPECT_EQ(Player.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false), 0);
		Player.Stop();
	}
	EXPECT_EQ(Memory.m_LiveBytes, 0U);
}

TEST_F(DemoLoadAllocationIntegration, IncrementalIndexGrowthFailureClosesPlaybackWithinTheUpdate)
{
	ASSERT_EQ(Load(), 0);
	ASSERT_TRUE(m_Player.Play());
	ASSERT_LT(m_Memory.m_LiveBytes, 4096U);
	ASSERT_NO_FATAL_FAILURE(RecordTicks("append.demo", static_cast<int>(m_Memory.m_LiveBytes) + 1));
	const auto Bytes = m_State.Read("append.demo");
	const size_t Offset = sizeof(CDemoHeader) + sizeof(CTimelineMarkers) + sizeof(CUuid) + sizeof(SHA256_DIGEST) + 8;
	ASSERT_GT(Bytes.size(), Offset);
	IOHANDLE File = m_State.m_pStorage->OpenFile("source.demo", IOFLAG_APPEND, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, Bytes.data() + Offset, static_cast<unsigned>(Bytes.size() - Offset)), Bytes.size() - Offset);
	EXPECT_EQ(io_close(File), 0);
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(m_Player.IsPlaying() && std::chrono::steady_clock::now() < Deadline)
	{
		m_Player.Update(false);
		if(m_Player.IsPlaying())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ExpectFailed("Not enough memory to load demo");
}

TEST_F(DemoLoadAllocationIntegration, MetadataOnlyReadDoesNotAllocateAPlaybackIndex)
{
	m_Memory.m_Failure = CObservedKeyFrameMemory::EFailure::ALLOCATION;
	CDemoHeader Header;
	CTimelineMarkers Markers;
	CMapInfo Map;
	ASSERT_TRUE(m_Player.GetDemoInfo(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, &Header, &Markers, &Map));
	EXPECT_EQ(Map.m_Size, 8U);
	EXPECT_EQ(m_Memory.m_Requests, 0U);
	EXPECT_FALSE(m_Player.IsPlaying());
}

TEST_F(DemoLoadAllocationIntegration, MetadataReadTransfersTheFileOnlyOnSuccessfulRequest)
{
	CDemoHeader Header;
	CTimelineMarkers Markers;
	CMapInfo Map;
	IOHANDLE File = nullptr;
	ASSERT_TRUE(m_Player.GetDemoInfo(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, &Header, &Markers, &Map, &File));
	ASSERT_NE(File, nullptr);
	std::unique_ptr<void, decltype(&io_close)> pFile(File, &io_close);
	char aMap[8];
	EXPECT_EQ(io_read(pFile.get(), aMap, sizeof(aMap)), sizeof(aMap));
	EXPECT_EQ(std::string(aMap, sizeof(aMap)), "mapbytes");
	EXPECT_FALSE(m_Player.IsPlaying());
}

TEST_F(DemoLoadAllocationIntegration, InvalidMetadataDoesNotReturnAnOpenFile)
{
	m_State.Write("source.demo", "broken");
	CDemoHeader Header;
	CTimelineMarkers Markers;
	CMapInfo Map;
	IOHANDLE File = nullptr;
	char aError[256] = "";
	EXPECT_FALSE(m_Player.GetDemoInfo(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, &Header, &Markers, &Map, &File, aError, sizeof(aError)));
	EXPECT_EQ(File, nullptr);
	EXPECT_NE(aError[0], '\0');
}
