#include <game/client/components/qmclient/practice_recorder.h>

#include <gtest/gtest.h>

namespace
{
	SLocalActorFrame Frame(int Tick)
	{
		SLocalActorFrame Out;
		Out.m_Tick = Tick;
		return Out;
	}
}

TEST(QmPracticeRecorder, BeginStartsATrackAndBindsTrackIdentities)
{
	CLocalWorld World;
	CPracticeRecorder Recorder;
	Recorder.Begin(World, 100, 3, 4, "Main", "Dummy");

	EXPECT_TRUE(Recorder.Active(World));
	EXPECT_FALSE(Recorder.Paused());
	EXPECT_EQ(World.TrackStartTick(), 100);

	// 达到最小时长才是一段可保存线路；这里只关心身份是否随 Begin 绑定。
	for(int Tick = 100; Tick < 100 + CLocalWorld::MIN_SEGMENT_FRAMES; Tick++)
	{
		World.AddTrackFrame(0, Frame(Tick));
		World.AddTrackFrame(1, Frame(Tick));
	}
	SLocalSegment Segment;
	ASSERT_TRUE(World.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vTracks.size(), 2u);
	EXPECT_EQ(Segment.m_vTracks[0].m_ClientId, 3);
	EXPECT_STREQ(Segment.m_vTracks[0].m_aName, "Main");
	EXPECT_EQ(Segment.m_vTracks[1].m_ClientId, 4);
	EXPECT_STREQ(Segment.m_vTracks[1].m_aName, "Dummy");
}

TEST(QmPracticeRecorder, ResumeAfterPauseStartsANewTake)
{
	CLocalWorld World;
	CPracticeRecorder Recorder;
	Recorder.Begin(World, 100, 3, -1, "Main", "");
	for(int Tick = 100; Tick < 160; Tick++)
		World.AddTrackFrame(0, Frame(Tick));

	Recorder.Pause();
	EXPECT_TRUE(Recorder.Paused());

	Recorder.Resume(World);
	EXPECT_FALSE(Recorder.Paused());
	EXPECT_EQ(World.CurrentFrameCount(), 0);
	EXPECT_TRUE(World.HasCompletedSegment());

	SLocalSegment Segment;
	ASSERT_TRUE(World.BestSegment(&Segment));
	ASSERT_EQ(Segment.m_vTracks.size(), 1u);
	EXPECT_EQ((int)Segment.m_vTracks[0].m_vFrames.size(), 60);
	EXPECT_EQ(Segment.m_StartTick, 100);
}

TEST(QmPracticeRecorder, BoundarySealsTheCurrentTakeWithoutStoppingRecording)
{
	CLocalWorld World;
	CPracticeRecorder Recorder;
	Recorder.Begin(World, 10, 3, -1, "Main", "");
	for(int Tick = 10; Tick < 70; Tick++)
		World.AddTrackFrame(0, Frame(Tick));

	Recorder.MarkBoundary(World);
	EXPECT_TRUE(Recorder.Active(World));
	EXPECT_EQ(World.CurrentFrameCount(), 0);
	EXPECT_TRUE(World.HasCompletedSegment());
}

TEST(QmPracticeRecorder, EndStopsRecordingAndClearsBuffers)
{
	CLocalWorld World;
	CPracticeRecorder Recorder;
	Recorder.Begin(World, 10, 3, -1, "Main", "");
	for(int Tick = 10; Tick < 70; Tick++)
		World.AddTrackFrame(0, Frame(Tick));
	Recorder.Pause();

	Recorder.End(World);
	EXPECT_FALSE(Recorder.Active(World));
	EXPECT_FALSE(Recorder.Paused());
	EXPECT_EQ(World.CurrentFrameCount(), 0);
	EXPECT_FALSE(World.HasCompletedSegment());
}
