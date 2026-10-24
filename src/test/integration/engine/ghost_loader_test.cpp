#include <base/system.h>

#include <engine/client/ghost.h>
#include <engine/kernel.h>
#include <engine/shared/compression.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/ghost_path.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <vector>

namespace
{
	class GhostLoaderIntegration : public ::testing::Test
	{
	protected:
		struct SChunk
		{
			unsigned char m_Type;
			std::vector<uint32_t> m_vWords;
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		CGhostLoader m_Loader;
		CGhostRecorder m_Recorder;
		static constexpr const char *FILENAME = "input.gho";
		const SHA256_DIGEST m_MapHash{};

		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NE(m_pKernel, nullptr);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGhostLoader>(&m_Loader, false);
			m_Loader.Init();
			m_pKernel->RegisterInterface<IGhostRecorder>(&m_Recorder, false);
			m_Recorder.Init();
			CNetBase::Init();
		}
		void TearDown() override
		{
			m_Loader.Close();
			m_Recorder.Stop(0, 0);
		}

		void Write(unsigned char Version, const std::vector<SChunk> &vChunks)
		{
			std::unique_ptr<void, decltype(&io_close)> File(m_pStorage->OpenFile(FILENAME, IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
			ASSERT_NE(File, nullptr);
			CGhostHeader Header{};
			const unsigned char aMarker[] = {'T', 'W', 'G', 'H', 'O', 'S', 'T', 0};
			mem_copy(Header.m_aMarker, aMarker, sizeof(aMarker));
			Header.m_Version = Version;
			str_copy(Header.m_aOwner, "test");
			str_copy(Header.m_aMap, "test-map");
			uint_to_bytes_be(Header.m_aNumTicks, 2);
			uint_to_bytes_be(Header.m_aTime, 1000);
			const size_t HeaderSize = Version < 6 ? sizeof(Header) - sizeof(SHA256_DIGEST) : sizeof(Header);
			ASSERT_EQ(io_write(File.get(), &Header, HeaderSize), HeaderSize);
			for(const auto &Chunk : vChunks)
			{
				std::array<unsigned char, MAX_CHUNK_SIZE> aPacked{}, aCompressed{};
				const int Packed = CVariableInt::Compress(Chunk.m_vWords.data(), Chunk.m_vWords.size() * sizeof(uint32_t), aPacked.data(), aPacked.size());
				ASSERT_GT(Packed, 0);
				const int Size = CNetBase::Compress(aPacked.data(), Packed, aCompressed.data(), aCompressed.size());
				ASSERT_GT(Size, 0);
				const unsigned char aChunkHeader[] = {Chunk.m_Type, 1, static_cast<unsigned char>(Size >> 8), static_cast<unsigned char>(Size)};
				ASSERT_EQ(io_write(File.get(), aChunkHeader, sizeof(aChunkHeader)), sizeof(aChunkHeader));
				ASSERT_EQ(io_write(File.get(), aCompressed.data(), Size), static_cast<unsigned>(Size));
			}
			ASSERT_EQ(io_close(File.release()), 0);
		}
		void Open() { ASSERT_TRUE(m_Loader.Load(FILENAME, "test-map", m_MapHash, 0)); }
	};
}

TEST_F(GhostLoaderIntegration, VersionFourContinuesTheSameDeltaBaseAcrossChunks)
{
	ASSERT_NO_FATAL_FAILURE(Write(4, {{2, {7, 11}}, {2, {3, 5}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	std::array<uint32_t, 2> aValue{};
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_EQ(Type, 2);
	ASSERT_TRUE(m_Loader.ReadData(Type, aValue.data(), sizeof(aValue)));
	EXPECT_EQ(aValue, (std::array<uint32_t, 2>{7, 11}));
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(m_Loader.ReadData(Type, aValue.data(), sizeof(aValue)));
	EXPECT_EQ(aValue, (std::array<uint32_t, 2>{10, 16}));
	EXPECT_FALSE(m_Loader.ReadNextType(&Type));
}

TEST_F(GhostLoaderIntegration, VersionFourTypeMismatchReturnsFailureWithoutWritingOutput)
{
	ASSERT_NO_FATAL_FAILURE(Write(4, {{0, {7}}, {3, {11}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	uint32_t Value = 0;
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(m_Loader.ReadData(Type, &Value, sizeof(Value)));
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_EQ(Type, 3);
	Value = 123;
	EXPECT_FALSE(m_Loader.ReadData(Type, &Value, sizeof(Value)));
	EXPECT_EQ(Value, 123u);
	EXPECT_FALSE(m_Loader.ReadData(Type, &Value, sizeof(Value)));
	EXPECT_EQ(Value, 123u);
}

TEST_F(GhostLoaderIntegration, VersionFourSizeMismatchCanBeClosedAndReopened)
{
	ASSERT_NO_FATAL_FAILURE(Write(4, {{2, {7}}, {2, {3, 5}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	uint32_t First = 0;
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(m_Loader.ReadData(Type, &First, sizeof(First)));
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	std::array<uint32_t, 2> aValue{123, 456};
	EXPECT_FALSE(m_Loader.ReadData(Type, aValue.data(), sizeof(aValue)));
	EXPECT_EQ(aValue, (std::array<uint32_t, 2>{123, 456}));
	m_Loader.Close();
	ASSERT_NO_FATAL_FAILURE(Write(4, {{2, {9, 10}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(m_Loader.ReadData(Type, aValue.data(), sizeof(aValue)));
	EXPECT_EQ(aValue, (std::array<uint32_t, 2>{9, 10}));
}

TEST_F(GhostLoaderIntegration, NewerVersionsResetTheBaseWhenChangingChunkType)
{
	for(unsigned char Version : {5, 6})
	{
		SCOPED_TRACE(static_cast<int>(Version));
		ASSERT_NO_FATAL_FAILURE(Write(Version, {{0, {7}}, {3, {11, 12}}}));
		ASSERT_NO_FATAL_FAILURE(Open());
		int Type = -1;
		uint32_t First = 0;
		ASSERT_TRUE(m_Loader.ReadNextType(&Type));
		ASSERT_TRUE(m_Loader.ReadData(Type, &First, sizeof(First)));
		ASSERT_TRUE(m_Loader.ReadNextType(&Type));
		ASSERT_EQ(Type, 3);
		std::array<uint32_t, 2> aValue{};
		ASSERT_TRUE(m_Loader.ReadData(Type, aValue.data(), sizeof(aValue)));
		EXPECT_EQ(aValue, (std::array<uint32_t, 2>{11, 12}));
		m_Loader.Close();
	}
}

TEST_F(GhostLoaderIntegration, FailedRecordingStartIgnoresWritesAndAllowsAValidRetry)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("blocked.gho.tmp", IStorage::TYPE_SAVE));
	ASSERT_EQ(m_Recorder.Start("blocked.gho", "test-map", m_MapHash, "test"), -1);
	const uint32_t Value = 11;
	m_Recorder.WriteData(3, &Value, sizeof(Value));
	EXPECT_FALSE(m_Recorder.IsRecording());
	m_Recorder.Stop(1, 1000);
	ASSERT_EQ(m_Recorder.Start(FILENAME, "test-map", m_MapHash, "test"), 0);
	m_Recorder.WriteData(3, &Value, sizeof(Value));
	m_Recorder.Stop(1, 1000);
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_EQ(Type, 3);
	uint32_t Restored = 0;
	ASSERT_TRUE(m_Loader.ReadData(Type, &Restored, sizeof(Restored)));
	EXPECT_EQ(Restored, Value);
}

TEST_F(GhostLoaderIntegration, PathAllocationFailureDoesNotPublishTheReadRecordAndTheFileCanReopen)
{
	class CHeap : public qm_ghost_path::CAllocation
	{
	public:
		bool m_Fail = true;
		void *Allocate(size_t Count, size_t Size) override { return m_Fail ? nullptr : CAllocation::Allocate(Count, Size); }
	} Heap;
	qm_ghost_path::CChunkedPath<uint32_t> Path(1, &Heap);
	ASSERT_NO_FATAL_FAILURE(Write(6, {{2, {7}}, {2, {11}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	const auto Read = [&](uint32_t &Value) { return m_Loader.ReadData(Type, &Value, sizeof(Value)); };
	EXPECT_FALSE(Path.ReadAndAdd(Read));
	EXPECT_EQ(Path.Size(), 0);
	EXPECT_EQ(Path.Get(0), nullptr);
	m_Loader.Close();
	Heap.m_Fail = false;
	ASSERT_NO_FATAL_FAILURE(Open());
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(Path.ReadAndAdd(Read));
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(*Path.Get(0), 7u);
}

TEST_F(GhostLoaderIntegration, ReadingAnOlderPrefixIntoThePathKeepsTheNewFieldZero)
{
	struct SRecord
	{
		uint32_t m_Value;
		uint32_t m_NewField;
	};
	qm_ghost_path::CChunkedPath<SRecord> Path;
	ASSERT_NO_FATAL_FAILURE(Write(4, {{2, {7}}}));
	ASSERT_NO_FATAL_FAILURE(Open());
	int Type = -1;
	ASSERT_TRUE(m_Loader.ReadNextType(&Type));
	ASSERT_TRUE(Path.ReadAndAdd([&](SRecord &Value) { return m_Loader.ReadData(Type, &Value, sizeof(Value.m_Value)); }));
	ASSERT_NE(Path.Get(0), nullptr);
	EXPECT_EQ(Path.Get(0)->m_Value, 7u);
	EXPECT_EQ(Path.Get(0)->m_NewField, 0u);
}
