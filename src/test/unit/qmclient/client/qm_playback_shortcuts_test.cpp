// 回放快捷键键表与查看时间线事件新鲜度的行为测试。
#include <base/system.h>

#include <game/client/components/qmclient/playback_shortcuts.h>
#include <game/client/components/qmclient/rank_view_timeline.h>

#include <gtest/gtest.h>

// 键表是回放快捷键与绑定层让路的唯一登记处：暂停键必须全部在表内，
// 否则绑定层不会截走这些按键，按 K 暂停会顺带执行默认的 kill 绑定。
TEST(QmPlaybackShortcuts, PauseKeysAreRegistered)
{
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_SPACE));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_RETURN));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_KP_ENTER));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_K));
}

TEST(QmPlaybackShortcuts, SeekAndStepKeysAreRegistered)
{
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_LEFT));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_J));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_RIGHT));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_L));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_HOME));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_END));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_PERIOD));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_COMMA));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_UP));
	EXPECT_TRUE(QmPlaybackShortcutKey(KEY_DOWN));
	for(int Key = KEY_0; Key <= KEY_9; Key++)
		EXPECT_TRUE(QmPlaybackShortcutKey(Key));
}

// 玩图按键必须留在绑定层：查看回放时玩家仍连着服务器，移动/钩子不能被截走。
TEST(QmPlaybackShortcuts, GameplayKeysAreNotClaimed)
{
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_A));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_D));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_W));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_S));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_ESCAPE));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_F1));
	EXPECT_FALSE(QmPlaybackShortcutKey(KEY_MOUSE_1));
}

// 播放头附近的事件才算新鲜：大幅快进后，几秒前的事件不该再补播。
TEST(QmViewTimelineEventFreshness, OnlyEventsNearThePlayheadAreFresh)
{
	using qmclient::rank_ghost::ViewEventFresh;
	constexpr int Lag = 50;
	EXPECT_TRUE(ViewEventFresh(1000, 1000, Lag)); // 当前 tick
	EXPECT_TRUE(ViewEventFresh(1000, 1049, Lag)); // 刚落后不到一秒
	EXPECT_TRUE(ViewEventFresh(1000, 1050, Lag)); // 恰好一秒
	EXPECT_FALSE(ViewEventFresh(1000, 1051, Lag)); // 超过一秒
	EXPECT_FALSE(ViewEventFresh(1200, 1000, Lag)); // 未来事件不播
}

TEST(QmViewTimelineEventFreshness, NonPositiveLagStillKeepsTheCurrentTick)
{
	using qmclient::rank_ghost::ViewEventFresh;
	// 非正延迟按 1 tick 处理：至少保留“当前 tick 与上一 tick”的事件，
	// 快照事件本来就落后播放头一帧。
	EXPECT_TRUE(ViewEventFresh(1000, 1000, 0));
	EXPECT_TRUE(ViewEventFresh(1000, 1000, -5));
	EXPECT_TRUE(ViewEventFresh(999, 1000, 0));
	EXPECT_FALSE(ViewEventFresh(998, 1000, 0));
}
