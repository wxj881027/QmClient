#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_APP_WATCHER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_APP_WATCHER_H

#include <game/client/component.h>

#include <cstdint>

// 音乐应用启动跟随:监听已注册音乐应用(网易云/汽水等)的启动与退出,
// 自动把启用的 Hook 切换到当前正在运行的应用(互斥,同一时间只启用一个)。
// 只在「应用启动/退出」事件发生时切换,因此用户在设置里手动切换后
// 不会被立刻改回;全部 Hook 都被手动关闭时也不自动打开。
class CQmMusicAppWatcher : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;

private:
	void CheckRunningApps();

	bool m_Initialized = false;
	uint64_t m_PrevRunningMask = 0;
	uint64_t m_LastCheckTick = 0;
};

#endif
