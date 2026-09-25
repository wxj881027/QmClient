/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "client.h"

#include "demoedit.h"
#include "friends.h"
#include "perf_file_logger.h"
#include "serverbrowser.h"

#include <base/crashdump.h>
#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/logger.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/thread.h>
#include <base/windows.h>

#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/discord.h>
#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/external/json-parser/json.h>
#include <engine/favorites.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/notifications.h>
#include <engine/serverbrowser.h>
#include <engine/shared/assertion_logger.h>
#include <engine/shared/client_brand.h>
#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/demo.h>
#include <engine/shared/fifo.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/masterserver.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/protocolglue.h>
#include <engine/shared/qm_removed_config.h>
#include <engine/shared/rust_version.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>
#include <engine/sound.h>
#include <engine/steam.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>
#include <generated/protocolglue.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/frame_scheduler.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_VIDEORECORDER)
#include "video.h"
#endif

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#elif defined(CONF_PLATFORM_IOS)
#include <ios/ios_main.h>
#endif

#include "SDL.h"

namespace
{
}
#ifdef main
#undef main
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <stack>
#include <string>
#include <thread>
#include <tuple>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <dbghelp.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

using namespace std::chrono_literals;

static constexpr ColorRGBA gs_ClientNetworkPrintColor{0.7f, 1, 0.7f, 1.0f};
static constexpr ColorRGBA gs_ClientNetworkErrPrintColor{1.0f, 0.25f, 0.25f, 1.0f};
// 网络积压分帧处理，避免异常 burst 把整个渲染帧占满。
static constexpr int gs_NetworkPumpMaxChunksPerFrame = 256;
static constexpr std::chrono::nanoseconds gs_NetworkPumpOnlineBudget = 2ms;
static constexpr std::chrono::nanoseconds gs_NetworkPumpLoadingBudget = 6ms;
static constexpr int64_t gs_HangTimeoutSeconds = 10;
// QmClient: 退出兜底超时（秒）。正常退出通常 1-2 秒，超过该时间视为退出清理挂起。
static constexpr int64_t gs_ForcedExitTimeoutSeconds = 10;
static std::atomic<uint64_t> gs_ForcedExitWatchdogGeneration{0};
static std::atomic<bool> gs_ForcedExitWatchdogArmed{false};
// QmClient: 测试专用注入开关（--qm-test-main-thread-assert），供进程级回归测试
// 在真实客户端里触发一次主线程断言，验证弹窗期间看门狗的行为。
static bool gs_QmTestMainThreadAssert = false;
// QmClient: 测试专用注入开关（--qm-test-main-thread-stall），供进程级回归测试在
// 主循环内模拟一次长时间阻塞，验证看门狗使用单调时钟后能真实报告卡死。
static bool gs_QmTestMainThreadStall = false;
static constexpr const char *gs_pQmCrashDumpDir = "dumps/QmClient_Crash";
static constexpr const char *gs_pQmLifecycleMarkerFile = "qmclient/lifecycle_pending.marker";
static constexpr const char *gs_pQmGraphicsRecoveryStateFile = "qmclient/graphics_recovery.marker";
#if defined(CONF_FAMILY_WINDOWS)
static constexpr const char *gs_pQmCrashReporterBaselineFile = "qmclient/crash_reporter_started_at.marker";
#endif
static bool gs_aLoadedPreviousConfigPath[ConfigDomain::NUM] = {};

struct SQmLatestCrashReport
{
	char m_aPath[IO_MAX_PATH_LENGTH] = "";
	time_t m_TimeModified = 0;
	time_t m_MinTimeModified = 0;
	int m_Rank = 0;
};

static int QmCrashReportRank(const char *pName)
{
	if(str_endswith_nocase(pName, "_fatal_report.txt") != nullptr)
		return 2;
	if(str_endswith_nocase(pName, ".RTP") != nullptr)
		return 1;
	return 0;
}

static int FindLatestQmCrashReportCallback(const CFsFileInfo *pInfo, int IsDir, int Type, void *pUser)
{
	(void)Type;
	if(IsDir || pInfo == nullptr || pInfo->m_pName == nullptr)
		return 0;

	SQmLatestCrashReport *pLatest = static_cast<SQmLatestCrashReport *>(pUser);
	const int Rank = QmCrashReportRank(pInfo->m_pName);
	if(Rank == 0 || pInfo->m_TimeModified < pLatest->m_MinTimeModified)
		return 0;
	if(pLatest->m_aPath[0] != '\0' &&
		(pInfo->m_TimeModified < pLatest->m_TimeModified ||
			(pInfo->m_TimeModified == pLatest->m_TimeModified && Rank <= pLatest->m_Rank)))
	{
		return 0;
	}

	str_format(pLatest->m_aPath, sizeof(pLatest->m_aPath), "%s/%s", gs_pQmCrashDumpDir, pInfo->m_pName);
	pLatest->m_TimeModified = pInfo->m_TimeModified;
	pLatest->m_Rank = Rank;
	return 0;
}

// QmClient: 退出流程兜底。个别图形驱动会让进程停留在退出清理阶段不再响应，
// 用户只能手动结束进程（证据见 dumps/QmClient_Crash 下的 hang report）。
// 配置保存完成后启动本看门狗线程：清理若未在超时前解除看门狗，直接强制退出。
static void StartForcedExitWatchdog()
{
	gs_ForcedExitWatchdogArmed.store(true, std::memory_order_release);
	const uint64_t Generation = gs_ForcedExitWatchdogGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
	log_info("client", "shutdown watchdog armed: forcing exit if shutdown does not finish within %lld seconds", (long long)gs_ForcedExitTimeoutSeconds);
	std::thread([Generation]() {
		std::this_thread::sleep_for(std::chrono::seconds(gs_ForcedExitTimeoutSeconds));
		if(gs_ForcedExitWatchdogGeneration.load(std::memory_order_acquire) != Generation)
			return;
		// 使用 std::_Exit 跳过 atexit/静态析构，确保清理阶段卡死时也能结束进程。
		std::_Exit(0);
	}).detach();
}

static void StopForcedExitWatchdog()
{
	// 弹窗路径也会调用本函数（此时看门狗可能并未武装），因此只在真正解除时记录日志。
	const bool WasArmed = gs_ForcedExitWatchdogArmed.exchange(false, std::memory_order_acq_rel);
	gs_ForcedExitWatchdogGeneration.fetch_add(1, std::memory_order_release);
	if(WasArmed)
		log_info("client", "shutdown watchdog disarmed after cleanup completed");
}

#if defined(CONF_FAMILY_WINDOWS)
static bool IsQmCrashReporterFilename(const char *pName)
{
	return pName != nullptr &&
	       (str_endswith_nocase(pName, "_fatal_report.txt") != nullptr ||
		       (str_find_nocase(pName, "_hang_report_") != nullptr && str_endswith_nocase(pName, ".txt") != nullptr));
}

static bool IsQmCrashReporterPath(const char *pPath)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return false;

	char aNormalizedPath[IO_MAX_PATH_LENGTH];
	str_copy(aNormalizedPath, pPath);
	fs_normalize_path(aNormalizedPath);
	return !fs_is_relative_path(aNormalizedPath) &&
	       str_find_nocase(aNormalizedPath, "/dumps/QmClient_Crash/") != nullptr &&
	       IsQmCrashReporterFilename(fs_filename(aNormalizedPath));
}

static bool MarkQmCrashReportShown(const char *pReportPath)
{
	char aMarkerPath[IO_MAX_PATH_LENGTH + 16];
	str_format(aMarkerPath, sizeof(aMarkerPath), "%s.shown", pReportPath);
	IOHANDLE File = io_open(aMarkerPath, IOFLAG_WRITE);
	if(!File)
		return false;

	char aTimestamp[64];
	str_format(aTimestamp, sizeof(aTimestamp), "%lld\n", (long long)time_timestamp());
	const bool Success = io_write(File, aTimestamp, str_length(aTimestamp)) == str_length(aTimestamp);
	io_close(File);
	return Success;
}

static bool ShowQmCrashReporterDialog(const char *pReportPath)
{
	if(!IsQmCrashReporterPath(pReportPath))
		return false;

	IOHANDLE File = io_open(pReportPath, IOFLAG_READ);
	if(!File)
		return false;
	const int64_t ReportLength = io_length(File);
	if(ReportLength < 0 || ReportLength > 2 * 1024 * 1024)
	{
		io_close(File);
		return false;
	}
	char *pReport = io_read_all_str(File);
	io_close(File);
	if(pReport == nullptr)
		return false;

	const std::vector<IGraphics::CMessageBoxButton> vButtons = {
		{.m_pLabel = "Show crash reports"},
		{.m_pLabel = "Close report", .m_Confirm = true, .m_Cancel = true},
	};
	const std::optional<int> Result = ShowMessageBoxWithoutGraphics({
		.m_pTitle = "QmClient Crash Report",
		.m_pMessage = pReport,
		.m_Style = IGraphics::EMessageBoxStyle::QM_FESTIVE,
		.m_vButtons = vButtons,
	});
	free(pReport);
	if(!Result.has_value())
		return false;

	if(!MarkQmCrashReportShown(pReportPath))
		log_warn("crash_reporter", "failed to mark report as shown: %s", pReportPath);
	if(*Result == 0)
	{
		char aReportDirectory[IO_MAX_PATH_LENGTH];
		str_copy(aReportDirectory, pReportPath);
		if(fs_parent_dir(aReportDirectory) == 0)
			open_file(aReportDirectory);
	}
	return true;
}

struct SQmPendingCrashReportSearch
{
	IStorage *m_pStorage = nullptr;
	int64_t m_StartedAt = 0;
	char m_aPath[IO_MAX_PATH_LENGTH] = "";
	time_t m_TimeModified = 0;
};

static int FindPendingQmCrashReportCallback(const CFsFileInfo *pInfo, int IsDir, int Type, void *pUser)
{
	(void)Type;
	if(IsDir || pInfo == nullptr || !IsQmCrashReporterFilename(pInfo->m_pName))
		return 0;

	SQmPendingCrashReportSearch *pSearch = static_cast<SQmPendingCrashReportSearch *>(pUser);
	if((int64_t)pInfo->m_TimeModified < pSearch->m_StartedAt || pInfo->m_TimeModified < pSearch->m_TimeModified)
		return 0;

	char aRelativePath[IO_MAX_PATH_LENGTH];
	str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", gs_pQmCrashDumpDir, pInfo->m_pName);
	// fatal signal 路径不能在信号处理器中创建子进程，因此这类报告没有
	// .confirmed 标记；报告写入已完成，启动时统一按时间基线展示。
	char aShownMarkerPath[IO_MAX_PATH_LENGTH + 16];
	str_format(aShownMarkerPath, sizeof(aShownMarkerPath), "%s.shown", aRelativePath);
	if(pSearch->m_pStorage->FileExists(aShownMarkerPath, IStorage::TYPE_SAVE))
		return 0;

	str_copy(pSearch->m_aPath, aRelativePath);
	pSearch->m_TimeModified = pInfo->m_TimeModified;
	return 0;
}

static bool ReadQmCrashReporterStartedAt(IStorage *pStorage, int64_t &StartedAt)
{
	StartedAt = 0;
	char *pState = pStorage->ReadFileStr(gs_pQmCrashReporterBaselineFile, IStorage::TYPE_SAVE);
	if(pState == nullptr)
		return false;
	StartedAt = str_toint64_base(pState);
	free(pState);
	return StartedAt > 0;
}

static bool WriteQmCrashReporterStartedAt(IStorage *pStorage, int64_t StartedAt)
{
	pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(gs_pQmCrashReporterBaselineFile, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aTimestamp[64];
	str_format(aTimestamp, sizeof(aTimestamp), "%lld\n", (long long)StartedAt);
	const bool Success = io_write(File, aTimestamp, str_length(aTimestamp)) == str_length(aTimestamp);
	io_close(File);
	return Success;
}

static void ShowPendingQmCrashReport(IStorage *pStorage)
{
	int64_t StartedAt = 0;
	if(!ReadQmCrashReporterStartedAt(pStorage, StartedAt))
	{
		// 首次启用时只建立基线，不对更新前的历史报告重复弹窗。
		WriteQmCrashReporterStartedAt(pStorage, time_timestamp());
		return;
	}

	SQmPendingCrashReportSearch Search;
	Search.m_pStorage = pStorage;
	Search.m_StartedAt = StartedAt;
	pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, gs_pQmCrashDumpDir, FindPendingQmCrashReportCallback, &Search);
	if(Search.m_aPath[0] == '\0')
		return;

	// 默认不打扰：报告仍保留在 dumps/QmClient_Crash 供反馈问题时取用，打开开关才在启动时弹窗
	if(g_Config.m_QmCrashReportOnStartup == 0)
	{
		log_info("crash_reporter", "pending crash report kept on disk: %s (set qm_crash_report_on_startup 1 to show it at startup)", Search.m_aPath);
		return;
	}

	char aAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, Search.m_aPath, aAbsolutePath, sizeof(aAbsolutePath));
	// 启动阶段只负责拉起独立报告进程，不能在这里进入报告窗口的消息循环，
	// 否则用户必须先关闭报告窗口，客户端才会继续启动。
	if(!crashdump_launch_reporter_if_available(aAbsolutePath))
		log_warn("crash_reporter", "failed to launch pending report '%s'; client startup will continue", aAbsolutePath);
}
#endif

static bool ReadQmLifecycleMarkerStartedAt(IStorage *pStorage, int64_t &StartedAt)
{
	StartedAt = 0;
	char *pMarker = pStorage->ReadFileStr(gs_pQmLifecycleMarkerFile, IStorage::TYPE_SAVE);
	if(pMarker == nullptr)
		return false;

	char aLine[256];
	const char *pStr = pMarker;
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		if(const char *pValue = str_startswith(aLine, "started_at="))
		{
			StartedAt = str_toint64_base(pValue);
			break;
		}
	}
	free(pMarker);
	return StartedAt > 0;
}

// 图形崩溃恢复状态：记录触发恢复的崩溃报告指纹和被安全设置覆盖前的用户偏好，
// 用于在下一次启动时把用户偏好还回去，避免恢复逻辑永久改写用户配置。
struct SQmGraphicsRecoveryState
{
	int64_t m_ReportTimeModified = 0;
	char m_aReportPath[IO_MAX_PATH_LENGTH] = "";
	int m_Mode = 0;
	char m_aBackend[256] = "";
	char m_aRecoveryBackend[32] = "OpenGL";
	char m_aFailedBackend[32] = "";
	int m_GLMajor = 0;
	int m_GLMinor = 0;
	int m_GLPatch = 0;
	int m_FsaaSamples = 0;
	int m_Fullscreen = 0;
	int m_Borderless = 0;
	int m_3DTextureAnalysisRan = 0;
	int m_DriverIsBlocked = 0;
	bool m_HasFullPreference = false;
	graphics_backend::SRecoveryFailures m_Failures;
	bool m_Applied = false;
};

static bool ReadQmGraphicsRecoveryState(IStorage *pStorage, SQmGraphicsRecoveryState &State)
{
	State = {};
	char *pState = pStorage->ReadFileStr(gs_pQmGraphicsRecoveryStateFile, IStorage::TYPE_SAVE);
	if(pState == nullptr)
		return false;

	char aLine[IO_MAX_PATH_LENGTH + 64];
	const char *pStr = pState;
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		if(const char *pValue = str_startswith(aLine, "report_time="))
			State.m_ReportTimeModified = str_toint64_base(pValue);
		else if(const char *pValue = str_startswith(aLine, "report_path="))
			str_copy(State.m_aReportPath, pValue);
		else if(const char *pValue = str_startswith(aLine, "mode="))
			State.m_Mode = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "backend="))
			str_copy(State.m_aBackend, pValue);
		else if(const char *pValue = str_startswith(aLine, "recovery_backend="))
			str_copy(State.m_aRecoveryBackend, pValue);
		else if(const char *pValue = str_startswith(aLine, "failed_backend="))
			str_copy(State.m_aFailedBackend, pValue);
		else if(const char *pValue = str_startswith(aLine, "gl_major="))
			State.m_GLMajor = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "gl_minor="))
			State.m_GLMinor = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "gl_patch="))
			State.m_GLPatch = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "fsaa_samples="))
			State.m_FsaaSamples = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "fullscreen="))
			State.m_Fullscreen = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "borderless="))
			State.m_Borderless = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "analysis_ran="))
			State.m_3DTextureAnalysisRan = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "driver_blocked="))
			State.m_DriverIsBlocked = str_toint_base(pValue, 10);
		else if(const char *pValue = str_startswith(aLine, "pref_complete="))
			State.m_HasFullPreference = str_toint_base(pValue, 10) != 0;
		else if(const char *pValue = str_startswith(aLine, "failed_opengl="))
			State.m_Failures.m_aCount[BACKEND_TYPE_OPENGL] = std::max(str_toint_base(pValue, 10), 0);
		else if(const char *pValue = str_startswith(aLine, "failed_gles="))
			State.m_Failures.m_aCount[BACKEND_TYPE_OPENGL_ES] = std::max(str_toint_base(pValue, 10), 0);
		else if(const char *pValue = str_startswith(aLine, "failed_vulkan="))
			State.m_Failures.m_aCount[BACKEND_TYPE_VULKAN] = std::max(str_toint_base(pValue, 10), 0);
		else if(const char *pValue = str_startswith(aLine, "failed_metal="))
			State.m_Failures.m_aCount[BACKEND_TYPE_METAL] = std::max(str_toint_base(pValue, 10), 0);
		else if(const char *pValue = str_startswith(aLine, "applied="))
			State.m_Applied = str_toint_base(pValue, 10) != 0;
	}
	free(pState);
	return State.m_ReportTimeModified > 0 && State.m_aReportPath[0] != '\0';
}

static bool WriteQmGraphicsRecoveryState(IStorage *pStorage, const SQmGraphicsRecoveryState &State)
{
	pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(gs_pQmGraphicsRecoveryStateFile, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aBuf[IO_MAX_PATH_LENGTH + 1280];
	str_format(aBuf, sizeof(aBuf), "report_time=%lld\nreport_path=%s\nmode=%d\nbackend=%s\nrecovery_backend=%s\nfailed_backend=%s\n"
				       "gl_major=%d\ngl_minor=%d\ngl_patch=%d\nfsaa_samples=%d\nfullscreen=%d\nborderless=%d\nanalysis_ran=%d\ndriver_blocked=%d\npref_complete=%d\n"
				       "failed_opengl=%d\nfailed_gles=%d\nfailed_vulkan=%d\nfailed_metal=%d\napplied=%d\n",
		(long long)State.m_ReportTimeModified, State.m_aReportPath, State.m_Mode, State.m_aBackend, State.m_aRecoveryBackend, State.m_aFailedBackend,
		State.m_GLMajor, State.m_GLMinor, State.m_GLPatch, State.m_FsaaSamples, State.m_Fullscreen, State.m_Borderless,
		State.m_3DTextureAnalysisRan, State.m_DriverIsBlocked, State.m_HasFullPreference ? 1 : 0,
		State.m_Failures.m_aCount[BACKEND_TYPE_OPENGL], State.m_Failures.m_aCount[BACKEND_TYPE_OPENGL_ES],
		State.m_Failures.m_aCount[BACKEND_TYPE_VULKAN], State.m_Failures.m_aCount[BACKEND_TYPE_METAL], State.m_Applied ? 1 : 0);
	const bool Success = io_write(File, aBuf, str_length(aBuf)) == str_length(aBuf);
	io_close(File);
	return Success;
}

static bool QmCrashTextHasGraphicsDriverFault(const char *pText)
{
	if(pText == nullptr || pText[0] == '\0')
		return false;
	if(str_find(pText, "Report type: graphics_fatal_error\n") != nullptr)
		return true;

	static constexpr const char *s_apGraphicsDriverFaults[] = {
		"Exception module: nvoglv64.dll",
		" in module nvoglv64.dll",
		"Exception module: nvd3dumx.dll",
		" in module nvd3dumx.dll",
		"Exception module: nvwgf2umx.dll",
		" in module nvwgf2umx.dll",
		"Exception module: amdvlk64.dll",
		" in module amdvlk64.dll",
		"Exception module: atio6axx.dll",
		" in module atio6axx.dll",
		"Exception module: ig9icd64.dll",
		" in module ig9icd64.dll",
		"Exception module: igvk64.dll",
		" in module igvk64.dll",
		"Exception module: opengl32.dll",
		" in module opengl32.dll",
		"Exception module: vulkan-1.dll",
		" in module vulkan-1.dll",
		"Exception module: D3D12Core.dll",
		" in module D3D12Core.dll",
		"Exception module: d3d12.dll",
		" in module d3d12.dll",
		"Exception module: dxgi.dll",
		" in module dxgi.dll",
	};
	for(const char *pNeedle : s_apGraphicsDriverFaults)
	{
		if(str_find_nocase(pText, pNeedle) != nullptr)
			return true;
	}
	return false;
}

static bool ApplyQmSafeGraphicsRecovery(EBackendType RecoveryBackend)
{
	const auto SafeConfig = graphics_backend::SafeBackendConfig();
	const int RecoveryFullscreen = graphics_backend::RecoveryFullscreenMode(g_Config.m_GfxFullscreen);
	bool Changed = false;
	// 图形设备已丢失后，下一次启动必须真正绕开触发故障的后端。
	// InitWindow 会按模式覆盖后端，恢复模式必须与候选后端一致。
	const char *pRecoveryBackend = graphics_backend::BackendName(RecoveryBackend);
	if(str_comp_nocase(g_Config.m_GfxBackend, pRecoveryBackend) != 0)
	{
		str_copy(g_Config.m_GfxBackend, pRecoveryBackend);
		Changed = true;
	}
	const int RecoveryMode = graphics_backend::ModeForRecoveryBackend(RecoveryBackend);
	if(g_Config.m_QmGraphicsMode != RecoveryMode)
	{
		g_Config.m_QmGraphicsMode = RecoveryMode;
		Changed = true;
	}
	if(g_Config.m_GfxGLMajor != SafeConfig.m_GLMajor || g_Config.m_GfxGLMinor != SafeConfig.m_GLMinor || g_Config.m_GfxGLPatch != SafeConfig.m_GLPatch)
	{
		g_Config.m_GfxGLMajor = SafeConfig.m_GLMajor;
		g_Config.m_GfxGLMinor = SafeConfig.m_GLMinor;
		g_Config.m_GfxGLPatch = SafeConfig.m_GLPatch;
		Changed = true;
	}
	if(g_Config.m_GfxFsaaSamples != SafeConfig.m_FsaaSamples)
	{
		g_Config.m_GfxFsaaSamples = SafeConfig.m_FsaaSamples;
		Changed = true;
	}
	if(g_Config.m_GfxFullscreen != RecoveryFullscreen)
	{
		g_Config.m_GfxFullscreen = RecoveryFullscreen;
		Changed = true;
	}
	if(RecoveryFullscreen == 0 && g_Config.m_GfxBorderless != SafeConfig.m_Borderless)
	{
		g_Config.m_GfxBorderless = SafeConfig.m_Borderless;
		Changed = true;
	}
	if(g_Config.m_Gfx3DTextureAnalysisRan != 0)
	{
		g_Config.m_Gfx3DTextureAnalysisRan = 0;
		Changed = true;
	}
	if(g_Config.m_GfxDriverIsBlocked != 0)
	{
		g_Config.m_GfxDriverIsBlocked = 0;
		Changed = true;
	}
	return Changed;
}

static bool QmGraphicsRecoverySettingsUntouched(const SQmGraphicsRecoveryState &State)
{
	const auto SafeConfig = graphics_backend::SafeBackendConfig();
	const EBackendType RecoveryBackend = graphics_backend::ParseBackendName(State.m_aRecoveryBackend, BACKEND_TYPE_AUTO);
	return g_Config.m_QmGraphicsMode == graphics_backend::ModeForRecoveryBackend(RecoveryBackend) &&
	       str_comp_nocase(g_Config.m_GfxBackend, State.m_aRecoveryBackend) == 0 &&
	       (!State.m_HasFullPreference ||
		       (g_Config.m_GfxFsaaSamples == SafeConfig.m_FsaaSamples &&
			       g_Config.m_GfxFullscreen == graphics_backend::RecoveryFullscreenMode(State.m_Fullscreen) &&
			       (g_Config.m_GfxFullscreen != 0 || g_Config.m_GfxBorderless == SafeConfig.m_Borderless)));
}

static bool RecoverQmGraphicsSettingsAfterDriverCrash(IStorage *pStorage)
{
	if(pStorage == nullptr)
		return true;

	SQmGraphicsRecoveryState State;
	const bool HasState = ReadQmGraphicsRecoveryState(pStorage, State);

	// 崩溃基线：lifecycle marker 只在上一次会话异常结束时保留，其 started_at
	// 是异常会话的启动时间，用于圈定本次需要处理的崩溃报告。
	char aLatestReport[IO_MAX_PATH_LENGTH] = "";
	time_t LatestReportTime = 0;
	if(pStorage->FileExists(gs_pQmLifecycleMarkerFile, IStorage::TYPE_SAVE))
	{
		int64_t SessionStartedAt = 0;
		if(ReadQmLifecycleMarkerStartedAt(pStorage, SessionStartedAt))
		{
			SQmLatestCrashReport Latest;
			Latest.m_MinTimeModified = (time_t)SessionStartedAt;
			pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, gs_pQmCrashDumpDir, FindLatestQmCrashReportCallback, &Latest);
			str_copy(aLatestReport, Latest.m_aPath);
			LatestReportTime = Latest.m_TimeModified;
		}
	}

	const bool IsRecoveredReport = HasState &&
				       State.m_ReportTimeModified == (int64_t)LatestReportTime &&
				       str_comp(State.m_aReportPath, aLatestReport) == 0;

	// 新的图形驱动崩溃：把用户当前图形偏好记入恢复状态，本次启动使用安全设置；
	// 下一次启动（无新图形崩溃时）自动还回用户偏好，不再永久改写用户配置。
	if(aLatestReport[0] != '\0' && !IsRecoveredReport)
	{
		char *pCrashReport = pStorage->ReadFileStr(aLatestReport, IStorage::TYPE_SAVE);
		if(pCrashReport != nullptr)
		{
			const bool HasGraphicsDriverFault = QmCrashTextHasGraphicsDriverFault(pCrashReport);
			if(HasGraphicsDriverFault)
			{
				EBackendType CrashedBackend = graphics_backend::BackendFromCrashReport(pCrashReport);
				if(CrashedBackend == BACKEND_TYPE_AUTO)
					CrashedBackend = graphics_backend::ParseBackendName(g_Config.m_GfxBackend, BACKEND_TYPE_AUTO);
				free(pCrashReport);
				SQmGraphicsRecoveryState NewState = HasState ? State : SQmGraphicsRecoveryState{};
				NewState.m_ReportTimeModified = (int64_t)LatestReportTime;
				str_copy(NewState.m_aReportPath, aLatestReport);
				if(!HasState || !State.m_Applied ||
					!QmGraphicsRecoverySettingsUntouched(State))
				{
					NewState.m_Mode = g_Config.m_QmGraphicsMode;
					str_copy(NewState.m_aBackend, g_Config.m_GfxBackend);
					NewState.m_GLMajor = g_Config.m_GfxGLMajor;
					NewState.m_GLMinor = g_Config.m_GfxGLMinor;
					NewState.m_GLPatch = g_Config.m_GfxGLPatch;
					NewState.m_FsaaSamples = g_Config.m_GfxFsaaSamples;
					NewState.m_Fullscreen = g_Config.m_GfxFullscreen;
					NewState.m_Borderless = g_Config.m_GfxBorderless;
					NewState.m_3DTextureAnalysisRan = g_Config.m_Gfx3DTextureAnalysisRan;
					NewState.m_DriverIsBlocked = g_Config.m_GfxDriverIsBlocked;
					NewState.m_HasFullPreference = true;
				}
				NewState.m_Failures.Record(CrashedBackend);
				str_copy(NewState.m_aFailedBackend, graphics_backend::BackendName(CrashedBackend));
				const EBackendType Candidate = graphics_backend::RecoveryBackend(NewState.m_Failures, CrashedBackend);
				NewState.m_Applied = Candidate != BACKEND_TYPE_AUTO;
				if(NewState.m_Applied)
				{
					str_copy(NewState.m_aRecoveryBackend, graphics_backend::BackendName(Candidate));
					ApplyQmSafeGraphicsRecovery(Candidate);
				}
				if(WriteQmGraphicsRecoveryState(pStorage, NewState))
				{
					log_warn("client", "previous graphics crash '%s' on backend '%s'; recovery backend '%s', user preference (mode=%d backend='%s')",
						aLatestReport, graphics_backend::BackendName(CrashedBackend),
						NewState.m_Applied ? NewState.m_aRecoveryBackend : "(none available)", NewState.m_Mode, NewState.m_aBackend);
				}
				else
				{
					log_warn("client", "failed to write graphics recovery state");
				}
				// 无候选时不再带着同一故障配置进入图形初始化。
				return NewState.m_Applied;
			}
			free(pCrashReport);
		}
	}

	if(HasState && !State.m_Applied && State.m_aFailedBackend[0] != '\0' &&
		str_comp_nocase(g_Config.m_GfxBackend, State.m_aFailedBackend) == 0 &&
		graphics_backend::RecoveryBackend(State.m_Failures, graphics_backend::ParseBackendName(State.m_aFailedBackend, BACKEND_TYPE_AUTO)) == BACKEND_TYPE_AUTO)
		return false;

	// 上一次启动执行过安全恢复且本次没有发现新的图形崩溃：把用户偏好还回去。
	// 用户若已在安全会话中自行修改了图形设置（与安全值不一致），尊重用户改动。
	if(HasState && State.m_Applied)
	{
		const bool UntouchedByUser = QmGraphicsRecoverySettingsUntouched(State);
		const bool PreferenceDiffers = State.m_Mode != g_Config.m_QmGraphicsMode ||
					       str_comp_nocase(State.m_aBackend, g_Config.m_GfxBackend) != 0 || State.m_HasFullPreference;
		if(UntouchedByUser && PreferenceDiffers && !State.m_Failures.IsBlocked(graphics_backend::ParseBackendName(State.m_aBackend, BACKEND_TYPE_AUTO)))
		{
			g_Config.m_QmGraphicsMode = State.m_Mode;
			str_copy(g_Config.m_GfxBackend, State.m_aBackend);
			if(State.m_HasFullPreference)
			{
				g_Config.m_GfxGLMajor = State.m_GLMajor;
				g_Config.m_GfxGLMinor = State.m_GLMinor;
				g_Config.m_GfxGLPatch = State.m_GLPatch;
				g_Config.m_GfxFsaaSamples = State.m_FsaaSamples;
				g_Config.m_GfxFullscreen = State.m_Fullscreen;
				g_Config.m_GfxBorderless = State.m_Borderless;
				g_Config.m_Gfx3DTextureAnalysisRan = State.m_3DTextureAnalysisRan;
				g_Config.m_GfxDriverIsBlocked = State.m_DriverIsBlocked;
			}
			log_info("client", "restoring user graphics preference after safe recovery launch: mode=%d backend='%s'", State.m_Mode, State.m_aBackend);
		}
		State.m_Applied = false;
		if(!WriteQmGraphicsRecoveryState(pStorage, State))
			log_warn("client", "failed to persist graphics recovery failure counts");
	}
	return true;
}

static const char *ClientStateToString(int State)
{
	switch(State)
	{
	case IClient::STATE_OFFLINE:
		return "offline";
	case IClient::STATE_CONNECTING:
		return "connecting";
	case IClient::STATE_LOADING:
		return "loading";
	case IClient::STATE_ONLINE:
		return "online";
	case IClient::STATE_DEMOPLAYBACK:
		return "demoplayback";
	case IClient::STATE_QUITTING:
		return "quitting";
	case IClient::STATE_RESTARTING:
		return "restarting";
	default:
		return "unknown";
	}
}

#if defined(CONF_FAMILY_WINDOWS)
static bool WriteMiniDumpFile(const char *pFilename)
{
	using MiniDumpWriteDumpFunc = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
		const MINIDUMP_EXCEPTION_INFORMATION *, const MINIDUMP_USER_STREAM_INFORMATION *, const MINIDUMP_CALLBACK_INFORMATION *);

	HMODULE pDbgHelp = LoadLibraryA("dbghelp.dll");
	if(pDbgHelp == nullptr)
		return false;

	auto pMiniDumpWriteDump = (MiniDumpWriteDumpFunc)GetProcAddress(pDbgHelp, "MiniDumpWriteDump");
	if(pMiniDumpWriteDump == nullptr)
	{
		FreeLibrary(pDbgHelp);
		return false;
	}

	HANDLE FileHandle = CreateFileA(pFilename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(FileHandle == INVALID_HANDLE_VALUE)
	{
		FreeLibrary(pDbgHelp);
		return false;
	}

	const MINIDUMP_TYPE DumpType = MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
	const BOOL Result = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, DumpType, nullptr, nullptr, nullptr);

	CloseHandle(FileHandle);
	FreeLibrary(pDbgHelp);
	return Result != FALSE;
}

// QmClient 测试专用：阻塞主线程指定时长，同时泵窗口消息使窗口保持"响应"状态，
// 避免 Windows 幽灵窗口机制打扰桌面。看门狗心跳在此期间照旧停滞，不影响被测
// 行为（已实测： pump 不能消除退出清理阶段 NVIDIA ICD 的访问违例，那是驱动
// 内部问题，由 crashdump 的退出期驱动故障忽略策略兜底）。
static void QmTestStallPumpWindowMessages(std::chrono::nanoseconds Duration)
{
	const auto Deadline = std::chrono::steady_clock::now() + Duration;
	MSG Message;
	while(std::chrono::steady_clock::now() < Deadline)
	{
		while(PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
		std::this_thread::sleep_for(10ms);
	}
}
#endif

CSnapshotDelta *CClient::SnapshotDelta()
{
	if(IsSixup())
	{
		return &*m_pSnapshotDeltaSixup;
	}
	return &*m_pSnapshotDelta;
}

CClient::CClient() :
	m_pSnapshotDelta(CSnapshotDelta::New()),
	m_pSnapshotDeltaSixup(CSnapshotDelta::New()),
	m_DemoPlayer(&*m_pSnapshotDelta, &*m_pSnapshotDeltaSixup, true, [&]() { UpdateDemoIntraTimers(); }),
	m_InputtimeMarginGraph(128, 2, true),
	m_aGametimeMarginGraphs{{128, 2, true}, {128, 2, true}},
	m_FpsGraph(4096, 0, true)
{
	m_StateStartTime = time_get();
	for(auto &DemoRecorder : m_aDemoRecorders)
		DemoRecorder = CDemoRecorder(&*m_pSnapshotDelta);
	for(auto &DemoRecorder : m_aDemoRecordersSixup)
		DemoRecorder = CDemoRecorder(&*m_pSnapshotDeltaSixup);
	m_LastRenderTime = time_get();
	mem_zero(m_aInputs, sizeof(m_aInputs));
	mem_zero(m_aapSnapshots, sizeof(m_aapSnapshots));
	for(auto &SnapshotStorage : m_aSnapshotStorage)
		SnapshotStorage.Init();
	mem_zero(m_aDemorecSnapshotHolders, sizeof(m_aDemorecSnapshotHolders));
	m_CurrentServerInfo = {};
	mem_zero(&m_Checksum, sizeof(m_Checksum));
	for(auto &GameTime : m_aGameTime)
		GameTime.Init(0);
	m_PredictedTime.Init(0);

	m_Sixup = false;
}

// ----- send functions -----
static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer, bool Sixup)
{
	int MsgId = pMsg->m_MsgId;
	Packer.Reset();

	if(Sixup && !pMsg->m_NoTranslate)
	{
		if(pMsg->m_System)
		{
			if(MsgId >= OFFSET_UUID)
			{
				;
			}
			else if(MsgId == NETMSG_INFO || MsgId == NETMSG_REQUEST_MAP_DATA)
			{
				;
			}
			else if(MsgId == NETMSG_READY)
			{
				MsgId = protocol7::NETMSG_READY;
			}
			else if(MsgId == NETMSG_RCON_CMD)
			{
				MsgId = protocol7::NETMSG_RCON_CMD;
			}
			else if(MsgId == NETMSG_ENTERGAME)
			{
				MsgId = protocol7::NETMSG_ENTERGAME;
			}
			else if(MsgId == NETMSG_INPUT)
			{
				MsgId = protocol7::NETMSG_INPUT;
			}
			else if(MsgId == NETMSG_RCON_AUTH)
			{
				MsgId = protocol7::NETMSG_RCON_AUTH;
			}
			else if(MsgId == NETMSG_PING)
			{
				MsgId = protocol7::NETMSG_PING;
			}
			else
			{
				log_error("net", "0.7 DROP send sys %d", MsgId);
				return false;
			}
		}
		else
		{
			if(MsgId >= 0 && MsgId < OFFSET_UUID)
				MsgId = Msg_SixToSeven(MsgId);

			if(MsgId < 0)
				return false;
		}
	}

	if(pMsg->m_MsgId < OFFSET_UUID)
	{
		Packer.AddInt((MsgId << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt(pMsg->m_System ? 1 : 0); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(pMsg->m_MsgId, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return true;
}

int CClient::SendMsg(int Conn, CMsgPacker *pMsg, int Flags)
{
	CNetChunk Packet;

	if(State() == IClient::STATE_OFFLINE)
		return 0;

	// repack message (inefficient)
	CPacker Pack;
	if(!RepackMsg(pMsg, Pack, IsSixup()))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientId = 0;
	Packet.m_pData = Pack.Data();
	Packet.m_DataSize = Pack.Size();

	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if((Flags & MSGFLAG_RECORD) && Conn == g_Config.m_ClDummy)
	{
		for(auto &DemoRecorder : DemoRecorders())
		{
			if(DemoRecorder.IsRecording())
			{
				DemoRecorder.RecordMessage(Packet.m_pData, Packet.m_DataSize);
			}
		}
	}

	if(!(Flags & MSGFLAG_NOSEND))
	{
		m_aNetClient[Conn].Send(&Packet);
	}

	return 0;
}

int CClient::SendMsgActive(CMsgPacker *pMsg, int Flags)
{
	return SendMsg(g_Config.m_ClDummy, pMsg, Flags);
}

void CClient::SendTClientInfo(int Conn)
{
	CMsgPacker Msg(NETMSG_IAMTATER, true);
	Msg.AddString(QMCLIENT_VERSION " built on " __DATE__ ", " __TIME__);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CClient::SendInfo(int Conn)
{
	// 静默断开后的同端口重连会保留服务器游戏态，因此先用游戏层消息恢复 DDNet 版本。
	CMsgPacker MsgLegacyVersion(NETMSGTYPE_CL_ISDDNETLEGACY, false);
	MsgLegacyVersion.AddInt(GameClient()->DDNetVersion());
	SendMsg(Conn, &MsgLegacyVersion, MSGFLAG_VITAL);

	SendTClientInfo(Conn);

	CMsgPacker MsgVer(NETMSG_CLIENTVER, true);
	MsgVer.AddRaw(&m_ConnectionId, sizeof(m_ConnectionId));
	MsgVer.AddInt(GameClient()->DDNetVersion());
	MsgVer.AddString(GameClient()->DDNetVersionStr());
	SendMsg(Conn, &MsgVer, MSGFLAG_VITAL);

	if(IsSixup())
	{
		CMsgPacker Msg(NETMSG_INFO, true);
		Msg.AddString(GAME_NETVERSION7, 128);
		Msg.AddString(Config()->m_Password);
		Msg.AddInt(GameClient()->ClientVersion7());
		SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
		return;
	}

	CMsgPacker Msg(NETMSG_INFO, true);
	Msg.AddString(GameClient()->NetVersion());
	Msg.AddString(m_aPassword);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	SendKcpCapability(Conn);
}

void CClient::SendKcpCapability(int Conn)
{
	if(Conn != CONN_MAIN)
		return;
	CMsgPacker Msg(NETMSG_KCP_CAPABLE, true);
	Msg.AddInt(1); // negotiation version
	Msg.AddInt(NET_MAX_PACKETSIZE);
	Msg.AddInt(NET_MAX_CONNLESS_PAYLOAD);
	Msg.AddInt(Conn == CONN_DUMMY ? 1 : 0);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	if(g_Config.m_Debug)
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "sent kcp capability", gs_ClientNetworkPrintColor);
	}

	m_KcpNegotiationPending = true;
	m_KcpNegotiationStartTime = time_get();
}

void CClient::SendKcpProbe(int Conn)
{
	if(Conn != CONN_MAIN || !m_aNetClient[Conn].IsKcpActive())
		return;
	CMsgPacker Msg(NETMSG_KCP_CAPABLE, true);
	Msg.AddInt(1);
	Msg.AddInt(NET_MAX_PACKETSIZE);
	Msg.AddInt(NET_MAX_CONNLESS_PAYLOAD);
	Msg.AddInt(0);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendEnterGame(int Conn)
{
	CMsgPacker Msg(NETMSG_ENTERGAME, true);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendReady(int Conn)
{
	CMsgPacker Msg(NETMSG_READY, true);
	SendMsg(Conn, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendMapRequest()
{
	dbg_assert(!m_MapdownloadFileTemp, "Map download already in progress");
	m_MapdownloadFileTemp = Storage()->OpenFile(m_aMapdownloadFilenameTemp, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(IsSixup())
	{
		CMsgPacker MsgP(protocol7::NETMSG_REQUEST_MAP_DATA, true, true);
		SendMsg(CONN_MAIN, &MsgP, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	}
	else
	{
		CMsgPacker Msg(NETMSG_REQUEST_MAP_DATA, true);
		Msg.AddInt(m_MapdownloadChunk);
		SendMsg(CONN_MAIN, &Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	}
}

void CClient::RconAuth(const char *pName, const char *pPassword, bool Dummy)
{
	if(m_aRconAuthed[Dummy] != 0)
		return;

	if(pName != m_aRconUsername)
		str_copy(m_aRconUsername, pName);
	if(pPassword != m_aRconPassword)
		str_copy(m_aRconPassword, pPassword);

	if(IsSixup())
	{
		CMsgPacker Msg7(protocol7::NETMSG_RCON_AUTH, true, true);
		Msg7.AddString(pPassword);
		SendMsg(Dummy, &Msg7, MSGFLAG_VITAL);
		return;
	}

	CMsgPacker Msg(NETMSG_RCON_AUTH, true);
	Msg.AddString(pName);
	Msg.AddString(pPassword);
	Msg.AddInt(1);
	SendMsg(Dummy, &Msg, MSGFLAG_VITAL);
}

void CClient::Rcon(const char *pCmd)
{
	// TClient
	if(str_comp_nocase(pCmd, "clear") == 0)
	{
		m_pConsole->ExecuteLine("clear_remote_console", IConsole::CLIENT_ID_UNSPECIFIED);
		return;
	}
	CMsgPacker Msg(NETMSG_RCON_CMD, true);
	Msg.AddString(pCmd);
	SendMsgActive(&Msg, MSGFLAG_VITAL);
}

float CClient::GotRconCommandsPercentage() const
{
	if(m_ExpectedRconCommands <= 0)
		return -1.0f;
	if(m_GotRconCommands > m_ExpectedRconCommands)
		return -1.0f;

	return (float)m_GotRconCommands / (float)m_ExpectedRconCommands;
}

float CClient::GotMaplistPercentage() const
{
	if(m_ExpectedMaplistEntries <= 0)
		return -1.0f;
	if(m_vMaplistEntries.size() > (size_t)m_ExpectedMaplistEntries)
		return -1.0f;

	return (float)m_vMaplistEntries.size() / (float)m_ExpectedMaplistEntries;
}

bool CClient::ConnectionProblems() const
{
	return m_aNetClient[g_Config.m_ClDummy].GotProblems(MaxLatencyTicks() * time_freq() / GameTickSpeed());
}

float CClient::PacketLoss() const
{
	return m_aNetClient[g_Config.m_ClDummy].PacketLoss();
}

void CClient::UpdateNetStatsSnapshot() const
{
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	if(Now - m_NetstatsLastUpdate <= 1s)
		return;

	m_NetstatsSampleInterval = m_NetstatsLastUpdate.count() > 0 ? Now - m_NetstatsLastUpdate : std::chrono::nanoseconds::zero();
	m_NetstatsLastUpdate = Now;
	m_NetstatsPrev = m_NetstatsCurrent;
	net_stats(&m_NetstatsCurrent);
}

float CClient::PingMs() const
{
	const int Conn = g_Config.m_ClDummy;
	if(!IsGameConnectionAlive())
		return -1.0f;
	if(m_aNetClient[Conn].IsKcpActive())
		return (float)m_aNetClient[Conn].TransportStats().m_RttMs;
	return (float)m_aGamePingProbes[Conn].m_RttMs;
}

float CClient::PredictionLeadMs() const
{
	if(!IsGameConnectionAlive())
		return -1.0f;
	const int64_t Now = time_get();
	return (float)((m_PredictedTime.Get(Now) - m_aGameTime[g_Config.m_ClDummy].Get(Now)) * 1000 / (float)time_freq());
}

float CClient::PredictionMarginMs() const
{
	if(!IsGameConnectionAlive())
		return -1.0f;
	return (float)PredictionMargin();
}

float CClient::PredictionJitterMs() const
{
	if(!IsGameConnectionAlive())
		return -1.0f;
	return std::max(0.0f, m_AutoMarginLatencyJitterMs);
}

float CClient::GameTimeMarginMs() const
{
	return m_aLastGameTimeMarginMs[g_Config.m_ClDummy];
}

bool CClient::IsGameConnectionAlive() const
{
	if(State() != IClient::STATE_ONLINE)
		return false;
	if(g_Config.m_ClDummy == 0)
		return true;
	return m_DummyConnected && m_aNetClient[CONN_DUMMY].State() == NETSTATE_ONLINE;
}

void CClient::NetStatsSnapshot(NETSTATS &Prev, NETSTATS &Current, std::chrono::nanoseconds &LastUpdate) const
{
	UpdateNetStatsSnapshot();
	Prev = m_NetstatsPrev;
	Current = m_NetstatsCurrent;
	LastUpdate = m_NetstatsSampleInterval;
}

void CClient::UpdateGamePing()
{
	const int64_t Now = time_get();
	const int64_t Frequency = time_freq();
	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
	{
		SGamePingProbe &Probe = m_aGamePingProbes[Conn];
		if(Conn == CONN_MAIN && m_ManualPingProbe.HandleTimeout(Now, Frequency))
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client/network", "ping timeout");
		if(m_aNetClient[Conn].State() != NETSTATE_ONLINE || m_aNetClient[Conn].IsKcpActive())
		{
			Probe.Reset();
			continue;
		}

		if(Probe.m_StartTime >= 0)
		{
			if(!Probe.HandleTimeout(Now, Frequency))
				continue;
		}

		if(Probe.m_NextTime >= 0 && Now < Probe.m_NextTime)
			continue;

		if(m_ServerCapabilities.m_PingEx)
		{
			const CUuid Uuid = RandomUuid();
			CMsgPacker Msg(NETMSG_PINGEX, true);
			Msg.AddRaw(&Uuid, sizeof(Uuid));
			SendMsg(Conn, &Msg, MSGFLAG_FLUSH);
			Probe.Begin(Uuid, Now, Frequency);
		}
		else
		{
			CMsgPacker Msg(NETMSG_PING, true);
			SendMsg(Conn, &Msg, MSGFLAG_FLUSH);
			Probe.BeginLegacy(Now, Frequency);
		}
	}
}

void CClient::SnapshotStats(SClientSnapshotStats &Stats) const
{
	Stats = m_aSnapshotStats[g_Config.m_ClDummy];
	if(Stats.m_SnapshotCount == 0 || m_aLastSnapshotTime[g_Config.m_ClDummy] <= 0)
	{
		Stats.m_CurrentGapMs = -1.0f;
		return;
	}

	const int64_t Now = time_get();
	Stats.m_CurrentGapMs = std::max(0.0f, (float)(Now - m_aLastSnapshotTime[g_Config.m_ClDummy]) * 1000.0f / (float)time_freq());
}

int CClient::PendingResendCount() const
{
	return m_aNetClient[g_Config.m_ClDummy].PendingResendCount();
}

void CClient::SendInput()
{
	int64_t Now = time_get();

	if(m_aPredTick[g_Config.m_ClDummy] <= 0)
		return;

	bool Force = false;
	// fetch input
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		if(!DummyConnected() && Dummy != 0)
		{
			break;
		}
		int i = g_Config.m_ClDummy ^ Dummy;
		int Size = GameClient()->OnSnapInput(m_aInputs[i][m_aCurrentInput[i]].m_aData, Dummy, Force);

		if(Size)
		{
			int aSendData[MAX_INPUT_SIZE];
			dbg_assert(Size <= (int)sizeof(aSendData), "input size exceeds send buffer");
			mem_copy(aSendData, m_aInputs[i][m_aCurrentInput[i]].m_aData, (size_t)Size);
			GameClient()->PrepareInputForSend(aSendData, Size, Dummy);

			// pack input
			CMsgPacker Msg(NETMSG_INPUT, true);
			Msg.AddInt(m_aAckGameTick[i]);
			Msg.AddInt(m_aPredTick[g_Config.m_ClDummy]);
			Msg.AddInt(Size);

			m_aInputs[i][m_aCurrentInput[i]].m_Tick = m_aPredTick[g_Config.m_ClDummy];
			m_aInputs[i][m_aCurrentInput[i]].m_PredictedTime = m_PredictedTime.Get(Now);
			m_aInputs[i][m_aCurrentInput[i]].m_PredictionMargin = PredictionMargin() * time_freq() / 1000;
			if(g_Config.m_TcSmoothPredictionMargin)
				m_aInputs[i][m_aCurrentInput[i]].m_PredictionMargin = m_PredictedTime.GetMargin(Now);
			m_aInputs[i][m_aCurrentInput[i]].m_Time = Now;

			// pack it
			for(int k = 0; k < Size / 4; k++)
			{
				static const int FlagsOffset = offsetof(CNetObj_PlayerInput, m_PlayerFlags) / sizeof(int);
				if(k == FlagsOffset && IsSixup())
				{
					int PlayerFlags = aSendData[k];
					Msg.AddInt(PlayerFlags_SixToSeven(PlayerFlags));
				}
				else
				{
					Msg.AddInt(aSendData[k]);
				}
			}

			m_aCurrentInput[i]++;
			m_aCurrentInput[i] %= 200;

			SendMsg(i, &Msg, MSGFLAG_FLUSH);
			if(i == CONN_MAIN && m_aNetClient[i].IsKcpActive())
				SendMsg(i, &Msg, MSGFLAG_FLUSH);
			// ugly workaround for dummy. we need to send input with dummy to prevent
			// prediction time resets. but if we do it too often, then it's
			// impossible to use grenade with frozen dummy that gets hammered...
			if(g_Config.m_ClDummyCopyMoves || m_aCurrentInput[i] % 2)
				Force = true;
		}
	}
}

const char *CClient::LatestVersion() const
{
	return m_aVersionStr;
}

// TODO: OPT: do this a lot smarter!
int *CClient::GetInput(int Tick, int IsDummy) const
{
	int Best = -1;
	const int d = IsDummy ^ g_Config.m_ClDummy;
	for(int i = 0; i < 200; i++)
	{
		if(m_aInputs[d][i].m_Tick != -1 && m_aInputs[d][i].m_Tick <= Tick && (Best == -1 || m_aInputs[d][Best].m_Tick < m_aInputs[d][i].m_Tick))
			Best = i;
	}

	if(Best != -1)
		return (int *)m_aInputs[d][Best].m_aData;
	return nullptr;
}

// ------ state handling -----
void CClient::SetState(EClientState State)
{
	if(m_State == IClient::STATE_QUITTING || m_State == IClient::STATE_RESTARTING)
		return;
	if(m_State == State)
		return;

	if(g_Config.m_Debug)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "state change. last=%d current=%d", m_State, State);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
	}

	const EClientState OldState = m_State;
	m_State = State;

	m_StateStartTime = time_get();
	GameClient()->OnStateChange(m_State, OldState);

	if(State == IClient::STATE_OFFLINE && m_ReconnectTime == 0)
	{
		if(g_Config.m_ClReconnectFull > 0 && (str_find_nocase(ErrorString(), "full") || str_find_nocase(ErrorString(), "reserved")))
			m_ReconnectTime = time_get() + time_freq() * g_Config.m_ClReconnectFull;
		else if(g_Config.m_ClReconnectTimeout > 0 && (str_find_nocase(ErrorString(), "Timeout") || str_find_nocase(ErrorString(), "Too weak connection")))
			m_ReconnectTime = time_get() + time_freq() * g_Config.m_ClReconnectTimeout;
	}

	if(State == IClient::STATE_ONLINE)
	{
		const NETADDR *pServerAddr = ServerAddress();
		dbg_assert(pServerAddr != nullptr, "online state requires server address");
		const bool Registered = m_ServerBrowser.IsRegistered(*pServerAddr);
		Discord()->SetGameInfo(m_CurrentServerInfo, Registered);
		Steam()->SetGameInfo(*pServerAddr, m_aCurrentMap, Registered);
	}
	else if(OldState == IClient::STATE_ONLINE)
	{
		Discord()->ClearGameInfo();
		Steam()->ClearGameInfo();
	}
}

// called when the map is loaded and we should init for a new round
void CClient::OnEnterGame(bool Dummy)
{
	// reset input
	for(int i = 0; i < 200; i++)
	{
		m_aInputs[Dummy][i].m_Tick = -1;
	}
	m_aCurrentInput[Dummy] = 0;

	// reset snapshots
	m_aapSnapshots[Dummy][SNAP_CURRENT] = nullptr;
	m_aapSnapshots[Dummy][SNAP_PREV] = nullptr;
	m_aSnapshotStorage[Dummy].PurgeAll();
	m_aReceivedSnapshots[Dummy] = 0;
	m_aSnapshotStats[Dummy] = {};
	m_aLastSnapshotTime[Dummy] = 0;
	m_aLastSnapshotTick[Dummy] = -1;
	m_aSnapshotParts[Dummy] = 0;
	m_aSnapshotIncomingDataSize[Dummy] = 0;
	m_SnapCrcErrors = 0;
	// Also make gameclient aware that snapshots have been purged
	GameClient()->InvalidateSnapshot();

	// reset times
	m_aAckGameTick[Dummy] = -1;
	m_aCurrentRecvTick[Dummy] = 0;
	m_aPrevGameTick[Dummy] = 0;
	m_aCurGameTick[Dummy] = 0;
	m_aGameIntraTick[Dummy] = 0.0f;
	m_aGameTickTime[Dummy] = 0.0f;
	m_aGameIntraTickSincePrev[Dummy] = 0.0f;
	m_aPredTick[Dummy] = 0;
	m_aPredIntraTick[Dummy] = 0.0f;
	m_aGameTime[Dummy].Init(0);
	m_PredictedTime.Init(0);
	if(!Dummy)
	{
		ResetAutoPredictionMargin();
	}

	if(!Dummy)
	{
		m_LastDummyConnectTime = 0.0f;
	}

	GameClient()->OnEnterGame();
}

void CClient::EnterGame(int Conn)
{
	if(State() == IClient::STATE_DEMOPLAYBACK)
		return;

	m_aDidPostConnect[Conn] = false;
	m_aGamePingProbes[Conn].Reset();
	m_aGamePingProbes[Conn].m_NextTime = time_get() + time_freq() / 2;

	// TClient
	m_aExecuteOnJoinDone[Conn] = false;

	// now we will wait for two snapshots
	// to finish the connection
	SendEnterGame(Conn);
	OnEnterGame(Conn);

	ServerInfoRequest(); // fresh one for timeout protection
	m_CurrentServerNextPingTime = time_get() + time_freq() / 2;
}

void CClient::OnPostConnect(int Conn)
{
	if(!m_ServerCapabilities.m_ChatTimeoutCode)
		return;

	char aBufMsg[256];
	if(!g_Config.m_ClRunOnJoin[0] && !g_Config.m_ClDummyDefaultEyes && !g_Config.m_ClPlayerDefaultEyes)
		str_format(aBufMsg, sizeof(aBufMsg), "/timeout %s", m_aTimeoutCodes[Conn]);
	else
		str_format(aBufMsg, sizeof(aBufMsg), "/mc;timeout %s", m_aTimeoutCodes[Conn]);

	if(g_Config.m_ClDummyDefaultEyes || g_Config.m_ClPlayerDefaultEyes)
	{
		int Emote = Conn == CONN_DUMMY ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;

		if(Emote != EMOTE_NORMAL)
		{
			char aBuf[32];
			static const char *s_EMOTE_NAMES[] = {
				"pain",
				"happy",
				"surprise",
				"angry",
				"blink",
			};
			static_assert(std::size(s_EMOTE_NAMES) == NUM_EMOTES - 1, "The size of EMOTE_NAMES must match NUM_EMOTES - 1");

			str_append(aBufMsg, ";");
			str_format(aBuf, sizeof(aBuf), "emote %s %d", s_EMOTE_NAMES[Emote - 1], g_Config.m_ClEyeDuration);
			str_append(aBufMsg, aBuf);
		}
	}
	if(g_Config.m_ClRunOnJoin[0])
	{
		str_append(aBufMsg, ";");
		str_append(aBufMsg, g_Config.m_ClRunOnJoin);
	}
	if(IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = aBufMsg;
		SendPackMsg(Conn, &Msg7, MSGFLAG_VITAL, true);
	}
	else
	{
		CNetMsg_Cl_Say MsgP;
		MsgP.m_Team = 0;
		MsgP.m_pMessage = aBufMsg;
		CMsgPacker PackerTimeout(&MsgP);
		MsgP.Pack(&PackerTimeout);
		SendMsg(Conn, &PackerTimeout, MSGFLAG_VITAL);
	}
}

static void GenerateTimeoutCode(char *pBuffer, unsigned Size, char *pSeed, const NETADDR *pAddrs, int NumAddrs, bool Dummy)
{
	MD5_CTX Md5;
	md5_init(&Md5);
	const char *pDummy = Dummy ? "dummy" : "normal";
	md5_update(&Md5, (unsigned char *)pDummy, str_length(pDummy) + 1);
	md5_update(&Md5, (unsigned char *)pSeed, str_length(pSeed) + 1);
	for(int i = 0; i < NumAddrs; i++)
	{
		md5_update(&Md5, (unsigned char *)&pAddrs[i], sizeof(pAddrs[i]));
	}
	MD5_DIGEST Digest = md5_finish(&Md5);

	unsigned short aRandom[8];
	mem_copy(aRandom, Digest.data, sizeof(aRandom));
	generate_password(pBuffer, Size, aRandom, 8);
}

void CClient::GenerateTimeoutSeed()
{
	secure_random_password(g_Config.m_ClTimeoutSeed, sizeof(g_Config.m_ClTimeoutSeed), 16);
}

void CClient::GenerateTimeoutCodes(const NETADDR *pAddrs, int NumAddrs)
{
	if(g_Config.m_ClTimeoutSeed[0])
	{
		for(int i = 0; i < 2; i++)
		{
			GenerateTimeoutCode(m_aTimeoutCodes[i], sizeof(m_aTimeoutCodes[i]), g_Config.m_ClTimeoutSeed, pAddrs, NumAddrs, i);

			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "timeout code '%s' (%s)", m_aTimeoutCodes[i], i == 0 ? "normal" : "dummy");
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
		}
	}
	else
	{
		str_copy(m_aTimeoutCodes[0], g_Config.m_ClTimeoutCode);
		str_copy(m_aTimeoutCodes[1], g_Config.m_ClDummyTimeoutCode);
	}
}

void CClient::Connect(const char *pAddress, const char *pPassword)
{
	// Disconnect will not change the state if we are already quitting/restarting
	if(m_State == IClient::STATE_QUITTING || m_State == IClient::STATE_RESTARTING)
		return;
	const NETADDR *pLastAddr = m_aNetClient[CONN_MAIN].ServerAddress();
	const NETADDR LastAddr = pLastAddr ? *pLastAddr : NETADDR{};
	Disconnect();
	dbg_assert(m_State == IClient::STATE_OFFLINE, "Disconnect must ensure that client is offline");

	if(pAddress != m_aConnectAddressStr)
		str_copy(m_aConnectAddressStr, pAddress);

	char aMsg[512];
	str_format(aMsg, sizeof(aMsg), "connecting to '%s'", m_aConnectAddressStr);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aMsg, gs_ClientNetworkPrintColor);

	int NumConnectAddrs = 0;
	NETADDR aConnectAddrs[MAX_SERVER_ADDRESSES];
	mem_zero(aConnectAddrs, sizeof(aConnectAddrs));
	const char *pNextAddr = pAddress;
	char aBuffer[128];
	bool OnlySixup = true;
	while((pNextAddr = str_next_token(pNextAddr, ",", aBuffer, sizeof(aBuffer))))
	{
		NETADDR NextAddr;
		char aHost[128];
		const int UrlParseResult = net_addr_from_url(&NextAddr, aBuffer, aHost, sizeof(aHost));
		bool Sixup = NextAddr.type & NETTYPE_TW7;
		if(UrlParseResult > 0)
			str_copy(aHost, aBuffer);

		if(net_host_lookup(aHost, &NextAddr, m_aNetClient[CONN_MAIN].NetType()) != 0)
		{
			log_error("client", "could not find address of %s", aHost);
			continue;
		}
		if(NumConnectAddrs == (int)std::size(aConnectAddrs))
		{
			log_warn("client", "too many connect addresses, ignoring %s", aHost);
			continue;
		}
		if(NextAddr.port == 0)
		{
			NextAddr.port = 8303;
		}
		if(Sixup)
			NextAddr.type |= NETTYPE_TW7;
		else
			OnlySixup = false;

		char aNextAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&NextAddr, aNextAddr, sizeof(aNextAddr), true);
		log_debug("client", "resolved connect address '%s' to %s", aBuffer, aNextAddr);

		if(NextAddr == LastAddr)
		{
			m_SendPassword = true;
		}

		aConnectAddrs[NumConnectAddrs] = NextAddr;
		NumConnectAddrs += 1;
	}

	if(NumConnectAddrs == 0)
	{
		log_error("client", "could not find any connect address");
		char aWarning[256];
		str_format(aWarning, sizeof(aWarning), Localize("Could not resolve connect address '%s'. See local console for details."), m_aConnectAddressStr);
		SWarning Warning(Localize("Connect address error"), aWarning);
		Warning.m_AutoHide = false;
		AddWarning(Warning);
		return;
	}

	m_ConnectionId = RandomUuid();
	ServerInfoRequest();

	// TClient
	// If user has manually specified password don't run autoexec
	if(!m_SendPassword)
	{
		m_pGameClient->SetConnectInfo(&aConnectAddrs[0]);
		m_pConsole->ExecuteLine(g_Config.m_TcExecuteOnConnect, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	m_pGameClient->SetConnectInfo(nullptr);

	if(m_SendPassword)
	{
		str_copy(m_aPassword, g_Config.m_Password);
		m_SendPassword = false;
	}
	else if(!pPassword)
	{
		m_aPassword[0] = 0;
	}
	else
	{
		str_copy(m_aPassword, pPassword);
	}

	m_CanReceiveServerCapabilities = true;

	m_Sixup = OnlySixup;
	if(m_Sixup)
	{
		m_aNetClient[CONN_MAIN].Connect7(aConnectAddrs, NumConnectAddrs);
	}
	else
	{
		m_aNetClient[CONN_MAIN].Connect(aConnectAddrs, NumConnectAddrs);
	}

	m_aNetClient[CONN_MAIN].RefreshStun();
	SetState(IClient::STATE_CONNECTING);

	m_InputtimeMarginGraph.Init(-150.0f, 150.0f);
	m_aGametimeMarginGraphs[CONN_MAIN].Init(-150.0f, 150.0f);

	GenerateTimeoutCodes(aConnectAddrs, NumConnectAddrs);
}

void CClient::DisconnectWithReason(const char *pReason)
{
	if(pReason != nullptr && pReason[0] == '\0')
		pReason = nullptr;

	DummyDisconnect(pReason);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "disconnecting. reason='%s'", pReason ? pReason : "unknown");
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, gs_ClientNetworkPrintColor);

	// stop demo playback and recorder
	// make sure to remove replay tmp demo
	m_DemoPlayer.Stop();
	for(int Recorder = 0; Recorder < RECORDER_MAX; Recorder++)
	{
		DemoRecorder(Recorder)->Stop(Recorder == RECORDER_REPLAYS ? IDemoRecorder::EStopMode::REMOVE_FILE : IDemoRecorder::EStopMode::KEEP_FILE);
	}

	m_aRconAuthed[0] = 0;
	// Make sure to clear credentials completely from memory
	mem_zero(m_aRconUsername, sizeof(m_aRconUsername));
	mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
	m_MapDetails = std::nullopt;
	m_ServerSentCapabilities = false;
	m_ServerCapabilities = {};
	m_KcpNegotiationPending = false;
	m_KcpNegotiated = false;
	m_KcpNegotiationStartTime = 0;
	m_KcpNegotiationConv = 0;
	m_UseTempRconCommands = 0;
	m_ExpectedRconCommands = -1;
	m_GotRconCommands = 0;
	m_pConsole->DeregisterTempAll();
	m_ExpectedMaplistEntries = -1;
	m_vMaplistEntries.clear();
	GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
	m_aNetClient[CONN_MAIN].Disconnect(pReason);
	SetState(IClient::STATE_OFFLINE);
	m_pMap->Unload();
	m_CurrentServerPingInfoType = -1;
	m_CurrentServerPingBasicToken = -1;
	m_CurrentServerPingToken = -1;
	mem_zero(&m_CurrentServerPingUuid, sizeof(m_CurrentServerPingUuid));
	m_CurrentServerCurrentPingTime = -1;
	m_CurrentServerNextPingTime = -1;
	m_ManualPingProbe.Reset();
	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
		m_aGamePingProbes[Conn].Reset();
	ResetAutoPredictionMargin();

	ResetMapDownload(true);

	// clear the current server info
	m_CurrentServerInfo = {};

	// clear snapshots
	m_aapSnapshots[0][SNAP_CURRENT] = nullptr;
	m_aapSnapshots[0][SNAP_PREV] = nullptr;
	m_aReceivedSnapshots[0] = 0;
	m_aSnapshotStats[0] = {};
	m_aLastSnapshotTime[0] = 0;
	m_aLastSnapshotTick[0] = -1;
	m_LastDummy = false;

	// 0.7
	m_TranslationContext.Reset();
	m_Sixup = false;
}

void CClient::Disconnect()
{
	if(m_State != IClient::STATE_OFFLINE)
	{
		DisconnectWithReason(nullptr);
	}
}

void CClient::DropCurrentServerConnection()
{
	if(m_State == IClient::STATE_OFFLINE || m_State == IClient::STATE_DEMOPLAYBACK)
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "not connected", gs_ClientNetworkErrPrintColor);
		return;
	}

	static constexpr const char *pReason = "abnormal disconnect";
	m_aNetClient[CONN_DUMMY].Drop(pReason);
	m_aNetClient[CONN_MAIN].Drop(pReason);
	DisconnectWithReason(pReason);
}

bool CClient::DummyConnected() const
{
	return m_DummyConnected;
}

bool CClient::DummyConnecting() const
{
	return m_DummyConnecting;
}

bool CClient::DummyConnectingDelayed() const
{
	return !DummyConnected() && !DummyConnecting() && m_LastDummyConnectTime > 0.0f && m_LastDummyConnectTime + 5.0f > GlobalTime();
}

void CClient::DummyConnect()
{
	if(m_aNetClient[CONN_MAIN].State() != NETSTATE_ONLINE)
	{
		log_info("client", "Not online.");
		return;
	}

	if(!DummyAllowed())
	{
		log_info("client", "Dummy is not allowed on this server.");
		return;
	}
	if(DummyConnecting())
	{
		log_info("client", "Dummy is already connecting.");
		return;
	}
	if(DummyConnected())
	{
		// causes log spam with connect+swap binds
		// https://github.com/ddnet/ddnet/issues/9426
		// log_info("client", "Dummy is already connected.");
		return;
	}
	if(DummyConnectingDelayed())
	{
		log_info("client", "Wait before connecting dummy again.");
		return;
	}

	m_LastDummyConnectTime = GlobalTime();
	m_aRconAuthed[1] = 0;
	m_DummySendConnInfo = true;

	g_Config.m_ClDummyCopyMoves = 0;
	g_Config.m_ClDummyHammer = 0;

	const NETADDR *pMainAddr = m_aNetClient[CONN_MAIN].ServerAddress();
	if(!pMainAddr)
		return;
	m_DummyConnecting = true;
	// connect to the server
	if(IsSixup())
		m_aNetClient[CONN_DUMMY].Connect7(pMainAddr, 1);
	else
		m_aNetClient[CONN_DUMMY].Connect(pMainAddr, 1);

	m_aGametimeMarginGraphs[CONN_DUMMY].Init(-150.0f, 150.0f);
}

void CClient::DummyDisconnect(const char *pReason)
{
	m_aNetClient[CONN_DUMMY].Disconnect(pReason);
	g_Config.m_ClDummy = 0;

	m_aRconAuthed[1] = 0;
	m_aapSnapshots[1][SNAP_CURRENT] = nullptr;
	m_aapSnapshots[1][SNAP_PREV] = nullptr;
	m_aReceivedSnapshots[1] = 0;
	m_aGamePingProbes[CONN_DUMMY].Reset();
	m_aSnapshotStats[1] = {};
	m_aLastSnapshotTime[1] = 0;
	m_aLastSnapshotTick[1] = -1;
	m_DummyConnected = false;
	m_DummyConnecting = false;
	m_DummyReconnectOnReload = false;
	m_DummyDeactivateOnReconnect = false;
#if defined(CONF_PLATFORM_IOS)
	m_DummyReconnectOnResume = false;
#endif
	GameClient()->OnDummyDisconnect();
}

bool CClient::DummyAllowed() const
{
	return m_ServerCapabilities.m_AllowDummy;
}

const CServerInfo &CClient::ServerInfo() const
{
	return m_CurrentServerInfo;
}

void CClient::GetServerInfo(CServerInfo *pServerInfo) const
{
	*pServerInfo = m_CurrentServerInfo;
}

void CClient::SetCurrentServerInfo(const CServerInfo &ServerInfo)
{
	m_CurrentServerInfo = ServerInfo;
	m_CurrentServerInfoRequestTime = -1;
	str_copy(m_CurrentServerInfo.m_aMap, GetCurrentMap());
	m_CurrentServerInfo.m_MapCrc = m_pMap->Crc();
	m_CurrentServerInfo.m_MapSize = m_pMap->Size();
}

void CClient::ServerInfoRequest()
{
	m_CurrentServerInfo = {};
	m_CurrentServerInfoRequestTime = 0;
}

void CClient::LoadDebugFont()
{
	m_DebugFont = Graphics()->LoadTexture("debug_font.png", IStorage::TYPE_ALL);
}

// ---

IClient::CSnapItem CClient::SnapGetItem(int SnapId, int Index) const
{
	dbg_assert(SnapId >= 0 && SnapId < NUM_SNAPSHOT_TYPES, "invalid SnapId");
	const CSnapshot *pSnapshot = m_aapSnapshots[g_Config.m_ClDummy][SnapId]->m_pAltSnap;
	const CSnapshotItem *pSnapshotItem = pSnapshot->GetItem(Index);
	CSnapItem Item;
	Item.m_Type = pSnapshot->GetItemType(Index);
	Item.m_Id = pSnapshotItem->Id();
	Item.m_pData = pSnapshotItem->Data();
	Item.m_DataSize = pSnapshot->GetItemSize(Index);
	return Item;
}

const void *CClient::SnapFindItem(int SnapId, int Type, int Id) const
{
	if(!m_aapSnapshots[g_Config.m_ClDummy][SnapId])
		return nullptr;

	return m_aapSnapshots[g_Config.m_ClDummy][SnapId]->m_pAltSnap->FindItem(Type, Id);
}

int CClient::SnapNumItems(int SnapId) const
{
	dbg_assert(SnapId >= 0 && SnapId < NUM_SNAPSHOT_TYPES, "invalid SnapId");
	if(!m_aapSnapshots[g_Config.m_ClDummy][SnapId])
		return 0;
	return m_aapSnapshots[g_Config.m_ClDummy][SnapId]->m_pAltSnap->NumItems();
}

void CClient::SnapSetStaticsize(int ItemType, int Size)
{
	m_pSnapshotDelta->SetStaticsize(ItemType, Size);
}

void CClient::SnapSetStaticsize7(int ItemType, int Size)
{
	m_pSnapshotDeltaSixup->SetStaticsize(ItemType, Size);
}

void CClient::RenderDebug()
{
	if(!g_Config.m_Debug)
	{
		return;
	}

	UpdateNetStatsSnapshot();

	char aBuffer[512];
	const float FontSize = 16.0f;

	Graphics()->TextureSet(m_DebugFont);
	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	Graphics()->QuadsBegin();

	str_format(aBuffer, sizeof(aBuffer), "Game/predicted tick: %d/%d", m_aCurGameTick[g_Config.m_ClDummy], m_aPredTick[g_Config.m_ClDummy]);
	Graphics()->QuadsText(2, 2, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "Prediction time: %d ms", GetPredictionTime());
	Graphics()->QuadsText(2, 2 + FontSize, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "FPS: %3d", round_to_int(1.0f / m_FrameTimeAverage));
	Graphics()->QuadsText(20.0f * FontSize, 2, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "Frametime: %4d us", round_to_int(m_FrameTimeAverage * 1000000.0f));
	Graphics()->QuadsText(20.0f * FontSize, 2 + FontSize, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "%16s: %" PRIu64 " KiB", "Texture memory", Graphics()->TextureMemoryUsage() / 1024);
	Graphics()->QuadsText(32.0f * FontSize, 2, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "%16s: %" PRIu64 " KiB", "Buffer memory", Graphics()->BufferMemoryUsage() / 1024);
	Graphics()->QuadsText(32.0f * FontSize, 2 + FontSize, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "%16s: %" PRIu64 " KiB", "Streamed memory", Graphics()->StreamedMemoryUsage() / 1024);
	Graphics()->QuadsText(32.0f * FontSize, 2 + 2 * FontSize, FontSize, aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "%16s: %" PRIu64 " KiB", "Staging memory", Graphics()->StagingMemoryUsage() / 1024);
	Graphics()->QuadsText(32.0f * FontSize, 2 + 3 * FontSize, FontSize, aBuffer);

	// Network
	{
		const uint64_t OverheadSize = 14 + 20 + 8; // ETH + IP + UDP
		const auto CounterDelta = [](uint64_t Current, uint64_t Previous) {
			return Current >= Previous ? Current - Previous : 0;
		};
		const double SampleSeconds = m_NetstatsSampleInterval.count() > 0 ? (double)m_NetstatsSampleInterval.count() / 1000000000.0 : 0.0;
		const auto RateKibitPerSec = [SampleSeconds](uint64_t Bytes) {
			return SampleSeconds > 0.0 ? (uint64_t)((double)Bytes * 8.0 / 1024.0 / SampleSeconds) : 0;
		};
		const bool HasSample = SampleSeconds > 0.0;
		const bool SendCountersValid = m_NetstatsCurrent.sent_packets >= m_NetstatsPrev.sent_packets && m_NetstatsCurrent.sent_bytes >= m_NetstatsPrev.sent_bytes;
		const bool RecvCountersValid = m_NetstatsCurrent.recv_packets >= m_NetstatsPrev.recv_packets && m_NetstatsCurrent.recv_bytes >= m_NetstatsPrev.recv_bytes;
		const uint64_t SendPackets = CounterDelta(m_NetstatsCurrent.sent_packets, m_NetstatsPrev.sent_packets);
		const uint64_t SendBytes = CounterDelta(m_NetstatsCurrent.sent_bytes, m_NetstatsPrev.sent_bytes);
		const uint64_t SendTotal = SendBytes + SendPackets * OverheadSize;
		const uint64_t RecvPackets = CounterDelta(m_NetstatsCurrent.recv_packets, m_NetstatsPrev.recv_packets);
		const uint64_t RecvBytes = CounterDelta(m_NetstatsCurrent.recv_bytes, m_NetstatsPrev.recv_bytes);
		const uint64_t RecvTotal = RecvBytes + RecvPackets * OverheadSize;
		char aSendRateBuf[32];
		char aRecvRateBuf[32];
		if(HasSample && SendCountersValid)
			str_format(aSendRateBuf, sizeof(aSendRateBuf), "%" PRIu64, RateKibitPerSec(SendTotal));
		else
			str_copy(aSendRateBuf, "--", sizeof(aSendRateBuf));
		if(HasSample && RecvCountersValid)
			str_format(aRecvRateBuf, sizeof(aRecvRateBuf), "%" PRIu64, RateKibitPerSec(RecvTotal));
		else
			str_copy(aRecvRateBuf, "--", sizeof(aRecvRateBuf));

		str_format(aBuffer, sizeof(aBuffer), "Process UDP TX (estimated): %3" PRIu64 " %5" PRIu64 "+%4" PRIu64 "=%5" PRIu64 " (%s Kibit/s) avg payload: %5" PRIu64,
			SendPackets, SendBytes, SendPackets * OverheadSize, SendTotal, aSendRateBuf, SendPackets == 0 ? 0 : SendBytes / SendPackets);
		Graphics()->QuadsText(2, 2 + 3 * FontSize, FontSize, aBuffer);
		str_format(aBuffer, sizeof(aBuffer), "Process UDP RX (estimated): %3" PRIu64 " %5" PRIu64 "+%4" PRIu64 "=%5" PRIu64 " (%s Kibit/s) avg payload: %5" PRIu64,
			RecvPackets, RecvBytes, RecvPackets * OverheadSize, RecvTotal, aRecvRateBuf, RecvPackets == 0 ? 0 : RecvBytes / RecvPackets);
		Graphics()->QuadsText(2, 2 + 4 * FontSize, FontSize, aBuffer);
	}

	// Snapshots
	{
		const float OffsetY = 2 + 6 * FontSize;
		int Row = 0;
		str_format(aBuffer, sizeof(aBuffer), "%5s %20s: %8s %8s %8s", "ID", "Name", "Rate", "Updates", "R/U");
		Graphics()->QuadsText(2, OffsetY + Row * 12, FontSize, aBuffer);
		Row++;
		for(int i = 0; i < NUM_NETOBJTYPES; i++)
		{
			const uint64_t DataRate = SnapshotDelta()->GetDataRate(i);
			const uint64_t DataUpdates = SnapshotDelta()->GetDataUpdates(i);
			if(DataRate && DataUpdates)
			{
				str_format(
					aBuffer,
					sizeof(aBuffer),
					"%5d %20s: %8" PRIu64 " %8" PRIu64 " %8" PRIu64,
					i,
					GameClient()->GetItemName(i),
					DataRate / 8, DataUpdates,
					(DataRate / DataUpdates) / 8);
				Graphics()->QuadsText(2, OffsetY + Row * 12, FontSize, aBuffer);
				Row++;
			}
		}
		for(int i = CSnapshot::MAX_TYPE; i > (CSnapshot::MAX_TYPE - 64); i--)
		{
			const uint64_t DataRate = SnapshotDelta()->GetDataRate(i);
			const uint64_t DataUpdates = SnapshotDelta()->GetDataUpdates(i);
			if(DataRate && DataUpdates && m_aapSnapshots[g_Config.m_ClDummy][IClient::SNAP_CURRENT])
			{
				const int Type = m_aapSnapshots[g_Config.m_ClDummy][IClient::SNAP_CURRENT]->m_pAltSnap->GetExternalItemType(i);
				if(Type == UUID_INVALID)
				{
					str_format(
						aBuffer,
						sizeof(aBuffer),
						"%5d %20s: %8" PRIu64 " %8" PRIu64 " %8" PRIu64,
						i,
						"Unknown UUID",
						DataRate / 8,
						DataUpdates,
						(DataRate / DataUpdates) / 8);
					Graphics()->QuadsText(2, OffsetY + Row * 12, FontSize, aBuffer);
					Row++;
				}
				else if(Type != i)
				{
					str_format(
						aBuffer,
						sizeof(aBuffer),
						"%5d %20s: %8" PRIu64 " %8" PRIu64 " %8" PRIu64,
						Type,
						GameClient()->GetItemName(Type),
						DataRate / 8,
						DataUpdates,
						(DataRate / DataUpdates) / 8);
					Graphics()->QuadsText(2, OffsetY + Row * 12, FontSize, aBuffer);
					Row++;
				}
			}
		}
	}

	Graphics()->QuadsEnd();
}

void CClient::RenderGraphs()
{
	if(!g_Config.m_DbgGraphs)
		return;

	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float GraphSpacing = std::round(Graphics()->ScreenWidth() / 100.0f);
	const float GraphX = Graphics()->ScreenWidth() - GraphSpacing;

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->RenderQmMonitoringHud(GraphX, GraphSpacing);
}

void CClient::Restart()
{
	SetState(IClient::STATE_RESTARTING);
}

void CClient::Quit()
{
	if(!GameClient()->PrepareForShutdown(false))
		SetState(IClient::STATE_QUITTING);
}

bool CClient::ResetSocket()
{
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] == '\0')
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
	}
	else if(net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) != 0)
	{
		log_error("client", "The configured bindaddr '%s' cannot be resolved.", g_Config.m_Bindaddr);
		return false;
	}
	BindAddr.type = NETTYPE_ALL;
	bool Success = true;
	for(size_t Conn = 0; Conn < std::size(m_aNetClient); Conn++)
	{
		char aError[256];
		if(!InitNetworkClientImpl(BindAddr, Conn, aError, sizeof(aError)))
		{
			Success = false;
			log_error("client", "%s", aError);
		}
	}
	return Success;
}

#if defined(CONF_PLATFORM_IOS)
void CClient::RecreateBrokenSockets()
{
	if(std::none_of(std::begin(m_aNetClient), std::end(m_aNetClient), [](const CNetClient &NetClient) { return NetClient.SocketIsBroken(); }))
	{
		return;
	}

	// iOS 在应用挂起期间关闭 UDP socket，恢复后仅在明确检测到 EPIPE 时重建。
	log_info("client", "network sockets were closed by the system, recreating them");

	char aConnectAddress[sizeof(m_aConnectAddressStr)];
	str_copy(aConnectAddress, m_aConnectAddressStr);
	const bool Reconnect = State() != IClient::STATE_OFFLINE && State() < IClient::STATE_QUITTING;
	const bool ReconnectDummy = Reconnect && m_DummyConnected;
	const bool DeactivateDummy = g_Config.m_ClDummy == 0;

	Disconnect();
	for(CNetClient &NetClient : m_aNetClient)
		NetClient.Close();
	if(!ResetSocket())
	{
		log_error("client", "network socket recreation failed");
		return;
	}
	// 重建后的 socket 不包含旧实例加载的 STUN server。
	LoadDDNetInfo();

	if(Reconnect)
	{
		Connect(aConnectAddress);
		if(ReconnectDummy)
		{
			// 等主连接就绪后再连接分身，沿用既有 dummy 建连流程。
			m_DummyReconnectOnResume = true;
			m_DummyDeactivateOnReconnect = DeactivateDummy;
		}
	}
}
#endif

const char *CClient::PlayerName() const
{
	if(g_Config.m_PlayerName[0])
	{
		return g_Config.m_PlayerName;
	}
	if(g_Config.m_SteamName[0])
	{
		return g_Config.m_SteamName;
	}
	return "nameless tee";
}

const char *CClient::DummyName()
{
	if(g_Config.m_ClDummyName[0])
	{
		return g_Config.m_ClDummyName;
	}
	const char *pBase = nullptr;
	if(g_Config.m_PlayerName[0])
	{
		pBase = g_Config.m_PlayerName;
	}
	else if(g_Config.m_SteamName[0])
	{
		pBase = g_Config.m_SteamName;
	}
	if(pBase)
	{
		str_format(m_aAutomaticDummyName, sizeof(m_aAutomaticDummyName), "[D] %s", pBase);
		return m_aAutomaticDummyName;
	}
	return "brainless tee";
}

const char *CClient::ErrorString() const
{
	return m_aNetClient[CONN_MAIN].ErrorString();
}

void CClient::Render()
{
	if(!QmPerfEnabled())
	{
		if(m_EditorActive)
			m_pEditor->OnRender();
		else
			GameClient()->OnRender();
		RenderDebug();
		RenderGraphs();
		return;
	}

	CPerfTimer RenderTimer;

	if(m_EditorActive)
	{
		CPerfTimer StageTimer;
		m_pEditor->OnRender();
		QmPerfLogStage("perf/render", "editor_onrender", StageTimer.ElapsedMs(), false, this);
	}
	else
	{
		CPerfTimer StageTimer;
		GameClient()->OnRender();
		QmPerfLogStage("perf/render", "gameclient_onrender", StageTimer.ElapsedMs(), false, this);
	}

	{
		CPerfTimer StageTimer;
		RenderDebug();
		QmPerfLogStage("perf/render", "client_render_debug", StageTimer.ElapsedMs(), false, this);
	}

	{
		CPerfTimer StageTimer;
		RenderGraphs();
		QmPerfLogStage("perf/render", "client_render_graphs", StageTimer.ElapsedMs(), false, this);
	}

	QmPerfLogStage("perf/render", "client_render_total", RenderTimer.ElapsedMs(), false, this);
}

const char *CClient::LoadMap(const char *pName, const char *pFilename, const std::optional<SHA256_DIGEST> &WantedSha256, unsigned WantedCrc)
{
	static char s_aErrorMsg[128];

	SetState(IClient::STATE_LOADING);
	SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_LOADING_MAP);
	if((bool)m_LoadingCallback)
		m_LoadingCallback(IClient::LOADING_CALLBACK_DETAIL_MAP);

	// 加载新地图前停止 demo 录制。
	for(int Recorder = 0; Recorder < RECORDER_MAX; Recorder++)
	{
		DemoRecorder(Recorder)->Stop(Recorder == RECORDER_REPLAYS ? IDemoRecorder::EStopMode::REMOVE_FILE : IDemoRecorder::EStopMode::KEEP_FILE);
	}

	// 加载新地图前卸载当前地图并重置所有快照，因为快照只对旧地图有效。
	m_pMap->Unload();
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		m_aapSnapshots[Dummy][SNAP_CURRENT] = nullptr;
		m_aapSnapshots[Dummy][SNAP_PREV] = nullptr;
		m_aSnapshotStorage[Dummy].PurgeAll();
		m_aReceivedSnapshots[Dummy] = 0;
		m_aSnapshotStats[Dummy] = {};
		m_aLastSnapshotTime[Dummy] = 0;
		m_aLastSnapshotTick[Dummy] = -1;
		m_aSnapshotParts[Dummy] = 0;
		m_aSnapshotIncomingDataSize[Dummy] = 0;
	}
	m_SnapCrcErrors = 0;
	GameClient()->InvalidateSnapshot();

	if(!m_pMap->Load(pFilename, IStorage::TYPE_ALL))
	{
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map '%s' not found", pFilename);
		return s_aErrorMsg;
	}

	if(WantedSha256.has_value() && m_pMap->Sha256() != WantedSha256.value())
	{
		char aWanted[SHA256_MAXSTRSIZE];
		char aGot[SHA256_MAXSTRSIZE];
		sha256_str(WantedSha256.value(), aWanted, sizeof(aWanted));
		sha256_str(m_pMap->Sha256(), aGot, sizeof(aWanted));
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map differs from the server. %s != %s", aGot, aWanted);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", s_aErrorMsg);
		m_pMap->Unload();
		return s_aErrorMsg;
	}

	// Only check CRC if we don't have the secure SHA256.
	if(!WantedSha256.has_value() && m_pMap->Crc() != WantedCrc)
	{
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map differs from the server. %08x != %08x", m_pMap->Crc(), WantedCrc);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", s_aErrorMsg);
		m_pMap->Unload();
		return s_aErrorMsg;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "loaded map '%s'", pFilename);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);

	str_copy(m_aCurrentMap, pName);
	str_copy(m_aCurrentMapPath, pFilename);

	return nullptr;
}

static void FormatMapDownloadFilename(const char *pName, const std::optional<SHA256_DIGEST> &Sha256, int Crc, bool Temp, char *pBuffer, int BufferSize)
{
	char aSuffix[32];
	if(Temp)
	{
		IStorage::FormatTmpPath(aSuffix, sizeof(aSuffix), "");
	}
	else
	{
		str_copy(aSuffix, ".map");
	}

	if(Sha256.has_value())
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(Sha256.value(), aSha256, sizeof(aSha256));
		str_format(pBuffer, BufferSize, "downloadedmaps/%s_%s%s", pName, aSha256, aSuffix);
	}
	else
	{
		str_format(pBuffer, BufferSize, "downloadedmaps/%s_%08x%s", pName, Crc, aSuffix);
	}
}

const char *CClient::LoadMapSearch(const char *pMapName, const std::optional<SHA256_DIGEST> &WantedSha256, int WantedCrc)
{
	char aBuf[512];
	char aWanted[SHA256_MAXSTRSIZE + 16];
	aWanted[0] = 0;
	if(WantedSha256.has_value())
	{
		char aWantedSha256[SHA256_MAXSTRSIZE];
		sha256_str(WantedSha256.value(), aWantedSha256, sizeof(aWantedSha256));
		str_format(aWanted, sizeof(aWanted), "sha256=%s ", aWantedSha256);
	}
	str_format(aBuf, sizeof(aBuf), "loading map, map=%s wanted %scrc=%08x", pMapName, aWanted, WantedCrc);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);

	// try the normal maps folder
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	const char *pError = LoadMap(pMapName, aBuf, WantedSha256, WantedCrc);
	if(!pError)
		return nullptr;

	// try the downloaded maps
	FormatMapDownloadFilename(pMapName, WantedSha256, WantedCrc, false, aBuf, sizeof(aBuf));
	pError = LoadMap(pMapName, aBuf, WantedSha256, WantedCrc);
	if(!pError)
		return nullptr;

	// backward compatibility with old names
	if(WantedSha256.has_value())
	{
		FormatMapDownloadFilename(pMapName, std::nullopt, WantedCrc, false, aBuf, sizeof(aBuf));
		pError = LoadMap(pMapName, aBuf, WantedSha256, WantedCrc);
		if(!pError)
			return nullptr;
	}

	// search for the map within subfolders
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s.map", pMapName);
	if(Storage()->FindFile(aFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf)))
	{
		pError = LoadMap(pMapName, aBuf, WantedSha256, WantedCrc);
		if(!pError)
			return nullptr;
	}

	static char s_aErrorMsg[256];
	str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "Could not find map '%s'", pMapName);
	return s_aErrorMsg;
}

void CClient::ProcessConnlessPacket(CNetChunk *pPacket)
{
	// server info
	if(pPacket->m_DataSize >= (int)sizeof(SERVERBROWSE_INFO))
	{
		int Type = -1;
		if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO)) == 0)
			Type = SERVERINFO_VANILLA;
		else if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED)) == 0)
			Type = SERVERINFO_EXTENDED;
		else if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE)) == 0)
			Type = SERVERINFO_EXTENDED_MORE;

		if(Type != -1)
		{
			void *pData = (unsigned char *)pPacket->m_pData + sizeof(SERVERBROWSE_INFO);
			int DataSize = pPacket->m_DataSize - sizeof(SERVERBROWSE_INFO);
			ProcessServerInfo(Type, &pPacket->m_Address, pData, DataSize);
		}
	}
}

static int SavedServerInfoType(int Type)
{
	if(Type == SERVERINFO_EXTENDED_MORE)
		return SERVERINFO_EXTENDED;

	return Type;
}

void CClient::ProcessServerInfo(int RawType, NETADDR *pFrom, const void *pData, int DataSize)
{
	CServerBrowser::CServerEntry *pEntry = m_ServerBrowser.Find(*pFrom);

	CServerInfo Info = {0};
	int SavedType = SavedServerInfoType(RawType);
	if(SavedType == SERVERINFO_EXTENDED && pEntry && pEntry->m_GotInfo && SavedType == pEntry->m_Info.m_Type)
	{
		Info = pEntry->m_Info;
	}
	else
	{
		Info.m_NumAddresses = 1;
		Info.m_aAddresses[0] = *pFrom;
	}

	Info.m_Type = SavedType;

	net_addr_str(pFrom, Info.m_aAddress, sizeof(Info.m_aAddress), true);

	CUnpacker Up;
	Up.Reset(pData, DataSize);

#define GET_STRING(array) str_copy(array, Up.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES), sizeof(array))
#define GET_INT(integer) (integer) = str_toint(Up.GetString())

	int Token;
	int PacketNo = 0; // Only used if SavedType == SERVERINFO_EXTENDED

	GET_INT(Token);
	if(RawType != SERVERINFO_EXTENDED_MORE)
	{
		GET_STRING(Info.m_aVersion);
		GET_STRING(Info.m_aName);
		GET_STRING(Info.m_aMap);

		if(SavedType == SERVERINFO_EXTENDED)
		{
			GET_INT(Info.m_MapCrc);
			GET_INT(Info.m_MapSize);
		}

		GET_STRING(Info.m_aGameType);
		GET_INT(Info.m_Flags);
		GET_INT(Info.m_NumPlayers);
		GET_INT(Info.m_MaxPlayers);
		GET_INT(Info.m_NumClients);
		GET_INT(Info.m_MaxClients);

		// don't add invalid info to the server browser list
		if(Info.m_NumClients < 0 || Info.m_MaxClients < 0 ||
			Info.m_NumPlayers < 0 || Info.m_MaxPlayers < 0 ||
			Info.m_NumPlayers > Info.m_NumClients || Info.m_MaxPlayers > Info.m_MaxClients)
		{
			return;
		}

		m_ServerBrowser.UpdateServerCommunity(&Info);
		m_ServerBrowser.UpdateServerRank(&Info);

		switch(SavedType)
		{
		case SERVERINFO_VANILLA:
			if(Info.m_MaxPlayers > VANILLA_MAX_CLIENTS ||
				Info.m_MaxClients > VANILLA_MAX_CLIENTS)
			{
				return;
			}
			break;
		case SERVERINFO_64_LEGACY:
			if(Info.m_MaxPlayers > MAX_CLIENTS ||
				Info.m_MaxClients > MAX_CLIENTS)
			{
				return;
			}
			break;
		case SERVERINFO_EXTENDED:
			if(Info.m_NumPlayers > Info.m_NumClients)
				return;
			break;
		default:
			dbg_assert_failed("unknown serverinfo type");
		}

		if(SavedType == SERVERINFO_EXTENDED)
			PacketNo = 0;
	}
	else
	{
		GET_INT(PacketNo);
		// 0 needs to be excluded because that's reserved for the main packet.
		if(PacketNo <= 0 || PacketNo >= 64)
			return;
	}

	bool DuplicatedPacket = false;
	if(SavedType == SERVERINFO_EXTENDED)
	{
		Up.GetString(); // extra info, reserved

		uint64_t Flag = (uint64_t)1 << PacketNo;
		DuplicatedPacket = Info.m_ReceivedPackets & Flag;
		Info.m_ReceivedPackets |= Flag;
	}

	bool IgnoreError = false;
	for(int i = 0; i < MAX_CLIENTS && (int)Info.m_vClients.size() < MAX_CLIENTS && !Up.Error(); i++)
	{
		CServerInfo::CClient Client = {};
		GET_STRING(Client.m_aName);
		if(Up.Error())
		{
			// Packet end, no problem unless it happens during one
			// player info, so ignore the error.
			IgnoreError = true;
			break;
		}
		GET_STRING(Client.m_aClan);
		GET_INT(Client.m_Country);
		if(!in_range(Client.m_Country, CountryCode::MINIMUM, CountryCode::MAXIMUM))
		{
			Client.m_Country = CountryCode::DEFAULT;
		}
		GET_INT(Client.m_Score);
		GET_INT(Client.m_Player);
		if(SavedType == SERVERINFO_EXTENDED)
		{
			Up.GetString(); // extra info, reserved
		}
		if(!Up.Error())
		{
			if(SavedType == SERVERINFO_64_LEGACY)
			{
				uint64_t Flag = (uint64_t)1 << i;
				if(!(Info.m_ReceivedPackets & Flag))
				{
					Info.m_ReceivedPackets |= Flag;
					Info.m_vClients.push_back(Client);
				}
			}
			else
			{
				Info.m_vClients.push_back(Client);
			}
		}
	}

	str_clean_whitespaces(Info.m_aName);

	if(!Up.Error() || IgnoreError)
	{
		if(!DuplicatedPacket && (!pEntry || !pEntry->m_GotInfo || SavedType >= pEntry->m_Info.m_Type))
		{
			m_ServerBrowser.OnServerInfoUpdate(*pFrom, Token, &Info);
		}

		// Player info is irrelevant for the client (while connected),
		// it gets its info from elsewhere.
		//
		// SERVERINFO_EXTENDED_MORE doesn't carry any server
		// information, so just skip it.
		const NETADDR *pServerAddr = ServerAddress();
		if(m_aNetClient[CONN_MAIN].State() == NETSTATE_ONLINE &&
			pServerAddr != nullptr &&
			net_addr_comp(pServerAddr, pFrom) == 0 &&
			RawType != SERVERINFO_EXTENDED_MORE)
		{
			// Only accept server info that has a type that is
			// newer or equal to something the server already sent
			// us.
			if(SavedType >= m_CurrentServerInfo.m_Type &&
				m_pMap->IsLoaded())
			{
				SetCurrentServerInfo(Info);
				Discord()->UpdateServerInfo(m_CurrentServerInfo);
			}

			bool ValidPong = false;
			if(!m_ServerCapabilities.m_PingEx && m_CurrentServerCurrentPingTime >= 0 && SavedType >= m_CurrentServerPingInfoType)
			{
				if(RawType == SERVERINFO_VANILLA)
				{
					ValidPong = Token == m_CurrentServerPingBasicToken;
				}
				else if(RawType == SERVERINFO_EXTENDED)
				{
					ValidPong = Token == m_CurrentServerPingToken;
				}
			}
			if(ValidPong)
			{
				int LatencyMs = (time_get() - m_CurrentServerCurrentPingTime) * 1000 / time_freq();
				m_ServerBrowser.SetCurrentServerPing(*pServerAddr, LatencyMs);
				m_CurrentServerInfo.m_Latency = LatencyMs;
				m_CurrentServerPingInfoType = SavedType;
				m_CurrentServerCurrentPingTime = -1;

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "got pong from current server, latency=%dms", LatencyMs);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
			}
		}
	}

#undef GET_STRING
#undef GET_INT
}

static CServerCapabilities GetServerCapabilities(int Version, int Flags, bool Sixup)
{
	CServerCapabilities Result;
	bool DDNet = false;
	if(Version >= 1)
	{
		DDNet = Flags & SERVERCAPFLAG_DDNET;
	}
	Result.m_ChatTimeoutCode = DDNet;
	Result.m_AnyPlayerFlag = !Sixup;
	Result.m_PingEx = false;
	Result.m_AllowDummy = true;
	Result.m_SyncWeaponInput = true;
	if(Version >= 1)
	{
		Result.m_ChatTimeoutCode = Flags & SERVERCAPFLAG_CHATTIMEOUTCODE;
	}
	if(Version >= 2)
	{
		Result.m_AnyPlayerFlag = Flags & SERVERCAPFLAG_ANYPLAYERFLAG;
	}
	if(Version >= 3)
	{
		Result.m_PingEx = Flags & SERVERCAPFLAG_PINGEX;
	}
	if(Version >= 4)
	{
		Result.m_AllowDummy = Flags & SERVERCAPFLAG_ALLOWDUMMY;
	}
	if(Version >= 5)
	{
		Result.m_SyncWeaponInput = true;
	}
	if(Version >= 6)
	{
		Result.m_Kcp = Flags & SERVERCAPFLAG_KCP;
	}
	return Result;
}

void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)
{
	if(Conn < 0 || Conn >= NUM_DUMMIES)
		return;

	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageId(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}
	else if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(Conn, &Packer, MSGFLAG_VITAL);
	}

	// allocates the memory for the translated data
	CPacker Packer6;
	if(IsSixup())
	{
		bool IsExMsg = false;
		int Success = !TranslateSysMsg(&Msg, Sys, &Unpacker, &Packer6, pPacket, &IsExMsg);
		if(Msg < 0)
			return;
		if(Success && !IsExMsg)
		{
			Unpacker.Reset(Packer6.Data(), Packer6.Size());
		}
	}

	if(Sys)
	{
		// system message
		if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_DETAILS)
		{
			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
			SHA256_DIGEST *pMapSha256 = (SHA256_DIGEST *)Unpacker.GetRaw(sizeof(*pMapSha256));
			int MapCrc = Unpacker.GetInt();
			int MapSize = Unpacker.GetInt();
			if(Unpacker.Error())
			{
				return;
			}

			const char *pMapUrl = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if(Unpacker.Error())
			{
				pMapUrl = "";
			}

			CMapDetails MapDetails;
			str_copy(MapDetails.m_aName, pMap);
			MapDetails.m_Size = MapSize;
			MapDetails.m_Crc = MapCrc;
			MapDetails.m_Sha256 = *pMapSha256;
			str_copy(MapDetails.m_aUrl, pMapUrl);
			m_MapDetails = MapDetails;
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CAPABILITIES)
		{
			if(!m_CanReceiveServerCapabilities)
			{
				return;
			}
			int Version = Unpacker.GetInt();
			int Flags = Unpacker.GetInt();
			if(Unpacker.Error() || Version <= 0)
			{
				return;
			}
			m_ServerCapabilities = GetServerCapabilities(Version, Flags, IsSixup());
			m_CanReceiveServerCapabilities = false;
			m_ServerSentCapabilities = true;
			if(m_ServerCapabilities.m_Kcp && !m_KcpNegotiated && !m_KcpNegotiationPending)
			{
				SendKcpCapability(Conn);
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_KCP_ACCEPT)
		{
			const int Version = Unpacker.GetInt();
			const int Conv = Unpacker.GetInt();
			if(Unpacker.Error() || Version != 1 || Conv <= 0)
			{
				return;
			}
			if(!m_aNetClient[Conn].ActivateKcp((uint32_t)Conv))
			{
				m_KcpNegotiationPending = false;
				m_KcpNegotiated = false;
				m_KcpNegotiationConv = 0;
				if(g_Config.m_Debug)
				{
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "kcp negotiation accepted but activation failed", gs_ClientNetworkErrPrintColor);
				}
				return;
			}
			m_KcpNegotiationPending = false;
			m_KcpNegotiated = true;
			m_KcpNegotiationConv = Conv;
			SendKcpProbe(Conn);
			if(g_Config.m_Debug)
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "kcp negotiation accepted conv=%d", Conv);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, gs_ClientNetworkPrintColor);
			}
		}
		else if(Conn == CONN_MAIN && Msg == NETMSG_KCP_FALLBACK)
		{
			const char *pReason = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if(Unpacker.Error())
			{
				return;
			}
			m_KcpNegotiationPending = false;
			m_KcpNegotiated = false;
			m_KcpNegotiationConv = 0;
			m_aNetClient[Conn].DeactivateKcp();
			if(g_Config.m_Debug)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "kcp negotiation fallback reason='%s'", pReason);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, gs_ClientNetworkPrintColor);
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CLIENT_BRANDS)
		{
			GameClient()->OnClientBrandsMessage(&Unpacker);
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_CHANGE)
		{
			if(m_CanReceiveServerCapabilities)
			{
				m_ServerCapabilities = GetServerCapabilities(0, 0, IsSixup());
				m_CanReceiveServerCapabilities = false;
			}
			std::optional<CMapDetails> MapDetails = std::nullopt;
			std::swap(MapDetails, m_MapDetails);

			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
			int MapCrc = Unpacker.GetInt();
			int MapSize = Unpacker.GetInt();
			if(Unpacker.Error())
			{
				return;
			}
			if(MapSize < 0 || MapSize > 1024 * 1024 * 1024) // 1 GiB
			{
				DisconnectWithReason("invalid map size");
				return;
			}

			if(!str_valid_filename(pMap))
			{
				DisconnectWithReason("map name is not a valid filename");
				return;
			}

			if(m_DummyConnected && !m_DummyReconnectOnReload)
			{
				DummyDisconnect(nullptr);
			}

			ResetMapDownload(true);

			std::optional<SHA256_DIGEST> MapSha256;
			const char *pMapUrl = nullptr;
			if(MapDetails.has_value() &&
				str_comp(MapDetails->m_aName, pMap) == 0 &&
				MapDetails->m_Size == MapSize &&
				MapDetails->m_Crc == MapCrc)
			{
				MapSha256 = MapDetails->m_Sha256;
				pMapUrl = MapDetails->m_aUrl[0] ? MapDetails->m_aUrl : nullptr;
			}

			if(LoadMapSearch(pMap, MapSha256, MapCrc) == nullptr)
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "loading done");
				SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_SENDING_READY);
				SendReady(CONN_MAIN);
			}
			else
			{
				// start map download
				FormatMapDownloadFilename(pMap, MapSha256, MapCrc, false, m_aMapdownloadFilename, sizeof(m_aMapdownloadFilename));
				FormatMapDownloadFilename(pMap, MapSha256, MapCrc, true, m_aMapdownloadFilenameTemp, sizeof(m_aMapdownloadFilenameTemp));

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "starting to download map to '%s'", m_aMapdownloadFilenameTemp);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", aBuf);

				str_copy(m_aMapdownloadName, pMap);
				m_MapdownloadSha256 = MapSha256;
				m_MapdownloadCrc = MapCrc;
				m_MapdownloadTotalsize = MapSize;

				if(MapSha256.has_value())
				{
					char aUrl[256];
					char aEscaped[256];
					EscapeUrl(aEscaped, str_startswith(m_aMapdownloadFilename, "downloadedmaps/"));
					bool UseConfigUrl = str_comp(g_Config.m_ClMapDownloadUrl, "https://maps.ddnet.org") != 0 || m_aMapDownloadUrl[0] == '\0';
					str_format(aUrl, sizeof(aUrl), "%s/%s", UseConfigUrl ? g_Config.m_ClMapDownloadUrl : m_aMapDownloadUrl, aEscaped);

					m_pMapdownloadTask = HttpGetFile(pMapUrl ? pMapUrl : aUrl, Storage(), m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
					m_pMapdownloadTask->Timeout(CTimeout{g_Config.m_ClMapDownloadConnectTimeoutMs, 0, g_Config.m_ClMapDownloadLowSpeedLimit, g_Config.m_ClMapDownloadLowSpeedTime});
					m_pMapdownloadTask->MaxResponseSize(MapSize);
					m_pMapdownloadTask->ExpectSha256(MapSha256.value());
					Http()->Run(m_pMapdownloadTask);
				}
				else
				{
					SendMapRequest();
				}
			}
		}
		else if(Conn == CONN_MAIN && Msg == NETMSG_MAP_DATA)
		{
			if(!m_MapdownloadFileTemp)
			{
				return;
			}
			int Last = -1;
			int MapCRC = -1;
			int Chunk = -1;
			int Size = -1;

			if(IsSixup())
			{
				if(m_TranslationContext.m_MapdownloadTotalsize <= 0 ||
					m_TranslationContext.m_MapDownloadChunkSize <= 0 ||
					m_TranslationContext.m_MapDownloadChunksPerRequest <= 0)
				{
					return;
				}
				MapCRC = m_MapdownloadCrc;
				Chunk = m_MapdownloadChunk;
				Size = minimum(m_TranslationContext.m_MapDownloadChunkSize, m_TranslationContext.m_MapdownloadTotalsize - m_MapdownloadAmount);
			}
			else
			{
				Last = Unpacker.GetInt();
				MapCRC = Unpacker.GetInt();
				Chunk = Unpacker.GetInt();
				Size = Unpacker.GetInt();
			}

			const unsigned char *pData = Unpacker.GetRaw(Size);
			if(Unpacker.Error() || Size <= 0 || MapCRC != m_MapdownloadCrc || Chunk != m_MapdownloadChunk)
			{
				return;
			}

			io_write(m_MapdownloadFileTemp, pData, Size);

			m_MapdownloadAmount += Size;

			if(IsSixup())
				Last = m_MapdownloadAmount == m_TranslationContext.m_MapdownloadTotalsize;

			if(Last)
			{
				if(m_MapdownloadFileTemp)
				{
					io_close(m_MapdownloadFileTemp);
					m_MapdownloadFileTemp = nullptr;
				}
				FinishMapDownload();
			}
			else
			{
				// request new chunk
				m_MapdownloadChunk++;

				if(IsSixup() && (m_MapdownloadChunk % m_TranslationContext.m_MapDownloadChunksPerRequest == 0))
				{
					CMsgPacker MsgP(protocol7::NETMSG_REQUEST_MAP_DATA, true, true);
					SendMsg(CONN_MAIN, &MsgP, MSGFLAG_VITAL | MSGFLAG_FLUSH);
				}
				else
				{
					CMsgPacker MsgP(NETMSG_REQUEST_MAP_DATA, true);
					MsgP.AddInt(m_MapdownloadChunk);
					SendMsg(CONN_MAIN, &MsgP, MSGFLAG_VITAL | MSGFLAG_FLUSH);
				}

				if(g_Config.m_Debug)
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "requested chunk %d", m_MapdownloadChunk);
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client/network", aBuf);
				}
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_RELOAD)
		{
			if(m_DummyConnected)
			{
				m_DummyReconnectOnReload = true;
				m_DummyDeactivateOnReconnect = g_Config.m_ClDummy == 0;
				g_Config.m_ClDummy = 0;
			}
			else
			{
				m_DummyDeactivateOnReconnect = false;
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CON_READY)
		{
			if(!m_pMap->IsLoaded())
			{
				return;
			}
			GameClient()->OnConnected();
			if(m_DummyReconnectOnReload)
			{
				m_DummySendConnInfo = true;
				m_DummyReconnectOnReload = false;
			}
#if defined(CONF_PLATFORM_IOS)
			else if(m_DummyReconnectOnResume)
			{
				m_DummyReconnectOnResume = false;
				DummyConnect();
			}
#endif
		}
		else if(Conn == CONN_DUMMY && Msg == NETMSG_CON_READY)
		{
			m_DummyConnected = true;
			m_DummyConnecting = false;
			g_Config.m_ClDummy = 1;
			Rcon("crashmeplx");
			if(m_aRconAuthed[0] && !m_aRconAuthed[1])
				RconAuth(m_aRconUsername, m_aRconPassword);
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker MsgP(NETMSG_PING_REPLY, true);
			int Vital = (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 ? MSGFLAG_VITAL : 0;
			SendMsg(Conn, &MsgP, MSGFLAG_FLUSH | Vital);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pId = (CUuid *)Unpacker.GetRaw(sizeof(*pId));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker MsgP(NETMSG_PONGEX, true);
			MsgP.AddRaw(pId, sizeof(*pId));
			int Vital = (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 ? MSGFLAG_VITAL : 0;
			SendMsg(Conn, &MsgP, MSGFLAG_FLUSH | Vital);
		}
		else if(Conn < NUM_DUMMIES && Msg == NETMSG_PONGEX)
		{
			CUuid *pId = (CUuid *)Unpacker.GetRaw(sizeof(*pId));
			if(Unpacker.Error())
			{
				return;
			}
			const int64_t Now = time_get();
			m_aGamePingProbes[Conn].HandlePong(*pId, Now, time_freq());
			if(Conn == CONN_MAIN && m_ServerCapabilities.m_PingEx && m_CurrentServerCurrentPingTime >= 0 && *pId == m_CurrentServerPingUuid)
			{
				int LatencyMs = (Now - m_CurrentServerCurrentPingTime) * 1000 / time_freq();
				const NETADDR *pServerAddr = ServerAddress();
				if(pServerAddr)
					m_ServerBrowser.SetCurrentServerPing(*pServerAddr, LatencyMs);
				m_CurrentServerInfo.m_Latency = LatencyMs;
				m_CurrentServerCurrentPingTime = -1;

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "got pong from current server, latency=%dms", LatencyMs);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
			}
		}
		else if(Msg == NETMSG_CHECKSUM_REQUEST)
		{
			CUuid *pUuid = (CUuid *)Unpacker.GetRaw(sizeof(*pUuid));
			if(Unpacker.Error())
			{
				return;
			}
			int ResultCheck = HandleChecksum(Conn, *pUuid, &Unpacker);
			if(ResultCheck)
			{
				CMsgPacker MsgP(NETMSG_CHECKSUM_ERROR, true);
				MsgP.AddRaw(pUuid, sizeof(*pUuid));
				MsgP.AddInt(ResultCheck);
				SendMsg(Conn, &MsgP, MSGFLAG_VITAL);
			}
		}
		else if(Msg == NETMSG_TATER_CHECKSUM_REQUEST)
		{
#ifndef TCLIENT_CHECKSUM_SALT
// salt@sjrc6.github.io: 26e65800-d8d9-3e8f-8d53-acdd1461f0a9
#define TCLIENT_CHECKSUM_SALT \
	{ \
		{ \
			0x26, 0xe6, 0x58, 0x00, 0xd8, 0xd9, 0x3e, 0x8f, \
				0x8d, 0x53, 0xac, 0xdd, 0x14, 0x61, 0xf0, 0xa9, \
		} \
	}
#endif
			CUuid *pUuid = (CUuid *)Unpacker.GetRaw(sizeof(*pUuid));
			if(Unpacker.Error())
			{
				return;
			}
			SHA256_CTX Sha256Ctxt;
			sha256_init(&Sha256Ctxt);
			CUuid Salt = TCLIENT_CHECKSUM_SALT;
			sha256_update(&Sha256Ctxt, &Salt, sizeof(Salt));
			sha256_update(&Sha256Ctxt, pUuid, sizeof(*pUuid));
			SHA256_DIGEST Sha256 = sha256_finish(&Sha256Ctxt);

			CMsgPacker CSMsg(NETMSG_TATER_CHECKSUM_RESPONSE, true);
			CSMsg.AddRaw(pUuid, sizeof(*pUuid));
			CSMsg.AddRaw(&Sha256, sizeof(Sha256));
			SendMsg(Conn, &CSMsg, MSGFLAG_VITAL);
		}
		else if(Msg == NETMSG_RECONNECT)
		{
			if(Conn == CONN_MAIN)
			{
				Connect(m_aConnectAddressStr);
			}
			else
			{
				DummyDisconnect("reconnect");
				// Reset dummy connect time to allow immediate reconnect
				m_LastDummyConnectTime = 0.0f;
				DummyConnect();
			}
		}
		else if(Msg == NETMSG_REDIRECT)
		{
			int RedirectPort = Unpacker.GetInt();
			if(Unpacker.Error())
			{
				return;
			}
			if(Conn == CONN_MAIN)
			{
				const NETADDR *pServerAddr = ServerAddress();
				if(!pServerAddr)
					return;
				NETADDR ServerAddr = *pServerAddr;
				ServerAddr.port = RedirectPort;
				char aAddr[NETADDR_MAXSTRSIZE];
				net_addr_str(&ServerAddr, aAddr, sizeof(aAddr), true);
				Connect(aAddr);
			}
			else
			{
				DummyDisconnect("redirect");
				const NETADDR *pServerAddr = ServerAddress();
				if(!pServerAddr || pServerAddr->port != RedirectPort)
				{
					// Only allow redirecting to the same port to reconnect. The dummy
					// should not be connected to a different server than the main, as
					// the client assumes that main and dummy use the same map.
					return;
				}
				// Reset dummy connect time to allow immediate reconnect
				m_LastDummyConnectTime = 0.0f;
				DummyConnect();
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_ADD)
		{
			const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			const char *pHelp = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			const char *pParams = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if(!Unpacker.Error())
			{
				m_pConsole->RegisterTemp(pName, pParams, CFGFLAG_SERVER, pHelp);
				GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
			}
			m_GotRconCommands++;
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_REM)
		{
			const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if(!Unpacker.Error())
			{
				m_pConsole->DeregisterTemp(pName);
				GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
			}
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_AUTH_STATUS)
		{
			int ResultInt = Unpacker.GetInt();
			if(!Unpacker.Error())
			{
				m_aRconAuthed[Conn] = ResultInt;

				if(m_aRconAuthed[Conn])
					RconAuth(m_aRconUsername, m_aRconPassword, g_Config.m_ClDummy ^ 1);
			}
			if(Conn == CONN_MAIN)
			{
				int Old = m_UseTempRconCommands;
				m_UseTempRconCommands = Unpacker.GetInt();
				if(Unpacker.Error())
				{
					m_UseTempRconCommands = 0;
				}
				if(Old != 0 && m_UseTempRconCommands == 0)
				{
					m_pConsole->DeregisterTempAll();
					m_ExpectedRconCommands = -1;
					m_vMaplistEntries.clear();
					GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
					m_ExpectedMaplistEntries = -1;
				}
			}
		}
		else if(!Dummy && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_LINE)
		{
			const char *pLine = Unpacker.GetString();
			if(!Unpacker.Error())
			{
				GameClient()->OnRconLine(pLine);
			}
		}
		else if(Msg == NETMSG_PING_REPLY)
		{
			if(Conn < NUM_DUMMIES)
				m_aGamePingProbes[Conn].HandleLegacyPong(time_get(), time_freq());
			float RttMs;
			if(Conn == CONN_MAIN && m_ManualPingProbe.HandlePong(time_get(), time_freq(), RttMs))
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "latency %.2f", RttMs);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client/network", aBuf);
			}
		}
		else if(Msg == NETMSG_INPUTTIMING)
		{
			int InputPredTick = Unpacker.GetInt();
			int TimeLeft = Unpacker.GetInt();
			if(Unpacker.Error())
			{
				return;
			}

			int64_t Now = time_get();

			// adjust our prediction time
			int64_t Target = 0;
			for(int k = 0; k < 200; k++)
			{
				if(m_aInputs[Conn][k].m_Tick == InputPredTick)
				{
					Target = m_aInputs[Conn][k].m_PredictedTime + (Now - m_aInputs[Conn][k].m_Time);
					if(g_Config.m_TcSmoothPredictionMargin)
						Target = Target - (int64_t)((TimeLeft / 1000.0f) * time_freq()) + m_aInputs[Conn][k].m_PredictionMargin;
					else
						Target = Target - (int64_t)((TimeLeft / 1000.0f) * time_freq());
					break;
				}
			}

			if(Target)
			{
				const CSmoothTime::EUpdateStatus UpdateStatus = m_PredictedTime.Update(&m_InputtimeMarginGraph, Target, TimeLeft, CSmoothTime::ADJUSTDIRECTION_UP);
				switch(UpdateStatus)
				{
				case CSmoothTime::EUpdateStatus::IGNORED_SPIKE:
					m_PredictionMarginState = EPredictionMarginState::IGNORED_SPIKE;
					break;
				case CSmoothTime::EUpdateStatus::UNSTABLE:
					m_PredictionMarginState = EPredictionMarginState::UNSTABLE;
					break;
				case CSmoothTime::EUpdateStatus::STABLE:
					m_PredictionMarginState = EPredictionMarginState::STABLE;
					break;
				}
			}
		}
		else if(Msg == NETMSG_SNAP || Msg == NETMSG_SNAPSINGLE || Msg == NETMSG_SNAPEMPTY)
		{
			// 还不能处理快照。
			if(State() < IClient::STATE_LOADING ||
				!m_pMap->IsLoaded())
			{
				return;
			}

			int GameTick = Unpacker.GetInt();
			int DeltaTick = GameTick - Unpacker.GetInt();

			int NumParts = 1;
			int Part = 0;
			if(Msg == NETMSG_SNAP)
			{
				NumParts = Unpacker.GetInt();
				Part = Unpacker.GetInt();
			}

			unsigned int Crc = 0;
			int PartSize = 0;
			if(Msg != NETMSG_SNAPEMPTY)
			{
				Crc = Unpacker.GetInt();
				PartSize = Unpacker.GetInt();
			}

			const char *pData = (const char *)Unpacker.GetRaw(PartSize);
			if(Unpacker.Error() || NumParts < 1 || NumParts > CSnapshot::MAX_PARTS || Part < 0 || Part >= NumParts || PartSize < 0 || PartSize > MAX_SNAPSHOT_PACKSIZE)
			{
				return;
			}

			// Count protocol-valid snapshot part payload before assembly filtering. This is
			// the compressed snapshot traffic accepted by the client, including duplicates
			// or incomplete snapshots, rather than only successfully stored snapshots.
			m_aSnapshotStats[Conn].m_PartCount++;
			m_aSnapshotStats[Conn].m_PayloadBytes += (uint64_t)PartSize;

			// Check m_aAckGameTick to see if we already got a snapshot for that tick
			if(GameTick >= m_aCurrentRecvTick[Conn] && GameTick > m_aAckGameTick[Conn])
			{
				if(GameTick != m_aCurrentRecvTick[Conn])
				{
					m_aSnapshotParts[Conn] = 0;
					m_aCurrentRecvTick[Conn] = GameTick;
					m_aSnapshotIncomingDataSize[Conn] = 0;
				}

				const uint64_t PartMask = uint64_t{1} << Part;
				mem_copy((char *)m_aaSnapshotIncomingData[Conn] + Part * MAX_SNAPSHOT_PACKSIZE, pData, std::clamp(PartSize, 0, (int)sizeof(m_aaSnapshotIncomingData[Conn]) - Part * MAX_SNAPSHOT_PACKSIZE));
				m_aSnapshotParts[Conn] |= PartMask;

				if(Part == NumParts - 1)
				{
					m_aSnapshotIncomingDataSize[Conn] = (NumParts - 1) * MAX_SNAPSHOT_PACKSIZE + PartSize;
				}

				if((NumParts < CSnapshot::MAX_PARTS && m_aSnapshotParts[Conn] == (((uint64_t)(1) << NumParts) - 1)) ||
					(NumParts == CSnapshot::MAX_PARTS && m_aSnapshotParts[Conn] == std::numeric_limits<uint64_t>::max()))
				{
					CSnapshotDeltaBuffer TmpBuffer2;
					CSnapshotBuffer TmpBuffer3;

					// reset snapshotting
					m_aSnapshotParts[Conn] = 0;

					// find snapshot that we should use as delta
					const CSnapshot *pDeltaShot = CSnapshot::EmptySnapshot();
					if(DeltaTick >= 0)
					{
						int DeltashotSize = m_aSnapshotStorage[Conn].Get(DeltaTick, nullptr, &pDeltaShot, nullptr);

						if(DeltashotSize < 0)
						{
							// couldn't find the delta snapshots that the server used
							// to compress this snapshot. force the server to resync
							if(g_Config.m_Debug)
							{
								m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", "error, couldn't find the delta snapshot");
							}

							// ack snapshot
							m_aAckGameTick[Conn] = -1;
							SendInput();
							return;
						}
					}

					// decompress snapshot
					const void *pDeltaData = SnapshotDelta()->EmptyDelta().data();
					int DeltaSize = sizeof(int) * 3;

					if(m_aSnapshotIncomingDataSize[Conn])
					{
						int IntSize = CVariableInt::Decompress(m_aaSnapshotIncomingData[Conn], m_aSnapshotIncomingDataSize[Conn], TmpBuffer2.m_aData, sizeof(TmpBuffer2.m_aData));

						if(IntSize < 0) // failure during decompression
							return;

						pDeltaData = TmpBuffer2.m_aData;
						DeltaSize = IntSize;
					}

					// unpack delta
					// TODO: this needs alignment for
					// `m_aChunkData` of 4, but this is not
					// guaranteed. This is assumed above,
					// too, anyway, in
					// `CVariableInt::Decompress`.
					const int SnapSize = SnapshotDelta()->UnpackDelta(*pDeltaShot, TmpBuffer3, rust::Slice((const int32_t *)pDeltaData, DeltaSize / sizeof(int32_t)));
					if(SnapSize < 0)
					{
						dbg_msg("client", "delta unpack failed. error=%d", SnapSize);
						return;
					}
					if(!TmpBuffer3.AsSnapshot()->IsValid(SnapSize))
					{
						dbg_msg("client", "snapshot invalid. SnapSize=%d, DeltaSize=%d", SnapSize, DeltaSize);
						return;
					}

					if(Msg != NETMSG_SNAPEMPTY && TmpBuffer3.AsSnapshot()->Crc() != Crc)
					{
						log_error("client", "snapshot crc error #%d - tick=%d wantedcrc=%d gotcrc=%d compressed_size=%d delta_tick=%d",
							m_SnapCrcErrors, GameTick, Crc, TmpBuffer3.AsSnapshot()->Crc(), m_aSnapshotIncomingDataSize[Conn], DeltaTick);

						m_SnapCrcErrors++;
						if(m_SnapCrcErrors > 10)
						{
							// to many errors, send reset
							m_aAckGameTick[Conn] = -1;
							SendInput();
							m_SnapCrcErrors = 0;
						}
						return;
					}
					else
					{
						if(m_SnapCrcErrors)
							m_SnapCrcErrors--;
					}

					// purge old snapshots
					int PurgeTick = DeltaTick;
					if(m_aapSnapshots[Conn][SNAP_PREV] && m_aapSnapshots[Conn][SNAP_PREV]->m_Tick < PurgeTick)
						PurgeTick = m_aapSnapshots[Conn][SNAP_PREV]->m_Tick;
					if(m_aapSnapshots[Conn][SNAP_CURRENT] && m_aapSnapshots[Conn][SNAP_CURRENT]->m_Tick < PurgeTick)
						PurgeTick = m_aapSnapshots[Conn][SNAP_CURRENT]->m_Tick;
					m_aSnapshotStorage[Conn].PurgeUntil(PurgeTick);

					// create a verified and unpacked snapshot
					int AltSnapSize = -1;
					CSnapshotBuffer AltSnapBuffer;

					if(IsSixup())
					{
						CSnapshotBuffer TmpTransSnapBuffer;
						mem_copy(&TmpTransSnapBuffer, &TmpBuffer3, sizeof(TmpTransSnapBuffer));
						AltSnapSize = GameClient()->TranslateSnap(&AltSnapBuffer, TmpTransSnapBuffer.AsSnapshot(), Conn, Dummy);
					}
					else
					{
						AltSnapSize = UnpackAndValidateSnapshot(TmpBuffer3.AsSnapshot(), &AltSnapBuffer);
					}

					if(AltSnapSize < 0)
					{
						dbg_msg("client", "unpack snapshot and validate failed. error=%d", AltSnapSize);
						return;
					}

					// add new
					const int64_t SnapshotTime = time_get();
					m_aSnapshotStorage[Conn].Add(GameTick, SnapshotTime, SnapSize, TmpBuffer3.AsSnapshot(), AltSnapSize, AltSnapBuffer.AsSnapshot());

					SClientSnapshotStats &SnapshotStats = m_aSnapshotStats[Conn];
					if(SnapshotStats.m_SnapshotCount > 0)
					{
						SnapshotStats.m_LastTickGap = GameTick - m_aLastSnapshotTick[Conn];
					}
					SnapshotStats.m_SnapshotCount++;
					m_aLastSnapshotTime[Conn] = SnapshotTime;
					m_aLastSnapshotTick[Conn] = GameTick;

					if(!Dummy)
					{
						GameClient()->ProcessDemoSnapshot(TmpBuffer3.AsSnapshot());

						CSnapshotBuffer SnapSeven;
						int DemoSnapSize = SnapSize;
						if(IsSixup())
						{
							DemoSnapSize = GameClient()->OnDemoRecSnap7(TmpBuffer3.AsSnapshot(), &SnapSeven, Conn);
							if(DemoSnapSize < 0)
							{
								dbg_msg("sixup", "demo snapshot failed. error=%d", DemoSnapSize);
							}
						}

						if(DemoSnapSize >= 0)
						{
							// add snapshot to demo
							for(auto &DemoRecorder : DemoRecorders())
							{
								if(DemoRecorder.IsRecording())
								{
									// write snapshot
									DemoRecorder.RecordSnapshot(GameTick, IsSixup() ? SnapSeven.AsSnapshot() : TmpBuffer3.AsSnapshot(), DemoSnapSize);
								}
							}
						}
					}

					// apply snapshot, cycle pointers
					m_aReceivedSnapshots[Conn]++;

					// TClient
					if(!m_aExecuteOnJoinDone[Conn] && m_aReceivedSnapshots[Conn] > g_Config.m_TcExecuteOnJoinDelay)
					{
						m_aExecuteOnJoinDone[Conn] = true;
						if(g_Config.m_TcExecuteOnJoin[0] != '\0')
							m_pConsole->ExecuteLine(g_Config.m_TcExecuteOnJoin, IConsole::CLIENT_ID_UNSPECIFIED);
					}

					// we got two snapshots until we see us self as connected
					if(m_aReceivedSnapshots[Conn] == 2)
					{
						m_aGameTime[Conn].Init((GameTick - 1) * time_freq() / GameTickSpeed());
						// start at 200ms and work from there
						if(!Dummy)
						{
							m_PredictedTime.Init(GameTick * time_freq() / GameTickSpeed());
							m_PredictedTime.SetAdjustSpeed(CSmoothTime::ADJUSTDIRECTION_UP, 1000.0f);
							UpdatePredictionMargin();
							m_PredictedTime.UpdateMargin(PredictionMargin() * time_freq() / 1000);
						}
						m_aapSnapshots[Conn][SNAP_PREV] = m_aSnapshotStorage[Conn].m_pFirst;
						m_aapSnapshots[Conn][SNAP_CURRENT] = m_aSnapshotStorage[Conn].m_pLast;
						m_aPrevGameTick[Conn] = m_aapSnapshots[Conn][SNAP_PREV]->m_Tick;
						m_aCurGameTick[Conn] = m_aapSnapshots[Conn][SNAP_CURRENT]->m_Tick;
						if(Conn == CONN_MAIN)
						{
							m_LocalStartTime = time_get();
#if defined(CONF_VIDEORECORDER)
							IVideo::SetLocalStartTime(m_LocalStartTime);
#endif
						}
						if(!Dummy)
						{
							GameClient()->OnNewSnapshot(false);
						}
						SetState(IClient::STATE_ONLINE);
						if(Conn == CONN_MAIN)
						{
							DemoRecorder_HandleAutoStart();
						}
					}

					// adjust game time
					if(m_aReceivedSnapshots[Conn] > 2)
					{
						int64_t Now = m_aGameTime[Conn].Get(time_get());
						int64_t TickStart = GameTick * time_freq() / GameTickSpeed();
						int64_t TimeLeft = (TickStart - Now) * 1000 / time_freq();
						m_aLastGameTimeMarginMs[Conn] = (float)TimeLeft;
						m_aGameTime[Conn].Update(&m_aGametimeMarginGraphs[Conn], (GameTick - 1) * time_freq() / GameTickSpeed(), TimeLeft, CSmoothTime::ADJUSTDIRECTION_DOWN);
					}

					if(m_aReceivedSnapshots[Conn] > GameTickSpeed() && !m_aDidPostConnect[Conn])
					{
						OnPostConnect(Conn);
						m_aDidPostConnect[Conn] = true;
					}

					// ack snapshot
					m_aAckGameTick[Conn] = GameTick;
				}
			}
		}
		else if(Conn == CONN_MAIN && Msg == NETMSG_RCONTYPE)
		{
			bool UsernameReq = Unpacker.GetInt() & 1;
			if(!Unpacker.Error())
			{
				GameClient()->OnRconType(UsernameReq);
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_GROUP_START)
		{
			const int ExpectedRconCommands = Unpacker.GetInt();
			if(Unpacker.Error() || ExpectedRconCommands < 0)
				return;

			m_ExpectedRconCommands = ExpectedRconCommands;
			m_GotRconCommands = 0;
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_GROUP_END)
		{
			m_ExpectedRconCommands = -1;
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAPLIST_ADD)
		{
			while(true)
			{
				const char *pMapName = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
				if(Unpacker.Error())
				{
					return;
				}
				if(pMapName[0] != '\0')
				{
					m_vMaplistEntries.emplace_back(pMapName);
					GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
				}
			}
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAPLIST_GROUP_START)
		{
			const int ExpectedMaplistEntries = Unpacker.GetInt();
			if(Unpacker.Error() || ExpectedMaplistEntries < 0)
				return;

			m_vMaplistEntries.clear();
			GameClient()->ForceUpdateConsoleRemoteCompletionSuggestions();
			m_ExpectedMaplistEntries = ExpectedMaplistEntries;
		}
		else if(Conn == CONN_MAIN && (pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAPLIST_GROUP_END)
		{
			m_ExpectedMaplistEntries = -1;
		}
	}
	// the client handles only vital messages https://github.com/ddnet/ddnet/issues/11178
	else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 || Msg == NETMSGTYPE_SV_PREINPUT)
	{
		// game message
		if(!Dummy)
		{
			for(auto &DemoRecorder : DemoRecorders())
			{
				if(DemoRecorder.IsRecording())
				{
					DemoRecorder.RecordMessage(pPacket->m_pData, pPacket->m_DataSize);
				}
			}
		}

		GameClient()->OnMessage(Msg, &Unpacker, Conn, Dummy);
	}
}

int CClient::UnpackAndValidateSnapshot(CSnapshot *pFrom, CSnapshotBuffer *pTo)
{
	CUnpacker Unpacker;
	rust::Box<CSnapshotBuilder> pBuilder = CSnapshotBuilder::New();
	pBuilder->Init(false);
	CNetObjHandler *pNetObjHandler = GameClient()->GetNetObjHandler();

	int Num = pFrom->NumItems();
	for(int Index = 0; Index < Num; Index++)
	{
		const CSnapshotItem *pFromItem = pFrom->GetItem(Index);
		const int FromItemSize = pFrom->GetItemSize(Index);
		const int ItemType = pFrom->GetItemType(Index);
		if(ItemType <= 0)
		{
			// Don't add extended item type descriptions, they get
			// added implicitly (== 0).
			//
			// Don't add items of unknown item types either (< 0).
			continue;
		}
		const void *pData = pFromItem->Data();
		Unpacker.Reset(pData, FromItemSize);

		const void *pSecuredData = pNetObjHandler->SecureUnpackObj(ItemType, &Unpacker);
		if(!pSecuredData)
		{
			if(g_Config.m_Debug && ItemType != UUID_UNKNOWN)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "dropped weird object '%s' (%d), failed on '%s'", pNetObjHandler->GetObjName(ItemType), ItemType, pNetObjHandler->FailedObjOn());
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
			}
			continue;
		}
		const int ItemSize = pNetObjHandler->GetUnpackedObjSize(ItemType);

		if(!pBuilder->NewItem(ItemType, pFromItem->Id(), rust::Slice((const int32_t *)pSecuredData, ItemSize / sizeof(int32_t))))
		{
			return -4;
		}
	}

	return pBuilder->Finish(*pTo);
}

void CClient::ResetMapDownload(bool ResetActive)
{
	if(m_pMapdownloadTask)
	{
		m_pMapdownloadTask->Abort();
		m_pMapdownloadTask = nullptr;
	}

	if(m_MapdownloadFileTemp)
	{
		io_close(m_MapdownloadFileTemp);
		m_MapdownloadFileTemp = nullptr;
	}

	if(Storage()->FileExists(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE))
	{
		Storage()->RemoveFile(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
	}

	if(ResetActive)
	{
		m_MapdownloadChunk = 0;
		m_MapdownloadSha256 = std::nullopt;
		m_MapdownloadCrc = 0;
		m_MapdownloadTotalsize = -1;
		m_MapdownloadAmount = 0;
		m_aMapdownloadFilename[0] = '\0';
		m_aMapdownloadFilenameTemp[0] = '\0';
		m_aMapdownloadName[0] = '\0';
	}
}

void CClient::FinishMapDownload()
{
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "download complete, loading map");

	bool FileSuccess = true;
	FileSuccess &= Storage()->RemoveFile(m_aMapdownloadFilename, IStorage::TYPE_SAVE);
	FileSuccess &= Storage()->RenameFile(m_aMapdownloadFilenameTemp, m_aMapdownloadFilename, IStorage::TYPE_SAVE);
	if(!FileSuccess)
	{
		char aError[128 + IO_MAX_PATH_LENGTH];
		str_format(aError, sizeof(aError), Localize("Could not save downloaded map. Try manually deleting this file: %s"), m_aMapdownloadFilename);
		DisconnectWithReason(aError);
		return;
	}

	const char *pError = LoadMap(m_aMapdownloadName, m_aMapdownloadFilename, m_MapdownloadSha256, m_MapdownloadCrc);
	if(!pError)
	{
		ResetMapDownload(true);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "loading done");
		SendReady(CONN_MAIN);
	}
	else if(m_pMapdownloadTask) // fallback
	{
		ResetMapDownload(false);
		SendMapRequest();
	}
	else
	{
		DisconnectWithReason(pError);
	}
}

void CClient::ResetDDNetInfoTask()
{
	if(m_pDDNetInfoTask)
	{
		m_pDDNetInfoTask->Abort();
		m_pDDNetInfoTask = nullptr;
	}
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidVersion = std::make_tuple(-1, -1, -1);

static TVersion ToVersion(char *pStr)
{
	int aVersion[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return gs_InvalidVersion;

		aVersion[i] = str_toint(p);
		p = strtok(nullptr, ".");
	}

	if(p)
		return gs_InvalidVersion;

	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CClient::LoadDDNetInfo()
{
	const json_value *pDDNetInfo = m_ServerBrowser.LoadDDNetInfo();

	if(!pDDNetInfo)
	{
		m_InfoState = EInfoState::ERROR;
		return;
	}

	const json_value &DDNetInfo = *pDDNetInfo;
	const json_value &CurrentVersion = DDNetInfo["version"];
	if(CurrentVersion.type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, CurrentVersion);
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, GAME_RELEASE_VERSION);
		if(ToVersion(aNewVersionStr) > ToVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, CurrentVersion);
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
	}

	const json_value &News = DDNetInfo["news"];
	if(News.type == json_string)
	{
		// Only mark news button if something new was added to the news
		if(m_aNews[0] && str_find(m_aNews, News) == nullptr)
			g_Config.m_UiUnreadNews = true;

		str_copy(m_aNews, News);
	}

	const json_value &MapDownloadUrl = DDNetInfo["map-download-url"];
	if(MapDownloadUrl.type == json_string)
	{
		str_copy(m_aMapDownloadUrl, MapDownloadUrl);
	}

	const json_value &Points = DDNetInfo["points"];
	if(Points.type == json_integer)
	{
		m_Points = Points.u.integer;
	}

	const json_value &StunServersIpv6 = DDNetInfo["stun-servers-ipv6"];
	if(StunServersIpv6.type == json_array && StunServersIpv6[0].type == json_string)
	{
		NETADDR Addr;
		if(!net_addr_from_str(&Addr, StunServersIpv6[0]))
		{
			m_aNetClient[CONN_MAIN].FeedStunServer(Addr);
		}
	}
	const json_value &StunServersIpv4 = DDNetInfo["stun-servers-ipv4"];
	if(StunServersIpv4.type == json_array && StunServersIpv4[0].type == json_string)
	{
		NETADDR Addr;
		if(!net_addr_from_str(&Addr, StunServersIpv4[0]))
		{
			m_aNetClient[CONN_MAIN].FeedStunServer(Addr);
		}
	}
	const json_value &ConnectingIp = DDNetInfo["connecting-ip"];
	if(ConnectingIp.type == json_string)
	{
		NETADDR Addr;
		if(!net_addr_from_str(&Addr, ConnectingIp))
		{
			m_HaveGlobalTcpAddr = true;
			m_GlobalTcpAddr = Addr;
			log_debug("info", "got global tcp ip address: %s", (const char *)ConnectingIp);
		}
	}
	const json_value &WarnPngliteIncompatibleImages = DDNetInfo["warn-pnglite-incompatible-images"];
	Graphics()->WarnPngliteIncompatibleImages(WarnPngliteIncompatibleImages.type == json_boolean && (bool)WarnPngliteIncompatibleImages);
	m_InfoState = EInfoState::SUCCESS;
}

int CClient::ConnectNetTypes() const
{
	const NETADDR *pConnectAddrs;
	int NumConnectAddrs;
	m_aNetClient[CONN_MAIN].ConnectAddresses(&pConnectAddrs, &NumConnectAddrs);
	int NetType = 0;
	for(int i = 0; i < NumConnectAddrs; i++)
	{
		NetType |= pConnectAddrs[i].type;
	}
	return NetType;
}

void CClient::PumpNetwork()
{
#if defined(CONF_PLATFORM_IOS)
	RecreateBrokenSockets();
#endif

	for(int Conn = 0; Conn < NUM_CONNS; ++Conn)
	{
		m_aNetClient[Conn].SetLowLatency(g_Config.m_QmNetQos && (Conn == CONN_MAIN || Conn == CONN_DUMMY));
		m_aNetClient[Conn].Update();
	}

	if(State() != IClient::STATE_DEMOPLAYBACK)
	{
		// check for errors of main and dummy
		if(State() != IClient::STATE_OFFLINE && State() < IClient::STATE_QUITTING)
		{
			if(m_aNetClient[CONN_MAIN].State() == NETSTATE_OFFLINE)
			{
				// This will also disconnect the dummy, so the branch below is an `else if`
				Disconnect();
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "offline error='%s'", m_aNetClient[CONN_MAIN].ErrorString());
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, gs_ClientNetworkErrPrintColor);
			}
			else if((DummyConnecting() || DummyConnected()) && m_aNetClient[CONN_DUMMY].State() == NETSTATE_OFFLINE)
			{
				const bool WasConnecting = DummyConnecting();
				DummyDisconnect(nullptr);
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "offline dummy error='%s'", m_aNetClient[CONN_DUMMY].ErrorString());
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, gs_ClientNetworkErrPrintColor);
				if(WasConnecting)
				{
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Could not connect dummy"), m_aNetClient[CONN_DUMMY].ErrorString());
					GameClient()->Echo(aBuf);
				}
			}
		}

		// check if main was connected
		if(State() == IClient::STATE_CONNECTING && m_aNetClient[CONN_MAIN].State() == NETSTATE_ONLINE)
		{
			// we switched to online
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "connected, sending info", gs_ClientNetworkPrintColor);
			SetState(IClient::STATE_LOADING);
			SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_INITIAL);
			SendInfo(CONN_MAIN);
		}
		if(m_KcpNegotiationPending && !m_KcpNegotiated && time_get() - m_KcpNegotiationStartTime > time_freq() * 3)
		{
			m_KcpNegotiationPending = false;
			m_KcpNegotiationStartTime = 0;
			if(g_Config.m_Debug)
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "kcp negotiation timed out, using legacy udp", gs_ClientNetworkPrintColor);
			}
		}

		// progress on dummy connect when the connection is online
		if(m_DummySendConnInfo && m_aNetClient[CONN_DUMMY].State() == NETSTATE_ONLINE)
		{
			m_DummySendConnInfo = false;
			SendInfo(CONN_DUMMY);
			m_aNetClient[CONN_DUMMY].Update();
			SendReady(CONN_DUMMY);
			GameClient()->SendDummyInfo(true);
			SendEnterGame(CONN_DUMMY);
		}
	}

	// process packets
	CNetChunk Packet;
	SECURITY_TOKEN ResponseToken;
	const std::chrono::nanoseconds NetworkPumpStart = time_get_nanoseconds();
	const std::chrono::nanoseconds NetworkPumpBudget = State() == IClient::STATE_ONLINE ? gs_NetworkPumpOnlineBudget : gs_NetworkPumpLoadingBudget;
	const bool PerfEnabled = QmPerfEnabled();
	double aNetworkRecvMs[NUM_CONNS] = {};
	double aNetworkProcessMs[NUM_CONNS] = {};
	int aNetworkChunks[NUM_CONNS] = {};
	double MaxNetworkProcessMs = 0.0;
	int MaxNetworkProcessConn = -1;
	int MaxNetworkPacketBytes = 0;
	int NetworkChunksProcessed = 0;
	const int FirstConn = m_NetworkPumpFirstConn;
	m_NetworkPumpFirstConn = (FirstConn + 1) % NUM_CONNS;
	for(int ConnIndex = 0; ConnIndex < NUM_CONNS; ConnIndex++)
	{
		const int Conn = (FirstConn + ConnIndex) % NUM_CONNS;
		while(NetworkChunksProcessed < gs_NetworkPumpMaxChunksPerFrame &&
			(NetworkChunksProcessed == 0 || time_get_nanoseconds() - NetworkPumpStart < NetworkPumpBudget))
		{
			int HasPacket;
			if(PerfEnabled)
			{
				CPerfTimer RecvTimer;
				HasPacket = m_aNetClient[Conn].Recv(&Packet, &ResponseToken, IsSixup());
				aNetworkRecvMs[Conn] += RecvTimer.ElapsedMs();
			}
			else
			{
				HasPacket = m_aNetClient[Conn].Recv(&Packet, &ResponseToken, IsSixup());
			}
			if(!HasPacket)
				break;

			++NetworkChunksProcessed;
			++aNetworkChunks[Conn];
			MaxNetworkPacketBytes = maximum(MaxNetworkPacketBytes, Packet.m_DataSize);
			if(Packet.m_ClientId == -1)
			{
				if(ResponseToken != NET_SECURITY_TOKEN_UNKNOWN && !PreprocessConnlessPacket7(&Packet))
					continue;

				if(PerfEnabled)
				{
					CPerfTimer ProcessTimer;
					ProcessConnlessPacket(&Packet);
					const double ProcessMs = ProcessTimer.ElapsedMs();
					aNetworkProcessMs[Conn] += ProcessMs;
					if(ProcessMs > MaxNetworkProcessMs)
					{
						MaxNetworkProcessMs = ProcessMs;
						MaxNetworkProcessConn = Conn;
					}
				}
				else
				{
					ProcessConnlessPacket(&Packet);
				}
				continue;
			}
			if(Conn == CONN_MAIN || Conn == CONN_DUMMY)
			{
				if(PerfEnabled)
				{
					CPerfTimer ProcessTimer;
					ProcessServerPacket(&Packet, Conn, g_Config.m_ClDummy ^ Conn);
					const double ProcessMs = ProcessTimer.ElapsedMs();
					aNetworkProcessMs[Conn] += ProcessMs;
					if(ProcessMs > MaxNetworkProcessMs)
					{
						MaxNetworkProcessMs = ProcessMs;
						MaxNetworkProcessConn = Conn;
					}
				}
				else
				{
					ProcessServerPacket(&Packet, Conn, g_Config.m_ClDummy ^ Conn);
				}
			}
		}
	}

	if(PerfEnabled)
	{
		const double NetworkPumpMs = (time_get_nanoseconds() - NetworkPumpStart).count() / 1000000.0;
		if(NetworkPumpMs >= maximum(QmPerfThresholdMs(), 8.0))
		{
			char aPayload[768];
			str_format(aPayload, sizeof(aPayload),
				"event=network_pump_detail state=%d chunks=%d conn0_chunks=%d conn0_recv_ms=%.3f conn0_process_ms=%.3f conn1_chunks=%d conn1_recv_ms=%.3f conn1_process_ms=%.3f max_process_ms=%.3f max_process_conn=%d max_packet_bytes=%d",
				State(), NetworkChunksProcessed,
				aNetworkChunks[0], aNetworkRecvMs[0], aNetworkProcessMs[0],
				aNetworkChunks[1], aNetworkRecvMs[1], aNetworkProcessMs[1],
				MaxNetworkProcessMs, MaxNetworkProcessConn, MaxNetworkPacketBytes);
			QmPerfLogPayloadForce("perf/main_thread", aPayload, this);
		}
	}
}

void CClient::OnDemoPlayerSnapshot(void *pData, int Size)
{
	// update ticks, they could have changed
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	m_aCurGameTick[0] = pInfo->m_Info.m_CurrentTick;
	m_aPrevGameTick[0] = pInfo->m_PreviousTick;

	// create a verified and unpacked snapshot
	CSnapshotBuffer AltSnapBuffer;
	int AltSnapSize;

	if(IsSixup())
	{
		AltSnapSize = GameClient()->TranslateSnap(&AltSnapBuffer, (CSnapshot *)pData, CONN_MAIN, false);
		if(AltSnapSize < 0)
		{
			dbg_msg("sixup", "failed to translate snapshot. error=%d", AltSnapSize);
			return;
		}
	}
	else
	{
		AltSnapSize = UnpackAndValidateSnapshot((CSnapshot *)pData, &AltSnapBuffer);
		if(AltSnapSize < 0)
		{
			dbg_msg("client", "unpack snapshot and validate failed. error=%d", AltSnapSize);
			return;
		}
	}

	// handle snapshots after validation
	std::swap(m_aapSnapshots[0][SNAP_PREV], m_aapSnapshots[0][SNAP_CURRENT]);
	mem_copy(m_aapSnapshots[0][SNAP_CURRENT]->m_pSnap, pData, Size);
	mem_copy(m_aapSnapshots[0][SNAP_CURRENT]->m_pAltSnap, &AltSnapBuffer, AltSnapSize);

	GameClient()->OnNewSnapshot(false);
}

void CClient::OnDemoPlayerMessage(void *pData, int Size)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageId(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if(GameClient()->OnDemoPlaybackMessage(Msg, &Unpacker))
		return;

	if(!Sys)
		GameClient()->OnMessage(Msg, &Unpacker, CONN_MAIN, false);
}

void CClient::UpdateDemoIntraTimers()
{
	// update timers
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	m_aCurGameTick[0] = pInfo->m_Info.m_CurrentTick;
	m_aPrevGameTick[0] = pInfo->m_PreviousTick;
	m_aGameIntraTick[0] = pInfo->m_IntraTick;
	m_aGameTickTime[0] = pInfo->m_TickTime;
	m_aGameIntraTickSincePrev[0] = pInfo->m_IntraTickSincePrev;
}

void CClient::Update()
{
	const bool GraphicsTrace = QmGraphicsTraceEnabled(2);
	const bool PerfRuntime = QmPerfEnabled();
	// GraphicsTrace 走原有 perf/graphics 通道；常规性能/卡顿诊断下把 client_update 的子阶段
	// 记录到 perf/main_thread，避免定位 [client_update] 长帧时必须开启 graphics trace。
	const bool UpdateStagePerf = GraphicsTrace || PerfRuntime;
	const bool MainThreadStagePerf = PerfRuntime && !GraphicsTrace;
	const auto PumpStart = GraphicsTrace ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	const int64_t PumpGapNs = GraphicsTrace && m_QmGraphicsLastPumpNetworkNs != 0 ? PumpStart.count() - m_QmGraphicsLastPumpNetworkNs : 0;
	if(UpdateStagePerf)
	{
		CPerfTimer PumpTimer;
		PumpNetwork();
		const double PumpMs = PumpTimer.ElapsedMs();
		if(GraphicsTrace)
		{
			const double GapMs = PumpGapNs > 0 ? (double)PumpGapNs / 1000000.0 : 0.0;
			if(PumpMs >= 8.0 || GapMs >= 100.0)
			{
				char aPayload[256];
				str_format(aPayload, sizeof(aPayload), "event=network_pump pump_ms=%.3f gap_ms=%.3f state=%d", PumpMs, GapMs, State());
				QmPerfLogPayloadForce("perf/graphics/network", aPayload, this);
			}
			m_QmGraphicsLastPumpNetworkNs = time_get_nanoseconds().count();
		}
		else if(MainThreadStagePerf)
		{
			char aExtra[96];
			str_format(aExtra, sizeof(aExtra), "state=%d", State());
			QmPerfLogStage("perf/main_thread", "pump_network", PumpMs, false, this, nullptr, nullptr, aExtra);
		}
	}
	else
		PumpNetwork();

	// 官方 178da1ead：在采集/发送输入之前先更新 editor/gameclient，
	// 低刷新率下输入能早一个循环发出。
	if(m_EditorActive)
	{
		if(UpdateStagePerf)
		{
			CPerfTimer StageTimer;
			m_pEditor->OnUpdate();
			if(GraphicsTrace)
				QmPerfLogStageForce("perf/graphics/update", "editor_onupdate", StageTimer.ElapsedMs(), this);
			else
				QmPerfLogStage("perf/main_thread", "editor_onupdate", StageTimer.ElapsedMs(), false, this);
		}
		else
			m_pEditor->OnUpdate();
	}
	else
	{
		if(UpdateStagePerf)
		{
			CPerfTimer StageTimer;
			GameClient()->OnUpdate();
			if(GraphicsTrace)
				QmPerfLogStageForce("perf/graphics/update", "gameclient_onupdate", StageTimer.ElapsedMs(), this);
			else
				QmPerfLogStage("perf/main_thread", "gameclient_onupdate", StageTimer.ElapsedMs(), false, this);
		}
		else
			GameClient()->OnUpdate();
	}
	// 上游快照/预测子阶段计时：声明位置保持在上游流程中的同一语义点，
	// 共享上下文尾部的 update_snapshot_predict 记录会消费它。
	const bool PerfEnabled = PerfRuntime;
	std::optional<CPerfTimer> SnapshotPredictTimer;
	if(PerfEnabled)
		SnapshotPredictTimer.emplace();

	if(State() == IClient::STATE_ONLINE)
	{
		UpdatePredictionMargin();
		m_PredictedTime.UpdateMargin(PredictionMargin() * time_freq() / 1000);
	}

	if(State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(m_DemoPlayer.IsPlaying())
		{
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				IVideo::Current()->NextVideoFrame();
				IVideo::Current()->NextAudioFrameTimeline([this](short *pFinalOut, unsigned Frames) {
					Sound()->Mix(pFinalOut, Frames);
				});
			}
#endif

			m_DemoPlayer.Update();

			// update timers
			const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
			m_aCurGameTick[0] = pInfo->m_Info.m_CurrentTick;
			m_aPrevGameTick[0] = pInfo->m_PreviousTick;
			m_aGameIntraTick[0] = pInfo->m_IntraTick;
			m_aGameTickTime[0] = pInfo->m_TickTime;
		}
		else
		{
			// Disconnect when demo playback stopped, either due to playback error
			// or because the end of the demo was reached when rendering it.
			DisconnectWithReason(m_DemoPlayer.ErrorMessage());
			if(m_DemoPlayer.ErrorMessage()[0] != '\0')
			{
				SWarning Warning(Localize("Error playing demo"), m_DemoPlayer.ErrorMessage());
				Warning.m_AutoHide = false;
				AddWarning(Warning);
			}
		}
	}
	else if(State() == IClient::STATE_ONLINE)
	{
		if(m_LastDummy != (bool)g_Config.m_ClDummy)
		{
			// Invalidate references to !m_ClDummy snapshots
			GameClient()->InvalidateSnapshot();
			GameClient()->OnDummySwap();
		}

		if(m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT])
		{
			// switch dummy snapshot
			int64_t Now = m_aGameTime[!g_Config.m_ClDummy].Get(time_get());
			while(true)
			{
				if(!m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext)
					break;
				int64_t TickStart = m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick * time_freq() / GameTickSpeed();
				if(TickStart >= Now)
					break;

				m_aapSnapshots[!g_Config.m_ClDummy][SNAP_PREV] = m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT];
				m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT] = m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;

				// set ticks
				m_aCurGameTick[!g_Config.m_ClDummy] = m_aapSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
				m_aPrevGameTick[!g_Config.m_ClDummy] = m_aapSnapshots[!g_Config.m_ClDummy][SNAP_PREV]->m_Tick;
			}
		}

		if(m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT])
		{
			// switch snapshot
			bool Repredict = false;
			int64_t Now = m_aGameTime[g_Config.m_ClDummy].Get(time_get());
			int64_t PredNow = m_PredictedTime.Get(time_get());

			if(m_LastDummy != (bool)g_Config.m_ClDummy && m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV])
			{
				// Load snapshot for m_ClDummy
				GameClient()->OnNewSnapshot(true);
				Repredict = true;
			}

			while(true)
			{
				if(!m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext)
					break;
				int64_t TickStart = m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick * time_freq() / GameTickSpeed();
				if(TickStart >= Now)
					break;

				m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV] = m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
				m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;

				// set ticks
				m_aCurGameTick[g_Config.m_ClDummy] = m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
				m_aPrevGameTick[g_Config.m_ClDummy] = m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick;

				GameClient()->OnNewSnapshot(false);
				Repredict = true;
			}

			if(m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV])
			{
				int64_t CurTickStart = m_aapSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick * time_freq() / GameTickSpeed();
				int64_t PrevTickStart = m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick * time_freq() / GameTickSpeed();
				int PrevPredTick = (int)(PredNow * GameTickSpeed() / time_freq());
				int NewPredTick = PrevPredTick + 1;

				m_aGameIntraTick[g_Config.m_ClDummy] = (Now - PrevTickStart) / (float)(CurTickStart - PrevTickStart);
				m_aGameTickTime[g_Config.m_ClDummy] = (Now - PrevTickStart) / (float)time_freq();
				m_aGameIntraTickSincePrev[g_Config.m_ClDummy] = (Now - PrevTickStart) / (float)(time_freq() / GameTickSpeed());

				int64_t CurPredTickStart = NewPredTick * time_freq() / GameTickSpeed();
				int64_t PrevPredTickStart = PrevPredTick * time_freq() / GameTickSpeed();
				m_aPredIntraTick[g_Config.m_ClDummy] = (PredNow - PrevPredTickStart) / (float)(CurPredTickStart - PrevPredTickStart);

				if(absolute(NewPredTick - m_aapSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick) > MaxLatencyTicks())
				{
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", "prediction time reset!");
					m_PredictedTime.Init(CurTickStart + 2 * time_freq() / GameTickSpeed());
				}

				if(NewPredTick > m_aPredTick[g_Config.m_ClDummy])
				{
					m_aPredTick[g_Config.m_ClDummy] = NewPredTick;
					Repredict = true;

					// send input
					SendInput();
				}

				if(GameClient()->IsFastInputActive() && GameClient()->CheckNewInput())
				{
					Repredict = true;
				}
			}

			// only do sane predictions
			if(Repredict)
			{
				if(m_aPredTick[g_Config.m_ClDummy] > m_aCurGameTick[g_Config.m_ClDummy] && m_aPredTick[g_Config.m_ClDummy] < m_aCurGameTick[g_Config.m_ClDummy] + MaxLatencyTicks())
					GameClient()->OnPredict();
			}

			// fetch server info if we don't have it
			if(m_CurrentServerInfoRequestTime >= 0 &&
				time_get() > m_CurrentServerInfoRequestTime)
			{
				const NETADDR *pServerAddr = ServerAddress();
				if(pServerAddr)
					m_ServerBrowser.RequestCurrentServer(*pServerAddr);
				m_CurrentServerInfoRequestTime = time_get() + time_freq() * 2;
			}

			// periodically ping server
			if(m_CurrentServerNextPingTime >= 0 &&
				time_get() > m_CurrentServerNextPingTime)
			{
				int64_t NowPing = time_get();
				int64_t Freq = time_freq();

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "pinging current server%s", !m_ServerCapabilities.m_PingEx ? ", using fallback via server info" : "");
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);

				m_CurrentServerPingUuid = RandomUuid();
				if(!m_ServerCapabilities.m_PingEx)
				{
					const NETADDR *pServerAddr = ServerAddress();
					if(pServerAddr)
						m_ServerBrowser.RequestCurrentServerWithRandomToken(*pServerAddr, &m_CurrentServerPingBasicToken, &m_CurrentServerPingToken);
				}
				else
				{
					CMsgPacker Msg(NETMSG_PINGEX, true);
					Msg.AddRaw(&m_CurrentServerPingUuid, sizeof(m_CurrentServerPingUuid));
					SendMsg(CONN_MAIN, &Msg, MSGFLAG_FLUSH);
				}
				m_CurrentServerCurrentPingTime = NowPing;
				m_CurrentServerNextPingTime = NowPing + 600 * Freq; // ping every 10 minutes
			}

			UpdateGamePing();
		}

		if(m_DummyDeactivateOnReconnect && g_Config.m_ClDummy == 1)
		{
			m_DummyDeactivateOnReconnect = false;
			g_Config.m_ClDummy = 0;
		}
		else if(!m_DummyConnected && !m_DummyConnecting && m_DummyDeactivateOnReconnect)
		{
			m_DummyDeactivateOnReconnect = false;
		}

		m_LastDummy = (bool)g_Config.m_ClDummy;
	}

	if(PerfEnabled)
		QmPerfLogStage("perf/main_thread", "update_snapshot_predict", SnapshotPredictTimer->ElapsedMs(), false, this);

	// STRESS TEST: join the server again
	if(g_Config.m_DbgStress)
	{
		static int64_t s_ActionTaken = 0;
		int64_t Now = time_get();
		if(State() == IClient::STATE_OFFLINE)
		{
			if(Now > s_ActionTaken + time_freq() * 2)
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "reconnecting!");
				Connect(g_Config.m_DbgStressServer);
				s_ActionTaken = Now;
			}
		}
		else
		{
			if(Now > s_ActionTaken + time_freq() * (10 + g_Config.m_DbgStress))
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "disconnecting!");
				Disconnect();
				s_ActionTaken = Now;
			}
		}
	}

	if(m_pMapdownloadTask)
	{
		if(m_pMapdownloadTask->State() == EHttpState::DONE)
		{
			FinishMapDownload();
		}
		else if(m_pMapdownloadTask->State() == EHttpState::ERROR || m_pMapdownloadTask->State() == EHttpState::ABORTED)
		{
			dbg_msg("webdl", "http failed, falling back to gameserver");
			ResetMapDownload(false);
			SendMapRequest();
		}
	}

	if(m_pDDNetInfoTask)
	{
		if(m_pDDNetInfoTask->State() == EHttpState::DONE)
		{
			if(m_ServerBrowser.DDNetInfoSha256() == m_pDDNetInfoTask->ResultSha256())
			{
				log_debug("client/info", "DDNet info already up-to-date");
				m_InfoState = EInfoState::SUCCESS;
			}
			else
			{
				log_debug("client/info", "Loading new DDNet info");
				LoadDDNetInfo();
			}

			ResetDDNetInfoTask();
		}
		else if(m_pDDNetInfoTask->State() == EHttpState::ERROR || m_pDDNetInfoTask->State() == EHttpState::ABORTED)
		{
			ResetDDNetInfoTask();
			m_InfoState = EInfoState::ERROR;
		}
	}

	if(State() == IClient::STATE_ONLINE)
	{
		if(!m_EditJobs.empty())
		{
			std::shared_ptr<CDemoEdit> pJob = m_EditJobs.front();
			if(pJob->State() == IJob::STATE_DONE)
			{
				char aBuf[IO_MAX_PATH_LENGTH + 64];
				if(pJob->Success())
				{
					str_format(aBuf, sizeof(aBuf), "Successfully saved the replay to '%s'!", pJob->Destination());
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", aBuf);

					GameClient()->Echo(Localize("Successfully saved the replay!"));
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "Failed saving the replay to '%s'...", pJob->Destination());
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", aBuf);

					GameClient()->Echo(Localize("Failed saving the replay!"));
				}
				m_EditJobs.pop_front();
			}
		}
	}

	// update the server browser
	if(UpdateStagePerf)
	{
		CPerfTimer StageTimer;
		m_ServerBrowser.Update();
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "servers=%d sorted=%d", m_ServerBrowser.NumServers(), m_ServerBrowser.NumSortedServers());
		if(GraphicsTrace)
			QmPerfLogStageForce("perf/graphics/update", "serverbrowser_update", StageTimer.ElapsedMs(), this, nullptr, nullptr, aExtra);
		else
			QmPerfLogStage("perf/main_thread", "serverbrowser_update", StageTimer.ElapsedMs(), false, this, nullptr, nullptr, aExtra);
	}
	else
		m_ServerBrowser.Update();

	if(MainThreadStagePerf)
	{
		CPerfTimer StageTimer;
		Discord()->Update(g_Config.m_TcDiscordRPC);
		Steam()->Update();
		char aExtra[64];
		str_format(aExtra, sizeof(aExtra), "rpc=%d", g_Config.m_TcDiscordRPC);
		QmPerfLogStage("perf/main_thread", "discord_steam_update", StageTimer.ElapsedMs(), false, this, nullptr, nullptr, aExtra);
	}
	else
	{
		Discord()->Update(g_Config.m_TcDiscordRPC);
		Steam()->Update();
	}
	if(Steam()->GetConnectAddress())
	{
		HandleConnectAddress(Steam()->GetConnectAddress());
		Steam()->ClearConnectAddress();
	}

	if(m_ReconnectTime > 0 && time_get() > m_ReconnectTime)
	{
		if(State() != STATE_ONLINE)
			Connect(m_aConnectAddressStr);
		m_ReconnectTime = 0;
	}
}

void CClient::RegisterInterfaces()
{
	Kernel()->RegisterInterface(static_cast<IDemoPlayer *>(&m_DemoPlayer), false);
	Kernel()->RegisterInterface(static_cast<IGhostRecorder *>(&m_GhostRecorder), false);
	Kernel()->RegisterInterface(static_cast<IGhostLoader *>(&m_GhostLoader), false);
	Kernel()->RegisterInterface(static_cast<IServerBrowser *>(&m_ServerBrowser), false);
#if defined(CONF_AUTOUPDATE)
	Kernel()->RegisterInterface(static_cast<IUpdater *>(&m_Updater), false);
#endif
	Kernel()->RegisterInterface(static_cast<IFriends *>(&m_Friends), false);
	Kernel()->ReregisterInterface(static_cast<IFriends *>(&m_Foes));
}

void CClient::InitInterfaces()
{
	// fetch interfaces
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pEditor = Kernel()->RequestInterface<IEditor>();
	m_pFavorites = Kernel()->RequestInterface<IFavorites>();
	m_pSound = Kernel()->RequestInterface<IEngineSound>();
	m_pGameClient = Kernel()->RequestInterface<IGameClient>();
	m_pInput = Kernel()->RequestInterface<IEngineInput>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pConfig = m_pConfigManager->Values();
#if defined(CONF_AUTOUPDATE)
	m_pUpdater = Kernel()->RequestInterface<IUpdater>();
#endif
	m_pDiscord = Kernel()->RequestInterface<IDiscord>();
	m_pSteam = Kernel()->RequestInterface<ISteam>();
	m_pNotifications = Kernel()->RequestInterface<INotifications>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = Kernel()->RequestInterface<IEngineHttp>();

	m_DemoEditor.Init(&*m_pSnapshotDelta, &*m_pSnapshotDeltaSixup, m_pConsole, m_pStorage);

#if defined(CONF_AUTOUPDATE)
	m_Updater.Init(m_pHttp);
#endif

	m_pConfigManager->RegisterCallback(IFavorites::ConfigSaveCallback, m_pFavorites);

	m_GhostRecorder.Init();
	m_GhostLoader.Init();
}

void CClient::InitConfigCommands()
{
	IGameClient *pGameClient = Kernel()->RequestInterface<IGameClient>();
	m_ServerBrowser.SetBaseInfo(&m_aNetClient[CONN_CONTACT], pGameClient->NetVersion());
	m_Friends.Init();
	m_Foes.Init(true);
}

void CClient::Run()
{
	m_LocalStartTime = m_GlobalStartTime = time_get();
#if defined(CONF_VIDEORECORDER)
	IVideo::SetLocalStartTime(m_LocalStartTime);
#endif
	m_aSnapshotParts[0] = 0;
	m_aSnapshotParts[1] = 0;

	StartHangWatchdog();
	struct CHangWatchdogGuard
	{
		CClient *m_pClient;
		~CHangWatchdogGuard()
		{
			m_pClient->StopHangWatchdog();
		}
	} HangWatchdogGuard{this};

	if(m_GenerateTimeoutSeed)
	{
		GenerateTimeoutSeed();
	}

	unsigned int Seed;
	secure_random_fill(&Seed, sizeof(Seed));
	srand(Seed);

	if(g_Config.m_Debug)
	{
		g_UuidManager.DebugDump();
	}

	char aNetworkError[256];
	if(!InitNetworkClient(aNetworkError, sizeof(aNetworkError)))
	{
		log_error("client", "%s", aNetworkError);
		ShowMessageBox({.m_pTitle = "Network Error", .m_pMessage = aNetworkError});
		return;
	}

	if(!m_pHttp->Init(std::chrono::seconds{1}))
	{
		const char *pErrorMessage = "Failed to initialize the HTTP client.";
		log_error("client", "%s", pErrorMessage);
		ShowMessageBox({.m_pTitle = "HTTP Error", .m_pMessage = pErrorMessage});
		return;
	}

	// init graphics
	m_pGraphics = CreateEngineGraphicsThreaded();
	Kernel()->RegisterInterface(m_pGraphics); // IEngineGraphics
	Kernel()->RegisterInterface(static_cast<IGraphics *>(m_pGraphics), false);
	{
		CMemoryLogger MemoryLogger;
		MemoryLogger.SetParent(log_get_scope_logger());
		bool Success;
		{
			CLogScope LogScope(&MemoryLogger);
			Success = m_pGraphics->Init() == 0;
		}
		if(!Success)
		{
			log_error("client", "Failed to initialize the graphics (see details above)");
			const std::string Message = std::string(
							    "Failed to initialize the graphics. See details below.\n\n"
							    "For detailed troubleshooting instructions please read our Wiki:\n"
							    "https://wiki.ddnet.org/wiki/GFX_Troubleshooting\n\n") +
						    MemoryLogger.ConcatenatedLines();
			const std::vector<IGraphics::CMessageBoxButton> vButtons = {
				{.m_pLabel = "Show Wiki"},
				{.m_pLabel = "OK", .m_Confirm = true, .m_Cancel = true},
			};
			const std::optional<int> MessageResult = ShowMessageBox({.m_pTitle = "Graphics Initialization Error", .m_pMessage = Message.c_str(), .m_vButtons = vButtons});
			if(MessageResult && *MessageResult == 0)
			{
				ViewLink("https://wiki.ddnet.org/wiki/GFX_Troubleshooting");
			}
			return;
		}
	}

	// make sure the first frame just clears everything to prevent undesired colors when waiting for io
	// QmClient: 首帧清屏必须保持纯黑。曾改用 cl_background_color，但该值默认 128，
	// 经 ColorHSLA 解成 l=128/255≈0.502 → #808080 中灰；窗口打开后到首个加载帧之间
	// （GameClient 初始化还没跑完）呈现的就是这个清屏色，表现为启动整屏灰。
	// 菜单主题是 .map 时会铺满全屏，只有这种"空帧"才露出清屏色，故与主题选择无关。
	Graphics()->Clear(0, 0, 0);
	Graphics()->Swap();

	// init localization first, making sure all errors during init can be localized
	GameClient()->InitializeLanguage();

	// init sound, allowed to fail
	const bool SoundInitFailed = Sound()->Init() != 0;

#if defined(CONF_VIDEORECORDER)
	// init video recorder aka ffmpeg
	CVideo::Init();
#endif

	// init text render
	m_pTextRender = Kernel()->RequestInterface<IEngineTextRender>();
	m_pTextRender->Init();

	// init the input
	Input()->Init();

	// init the editor
	m_pEditor->Init();

	m_ServerBrowser.OnInit();
	// loads the existing ddnet info file if it exists
	LoadDDNetInfo();

	LoadDebugFont();

	if(Steam()->GetPlayerName())
	{
		str_copy(g_Config.m_SteamName, Steam()->GetPlayerName());
	}

	Graphics()->AddWindowResizeListener([this] { OnWindowResize(); });

	GameClient()->OnInit();

	m_Fifo.Init(m_pConsole, g_Config.m_ClInputFifo, CFGFLAG_CLIENT);

	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "version " GAME_RELEASE_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING, ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f));
	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "git revision hash: %s", GIT_SHORTREV_HASH);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f));
	}

	//
	m_FpsGraph.Init(0.0f, 120.0f);

	// never start with the editor
	g_Config.m_ClEditor = 0;

	// process pending commands
	m_pConsole->StoreCommands(false);
	FinishQmConfigMigration();

	InitChecksum();
	m_pConsole->InitChecksum(ChecksumData());

	// request the new ddnet info from server if already past the welcome dialog
	if(g_Config.m_ClShowWelcome)
		g_Config.m_ClShowWelcome = 0;
	else
		RequestDDNetInfo();

	if(SoundInitFailed)
	{
		SWarning Warning(Localize("Sound error"), Localize("The audio device couldn't be initialised."));
		Warning.m_AutoHide = false;
		AddWarning(Warning);
	}

	// QmClient: 测试专用注入点（--qm-test-main-thread-assert）。放在主循环之前，
	// 模拟启动阶段的错误弹窗（网络/图形初始化失败等）阻塞主线程的情形：
	// 此时看门狗时钟仍在推进，可验证弹窗期间不会误报“客户端卡死”。
	if(gs_QmTestMainThreadAssert)
		dbg_assert_failed("qm test main thread assertion (--qm-test-main-thread-assert)");

	bool LastD = false;
	bool LastE = false;

	auto LastTime = time_get_nanoseconds();
	int64_t LastRenderTime = time_get();
	int64_t NextUpdateTime = time_get();
	int64_t NextRenderTime = time_get();
	int LastIdleRenderThrottleRate = -1;
	int LastRequestedRenderThrottleRate = -1;

	// QmClient: 兜底显示窗口。窗口是隐藏创建的，正常路径由菜单加载界面 present 首帧后
	// 调用 ShowWindow()；但若本次启动压根没有走过加载界面，窗口会一直隐藏，而
	// WindowOpen() 依赖 SDL_WINDOW_SHOWN，一旦 gfx_backgroundrender 为 0，
	// IsRenderActive 就恒为 false，主循环永远不渲染、也就永远不会显示窗口 —— 死锁。
	// 进主循环前无条件显示一次即可消除该路径（重复调用无副作用）。
	Graphics()->ShowWindow();

	while(true)
	{
		const bool PerfEnabled = QmPerfEnabled();
		UpdateQmPerfFileLogger(); // 游戏内开关立即开/关性能日志文件（状态无变化时仅几次内存读）
		// QmClient：日志开启期间按秒把生效配置的增量变化写进性能日志（见 perf_diagnostics.h）。
		if(m_QmPerfFileLoggerActive && time_get() - m_QmPerfLastConfigCheck >= time_freq())
		{
			m_QmPerfConfigSnapshot.Update(this);
			m_QmPerfLastConfigCheck = time_get();
		}
		std::optional<CPerfTimer> LoopTimer;
		if(PerfEnabled)
			LoopTimer.emplace();
		++m_PerfFrame;
		set_new_tick();
		UpdateHangHeartbeat();

		// QmClient: 测试专用注入点（--qm-test-main-thread-stall）。主循环内阻塞一次，
		// 验证看门狗使用单调时钟后能在主线程阻塞期间真实写出卡死报告。
		if(gs_QmTestMainThreadStall)
		{
			gs_QmTestMainThreadStall = false;
#if defined(CONF_FAMILY_WINDOWS)
			QmTestStallPumpWindowMessages(12s); // 必须超过 gs_HangTimeoutSeconds(10s)
#else
			std::this_thread::sleep_for(12s);
#endif
		}

		// 图形后端已经记录致命错误时，立刻在提交（会断言退出）之前收口。
		// 这样设备丢失等故障走的是「写诊断 + 干净重启」，而不是弹模态框把
		// 主线程和心跳一起卡住（那会写出误导性的 hang 报告）。
		// TakeFatalError 会同时清掉标记，让随后的收尾流程不再重复触发断言。
		if(Graphics()->TakeFatalError())
		{
			if(HandleQmGraphicsFatalError())
				break;
		}

		// handle pending connects
		if(m_aCmdConnect[0])
		{
			str_copy(g_Config.m_UiServerAddress, m_aCmdConnect);
			Connect(m_aCmdConnect);
			m_aCmdConnect[0] = 0;
		}

		// handle pending demo play
		if(m_aCmdPlayDemo[0])
		{
			const char *pError = DemoPlayer_Play(m_aCmdPlayDemo, IStorage::TYPE_ALL_OR_ABSOLUTE);
			if(pError)
				log_error("demo_player", "playing passed demo file '%s' failed: %s", m_aCmdPlayDemo, pError);
			m_aCmdPlayDemo[0] = 0;
		}

		// handle pending map edits
		if(m_aCmdEditMap[0])
		{
			int Result = m_pEditor->HandleMapDrop(m_aCmdEditMap, IStorage::TYPE_ALL_OR_ABSOLUTE);
			if(Result)
				g_Config.m_ClEditor = true;
			else
				log_error("editor", "editing passed map file '%s' failed", m_aCmdEditMap);
			m_aCmdEditMap[0] = 0;
		}

		// update input
		{
			bool QuitRequested;
			if(PerfEnabled)
			{
				CPerfTimer StageTimer;
				QuitRequested = Input()->Update();
				char aExtra[96];
				str_format(aExtra, sizeof(aExtra), "state=%d quit=%d", State(), QuitRequested ? 1 : 0);
				QmPerfLogStage("perf/main_thread", "input_update", StageTimer.ElapsedMs(), QuitRequested, this, nullptr, nullptr, aExtra);
			}
			else
				QuitRequested = Input()->Update();
			if(QuitRequested)
			{
				if(!GameClient()->PrepareForShutdown(true))
				{
					SetState(IClient::STATE_QUITTING); // SDL_QUIT
					break;
				}
			}
		}

		char aFile[IO_MAX_PATH_LENGTH];
		if(Input()->GetDropFile(aFile, sizeof(aFile)))
		{
			if(str_startswith(aFile, CONNECTLINK_NO_SLASH))
				HandleConnectLink(aFile);
			else if(str_endswith(aFile, ".demo"))
				HandleDemoPath(aFile);
			else if(str_endswith(aFile, ".map"))
				HandleMapPath(aFile);
		}

#if defined(CONF_AUTOUPDATE)
		if(PerfEnabled)
		{
			CPerfTimer StageTimer;
			Updater()->Update();
			QmPerfLogStage("perf/main_thread", "updater_update", StageTimer.ElapsedMs(), false, this);
		}
		else
			Updater()->Update();
#endif

		// update sound
		if(PerfEnabled)
		{
			CPerfTimer StageTimer;
			Sound()->Update();
			QmPerfLogStage("perf/main_thread", "sound_update", StageTimer.ElapsedMs(), false, this);
		}
		else
			Sound()->Update();

		if(CtrlShiftKey(KEY_D, LastD))
			g_Config.m_Debug ^= 1;

		if(CtrlShiftKey(KEY_E, LastE))
		{
			if(g_Config.m_ClEditor)
				m_pEditor->OnClose();
			g_Config.m_ClEditor = g_Config.m_ClEditor ^ 1;
		}

		int IdleRenderThrottleRate = 0;
		bool Inactive = false;
		int64_t WakeTime = std::numeric_limits<int64_t>::max();

		// render
		{
			if(g_Config.m_ClEditor)
			{
				if(!m_EditorActive)
				{
					Input()->MouseModeRelative();
					GameClient()->OnActivateEditor();
					m_pEditor->OnActivate();
					m_EditorActive = true;
				}
			}
			else if(m_EditorActive)
			{
				m_EditorActive = false;
			}

			if(PerfEnabled)
			{
				CPerfTimer StageTimer;
				Update();
				char aExtra[96];
				str_format(aExtra, sizeof(aExtra), "state=%d editor=%d", State(), m_EditorActive ? 1 : 0);
				QmPerfLogStage("perf/main_thread", "client_update", StageTimer.ElapsedMs(), false, this, nullptr, nullptr, aExtra);
			}
			else
				Update();
			int64_t Now = time_get();

			bool IsRenderActive = (g_Config.m_GfxBackgroundRender || m_pGraphics->WindowOpen());

			bool AsyncRenderOld = g_Config.m_GfxAsyncRenderOld;
			Inactive = g_Config.m_ClRefreshRateInactive && !m_pGraphics->WindowActive();
			const int RefreshRate = Inactive ? g_Config.m_ClRefreshRateInactive : g_Config.m_ClRefreshRate;
			bool UpdateDue = true;
			if(RefreshRate)
			{
				UpdateDue = Now >= NextUpdateTime;
				if(UpdateDue)
					NextUpdateTime = std::max(NextUpdateTime + time_freq() / RefreshRate, Now);
			}

			int GfxRefreshRate = g_Config.m_GfxRefreshRate;
			int RequestedRenderThrottleRate = 0;
			if(g_Config.m_GfxVsync == 0 && GfxRefreshRate == 0)
			{
				RequestedRenderThrottleRate = GameClient()->RenderThrottleRefreshRate();
				if(RequestedRenderThrottleRate > 0)
				{
					GfxRefreshRate = std::clamp(RequestedRenderThrottleRate, 10, 10000);
					IdleRenderThrottleRate = GfxRefreshRate;
				}
			}
			if(RefreshRate && GfxRefreshRate >= RefreshRate)
				GfxRefreshRate = 0;

#if defined(CONF_VIDEORECORDER)
			// keep rendering synced
			if(IVideo::Current())
			{
				AsyncRenderOld = false;
				GfxRefreshRate = 0;
				RequestedRenderThrottleRate = 0;
				IdleRenderThrottleRate = 0;
			}
#endif
			if(QmPerfEnabled() && (IdleRenderThrottleRate != LastIdleRenderThrottleRate || RequestedRenderThrottleRate != LastRequestedRenderThrottleRate))
			{
				char aPayload[192];
				str_format(aPayload, sizeof(aPayload), "event=idle_render_throttle rate=%d requested=%d configured=%d vsync=%d state=%d render_active=%d async_old=%d",
					IdleRenderThrottleRate, RequestedRenderThrottleRate, g_Config.m_GfxRefreshRate, g_Config.m_GfxVsync, State(), IsRenderActive ? 1 : 0, AsyncRenderOld ? 1 : 0);
				QmPerfLogPayload("perf/main_thread", aPayload, this);
				LastIdleRenderThrottleRate = IdleRenderThrottleRate;
				LastRequestedRenderThrottleRate = RequestedRenderThrottleRate;
			}
			const int64_t RenderFrameTicks = GfxRefreshRate > 0 ? time_freq() / (int64_t)GfxRefreshRate : 0;
			const bool RenderDue = GfxRefreshRate ? Now >= NextRenderTime : UpdateDue;

			if(IsRenderActive &&
				(!AsyncRenderOld || m_pGraphics->IsIdle()) &&
				RenderDue)
			{
				// update frametime
				m_RenderFrameTime = (Now - m_LastRenderTime) / (float)time_freq();
				m_FpsGraph.Add(1.0f / m_RenderFrameTime);

				if(m_BenchmarkFile)
				{
					char aBuf[64];
					str_format(aBuf, sizeof(aBuf), "Frametime %d us\n", (int)(m_RenderFrameTime * 1000000));
					io_write(m_BenchmarkFile, aBuf, str_length(aBuf));
					if(time_get() > m_BenchmarkStopTime)
					{
						io_close(m_BenchmarkFile);
						m_BenchmarkFile = nullptr;
						Quit();
					}
				}

				m_FrameTimeAverage = m_FrameTimeAverage * 0.9f + m_RenderFrameTime * 0.1f;

				// keep the overflow time - it's used to make sure the gfx refreshrate is reached
				int64_t AdditionalTime = GfxRefreshRate ? ((Now - LastRenderTime) - RenderFrameTicks) : 0;
				// if the value is over the frametime of a 60 fps frame, reset the additional time (drop the frames, that are lost already)
				if(AdditionalTime > (time_freq() / 60))
					AdditionalTime = (time_freq() / 60);
				LastRenderTime = Now - AdditionalTime;
				if(GfxRefreshRate)
					NextRenderTime = std::max(NextRenderTime + time_freq() / GfxRefreshRate, Now);
				m_LastRenderTime = Now;

				if(PerfEnabled)
				{
					CPerfTimer StageTimer;
					Render();
					char aExtra[96];
					str_format(aExtra, sizeof(aExtra), "state=%d render_rate=%d throttle=%d", State(), GfxRefreshRate, RequestedRenderThrottleRate);
					QmPerfLogStage("perf/main_thread", "frame_render", StageTimer.ElapsedMs(), false, this, nullptr, nullptr, aExtra);
				}
				else
					Render();
				if(PerfEnabled)
				{
					CPerfTimer StageTimer;
					m_pGraphics->Swap();
					char aExtra[96];
					str_format(aExtra, sizeof(aExtra), "state=%d render_rate=%d throttle=%d", State(), GfxRefreshRate, RequestedRenderThrottleRate);
					QmPerfLogStage("perf/main_thread", "graphics_swap", StageTimer.ElapsedMs(), false, this, nullptr, nullptr, aExtra);
				}
				else
					m_pGraphics->Swap();
				// 逐帧阶段日志按「容量或耗时」攒批落盘：帧统计不参与明细限流，
				// 这里把帧号与帧耗时压成一条 frame_batch 事件，避免逐帧刷屏。
				if(PerfEnabled && QmPerfEnabled() && m_QmPerfFileLoggerActive)
				{
					const int64_t FrameEnd = time_get();
					const double FrameMs = m_QmPerfLastFrameEnd != 0 ? (FrameEnd - m_QmPerfLastFrameEnd) * 1000.0 / time_freq() : 0.0;
					m_QmPerfLastFrameEnd = FrameEnd;
					if(m_QmPerfFrameBatch.Record(PerfFrame(), FrameMs))
						QmPerfLogFields("perf/frame", m_QmPerfFrameBatch.TakeFields(), this);
				}
				// 只在连接/加载阶段记录，避免菜单和游戏内每帧刷屏
				if(g_Config.m_QmGraphicsTrace >= 3 &&
					(State() == IClient::STATE_CONNECTING || State() == IClient::STATE_LOADING))
					dbg_msg("gfx/swap", "swap source=mainloop state=%d", State());
			}
			else if(!IsRenderActive)
			{
				// if the client does not render, it should reset its render time to a time where it would render the first frame, when it wakes up again
				LastRenderTime = GfxRefreshRate ? (Now - RenderFrameTicks) : Now;
				if(GfxRefreshRate)
					NextRenderTime = Now;
			}
			if(RefreshRate)
				WakeTime = NextUpdateTime;
			if(IsRenderActive && GfxRefreshRate)
				WakeTime = std::min(WakeTime, NextRenderTime);
			if(IdleRenderThrottleRate > 0 && !RefreshRate && WakeTime == std::numeric_limits<int64_t>::max())
				WakeTime = Now + time_freq() / IdleRenderThrottleRate;
			if(State() == IClient::STATE_ONLINE && m_aPredTick[g_Config.m_ClDummy] > 0 && !Inactive && WakeTime != std::numeric_limits<int64_t>::max())
				WakeTime = std::min(WakeTime, Now + (m_aPredTick[g_Config.m_ClDummy] * time_freq() / GameTickSpeed() - m_PredictedTime.Get(Now)));
		}

		AutoScreenshot_Cleanup();
		AutoStatScreenshot_Cleanup();
		AutoCSV_Cleanup();

		// QmClient: 渲染帧收尾，结算本帧文本统计（容器创建/字形光栅化），
		// 超阈值帧会打 text_frame_stats 日志用于定位文本渲染卡顿。
		TextRender()->QmTextFrameEnd();

		m_Fifo.Update();
		if(PerfEnabled)
			QmPerfLogStage("perf/main_thread", "loop_total", LoopTimer->ElapsedMs(), false, this);

		if(State() == IClient::STATE_QUITTING || State() == IClient::STATE_RESTARTING)
			break;

		// beNice
		if(WakeTime != std::numeric_limits<int64_t>::max())
		{
			const std::chrono::nanoseconds Deadline(WakeTime);
			std::chrono::nanoseconds WaitTime = Deadline - time_get_nanoseconds();
			if(Inactive)
			{
				std::this_thread::sleep_for(WaitTime);
			}
			else
			{
				while(WaitTime > 0ns && net_socket_read_wait(m_aNetClient[CONN_MAIN].m_Socket, WaitTime > 1000us ? WaitTime / 2 : 0ns) == 0)
					WaitTime = Deadline - time_get_nanoseconds();
			}
		}

		// update local and global time
		m_LocalTime = (time_get() - m_LocalStartTime) / (float)time_freq();
		m_GlobalTime = (time_get() - m_GlobalStartTime) / (float)time_freq();
	}

	GameClient()->RenderShutdownMessage();
	Disconnect();

	if(!m_pConfigManager->Save())
	{
		/*
		char aError[128] = "";
		for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
		{
			if(DIDNTFAIL)
				continue
			if(aError[0] != '\0')
				str_append(aError, ", ");
			str_append(s_aConfigDomains[ConfigDomain].m_aConfigPath);
		}
		*/
		// TODO
		m_vQuittingWarnings.emplace_back(Localize("Error saving settings"));
	}

	// QmClient: 配置已保存，启动退出兜底看门狗；若退出清理（如驱动销毁渲染上下文、
	// 图形设备丢失弹出的错误框）挂起，超时后强制结束进程，避免用户手动杀进程。
	StartForcedExitWatchdog();

	// QmClient: 退出清理可能因驱动/GPU 挂起长时间无响应，窗口会停留在最后一次
	// present 的旧帧上（实测为游戏画面），看起来像卡死。这里不经渲染线程直接隐藏
	// 窗口；即便后续步骤挂起并最终由兜底看门狗强杀，用户看到的也是干净的退出。
	Graphics()->HideWindow();
	dbg_msg("perf/client", "event=shutdown_step step=window_hidden");

	m_ServerBrowser.Shutdown();
	dbg_msg("perf/client", "event=shutdown_step step=server_browser");
	m_Fifo.Shutdown();
	dbg_msg("perf/client", "event=shutdown_step step=fifo");
	m_pHttp->Shutdown();
	dbg_msg("perf/client", "event=shutdown_step step=http");
	// 性能日志的异步关闭任务只持有日志对象；退出时在这里同步接管，
	// 不把未完成的文件关闭留给作业线程。
	if(m_pQmPerfFileSwitch != nullptr)
		m_pQmPerfFileSwitch->FinishPending();
	Engine()->ShutdownJobs();
	dbg_msg("perf/client", "event=shutdown_step step=jobs");

	// Stop the hang watchdog AFTER ShutdownJobs() so that hangs occurring
	// during the shutdown sequence (e.g. a stuck non-abortable job) are
	// still detected and reported while we wait.
	StopHangWatchdog();
	dbg_msg("perf/client", "event=shutdown_step step=hang_watchdog");

	GameClient()->RenderShutdownMessage();
	dbg_msg("perf/client", "event=shutdown_step step=render_shutdown_message");
	if(m_QmPerfFileLoggerActive)
		FinishQmPerfSession(true);
	GameClient()->OnShutdown();
	dbg_msg("perf/client", "event=shutdown_step step=game_client");
	delete m_pEditor;
	dbg_msg("perf/client", "event=shutdown_step step=editor");

	// close sockets
	for(unsigned int i = 0; i < std::size(m_aNetClient); i++)
		m_aNetClient[i].Close();
	dbg_msg("perf/client", "event=shutdown_step step=sockets");

	// shutdown text render while graphics are still available
	m_pTextRender->Shutdown();
	dbg_msg("perf/client", "event=shutdown_step step=text_render");

	// 清理已经完成，后续显示的崩溃报告需要保持到用户主动关闭。
	StopForcedExitWatchdog();
	dbg_msg("perf/client", "event=shutdown_step step=done");
}

void CClient::FinishQmConfigMigration()
{
	if(!QmConfigMigrationPending(m_pStorage))
		return;

	if(!m_pConfigManager->Save(true) || !QmFinalizeConfigMigration(m_pStorage))
	{
		AddWarning(SWarning(Localize("Error saving settings")));
		return;
	}
	log_info("config", "Merged managed client configs into qmclient/settings.cfg");
}

bool CClient::InitNetworkClient(char *pError, size_t ErrorSize)
{
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] == '\0')
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
	}
	else if(net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) != 0)
	{
		str_format(pError, ErrorSize, "The configured bindaddr '%s' cannot be resolved.", g_Config.m_Bindaddr);
		return false;
	}
	BindAddr.type = NETTYPE_ALL;
	for(size_t i = 0; i < std::size(m_aNetClient); i++)
	{
		if(!InitNetworkClientImpl(BindAddr, i, pError, ErrorSize))
		{
			return false;
		}
	}
	return true;
}

bool CClient::InitNetworkClientImpl(NETADDR BindAddr, int Conn, char *pError, size_t ErrorSize)
{
	int *pPort;
	const char *pName;
	switch(Conn)
	{
	case CONN_MAIN:
		pPort = &g_Config.m_ClPort;
		pName = "main";
		break;
	case CONN_DUMMY:
		pPort = &g_Config.m_ClDummyPort;
		pName = "dummy";
		break;
	case CONN_CONTACT:
		pPort = &g_Config.m_ClContactPort;
		pName = "contact";
		break;
	default:
		dbg_assert_failed("unreachable");
	}
	if(m_aNetClient[Conn].State() != NETSTATE_OFFLINE)
	{
		str_format(pError, ErrorSize, "Could not open network client %s while already connected.", pName);
		return false;
	}
	if(*pPort < 1024) // Reject users setting ports that we don't want to use
		*pPort = 0;
	BindAddr.port = *pPort;

	unsigned RemainingAttempts = 25;
	while(!m_aNetClient[Conn].Open(BindAddr, g_Config.m_QmNetQos && (Conn == CONN_MAIN || Conn == CONN_DUMMY)))
	{
		--RemainingAttempts;
		if(RemainingAttempts == 0)
		{
			if(g_Config.m_Bindaddr[0])
				str_format(pError, ErrorSize, "Could not open network client %s, try changing or unsetting the bindaddr '%s'.", pName, g_Config.m_Bindaddr);
			else
				str_format(pError, ErrorSize, "Could not open network client %s.", pName);
			return false;
		}
		if(BindAddr.port != 0)
			BindAddr.port = 0;
	}
	return true;
}

bool CClient::CtrlShiftKey(int Key, bool &Last)
{
	if(Input()->ModifierIsPressed() && Input()->ShiftIsPressed() && !Last && Input()->KeyIsPressed(Key))
	{
		Last = true;
		return true;
	}
	else if(Last && !Input()->KeyIsPressed(Key))
	{
		Last = false;
	}

	return false;
}

void CClient::Con_Connect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->HandleConnectLink(pResult->GetString(0));
}

void CClient::Con_Disconnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Disconnect();
}

void CClient::Con_QmTimeoutDisconnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DropCurrentServerConnection();
}

void CClient::Con_QmNetQosStatus(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	NETSTATS Stats = {};
	net_stats(&Stats);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "enabled=%d main=%s dummy=%s send_errors=%llu",
		g_Config.m_QmNetQos,
		pSelf->m_aNetClient[CONN_MAIN].QosStatusName(),
		pSelf->m_aNetClient[CONN_DUMMY].QosStatusName(),
		(unsigned long long)Stats.send_errors);
	pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net/qos", aBuf);
}

void CClient::Con_DummyConnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DummyConnect();
}

void CClient::Con_DummyDisconnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->GameClient()->OnDummyManualDisconnect();
	pSelf->DummyDisconnect(nullptr);
}

void CClient::Con_DummyResetInput(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->GameClient()->DummyResetInput();
}

void CClient::Con_Quit(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Quit();
}

void CClient::Con_Restart(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Restart();
}

void CClient::Con_Minimize(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->Minimize();
}

void CClient::Con_Ping(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->m_aNetClient[CONN_MAIN].State() != NETSTATE_ONLINE)
	{
		pSelf->m_ManualPingProbe.Reset();
		return;
	}
	const int64_t Now = time_get();
	if(!pSelf->m_ServerCapabilities.m_PingEx)
	{
		SGamePingProbe &Probe = pSelf->m_aGamePingProbes[CONN_MAIN];
		if(!pSelf->m_ManualPingProbe.Begin(Now))
			return;
		if(Probe.m_Legacy)
		{
			pSelf->m_ManualPingProbe.m_StartTime = Probe.m_StartTime;
			return;
		}

		CMsgPacker Msg(NETMSG_PING, true);
		pSelf->SendMsg(CONN_MAIN, &Msg, MSGFLAG_FLUSH);
		Probe.BeginLegacy(Now, time_freq());
		return;
	}
	if(!pSelf->m_ManualPingProbe.Begin(Now))
		return;

	CMsgPacker Msg(NETMSG_PING, true);
	pSelf->SendMsg(CONN_MAIN, &Msg, MSGFLAG_FLUSH);
}

void CClient::ConNetReset(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->ResetSocket();
}

void CClient::AutoScreenshot_Start()
{
	if(g_Config.m_ClAutoScreenshot)
	{
		Graphics()->TakeScreenshot("auto/autoscreen");
		m_AutoScreenshotRecycle = true;
	}
}

void CClient::AutoStatScreenshot_Start()
{
	if(g_Config.m_ClAutoStatboardScreenshot)
	{
		Graphics()->TakeScreenshot("auto/stats/autoscreen");
		m_AutoStatScreenshotRecycle = true;
	}
}

void CClient::AutoScreenshot_Cleanup()
{
	if(m_AutoScreenshotRecycle)
	{
		if(g_Config.m_ClAutoScreenshotMax)
		{
			// clean up auto taken screens
			CFileCollection AutoScreens;
			AutoScreens.Init(Storage(), "screenshots/auto", "autoscreen", ".png", g_Config.m_ClAutoScreenshotMax);
		}
		m_AutoScreenshotRecycle = false;
	}
}

void CClient::AutoStatScreenshot_Cleanup()
{
	if(m_AutoStatScreenshotRecycle)
	{
		if(g_Config.m_ClAutoStatboardScreenshotMax)
		{
			// clean up auto taken screens
			CFileCollection AutoScreens;
			AutoScreens.Init(Storage(), "screenshots/auto/stats", "autoscreen", ".png", g_Config.m_ClAutoStatboardScreenshotMax);
		}
		m_AutoStatScreenshotRecycle = false;
	}
}

void CClient::AutoCSV_Start()
{
	if(g_Config.m_ClAutoCSV)
		m_AutoCSVRecycle = true;
}

void CClient::AutoCSV_Cleanup()
{
	if(m_AutoCSVRecycle)
	{
		if(g_Config.m_ClAutoCSVMax)
		{
			// clean up auto csvs
			CFileCollection AutoRecord;
			AutoRecord.Init(Storage(), "record/csv", "autorecord", ".csv", g_Config.m_ClAutoCSVMax);
		}
		m_AutoCSVRecycle = false;
	}
}

void CClient::Con_Screenshot(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->TakeScreenshot(nullptr, [pSelf](CImageInfo &&Image) {
		pSelf->GameClient()->OnScreenshotTaken(std::move(Image));
	});
}

#if defined(CONF_VIDEORECORDER)

void CClient::Con_StartVideo(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = static_cast<CClient *>(pUserData);

	if(pResult->NumArguments())
	{
		pSelf->StartVideo(pResult->GetString(0), false);
	}
	else
	{
		pSelf->StartVideo("video", true);
	}
}

void CClient::StartVideo(const char *pFilename, bool WithTimestamp)
{
	if(State() != IClient::STATE_DEMOPLAYBACK)
	{
		log_error("videorecorder", "Video can only be recorded in demo player.");
		return;
	}

	if(IVideo::Current())
	{
		log_error("videorecorder", "Already recording.");
		return;
	}

	char aFilename[IO_MAX_PATH_LENGTH];
	if(WithTimestamp)
	{
		char aTimestamp[20];
		str_timestamp(aTimestamp, sizeof(aTimestamp));
		str_format(aFilename, sizeof(aFilename), "videos/%s_%s.mp4", pFilename, aTimestamp);
	}
	else
	{
		str_format(aFilename, sizeof(aFilename), "videos/%s.mp4", pFilename);
	}

	// wait for idle, so there is no data race
	Graphics()->WaitForIdle();
	// pause the sound device while creating the video instance
	Sound()->PauseAudioDevice();
	new CVideo(Graphics(), Sound(), Storage(), Graphics()->ScreenWidth(), Graphics()->ScreenHeight(), aFilename);
	Sound()->UnpauseAudioDevice();
	if(!IVideo::Current()->Start())
	{
		// Release partially initialized recorder state (threads/ffmpeg resources).
		IVideo::Current()->Stop();
		log_error("videorecorder", "Failed to start recording to '%s'", aFilename);
		m_DemoPlayer.Stop("Failed to start video recording. See local console for details.");
		return;
	}
	if(m_DemoPlayer.Info()->m_Info.m_Paused)
	{
		IVideo::Current()->Pause(true);
	}
	log_info("videorecorder", "Recording to '%s'", aFilename);
}

void CClient::Con_StopVideo(IConsole::IResult *pResult, void *pUserData)
{
	if(!IVideo::Current())
	{
		log_error("videorecorder", "Not recording.");
		return;
	}

	IVideo::Current()->Stop();
	log_info("videorecorder", "Stopped recording.");
}

#endif

void CClient::Con_Rcon(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Rcon(pResult->GetString(0));
}

void CClient::Con_RconAuth(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->RconAuth("", pResult->GetString(0));
}

void CClient::Con_RconLogin(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->RconAuth(pResult->GetString(0), pResult->GetString(1));
}

void CClient::Con_BeginFavoriteGroup(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->m_FavoritesGroup)
	{
		log_error("client", "opening favorites group while there is already one, discarding old one");
		for(int i = 0; i < pSelf->m_FavoritesGroupNum; i++)
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&pSelf->m_aFavoritesGroupAddresses[i], aAddr, sizeof(aAddr), true);
			log_warn("client", "discarding %s", aAddr);
		}
	}
	pSelf->m_FavoritesGroup = true;
	pSelf->m_FavoritesGroupAllowPing = false;
	pSelf->m_FavoritesGroupNum = 0;
}

void CClient::Con_EndFavoriteGroup(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(!pSelf->m_FavoritesGroup)
	{
		log_error("client", "closing favorites group while there is none, ignoring");
		return;
	}
	log_info("client", "adding group of %d favorites", pSelf->m_FavoritesGroupNum);
	pSelf->m_pFavorites->Add(pSelf->m_aFavoritesGroupAddresses, pSelf->m_FavoritesGroupNum);
	if(pSelf->m_FavoritesGroupAllowPing)
	{
		pSelf->m_pFavorites->AllowPing(pSelf->m_aFavoritesGroupAddresses, pSelf->m_FavoritesGroupNum, true);
	}
	pSelf->m_FavoritesGroup = false;
}

void CClient::Con_AddFavorite(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	NETADDR Addr;

	if(net_addr_from_url(&Addr, pResult->GetString(0), nullptr, 0) != 0 && net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "invalid address '%s'", pResult->GetString(0));
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
		return;
	}
	bool AllowPing = pResult->NumArguments() > 1 && str_find(pResult->GetString(1), "allow_ping");
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
	if(pSelf->m_FavoritesGroup)
	{
		if(pSelf->m_FavoritesGroupNum == (int)std::size(pSelf->m_aFavoritesGroupAddresses))
		{
			log_error("client", "discarding %s because groups can have at most a size of %d", aAddr, pSelf->m_FavoritesGroupNum);
			return;
		}
		log_info("client", "adding %s to favorites group", aAddr);
		pSelf->m_aFavoritesGroupAddresses[pSelf->m_FavoritesGroupNum] = Addr;
		pSelf->m_FavoritesGroupAllowPing = pSelf->m_FavoritesGroupAllowPing || AllowPing;
		pSelf->m_FavoritesGroupNum += 1;
	}
	else
	{
		log_info("client", "adding %s to favorites", aAddr);
		pSelf->m_pFavorites->Add(&Addr, 1);
		if(AllowPing)
		{
			pSelf->m_pFavorites->AllowPing(&Addr, 1, true);
		}
	}
}

void CClient::Con_RemoveFavorite(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) == 0)
		pSelf->m_pFavorites->Remove(&Addr, 1);
}

void CClient::DemoSliceBegin()
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	g_Config.m_ClDemoSliceBegin = pInfo->m_Info.m_CurrentTick;
}

void CClient::DemoSliceEnd()
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	g_Config.m_ClDemoSliceEnd = pInfo->m_Info.m_CurrentTick;
}

void CClient::Con_DemoSliceBegin(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoSliceBegin();
}

void CClient::Con_DemoSliceEnd(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoSliceEnd();
}

void CClient::Con_SaveReplay(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pResult->NumArguments())
	{
		int Length = pResult->GetInteger(0);
		if(Length <= 0)
		{
			pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: length must be greater than 0 second.");
		}
		else
		{
			if(pResult->NumArguments() >= 2)
			{
				pSelf->SaveReplay(Length, pResult->GetString(1));
			}
			else
			{
				pSelf->SaveReplay(Length);
			}
		}
	}
	else
	{
		pSelf->SaveReplay(g_Config.m_ClReplayLength);
	}
}

void CClient::SaveReplay(const int Length, const char *pFilename)
{
	if(!g_Config.m_ClReplays)
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "Feature is disabled. Please enable it via configuration.");
		GameClient()->Echo(Localize("Replay feature is disabled!"));
		return;
	}

	if(!DemoRecorder(RECORDER_REPLAYS)->IsRecording())
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: demorecorder isn't recording. Try to rejoin to fix that.");
	}
	else if(DemoRecorder(RECORDER_REPLAYS)->Length() < 1)
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: demorecorder isn't recording for at least 1 second.");
	}
	else
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		if(pFilename[0] == '\0')
		{
			char aTimestamp[20];
			str_timestamp(aTimestamp, sizeof(aTimestamp));
			m_pStorage->CreateFolder("demos/highlight", IStorage::TYPE_SAVE);
			str_format(aFilename, sizeof(aFilename), "demos/highlight/高光-%s.demo", aTimestamp);
		}
		else
		{
			str_format(aFilename, sizeof(aFilename), "demos/replays/%s.demo", pFilename);
			IOHANDLE Handle = m_pStorage->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(!Handle)
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: invalid filename. Try a different one!");
				return;
			}
			io_close(Handle);
			m_pStorage->RemoveFile(aFilename, IStorage::TYPE_SAVE);
		}

		// Stop the recorder to correctly slice the demo after
		DemoRecorder(RECORDER_REPLAYS)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);

		// Slice the demo to get only the last cl_replay_length seconds
		const char *pSrc = DemoRecorder(RECORDER_REPLAYS)->CurrentFilename();
		const int EndTick = GameTick(g_Config.m_ClDummy);
		const int StartTick = EndTick - Length * GameTickSpeed();

		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "Saving replay...");

		// Create a job to do this slicing in background because it can be a bit long depending on the file size
		std::shared_ptr<CDemoEdit> pDemoEditTask = std::make_shared<CDemoEdit>(GameClient()->NetVersion(), &*m_pSnapshotDelta, &*m_pSnapshotDeltaSixup, m_pStorage, pSrc, aFilename, StartTick, EndTick);
		Engine()->AddJob(pDemoEditTask);
		m_EditJobs.push_back(pDemoEditTask);

		// And we restart the recorder
		DemoRecorder_UpdateReplayRecorder();
	}
}

bool CClient::DemoSlice(const char *pDstPath, CLIENTFUNC_FILTER pfnFilter, void *pUser)
{
	if(m_DemoPlayer.IsPlaying())
	{
		return m_DemoEditor.Slice(m_DemoPlayer.Filename(), pDstPath, g_Config.m_ClDemoSliceBegin, g_Config.m_ClDemoSliceEnd, pfnFilter, pUser);
	}
	return false;
}

bool CClient::DemoSlice(const char *pDstPath, const std::vector<SDemoSliceSegment> &vSegments, CLIENTFUNC_FILTER pfnFilter, void *pUser)
{
	if(m_DemoPlayer.IsPlaying())
	{
		return m_DemoEditor.Slice(m_DemoPlayer.Filename(), pDstPath, vSegments, pfnFilter, pUser);
	}
	return false;
}

const char *CClient::DemoPlayer_Play(const char *pFilename, int StorageType)
{
	// Don't disconnect unless the file exists (only for play command)
	if(!Storage()->FileExists(pFilename, StorageType))
		return Localize("No demo with this filename exists");

	Disconnect();
	m_aNetClient[CONN_MAIN].ResetErrorString();

	SetState(IClient::STATE_LOADING);
	SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_LOADING_DEMO);
	if((bool)m_LoadingCallback)
		m_LoadingCallback(IClient::LOADING_CALLBACK_DETAIL_DEMO);

	// try to start playback
	m_DemoPlayer.SetListener(this);
	if(m_DemoPlayer.Load(Storage(), m_pConsole, pFilename, StorageType))
	{
		DisconnectWithReason(m_DemoPlayer.ErrorMessage());
		return m_DemoPlayer.ErrorMessage();
	}

	m_Sixup = m_DemoPlayer.IsSixup();

	// load map
	const CMapInfo *pMapInfo = m_DemoPlayer.GetMapInfo();
	const char *pError = LoadMapSearch(pMapInfo->m_aName, pMapInfo->m_Sha256, pMapInfo->m_Crc);
	if(pError)
	{
		if(!m_DemoPlayer.ExtractMap(Storage()))
		{
			DisconnectWithReason(pError);
			return pError;
		}

		pError = LoadMapSearch(pMapInfo->m_aName, pMapInfo->m_Sha256, pMapInfo->m_Crc);
		if(pError)
		{
			DisconnectWithReason(pError);
			return pError;
		}
	}

	// setup current server info
	m_CurrentServerInfo = {};
	str_copy(m_CurrentServerInfo.m_aMap, pMapInfo->m_aName);
	m_CurrentServerInfo.m_MapCrc = pMapInfo->m_Crc;
	m_CurrentServerInfo.m_MapSize = pMapInfo->m_Size;

	// enter demo playback state
	SetState(IClient::STATE_DEMOPLAYBACK);

	GameClient()->OnConnected();

	// setup buffers
	mem_zero(m_aaDemorecSnapshotData, sizeof(m_aaDemorecSnapshotData));

	for(int SnapshotType = 0; SnapshotType < NUM_SNAPSHOT_TYPES; SnapshotType++)
	{
		m_aapSnapshots[0][SnapshotType] = &m_aDemorecSnapshotHolders[SnapshotType];
		m_aapSnapshots[0][SnapshotType]->m_pSnap = m_aaDemorecSnapshotData[SnapshotType][0].AsSnapshot();
		m_aapSnapshots[0][SnapshotType]->m_pAltSnap = m_aaDemorecSnapshotData[SnapshotType][1].AsSnapshot();
		m_aapSnapshots[0][SnapshotType]->m_SnapSize = 0;
		m_aapSnapshots[0][SnapshotType]->m_AltSnapSize = 0;
		m_aapSnapshots[0][SnapshotType]->m_Tick = -1;
	}

	m_DemoPlayer.Play();
	GameClient()->OnEnterGame();

	return nullptr;
}

#if defined(CONF_VIDEORECORDER)
const char *CClient::DemoPlayer_Render(const char *pFilename, int StorageType, const char *pVideoName, int SpeedIndex, bool StartPaused)
{
	const char *pError = DemoPlayer_Play(pFilename, StorageType);
	if(pError)
		return pError;

	StartVideo(pVideoName, false);
	m_DemoPlayer.SetSpeedIndex(SpeedIndex);
	if(StartPaused)
	{
		m_DemoPlayer.Pause();
	}
	return nullptr;
}
#endif

void CClient::Con_Play(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->HandleDemoPath(pResult->GetString(0));
}

void CClient::Con_DemoPlay(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->m_DemoPlayer.IsPlaying())
	{
		if(pSelf->m_DemoPlayer.BaseInfo()->m_Paused)
		{
			pSelf->m_DemoPlayer.Unpause();
		}
		else
		{
			pSelf->m_DemoPlayer.Pause();
		}
	}
}

void CClient::Con_DemoSpeed(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->m_DemoPlayer.SetSpeed(pResult->GetFloat(0));
}

void CClient::DemoRecorder_Start(const char *pFilename, bool WithTimestamp, int Recorder)
{
	dbg_assert(State() == IClient::STATE_ONLINE, "Client must be online to record demo");

	if(!m_pMap || !m_pMap->IsLoaded())
	{
		log_error("demo_recorder", "Map is not loaded yet.");
		return;
	}

	char aFilename[IO_MAX_PATH_LENGTH];
	if(WithTimestamp)
	{
		char aTimestamp[20];
		str_timestamp(aTimestamp, sizeof(aTimestamp));
		if(Recorder == RECORDER_REPLAYS)
		{
			char aRandomSuffix[9];
			secure_random_password(aRandomSuffix, sizeof(aRandomSuffix), 8);
			str_format(aFilename, sizeof(aFilename), "demos/%s_%s_%s.demo", pFilename, aTimestamp, aRandomSuffix);
		}
		else
		{
			str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", pFilename, aTimestamp);
		}
	}
	else
	{
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pFilename);
	}

	DemoRecorders()[Recorder].Start(
		Storage(),
		m_pConsole,
		aFilename,
		IsSixup() ? GameClient()->NetVersion7() : GameClient()->NetVersion(),
		m_aCurrentMap,
		m_pMap->Sha256(),
		m_pMap->Crc(),
		"client",
		m_pMap->Size(),
		nullptr,
		m_pMap->File(),
		nullptr,
		nullptr);
}

void CClient::DemoRecorder_HandleAutoStart()
{
	if(State() != IClient::STATE_ONLINE)
	{
		return;
	}

	if(g_Config.m_ClAutoDemoRecord)
	{
		DemoRecorder(RECORDER_AUTO)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);

		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "auto/%s", m_aCurrentMap);
		DemoRecorder_Start(aFilename, true, RECORDER_AUTO);

		if(g_Config.m_ClAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/auto", "" /* empty for wild card */, ".demo", g_Config.m_ClAutoDemoMax);
		}
	}

	DemoRecorder_UpdateReplayRecorder();
}

void CClient::DemoRecorder_UpdateReplayRecorder()
{
	if(!g_Config.m_ClReplays && DemoRecorder(RECORDER_REPLAYS)->IsRecording())
	{
		DemoRecorder(RECORDER_REPLAYS)->Stop(IDemoRecorder::EStopMode::REMOVE_FILE);
	}

	if(g_Config.m_ClReplays && !DemoRecorder(RECORDER_REPLAYS)->IsRecording())
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "replays/replay_tmp_%s", m_aCurrentMap);
		DemoRecorder_Start(aFilename, true, RECORDER_REPLAYS);
	}
}

bool CClient::DemoRecorder_AddDemoMarker(int Recorder)
{
	return DemoRecorders()[Recorder].AddDemoMarker();
}

CDemoRecorder (&CClient::DemoRecorders()) [RECORDER_MAX] {
	if(IsSixup())
	{
		return m_aDemoRecordersSixup;
	}
	return m_aDemoRecorders;
}

IDemoRecorder *CClient::DemoRecorder(int Recorder)
{
	return &DemoRecorders()[Recorder];
}

void CClient::Con_Record(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;

	if(pSelf->State() != IClient::STATE_ONLINE)
	{
		log_error("demo_recorder", "Client is not online.");
		return;
	}
	if(pSelf->DemoRecorder(RECORDER_MANUAL)->IsRecording())
	{
		log_error("demo_recorder", "Demo recorder already recording to '%s'.", pSelf->DemoRecorder(RECORDER_MANUAL)->CurrentFilename());
		return;
	}

	if(pResult->NumArguments())
		pSelf->DemoRecorder_Start(pResult->GetString(0), false, RECORDER_MANUAL);
	else
		pSelf->DemoRecorder_Start(pSelf->m_aCurrentMap, true, RECORDER_MANUAL);
}

void CClient::Con_StopRecord(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoRecorder(RECORDER_MANUAL)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
}

void CClient::Con_AddDemoMarker(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	for(int Recorder = 0; Recorder < RECORDER_MAX; Recorder++)
		pSelf->DemoRecorder_AddDemoMarker(Recorder);
}

void CClient::Con_BenchmarkQuit(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	int Seconds = pResult->GetInteger(0);
	const char *pFilename = pResult->GetString(1);
	pSelf->BenchmarkQuit(Seconds, pFilename);
}

void CClient::BenchmarkQuit(int Seconds, const char *pFilename)
{
	m_BenchmarkFile = Storage()->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
	m_BenchmarkStopTime = time_get() + time_freq() * Seconds;
}

void CClient::StartHangWatchdog()
{
	m_HangWatchdogStop.store(false, std::memory_order_release);
	m_HangReportWritten.store(false, std::memory_order_release);
	if(m_aHangDumpDir[0] == '\0' && Storage() != nullptr)
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, gs_pQmCrashDumpDir, m_aHangDumpDir, sizeof(m_aHangDumpDir));
	UpdateHangHeartbeat();

	if(m_HangWatchdogThread.joinable())
		return;

	m_HangWatchdogThread = std::thread([this]() {
		// 必须使用单调时钟（time_get_nanoseconds）而不是 time_get()：后者只在主循环
		// set_new_tick() 后刷新一次，主线程卡住时全进程时钟冻结，看门狗将永远无法
		// 感知心跳停滞。看门狗线程恰恰需要在主线程阻塞时继续计时。
		const int64_t TimeoutNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(gs_HangTimeoutSeconds)).count();
		while(!m_HangWatchdogStop.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(1s);
			const int64_t LastHeartbeat = m_HangLastHeartbeat.load(std::memory_order_acquire);
			if(LastHeartbeat == 0)
				continue;
			const int64_t Now = time_get_nanoseconds().count();
			if(Now - LastHeartbeat >= TimeoutNanoseconds)
			{
				if(!m_HangReportWritten.exchange(true, std::memory_order_acq_rel))
					WriteHangReportAndDump(Now, LastHeartbeat);
			}
		}
	});
}

void CClient::StopHangWatchdog()
{
	m_HangWatchdogStop.store(true, std::memory_order_release);
	if(m_HangWatchdogThread.joinable())
		m_HangWatchdogThread.join();
}

void CClient::UpdateHangHeartbeat()
{
	const int NextIndex = 1 - m_HangInfoIndex.load(std::memory_order_relaxed);
	SHangInfo &Info = m_aHangInfo[NextIndex];
	Info.m_State = m_State;
	str_copy(Info.m_aCurrentMap, m_aCurrentMap, sizeof(Info.m_aCurrentMap));
	const NETADDR *pAddr = ServerAddress();
	if(!pAddr || pAddr->type == NETTYPE_INVALID)
	{
		str_copy(Info.m_aServerAddr, "unknown", sizeof(Info.m_aServerAddr));
	}
	else
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(pAddr, aAddr, sizeof(aAddr), true);
		str_copy(Info.m_aServerAddr, aAddr, sizeof(Info.m_aServerAddr));
	}
	m_HangInfoIndex.store(NextIndex, std::memory_order_release);
	// 心跳使用单调时钟纳秒（time_get_nanoseconds），与主循环 tick 缓存解耦，
	// 保证主线程阻塞时看门狗仍能度量真实流逝时间。
	m_HangLastHeartbeat.store(time_get_nanoseconds().count(), std::memory_order_release);
}

void CClient::WriteHangReportAndDump(int64_t Now, int64_t LastHeartbeat)
{
	if(m_aHangDumpDir[0] == '\0')
		return;

	char aDate[64];
	str_timestamp(aDate, sizeof(aDate));

	char aReportFilename[IO_MAX_PATH_LENGTH];
	str_format(aReportFilename, sizeof(aReportFilename),
		GAME_NAME "_%s_hang_report_%s_%d_%s.txt",
		CONF_PLATFORM_STRING, aDate, pid(), GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");
	char aReportPath[IO_MAX_PATH_LENGTH];
	str_format(aReportPath, sizeof(aReportPath), "%s/%s", m_aHangDumpDir, aReportFilename);
	fs_makedir_rec_for(aReportPath);

	IOHANDLE File = io_open(aReportPath, IOFLAG_WRITE);
	if(File)
	{
		const int SnapshotIndex = m_HangInfoIndex.load(std::memory_order_acquire);
		const SHangInfo Snapshot = m_aHangInfo[SnapshotIndex];
		const float SecondsSinceHeartbeat = (Now - LastHeartbeat) / 1e9f;
		char aOsVersion[128];
		if(!os_version_str(aOsVersion, sizeof(aOsVersion)))
			str_copy(aOsVersion, "unknown");

		char aBuf[2048];
		str_copy(aBuf, "QmClient hang diagnostic report\n");
		io_write(File, aBuf, str_length(aBuf));

		str_copy(aBuf, "Report type: hang\n");
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Timestamp: %s\n", aDate);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Process ID: %d\n", pid());
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Hang timeout threshold: %lld seconds\n", (long long)gs_HangTimeoutSeconds);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "No heartbeat duration: %.1f seconds\n", SecondsSinceHeartbeat);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Heartbeat clock: now=%lld, last=%lld, delta=%lld\n", (long long)Now, (long long)LastHeartbeat, (long long)(Now - LastHeartbeat));
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Client state: %s (%d)\n", ClientStateToString(Snapshot.m_State), Snapshot.m_State);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Current map: %s\n", Snapshot.m_aCurrentMap);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Server address: %s\n", Snapshot.m_aServerAddr);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "OS version: %s\n", aOsVersion);
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Game version: %s %s %s\n", GAME_NAME, GAME_RELEASE_VERSION, GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");
		io_write(File, aBuf, str_length(aBuf));

		str_format(aBuf, sizeof(aBuf), "Report directory: %s\n", m_aHangDumpDir);
		io_write(File, aBuf, str_length(aBuf));

		io_sync(File);
		io_close(File);
	}

#if defined(CONF_FAMILY_WINDOWS)
	char aDumpFilename[IO_MAX_PATH_LENGTH];
	str_format(aDumpFilename, sizeof(aDumpFilename),
		GAME_NAME "_%s_hang_dump_%s_%d_%s.dmp",
		CONF_PLATFORM_STRING, aDate, pid(), GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");
	char aDumpPath[IO_MAX_PATH_LENGTH];
	str_format(aDumpPath, sizeof(aDumpPath), "%s/%s", m_aHangDumpDir, aDumpFilename);
	fs_makedir_rec_for(aDumpPath);
	WriteMiniDumpFile(aDumpPath);
#endif

	if(!crashdump_launch_reporter_if_available(aReportPath))
		log_warn("hang", "failed to launch crash reporter for '%s'; it will be offered on the next start", aReportPath);
}

bool CClient::HandleQmGraphicsFatalError()
{
	// 图形后端已经记录了致命错误（例如 Vulkan VK_ERROR_DEVICE_LOST）。
	// 一旦这个错误被提交（CGraphicsBackend_Threaded::ProcessError）就会断言退出，
	// 而断言弹窗会阻塞主线程、停掉心跳，看门狗随后写出误导性的 hang 报告。
	// 这里在提交之前主动收口：写诊断报告 -> 干净重启走安全图形设置。
	if(m_QmGraphicsRecoveryAttempted)
		return false;
	m_QmGraphicsRecoveryAttempted = true;

	const char *pFatalError = Graphics()->GetFatalError();
	char aGpuInfo[512];
	GetGpuInfoString(aGpuInfo);
	char aDate[64];
	str_timestamp(aDate, sizeof(aDate));
	char aBackend[64];
	str_copy(aBackend, g_Config.m_GfxBackend);
	EBackendType FailedBackend = graphics_backend::BackendFromCrashReport(aGpuInfo);
	if(FailedBackend == BACKEND_TYPE_AUTO)
		FailedBackend = graphics_backend::ParseBackendName(aBackend, BACKEND_TYPE_AUTO);
	char aServerAddr[NETADDR_MAXSTRSIZE];
	const NETADDR *pAddr = ServerAddress();
	if(!pAddr || pAddr->type == NETTYPE_INVALID)
		str_copy(aServerAddr, "unknown");
	else
		net_addr_str(pAddr, aServerAddr, sizeof(aServerAddr), true);
	char aFilename[IO_MAX_PATH_LENGTH];
	// 文件名必须与 echndl 的崩溃报告一致，才能在下次启动时被
	// RecoverQmGraphicsSettingsAfterDriverCrash 识别并做安全图形恢复。
	str_format(aFilename, sizeof(aFilename), "%s/" GAME_NAME "_%s_crash_log_%s_%d_%s_fatal_report.txt",
		gs_pQmCrashDumpDir, CONF_PLATFORM_STRING, aDate, pid(), GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");

	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aFilename, aPath, sizeof(aPath));
	fs_makedir_rec_for(aPath);

	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	if(File)
	{
		char aBuf[2048];
		str_format(aBuf, sizeof(aBuf),
			"QmClient runtime graphics fault report\n"
			"Report type: graphics_fatal_error\n"
			"Timestamp: %s\n"
			"Process ID: %d\n"
			"Graphics backend: %s\n"
			"Configured graphics backend: %s\n"
			"Client state: %s (%d)\n"
			"Current map: %s\n"
			"Server address: %s\n"
			"Game version: %s %s %s\n"
			"\n"
			"Graphics error:\n%s\n"
			"\n"
			"%s\n",
			aDate, pid(), graphics_backend::BackendName(FailedBackend), aBackend, ClientStateToString(m_State), m_State,
			m_aCurrentMap[0] != '\0' ? m_aCurrentMap : "(none)",
			aServerAddr,
			GAME_NAME, GAME_RELEASE_VERSION, GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "",
			pFatalError[0] != '\0' ? pFatalError : "(not reported by the backend)",
			aGpuInfo);
		io_write(File, aBuf, str_length(aBuf));
		io_sync(File);
		io_close(File);
	}
	else
	{
		log_error("gfx", "could not write runtime graphics fault report to '%s'", aPath);
	}

	SQmGraphicsRecoveryState RecoveryState;
	ReadQmGraphicsRecoveryState(Storage(), RecoveryState);
	RecoveryState.m_Failures.Record(FailedBackend);
	if(graphics_backend::RecoveryBackend(RecoveryState.m_Failures, FailedBackend) == BACKEND_TYPE_AUTO)
	{
		log_error("gfx", "graphics recovery has no available backend; stopping instead of restarting: %s",
			pFatalError[0] != '\0' ? pFatalError : "(no details)");
		SetState(IClient::STATE_QUITTING);
		return true;
	}
	log_error("gfx", "graphics backend reported a fatal error, restarting the client with safe graphics settings: %s",
		pFatalError[0] != '\0' ? pFatalError : "(no details)");
	Restart();
	return true;
}

void CClient::UpdateAndSwap()
{
	Input()->Update();
	Graphics()->Swap();
	// QmClient: 窗口是隐藏创建的（见 backend_sdl.cpp 里的 SDL_WINDOW_HIDDEN）。
	// 这里在第一帧真正 present 之后再显示它，启动就不会先闪一帧纯黑。
	// 本函数只有一条调用路径：加载界面 RenderLoadingDirect() 末尾的
	// UpdateAndSwapClient()。主循环的呈现走的是 m_pGraphics->Swap()，不经过这里，
	// 所以主循环由 Run() 进 while 前那次无条件 ShowWindow() 负责。
	// 用标志只调一次：加载界面每帧都会走到这里，没必要重复调 SDL_ShowWindow。
	if(!m_WindowShown)
	{
		m_WindowShown = true;
		// 必须等这一帧真的 present 出去再显示窗口。Swap() 是异步的：它只把
		// SCommand_Swap 入队、KickCommandBuffer() 就返回，present 由渲染线程执行。
		// 不等的话，窗口显示出来的那一刻屏幕上还是 Run() 里 Clear(0,0,0) 的那帧黑，
		// 加载帧仍在渲染线程里排队 —— 用户看到的就是"先黑一下再出现加载界面"。
		// Vulkan 首次 present 叠加 vsync 等待更久，这段黑尤其明显；GL 上同样存在。
		// WaitForIdle() 等到的是渲染线程处理完整个缓冲（CGraphicsBackend_Threaded
		// 在 m_pProcessor->RunBuffer() 返回之后才置空 m_pBuffer），所以返回即已 present，
		// 且不依赖时序猜测。只在首帧付一次等待成本。
		Graphics()->WaitForIdle();
		Graphics()->ShowWindow();
	}
	// QmClient: 帧间清屏同样保持纯黑，理由同 CClient::Run() 的首帧清屏。
	// 这里曾是 cl_background_color（默认 128 → #808080），加载期间任何
	// "还没绘制就被呈现"的空帧都会露出整屏中灰。
	Graphics()->Clear(0, 0, 0);
	if(g_Config.m_QmGraphicsTrace >= 3)
		dbg_msg("gfx/swap", "swap source=loading state=%d", State());
	m_GlobalTime = (time_get() - m_GlobalStartTime) / (float)time_freq();
}

void CClient::ServerBrowserUpdate()
{
	m_ServerBrowser.RequestResort();
}

void CClient::ConchainServerBrowserUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CClient *)pUserData)->ServerBrowserUpdate();
}

void CClient::InitChecksum()
{
	CChecksumData *pData = &m_Checksum.m_Data;
	pData->m_SizeofData = sizeof(*pData);
	str_copy(pData->m_aVersionStr, CLIENT_NAME " " GAME_RELEASE_VERSION " (" CONF_PLATFORM_STRING "; " CONF_ARCH_STRING ")");
	pData->m_Start = time_get();
	os_version_str(pData->m_aOsVersion, sizeof(pData->m_aOsVersion));
	secure_random_fill(&pData->m_Random, sizeof(pData->m_Random));
	pData->m_Version = GameClient()->DDNetVersion();
	pData->m_SizeofClient = sizeof(*this);
	pData->m_SizeofConfig = sizeof(pData->m_Config);
	pData->InitFiles();
}

#ifndef DDNET_CHECKSUM_SALT
// salt@checksum.ddnet.tw: db877f2b-2ddb-3ba6-9f67-a6d169ec671d
#define DDNET_CHECKSUM_SALT \
	{ \
		{ \
			0xdb, 0x87, 0x7f, 0x2b, 0x2d, 0xdb, 0x3b, 0xa6, \
				0x9f, 0x67, 0xa6, 0xd1, 0x69, 0xec, 0x67, 0x1d, \
		} \
	}
#endif

int CClient::HandleChecksum(int Conn, CUuid Uuid, CUnpacker *pUnpacker)
{
	int Start = pUnpacker->GetInt();
	int Length = pUnpacker->GetInt();
	if(pUnpacker->Error())
	{
		return 1;
	}
	if(Start < 0 || Length < 0 || Start > std::numeric_limits<int>::max() - Length)
	{
		return 2;
	}
	int End = Start + Length;
	int ChecksumBytesEnd = minimum(End, (int)sizeof(m_Checksum.m_aBytes));
	int FileStart = maximum(Start, (int)sizeof(m_Checksum.m_aBytes));
	unsigned char aStartBytes[sizeof(int32_t)];
	unsigned char aEndBytes[sizeof(int32_t)];
	uint_to_bytes_be(aStartBytes, Start);
	uint_to_bytes_be(aEndBytes, End);

	if(Start <= (int)sizeof(m_Checksum.m_aBytes))
	{
		mem_zero(&m_Checksum.m_Data.m_Config, sizeof(m_Checksum.m_Data.m_Config));
#define CHECKSUM_RECORD(Flags) (((Flags) & CFGFLAG_CLIENT) == 0 || ((Flags) & CFGFLAG_INSENSITIVE) != 0)
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	if(CHECKSUM_RECORD(Flags)) \
	{ \
		m_Checksum.m_Data.m_Config.m_##Name = g_Config.m_##Name; \
	}
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	if(CHECKSUM_RECORD(Flags)) \
	{ \
		m_Checksum.m_Data.m_Config.m_##Name = g_Config.m_##Name; \
	}
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	if(CHECKSUM_RECORD(Flags)) \
	{ \
		str_copy(m_Checksum.m_Data.m_Config.m_##Name, g_Config.m_##Name, sizeof(m_Checksum.m_Data.m_Config.m_##Name)); \
	}
#include <engine/shared/config_variables.h>
#undef CHECKSUM_RECORD
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
	}
	if(End > (int)sizeof(m_Checksum.m_aBytes))
	{
		if(m_OwnExecutableSize == 0)
		{
			m_OwnExecutable = io_current_exe();
			// io_length returns -1 on error.
			m_OwnExecutableSize = m_OwnExecutable ? io_length(m_OwnExecutable) : -1;
		}
		// Own executable not available.
		if(m_OwnExecutableSize < 0)
		{
			return 3;
		}
		if(End - (int)sizeof(m_Checksum.m_aBytes) > m_OwnExecutableSize)
		{
			return 4;
		}
	}

	SHA256_CTX Sha256Ctxt;
	sha256_init(&Sha256Ctxt);
	CUuid Salt = DDNET_CHECKSUM_SALT;
	sha256_update(&Sha256Ctxt, &Salt, sizeof(Salt));
	sha256_update(&Sha256Ctxt, &Uuid, sizeof(Uuid));
	sha256_update(&Sha256Ctxt, aStartBytes, sizeof(aStartBytes));
	sha256_update(&Sha256Ctxt, aEndBytes, sizeof(aEndBytes));
	if(Start < (int)sizeof(m_Checksum.m_aBytes))
	{
		sha256_update(&Sha256Ctxt, m_Checksum.m_aBytes + Start, ChecksumBytesEnd - Start);
	}
	if(End > (int)sizeof(m_Checksum.m_aBytes))
	{
		unsigned char aBuf[2048];
		if(io_seek(m_OwnExecutable, FileStart - sizeof(m_Checksum.m_aBytes), IOSEEK_START))
		{
			return 5;
		}
		for(int i = FileStart; i < End; i += sizeof(aBuf))
		{
			int Read = io_read(m_OwnExecutable, aBuf, minimum((int)sizeof(aBuf), End - i));
			sha256_update(&Sha256Ctxt, aBuf, Read);
		}
	}
	SHA256_DIGEST Sha256 = sha256_finish(&Sha256Ctxt);

	CMsgPacker Msg(NETMSG_CHECKSUM_RESPONSE, true);
	Msg.AddRaw(&Uuid, sizeof(Uuid));
	Msg.AddRaw(&Sha256, sizeof(Sha256));
	SendMsg(Conn, &Msg, MSGFLAG_VITAL);

	return 0;
}

void CClient::ConchainWindowScreen(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxScreen != pResult->GetInteger(0))
		{
			pSelf->Graphics()->SwitchWindowScreen(pResult->GetInteger(0), true);
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CClient::ConchainFullscreen(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxFullscreen != pResult->GetInteger(0))
		{
			pSelf->Graphics()->SetWindowParams(pResult->GetInteger(0), g_Config.m_GfxBorderless);
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CClient::ConchainWindowBordered(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(!g_Config.m_GfxFullscreen && (g_Config.m_GfxBorderless != pResult->GetInteger(0)))
		{
			pSelf->Graphics()->SetWindowParams(g_Config.m_GfxFullscreen, !g_Config.m_GfxBorderless);
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CClient::Notify(const char *pTitle, const char *pMessage)
{
	if(m_pGraphics->WindowActive() || !g_Config.m_ClShowNotifications)
		return;

	Notifications()->Notify(pTitle, pMessage);
	Graphics()->NotifyWindow();
}

void CClient::OnWindowResize()
{
	TextRender()->OnPreWindowResize();
	GameClient()->OnWindowResize();
	m_pEditor->OnWindowResize();
	TextRender()->OnWindowResize();
}

void CClient::ConchainWindowVSync(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxVsync != pResult->GetInteger(0))
		{
			pSelf->Graphics()->SetVSync(pResult->GetInteger(0));
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CClient::ConchainWindowResize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		pSelf->Graphics()->ResizeToScreen();
	}
}

void CClient::ConchainTimeoutSeed(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		pSelf->m_GenerateTimeoutSeed = false;
}

void CClient::ConchainPassword(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pSelf->m_LocalStartTime) //won't set m_SendPassword before game has started
		pSelf->m_SendPassword = true;
}

void CClient::ConchainReplays(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pSelf->State() == IClient::STATE_ONLINE)
	{
		pSelf->DemoRecorder_UpdateReplayRecorder();
	}
}

void CClient::ConchainInputFifo(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pSelf->m_Fifo.IsInit())
	{
		pSelf->m_Fifo.Shutdown();
		pSelf->m_Fifo.Init(pSelf->m_pConsole, pSelf->Config()->m_ClInputFifo, CFGFLAG_CLIENT);
	}
}

void CClient::ConchainNetReset(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		pSelf->ResetSocket();
}

void CClient::ConchainLoglevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		pSelf->m_pFileLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_Loglevel)});
	}
}

void CClient::ConchainStdoutOutputLevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pSelf->m_pStdoutLogger)
	{
		pSelf->m_pStdoutLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_StdoutOutputLevel)});
	}
}

void CClient::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pFavorites = Kernel()->RequestInterface<IFavorites>();

	m_pConsole->Register("dummy_connect", "", CFGFLAG_CLIENT, Con_DummyConnect, this, "Connect dummy");
	m_pConsole->Register("dummy_disconnect", "", CFGFLAG_CLIENT, Con_DummyDisconnect, this, "Disconnect dummy");
	m_pConsole->Register("dummy_reset", "", CFGFLAG_CLIENT, Con_DummyResetInput, this, "Reset dummy");

	m_pConsole->Register("quit", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Quit, this, "Quit the client");
	m_pConsole->Register("exit", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Quit, this, "Quit the client");
	m_pConsole->Register("restart", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Restart, this, "Restart the client");
	m_pConsole->Register("minimize", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Minimize, this, "Minimize the client");
	m_pConsole->Register("connect", "r[host|ip]", CFGFLAG_CLIENT, Con_Connect, this, "Connect to the specified host/ip");
	m_pConsole->Register("disconnect", "", CFGFLAG_CLIENT, Con_Disconnect, this, "Disconnect from the server");
	m_pConsole->Register("qm_timeout_disconnect", "", CFGFLAG_CLIENT, Con_QmTimeoutDisconnect, this, "Silently disconnect from the current server while keeping Tee timeout protection");
	m_pConsole->Register("qm_net_qos_status", "", CFGFLAG_CLIENT, Con_QmNetQosStatus, this, "Show outgoing game traffic QoS status");
	m_pConsole->Register("ping", "", CFGFLAG_CLIENT, Con_Ping, this, "Ping the current server");
	m_pConsole->Register("screenshot", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Screenshot, this, "Take a screenshot");
	m_pConsole->Register("net_reset", "", CFGFLAG_CLIENT, ConNetReset, this, "Rebinds the client's listening address and port");

#if defined(CONF_VIDEORECORDER)
	m_pConsole->Register("start_video", "?r[file]", CFGFLAG_CLIENT, Con_StartVideo, this, "Start recording a video");
	m_pConsole->Register("stop_video", "", CFGFLAG_CLIENT, Con_StopVideo, this, "Stop recording a video");
#endif

	m_pConsole->Register("rcon", "r[rcon-command]", CFGFLAG_CLIENT, Con_Rcon, this, "Send specified command to rcon");
	m_pConsole->Register("rcon_auth", "r[password]", CFGFLAG_CLIENT, Con_RconAuth, this, "Authenticate to rcon");
	m_pConsole->Register("rcon_login", "s[username] r[password]", CFGFLAG_CLIENT, Con_RconLogin, this, "Authenticate to rcon with a username");
	m_pConsole->Register("play", "r[file]", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Play, this, "Play back a demo");
	m_pConsole->Register("record", "?r[file]", CFGFLAG_CLIENT, Con_Record, this, "Start recording a demo");
	m_pConsole->Register("stoprecord", "", CFGFLAG_CLIENT, Con_StopRecord, this, "Stop recording a demo");
	m_pConsole->Register("add_demomarker", "", CFGFLAG_CLIENT, Con_AddDemoMarker, this, "Add demo timeline marker");
	m_pConsole->Register("begin_favorite_group", "", CFGFLAG_CLIENT, Con_BeginFavoriteGroup, this, "Use this before `add_favorite` to group favorites. End with `end_favorite_group`");
	m_pConsole->Register("end_favorite_group", "", CFGFLAG_CLIENT, Con_EndFavoriteGroup, this, "Use this after `add_favorite` to group favorites. Start with `begin_favorite_group`");
	m_pConsole->Register("add_favorite", "s[host|ip] ?s['allow_ping']", CFGFLAG_CLIENT, Con_AddFavorite, this, "Add a server as a favorite");
	m_pConsole->Register("remove_favorite", "r[host|ip]", CFGFLAG_CLIENT, Con_RemoveFavorite, this, "Remove a server from favorites");
	m_pConsole->Register("demo_slice_start", "", CFGFLAG_CLIENT, Con_DemoSliceBegin, this, "Mark the beginning of a demo cut");
	m_pConsole->Register("demo_slice_end", "", CFGFLAG_CLIENT, Con_DemoSliceEnd, this, "Mark the end of a demo cut");
	m_pConsole->Register("demo_play", "", CFGFLAG_CLIENT, Con_DemoPlay, this, "Play/pause the current demo");
	m_pConsole->Register("demo_speed", "f[speed]", CFGFLAG_CLIENT, Con_DemoSpeed, this, "Set current demo speed");

	m_pConsole->Register("save_replay", "?i[length] ?r[filename]", CFGFLAG_CLIENT, Con_SaveReplay, this, "Save a replay of the last defined amount of seconds");
	m_pConsole->Register("benchmark_quit", "i[seconds] r[file]", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_BenchmarkQuit, this, "Benchmark frame times for number of seconds to file, then quit");

	RustVersionRegister(*m_pConsole);

	m_pConsole->Chain("cl_timeout_seed", ConchainTimeoutSeed, this);
	m_pConsole->Chain("cl_replays", ConchainReplays, this);
	m_pConsole->Chain("cl_input_fifo", ConchainInputFifo, this);
	m_pConsole->Chain("cl_port", ConchainNetReset, this);
	m_pConsole->Chain("cl_dummy_port", ConchainNetReset, this);
	m_pConsole->Chain("cl_contact_port", ConchainNetReset, this);
	m_pConsole->Chain("bindaddr", ConchainNetReset, this);

	m_pConsole->Chain("password", ConchainPassword, this);

	// used for server browser update
	m_pConsole->Chain("br_filter_string", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_exclude_string", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_full", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_empty", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_spectators", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_friends", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_country", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_country_index", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_pw", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_gametype", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_gametype_strict", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_connecting_players", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_serveraddress", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_unfinished_map", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_login", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("add_favorite", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("remove_favorite", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("end_favorite_group", ConchainServerBrowserUpdate, this);

	m_pConsole->Chain("gfx_screen", ConchainWindowScreen, this);
	m_pConsole->Chain("gfx_screen_width", ConchainWindowResize, this);
	m_pConsole->Chain("gfx_screen_height", ConchainWindowResize, this);
	m_pConsole->Chain("gfx_screen_refresh_rate", ConchainWindowResize, this);
	m_pConsole->Chain("gfx_fullscreen", ConchainFullscreen, this);
	m_pConsole->Chain("gfx_borderless", ConchainWindowBordered, this);
	m_pConsole->Chain("gfx_vsync", ConchainWindowVSync, this);

	m_pConsole->Chain("loglevel", ConchainLoglevel, this);
	m_pConsole->Chain("stdout_output_level", ConchainStdoutOutputLevel, this);
}

static CClient *CreateClient()
{
	return new CClient;
}

void CClient::HandleConnectAddress(const NETADDR *pAddr)
{
	net_addr_str(pAddr, m_aCmdConnect, sizeof(m_aCmdConnect), true);
}

void CClient::HandleConnectLink(const char *pLink)
{
	// Chrome works fine with ddnet:// but not with ddnet:
	// Check ddnet:// before ddnet: because we don't want the // as part of connect command
	const char *pConnectLink = str_startswith(pLink, CONNECTLINK_DOUBLE_SLASH);
	if(pConnectLink)
	{
		str_copy(m_aCmdConnect, pConnectLink);
	}
	else
	{
		pConnectLink = str_startswith(pLink, CONNECTLINK_NO_SLASH);
		if(pConnectLink)
		{
			str_copy(m_aCmdConnect, pConnectLink);
		}
		else
		{
			str_copy(m_aCmdConnect, pLink);
		}
	}
	// Edge appends / to the URL
	const int Length = str_length(m_aCmdConnect);
	if(m_aCmdConnect[Length - 1] == '/')
		m_aCmdConnect[Length - 1] = '\0';
}

void CClient::HandleDemoPath(const char *pPath)
{
	str_copy(m_aCmdPlayDemo, pPath);
}

void CClient::HandleMapPath(const char *pPath)
{
	str_copy(m_aCmdEditMap, pPath);
}

static bool UnknownArgumentCallback(const char *pCommand, void *pUser)
{
	CClient *pClient = static_cast<CClient *>(pUser);
	if(str_startswith(pCommand, CONNECTLINK_NO_SLASH))
	{
		pClient->HandleConnectLink(pCommand);
		return true;
	}
	else if(str_endswith(pCommand, ".demo"))
	{
		pClient->HandleDemoPath(pCommand);
		return true;
	}
	else if(str_endswith(pCommand, ".map"))
	{
		pClient->HandleMapPath(pCommand);
		return true;
	}
	return false;
}

struct SSaveUnknownCommandContext
{
	IConfigManager *m_pConfigManager;
	ConfigDomain m_ConfigDomain;
};

static bool SaveUnknownDomainCommandCallback(const char *pCommand, void *pUser)
{
	SSaveUnknownCommandContext *pContext = static_cast<SSaveUnknownCommandContext *>(pUser);
	pContext->m_pConfigManager->StoreUnknownCommand(pCommand, pContext->m_ConfigDomain);
	return true;
}

static const char *GetConfigLoadPath(IStorage *pStorage, const CConfigDomain &ConfigDomain)
{
	if(ConfigDomain.m_aConfigPath == nullptr)
		return nullptr;
	if(pStorage->FileExists(ConfigDomain.m_aConfigPath, IStorage::TYPE_ALL))
		return ConfigDomain.m_aConfigPath;
	if(ConfigDomain.m_aPreviousConfigPath != nullptr && pStorage->FileExists(ConfigDomain.m_aPreviousConfigPath, IStorage::TYPE_ALL))
		return ConfigDomain.m_aPreviousConfigPath;
	if(ConfigDomain.m_aLegacyConfigPath != nullptr && pStorage->FileExists(ConfigDomain.m_aLegacyConfigPath, IStorage::TYPE_ALL))
		return ConfigDomain.m_aLegacyConfigPath;
	return nullptr;
}

/*
	Server Time
	Client Mirror Time
	Client Predicted Time

	Snapshot Latency
		Downstream latency

	Prediction Latency
		Upstream latency
*/

#if defined(CONF_PLATFORM_MACOS)
extern "C" int TWMain(int argc, const char **argv)
#elif defined(CONF_PLATFORM_ANDROID)
static int gs_AndroidStarted = false;
extern "C" [[gnu::visibility("default")]] int SDL_main(int argc, char *argv[]);
int SDL_main(int argc, char *argv2[])
#elif defined(CONF_PLATFORM_IOS)
extern "C" int SDL_main(int argc, char *argv[]);
int SDL_main(int argc, char *argv2[])
#else
int main(int argc, const char **argv)
#endif
{
	const int64_t MainStart = time_get();

#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	const char **argv = const_cast<const char **>(argv2);
#endif
#if defined(CONF_PLATFORM_ANDROID)
	// Android might not unload the library from memory, causing globals like gs_AndroidStarted
	// not to be initialized correctly when starting the app again.
	if(gs_AndroidStarted)
	{
		ShowMessageBoxWithoutGraphics({.m_pTitle = "Android Error", .m_pMessage = "The app was started, but not closed properly, this causes bugs. Please restart or manually close this task."});
		std::exit(0);
	}
	gs_AndroidStarted = true;
#elif defined(CONF_FAMILY_WINDOWS)
	CWindowsComLifecycle WindowsComLifecycle(true);
#endif
	CCmdlineFix CmdlineFix(&argc, &argv);

	// QmClient: 测试专用注入开关，供 qmclient_scripts/integration 的进程级回归测试
	// 在真实客户端里触发主线程断言。正常运行不会传入该参数。
	for(int i = 1; i < argc; ++i)
	{
		if(str_comp(argv[i], "--qm-test-main-thread-assert") == 0)
			gs_QmTestMainThreadAssert = true;
		if(str_comp(argv[i], "--qm-test-main-thread-stall") == 0)
			gs_QmTestMainThreadStall = true;
	}

#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; ++i)
	{
		if(str_comp(argv[i], "--qm-preview-launch-crash-reporter") == 0)
		{
			if(i + 1 >= argc || !IsQmCrashReporterPath(argv[i + 1]) || !crashdump_launch_reporter_if_available(argv[i + 1]))
				return 2;
			return 0;
		}
		if(str_comp(argv[i], "--qm-crash-reporter") == 0)
		{
			if(i + 1 >= argc || !ShowQmCrashReporterDialog(argv[i + 1]))
				return 2;
			return 0;
		}
		if(str_comp(argv[i], "--qm-preview-crash-dialog") == 0)
		{
			// 预览也覆盖“退出清理已完成后弹出报告”的真实时序，
			// 确保过期的看门狗不会在 10 秒后关掉用户正在阅读的窗口。
			StartForcedExitWatchdog();
			StopForcedExitWatchdog();
			const char *pPreviewType = i + 1 < argc ? argv[i + 1] : "graphics";
			const char *pPreviewTitle = "Graphics Error";
			const char *pPreviewMessage =
				"Submitting to graphics queue failed.\n"
				"device lost\n"
				"Submitting the render commands failed. Try to update your GPU drivers.\n\n"
				"For detailed troubleshooting instructions please read our Wiki:\n"
				"https://wiki.ddnet.org/wiki/GFX_Troubleshooting\n\n"
				"Platform: win64 (little endian)\n"
				"Configuration: development preview\n"
				"Game version: QmClient development build\n"
				"OS version: Windows\n\n"
				"Configured graphics backend: Vulkan 1.4.0\n"
				"GPU: Preview GPU - 4K / high-DPI layout test\n"
				"Texture: 101.34 MiB, Buffer: 24.38 MiB, Streamed: 20.50 MiB, Staging: 48.00 MiB";
			if(str_comp_nocase(pPreviewType, "assertion") == 0)
			{
				pPreviewTitle = "Assertion Error";
				pPreviewMessage =
					"An assertion error occurred. Please take a screenshot and report this error.\n"
					"Please also share the assert log and crash log found in the 'dumps/QmClient_Crash' folder in your config directory.\n\n"
					"menus.cpp(2048): popup state must have an active owner\n\n"
					"Platform: win64 (little endian)\n"
					"Configuration: development preview\n"
					"Game version: QmClient development build\n"
					"OS version: Windows";
			}
			else if(str_comp_nocase(pPreviewType, "fatal") == 0)
			{
				pPreviewTitle = "QmClient Crash Report";
				pPreviewMessage =
					"QmClient fatal error report\n"
					"Report type: fatal-crash\n"
					"Timestamp: 2026-09-10 20:26:00.000\n"
					"Reason: Unhandled structured exception\n"
					"Process ID: 12345\n"
					"Thread ID: 67890\n"
					"Exception code: 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)\n"
					"Exception address: 0x00007FF612345678\n"
					"Exception module: DDNet.exe + 0x123456\n"
					"Access violation operation: read\n"
					"Access violation target address: 0x0000000000000000\n"
					"Fallback minidump written: yes";
			}
			else if(str_comp_nocase(pPreviewType, "hang") == 0)
			{
				pPreviewTitle = "QmClient Hang Report";
				pPreviewMessage =
					"QmClient hang diagnostic report\n"
					"Report type: hang\n"
					"Timestamp: 2026-09-10_20-26-00\n"
					"Process ID: 12345\n"
					"Hang timeout threshold: 10 seconds\n"
					"No heartbeat duration: 12.4 seconds\n"
					"Client state: online (3)\n"
					"Current map: Tutorial\n"
					"Server address: 127.0.0.1:8303\n"
					"OS version: Windows\n"
					"Game version: QmClient development build\n"
					"Report directory: dumps/QmClient_Crash";
			}
			std::vector<IGraphics::CMessageBoxButton> vPreviewButtons;
			if(str_comp_nocase(pPreviewType, "graphics") == 0)
				vPreviewButtons.push_back({.m_pLabel = "Show Wiki"});
			vPreviewButtons.push_back({.m_pLabel = "Show crash reports"});
			vPreviewButtons.push_back({.m_pLabel = "Close report", .m_Confirm = true, .m_Cancel = true});
			ShowMessageBoxWithoutGraphics({
				.m_pTitle = pPreviewTitle,
				.m_pMessage = pPreviewMessage,
				.m_Style = IGraphics::EMessageBoxStyle::QM_FESTIVE,
				.m_vButtons = vPreviewButtons,
			});
			return 0;
		}
	}
#endif

	std::vector<std::shared_ptr<ILogger>> vpLoggers;
	std::shared_ptr<ILogger> pStdoutLogger = nullptr;
#if defined(CONF_PLATFORM_ANDROID)
	pStdoutLogger = std::shared_ptr<ILogger>(log_logger_android());
#else
	bool Silent = false;
	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0)
		{
			Silent = true;
		}
	}
	if(!Silent)
	{
		pStdoutLogger = std::shared_ptr<ILogger>(log_logger_stdout());
	}
#endif
	std::shared_ptr<CFutureLogger> pFuturePerfFileLogger = std::make_shared<CFutureLogger>();
	if(pStdoutLogger)
	{
		vpLoggers.push_back(pStdoutLogger);
	}
	std::shared_ptr<CFutureLogger> pFutureFileLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureFileLogger);
	std::shared_ptr<CFutureLogger> pFutureConsoleLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureConsoleLogger);
	std::shared_ptr<CFutureLogger> pFutureAssertionLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureAssertionLogger);
	std::shared_ptr<ILogger> pFallbackLogger(log_logger_collection(std::move(vpLoggers)).release());
	log_set_global_logger(log_logger_prefix_router(pFuturePerfFileLogger, pFallbackLogger, "perf/").release());

#if defined(CONF_PLATFORM_ANDROID)
	// Initialize Android after logger is available
	const char *pAndroidInitError = InitAndroid();
	if(pAndroidInitError != nullptr)
	{
		log_error("android", "%s", pAndroidInitError);
		ShowMessageBoxWithoutGraphics({.m_pTitle = "Android Error", .m_pMessage = pAndroidInitError});
		std::exit(0);
	}
#elif defined(CONF_PLATFORM_IOS)
	const char *pIosInitError = InitIos();
	if(pIosInitError != nullptr)
	{
		log_error("ios", "%s", pIosInitError);
		ShowMessageBoxWithoutGraphics({.m_pTitle = "iOS Error", .m_pMessage = pIosInitError});
		std::exit(0);
	}
#endif

	std::stack<std::function<void()>> CleanerFunctions;
	std::function<void()> PerformCleanup = [&CleanerFunctions]() mutable {
		while(!CleanerFunctions.empty())
		{
			CleanerFunctions.top()();
			CleanerFunctions.pop();
		}
	};
	std::function<void()> PerformFinalCleanup = []() {
#if defined(CONF_PLATFORM_ANDROID)
		// Forcefully terminate the entire process, to ensure that static variables
		// will be initialized correctly when the app is started again after quitting.
		// Returning from the main function is not enough, as this only results in the
		// native thread terminating, but the Java thread will continue. Java does not
		// support unloading libraries once they have been loaded, so all static
		// variables will not have their expected initial values anymore when the app
		// is started again after quitting. The variable gs_AndroidStarted above is
		// used to check that static variables have been initialized properly.
		// TODO: This is not the correct way to close an activity on Android, as it
		//       ignores the activity lifecycle entirely, which may cause issues if
		//       we ever used any global resources like the camera.
		std::exit(0);
#elif defined(CONF_PLATFORM_EMSCRIPTEN)
		// We cannot use atexit with Emscripten so we finish the global logger here.
		// See comment in the log_set_global_logger function for details.
		log_global_logger_finish();
#endif
	};
	std::function<void()> PerformAllCleanup = [PerformCleanup, PerformFinalCleanup]() mutable {
		PerformCleanup();
		PerformFinalCleanup();
	};

	// Register SDL for cleanup before creating the kernel and client,
	// so SDL is shutdown after kernel and client. Otherwise the client
	// may crash when shutting down after SDL is already shutdown.
	CleanerFunctions.emplace([]() { SDL_Quit(); });

	CClient *pClient = CreateClient();
	pClient->SetLoggers(pFutureFileLogger, std::move(pStdoutLogger), pFuturePerfFileLogger);

	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pClient, false);
	pClient->RegisterInterfaces();
	CleanerFunctions.emplace([pKernel, pClient]() {
		// Ensure that the assert handler doesn't use the client/graphics after they've been destroyed
		dbg_assert_set_handler(nullptr);
		pKernel->Shutdown();
		delete pKernel;
		delete pClient;
	});

	const std::thread::id MainThreadId = std::this_thread::get_id();
	dbg_assert_set_handler([MainThreadId, pClient](const char *pMsg) {
		if(MainThreadId != std::this_thread::get_id())
			return;

		const char *pGraphicsError = pClient->Graphics() == nullptr ? "" : pClient->Graphics()->GetFatalError();
		const bool GotGraphicsError = pGraphicsError[0] != '\0';
		const char *pTitle;
		const char *pPreamble;
		const char *pPostamble;
		if(GotGraphicsError)
		{
			pTitle = "Graphics Error";
			pPreamble =
				"A graphics error occurred. Please see details and instructions below.\n\n";
			pPostamble =
				"For detailed troubleshooting instructions please read our Wiki:\n"
				"https://wiki.ddnet.org/wiki/GFX_Troubleshooting\n\n"
				"If this did not resolve the issue, please take a screenshot and report this error.\n"
				"Please also share the assert log"
#if defined(CONF_CRASHDUMP)
				" and crash log"
#endif
				" found in the 'dumps/QmClient_Crash' folder in your config directory.\n\n";
			// This is more human readable and we don't care about the source location here,
			// because all graphics assertions come from CGraphicsBackend_Threaded::ProcessError
			// and the original message is also logged separately by the assertion system.
			pMsg = pGraphicsError;
		}
		else
		{
			pTitle = "Assertion Error";
			pPreamble =
				"An assertion error occurred. Please take a screenshot and report this error.\n"
				"Please also share the assert log"
#if defined(CONF_CRASHDUMP)
				" and crash log"
#endif
				" found in the 'dumps/QmClient_Crash' folder in your config directory.\n\n";
			pPostamble = "";
		}

		char aOsVersionString[128];
		if(!os_version_str(aOsVersionString, sizeof(aOsVersionString)))
		{
			str_copy(aOsVersionString, "unknown");
		}

		char aGpuInfo[512];
		pClient->GetGpuInfoString(aGpuInfo);

		char aMessage[2048];
		str_format(aMessage, sizeof(aMessage),
			"%s"
			"%s\n\n"
			"%s"
			"Platform: %s (%s)\n"
			"Configuration: base"
#if defined(CONF_AUTOUPDATE)
			" + autoupdate"
#endif
#if defined(CONF_CRASHDUMP)
			" + crashdump"
#endif
#if defined(CONF_DEBUG)
			" + debug"
#endif
#if defined(CONF_DISCORD)
			" + discord"
#endif
#if defined(CONF_VIDEORECORDER)
			" + videorecorder"
#endif
#if defined(CONF_WEBSOCKETS)
			" + websockets"
#endif
			"\n"
			"Game version: %s %s %s\n"
			"OS version: %s\n\n"
			"%s", // GPU info
			pPreamble,
			pMsg,
			pPostamble,
			CONF_PLATFORM_STRING, CONF_ARCH_ENDIAN_STRING,
			GAME_NAME, GAME_RELEASE_VERSION, GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "",
			aOsVersionString,
			aGpuInfo);
		// Also log all of this information to the assertion log file
		log_error("assertion", "%s", aMessage);
		std::vector<IGraphics::CMessageBoxButton> vButtons;
		if(GotGraphicsError)
		{
			vButtons.push_back({.m_pLabel = "Show Wiki"});
		}
		// Storage may not have been initialized yet and viewing files is not supported on Android yet
#if !defined(CONF_PLATFORM_ANDROID)
		if(pClient->Storage() != nullptr)
		{
			vButtons.push_back({.m_pLabel = "Show crash reports"});
		}
#endif
		vButtons.push_back({.m_pLabel = "OK", .m_Confirm = true, .m_Cancel = true});
		const std::optional<int> MessageResult = pClient->ShowMessageBox({
			.m_pTitle = pTitle,
			.m_pMessage = aMessage,
			.m_Style = IGraphics::EMessageBoxStyle::QM_FESTIVE,
			.m_vButtons = vButtons,
		});
		if(MessageResult.has_value())
			crashdump_suppress_reporter_once();
		if(GotGraphicsError && MessageResult && *MessageResult == 0)
		{
			pClient->ViewLink("https://wiki.ddnet.org/wiki/GFX_Troubleshooting");
		}
#if !defined(CONF_PLATFORM_ANDROID)
		if(pClient->Storage() != nullptr && MessageResult && *MessageResult == (GotGraphicsError ? 1 : 0))
		{
			char aDumpsPath[IO_MAX_PATH_LENGTH];
			pClient->Storage()->GetCompletePath(IStorage::TYPE_SAVE, gs_pQmCrashDumpDir, aDumpsPath, sizeof(aDumpsPath));
			pClient->ViewFile(aDumpsPath);
		}
#endif
		// Client will crash due to assertion, don't call PerformAllCleanup in this inconsistent state
	});

	// create the components
	IEngine *pEngine = CreateEngine(GAME_NAME, pFutureConsoleLogger);
	pKernel->RegisterInterface(pEngine, false);
	CleanerFunctions.emplace([pEngine]() {
		// Engine has to be destroyed before the graphics so that skin download thread can finish
		delete pEngine;
	});

	IStorage *pStorage;
	{
		CMemoryLogger MemoryLogger;
		MemoryLogger.SetParent(log_get_scope_logger());
		{
			CLogScope LogScope(&MemoryLogger);
			pStorage = CreateStorage(IStorage::EInitializationType::CLIENT, argc, argv);
		}
		if(!pStorage)
		{
			log_error("client", "Failed to initialize the storage location (see details above)");
			std::string Message = std::string("Failed to initialize the storage location. See details below.\n\n") + MemoryLogger.ConcatenatedLines();
			pClient->ShowMessageBox({.m_pTitle = "Storage Error", .m_pMessage = Message.c_str()});
			PerformAllCleanup();
			return -1;
		}
	}
	pKernel->RegisterInterface(pStorage);

	pFutureAssertionLogger->Set(CreateAssertionLogger(pStorage, GAME_NAME));

	{
		char aBufPath[IO_MAX_PATH_LENGTH];
		char aBufName[IO_MAX_PATH_LENGTH];
		char aDate[64];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aBufName, sizeof(aBufName), "%s/" GAME_NAME "_%s_crash_log_%s_%d_%s.RTP", gs_pQmCrashDumpDir, CONF_PLATFORM_STRING, aDate, pid(), GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, aBufName, aBufPath, sizeof(aBufPath));
		fs_makedir_rec_for(aBufPath);
		crashdump_init_if_available(aBufPath);
	}

#if defined(CONF_FAMILY_WINDOWS)
	ShowPendingQmCrashReport(pStorage);
#endif

	IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT).release();
	pKernel->RegisterInterface(pConsole);

	IConfigManager *pConfigManager = CreateConfigManager();
	pKernel->RegisterInterface(pConfigManager);

	IEngineSound *pEngineSound = CreateEngineSound();
	pKernel->RegisterInterface(pEngineSound); // IEngineSound
	pKernel->RegisterInterface(static_cast<ISound *>(pEngineSound), false);

	IEngineInput *pEngineInput = CreateEngineInput();
	pKernel->RegisterInterface(pEngineInput); // IEngineInput
	pKernel->RegisterInterface(static_cast<IInput *>(pEngineInput), false);

	IEngineTextRender *pEngineTextRender = CreateEngineTextRender();
	pKernel->RegisterInterface(pEngineTextRender); // IEngineTextRender
	pKernel->RegisterInterface(static_cast<ITextRender *>(pEngineTextRender), false);

	IEngineHttp *pEngineHttp = CreateEngineHttp();
	pKernel->RegisterInterface(pEngineHttp); // IEngineHttp
	pKernel->RegisterInterface(static_cast<IHttp *>(pEngineHttp), false);

	IFrameScheduler *pFrameScheduler = CreateFrameScheduler();
	pKernel->RegisterInterface(pFrameScheduler);

	IEngineMap *pEngineMap = CreateEngineMap();
	pKernel->RegisterInterface(pEngineMap); // IEngineMap
	pKernel->RegisterInterface(static_cast<IMap *>(pEngineMap), false);

	IDiscord *pDiscord = CreateDiscord();
	pKernel->RegisterInterface(pDiscord);

	INotifications *pNotifications = CreateNotifications();
	pKernel->RegisterInterface(pNotifications);

	pKernel->RegisterInterface(CreateEditor(), false);
	pKernel->RegisterInterface(CreateFavorites().release());
	pKernel->RegisterInterface(CreateGameClient());

	pEngine->Init();
	pConsole->Init();
	pConfigManager->Init();
	pNotifications->Init(GAME_NAME " Client");

	// register all console commands
	pClient->RegisterCommands();

	pKernel->RequestInterface<IGameClient>()->OnConsoleInit();
	pClient->InitConfigCommands();

	// execute config file
	bool LoadedClientConfig = false;
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		std::vector<const char *> vConfigPaths;
		if(ConfigDomain == ConfigDomain::QMCLIENT)
		{
			// 变量域（v3 合并）：优先 qmclient/settings.cfg，否则按 v2 → v1 → v0 回退读取旧文件
			QmGetVariableConfigLoadPaths(pStorage, vConfigPaths);
		}
		else if(const char *pConfigPath = GetConfigLoadPath(pStorage, s_aConfigDomains[ConfigDomain]); pConfigPath != nullptr)
		{
			vConfigPaths.push_back(pConfigPath);
		}
		for(const char *pConfigPath : vConfigPaths)
		{
			LoadedClientConfig = true;
			if(s_aConfigDomains[ConfigDomain].m_aPreviousConfigPath != nullptr && str_comp(pConfigPath, s_aConfigDomains[ConfigDomain].m_aPreviousConfigPath) == 0)
				gs_aLoadedPreviousConfigPath[ConfigDomain] = true;

			SSaveUnknownCommandContext UnknownCommandContext{pConfigManager, ConfigDomain};
			pConsole->SetUnknownCommandCallback(SaveUnknownDomainCommandCallback, &UnknownCommandContext);
			if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))
			{
				pConsole->SetUnknownCommandCallback(IConsole::EmptyUnknownCommandCallback, nullptr);
				char aError[2048];
				str_format(aError, sizeof(aError), "Failed to load config from '%s'.", pConfigPath);
				log_error("client", "%s", aError);
				pClient->ShowMessageBox({.m_pTitle = "Config File Error", .m_pMessage = aError});
				PerformAllCleanup();
				return -1;
			}
			pConsole->SetUnknownCommandCallback(IConsole::EmptyUnknownCommandCallback, nullptr);
		}
	}

	if(pStorage->FileExists(AUTOEXEC_CLIENT_FILE, IStorage::TYPE_ALL))
	{
		pConsole->ExecuteFile(AUTOEXEC_CLIENT_FILE, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else // fallback
	{
		pConsole->ExecuteFile(AUTOEXEC_FILE, IConsole::CLIENT_ID_UNSPECIFIED);
	}

	if(g_Config.m_ClConfigVersion < 1)
	{
		if(g_Config.m_ClAntiPing == 0)
		{
			g_Config.m_ClAntiPingPlayers = 1;
			g_Config.m_ClAntiPingGrenade = 1;
			g_Config.m_ClAntiPingWeapons = 1;
		}
	}
	// 在清理旧遗留开关前记录其真实模式，供后续语义开关迁移使用。
	const bool WasLegacyCollisionHitbox = g_Config.m_QmShowCollisionHitbox != 0 && g_Config.m_QmHitboxMode == 0;
	const bool WasHitboxMode = g_Config.m_QmHitboxMode != 0;
	if(g_Config.m_ClConfigVersion < 3)
	{
		if(g_Config.m_QmShowCollisionHitbox && !g_Config.m_QmHitboxMode)
			g_Config.m_QmHitboxMode = 1;
		g_Config.m_QmHitboxAlpha = g_Config.m_QmCollisionHitboxAlpha;
		g_Config.m_QmHitboxColorFreeze = g_Config.m_QmCollisionHitboxColorFreeze;
		g_Config.m_QmShowCollisionHitbox = 0;
	}
	if(g_Config.m_ClConfigVersion < 4)
	{
		if(WasLegacyCollisionHitbox)
		{
			// 遗留模式不显示 Tee 圆和武器，只显示地图危险边界、探针及拾取物。
			g_Config.m_QmHitboxShowMap = 1;
			g_Config.m_QmHitboxShowTeeCollision = 0;
			g_Config.m_QmHitboxShowTeeFreeze = 1;
			g_Config.m_QmHitboxShowTeeDeath = 1;
			g_Config.m_QmHitboxShowPickups = 1;
			g_Config.m_QmHitboxShowHammer = 0;
			g_Config.m_QmHitboxShowProjectiles = 0;
			g_Config.m_QmHitboxShowLasers = 0;
			g_Config.m_QmHitboxShowFreezeLasers = 0;
			g_Config.m_QmHitboxShowHook = 0;
		}
		else if(WasHitboxMode || g_Config.m_QmHitboxShowMap || g_Config.m_QmHitboxShowTees || g_Config.m_QmHitboxShowPickups || g_Config.m_QmHitboxShowWeapons)
		{
			g_Config.m_QmHitboxShowTeeCollision = g_Config.m_QmHitboxShowTees;
			g_Config.m_QmHitboxShowTeeFreeze = g_Config.m_QmHitboxShowTees;
			g_Config.m_QmHitboxShowTeeDeath = g_Config.m_QmHitboxShowTees;
			g_Config.m_QmHitboxShowHammer = g_Config.m_QmHitboxShowWeapons;
			g_Config.m_QmHitboxShowProjectiles = g_Config.m_QmHitboxShowWeapons;
			g_Config.m_QmHitboxShowLasers = g_Config.m_QmHitboxShowWeapons;
			g_Config.m_QmHitboxShowFreezeLasers = g_Config.m_QmHitboxShowWeapons;
			g_Config.m_QmHitboxShowHook = g_Config.m_QmHitboxShowWeapons;
		}
	}
	if(LoadedClientConfig && g_Config.m_ClConfigVersion < 5)
	{
		// qm_graphics_mode 是新版新增配置。旧配置没有该字段时，按原 gfx_backend
		// 推导模式，避免首次启动新版时把用户手动选择的 OpenGL/GLES 改成现代后端。
		const char *pPerformanceBackend = graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_PERFORMANCE);
		g_Config.m_QmGraphicsMode = str_comp_nocase(g_Config.m_GfxBackend, pPerformanceBackend) == 0 ? graphics_backend::GRAPHICS_MODE_PERFORMANCE : graphics_backend::GRAPHICS_MODE_COMPATIBILITY;
	}
	g_Config.m_ClConfigVersion = 5;

	if(!RecoverQmGraphicsSettingsAfterDriverCrash(pStorage))
	{
		log_error("client", "graphics recovery has no available backend; change gfx_backend manually before restarting");
		PerformAllCleanup();
		return -1;
	}

	// parse the command line arguments
	pConsole->SetUnknownCommandCallback(UnknownArgumentCallback, pClient);
	pConsole->ParseArguments(argc - 1, &argv[1]);
	pConsole->SetUnknownCommandCallback(IConsole::EmptyUnknownCommandCallback, nullptr);

	// 仅打开 Steam 主窗口，客户端仍由当前进程继续启动。
	if(g_Config.m_QmSteamAutoLaunch)
		SteamOpenClient();

	ISteam *pSteam = CreateSteam();
	pKernel->RegisterInterface(pSteam);
	pClient->InitInterfaces();

	if(pSteam->GetConnectAddress())
	{
		pClient->HandleConnectAddress(pSteam->GetConnectAddress());
		pSteam->ClearConnectAddress();
	}

	if(g_Config.m_Logfile[0])
	{
		const int Mode = g_Config.m_Logappend ? IOFLAG_APPEND : IOFLAG_WRITE;
		IOHANDLE Logfile = pStorage->OpenFile(g_Config.m_Logfile, Mode, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		if(Logfile)
		{
			auto pFileLogger = log_logger_file(Logfile);
			pFileLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_Loglevel)});
			pFutureFileLogger->Set(std::move(pFileLogger));
		}
		else
		{
			log_error("client", "failed to open '%s' for logging", g_Config.m_Logfile);
			pFutureFileLogger->Set(log_logger_noop());
		}
	}
	else
	{
		pFutureFileLogger->Set(log_logger_noop());
	}

	// 性能日志文件：CFutureLogger 只能 Set 一次，启动时固定到可切换包装；
	// 游戏内 qm_perf_debug / qm_perf_logfile / qm_perf_stutter_diagnostics 任一
	// 变化由 CClient::UpdateQmPerfFileLogger 按帧检测，立即打开/关闭文件。
	std::shared_ptr<CQmPerfFileSwitchLogger> pQmPerfFileSwitchLogger = std::make_shared<CQmPerfFileSwitchLogger>();
	pFuturePerfFileLogger->Set(pQmPerfFileSwitchLogger);
	pClient->SetQmPerfFileSwitch(std::move(pQmPerfFileSwitchLogger));
	pClient->UpdateQmPerfFileLogger();
	// QmClient: 仅开启 macOS 自动诊断（未配置性能日志）时预创建诊断目录，
	// 目录按需写入方依赖启动期存在。
	if(g_Config.m_QmMacosGraphicsDiagnostics != 0 && !g_Config.m_QmPerfLogfile && !g_Config.m_QmPerfDebug && !g_Config.m_QmPerfStutterDiagnostics && g_Config.m_QmGraphicsTrace == 0)
	{
		pStorage->CreateFolder("dumps", IStorage::TYPE_SAVE);
		pStorage->CreateFolder("dumps/QmClient_AutoDiagnostics", IStorage::TYPE_SAVE);
	}

	// Register protocol and file extensions
#if defined(CONF_FAMILY_WINDOWS)
	pClient->ShellRegister();
#endif

	// Do not automatically translate touch events to mouse events and vice versa.
	SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "0");
	SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS", "0");

	// Support longer IME composition strings (enables SDL_TEXTEDITING_EXT).
#if SDL_VERSION_ATLEAST(2, 0, 22)
	SDL_SetHint(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, "1");
#endif

#if defined(CONF_PLATFORM_MACOS)
	// Hints will not be set if there is an existing override hint or environment variable that takes precedence.
	// So this respects cli environment overrides.
	SDL_SetHint("SDL_MAC_OPENGL_ASYNC_DISPATCH", "1");
#endif

#if defined(CONF_FAMILY_WINDOWS)
	SDL_SetHint("SDL_IME_SHOW_UI", g_Config.m_InpImeNativeUi ? "1" : "0");
	SDL_SetHint("SDL_QM_IME_SKIP_UILESS_UIELEMENT_PROCESSING", g_Config.m_InpImeNativeUi ? "0" : "1");
#else
	SDL_SetHint("SDL_IME_SHOW_UI", "1");
#endif

#if defined(CONF_PLATFORM_ANDROID)
	// Trap the Android back button so it can be handled in our code reliably
	// instead of letting the system handle it.
	SDL_SetHint("SDL_ANDROID_TRAP_BACK_BUTTON", "1");
	// Force landscape screen orientation.
	SDL_SetHint("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight");
#endif
#if defined(CONF_PLATFORM_IOS)
	SDL_SetHint("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight");
#endif

	// init SDL
	if(SDL_Init(0) < 0)
	{
		char aError[256];
		str_format(aError, sizeof(aError), "Unable to initialize SDL base: %s", SDL_GetError());
		log_error("client", "%s", aError);
		pClient->ShowMessageBox({.m_pTitle = "SDL Error", .m_pMessage = aError});
		PerformAllCleanup();
		return -1;
	}

	// SDL raises the timer resolution on Windows while initializing, the other platforms need this.
	thread_request_precise_wakeups();

	// run the client
	log_trace("client", "initialization finished after %.2fms, starting...", (time_get() - MainStart) * 1000.0f / (float)time_freq());
	pClient->Run();

	const bool Restarting = pClient->State() == CClient::STATE_RESTARTING;
#if !defined(CONF_PLATFORM_ANDROID)
	char aRestartBinaryPath[IO_MAX_PATH_LENGTH];
	if(Restarting)
	{
		pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aRestartBinaryPath, sizeof(aRestartBinaryPath));
	}
#endif

	std::vector<SWarning> vQuittingWarnings = pClient->QuittingWarnings();

	// QmClient: 从这里开始销毁图形后端。个别图形驱动（NVIDIA nvoglv64.dll 等）
	// 会在销毁设备/上下文时访问已释放内存。进程此时本来就要结束，这类退出期故障
	// 不再弹窗或写完整转储，只留一条日志；标记在清理前后各置一次以限定作用范围。
	crashdump_mark_shutdown_begin(nullptr);

	PerformCleanup();

	crashdump_mark_shutdown_end();

	for(const SWarning &Warning : vQuittingWarnings)
	{
		ShowMessageBoxWithoutGraphics({.m_pTitle = Warning.m_aWarningTitle, .m_pMessage = Warning.m_aWarningMsg});
	}

	if(Restarting)
	{
#if defined(CONF_PLATFORM_ANDROID)
		RestartAndroidApp();
#else
		shell_execute(aRestartBinaryPath, EShellExecuteWindowState::FOREGROUND);
#endif
	}

	PerformFinalCleanup();

	return 0;
}

// DDRace

const char *CClient::GetCurrentMap() const
{
	return m_aCurrentMap;
}

const char *CClient::GetCurrentMapPath() const
{
	return m_aCurrentMapPath;
}

void CClient::RaceRecord_Start(const char *pFilename)
{
	dbg_assert(State() == IClient::STATE_ONLINE, "Client must be online to record demo");
	dbg_assert(m_pMap && m_pMap->IsLoaded(), "Map must be loaded to record demo");

	DemoRecorders()[RECORDER_RACE].Start(
		Storage(),
		m_pConsole,
		pFilename,
		IsSixup() ? GameClient()->NetVersion7() : GameClient()->NetVersion(),
		m_aCurrentMap,
		m_pMap->Sha256(),
		m_pMap->Crc(),
		"client",
		m_pMap->Size(),
		nullptr,
		m_pMap->File(),
		nullptr,
		nullptr);
}

void CClient::RaceRecord_Stop()
{
	if(DemoRecorder(RECORDER_RACE)->IsRecording())
	{
		DemoRecorder(RECORDER_RACE)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
	}
}

bool CClient::RaceRecord_IsRecording()
{
	return DemoRecorder(RECORDER_RACE)->IsRecording();
}

EDemoMarkerResult CClient::AddDemoMarker()
{
	bool Added = false;
	for(auto &DemoRecorder : DemoRecorders())
	{
		if(DemoRecorder.IsRecording())
			Added |= DemoRecorder.AddDemoMarker();
	}
	return Added ? EDemoMarkerResult::ADDED : EDemoMarkerResult::NONE;
}

void CClient::RequestDDNetInfo()
{
	if(m_pDDNetInfoTask && !m_pDDNetInfoTask->Done())
		return;

	char aUrl[256];
	str_copy(aUrl, DDNET_INFO_URL);

	if(g_Config.m_BrIndicateFinished)
	{
		char aEscaped[128];
		EscapeUrl(aEscaped, PlayerName());
		str_append(aUrl, "?name=");
		str_append(aUrl, aEscaped);
	}

	m_pDDNetInfoTask = HttpGetFile(aUrl, Storage(), DDNET_INFO_FILE, IStorage::TYPE_SAVE);
	m_pDDNetInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pDDNetInfoTask->SkipByFileTime(false); // Always re-download.
	// Use ipv4 so we can know the ingame ip addresses of players before they join game servers
	m_pDDNetInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pDDNetInfoTask);
	m_InfoState = EInfoState::LOADING;
}

int CClient::GetPredictionTime()
{
	int64_t Now = time_get();
	return (int)((m_PredictedTime.Get(Now) - m_aGameTime[g_Config.m_ClDummy].Get(Now)) * 1000 / (float)time_freq());
}

int CClient::GetPredictionTick()
{
	int PredictionTick = GetPredictionTime() * GameTickSpeed() / 1000.0f;

	int PredictionMin = g_Config.m_ClAntiPingLimit * GameTickSpeed() / 1000.0f;

	if(g_Config.m_ClAntiPingLimit == 0)
	{
		float PredictionPercentage = 1 - g_Config.m_ClAntiPingPercent / 100.0f;
		PredictionMin = std::floor(PredictionTick * PredictionPercentage);
	}

	if(PredictionMin > PredictionTick - 1)
	{
		PredictionMin = PredictionTick - 1;
	}

	if(PredictionMin <= 0)
		return PredGameTick(g_Config.m_ClDummy);

	PredictionTick = PredGameTick(g_Config.m_ClDummy) - PredictionMin;

	if(PredictionTick < GameTick(g_Config.m_ClDummy) + 1)
	{
		PredictionTick = GameTick(g_Config.m_ClDummy) + 1;
	}
	return PredictionTick;
}

IClient::EPredictionMarginState CClient::PredictionMarginState() const
{
	return m_PredictionMarginState;
}

void CClient::GetSmoothTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount)
{
	int64_t GameTime = m_aGameTime[g_Config.m_ClDummy].Get(time_get());
	int64_t PredTime = m_PredictedTime.Get(time_get());
	int64_t SmoothTime = std::clamp(GameTime + (int64_t)(MixAmount * (PredTime - GameTime)), GameTime, PredTime);

	*pSmoothTick = (int)(SmoothTime * GameTickSpeed() / time_freq()) + 1;
	*pSmoothIntraTick = (SmoothTime - (*pSmoothTick - 1) * time_freq() / GameTickSpeed()) / (float)(time_freq() / GameTickSpeed());
}

void CClient::GetSmoothFreezeTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount)
{
	int64_t GameTime = m_aGameTime[g_Config.m_ClDummy].Get(time_get());
	int64_t PredTime = m_PredictedTime.Get(time_get());
	GameTime = std::min(GameTime, PredTime);

	int64_t UpperPredTime = std::clamp(PredTime - (time_freq() / 50) * g_Config.m_TcUnfreezeLagTicks, GameTime, PredTime);
	int64_t LowestPredTime = std::clamp(PredTime, GameTime, UpperPredTime);
	int64_t SmoothTime = std::clamp(LowestPredTime + (int64_t)(MixAmount * (PredTime - LowestPredTime)), LowestPredTime, PredTime);

	*pSmoothTick = (int)(SmoothTime * 50 / time_freq()) + 1;
	*pSmoothIntraTick = (SmoothTime - (*pSmoothTick - 1) * time_freq() / 50) / (float)(time_freq() / 50);
}

void CClient::AddWarning(const SWarning &Warning)
{
	const std::unique_lock<std::mutex> Lock(m_WarningsMutex);
	m_vWarnings.emplace_back(Warning);
}

std::optional<SWarning> CClient::CurrentWarning()
{
	const std::unique_lock<std::mutex> Lock(m_WarningsMutex);
	if(m_vWarnings.empty())
	{
		return std::nullopt;
	}
	else
	{
		std::optional<SWarning> Result = std::make_optional(m_vWarnings[0]);
		m_vWarnings.erase(m_vWarnings.begin());
		return Result;
	}
}

int CClient::MaxLatencyTicks() const
{
	return GameTickSpeed() + (PredictionMargin() * GameTickSpeed()) / 1000;
}

void CClient::ResetAutoPredictionMargin()
{
	m_PredictionMarginMs = 10;
	m_AutoMarginLastSampleTime = 0;
	m_AutoMarginLatencyAverageMs = 0.0f;
	m_AutoMarginLatencyJitterMs = 0.0f;
}

void CClient::UpdatePredictionMargin()
{
	if(!m_ServerCapabilities.m_SyncWeaponInput)
	{
		m_PredictionMarginMs = 10;
		return;
	}

	SQmFastInputSettings Settings;
	Settings.m_Enabled = g_Config.m_TcFastInput != 0;
	Settings.m_FastAmountMs = g_Config.m_TcFastInputAmount;
	Settings.m_BasePredictionMarginMs = g_Config.m_ClPredictionMargin;
	const int BaseMargin = QmFastInputBasePredictionMarginMs(Settings);
	if(!g_Config.m_QmAutoMargin)
	{
		m_PredictionMarginMs = std::clamp(BaseMargin, 1, 300);
		return;
	}
	const int64_t Now = time_get();
	// 自适应采样不能把已经应用的预测边距再次计入延迟。
	const int LivePredictionMs = std::max(0, (int)((m_PredictedTime.GetWithoutMargin(Now) - m_aGameTime[g_Config.m_ClDummy].Get(Now)) * 1000 / (float)time_freq()));

	if(m_AutoMarginLastSampleTime == 0)
	{
		m_AutoMarginLastSampleTime = Now;
		m_AutoMarginLatencyAverageMs = LivePredictionMs;
		m_AutoMarginLatencyJitterMs = 0.0f;
	}
	else if(Now > m_AutoMarginLastSampleTime + time_freq() / 20)
	{
		// Track current latency and its spread so auto margin can react to unstable links in real time.
		const float LatencyDelta = std::abs((float)LivePredictionMs - m_AutoMarginLatencyAverageMs);
		m_AutoMarginLatencyAverageMs += (LivePredictionMs - m_AutoMarginLatencyAverageMs) * 0.15f;
		m_AutoMarginLatencyJitterMs += (LatencyDelta - m_AutoMarginLatencyJitterMs) * 0.15f;
		m_AutoMarginLastSampleTime = Now;
	}

	const int BaseMaxLatencyTicks = GameTickSpeed() + (BaseMargin * GameTickSpeed()) / 1000;
	const bool ConnectionProblems = m_aNetClient[g_Config.m_ClDummy].GotProblems(BaseMaxLatencyTicks * time_freq() / GameTickSpeed());
	const NETADDR *pServerAddr = ServerAddress();
	const CServerBrowser::CServerEntry *pCurrentServerEntry = pServerAddr ? const_cast<CServerBrowser &>(m_ServerBrowser).Find(*pServerAddr) : nullptr;
	const bool HasMeasuredPing = pCurrentServerEntry != nullptr && !pCurrentServerEntry->m_Info.m_LatencyIsEstimated && pCurrentServerEntry->m_Info.m_Latency >= 0;
	const float MeasuredPingMargin = HasMeasuredPing ? pCurrentServerEntry->m_Info.m_Latency * 0.5f : 0.0f;
	m_PredictionMarginMs = QmComputeAutoPredictionMargin(BaseMargin, MeasuredPingMargin, m_AutoMarginLatencyAverageMs, (float)LivePredictionMs, m_AutoMarginLatencyJitterMs, ConnectionProblems);
}

int CClient::PredictionMargin() const
{
	return m_PredictionMarginMs;
}

int CClient::UdpConnectivity(int NetType)
{
	static const int NETTYPES[2] = {NETTYPE_IPV6, NETTYPE_IPV4};
	int Connectivity = CONNECTIVITY_UNKNOWN;
	for(int PossibleNetType : NETTYPES)
	{
		if((NetType & PossibleNetType) == 0)
		{
			continue;
		}
		NETADDR GlobalUdpAddr;
		int NewConnectivity;
		switch(m_aNetClient[CONN_MAIN].GetConnectivity(PossibleNetType, &GlobalUdpAddr))
		{
		case CONNECTIVITY::UNKNOWN:
			NewConnectivity = CONNECTIVITY_UNKNOWN;
			break;
		case CONNECTIVITY::CHECKING:
			NewConnectivity = CONNECTIVITY_CHECKING;
			break;
		case CONNECTIVITY::UNREACHABLE:
			NewConnectivity = CONNECTIVITY_UNREACHABLE;
			break;
		case CONNECTIVITY::REACHABLE:
			NewConnectivity = CONNECTIVITY_REACHABLE;
			break;
		case CONNECTIVITY::ADDRESS_KNOWN:
			GlobalUdpAddr.port = 0;
			if(m_HaveGlobalTcpAddr && NetType == (int)m_GlobalTcpAddr.type && net_addr_comp(&m_GlobalTcpAddr, &GlobalUdpAddr) != 0)
			{
				NewConnectivity = CONNECTIVITY_DIFFERING_UDP_TCP_IP_ADDRESSES;
				break;
			}
			NewConnectivity = CONNECTIVITY_REACHABLE;
			break;
		default:
			dbg_assert(false, "invalid connectivity value");
			return CONNECTIVITY_UNKNOWN;
		}
		Connectivity = std::max(Connectivity, NewConnectivity);
	}
	return Connectivity;
}

static bool ViewLinkImpl(const char *pLink)
{
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	if(SDL_OpenURL(pLink) == 0)
	{
		return true;
	}
	log_error("client", "Failed to open link '%s' (%s)", pLink, SDL_GetError());
	return false;
#else
	if(open_link(pLink))
	{
		return true;
	}
	log_error("client", "Failed to open link '%s'", pLink);
	return false;
#endif
}

bool CClient::ViewLink(const char *pLink)
{
	if(!str_startswith(pLink, "https://"))
	{
		log_error("client", "Failed to open link '%s': only https-links are allowed", pLink);
		return false;
	}
	return ViewLinkImpl(pLink);
}

bool CClient::ViewFile(const char *pFilename)
{
#if defined(CONF_PLATFORM_MACOS)
	return ViewLinkImpl(pFilename);
#else
	// Create a file link so the path can contain forward and
	// backward slashes. But the file link must be absolute.
	char aWorkingDir[IO_MAX_PATH_LENGTH];
	if(fs_is_relative_path(pFilename))
	{
		if(!fs_getcwd(aWorkingDir, sizeof(aWorkingDir)))
		{
			log_error("client", "Failed to open file '%s' (failed to get working directory)", pFilename);
			return false;
		}
		str_append(aWorkingDir, "/");
	}
	else
	{
		aWorkingDir[0] = '\0';
	}

	char aFileLink[IO_MAX_PATH_LENGTH];
#if defined(CONF_PLATFORM_IOS)
	str_format(aFileLink, sizeof(aFileLink), "shareddocuments://%s%s", aWorkingDir, pFilename);
#else
	str_format(aFileLink, sizeof(aFileLink), "file://%s%s", aWorkingDir, pFilename);
#endif
	return ViewLinkImpl(aFileLink);
#endif
}

#if defined(CONF_FAMILY_WINDOWS)
void CClient::ShellRegister()
{
	char aFullPath[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aFullPath, sizeof(aFullPath));
	if(!aFullPath[0])
	{
		log_error("client", "Failed to register protocol and file extensions: could not determine absolute path");
		return;
	}

	bool Updated = false;
	if(!windows_shell_register_protocol("ddnet", aFullPath, &Updated))
		log_error("client", "Failed to register ddnet protocol");
	if(!windows_shell_register_extension(".map", "Map File", GAME_NAME, aFullPath, &Updated))
		log_error("client", "Failed to register .map file extension");
	if(!windows_shell_register_extension(".demo", "Demo File", GAME_NAME, aFullPath, &Updated))
		log_error("client", "Failed to register .demo file extension");
	if(!windows_shell_register_application(GAME_NAME, aFullPath, &Updated))
		log_error("client", "Failed to register application");
	if(Updated)
		windows_shell_update();
}

void CClient::ShellUnregister()
{
	char aFullPath[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aFullPath, sizeof(aFullPath));
	if(!aFullPath[0])
	{
		log_error("client", "Failed to unregister protocol and file extensions: could not determine absolute path");
		return;
	}

	bool Updated = false;
	if(!windows_shell_unregister_class("ddnet", &Updated))
		log_error("client", "Failed to unregister ddnet protocol");
	if(!windows_shell_unregister_class(GAME_NAME ".map", &Updated))
		log_error("client", "Failed to unregister .map file extension");
	if(!windows_shell_unregister_class(GAME_NAME ".demo", &Updated))
		log_error("client", "Failed to unregister .demo file extension");
	if(!windows_shell_unregister_application(aFullPath, &Updated))
		log_error("client", "Failed to unregister application");
	if(Updated)
		windows_shell_update();
}
#endif

std::optional<int> CClient::ShowMessageBox(const IGraphics::CMessageBox &MessageBox)
{
	// QmClient: 弹窗在主线程上模态运行，期间主循环不再更新心跳，退出兜底看门狗也可能
	// 正在倒计时。若不在这里停用两个看门狗，用户阅读弹窗超过 10 秒会被误判为“客户端
	// 卡死”（写出误导性 hang 报告并叠加第二个弹窗），退出清理阶段的弹窗还会被兜底
	// 看门狗连窗带进程一起结束，丢失用户正在阅读的诊断信息。
	// 当前所有进程内弹窗路径在关闭后都会退出或终止进程；若将来出现关闭后继续正常
	// 运行的弹窗，需要改为暂停/恢复语义。
	StopHangWatchdog();
	StopForcedExitWatchdog();
	std::optional<int> Result = m_pGraphics == nullptr ? std::nullopt : m_pGraphics->ShowMessageBox(MessageBox);
	if(!Result)
	{
		Result = ShowMessageBoxWithoutGraphics(MessageBox);
	}
	return Result;
}

void CClient::GetGpuInfoString(char (&aGpuInfo)[512])
{
#if defined(CONF_HEADLESS_CLIENT)
	if(m_pGraphics == nullptr || !m_pGraphics->IsBackendInitialized())
	{
		str_format(aGpuInfo, std::size(aGpuInfo),
			"Configured graphics backend: headless\n"
			"Graphics %s not yet initialized.",
			m_pGraphics == nullptr ? "were" : "backend was");
	}
	else
	{
		str_copy(aGpuInfo, "Configured graphics backend: headless");
	}
#else
	char aConfiguredBackend[128];
	int DetectedMajor = 0, DetectedMinor = 0, DetectedPatch = 0;
	const char *pDetectedBackend = "";
	if(m_pGraphics != nullptr && m_pGraphics->IsBackendInitialized() && m_pGraphics->GetDetectedContextVersion(DetectedMajor, DetectedMinor, DetectedPatch, pDetectedBackend) && pDetectedBackend[0] != '\0')
		str_format(aConfiguredBackend, sizeof(aConfiguredBackend), "%s %d.%d.%d", pDetectedBackend, DetectedMajor, DetectedMinor, DetectedPatch);
	else if(str_comp_nocase(g_Config.m_GfxBackend, "Vulkan") == 0)
		str_format(aConfiguredBackend, sizeof(aConfiguredBackend), "Vulkan API %s", g_Config.m_QmVulkanApiVersion == 14 ? "1.4 (fallback 1.3/1.1)" : (g_Config.m_QmVulkanApiVersion == 13 ? "1.3 (fallback 1.1)" : "1.1"));
	else
		str_format(aConfiguredBackend, sizeof(aConfiguredBackend), "%s %d.%d.%d", g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
	if(m_pGraphics == nullptr || !m_pGraphics->IsBackendInitialized())
	{
		str_format(aGpuInfo, std::size(aGpuInfo),
			"Configured graphics backend: %s\n"
			"Graphics %s not yet initialized.",
			aConfiguredBackend,
			m_pGraphics == nullptr ? "were" : "backend was");
	}
	else
	{
		str_format(aGpuInfo, std::size(aGpuInfo),
			"Configured graphics backend: %s\n"
			"GPU: %s - %s - %s\n"
			"Texture: %.2f MiB, "
			"Buffer: %.2f MiB, "
			"Streamed: %.2f MiB, "
			"Staging: %.2f MiB",
			aConfiguredBackend,
			m_pGraphics->GetVendorString(), m_pGraphics->GetRendererString(), m_pGraphics->GetVersionString(),
			m_pGraphics->TextureMemoryUsage() / 1024.0 / 1024.0,
			m_pGraphics->BufferMemoryUsage() / 1024.0 / 1024.0,
			m_pGraphics->StreamedMemoryUsage() / 1024.0 / 1024.0,
			m_pGraphics->StagingMemoryUsage() / 1024.0 / 1024.0);
	}
#endif
}

void CClient::SetLoggers(std::shared_ptr<ILogger> &&pFileLogger, std::shared_ptr<ILogger> &&pStdoutLogger, std::shared_ptr<ILogger> &&pPerfFileLogger)
{
	m_pFileLogger = pFileLogger;
	m_pStdoutLogger = pStdoutLogger;
	m_pPerfFileLogger = pPerfFileLogger;
}

void CClient::SetQmPerfFileSwitch(std::shared_ptr<CQmPerfFileSwitchLogger> pSwitch)
{
	m_pQmPerfFileSwitchLogger = std::move(pSwitch);
	m_pQmPerfFileSwitch = static_cast<CQmPerfFileSwitchLogger *>(m_pQmPerfFileSwitchLogger.get());
}

// 按配置开/关性能日志文件：任一性能开关开启即打开专用文件并立即落盘，
// 全部关闭则关闭文件。启动时调用一次建立初始状态，主循环按帧调用处理游戏内切换。
void CClient::UpdateQmPerfFileLogger()
{
	const bool Wanted = QmPerfEnabled();
	if(Wanted == m_QmPerfFileLoggerWanted || m_pQmPerfFileSwitch == nullptr)
		return;
	// 打开失败后等待下一次开关变化再尝试，不把失败标记为正在采集。
	m_QmPerfFileLoggerWanted = Wanted;

	if(Wanted)
	{
		m_pStorage->CreateFolder("dumps", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder("dumps/QmClient_Perf", IStorage::TYPE_SAVE);
		char aDate[64];
		str_timestamp(aDate, sizeof(aDate));
		char aPerfLogPath[128];
		// 每次开启都新建文件、绝不覆盖旧日志：首次用干净时间戳名，
		// 之后（同一进程内）追加递增序号。
		++m_QmPerfLogReopenCounter;
		if(m_QmPerfLogReopenCounter == 1)
			str_format(aPerfLogPath, sizeof(aPerfLogPath), "dumps/QmClient_Perf/qm_perf_%s.log", aDate);
		else
			str_format(aPerfLogPath, sizeof(aPerfLogPath), "dumps/QmClient_Perf/qm_perf_%s_%d.log", aDate, m_QmPerfLogReopenCounter);

		char aPerfLogCompletePath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, aPerfLogPath, aPerfLogCompletePath, sizeof(aPerfLogCompletePath));
		IOHANDLE PerfLogfile = m_pStorage->OpenFile(aPerfLogPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!PerfLogfile)
		{
			fs_makedir_rec_for(aPerfLogCompletePath);
			PerfLogfile = io_open(aPerfLogCompletePath, IOFLAG_WRITE);
		}
		if(!PerfLogfile)
		{
			char aWorkingDir[IO_MAX_PATH_LENGTH];
			if(fs_getcwd(aWorkingDir, sizeof(aWorkingDir)))
			{
				str_format(aPerfLogCompletePath, sizeof(aPerfLogCompletePath), "%s/%s", aWorkingDir, aPerfLogPath);
				fs_makedir_rec_for(aPerfLogCompletePath);
				PerfLogfile = io_open(aPerfLogCompletePath, IOFLAG_WRITE);
			}
		}
		if(PerfLogfile)
		{
			m_QmPerfFileLoggerActive = true;
			QmPerfBeginSession();
			m_QmPerfLastFrameEnd = 0;
			m_QmPerfFrameBatch = CQmPerfFrameBatch();
			m_pQmPerfFileSwitch->Set(log_logger_prefix_file(PerfLogfile, "perf/"));
			QmPerfLogFields("perf/session", "\"event\":\"session_start\",\"schema\":2,\"sampling\":\"automatic\",\"frame_samples\":\"all\",\"target_fps\":300,\"version\":" + QmPerfJsonString(CLIENT_RELEASE_VERSION), this);
			// QmClient：日志文件就绪后先写一次完整配置快照（敏感项脱敏），
			// 之后由主循环按秒写增量；见 perf_diagnostics.h。
			m_QmPerfConfigSnapshot.Start(ConfigManager(), this);
			m_QmPerfLastConfigCheck = time_get();
			log_info("client", "writing performance log to '%s'", aPerfLogCompletePath);
		}
		else
		{
			// 打开失败不重试（避免每帧刷屏），等下次配置变化再尝试。
			log_error("client", "failed to open '%s' for performance logging", aPerfLogCompletePath);
		}
	}
	else
	{
		if(m_QmPerfFileLoggerActive)
			FinishQmPerfSession(false);
		log_info("client", "stopped writing performance log");
	}
}

// 关闭采集前的收尾：补齐帧批次、配置增量、被限流丢弃的明细与会话结束标记，
// 再把文件 logger 换回 noop（退出用同步，运行中切走用作业线程）。
void CClient::FinishQmPerfSession(bool Shutdown)
{
	if(m_QmPerfFrameBatch.Count() != 0)
		QmPerfLogFields("perf/frame", m_QmPerfFrameBatch.TakeFields(), this);
	m_QmPerfConfigSnapshot.Update(this);
	QmPerfFlushDropped(this);
	QmPerfLogFields("perf/session", std::string("\"event\":\"session_end\",\"reason\":") + QmPerfJsonString(Shutdown ? "shutdown" : "disabled"), this);
	if(Shutdown)
		m_pQmPerfFileSwitch->Set(log_logger_noop());
	else
		Engine()->AddJob(m_pQmPerfFileSwitch->SetAsync(log_logger_noop()));
	m_QmPerfFileLoggerActive = false;
	m_QmPerfLastFrameEnd = 0;
}
