#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_APP_WATCHER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_APP_WATCHER_H

#include <engine/shared/jobs.h>

#include <game/client/component.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

// 检测任务只持有采样函数和结果；组件释放后也无需回调或等待组件。
class CQmMusicAppScanJob : public IJob
{
	std::function<std::optional<uint64_t>()> m_Scan;
	std::optional<uint64_t> m_RunningMask;

	void Run() override { m_RunningMask = m_Scan(); }

public:
	explicit CQmMusicAppScanJob(std::function<std::optional<uint64_t>()> Scan) :
		m_Scan(std::move(Scan)) {}

	bool TryGetResult(uint64_t &RunningMask) const
	{
		// IJob 的完成状态发布结果；不能读取仍在执行中的普通成员。
		if(State() != STATE_DONE || !m_RunningMask)
			return false;
		RunningMask = *m_RunningMask;
		return true;
	}
};

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
	void OnShutdown() override;

private:
	void ApplyRunningApps(uint64_t RunningMask);
	std::shared_ptr<CQmMusicAppScanJob> m_pScanJob;

	bool m_Initialized = false;
	uint64_t m_PrevRunningMask = 0;
	int64_t m_LastCheckTick = 0;
};

#endif
