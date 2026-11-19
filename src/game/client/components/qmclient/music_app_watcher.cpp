#include "music_app_watcher.h"

#include "qm_music_hook_registry.h"

#include <base/system.h>

#include <engine/engine.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <memory>
#include <new>

#include <windows.h>

#include <tlhelp32.h>
#endif

namespace
{
	constexpr int CHECK_INTERVAL_SECONDS = 2;

#if defined(CONF_FAMILY_WINDOWS)
	std::optional<uint64_t> BuildRunningMask(const IJob &Job)
	{
		if(Job.State() == IJob::STATE_ABORTED)
			return std::nullopt;

		const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return std::nullopt;
		const std::unique_ptr<void, decltype(&CloseHandle)> pSnapshot(hSnapshot, &CloseHandle);

		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		// 快照至少应包含当前客户端进程，首条读取失败时不发布结果。
		if(Job.State() == IJob::STATE_ABORTED || !Process32FirstW(pSnapshot.get(), &Entry))
			return std::nullopt;

		size_t HookCount = 0;
		const SQmMusicHookEntry *pHooks = QmMusicHookRegistry(&HookCount);
		uint64_t Mask = 0;
		do
		{
			Mask |= QmMusicHookMaskForProcess(Entry.szExeFile, pHooks, HookCount);
			if(Job.State() == IJob::STATE_ABORTED)
				return std::nullopt;
		} while(Process32NextW(pSnapshot.get(), &Entry));
		if(GetLastError() != ERROR_NO_MORE_FILES)
			return std::nullopt;
		return Mask;
	}

	class CWindowsMusicAppScanJob final : public CQmMusicAppScanJob
	{
	public:
		CWindowsMusicAppScanJob() :
			CQmMusicAppScanJob([this] { return BuildRunningMask(*this); })
		{
			Abortable(true);
		}
	};
#endif
}

void CQmMusicAppWatcher::OnInit()
{
	m_pScanJob.reset();
	m_Initialized = false;
	m_PrevRunningMask = 0;
	m_LastCheckTick = 0;
}

void CQmMusicAppWatcher::OnShutdown()
{
	m_pScanJob.reset();
}

void CQmMusicAppWatcher::OnUpdate()
{
	if(m_pScanJob)
	{
		if(!m_pScanJob->Done())
			return;
		uint64_t RunningMask;
		if(m_pScanJob->TryGetResult(RunningMask))
			ApplyRunningApps(RunningMask);
		m_pScanJob.reset();
	}
	const int64_t Now = time_get();
	if(Now - m_LastCheckTick < time_freq() * CHECK_INTERVAL_SECONDS)
		return;
	m_LastCheckTick = Now;
#if defined(CONF_FAMILY_WINDOWS)
	try
	{
		auto pScanJob = std::make_shared<CWindowsMusicAppScanJob>();
		if(Engine()->TryAddJob(pScanJob))
			m_pScanJob = std::move(pScanJob);
	}
	catch(const std::bad_alloc &)
	{
		// 保留上次检测结果，下一周期再重试。
	}
#else
	ApplyRunningApps(0);
#endif
}

void CQmMusicAppWatcher::ApplyRunningApps(uint64_t RunningMask)
{
	size_t HookCount = 0;
	const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);

	// 只在「应用启动/退出」事件发生时切换;首个 tick 视为事件,
	// 让客户端启动时也能跟随已经在运行的音乐应用。
	if(m_Initialized && RunningMask == m_PrevRunningMask)
		return;
	m_Initialized = true;
	m_PrevRunningMask = RunningMask;

	// 同时运行多个音乐应用时不做切换,避免在两个来源之间来回抖动。
	int RunningIndex = -1;
	for(size_t i = 0; i < HookCount; ++i)
	{
		if((RunningMask & ((uint64_t)1 << i)) != 0)
		{
			if(RunningIndex != -1)
				return;
			RunningIndex = (int)i;
		}
	}
	if(RunningIndex < 0)
		return; // 没有音乐应用在运行

	// 全部 Hook 都已关闭时不自动打开,尊重用户的显式关闭。
	int EnabledCount = 0;
	for(size_t i = 0; i < HookCount; ++i)
	{
		if(*apHooks[i].m_pEnableConfig != 0)
			++EnabledCount;
	}
	if(EnabledCount == 0)
		return;

	// 正在运行的应用已经在使用它的 Hook,无需切换。
	if(*apHooks[RunningIndex].m_pEnableConfig != 0)
		return;

	// 切换到正在运行的应用:关闭其它 Hook,打开它的 Hook。
	for(size_t i = 0; i < HookCount; ++i)
		*apHooks[i].m_pEnableConfig = (int)(i == (size_t)RunningIndex);
}
