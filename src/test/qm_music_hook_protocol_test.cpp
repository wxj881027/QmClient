#include "test.h"

#include <gtest/gtest.h>
#include <qm-music-hook/qm_kugou_protocol.h>
#include <qm-music-hook/qm_qqmusic_protocol.h>

#include <string>

using namespace QmMusicHook;
using namespace QmMusicHook::QQMusic;

TEST(QmMusicHookProtocol, KugouRejectsMalformedPlaybackJson)
{
	SPlayback State;
	// 外部输入可能被截断或根本不是 JSON：必须返回 false 而不是崩溃/填入垃圾。
	EXPECT_FALSE(ParseKugouPlayback("", &State));
	EXPECT_FALSE(ParseKugouPlayback("{", &State));
	EXPECT_FALSE(ParseKugouPlayback("not json at all", &State));
	EXPECT_FALSE(ParseKugouPlayback("[]", &State));
	EXPECT_FALSE(State.m_HasSong);
}

TEST(QmMusicHookProtocol, KugouRejectsMalformedLyricResponses)
{
	std::string Text = "untouched";
	EXPECT_FALSE(DecodeKugouLyricResponse("", false, &Text));
	EXPECT_FALSE(DecodeKugouLyricResponse("{", true, &Text));
	EXPECT_FALSE(DecodeKugouLyricResponse("garbage", true, &Text));

	std::string Id = "untouched";
	std::string AccessKey = "untouched";
	EXPECT_FALSE(SelectKugouLyricCandidate("", &Id, &AccessKey));
	EXPECT_FALSE(SelectKugouLyricCandidate("{", &Id, &AccessKey));
}

TEST(QmMusicHookProtocol, KugouUnknownBuildIsNeverClassifiedAsPatched)
{
	// 指纹来自特定酷狗版本；读不到内容（未知版本/未运行）时必须判为 UNSUPPORTED，
	// 否则会向未知版本的进程写入补丁。
	const TKugouReadBytes ReadNothing = [](uint64_t, void *, size_t) { return false; };
	EXPECT_EQ(ClassifyKugouPatch(ReadNothing), EKugouPatchState::UNSUPPORTED);

	// 读得到但内容是全零，同样不匹配任何已知指纹。
	const TKugouReadBytes ReadZeros = [](uint64_t Offset, void *pBuffer, size_t Size) {
		(void)Offset;
		memset(pBuffer, 0, Size);
		return true;
	};
	EXPECT_EQ(ClassifyKugouPatch(ReadZeros), EKugouPatchState::UNSUPPORTED);
}

TEST(QmMusicHookProtocol, KugouPatchTableIsSelfConsistent)
{
	// 补丁表是「同长度替换」：若长度不等，写入会改变 DLL 布局。
	const std::vector<SKugouPatch> &vPatches = KugouPatches();
	ASSERT_FALSE(vPatches.empty());
	for(const SKugouPatch &Patch : vPatches)
	{
		EXPECT_EQ(Patch.m_Original.size(), Patch.m_Patched.size());
		EXPECT_FALSE(Patch.m_Original.empty());
	}
}

TEST(QmMusicHookProtocol, QQMusicUnknownVersionHasNoOffsets)
{
	// 未登记的版本必须返回 nullptr，调用方据此拒绝解析。
	EXPECT_EQ(FindOffsets(0, 0), nullptr);
	EXPECT_EQ(FindOffsets(-1, -1), nullptr);
}

TEST(QmMusicHookProtocol, QQMusicRejectsMalformedLyricsResponse)
{
	SLyrics Lyrics;
	EXPECT_FALSE(ParseLyricsResponse("", 0, false, Lyrics));
	EXPECT_FALSE(ParseLyricsResponse("{", 0, false, Lyrics));
	EXPECT_FALSE(ParseLyricsResponse("garbage", 12345, true, Lyrics));
}

TEST(QmMusicHookProtocol, QQMusicRejectsUndecodableSsoLayout)
{
	SSsoLayout Layout;
	const unsigned char aGarbage[] = {0x00, 0x01, 0x02, 0x03};
	// 过短/无结构的数据不得被当作有效布局。
	EXPECT_FALSE(DecodeSsoLayout(aGarbage, sizeof(aGarbage), Layout));
	EXPECT_FALSE(DecodeSsoLayout(nullptr, 0, Layout));
}
