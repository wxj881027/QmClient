#include <game/client/components/qmclient/pose_file.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
	// 按 MomentCap SavePoses 的布局构造一条记录。字段偏移与生产解析器同源，
	// 但这里是从格式规格独立写出的，不是把解析器实现复制一份：
	// 若解析器偏移写错，测试会读到别的字段值而失败。
	void WriteInt32(std::vector<unsigned char> &vData, size_t Offset, int32_t Value)
	{
		const uint32_t Raw = (uint32_t)Value;
		vData[Offset + 0] = (unsigned char)(Raw & 0xff);
		vData[Offset + 1] = (unsigned char)((Raw >> 8) & 0xff);
		vData[Offset + 2] = (unsigned char)((Raw >> 16) & 0xff);
		vData[Offset + 3] = (unsigned char)((Raw >> 24) & 0xff);
	}

	// 把字符串打包成「每字节 + 128」的 4 字节一组，未用完的补 0（也就是 '\0'-128）。
	void WritePackedString(std::vector<unsigned char> &vData, size_t Offset, size_t NumInts, const char *pText)
	{
		int32_t aInts[8] = {};
		size_t Index = 0;
		for(size_t i = 0; i < NumInts; ++i)
		{
			uint32_t Packed = 0;
			for(int Shift = 24; Shift >= 0; Shift -= 8)
			{
				const int Value = pText[Index] != '\0' ? (unsigned char)pText[Index++] : 0;
				Packed |= ((uint32_t)(Value + 128) & 0xff) << Shift;
			}
			aInts[i] = (int32_t)Packed;
		}
		for(size_t i = 0; i < NumInts; ++i)
			WriteInt32(vData, Offset + i * 4, aInts[i]);
	}

	size_t RecordOffset(size_t Index)
	{
		return sizeof(int64_t) + Index * qm_pose::RECORD_SIZE;
	}

	struct SPoseSpec
	{
		const char *m_pName = "nameless";
		const char *m_pClan = "";
		const char *m_pSkin = "default";
		int m_X = 100;
		int m_Y = 200;
		int m_Angle = 0;
		int m_HookState = 0;
		int m_HookX = 0;
		int m_HookY = 0;
		int m_Emote = 0;
		int m_Weapon = 0;
		bool m_CustomColor = false;
		int m_ColorBody = 0;
		int m_ColorFeet = 0;
	};

	std::vector<unsigned char> BuildFile(const std::vector<SPoseSpec> &vPoses)
	{
		std::vector<unsigned char> vData(sizeof(int64_t) + vPoses.size() * qm_pose::RECORD_SIZE, 0);
		WriteInt32(vData, 0, (int32_t)(vPoses.size() & 0xffffffff));
		WriteInt32(vData, 4, 0);
		for(size_t i = 0; i < vPoses.size(); ++i)
		{
			const SPoseSpec &Pose = vPoses[i];
			const size_t Base = RecordOffset(i);
			WritePackedString(vData, Base + 0, 4, Pose.m_pName);
			WritePackedString(vData, Base + 16, 3, Pose.m_pClan);
			WritePackedString(vData, Base + 32, 6, Pose.m_pSkin);
			WriteInt32(vData, Base + 56, Pose.m_CustomColor ? 1 : 0);
			WriteInt32(vData, Base + 60, Pose.m_ColorBody);
			WriteInt32(vData, Base + 64, Pose.m_ColorFeet);
			// CharacterCore 起点 68：tick(0), x(4), y(8), velx(12), vely(16), angle(20),
			// direction(24), jumped(28), hookedPlayer(32), hookState(36), hookTick(40),
			// hookX(44), hookY(48), hookDx(52), hookDy(56)
			WriteInt32(vData, Base + 68 + 4, Pose.m_X);
			WriteInt32(vData, Base + 68 + 8, Pose.m_Y);
			WriteInt32(vData, Base + 68 + 20, Pose.m_Angle);
			WriteInt32(vData, Base + 68 + 36, Pose.m_HookState);
			WriteInt32(vData, Base + 68 + 44, Pose.m_HookX);
			WriteInt32(vData, Base + 68 + 48, Pose.m_HookY);
			WriteInt32(vData, Base + 128, Pose.m_Emote);
			WriteInt32(vData, Base + 132, Pose.m_Weapon);
		}
		return vData;
	}
} // namespace

TEST(QmPoseFile, LoadsEveryFieldOfEachRecord)
{
	const std::vector<SPoseSpec> vSpecs = {
		{"HAI", "cat", "m_jumao", 2617, 2468, 333, 1, 2618, 2437, 5, 3, true, 65369, 16776960},
		{"second", "", "kitty", -640, 480, 128, 0, 0, 0, 0, 0, false, 0, 0},
	};
	const std::vector<unsigned char> vFile = BuildFile(vSpecs);

	std::vector<SPoseTee> vPoses;
	ASSERT_EQ(qm_pose::Load(vFile.data(), vFile.size(), vPoses), qm_pose::ELoadResult::OK);
	ASSERT_EQ(vPoses.size(), 2u);

	EXPECT_STREQ(vPoses[0].m_aName, "HAI");
	EXPECT_STREQ(vPoses[0].m_aClan, "cat");
	EXPECT_STREQ(vPoses[0].m_aSkin, "m_jumao");
	EXPECT_EQ(vPoses[0].m_X, 2617);
	EXPECT_EQ(vPoses[0].m_Y, 2468);
	EXPECT_EQ(vPoses[0].m_Angle, 333);
	EXPECT_EQ(vPoses[0].m_HookState, 1);
	EXPECT_EQ(vPoses[0].m_HookX, 2618);
	EXPECT_EQ(vPoses[0].m_HookY, 2437);
	EXPECT_EQ(vPoses[0].m_Emote, 5);
	EXPECT_EQ(vPoses[0].m_Weapon, 3);
	EXPECT_TRUE(vPoses[0].m_UseCustomColor);
	EXPECT_EQ(vPoses[0].m_ColorBody, 65369);
	EXPECT_EQ(vPoses[0].m_ColorFeet, 16776960);

	EXPECT_STREQ(vPoses[1].m_aName, "second");
	EXPECT_STREQ(vPoses[1].m_aClan, "");
	EXPECT_EQ(vPoses[1].m_X, -640);
	EXPECT_EQ(vPoses[1].m_Y, 480);
	EXPECT_FALSE(vPoses[1].m_UseCustomColor);
}

TEST(QmPoseFile, TruncatedFileIsRejectedInsteadOfPartiallyLoaded)
{
	const std::vector<unsigned char> vFile = BuildFile({{"a"}, {"b"}, {"c"}});
	std::vector<unsigned char> vTruncated(vFile.begin(), vFile.begin() + (long)(vFile.size() - 8));

	std::vector<SPoseTee> vPoses;
	vPoses.push_back(SPoseTee{}); // 预置一条，确认失败时不覆写
	const auto Result = qm_pose::Load(vTruncated.data(), vTruncated.size(), vPoses);
	EXPECT_EQ(Result, qm_pose::ELoadResult::SIZE_MISMATCH);
	EXPECT_EQ(vPoses.size(), 1u) << "a failed load must not leave a half-parsed result behind";
}

TEST(QmPoseFile, CountFieldDisagreeingWithSizeIsRejected)
{
	// 布局变了（例如对方换了 32 位构建或改了结构体）时，长度校验是唯一防线。
	std::vector<unsigned char> vFile = BuildFile({{"a"}});
	WriteInt32(vFile, 0, 2);
	std::vector<SPoseTee> vPoses;
	EXPECT_EQ(qm_pose::Load(vFile.data(), vFile.size(), vPoses), qm_pose::ELoadResult::SIZE_MISMATCH);
	EXPECT_TRUE(vPoses.empty());
}

TEST(QmPoseFile, EmptyFileAndZeroCountReportEmpty)
{
	std::vector<SPoseTee> vPoses;
	EXPECT_EQ(qm_pose::Load(nullptr, 0, vPoses), qm_pose::ELoadResult::EMPTY);
	EXPECT_EQ(qm_pose::Load("", 0, vPoses), qm_pose::ELoadResult::EMPTY);

	const std::vector<unsigned char> vZero(sizeof(int64_t), 0);
	EXPECT_EQ(qm_pose::Load(vZero.data(), vZero.size(), vPoses), qm_pose::ELoadResult::EMPTY);
}

TEST(QmPoseFile, AbsurdCoordinateIsRejected)
{
	SPoseSpec Spec;
	Spec.m_X = qm_pose::MAX_COORDINATE + 1;
	const std::vector<unsigned char> vFile = BuildFile({Spec});
	std::vector<SPoseTee> vPoses;
	EXPECT_EQ(qm_pose::Load(vFile.data(), vFile.size(), vPoses), qm_pose::ELoadResult::BAD_COORDINATE);
	EXPECT_TRUE(vPoses.empty());
}

TEST(QmPoseFile, RecordCountAboveTheCapIsRejectedBeforeAllocating)
{
	std::vector<unsigned char> vFile(sizeof(int64_t), 0);
	WriteInt32(vFile, 0, 100000);
	std::vector<SPoseTee> vPoses;
	EXPECT_EQ(qm_pose::Load(vFile.data(), vFile.size(), vPoses), qm_pose::ELoadResult::TOO_MANY);
}

TEST(QmPoseFile, PackedStringStopsAtZeroAndKeepsBytesVerbatim)
{
	// 填充字节在打包格式里是 128（解码后正好是 0）。写成裸 0 会解码成 -128，
	// 那是格式里的合法字符，不是结束符。
	const int32_t aPacked[1] = {(int32_t)(((uint32_t)('a' + 128) << 24) | ((uint32_t)('b' + 128) << 16) |
					      ((uint32_t)128 << 8) | (uint32_t)128)};
	char aOut[8];
	qm_pose::ReadPackedString(aPacked, 1, aOut, sizeof(aOut));
	EXPECT_STREQ(aOut, "ab");

	// 中文名在该格式里写端就已溢出丢位，读回不保证是原文；解析层原样保留字节，
	// 不做二次加工，避免伪造一个看起来正确的名字。
	const int32_t aHigh[1] = {(int32_t)0xff000000u};
	qm_pose::ReadPackedString(aHigh, 1, aOut, sizeof(aOut));
	EXPECT_EQ((unsigned char)aOut[0], 0x7fu);
}
