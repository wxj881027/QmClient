// 媒体会话选源与时间轴新鲜度：Windows SMTC 的「当前会话」不会随浏览器视频
// 停下自动让位，必须自己按播放状态挑。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/system_media_controls.h>

#include <gtest/gtest.h>

namespace
{
	int Priority(SystemMediaControls::ESessionActivity Activity)
	{
		return SystemMediaControls::SessionActivityPriority(Activity);
	}
}

TEST(QmMediaSessionPick, PlayingBeatsPausedAndStopped)
{
	using Activity = SystemMediaControls::ESessionActivity;
	const int aPriorities[] = {Priority(Activity::PAUSED), Priority(Activity::PLAYING), Priority(Activity::STOPPED)};
	// 当前会话（暂停的浏览器）不该压过另一个正在播放的播放器。
	EXPECT_EQ(SystemMediaControls::PickMediaSession(aPriorities, 3, 0), 1);
}

TEST(QmMediaSessionPick, TiesKeepCurrentSession)
{
	using Activity = SystemMediaControls::ESessionActivity;
	const int aPriorities[] = {Priority(Activity::PLAYING), Priority(Activity::PLAYING)};
	// 两个都在播：保留系统当前会话，不要为了「换而换」来回跳。
	EXPECT_EQ(SystemMediaControls::PickMediaSession(aPriorities, 2, 1), 1);
	const int aPaused[] = {Priority(Activity::PAUSED), Priority(Activity::PAUSED), Priority(Activity::STOPPED)};
	EXPECT_EQ(SystemMediaControls::PickMediaSession(aPaused, 3, 0), 0);
}

TEST(QmMediaSessionPick, InvalidCurrentFallsBackToFirstBest)
{
	using Activity = SystemMediaControls::ESessionActivity;
	const int aPriorities[] = {Priority(Activity::STOPPED), Priority(Activity::PLAYING), Priority(Activity::PLAYING)};
	EXPECT_EQ(SystemMediaControls::PickMediaSession(aPriorities, 3, -1), 1);
	EXPECT_EQ(SystemMediaControls::PickMediaSession(aPriorities, 3, 99), 1);
}

TEST(QmMediaSessionPick, EmptySessionListHasNoSelection)
{
	EXPECT_EQ(SystemMediaControls::PickMediaSession(nullptr, 0, -1), -1);
}

TEST(QmMediaSessionPick, StaleTimelineDemotesFakePlaying)
{
	constexpr int64_t Now = 100'000'000'000LL;
	// 时间单位是 100ns：1 秒 = 10'000'000。
	constexpr int64_t TwoSeconds = 20'000'000LL;
	constexpr int64_t SixSeconds = 60'000'000LL;
	// 正常播放：位置刚更新过。
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(true, 180'000, Now - TwoSeconds, Now));
	// 状态说在播，但六秒没更新过位置（视频早停了）。
	EXPECT_TRUE(SystemMediaControls::MediaTimelineLooksStale(true, 180'000, Now - SixSeconds, Now));
	// 没有明确时长（直播/未知）不做判断；暂停状态也不该被误判。
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(true, 0, Now - SixSeconds, Now));
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(false, 180'000, Now - SixSeconds, Now));
	// 播放器没填 LastUpdatedTime（=0）算「没上报」，不能当成停住了。
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(true, 180'000, 0, Now));
}
