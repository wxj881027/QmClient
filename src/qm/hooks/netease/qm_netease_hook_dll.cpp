// 网易云音乐 3.1.36.205322 x64 歌词 Hook。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_metadata.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_v5_writer.h>

#include <windows.h>

#include <MinHook.h>
#include <mmsystem.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "windowsapp.lib")

namespace
{
	using namespace winrt;
	using namespace Windows::Media::Control;

	// GDI+ 的 opaque 参数只按 ABI 传递；GdipAddPathString 在 x64 上使用 Windows ABI。
	using TGdipAddPathString = int(WINAPI *)(void *, const wchar_t *, int, const void *, int, float, const void *, const void *);
	using TGdipDrawString = int(WINAPI *)(void *, const wchar_t *, int, const void *, const void *, const void *, const void *);

	// v4 在共享块头部加入了运行时启停控制字段，不能与旧 DLL 共用映射。
	constexpr wchar_t SHARED_MAPPING_NAME[] = L"Local\\QmClient.NeteaseHook.v4";
	constexpr wchar_t GDIPLUS_MODULE_NAME[] = L"gdiplus.dll";
	constexpr char GDIPLUS_ADD_PATH_STRING_FUNCTION_NAME[] = "GdipAddPathString";
	constexpr char GDIPLUS_DRAW_STRING_FUNCTION_NAME[] = "GdipDrawString";
	constexpr wchar_t NETEASE_MAIN_WINDOW_CLASS[] = L"OrpheusBrowserHost";
	constexpr int MAX_CAPTURED_UTF16 = 512;
	constexpr int MAX_WINDOW_TITLE_UTF16 = MAX_CAPTURED_UTF16;
	constexpr uint64_t TEXT_STALE_MS = 2000;
	constexpr uint32_t COVER_MAX_DIMENSION = 256;
	constexpr uint64_t COVER_MAX_PNG_BYTES = 8 * 1024 * 1024;
	constexpr uint64_t COVER_MAX_DOWNLOAD_BYTES = 16 * 1024 * 1024;
	constexpr uint64_t LOCAL_METADATA_REFRESH_MS = 750;
	constexpr uint64_t LOCAL_METADATA_MAX_BYTES = 16 * 1024 * 1024;
	constexpr uint64_t WAVE_OUT_POSITION_STALE_MS = 1500;

	std::atomic_bool g_Stop{false};
	std::atomic<uint64_t> g_LastTextTick{0};
	std::mutex g_TextMutex;
	std::string g_LastRenderedText;
	std::mutex g_CoverMutex;
	std::string g_CoverKey;
	std::string g_CoverPath;
	std::string g_CoverUrl;
	std::string g_CoverSongId;
	ULONGLONG g_CoverLastAttemptTick = 0;
	uint64_t g_CoverRequestedSerial = 0;
	uint64_t g_CoverHandledSerial = 0;
	bool g_CoverFetchScheduled = false;

	using QmNeteaseHook::SLocalTrackMetadata;

	struct SLocalMetadataFileCache
	{
		ULONGLONG m_Size = 0;
		ULONGLONG m_WriteTime = 0;
		bool m_Loaded = false;
		std::vector<SLocalTrackMetadata> m_vTracks;
	};

	SLocalMetadataFileCache g_PlayingListCache;
	SLocalMetadataFileCache g_LastTimePlayingListCache;
	std::vector<SLocalTrackMetadata> g_vLocalTracks;
	ULONGLONG g_NextLocalMetadataRefreshTick = 0;

	HANDLE g_hMapping = nullptr;
	QmNeteaseHook::SSharedBlock *g_pShared = nullptr;
	std::unique_ptr<QmNeteaseHook::CV5Writer> g_pV5Writer;
	std::atomic_bool g_V5WriterReady{false};
	std::atomic<uint64_t> g_V5Generation{0};
	std::atomic<uint64_t> g_V5SongId{0};
	std::string g_V5MediaKey;
	std::mutex g_V5Mutex;
	std::atomic_bool g_HooksInstalled{false};
	bool g_MinHookInitializedByUs = false;
	bool g_GdipAddPathStringHookCreated = false;
	bool g_GdipDrawStringHookCreated = false;
	HMODULE g_hGdiplusLoadedByUs = nullptr;
	void *g_pGdipAddPathStringTarget = nullptr;
	void *g_pGdipDrawStringTarget = nullptr;
	TGdipAddPathString g_OriginalGdipAddPathString = nullptr;
	TGdipDrawString g_OriginalGdipDrawString = nullptr;

	using TWaveOutOpen = MMRESULT(WINAPI *)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
	using TWaveOutGetPosition = MMRESULT(WINAPI *)(HWAVEOUT, LPMMTIME, UINT);
	using TWaveOutWrite = MMRESULT(WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT);
	using TWaveOutControl = MMRESULT(WINAPI *)(HWAVEOUT);

	void *g_pWaveOutOpenTarget = nullptr;
	void *g_pWaveOutGetPositionTarget = nullptr;
	void *g_pWaveOutWriteTarget = nullptr;
	void *g_pWaveOutPauseTarget = nullptr;
	void *g_pWaveOutRestartTarget = nullptr;
	void *g_pWaveOutResetTarget = nullptr;
	void *g_pWaveOutCloseTarget = nullptr;
	TWaveOutOpen g_OriginalWaveOutOpen = nullptr;
	TWaveOutGetPosition g_OriginalWaveOutGetPosition = nullptr;
	TWaveOutWrite g_OriginalWaveOutWrite = nullptr;
	TWaveOutControl g_OriginalWaveOutPause = nullptr;
	TWaveOutControl g_OriginalWaveOutRestart = nullptr;
	TWaveOutControl g_OriginalWaveOutReset = nullptr;
	TWaveOutControl g_OriginalWaveOutClose = nullptr;

	std::atomic<uintptr_t> g_ActiveWaveOutHandle{0};
	std::atomic<uint32_t> g_WaveOutSampleRate{0};
	std::atomic<uint32_t> g_WaveOutBytesPerSecond{0};
	std::atomic<int64_t> g_WaveOutPositionMs{0};
	std::atomic<uint64_t> g_WaveOutObservedQpc{0};
	std::atomic<uint64_t> g_WaveOutObservedTick{0};
	std::atomic<uint64_t> g_WaveOutTimelineGeneration{0};
	std::atomic_bool g_WaveOutHasPosition{false};
	std::atomic_bool g_WaveOutPlaying{false};

	bool IsControlStopRequested()
	{
		if(g_pShared == nullptr)
			return false;
		const uint32_t Flags = (uint32_t)InterlockedCompareExchange(
			reinterpret_cast<volatile LONG *>(&g_pShared->m_ControlFlags), 0, 0);
		return (Flags & QmNeteaseHook::CONTROL_STOP_REQUESTED) != 0;
	}

	bool IsStopRequested()
	{
		return g_Stop.load(std::memory_order_acquire);
	}

	bool IsWorkCancelled()
	{
		return IsStopRequested() || IsControlStopRequested();
	}

	struct SLyricsWindowProbe
	{
		DWORD m_ThreadId = 0;
		bool m_Found = false;
	};

	bool IsKnownLyricsTitle(const wchar_t *pTitle)
	{
		if(pTitle == nullptr || pTitle[0] == L'\0')
			return true;
		return _wcsicmp(pTitle, L"Desktop Lyrics") == 0 || _wcsicmp(pTitle, L"桌面歌词") == 0;
	}

	BOOL CALLBACK FindLyricsWindow(HWND hWnd, LPARAM UserData)
	{
		auto *pProbe = reinterpret_cast<SLyricsWindowProbe *>(UserData);
		if(pProbe == nullptr || GetWindowThreadProcessId(hWnd, nullptr) != pProbe->m_ThreadId)
			return TRUE;

		wchar_t aClassName[64] = {};
		if(GetClassNameW(hWnd, aClassName, (int)std::size(aClassName)) <= 0 || _wcsicmp(aClassName, L"DesktopLyrics") != 0)
			return TRUE;

		wchar_t aTitle[128] = {};
		GetWindowTextW(hWnd, aTitle, (int)std::size(aTitle));
		if(IsKnownLyricsTitle(aTitle))
		{
			pProbe->m_Found = true;
			return FALSE;
		}
		return TRUE;
	}

	bool IsDesktopLyricsThread()
	{
		thread_local ULONGLONG s_NextProbeTick = 0;
		thread_local bool s_LastResult = false;
		const ULONGLONG Now = GetTickCount64();
		if(Now < s_NextProbeTick)
			return s_LastResult;
		SLyricsWindowProbe Probe;
		Probe.m_ThreadId = GetCurrentThreadId();
		EnumWindows(FindLyricsWindow, reinterpret_cast<LPARAM>(&Probe));
		s_LastResult = Probe.m_Found;
		s_NextProbeTick = Now + 500;
		return s_LastResult;
	}

	struct SMainWindowTitleProbe
	{
		DWORD m_ProcessId = 0;
		std::wstring m_Title;
		bool m_Ambiguous = false;
	};

	BOOL CALLBACK FindNeteaseMainWindow(HWND hWnd, LPARAM UserData)
	{
		auto *pProbe = reinterpret_cast<SMainWindowTitleProbe *>(UserData);
		if(pProbe == nullptr)
			return TRUE;
		DWORD ProcessId = 0;
		GetWindowThreadProcessId(hWnd, &ProcessId);
		if(ProcessId != pProbe->m_ProcessId)
			return TRUE;

		wchar_t aClassName[64] = {};
		if(GetClassNameW(hWnd, aClassName, (int)std::size(aClassName)) <= 0 || _wcsicmp(aClassName, NETEASE_MAIN_WINDOW_CLASS) != 0)
			return TRUE;
		const int TitleLength = GetWindowTextLengthW(hWnd);
		if(TitleLength <= 0 || TitleLength > MAX_WINDOW_TITLE_UTF16)
			return TRUE;
		std::wstring Title((size_t)TitleLength + 1, L'\0');
		const int Copied = GetWindowTextW(hWnd, Title.data(), (int)Title.size());
		if(Copied <= 0)
			return TRUE;
		Title.resize((size_t)Copied);
		if(pProbe->m_Title.empty())
			pProbe->m_Title = std::move(Title);
		else if(pProbe->m_Title != Title)
			pProbe->m_Ambiguous = true;
		return TRUE;
	}

	std::string WideToUtf8(const wchar_t *pText, int Length)
	{
		if(pText == nullptr)
			return {};
		if(Length < 0)
			Length = (int)wcsnlen_s(pText, MAX_CAPTURED_UTF16);
		if(Length <= 0 || Length > MAX_CAPTURED_UTF16)
			return {};
		int OutLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pText, Length, nullptr, 0, nullptr, nullptr);
		if(OutLength <= 0)
			OutLength = WideCharToMultiByte(CP_UTF8, 0, pText, Length, nullptr, 0, nullptr, nullptr);
		if(OutLength <= 0)
			return {};
		std::string Result((size_t)OutLength, '\0');
		if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pText, Length, Result.data(), OutLength, nullptr, nullptr) <= 0 &&
			WideCharToMultiByte(CP_UTF8, 0, pText, Length, Result.data(), OutLength, nullptr, nullptr) <= 0)
			return {};
		return Result;
	}

	bool ReadNeteaseMainWindowTitle(std::string *pTitle)
	{
		if(pTitle == nullptr)
			return false;
		SMainWindowTitleProbe Probe;
		Probe.m_ProcessId = GetCurrentProcessId();
		EnumWindows(FindNeteaseMainWindow, reinterpret_cast<LPARAM>(&Probe));
		if(Probe.m_Ambiguous || Probe.m_Title.empty())
			return false;
		*pTitle = WideToUtf8(Probe.m_Title.data(), (int)Probe.m_Title.size());
		return !pTitle->empty();
	}

	void CaptureRenderedText(const wchar_t *pText, int Length)
	{
		if(IsWorkCancelled() || !IsDesktopLyricsThread())
			return;
		try
		{
			std::string Text = WideToUtf8(pText, Length);
			if(Text.empty())
				return;
			bool HasVisible = false;
			for(const unsigned char C : Text)
			{
				if(C > 0x20)
				{
					HasVisible = true;
					break;
				}
			}
			if(!HasVisible)
				return;
			if(Text.size() >= QmNeteaseHook::MAX_TEXT_BYTES)
				Text.resize(QmNeteaseHook::MAX_TEXT_BYTES - 1);
			while(!Text.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), (int)Text.size(), nullptr, 0) <= 0)
				Text.pop_back();
			if(Text.empty())
				return;
			std::scoped_lock Lock(g_TextMutex);
			g_LastRenderedText = std::move(Text);
			g_LastTextTick.store(GetTickCount64(), std::memory_order_release);
		}
		catch(...)
		{
			// Hook 回调不能把异常传播回网易云渲染线程。
		}
	}

	int WINAPI HookGdipAddPathString(void *pPath, const wchar_t *pString, int Length, const void *pFamily, int Style, float EmSize, const void *pLayoutRect, const void *pFormat)
	{
		CaptureRenderedText(pString, Length);
		return g_OriginalGdipAddPathString != nullptr ?
			       g_OriginalGdipAddPathString(pPath, pString, Length, pFamily, Style, EmSize, pLayoutRect, pFormat) :
			       0;
	}

	int WINAPI HookGdipDrawString(void *pGraphics, const wchar_t *pString, int Length, const void *pFont, const void *pLayoutRect, const void *pFormat, const void *pBrush)
	{
		CaptureRenderedText(pString, Length);
		return g_OriginalGdipDrawString != nullptr ?
			       g_OriginalGdipDrawString(pGraphics, pString, Length, pFont, pLayoutRect, pFormat, pBrush) :
			       0;
	}

	uintptr_t WaveOutHandleValue(HWAVEOUT hWaveOut)
	{
		return reinterpret_cast<uintptr_t>(hWaveOut);
	}

	bool ObserveWaveOutHandle(HWAVEOUT hWaveOut)
	{
		const uintptr_t HandleValue = WaveOutHandleValue(hWaveOut);
		if(HandleValue == 0)
			return false;
		uintptr_t Expected = 0;
		if(g_ActiveWaveOutHandle.compare_exchange_strong(Expected, HandleValue, std::memory_order_acq_rel))
		{
			g_WaveOutTimelineGeneration.fetch_add(1, std::memory_order_acq_rel);
			return true;
		}
		return Expected == HandleValue;
	}

	bool IsObservedWaveOutHandle(HWAVEOUT hWaveOut)
	{
		const uintptr_t HandleValue = WaveOutHandleValue(hWaveOut);
		return HandleValue != 0 && g_ActiveWaveOutHandle.load(std::memory_order_acquire) == HandleValue;
	}

	void ObserveWaveOutOpen(HWAVEOUT hWaveOut, const WAVEFORMATEX *pFormat)
	{
		if(!ObserveWaveOutHandle(hWaveOut))
			return;
		g_WaveOutSampleRate.store(pFormat != nullptr ? pFormat->nSamplesPerSec : 0, std::memory_order_release);
		g_WaveOutBytesPerSecond.store(pFormat != nullptr ? pFormat->nAvgBytesPerSec : 0, std::memory_order_release);
		g_WaveOutPositionMs.store(0, std::memory_order_release);
		g_WaveOutObservedQpc.store(0, std::memory_order_release);
		g_WaveOutObservedTick.store(0, std::memory_order_release);
		g_WaveOutHasPosition.store(false, std::memory_order_release);
		g_WaveOutPlaying.store(false, std::memory_order_release);
	}

	void ObserveWaveOutPosition(HWAVEOUT hWaveOut, const MMTIME &Time)
	{
		if(!ObserveWaveOutHandle(hWaveOut))
			return;
		uint32_t Value = 0;
		if(Time.wType == TIME_MS)
			Value = Time.u.ms;
		else if(Time.wType == TIME_SAMPLES)
			Value = Time.u.sample;
		else if(Time.wType == TIME_BYTES)
			Value = Time.u.cb;
		else
			return;
		int64_t PositionMs = 0;
		if(!QmNeteaseHook::ConvertWaveOutPositionToMs(
			   Time.wType, Value,
			   g_WaveOutSampleRate.load(std::memory_order_acquire),
			   g_WaveOutBytesPerSecond.load(std::memory_order_acquire),
			   &PositionMs))
			return;
		const bool HadPosition = g_WaveOutHasPosition.exchange(true, std::memory_order_acq_rel);
		const int64_t PreviousPosition = g_WaveOutPositionMs.exchange(PositionMs, std::memory_order_acq_rel);
		if(HadPosition && PositionMs != PreviousPosition)
			g_WaveOutPlaying.store(true, std::memory_order_release);
		LARGE_INTEGER Qpc{};
		QueryPerformanceCounter(&Qpc);
		g_WaveOutObservedQpc.store((uint64_t)Qpc.QuadPart, std::memory_order_release);
		g_WaveOutObservedTick.store(GetTickCount64(), std::memory_order_release);
	}

	void ObserveWaveOutPause(HWAVEOUT hWaveOut)
	{
		if(!IsObservedWaveOutHandle(hWaveOut))
			return;
		g_WaveOutPlaying.store(false, std::memory_order_release);
		g_WaveOutTimelineGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	void ObserveWaveOutRestart(HWAVEOUT hWaveOut)
	{
		if(!IsObservedWaveOutHandle(hWaveOut))
			return;
		g_WaveOutPlaying.store(true, std::memory_order_release);
		g_WaveOutTimelineGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	void ObserveWaveOutReset(HWAVEOUT hWaveOut)
	{
		if(!IsObservedWaveOutHandle(hWaveOut))
			return;
		g_WaveOutPositionMs.store(0, std::memory_order_release);
		g_WaveOutObservedQpc.store(0, std::memory_order_release);
		g_WaveOutObservedTick.store(0, std::memory_order_release);
		g_WaveOutHasPosition.store(false, std::memory_order_release);
		g_WaveOutPlaying.store(false, std::memory_order_release);
		g_WaveOutTimelineGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	void ObserveWaveOutClose(HWAVEOUT hWaveOut)
	{
		uintptr_t Expected = WaveOutHandleValue(hWaveOut);
		if(Expected == 0 || !g_ActiveWaveOutHandle.compare_exchange_strong(Expected, 0, std::memory_order_acq_rel))
			return;
		g_WaveOutSampleRate.store(0, std::memory_order_release);
		g_WaveOutBytesPerSecond.store(0, std::memory_order_release);
		g_WaveOutPositionMs.store(0, std::memory_order_release);
		g_WaveOutObservedQpc.store(0, std::memory_order_release);
		g_WaveOutObservedTick.store(0, std::memory_order_release);
		g_WaveOutHasPosition.store(false, std::memory_order_release);
		g_WaveOutPlaying.store(false, std::memory_order_release);
		g_WaveOutTimelineGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	MMRESULT WINAPI HookWaveOutOpen(LPHWAVEOUT phwo, UINT DeviceId, LPCWAVEFORMATEX pFormat, DWORD_PTR Callback, DWORD_PTR Instance, DWORD Flags)
	{
		if(g_OriginalWaveOutOpen == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutOpen(phwo, DeviceId, pFormat, Callback, Instance, Flags);
		if(Result == MMSYSERR_NOERROR && phwo != nullptr && (Flags & WAVE_FORMAT_QUERY) == 0)
			ObserveWaveOutOpen(*phwo, pFormat);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutGetPosition(HWAVEOUT hWaveOut, LPMMTIME pTime, UINT TimeSize)
	{
		if(g_OriginalWaveOutGetPosition == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutGetPosition(hWaveOut, pTime, TimeSize);
		if(Result == MMSYSERR_NOERROR && pTime != nullptr && TimeSize >= sizeof(MMTIME))
			ObserveWaveOutPosition(hWaveOut, *pTime);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutWrite(HWAVEOUT hWaveOut, LPWAVEHDR pHeader, UINT HeaderSize)
	{
		if(g_OriginalWaveOutWrite == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutWrite(hWaveOut, pHeader, HeaderSize);
		if(Result == MMSYSERR_NOERROR)
			ObserveWaveOutHandle(hWaveOut);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutPause(HWAVEOUT hWaveOut)
	{
		if(g_OriginalWaveOutPause == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutPause(hWaveOut);
		if(Result == MMSYSERR_NOERROR)
			ObserveWaveOutPause(hWaveOut);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutRestart(HWAVEOUT hWaveOut)
	{
		if(g_OriginalWaveOutRestart == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutRestart(hWaveOut);
		if(Result == MMSYSERR_NOERROR)
			ObserveWaveOutRestart(hWaveOut);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutReset(HWAVEOUT hWaveOut)
	{
		if(g_OriginalWaveOutReset == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutReset(hWaveOut);
		if(Result == MMSYSERR_NOERROR)
			ObserveWaveOutReset(hWaveOut);
		return Result;
	}

	MMRESULT WINAPI HookWaveOutClose(HWAVEOUT hWaveOut)
	{
		if(g_OriginalWaveOutClose == nullptr)
			return MMSYSERR_ERROR;
		const MMRESULT Result = g_OriginalWaveOutClose(hWaveOut);
		if(Result == MMSYSERR_NOERROR)
			ObserveWaveOutClose(hWaveOut);
		return Result;
	}

	bool HasGdipHooks()
	{
		return g_GdipAddPathStringHookCreated || g_GdipDrawStringHookCreated;
	}

	void RemoveGdipHooks()
	{
		if(g_GdipAddPathStringHookCreated && g_pGdipAddPathStringTarget != nullptr)
		{
			MH_DisableHook(g_pGdipAddPathStringTarget);
			MH_RemoveHook(g_pGdipAddPathStringTarget);
		}
		if(g_GdipDrawStringHookCreated && g_pGdipDrawStringTarget != nullptr)
		{
			MH_DisableHook(g_pGdipDrawStringTarget);
			MH_RemoveHook(g_pGdipDrawStringTarget);
		}
		g_GdipAddPathStringHookCreated = false;
		g_GdipDrawStringHookCreated = false;
		g_pGdipAddPathStringTarget = nullptr;
		g_pGdipDrawStringTarget = nullptr;
		g_OriginalGdipAddPathString = nullptr;
		g_OriginalGdipDrawString = nullptr;
	}

	void ReleaseGdiplus()
	{
		if(g_hGdiplusLoadedByUs != nullptr)
		{
			FreeLibrary(g_hGdiplusLoadedByUs);
			g_hGdiplusLoadedByUs = nullptr;
		}
	}

	bool EnableHookTarget(void *pTarget)
	{
		if(pTarget == nullptr)
			return true;
		const MH_STATUS Status = MH_EnableHook(pTarget);
		return Status == MH_OK || Status == MH_ERROR_ENABLED;
	}

	void DisableHookTarget(void *pTarget)
	{
		if(pTarget != nullptr)
			MH_DisableHook(pTarget);
	}

	bool EnableGdipHooks()
	{
		return EnableHookTarget(g_pGdipAddPathStringTarget) && EnableHookTarget(g_pGdipDrawStringTarget);
	}

	void DisableGdipHooks()
	{
		DisableHookTarget(g_pGdipAddPathStringTarget);
		DisableHookTarget(g_pGdipDrawStringTarget);
	}

	bool CreateWinmmHook(HMODULE hWinmm, const char *pName, void *pDetour, void **ppOriginal, void **ppTarget)
	{
		if(hWinmm == nullptr || pName == nullptr || pDetour == nullptr || ppOriginal == nullptr || ppTarget == nullptr)
			return false;
		if(*ppTarget != nullptr)
			return true;
		const FARPROC pTarget = GetProcAddress(hWinmm, pName);
		if(pTarget == nullptr)
			return false;
		if(MH_CreateHook(reinterpret_cast<void *>(pTarget), pDetour, ppOriginal) != MH_OK)
			return false;
		*ppTarget = reinterpret_cast<void *>(pTarget);
		return true;
	}

	void InstallWaveOutHooks()
	{
		HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");
		if(hWinmm == nullptr)
			hWinmm = LoadLibraryW(L"winmm.dll");
		if(hWinmm == nullptr)
			return;
		CreateWinmmHook(hWinmm, "waveOutOpen", reinterpret_cast<void *>(&HookWaveOutOpen), reinterpret_cast<void **>(&g_OriginalWaveOutOpen), &g_pWaveOutOpenTarget);
		CreateWinmmHook(hWinmm, "waveOutGetPosition", reinterpret_cast<void *>(&HookWaveOutGetPosition), reinterpret_cast<void **>(&g_OriginalWaveOutGetPosition), &g_pWaveOutGetPositionTarget);
		CreateWinmmHook(hWinmm, "waveOutWrite", reinterpret_cast<void *>(&HookWaveOutWrite), reinterpret_cast<void **>(&g_OriginalWaveOutWrite), &g_pWaveOutWriteTarget);
		CreateWinmmHook(hWinmm, "waveOutPause", reinterpret_cast<void *>(&HookWaveOutPause), reinterpret_cast<void **>(&g_OriginalWaveOutPause), &g_pWaveOutPauseTarget);
		CreateWinmmHook(hWinmm, "waveOutRestart", reinterpret_cast<void *>(&HookWaveOutRestart), reinterpret_cast<void **>(&g_OriginalWaveOutRestart), &g_pWaveOutRestartTarget);
		CreateWinmmHook(hWinmm, "waveOutReset", reinterpret_cast<void *>(&HookWaveOutReset), reinterpret_cast<void **>(&g_OriginalWaveOutReset), &g_pWaveOutResetTarget);
		CreateWinmmHook(hWinmm, "waveOutClose", reinterpret_cast<void *>(&HookWaveOutClose), reinterpret_cast<void **>(&g_OriginalWaveOutClose), &g_pWaveOutCloseTarget);
	}

	void EnableWaveOutHooks()
	{
		EnableHookTarget(g_pWaveOutOpenTarget);
		EnableHookTarget(g_pWaveOutGetPositionTarget);
		EnableHookTarget(g_pWaveOutWriteTarget);
		EnableHookTarget(g_pWaveOutPauseTarget);
		EnableHookTarget(g_pWaveOutRestartTarget);
		EnableHookTarget(g_pWaveOutResetTarget);
		EnableHookTarget(g_pWaveOutCloseTarget);
	}

	void DisableWaveOutHooks()
	{
		DisableHookTarget(g_pWaveOutOpenTarget);
		DisableHookTarget(g_pWaveOutGetPositionTarget);
		DisableHookTarget(g_pWaveOutWriteTarget);
		DisableHookTarget(g_pWaveOutPauseTarget);
		DisableHookTarget(g_pWaveOutRestartTarget);
		DisableHookTarget(g_pWaveOutResetTarget);
		DisableHookTarget(g_pWaveOutCloseTarget);
	}

	bool InstallHooks()
	{
		if(g_HooksInstalled)
			return true;
		// 停用时保留 MinHook 的 trampoline，恢复时只重新开启同一条跳转。
		// 这避免渲染线程正从 trampoline 返回时被释放其代码页。
		if(HasGdipHooks())
		{
			if(!EnableGdipHooks())
				return false;
			InstallWaveOutHooks();
			EnableWaveOutHooks();
			g_HooksInstalled.store(true, std::memory_order_release);
			return true;
		}
		if(GetModuleHandleW(L"cloudmusic.dll") == nullptr)
			return false;
		HMODULE hGdiplus = GetModuleHandleW(GDIPLUS_MODULE_NAME);
		if(hGdiplus == nullptr)
		{
			hGdiplus = LoadLibraryW(GDIPLUS_MODULE_NAME);
			g_hGdiplusLoadedByUs = hGdiplus;
		}
		if(hGdiplus == nullptr)
			return false;
		FARPROC pGdipAddPathString = GetProcAddress(hGdiplus, GDIPLUS_ADD_PATH_STRING_FUNCTION_NAME);
		FARPROC pGdipDrawString = GetProcAddress(hGdiplus, GDIPLUS_DRAW_STRING_FUNCTION_NAME);
		if(pGdipAddPathString == nullptr && pGdipDrawString == nullptr)
		{
			ReleaseGdiplus();
			return false;
		}

		const MH_STATUS InitStatus = MH_Initialize();
		if(InitStatus != MH_OK && InitStatus != MH_ERROR_ALREADY_INITIALIZED)
		{
			ReleaseGdiplus();
			return false;
		}
		g_MinHookInitializedByUs = InitStatus == MH_OK;
		if(pGdipAddPathString != nullptr)
		{
			g_pGdipAddPathStringTarget = reinterpret_cast<void *>(pGdipAddPathString);
			g_GdipAddPathStringHookCreated = MH_CreateHook(g_pGdipAddPathStringTarget, reinterpret_cast<void *>(&HookGdipAddPathString), reinterpret_cast<void **>(&g_OriginalGdipAddPathString)) == MH_OK;
			if(!g_GdipAddPathStringHookCreated)
				g_pGdipAddPathStringTarget = nullptr;
		}
		if(pGdipDrawString != nullptr)
		{
			g_pGdipDrawStringTarget = reinterpret_cast<void *>(pGdipDrawString);
			g_GdipDrawStringHookCreated = MH_CreateHook(g_pGdipDrawStringTarget, reinterpret_cast<void *>(&HookGdipDrawString), reinterpret_cast<void **>(&g_OriginalGdipDrawString)) == MH_OK;
			if(!g_GdipDrawStringHookCreated)
				g_pGdipDrawStringTarget = nullptr;
		}
		if(!HasGdipHooks())
		{
			if(g_MinHookInitializedByUs)
				MH_Uninitialize();
			g_MinHookInitializedByUs = false;
			ReleaseGdiplus();
			return false;
		}
		if(!EnableGdipHooks())
		{
			RemoveGdipHooks();
			if(g_MinHookInitializedByUs)
				MH_Uninitialize();
			g_MinHookInitializedByUs = false;
			ReleaseGdiplus();
			return false;
		}
		InstallWaveOutHooks();
		EnableWaveOutHooks();
		g_HooksInstalled.store(true, std::memory_order_release);
		return true;
	}

	void SuspendHooks()
	{
		if(!g_HooksInstalled.exchange(false, std::memory_order_acq_rel))
			return;
		if(HasGdipHooks())
			DisableGdipHooks();
		DisableWaveOutHooks();
	}

	void Publish(QmNeteaseHook::SSnapshot Snapshot)
	{
		static std::atomic<uint64_t> Sequence{0};
		if(g_pShared == nullptr || IsWorkCancelled())
			return;
		const uint64_t EvenSequence = Sequence.fetch_add(2, std::memory_order_relaxed) + 2;
		Snapshot.m_Sequence = EvenSequence;
		QmNeteaseHook::FinalizeSnapshot(&Snapshot);
		const size_t Slot = (size_t)((EvenSequence / 2) & 1);
		std::memcpy(&g_pShared->m_aSnapshots[Slot], &Snapshot, sizeof(Snapshot));
		MemoryBarrier();
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&g_pShared->m_ActiveSequence), (LONG64)EvenSequence);
	}

	// DesktopLyrics GDI 只作为迁移期 fallback。Helper/CDP 发布 Frontend 结果
	// 后，DLL 不得覆盖它；同时不把标准 SMTC 字段写入 v5。
	void PublishV5Fallback(const QmNeteaseHook::SSnapshot &LegacySnapshot)
	{
		if(!g_V5WriterReady.load(std::memory_order_acquire) || !g_pV5Writer || !g_pV5Writer->IsOpen() || IsWorkCancelled())
			return;
		if(LegacySnapshot.m_SongId == 0 || (LegacySnapshot.m_Status & QmNeteaseHook::STATUS_HAS_CURRENT_LINE) == 0 || LegacySnapshot.m_aCurrentLine[0] == '\0')
			return;
		std::scoped_lock Lock(g_V5Mutex);

		const uint64_t SongId = LegacySnapshot.m_SongId;
		const std::string MediaKey = std::string(LegacySnapshot.m_aSongId) + '\n' + LegacySnapshot.m_aTitle + '\n' + LegacySnapshot.m_aArtist + '\n' + LegacySnapshot.m_aAlbum;
		if(MediaKey != g_V5MediaKey)
		{
			g_V5MediaKey = MediaKey;
			g_V5SongId.store(SongId, std::memory_order_release);
			const uint64_t Previous = g_V5Generation.load(std::memory_order_acquire);
			g_V5Generation.store(Previous == UINT64_MAX ? 1 : Previous + 1, std::memory_order_release);
		}
		QmNeteaseHook::SSnapshotV5 Snapshot{};
		Snapshot.m_CloudMusicPid = GetCurrentProcessId();
		Snapshot.m_SongId = SongId;
		Snapshot.m_Generation = g_V5Generation.load(std::memory_order_acquire);
		Snapshot.m_UpdatedAtTick = GetTickCount64();
		Snapshot.m_PositionMs = std::max<int64_t>(0, LegacySnapshot.m_PositionMs);
		if(SongId != 0)
			Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_HAS_SONG;
		if((LegacySnapshot.m_Status & QmNeteaseHook::STATUS_PLAYING) != 0)
			Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_PLAYING_HINT;
		Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_LYRIC_VALID | QmNeteaseHook::V5_FLAG_POSITION_VALID;
		Snapshot.m_LyricSource = (uint32_t)QmNeteaseHook::ENeteaseLyricSource::DesktopLyricsFallback;
		Snapshot.m_LineStartMs = -1;
		Snapshot.m_LineEndMs = -1;
		QmNeteaseHook::CopyUtf8Truncated(Snapshot.m_aCurrentLyric, sizeof(Snapshot.m_aCurrentLyric), LegacySnapshot.m_aCurrentLine, std::strlen(LegacySnapshot.m_aCurrentLine));
		g_pV5Writer->PublishFallback(Snapshot, GetTickCount64(), 2000);
	}

	template<typename TOperation>
	bool GetAsyncResult(const TOperation &Operation)
	{
		using winrt::Windows::Foundation::AsyncStatus;
		for(int i = 0; i < 100 && !IsWorkCancelled(); ++i)
		{
			const AsyncStatus Status = Operation.Status();
			if(Status == AsyncStatus::Completed)
				return true;
			if(Status == AsyncStatus::Canceled || Status == AsyncStatus::Error)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return false;
	}

	std::string WideStringToUtf8(const std::wstring &Value)
	{
		if(Value.empty())
			return {};
		const int Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Value.data(), (int)Value.size(), nullptr, 0, nullptr, nullptr);
		if(Length <= 0)
			return {};
		std::string Result((size_t)Length, '\0');
		if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Value.data(), (int)Value.size(), Result.data(), Length, nullptr, nullptr) != Length)
			return {};
		return Result;
	}

	std::wstring Utf8StringToWide(const std::string &Value)
	{
		if(Value.empty())
			return {};
		int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Value.data(), (int)Value.size(), nullptr, 0);
		if(Length <= 0)
			Length = MultiByteToWideChar(CP_UTF8, 0, Value.data(), (int)Value.size(), nullptr, 0);
		if(Length <= 0)
			return {};
		std::wstring Result((size_t)Length, L'\0');
		if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Value.data(), (int)Value.size(), Result.data(), Length) <= 0 &&
			MultiByteToWideChar(CP_UTF8, 0, Value.data(), (int)Value.size(), Result.data(), Length) <= 0)
			return {};
		return Result;
	}

	std::wstring LocalAppDataDirectory()
	{
		wchar_t aPath[32768] = {};
		const DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
			return {};
		return std::wstring(aPath, Length);
	}

	bool ReadUtf8File(const std::wstring &Path, std::string *pContents, ULONGLONG *pSize, ULONGLONG *pWriteTime)
	{
		if(pContents == nullptr || Path.empty())
			return false;
		HANDLE hFile = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if(hFile == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER FileSize;
		FILETIME WriteTime{};
		const bool HasSize = GetFileSizeEx(hFile, &FileSize) != FALSE;
		const bool HasWriteTime = GetFileTime(hFile, nullptr, nullptr, &WriteTime) != FALSE;
		if(!HasSize || FileSize.QuadPart <= 0 || (uint64_t)FileSize.QuadPart > LOCAL_METADATA_MAX_BYTES || (uint64_t)FileSize.QuadPart > SIZE_MAX)
		{
			CloseHandle(hFile);
			return false;
		}
		std::string Contents((size_t)FileSize.QuadPart, '\0');
		size_t Offset = 0;
		bool Success = true;
		while(Offset < Contents.size())
		{
			const DWORD Chunk = (DWORD)std::min<size_t>(Contents.size() - Offset, 1024 * 1024);
			DWORD Read = 0;
			if(!ReadFile(hFile, Contents.data() + Offset, Chunk, &Read, nullptr) || Read == 0)
			{
				Success = false;
				break;
			}
			Offset += Read;
		}
		CloseHandle(hFile);
		if(!Success || Offset != Contents.size())
			return false;
		if(Contents.size() >= 3 && (uint8_t)Contents[0] == 0xEF && (uint8_t)Contents[1] == 0xBB && (uint8_t)Contents[2] == 0xBF)
			Contents.erase(0, 3);
		*pContents = std::move(Contents);
		if(pSize != nullptr)
			*pSize = (ULONGLONG)FileSize.QuadPart;
		if(pWriteTime != nullptr)
			*pWriteTime = HasWriteTime ? ((ULONGLONG)WriteTime.dwHighDateTime << 32 | WriteTime.dwLowDateTime) : 0;
		return true;
	}

	using winrt::Windows::Data::Json::JsonArray;
	using winrt::Windows::Data::Json::JsonObject;
	using winrt::Windows::Data::Json::JsonValue;
	using winrt::Windows::Data::Json::JsonValueType;

	bool GetJsonValue(const JsonObject &Object, const wchar_t *pName, JsonValue *pValue)
	{
		if(pName == nullptr || pValue == nullptr)
			return false;
		try
		{
			*pValue = Object.GetNamedValue(pName);
			return *pValue != nullptr;
		}
		catch(...)
		{
			return false;
		}
	}

	bool GetJsonString(const JsonObject &Object, const wchar_t *pName, std::string *pOut)
	{
		JsonValue Value{nullptr};
		if(!GetJsonValue(Object, pName, &Value) || Value.ValueType() != JsonValueType::String)
			return false;
		try
		{
			*pOut = winrt::to_string(Value.GetString());
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool GetJsonNumber(const JsonObject &Object, const wchar_t *pName, double *pOut)
	{
		JsonValue Value{nullptr};
		if(!GetJsonValue(Object, pName, &Value) || Value.ValueType() != JsonValueType::Number)
			return false;
		try
		{
			const double Number = Value.GetNumber();
			if(!std::isfinite(Number))
				return false;
			*pOut = Number;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool GetJsonObject(const JsonObject &Object, const wchar_t *pName, JsonObject *pOut)
	{
		if(pOut == nullptr)
			return false;
		try
		{
			*pOut = Object.GetNamedObject(pName);
			return *pOut != nullptr;
		}
		catch(...)
		{
			return false;
		}
	}

	bool GetJsonArray(const JsonObject &Object, const wchar_t *pName, JsonArray *pOut)
	{
		if(pOut == nullptr)
			return false;
		try
		{
			*pOut = Object.GetNamedArray(pName);
			return *pOut != nullptr;
		}
		catch(...)
		{
			return false;
		}
	}

	std::string JsonNumberToString(double Number)
	{
		if(!std::isfinite(Number) || Number < 0 || Number > 9223372036854774784.0 || std::floor(Number) != Number)
			return {};
		char aNumber[64] = {};
		std::snprintf(aNumber, sizeof(aNumber), "%.0f", Number);
		return aNumber;
	}

	bool GetJsonStringOrNumber(const JsonObject &Object, const wchar_t *pName, std::string *pOut)
	{
		if(GetJsonString(Object, pName, pOut))
			return true;
		double Number = 0.0;
		if(!GetJsonNumber(Object, pName, &Number))
			return false;
		const std::string Text = JsonNumberToString(Number);
		if(Text.empty())
			return false;
		*pOut = Text;
		return true;
	}

	bool GetJsonInt64(const JsonObject &Object, const wchar_t *pName, int64_t *pOut)
	{
		if(pOut == nullptr)
			return false;
		double Number = 0.0;
		if(GetJsonNumber(Object, pName, &Number))
		{
			if(Number < 0 || Number > 9223372036854774784.0 || std::floor(Number) != Number)
				return false;
			*pOut = (int64_t)Number;
			return true;
		}
		std::string Text;
		if(!GetJsonString(Object, pName, &Text) || Text.empty())
			return false;
		const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), *pOut);
		return Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() && *pOut >= 0;
	}

	bool ParseLocalTrack(const JsonObject &Entry, SLocalTrackMetadata *pTrack)
	{
		if(pTrack == nullptr)
			return false;
		JsonObject Track;
		if(!GetJsonObject(Entry, L"track", &Track))
			Track = Entry;
		SLocalTrackMetadata Metadata;
		if(!GetJsonString(Track, L"name", &Metadata.m_Title) || Metadata.m_Title.empty())
			return false;
		if(!GetJsonStringOrNumber(Track, L"id", &Metadata.m_SongId))
		{
			GetJsonStringOrNumber(Entry, L"trackId", &Metadata.m_SongId);
			if(Metadata.m_SongId.empty())
				GetJsonStringOrNumber(Entry, L"id", &Metadata.m_SongId);
		}

		JsonArray Artists;
		if(GetJsonArray(Track, L"artists", &Artists))
		{
			for(uint32_t i = 0; i < Artists.Size(); ++i)
			{
				try
				{
					const JsonObject Artist = Artists.GetObjectAt(i);
					std::string Name;
					if(!GetJsonString(Artist, L"name", &Name) || Name.empty())
						continue;
					if(!Metadata.m_Artist.empty())
						Metadata.m_Artist.append(", ");
					Metadata.m_Artist.append(Name);
				}
				catch(...)
				{
					break;
				}
			}
		}
		if(Metadata.m_Artist.empty())
			GetJsonString(Track, L"artist", &Metadata.m_Artist);

		JsonObject Album;
		if(GetJsonObject(Track, L"album", &Album))
		{
			GetJsonString(Album, L"name", &Metadata.m_Album);
			if(Metadata.m_Album.empty())
				GetJsonString(Album, L"albumName", &Metadata.m_Album);
			GetJsonString(Album, L"picUrl", &Metadata.m_CoverUrl);
			if(Metadata.m_CoverUrl.empty())
				GetJsonString(Album, L"cover", &Metadata.m_CoverUrl);
		}
		if(Metadata.m_Album.empty())
			GetJsonString(Track, L"albumName", &Metadata.m_Album);
		if(Metadata.m_CoverUrl.empty())
			GetJsonString(Track, L"picUrl", &Metadata.m_CoverUrl);
		GetJsonInt64(Track, L"duration", &Metadata.m_DurationMs);
		if(Metadata.m_DurationMs < 0 || Metadata.m_DurationMs > 24LL * 60 * 60 * 1000)
			Metadata.m_DurationMs = 0;
		*pTrack = std::move(Metadata);
		return true;
	}

	bool ParseLocalTrackList(const std::string &Contents, std::vector<SLocalTrackMetadata> *pTracks)
	{
		if(pTracks == nullptr || Contents.empty())
			return false;
		const std::wstring WideContents = Utf8StringToWide(Contents);
		if(WideContents.empty())
			return false;
		try
		{
			const JsonObject Root = JsonObject::Parse(winrt::hstring(WideContents));
			JsonArray List;
			if(!GetJsonArray(Root, L"list", &List))
				return false;
			std::vector<SLocalTrackMetadata> Tracks;
			Tracks.reserve(List.Size());
			for(uint32_t i = 0; i < List.Size(); ++i)
			{
				try
				{
					SLocalTrackMetadata Track;
					if(ParseLocalTrack(List.GetObjectAt(i), &Track))
						Tracks.push_back(std::move(Track));
				}
				catch(...)
				{
					continue;
				}
			}
			*pTracks = std::move(Tracks);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	std::string NormalizeMediaField(const std::string &Value)
	{
		std::string Result;
		Result.reserve(Value.size());
		bool PendingSpace = false;
		for(const unsigned char Character : Value)
		{
			if(Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n')
			{
				PendingSpace = !Result.empty();
				continue;
			}
			if(PendingSpace)
			{
				Result.push_back(' ');
				PendingSpace = false;
			}
			Result.push_back(Character >= 'A' && Character <= 'Z' ? (char)(Character - 'A' + 'a') : (char)Character);
		}
		return Result;
	}

	std::string NormalizeArtistList(const std::string &Value)
	{
		const std::string Normalized = NormalizeMediaField(Value);
		std::vector<std::string> Parts;
		std::string Part;
		for(const char Character : Normalized)
		{
			if(Character == ',' || Character == ';' || Character == '|' || Character == '&')
			{
				if(!Part.empty())
					Parts.push_back(std::move(Part));
				Part.clear();
			}
			else
			{
				Part.push_back(Character);
			}
		}
		if(!Part.empty())
			Parts.push_back(std::move(Part));
		std::sort(Parts.begin(), Parts.end());
		std::string Result;
		for(const std::string &Item : Parts)
		{
			if(!Result.empty())
				Result.push_back('|');
			Result.append(Item);
		}
		return Result;
	}

	bool ArtistsMatch(const std::string &Left, const std::string &Right)
	{
		const std::string NormalizedLeft = NormalizeMediaField(Left);
		const std::string NormalizedRight = NormalizeMediaField(Right);
		return !NormalizedLeft.empty() && NormalizedLeft == NormalizedRight ||
		       (!NormalizedLeft.empty() && NormalizeArtistList(NormalizedLeft) == NormalizeArtistList(NormalizedRight));
	}

	bool RefreshLocalMetadataFile(const std::wstring &Path, SLocalMetadataFileCache *pCache)
	{
		if(pCache == nullptr || Path.empty())
			return false;
		WIN32_FILE_ATTRIBUTE_DATA Attributes{};
		if(!GetFileAttributesExW(Path.c_str(), GetFileExInfoStandard, &Attributes) || (Attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			return false;
		const ULONGLONG Size = ((ULONGLONG)Attributes.nFileSizeHigh << 32) | Attributes.nFileSizeLow;
		const ULONGLONG WriteTime = ((ULONGLONG)Attributes.ftLastWriteTime.dwHighDateTime << 32) | Attributes.ftLastWriteTime.dwLowDateTime;
		if(pCache->m_Loaded && pCache->m_Size == Size && pCache->m_WriteTime == WriteTime)
			return true;
		std::string Contents;
		ULONGLONG ReadSize = 0;
		ULONGLONG ReadWriteTime = 0;
		if(!ReadUtf8File(Path, &Contents, &ReadSize, &ReadWriteTime))
			return false;
		std::vector<SLocalTrackMetadata> Tracks;
		if(!ParseLocalTrackList(Contents, &Tracks))
			return false;
		pCache->m_Size = ReadSize;
		pCache->m_WriteTime = ReadWriteTime;
		pCache->m_Loaded = true;
		pCache->m_vTracks = std::move(Tracks);
		return true;
	}

	void RefreshLocalMetadata()
	{
		const ULONGLONG Now = GetTickCount64();
		if(Now < g_NextLocalMetadataRefreshTick)
			return;
		g_NextLocalMetadataRefreshTick = Now + LOCAL_METADATA_REFRESH_MS;
		const std::wstring BaseDirectory = LocalAppDataDirectory();
		if(BaseDirectory.empty())
			return;
		const std::filesystem::path FileDirectory = std::filesystem::path(BaseDirectory) / L"NetEase" / L"CloudMusic" / L"webdata" / L"file";
		RefreshLocalMetadataFile((FileDirectory / L"playingList").wstring(), &g_PlayingListCache);
		RefreshLocalMetadataFile((FileDirectory / L"lastTimePlayingList").wstring(), &g_LastTimePlayingListCache);

		g_vLocalTracks.clear();
		const auto AppendTracks = [](const std::vector<SLocalTrackMetadata> &Tracks) {
			for(const SLocalTrackMetadata &Track : Tracks)
			{
				const std::string Key = NormalizeMediaField(Track.m_Title) + '\n' + NormalizeArtistList(Track.m_Artist) + '\n' + NormalizeMediaField(Track.m_Album);
				bool Duplicate = false;
				for(const SLocalTrackMetadata &Existing : g_vLocalTracks)
				{
					const std::string ExistingKey = NormalizeMediaField(Existing.m_Title) + '\n' + NormalizeArtistList(Existing.m_Artist) + '\n' + NormalizeMediaField(Existing.m_Album);
					if((!Track.m_SongId.empty() && Track.m_SongId == Existing.m_SongId) || Key == ExistingKey)
					{
						Duplicate = true;
						break;
					}
				}
				if(!Duplicate)
					g_vLocalTracks.push_back(Track);
			}
		};
		// playingList 更接近当前播放状态，必须优先放入候选。
		AppendTracks(g_PlayingListCache.m_vTracks);
		AppendTracks(g_LastTimePlayingListCache.m_vTracks);
	}

	bool FindLocalTrack(const std::string &Title, const std::string &Artist, int64_t DurationMs, SLocalTrackMetadata *pTrack)
	{
		if(pTrack == nullptr || Title.empty())
			return false;
		RefreshLocalMetadata();
		const std::string NormalizedTitle = NormalizeMediaField(Title);
		if(NormalizedTitle.empty())
			return false;
		int BestScore = -1;
		for(const SLocalTrackMetadata &Candidate : g_vLocalTracks)
		{
			if(NormalizeMediaField(Candidate.m_Title) != NormalizedTitle || !ArtistsMatch(Artist, Candidate.m_Artist))
				continue;
			int Score = 1000;
			if(DurationMs > 0 && Candidate.m_DurationMs > 0)
			{
				const int64_t Difference = std::llabs(DurationMs - Candidate.m_DurationMs);
				if(Difference > 5000)
					Score -= (int)std::min<int64_t>(500, Difference / 1000);
				else
					Score += 100;
			}
			if(!Candidate.m_SongId.empty())
				Score += 1;
			if(Score > BestScore)
			{
				BestScore = Score;
				*pTrack = Candidate;
			}
		}
		return BestScore >= 0;
	}

	bool FindCurrentLocalTrack(SLocalTrackMetadata *pTrack)
	{
		if(pTrack == nullptr)
			return false;
		RefreshLocalMetadata();
		std::string WindowTitle;
		if(!ReadNeteaseMainWindowTitle(&WindowTitle))
			return false;
		const SLocalTrackMetadata *pActiveTrack = QmNeteaseHook::FindLocalTrackByWindowTitle(g_vLocalTracks, WindowTitle);
		if(pActiveTrack == nullptr)
			return false;
		*pTrack = *pActiveTrack;
		return true;
	}

	std::wstring CoverCacheDirectory()
	{
		wchar_t aPath[32768] = {};
		DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
		{
			Length = GetTempPathW((DWORD)std::size(aPath), aPath);
			if(Length == 0 || Length >= std::size(aPath))
				return {};
		}
		try
		{
			std::filesystem::path Directory(std::wstring(aPath, Length));
			Directory /= L"QmClient";
			Directory /= L"netease-hook";
			std::filesystem::create_directories(Directory);
			return Directory.wstring();
		}
		catch(...)
		{
			return {};
		}
	}

	std::string CoverFileToken(const std::string &SongId, const std::string &MediaKey)
	{
		std::string Token;
		for(const char C : SongId)
		{
			if((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_')
				Token.push_back(C);
		}
		if(Token.empty())
		{
			char aToken[32];
			std::snprintf(aToken, sizeof(aToken), "%08X", QmNeteaseHook::Crc32(MediaKey.data(), MediaKey.size()));
			Token = aToken;
		}
		return Token;
	}

	bool WriteFileAtomically(const std::wstring &Path, const std::vector<uint8_t> &Data)
	{
		if(Path.empty() || Data.empty())
			return false;
		const std::wstring TemporaryPath = Path + L".tmp";
		HANDLE hFile = CreateFileW(TemporaryPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
		if(hFile == INVALID_HANDLE_VALUE)
			return false;
		bool Success = true;
		size_t Offset = 0;
		while(Offset < Data.size())
		{
			const DWORD Chunk = (DWORD)std::min<size_t>(Data.size() - Offset, 1024 * 1024);
			DWORD Written = 0;
			if(!WriteFile(hFile, Data.data() + Offset, Chunk, &Written, nullptr) || Written != Chunk)
			{
				Success = false;
				break;
			}
			Offset += Written;
		}
		if(Success)
			Success = FlushFileBuffers(hFile) != FALSE;
		CloseHandle(hFile);
		if(!Success)
		{
			DeleteFileW(TemporaryPath.c_str());
			return false;
		}
		if(!MoveFileExW(TemporaryPath.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileW(TemporaryPath.c_str());
			return false;
		}
		return true;
	}

	bool EncodeImageStreamPng(const winrt::Windows::Storage::Streams::IRandomAccessStream &Stream, std::vector<uint8_t> *pBytes)
	{
		if(!Stream || pBytes == nullptr)
			return false;
		try
		{
			const auto DecoderOperation = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(Stream);
			if(!GetAsyncResult(DecoderOperation))
				return false;
			const auto Decoder = DecoderOperation.GetResults();
			if(!Decoder)
				return false;
			uint32_t Width = Decoder.PixelWidth();
			uint32_t Height = Decoder.PixelHeight();
			if(Width == 0 || Height == 0)
				return false;
			if(Width > COVER_MAX_DIMENSION || Height > COVER_MAX_DIMENSION)
			{
				if(Width >= Height)
				{
					Height = std::max<uint32_t>(1, (uint32_t)(((uint64_t)Height * COVER_MAX_DIMENSION + Width / 2) / Width));
					Width = COVER_MAX_DIMENSION;
				}
				else
				{
					Width = std::max<uint32_t>(1, (uint32_t)(((uint64_t)Width * COVER_MAX_DIMENSION + Height / 2) / Height));
					Height = COVER_MAX_DIMENSION;
				}
			}
			winrt::Windows::Graphics::Imaging::BitmapTransform Transform;
			Transform.ScaledWidth(Width);
			Transform.ScaledHeight(Height);
			Transform.InterpolationMode(winrt::Windows::Graphics::Imaging::BitmapInterpolationMode::Fant);
			const auto PixelOperation = Decoder.GetPixelDataAsync(
				winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
				winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
				Transform,
				winrt::Windows::Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,
				winrt::Windows::Graphics::Imaging::ColorManagementMode::DoNotColorManage);
			if(!GetAsyncResult(PixelOperation))
				return false;
			const auto PixelData = PixelOperation.GetResults();
			if(!PixelData)
				return false;
			auto Pixels = PixelData.DetachPixelData();
			const size_t ExpectedPixels = (size_t)Width * Height * 4;
			if(Pixels.size() < ExpectedPixels)
				return false;

			const auto Output = winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
			const auto EncoderOperation = winrt::Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(
				winrt::Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId(), Output);
			if(!GetAsyncResult(EncoderOperation))
				return false;
			const auto Encoder = EncoderOperation.GetResults();
			Encoder.SetPixelData(
				winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
				winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
				Width, Height, 96.0, 96.0,
				winrt::array_view<const uint8_t>(Pixels.data(), Pixels.data() + ExpectedPixels));
			const auto FlushOperation = Encoder.FlushAsync();
			if(!GetAsyncResult(FlushOperation))
				return false;
			const uint64_t OutputSize = Output.Size();
			if(OutputSize == 0 || OutputSize > COVER_MAX_PNG_BYTES || OutputSize > UINT32_MAX)
				return false;
			Output.Seek(0);
			const winrt::Windows::Storage::Streams::DataReader Reader(Output.GetInputStreamAt(0));
			const auto LoadOperation = Reader.LoadAsync((uint32_t)OutputSize);
			if(!GetAsyncResult(LoadOperation))
				return false;
			pBytes->assign((size_t)OutputSize, 0);
			Reader.ReadBytes(winrt::array_view<uint8_t>(pBytes->data(), pBytes->data() + pBytes->size()));
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool EncodeThumbnailPng(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Thumbnail, std::vector<uint8_t> *pBytes)
	{
		if(!Thumbnail || pBytes == nullptr)
			return false;
		try
		{
			const auto StreamOperation = Thumbnail.OpenReadAsync();
			if(!GetAsyncResult(StreamOperation))
				return false;
			return EncodeImageStreamPng(StreamOperation.GetResults(), pBytes);
		}
		catch(...)
		{
			return false;
		}
	}

	bool EncodeCoverUrlPng(const std::string &Url, std::vector<uint8_t> *pBytes)
	{
		if(Url.empty() || pBytes == nullptr)
			return false;
		try
		{
			const std::wstring WideUrl = Utf8StringToWide(Url);
			if(WideUrl.empty())
				return false;
			const winrt::Windows::Web::Http::HttpClient Client;
			const auto BufferOperation = Client.GetBufferAsync(winrt::Windows::Foundation::Uri(winrt::hstring(WideUrl)));
			if(!GetAsyncResult(BufferOperation))
				return false;
			const auto Buffer = BufferOperation.GetResults();
			if(!Buffer || Buffer.Length() == 0 || Buffer.Length() > COVER_MAX_DOWNLOAD_BYTES)
				return false;
			const winrt::Windows::Storage::Streams::InMemoryRandomAccessStream Stream;
			const winrt::Windows::Storage::Streams::DataWriter Writer(Stream);
			Writer.WriteBuffer(Buffer);
			const auto StoreOperation = Writer.StoreAsync();
			if(!GetAsyncResult(StoreOperation))
				return false;
			Stream.Seek(0);
			return EncodeImageStreamPng(Stream, pBytes);
		}
		catch(...)
		{
			return false;
		}
	}

	void ClearCoverCache()
	{
		std::scoped_lock Lock(g_CoverMutex);
		g_CoverKey.clear();
		g_CoverPath.clear();
		g_CoverUrl.clear();
		g_CoverSongId.clear();
		g_CoverLastAttemptTick = 0;
		++g_CoverRequestedSerial;
	}

	void UpdateCoverCache(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Thumbnail, const std::string &MediaKey, const std::string &SongId)
	{
		if(MediaKey.empty())
			return;
		bool NeedUpdate = false;
		const ULONGLONG Now = GetTickCount64();
		{
			std::scoped_lock Lock(g_CoverMutex);
			if(g_CoverKey != MediaKey)
			{
				g_CoverKey = MediaKey;
				g_CoverPath.clear();
				g_CoverUrl.clear();
				g_CoverSongId = SongId;
				g_CoverLastAttemptTick = 0;
				++g_CoverRequestedSerial;
				NeedUpdate = true;
			}
			else if(g_CoverPath.empty() && (g_CoverLastAttemptTick == 0 || Now - g_CoverLastAttemptTick >= 1000))
			{
				NeedUpdate = true;
			}
			if(NeedUpdate)
				g_CoverLastAttemptTick = Now;
		}
		if(!NeedUpdate || !Thumbnail)
			return;
		std::vector<uint8_t> PngBytes;
		if(!EncodeThumbnailPng(Thumbnail, &PngBytes))
			return;
		const std::wstring Directory = CoverCacheDirectory();
		if(Directory.empty())
			return;
		const std::string Token = CoverFileToken(SongId, MediaKey);
		const std::wstring Path = (std::filesystem::path(Directory) / (L"cover-" + std::wstring(Token.begin(), Token.end()) + L".png")).wstring();
		if(!WriteFileAtomically(Path, PngBytes))
			return;
		const std::string Utf8Path = WideStringToUtf8(Path);
		if(Utf8Path.empty())
			return;
		std::scoped_lock Lock(g_CoverMutex);
		if(g_CoverKey == MediaKey)
			g_CoverPath = Utf8Path;
	}

	struct SCoverFetchRequest
	{
		uint64_t m_Serial = 0;
		std::string m_MediaKey;
		std::string m_SongId;
		std::string m_Url;
	};

	bool TakeCoverFetchRequest(SCoverFetchRequest *pRequest)
	{
		if(pRequest == nullptr)
			return false;
		std::scoped_lock Lock(g_CoverMutex);
		if(g_CoverHandledSerial == g_CoverRequestedSerial)
		{
			g_CoverFetchScheduled = false;
			return false;
		}
		g_CoverHandledSerial = g_CoverRequestedSerial;
		pRequest->m_Serial = g_CoverHandledSerial;
		pRequest->m_MediaKey = g_CoverKey;
		pRequest->m_SongId = g_CoverSongId;
		pRequest->m_Url = g_CoverUrl;
		return true;
	}

	void CompleteCoverFetchRequest(const SCoverFetchRequest &Request, const std::vector<uint8_t> &PngBytes)
	{
		if(Request.m_MediaKey.empty() || PngBytes.empty())
			return;
		const std::wstring Directory = CoverCacheDirectory();
		if(Directory.empty())
			return;
		const std::string Token = CoverFileToken(Request.m_SongId, Request.m_MediaKey);
		const std::wstring Path = (std::filesystem::path(Directory) / (L"cover-" + std::wstring(Token.begin(), Token.end()) + L".png")).wstring();
		if(!WriteFileAtomically(Path, PngBytes))
			return;
		const std::string Utf8Path = WideStringToUtf8(Path);
		if(Utf8Path.empty())
			return;
		std::scoped_lock Lock(g_CoverMutex);
		if(g_CoverKey == Request.m_MediaKey && g_CoverUrl == Request.m_Url)
			g_CoverPath = Utf8Path;
	}

	void CALLBACK CoverFetchWorkCallback(PTP_CALLBACK_INSTANCE, void *)
	{
		bool ApartmentInitialized = false;
		try
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			ApartmentInitialized = true;
			while(!IsWorkCancelled())
			{
				SCoverFetchRequest Request;
				if(!TakeCoverFetchRequest(&Request))
					break;
				if(Request.m_MediaKey.empty() || Request.m_Url.empty())
					continue;
				std::vector<uint8_t> PngBytes;
				if(EncodeCoverUrlPng(Request.m_Url, &PngBytes) && !IsWorkCancelled())
					CompleteCoverFetchRequest(Request, PngBytes);
			}
		}
		catch(...)
		{
		}
		if(ApartmentInitialized)
			winrt::uninit_apartment();
		std::scoped_lock Lock(g_CoverMutex);
		g_CoverFetchScheduled = false;
	}

	void ScheduleCoverUrlCache(const std::string &MediaKey, const std::string &SongId, const std::string &Url)
	{
		if(MediaKey.empty() || Url.empty())
			return;
		bool SubmitWork = false;
		const ULONGLONG Now = GetTickCount64();
		{
			std::scoped_lock Lock(g_CoverMutex);
			if(g_CoverKey != MediaKey || g_CoverUrl != Url)
			{
				g_CoverKey = MediaKey;
				g_CoverPath.clear();
				g_CoverUrl = Url;
				g_CoverSongId = SongId;
				g_CoverLastAttemptTick = 0;
			}
			if(g_CoverPath.empty() && (g_CoverLastAttemptTick == 0 || Now - g_CoverLastAttemptTick >= 1000))
			{
				g_CoverLastAttemptTick = Now;
				++g_CoverRequestedSerial;
				if(!g_CoverFetchScheduled)
				{
					g_CoverFetchScheduled = true;
					SubmitWork = true;
				}
			}
		}
		if(SubmitWork && !TrySubmitThreadpoolCallback(CoverFetchWorkCallback, nullptr, nullptr))
		{
			std::scoped_lock Lock(g_CoverMutex);
			g_CoverFetchScheduled = false;
		}
	}

	std::string LowerAscii(std::string Value)
	{
		for(char &C : Value)
			if(C >= 'A' && C <= 'Z')
				C = (char)(C - 'A' + 'a');
		return Value;
	}

	bool IsNeteaseSession(const GlobalSystemMediaTransportControlsSession &Session)
	{
		try
		{
			const std::string SourceAppId = LowerAscii(winrt::to_string(Session.SourceAppUserModelId()));
			return SourceAppId.empty() || SourceAppId.find("cloudmusic") != std::string::npos || SourceAppId.find("netease") != std::string::npos;
		}
		catch(...)
		{
			return true;
		}
	}

	void ClearCapturedText()
	{
		std::scoped_lock Lock(g_TextMutex);
		g_LastRenderedText.clear();
		g_LastTextTick.store(0, std::memory_order_release);
	}

	void ApplyWaveOutTimeline(QmNeteaseHook::SSnapshot *pSnapshot)
	{
		if(pSnapshot == nullptr || !g_WaveOutHasPosition.load(std::memory_order_acquire))
			return;
		const int64_t PositionMs = std::max<int64_t>(0, g_WaveOutPositionMs.load(std::memory_order_acquire));
		pSnapshot->m_PositionMs = PositionMs;
		if(pSnapshot->m_DurationMs > 0 && pSnapshot->m_PositionMs > pSnapshot->m_DurationMs)
			pSnapshot->m_PositionMs = pSnapshot->m_DurationMs;
		pSnapshot->m_ObservedQpc = g_WaveOutObservedQpc.load(std::memory_order_acquire);
		pSnapshot->m_TimelineGeneration = g_WaveOutTimelineGeneration.load(std::memory_order_acquire);
		const uint64_t ObservedTick = g_WaveOutObservedTick.load(std::memory_order_acquire);
		if(ObservedTick != 0 && GetTickCount64() - ObservedTick <= WAVE_OUT_POSITION_STALE_MS && g_WaveOutPlaying.load(std::memory_order_acquire))
			pSnapshot->m_Status |= QmNeteaseHook::STATUS_PLAYING;
	}

	void TryPopulateFromSmtcFallback(QmNeteaseHook::SSnapshot *pSnapshot, std::string *pMediaKey, std::string *pCoverMediaKey)
	{
		if(pSnapshot == nullptr || pMediaKey == nullptr || pCoverMediaKey == nullptr)
			return;
		try
		{
			const auto ManagerOperation = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
			if(!GetAsyncResult(ManagerOperation))
				return;
			const auto Manager = ManagerOperation.GetResults();
			auto Session = Manager.GetCurrentSession();
			if(!Session || !IsNeteaseSession(Session))
			{
				Session = nullptr;
				for(const auto &Candidate : Manager.GetSessions())
				{
					if(IsNeteaseSession(Candidate))
					{
						Session = Candidate;
						break;
					}
				}
			}
			if(!Session)
				return;
			const auto Timeline = Session.GetTimelineProperties();
			const auto PropertiesOperation = Session.TryGetMediaPropertiesAsync();
			if(!GetAsyncResult(PropertiesOperation))
				return;
			const auto Properties = PropertiesOperation.GetResults();
			if(!Properties)
				return;
			const std::string Title = winrt::to_string(Properties.Title());
			const std::string Artist = winrt::to_string(Properties.Artist());
			if(Title.empty())
				return;

			strncpy_s(pSnapshot->m_aTitle, Title.c_str(), _TRUNCATE);
			strncpy_s(pSnapshot->m_aArtist, Artist.c_str(), _TRUNCATE);
			strncpy_s(pSnapshot->m_aAlbum, winrt::to_string(Properties.AlbumTitle()).c_str(), _TRUNCATE);
			for(const auto &Genre : Properties.Genres())
			{
				const std::string Value = winrt::to_string(Genre);
				if(Value.rfind("NCM-", 0) == 0)
					strncpy_s(pSnapshot->m_aSongId, Value.substr(4).c_str(), _TRUNCATE);
			}
			if(Timeline)
			{
				const int64_t Duration100ns = std::max<int64_t>(0, Timeline.EndTime().count() - Timeline.StartTime().count());
				pSnapshot->m_DurationMs = Duration100ns / 10000;
			}

			SLocalTrackMetadata LocalTrack;
			if(FindLocalTrack(Title, Artist, pSnapshot->m_DurationMs, &LocalTrack))
			{
				if(pSnapshot->m_aSongId[0] == '\0' && !LocalTrack.m_SongId.empty())
					strncpy_s(pSnapshot->m_aSongId, LocalTrack.m_SongId.c_str(), _TRUNCATE);
				if(pSnapshot->m_aAlbum[0] == '\0' && !LocalTrack.m_Album.empty())
					strncpy_s(pSnapshot->m_aAlbum, LocalTrack.m_Album.c_str(), _TRUNCATE);
				if(LocalTrack.m_DurationMs > 0)
				{
					pSnapshot->m_DurationMs = LocalTrack.m_DurationMs;
					if(pSnapshot->m_PositionMs > pSnapshot->m_DurationMs)
						pSnapshot->m_PositionMs = pSnapshot->m_DurationMs;
				}
				if(!LocalTrack.m_CoverUrl.empty())
					strncpy_s(pSnapshot->m_aCoverUrl, LocalTrack.m_CoverUrl.c_str(), _TRUNCATE);
			}
			pSnapshot->m_SongId = QmNeteaseHook::ParseLocalTrackSongId(pSnapshot->m_aSongId);
			pSnapshot->m_Status |= QmNeteaseHook::STATUS_HAS_MEDIA;
			if(pSnapshot->m_aCoverUrl[0] != '\0')
				pSnapshot->m_Status |= QmNeteaseHook::STATUS_HAS_COVER;
			*pMediaKey = std::string(pSnapshot->m_aSongId) + '\n' + pSnapshot->m_aTitle + '\n' + pSnapshot->m_aArtist + '\n' + pSnapshot->m_aAlbum;
			*pCoverMediaKey = *pMediaKey;
			UpdateCoverCache(Properties.Thumbnail(), *pCoverMediaKey, pSnapshot->m_aSongId);
		}
		catch(...)
		{
		}
	}

	void QueryAndPublish()
	{
		QmNeteaseHook::SSnapshot Snapshot{};
		Snapshot.m_ProducerPid = GetCurrentProcessId();
		// 本地播放列表和 WinMM 均来自被注入的网易云进程，不能再以 SMTC 是否可用作为前置条件。
		Snapshot.m_Status = QmNeteaseHook::STATUS_SOURCE_DIRECT;
		Snapshot.m_PlaybackRate = 1.0;
		std::string MediaKey;
		std::string CoverMediaKey;
		SLocalTrackMetadata LocalTrack;
		if(FindCurrentLocalTrack(&LocalTrack) && QmNeteaseHook::PopulateSnapshotFromLocalTrack(&Snapshot, LocalTrack))
		{
			MediaKey = QmNeteaseHook::LocalTrackMediaKey(LocalTrack);
			CoverMediaKey = MediaKey;
			ScheduleCoverUrlCache(MediaKey, LocalTrack.m_SongId, LocalTrack.m_CoverUrl);
		}
		else
		{
			// 仅兼容旧系统：本地播放列表暂时不可用时才读取 SMTC，绝不影响直连路径。
			TryPopulateFromSmtcFallback(&Snapshot, &MediaKey, &CoverMediaKey);
		}
		// 播放状态和位置只接受 WinMM 的真实 API 采样，不能由本地计时器或 SMTC 推测。
		ApplyWaveOutTimeline(&Snapshot);

		static std::string LastMediaKey;
		if((Snapshot.m_Status & QmNeteaseHook::STATUS_HAS_MEDIA) != 0 && !MediaKey.empty())
		{
			if(MediaKey != LastMediaKey)
				ClearCapturedText();
			LastMediaKey = MediaKey;
		}
		else if((Snapshot.m_Status & QmNeteaseHook::STATUS_HAS_MEDIA) == 0)
		{
			LastMediaKey.clear();
			ClearCoverCache();
		}

		std::string CoverPath;
		if((Snapshot.m_Status & QmNeteaseHook::STATUS_HAS_MEDIA) != 0 && !CoverMediaKey.empty())
		{
			std::scoped_lock Lock(g_CoverMutex);
			if(g_CoverKey == CoverMediaKey)
				CoverPath = g_CoverPath;
		}
		if(!CoverPath.empty())
		{
			strncpy_s(Snapshot.m_aCoverPath, CoverPath.c_str(), _TRUNCATE);
			Snapshot.m_Status |= QmNeteaseHook::STATUS_HAS_COVER;
		}
		if(Snapshot.m_aCoverUrl[0] != '\0')
			Snapshot.m_Status |= QmNeteaseHook::STATUS_HAS_COVER;

		std::string Text;
		{
			std::scoped_lock Lock(g_TextMutex);
			const uint64_t LastText = g_LastTextTick.load(std::memory_order_acquire);
			if(!g_LastRenderedText.empty() && LastText != 0 && GetTickCount64() - LastText <= TEXT_STALE_MS)
				Text = g_LastRenderedText;
		}
		if(!Text.empty() && (Snapshot.m_Status & QmNeteaseHook::STATUS_HAS_MEDIA) != 0)
		{
			strncpy_s(Snapshot.m_aCurrentLine, Text.c_str(), _TRUNCATE);
			// GDI+ 只提供当前绘制句；真实边界由 QmClient 网络时间轴匹配，不能伪造成固定 1.5 秒。
			Snapshot.m_CurrentLineStartMs = -1;
			Snapshot.m_CurrentLineEndMs = -1;
			Snapshot.m_Status |= QmNeteaseHook::STATUS_HAS_CURRENT_LINE;
		}
		Publish(Snapshot);
		PublishV5Fallback(Snapshot);
	}

	void AcknowledgeStop()
	{
		if(g_pShared == nullptr)
			return;
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&g_pShared->m_ActiveSequence), 0);
		MemoryBarrier();
		InterlockedOr(reinterpret_cast<volatile LONG *>(&g_pShared->m_ControlFlags), (LONG)QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED);
	}

	void ClearStopAcknowledgement()
	{
		if(g_pShared != nullptr)
			InterlockedAnd(reinterpret_cast<volatile LONG *>(&g_pShared->m_ControlFlags), ~(LONG)QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED);
	}

	void EnterDormantState()
	{
		SuspendHooks();
		ClearCapturedText();
		ClearCoverCache();
		AcknowledgeStop();
	}

	void WorkerMain()
	{
		bool ApartmentInitialized = false;
		bool Dormant = false;
		try
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			ApartmentInitialized = true;
			g_pV5Writer = std::make_unique<QmNeteaseHook::CV5Writer>();
			g_V5WriterReady.store(g_pV5Writer->Open(true), std::memory_order_release);
			g_hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(QmNeteaseHook::SSharedBlock), SHARED_MAPPING_NAME);
			if(g_hMapping != nullptr)
			{
				g_pShared = static_cast<QmNeteaseHook::SSharedBlock *>(MapViewOfFile(g_hMapping, FILE_MAP_WRITE, 0, 0, sizeof(QmNeteaseHook::SSharedBlock)));
				if(g_pShared != nullptr)
					std::memset(g_pShared, 0, sizeof(*g_pShared));
			}
			while(!IsStopRequested())
			{
				if(IsControlStopRequested())
				{
					if(!Dormant)
					{
						EnterDormantState();
						Dormant = true;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}
				if(Dormant)
				{
					ClearStopAcknowledgement();
					Dormant = false;
				}
				if(!g_HooksInstalled.load(std::memory_order_acquire) && !InstallHooks())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					continue;
				}
				QueryAndPublish();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		catch(...)
		{
		}
		// Helper 不会卸载 DLL；进程退出时由系统回收 Hook 资源。这里不能释放
		// trampoline，否则并发的网易云渲染线程可能仍会从中返回。
		SuspendHooks();
		if(IsControlStopRequested())
			AcknowledgeStop();
		if(g_pV5Writer)
		{
			g_pV5Writer->Close();
			g_pV5Writer.reset();
		}
		g_V5WriterReady.store(false, std::memory_order_release);
		if(ApartmentInitialized)
			winrt::uninit_apartment();
	}

	DWORD WINAPI BootstrapThread(void *)
	{
		for(int Attempt = 0; Attempt < 100 && !IsStopRequested() && GetModuleHandleW(L"cloudmusic.dll") == nullptr; ++Attempt)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if(!IsStopRequested())
			WorkerMain();
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD Reason, LPVOID lpReserved)
{
	if(Reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		g_Stop.store(false, std::memory_order_release);
		HANDLE hWorkerThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
		if(hWorkerThread != nullptr)
			CloseHandle(hWorkerThread);
		else
			return FALSE;
	}
	else if(Reason == DLL_PROCESS_DETACH)
	{
		g_Stop.store(true, std::memory_order_release);
		// loader lock 下不能等待工作线程；进程退出时由系统回收其句柄和映射。
	}
	return TRUE;
}
