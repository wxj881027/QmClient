#include "music_app_watcher.h"

#include "qm_music_hook_registry.h"

#include <base/system.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>
#endif

namespace
{
	constexpr int CHECK_INTERVAL_SECONDS = 2;

#if defined(CONF_FAMILY_WINDOWS)
	// 进程名大小写不敏感比较。
	bool ProcessNameEquals(const wchar_t *pLeft, const wchar_t *pRight)
	{
		for(;;)
		{
			const wchar_t A = *pLeft++;
			const wchar_t B = *pRight++;
			const wchar_t LowerA = (A >= L'A' && A <= L'Z') ? (wchar_t)(A - L'A' + L'a') : A;
			const wchar_t LowerB = (B >= L'A' && B <= L'Z') ? (wchar_t)(B - L'A' + L'a') : B;
			if(LowerA != LowerB)
				return false;
			if(LowerA == L'\0')
				return true;
		}
	}

	bool IsProcessRunning(const wchar_t *pTargetName)
	{
		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return false;
		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		bool Found = false;
		if(Process32FirstW(hSnapshot, &Entry))
		{
			do
			{
				if(ProcessNameEquals(Entry.szExeFile, pTargetName))
				{
					Found = true;
					break;
				}
			} while(Process32NextW(hSnapshot, &Entry));
		}
		CloseHandle(hSnapshot);
		return Found;
	}
#endif

	// 返回当前正在运行的已注册音乐应用的位掩码(按注册表下标)。
	uint64_t BuildRunningMask()
	{
		uint64_t Mask = 0;
#if defined(CONF_FAMILY_WINDOWS)
		size_t HookCount = 0;
		const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);
		for(size_t i = 0; i < HookCount; ++i)
		{
			if(apHooks[i].m_pProcessName != nullptr && IsProcessRunning(apHooks[i].m_pProcessName))
				Mask |= (uint64_t)1 << i;
		}
#endif
		return Mask;
	}
}

void CQmMusicAppWatcher::OnInit()
{
	m_Initialized = false;
	m_PrevRunningMask = 0;
	m_LastCheckTick = 0;
}

void CQmMusicAppWatcher::OnUpdate()
{
	const int64_t Now = time_get();
	if(Now - m_LastCheckTick < time_freq() * CHECK_INTERVAL_SECONDS)
		return;
	m_LastCheckTick = Now;
	CheckRunningApps();
}

void CQmMusicAppWatcher::CheckRunningApps()
{
	size_t HookCount = 0;
	const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);

	const uint64_t RunningMask = BuildRunningMask();

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
