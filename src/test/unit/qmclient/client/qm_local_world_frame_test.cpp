#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/world_event_gate.h>

#include <gtest/gtest.h>

static SLocalActorFrame MakeFrame(int Tick)
{
	SLocalActorFrame Frame;
	Frame.m_Tick = Tick;
	Frame.m_X = 3200 + Tick;
	Frame.m_Y = 1600;
	Frame.m_VelX = 512;
	Frame.m_VelY = -256;
	Frame.m_Angle = 42;
	Frame.m_Direction = -1;
	Frame.m_Weapon = 1; // 手枪
	Frame.m_AttackTick = Tick - 10;
	return Frame;
}

TEST(QmLocalWorldFrame, CarriesTheRecordedStateIntoTheGhostFormat)
{
	const CNetObj_Character Obj = QmLocalWorldFrameToNetObj(MakeFrame(1234));

	EXPECT_EQ(Obj.m_Tick, 1234);
	EXPECT_EQ(Obj.m_X, 4434);
	EXPECT_EQ(Obj.m_Y, 1600);
	// 速度是协议单位，换算由下游按 /256 处理。
	EXPECT_EQ(Obj.m_VelX, 512);
	EXPECT_EQ(Obj.m_VelY, -256);
	EXPECT_EQ(Obj.m_Angle, 42);
	EXPECT_EQ(Obj.m_Direction, -1);
	EXPECT_EQ(Obj.m_Weapon, 1);
	EXPECT_EQ(Obj.m_AttackTick, 1224);
}

TEST(QmLocalWorldFrame, AGrabbedRecordedHookNeverBecomesARealAttachment)
{
	// 录制里的 GRABBED 只是视觉状态（那条时空的人当时勾住了别人）。
	// 带上 HookedPlayer 会误建挂接，带上 HookTick 会让钩子按超时自动脱钩。
	SLocalActorFrame Frame = MakeFrame(77);
	Frame.m_HookState = 5; // HOOK_GRABBED
	Frame.m_HookX = 3300;
	Frame.m_HookY = 1700;

	const CNetObj_Character Obj = QmLocalWorldFrameToNetObj(Frame);

	EXPECT_EQ(Obj.m_HookState, 5);
	EXPECT_EQ(Obj.m_HookX, 3300);
	EXPECT_EQ(Obj.m_HookY, 1700);
	EXPECT_EQ(Obj.m_HookedPlayer, -1);
	EXPECT_EQ(Obj.m_HookTick, 0);
}

TEST(QmLocalWorldFrame, AnEmptyFrameIsIdleAndDoesNotHookAnyone)
{
	const CNetObj_Character Obj = QmLocalWorldFrameToNetObj(SLocalActorFrame{});

	EXPECT_EQ(Obj.m_HookState, 0); // HOOK_IDLE
	EXPECT_EQ(Obj.m_HookedPlayer, -1);
	EXPECT_EQ(Obj.m_HookTick, 0);
	EXPECT_EQ(Obj.m_X, 0);
	EXPECT_EQ(Obj.m_Y, 0);
}

TEST(QmLocalWorldTrack, AShortCurrentSegmentFallsBackToTheLastCompleteRun)
{
	// /r 之后刚跑几帧就想保存：应该给刚封存的那段，而不是一条 5 帧的碎片。
	CLocalWorld Track;
	Track.BeginTrack(1000);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(MakeFrame(1000 + i));
	Track.MarkSegmentBoundary();
	for(int i = 0; i < 5; i++)
		Track.AddTrackFrame(MakeFrame(2000 + i));

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	ASSERT_TRUE(Track.BestSegment(&vFrames, &StartTick));
	EXPECT_EQ(vFrames.size(), 60u);
	EXPECT_EQ(StartTick, 1000);
	EXPECT_EQ(vFrames.front().m_Tick, 1000);
	EXPECT_EQ(vFrames.back().m_Tick, 1059);
}

TEST(QmLocalWorldTrack, AShortCurrentSegmentIsRejectedWhenNothingWasCompleted)
{
	// 按下开始后不到一秒就停：没有可以回退的完成段，也不能把这条碎片当线路存下来。
	CLocalWorld Track;
	Track.BeginTrack(1000);
	for(int i = 0; i < 5; i++)
		Track.AddTrackFrame(MakeFrame(1000 + i));

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	EXPECT_FALSE(Track.BestSegment(&vFrames, &StartTick));
}

TEST(QmLocalWorldTrack, AMinimumLengthCurrentSegmentIsSaveable)
{
	// 恰好达到最小时长（50 帧）就是一条有效线路，即使没有完成段。
	CLocalWorld Track;
	Track.BeginTrack(1000);
	for(int i = 0; i < CLocalWorld::MIN_SEGMENT_FRAMES; i++)
		Track.AddTrackFrame(MakeFrame(1000 + i));

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	ASSERT_TRUE(Track.BestSegment(&vFrames, &StartTick));
	EXPECT_EQ(vFrames.size(), static_cast<size_t>(CLocalWorld::MIN_SEGMENT_FRAMES));
	EXPECT_EQ(StartTick, 1000);
}

TEST(QmLocalWorldTrack, TheLongerCurrentSegmentWins)
{
	// 正在跑的这段已经比上一段长，/pg 就该保存"现在这段"。
	CLocalWorld Track;
	Track.BeginTrack(100);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(MakeFrame(100 + i));
	Track.MarkSegmentBoundary();
	for(int i = 0; i < 80; i++)
		Track.AddTrackFrame(MakeFrame(500 + i));

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	ASSERT_TRUE(Track.BestSegment(&vFrames, &StartTick));
	EXPECT_EQ(vFrames.size(), 80u);
	EXPECT_EQ(StartTick, 500);
}

TEST(QmLocalWorldTrack, ShiftingTheWorldClockMovesEveryRecordedTick)
{
	CLocalWorld Track;
	Track.BeginTrack(100);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(MakeFrame(100 + i));
	Track.ShiftTrackTicks(37);

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	ASSERT_TRUE(Track.BestSegment(&vFrames, &StartTick));
	EXPECT_EQ(StartTick, 137);
	EXPECT_EQ(vFrames.front().m_Tick, 137);
	EXPECT_EQ(vFrames.front().m_AttackTick, 127);
}

TEST(QmLocalWorldTrack, EndingTheSessionDiscardsUnsavedTracks)
{
	CLocalWorld Track;
	Track.BeginTrack(100);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(MakeFrame(100 + i));
	Track.MarkSegmentBoundary();
	Track.EndTrack();

	std::vector<SLocalActorFrame> vFrames;
	int StartTick = 0;
	EXPECT_FALSE(Track.BestSegment(&vFrames, &StartTick));
	EXPECT_FALSE(Track.TrackActive());
}

TEST(QmLocalWorldTrack, BodyAndDummyAreSavedAsParallelTracks)
{
	CLocalWorld Track;
	Track.BeginTrack(1000);
	Track.SetTrackIdentity(0, 3, "self");
	Track.SetTrackIdentity(1, 7, "dummy");
	for(int i = 0; i < 60; i++)
	{
		Track.AddTrackFrame(0, MakeFrame(1000 + i));
		SLocalActorFrame Dummy = MakeFrame(1000 + i);
		Dummy.m_X += 512;
		Track.AddTrackFrame(1, Dummy);
	}

	SLocalSegment Segment;
	ASSERT_TRUE(Track.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vTracks.size(), 2u);
	EXPECT_EQ(Segment.m_vTracks[0].m_ClientId, 3);
	EXPECT_STREQ(Segment.m_vTracks[1].m_aName, "dummy");
	EXPECT_EQ(Segment.m_vTracks[1].m_vFrames.front().m_X, Segment.m_vTracks[0].m_vFrames.front().m_X + 512);
}

TEST(QmLocalWorldTrack, InteractionEventsAreRebasedAndClippedToTheSegment)
{
	CLocalWorld Track;
	Track.BeginTrack(1000);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(0, MakeFrame(1000 + i));

	SLocalActorEvent Outside = {};
	Outside.m_Tick = 900;
	Outside.m_Type = 1;
	SLocalActorEvent Inside = {};
	Inside.m_Tick = 1010;
	Inside.m_Type = 2;
	Inside.m_aData[2] = 5;
	Track.AddTrackEvent(Outside);
	Track.AddTrackEvent(Inside);

	SLocalSegment Segment;
	ASSERT_TRUE(Track.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vEvents.size(), 1u);
	EXPECT_EQ(Segment.m_vEvents[0].m_Tick, 10);
	EXPECT_EQ(Segment.m_vEvents[0].m_aData[2], 5);
}

TEST(QmLocalWorldTrack, ShiftingTheClockAdvancesTheStartTickExactlyOnce)
{
	CLocalWorld Track;
	Track.BeginTrack(100);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(0, MakeFrame(100 + i));
	Track.ShiftTrackTicks(37);

	EXPECT_EQ(Track.TrackStartTick(), 137);
	SLocalSegment Segment;
	ASSERT_TRUE(Track.BestSegment(&Segment));
	EXPECT_EQ(Segment.m_StartTick, 137);
}

TEST(QmLocalWorldSwitchers, EncodesOnlyTheActiveTeamIntoTheTimelineBitmask)
{
	std::vector<SSwitchers> vSwitchers(40);
	SSwitchers &First = vSwitchers[0];
	First = {};
	First.m_aStatus[3] = true;
	First.m_aStatus[5] = true;
	vSwitchers[7] = {};
	vSwitchers[7].m_aStatus[3] = true;
	vSwitchers[39] = {};
	vSwitchers[39].m_aStatus[3] = true;

	const SLocalSwitchState State = QmEncodeLocalSwitchState(123, vSwitchers, 3);
	EXPECT_EQ(State.m_Tick, 123);
	EXPECT_EQ(State.m_HighestSwitchNumber, 39);
	EXPECT_EQ(State.m_aStatus[0] & (1u << 0), 1u);
	EXPECT_EQ(State.m_aStatus[0] & (1u << 7), 1u << 7);
	EXPECT_EQ(State.m_aStatus[1] & (1u << 7), 1u << 7); // 39 = 32 + 7
	EXPECT_EQ(State.m_aStatus[0] & (1u << 5), 0u); // 5 号属于队伍 5，不是队伍 3

	// 越界队伍钳到合法范围，不会读到数组外。
	EXPECT_EQ(QmEncodeLocalSwitchState(0, vSwitchers, 999).m_aStatus[0], 0u);
}

TEST(QmLocalWorldSwitchers, IdenticalSnapshotsAreDeduplicatedButChangesAreKept)
{
	CLocalWorld Track;
	Track.BeginTrack(1000);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(0, MakeFrame(1000 + i));

	SLocalSwitchState State = {};
	State.m_HighestSwitchNumber = 7;
	State.m_Tick = 1000;
	Track.AddTrackSwitchState(State);
	State.m_Tick = 1001;
	Track.AddTrackSwitchState(State); // 相同状态，不该留第二条
	State.m_Tick = 1020;
	State.m_aStatus[0] |= 1u << 3;
	Track.AddTrackSwitchState(State);

	EXPECT_EQ(Track.CurrentSwitchStateCount(), 2);
	SLocalSegment Segment;
	ASSERT_TRUE(Track.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vSwitchStates.size(), 2u);
	EXPECT_EQ(Segment.m_vSwitchStates[0].m_Tick, 0);
	EXPECT_EQ(Segment.m_vSwitchStates[1].m_Tick, 20);
	EXPECT_EQ(Segment.m_vSwitchStates[1].m_aStatus[0] & (1u << 3), 1u << 3);
}

TEST(QmLocalWorldSwitchers, SegmentBoundaryKeepsSwitchHistoryWithTheCompletedRun)
{
	CLocalWorld Track;
	Track.BeginTrack(100);
	for(int i = 0; i < 60; i++)
		Track.AddTrackFrame(0, MakeFrame(100 + i));
	SLocalSwitchState State = {};
	State.m_Tick = 100;
	State.m_aStatus[0] = 1;
	Track.AddTrackSwitchState(State);
	Track.MarkSegmentBoundary();
	for(int i = 0; i < 5; i++)
		Track.AddTrackFrame(0, MakeFrame(500 + i));
	State = {};
	State.m_Tick = 500;
	Track.AddTrackSwitchState(State);

	SLocalSegment Segment;
	ASSERT_TRUE(Track.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Segment.m_vSwitchStates[0].m_aStatus[0], 1u); // 来自完成段
	EXPECT_EQ(Segment.m_StartTick, 100);
}

TEST(QmWorldEventGate, PredictionPlaybackAndRecordingUseTheSameSoundGate)
{
	const auto Allowed = [](int SoundId, bool SndGame, bool PlayJump, bool PlayDeath, bool RaceSounds, bool SndGun) {
		return QmShouldPlayWorldSound(SoundId, SndGame, PlayJump, PlayDeath, RaceSounds, SndGun, true);
	};
	EXPECT_TRUE(Allowed(SOUND_GUN_FIRE, true, true, true, false, false)); // 非赛道图忽略 snd_gun
	EXPECT_FALSE(Allowed(SOUND_GUN_FIRE, true, true, true, true, false)); // 赛道图关枪声
	EXPECT_FALSE(Allowed(SOUND_GUN_FIRE, false, true, true, false, true)); // snd_game 关
	EXPECT_FALSE(Allowed(SOUND_PLAYER_JUMP, true, false, true, false, true)); // 禅模式静音跳跃
	EXPECT_FALSE(Allowed(SOUND_PLAYER_DIE, true, true, false, false, true)); // 禅模式静音死亡
	EXPECT_TRUE(Allowed(SOUND_HAMMER_FIRE, true, true, true, true, false)); // 锤击音不受枪声开关影响
}

TEST(QmLocalWorldViewSession, RegistrationIsPureStateAndNeverTouchesTheTrack)
{
	CLocalWorld World;
	World.BeginTrack(10);
	World.AddTrackFrame(0, MakeFrame(10));
	World.BeginViewSession(3);
	EXPECT_TRUE(World.ViewSessionActive());
	EXPECT_EQ(World.ViewSessionSlotCount(), 3);
	EXPECT_EQ(World.CurrentFrameCount(), 1);
	World.EndViewSession();
	EXPECT_FALSE(World.ViewSessionActive());
	EXPECT_EQ(World.ViewSessionSlotCount(), 0);
	EXPECT_TRUE(World.TrackActive());
}
