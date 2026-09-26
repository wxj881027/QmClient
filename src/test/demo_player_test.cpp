// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <base/system.h>

#include <engine/demo.h>
#include <engine/shared/demo.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
	// 与 engine/shared/demo.cpp 内部的区块标记保持一致
	constexpr unsigned char DEMO_CHUNKTYPEFLAG_TICKMARKER = 0x80;
	constexpr unsigned char DEMO_CHUNKTICKFLAG_KEYFRAME = 0x40;
	constexpr unsigned char DEMO_CHUNKTICKFLAG_TICK_COMPRESSED = 0x20;
	constexpr int DEMO_FIRST_TICK = 100;

	// 写入的区块序列，用于覆盖扫描阶段的不同失败路径
	enum EDemoChunkLayout
	{
		DEMO_CHUNK_KEYFRAME, // 关键帧时间轴标记 + 普通时间轴标记
		DEMO_CHUNK_NO_KEYFRAME, // 只有普通时间轴标记
		DEMO_CHUNK_BROKEN_TICK, // 首个区块是缺少基准 tick 的压缩时间轴标记
	};

	void AppendBytes(std::vector<unsigned char> &vData, const void *pData, size_t DataSize)
	{
		const unsigned char *pBytes = (const unsigned char *)pData;
		vData.insert(vData.end(), pBytes, pBytes + DataSize);
	}

	void AppendTickMarker(std::vector<unsigned char> &vData, int Tick, bool Keyframe)
	{
		unsigned char aChunk[1 + sizeof(int32_t)];
		aChunk[0] = DEMO_CHUNKTYPEFLAG_TICKMARKER | (Keyframe ? DEMO_CHUNKTICKFLAG_KEYFRAME : 0);
		uint_to_bytes_be(aChunk + 1, Tick);
		AppendBytes(vData, aChunk, sizeof(aChunk));
	}

	// 生成只包含头部、时间轴标记、SHA256 扩展和区块的最小 demo 文件，
	// 用于验证加载阶段对时间轴标记和关键帧的校验。
	void WriteDemoFile(IStorage *pStorage, const char *pFilename, int MarkerTick, int NumMarkers, EDemoChunkLayout Layout)
	{
		CDemoHeader Header;
		mem_zero(&Header, sizeof(Header));
		mem_copy(Header.m_aMarker, gs_aHeaderMarker, sizeof(Header.m_aMarker));
		Header.m_Version = 6;
		str_copy(Header.m_aNetversion, "0.6 626fce9a778df4d4");
		str_copy(Header.m_aMapName, "demo_player_test");
		str_copy(Header.m_aType, "client");
		str_copy(Header.m_aTimestamp, "2026-09-08 00:00:00");
		uint_to_bytes_be(Header.m_aMapSize, 0);
		uint_to_bytes_be(Header.m_aMapCrc, 0);
		uint_to_bytes_be(Header.m_aLength, 0);

		CTimelineMarkers Markers;
		mem_zero(&Markers, sizeof(Markers));
		uint_to_bytes_be(Markers.m_aNumTimelineMarkers, NumMarkers);
		if(NumMarkers > 0)
			uint_to_bytes_be(Markers.m_aTimelineMarkers[0], MarkerTick);

		SHA256_DIGEST Sha256 = {};

		std::vector<unsigned char> vData;
		AppendBytes(vData, &Header, sizeof(Header));
		AppendBytes(vData, &Markers, sizeof(Markers));
		AppendBytes(vData, SHA256_EXTENSION.m_aData, sizeof(SHA256_EXTENSION.m_aData));
		AppendBytes(vData, &Sha256, sizeof(Sha256));
		if(Layout == DEMO_CHUNK_BROKEN_TICK)
		{
			const unsigned char aChunk = DEMO_CHUNKTYPEFLAG_TICKMARKER | DEMO_CHUNKTICKFLAG_TICK_COMPRESSED;
			AppendBytes(vData, &aChunk, sizeof(aChunk));
		}
		else
		{
			AppendTickMarker(vData, DEMO_FIRST_TICK, Layout == DEMO_CHUNK_KEYFRAME);
			AppendTickMarker(vData, DEMO_FIRST_TICK + 1, false);
		}

		IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_TRUE(File);
		ASSERT_EQ(io_write(File, vData.data(), vData.size()), (int)vData.size());
		ASSERT_FALSE(io_close(File));
	}

	class CDemoPlayerTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();

		int LoadDemo(const char *pFilename)
		{
			CDemoPlayer Player(nullptr, nullptr, false);
			const int Result = Player.Load(m_pStorage.get(), nullptr, pFilename, IStorage::TYPE_SAVE);
			Player.Stop();
			return Result;
		}
	};
}

TEST_F(CDemoPlayerTest, LoadsDemoWithMarkerAtFirstTick)
{
	WriteDemoFile(m_pStorage.get(), "valid.demo", DEMO_FIRST_TICK, 1, DEMO_CHUNK_KEYFRAME);
	EXPECT_EQ(LoadDemo("valid.demo"), 0);
}

TEST_F(CDemoPlayerTest, LoadsDemoWithoutMarkers)
{
	WriteDemoFile(m_pStorage.get(), "no_markers.demo", 0, 0, DEMO_CHUNK_KEYFRAME);
	EXPECT_EQ(LoadDemo("no_markers.demo"), 0);
}

TEST_F(CDemoPlayerTest, RejectsMarkerAfterLastTick)
{
	WriteDemoFile(m_pStorage.get(), "marker_after.demo", DEMO_FIRST_TICK + 2, 1, DEMO_CHUNK_KEYFRAME);
	EXPECT_EQ(LoadDemo("marker_after.demo"), -1);
}

TEST_F(CDemoPlayerTest, RejectsMarkerBeforeFirstTick)
{
	WriteDemoFile(m_pStorage.get(), "marker_before.demo", DEMO_FIRST_TICK - 1, 1, DEMO_CHUNK_KEYFRAME);
	EXPECT_EQ(LoadDemo("marker_before.demo"), -1);
}

TEST_F(CDemoPlayerTest, RejectsDemoWithoutKeyframe)
{
	WriteDemoFile(m_pStorage.get(), "no_keyframe.demo", DEMO_FIRST_TICK, 1, DEMO_CHUNK_NO_KEYFRAME);
	EXPECT_EQ(LoadDemo("no_keyframe.demo"), -1);
}

TEST_F(CDemoPlayerTest, RejectsDemoWhenScanFailsBeforeAnyKeyframe)
{
	// 没有可用的关键帧且扫描提前失败时，不能把 demo 当成可播放
	WriteDemoFile(m_pStorage.get(), "broken_tick.demo", 0, 0, DEMO_CHUNK_BROKEN_TICK);
	EXPECT_EQ(LoadDemo("broken_tick.demo"), -1);
}

TEST_F(CDemoPlayerTest, RejectsMarkerOfBrokenDemo)
{
	WriteDemoFile(m_pStorage.get(), "broken_tick_marker.demo", DEMO_FIRST_TICK, 1, DEMO_CHUNK_BROKEN_TICK);
	EXPECT_EQ(LoadDemo("broken_tick_marker.demo"), -1);
}
