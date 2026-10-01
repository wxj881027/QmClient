#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>

TEST(QmIconDiagnosticsWindow, AccumulatesDrawsAndRunBucketsUntilInterval)
{
	SQmIconDiagnosticsWindow Window;
	SQmIconDiagnostics First;
	First.m_MsdfIconDraws = 3;
	First.m_MaxMsdfManagerCallRun = 2;
	First.m_MsdfManagerCallRunBuckets[1] = 2;
	EXPECT_FALSE(Window.Add(First, 100, 10));

	SQmIconDiagnostics Second;
	Second.m_MsdfIconDraws = 5;
	Second.m_MaxMsdfManagerCallRun = 4;
	Second.m_MsdfManagerCallRunBuckets[1] = 1;
	Second.m_MsdfManagerCallRunBuckets[2] = 1;
	EXPECT_FALSE(Window.Add(Second, 109, 10));
	EXPECT_TRUE(Window.Add({}, 110, 10));
	EXPECT_EQ(Window.m_Frames, 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 8u);
	EXPECT_EQ(Window.m_MaxMsdfDraws, 5u);
	EXPECT_EQ(Window.m_Total.m_MaxMsdfManagerCallRun, 4u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[1], 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[2], 1u);

	Window.Clear(110);
	EXPECT_FALSE(Window.Add(First, 111, 10));
	EXPECT_EQ(Window.m_Frames, 1u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[1], 2u);
}

TEST(QmIconDiagnosticsWindow, ResourceChangesFlushImmediatelyAndOnlyOnce)
{
	SQmIconDiagnosticsWindow Window;
	SQmIconDiagnostics Draw;
	Draw.m_MsdfIconDraws = 7;
	EXPECT_FALSE(Window.Add(Draw, 100, 10));

	SQmIconDiagnostics Resources;
	Resources.m_ReloadAttempts = 1;
	Resources.m_ReloadSuccesses = 1;
	Resources.m_AtlasSwaps = 1;
	Resources.m_TextureLoads = 1;
	EXPECT_TRUE(Window.Add(Resources, 101, 10));
	EXPECT_EQ(Window.m_Frames, 2u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 7u);
	EXPECT_EQ(Window.m_Total.m_TextureLoads, 1u);
	Window.Clear(101);
	EXPECT_FALSE(Window.Add({}, 102, 10));
	EXPECT_EQ(Window.m_Total.m_ReloadAttempts, 0u);
	EXPECT_EQ(Window.m_Total.m_TextureLoads, 0u);

	SQmIconDiagnostics Failure;
	Failure.m_TextureLoadFailures = 1;
	EXPECT_TRUE(Window.Add(Failure, 103, 10));
	Window.Clear(103);
	SQmIconDiagnostics Unload;
	Unload.m_TextureUnloads = 1;
	EXPECT_TRUE(Window.Add(Unload, 104, 10));
}
