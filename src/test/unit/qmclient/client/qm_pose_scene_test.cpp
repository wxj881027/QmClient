#include <base/str.h>

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/pose_scene.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
	SPoseTee MakePose(const char *pName, int X, int Y, int Angle = 0)
	{
		SPoseTee Pose{};
		str_copy(Pose.m_aName, pName);
		str_copy(Pose.m_aSkin, "default");
		Pose.m_X = X;
		Pose.m_Y = Y;
		Pose.m_Angle = Angle;
		Pose.m_Direction = 1;
		Pose.m_Weapon = 0;
		return Pose;
	}

	const CNetObj_Character *FindCharacter(const CSnapshot *pSnap, int Id)
	{
		const void *pData = pSnap->FindItem(NETOBJTYPE_CHARACTER, Id);
		return (const CNetObj_Character *)pData;
	}

	const CNetObj_ClientInfo *FindClientInfo(const CSnapshot *pSnap, int Id)
	{
		const void *pData = pSnap->FindItem(NETOBJTYPE_CLIENTINFO, Id);
		return (const CNetObj_ClientInfo *)pData;
	}
} // namespace

TEST(QmPoseScene, BuildsOneObjectSetPerTee)
{
	const std::vector<SPoseTee> vPoses = {
		MakePose("first", 100, 200, 33),
		MakePose("second", 300, 400, 66),
	};

	CSnapshotBuffer Buffer;
	qm_pose::SSceneStats Stats{};
	const int Size = qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), &Stats);
	ASSERT_GT(Size, 0);
	ASSERT_TRUE(Buffer.AsSnapshot()->IsValid((size_t)Size));

	EXPECT_EQ(Stats.m_Tees, 2);

	// 每名 Tee 都必须带齐四类对象，缺任意一类客户端都不会画出来。
	for(int Id = 0; Id < 2; ++Id)
	{
		EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_CLIENTINFO, Id), nullptr) << "client info " << Id;
		EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_PLAYERINFO, Id), nullptr) << "player info " << Id;
		EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_DDNETPLAYER, Id), nullptr) << "ddnet player " << Id;
		EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_CHARACTER, Id), nullptr) << "character " << Id;
	}

	const CNetObj_Character *pFirst = FindCharacter(Buffer.AsSnapshot(), 0);
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_X, 100);
	EXPECT_EQ(pFirst->m_Y, 200);
	EXPECT_EQ(pFirst->m_Angle, 33);
	// 静止造型：有速度会让渲染器插值出朝向，造型会漂移。
	EXPECT_EQ(pFirst->m_VelX, 0);
	EXPECT_EQ(pFirst->m_VelY, 0);
}

TEST(QmPoseScene, SceneObjectsMakeTheSnapshotRenderable)
{
	const std::vector<SPoseTee> vPoses = {MakePose("solo", 500, 600)};
	CSnapshotBuffer Buffer;
	const int Size = qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), nullptr);
	ASSERT_GT(Size, 0);

	// 没有 GAMEINFO 时客户端多处逻辑会整体跳过。
	EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_GAMEINFO, 0), nullptr);
	const CNetObj_SpectatorInfo *pSpec = (const CNetObj_SpectatorInfo *)Buffer.AsSnapshot()->FindItem(NETOBJTYPE_SPECTATORINFO, 0);
	ASSERT_NE(pSpec, nullptr);
	// 自由视角 + 相机落在合影中心，否则画面会对着地图原点。
	EXPECT_EQ(pSpec->m_SpectatorId, -1);
	EXPECT_EQ(pSpec->m_X, 500);
	EXPECT_EQ(pSpec->m_Y, 600);
}

TEST(QmPoseScene, AutoCenterUsesTheBoundingBoxMiddle)
{
	const std::vector<SPoseTee> vPoses = {
		MakePose("a", -1000, 0),
		MakePose("b", 500, 400),
		MakePose("c", 0, -200),
	};

	int CenterX = 0, CenterY = 0;
	qm_pose::ComputeCenter(vPoses, CenterX, CenterY);
	EXPECT_EQ(CenterX, -250); // (-1000 + 500) / 2
	EXPECT_EQ(CenterY, 100); // (-200 + 400) / 2

	CSnapshotBuffer Buffer;
	qm_pose::SSceneStats Stats{};
	ASSERT_GT(qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), &Stats), 0);
	EXPECT_EQ(Stats.m_CenterX, CenterX);
	EXPECT_EQ(Stats.m_CenterY, CenterY);
}

TEST(QmPoseScene, FitZoomIncludesAllTeesAndPadding)
{
	const std::vector<SPoseTee> vPoses = {
		MakePose("left", 0, 0),
		MakePose("right", 1000, 200),
	};

	// 宽度要求 1400/1000=1.4，垂直要求 600/800=0.75，因此宽度主导。
	EXPECT_FLOAT_EQ(qm_pose::ComputeFitZoom(vPoses, 1000.0f, 800.0f), 1.4f);
	// 空输入和非法基础视口走安全回退，不产生 NaN 或无穷大。
	EXPECT_FLOAT_EQ(qm_pose::ComputeFitZoom({}, 1000.0f, 800.0f), 1.0f);
	EXPECT_FLOAT_EQ(qm_pose::ComputeFitZoom(vPoses, 0.0f, 800.0f), 1.0f);
}

TEST(QmPoseScene, ExplicitCenterOverridesTheBoundingBox)
{
	const std::vector<SPoseTee> vPoses = {MakePose("a", 1000, 1000)};
	qm_pose::SSceneOptions Options;
	Options.m_AutoCenter = false;
	Options.m_CenterX = 42;
	Options.m_CenterY = -42;

	CSnapshotBuffer Buffer;
	qm_pose::SSceneStats Stats{};
	ASSERT_GT(qm_pose::BuildSnapshot(vPoses, Options, Buffer.m_aData, sizeof(Buffer.m_aData), &Stats), 0);
	EXPECT_EQ(Stats.m_CenterX, 42);
	EXPECT_EQ(Stats.m_CenterY, -42);
}

TEST(QmPoseScene, TeeCountIsCappedAndReported)
{
	std::vector<SPoseTee> vPoses;
	for(int i = 0; i < qm_pose::MAX_SCENE_TEES + 20; ++i)
		vPoses.push_back(MakePose("tee", i, i));

	CSnapshotBuffer Buffer;
	qm_pose::SSceneStats Stats{};
	const int Size = qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), &Stats);
	ASSERT_GT(Size, 0);
	// 上限必须如实反映在返回值里，UI 才能告诉用户「只放了前 N 个」。
	EXPECT_EQ(Stats.m_Tees, qm_pose::MAX_SCENE_TEES);
	EXPECT_NE(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_CHARACTER, qm_pose::MAX_SCENE_TEES - 1), nullptr);
	EXPECT_EQ(Buffer.AsSnapshot()->FindItem(NETOBJTYPE_CHARACTER, qm_pose::MAX_SCENE_TEES), nullptr);
}

TEST(QmPoseScene, HookIsCollapsedWhenNotHooking)
{
	// 不挂钩时钩子端点必须落在 Tee 自身：否则渲染器会从 (0,0) 拉一条斜线到场内。
	std::vector<SPoseTee> vPoses = {MakePose("a", 700, 800)};
	vPoses[0].m_HookState = 0;
	vPoses[0].m_HookX = 0;
	vPoses[0].m_HookY = 0;

	CSnapshotBuffer Buffer;
	ASSERT_GT(qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), nullptr), 0);
	const CNetObj_Character *pChar = FindCharacter(Buffer.AsSnapshot(), 0);
	ASSERT_NE(pChar, nullptr);
	EXPECT_EQ(pChar->m_HookX, 700);
	EXPECT_EQ(pChar->m_HookY, 800);

	// 真在挂钩时保留原始端点。
	vPoses[0].m_HookState = 1;
	vPoses[0].m_HookX = 640;
	vPoses[0].m_HookY = 720;
	ASSERT_GT(qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), nullptr), 0);
	pChar = FindCharacter(Buffer.AsSnapshot(), 0);
	ASSERT_NE(pChar, nullptr);
	EXPECT_EQ(pChar->m_HookX, 640);
	EXPECT_EQ(pChar->m_HookY, 720);
}

TEST(QmPoseScene, ClientInfoCarriesNameAndSkin)
{
	const std::vector<SPoseTee> vPoses = {MakePose("HAI", 10, 20)};
	CSnapshotBuffer Buffer;
	ASSERT_GT(qm_pose::BuildSnapshot(vPoses, {}, Buffer.m_aData, sizeof(Buffer.m_aData), nullptr), 0);

	const CNetObj_ClientInfo *pInfo = FindClientInfo(Buffer.AsSnapshot(), 0);
	ASSERT_NE(pInfo, nullptr);
	// 名字按协议打包（每字节 +128），解码回来必须是原名。
	char aName[MAX_NAME_LENGTH] = "";
	for(int i = 0; i < (int)std::size(pInfo->m_aName); ++i)
	{
		const int Packed = pInfo->m_aName[i];
		for(int k = 0; k < 4; ++k)
		{
			const int Ch = ((Packed >> (24 - k * 8)) & 0xff) - 128;
			const size_t Index = (size_t)i * 4 + (size_t)k;
			if(Index + 1 < sizeof(aName))
				aName[Index] = (char)Ch;
		}
	}
	EXPECT_STREQ(aName, "HAI");
}

TEST(QmPoseScene, VideoFrameCountCoversShortDurations)
{
	// 录 5 秒 @ 60fps = 300 帧。
	EXPECT_EQ(qm_pose::VideoFrameCount(5.0f, 60), 300);
	// 短于 1 帧的请求不能被算成 0 帧，否则用户看到「已录制」却没有内容。
	EXPECT_EQ(qm_pose::VideoFrameCount(0.001f, 60), 1);
	// 向上取整：0.5 秒 @ 60fps = 30 帧，0.51 秒 = 31 帧。
	EXPECT_EQ(qm_pose::VideoFrameCount(0.5f, 60), 30);
	EXPECT_EQ(qm_pose::VideoFrameCount(0.51f, 60), 31);
	// 非法输入返回 0 而不是负帧数。
	EXPECT_EQ(qm_pose::VideoFrameCount(0.0f, 60), 0);
	EXPECT_EQ(qm_pose::VideoFrameCount(-1.0f, 60), 0);
	EXPECT_EQ(qm_pose::VideoFrameCount(5.0f, 0), 0);
	EXPECT_EQ(qm_pose::VideoFrameCount(5.0f, -60), 0);
}

TEST(QmPoseScene, EmptyInputProducesNoSnapshot)
{
	CSnapshotBuffer Buffer;
	EXPECT_EQ(qm_pose::BuildSnapshot({}, {}, Buffer.m_aData, sizeof(Buffer.m_aData), nullptr), 0);
}

TEST(QmPoseScene, TooSmallOutputBufferIsRejected)
{
	const std::vector<SPoseTee> vPoses = {MakePose("a", 1, 1)};
	unsigned char aTiny[16];
	EXPECT_EQ(qm_pose::BuildSnapshot(vPoses, {}, aTiny, sizeof(aTiny), nullptr), 0);
}
