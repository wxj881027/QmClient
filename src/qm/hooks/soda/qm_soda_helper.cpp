// 汽水音乐 Hook Helper:发现 SodaMusic.exe 主进程,激活 Node inspector(9229),
// 经主进程 executeJavaScript 桥进 rendererMain 提取 sharedState 播放态(整首 KRC 歌词
// + 逐字时间轴 + 翻译轨),解析后输出。复用网易云 CDP 会话与歌词模型。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "qm_soda_probe.h"
#include "qm_soda_protocol.h"
#include "qm_soda_watchdog.h"
#include "qm_soda_writer.h"

#include <windows.h>

#include <qm/hooks/netease/qm_netease_cdp.h>
#include <qm/hooks/netease/qm_netease_cdp_client.h>
#include <qm/hooks/netease/qm_netease_cdp_io.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace
{
	constexpr wchar_t HELPER_MUTEX_NAME[] = L"Local\\QmClient.SodaHook.Helper.v1";
	constexpr int EXTRACT_INTERVAL_MS = 300;
	constexpr int EXTRACT_TIMEOUT_MS = 8000;
	using THandle = std::unique_ptr<void, decltype(&CloseHandle)>;
	using TInternetHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

	// 简单文件日志:stdout 重定向在 Windows 上缓冲不可靠,真机诊断写文件。
	// 路径: %LOCALAPPDATA%\QmClient\soda-hook\helper.log
	void Log(const char *pFormat, ...)
	{
		wchar_t aPath[32768] = {};
		const DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= (DWORD)std::size(aPath))
			return;
		try
		{
			std::filesystem::path Directory(std::wstring(aPath, Length));
			Directory /= L"QmClient";
			Directory /= L"soda-hook";
			std::filesystem::create_directories(Directory);
			HANDLE hFile = CreateFileW((Directory / L"helper.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(hFile == INVALID_HANDLE_VALUE)
				return;
			char aBuffer[2048];
			va_list Args;
			va_start(Args, pFormat);
			_vsnprintf_s(aBuffer, sizeof(aBuffer), _TRUNCATE, pFormat, Args);
			va_end(Args);
			DWORD Written = 0;
			WriteFile(hFile, aBuffer, (DWORD)std::strlen(aBuffer), &Written, nullptr);
			CloseHandle(hFile);
		}
		catch(...)
		{
		}
	}

	// 歌词数据文件目录: %LOCALAPPDATA%\QmClient\soda-hook
	std::wstring LyricDataDirectory()
	{
		wchar_t aPath[32768] = {};
		const DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= (DWORD)std::size(aPath))
			return {};
		try
		{
			std::filesystem::path Directory(std::wstring(aPath, Length));
			Directory /= L"QmClient";
			Directory /= L"soda-hook";
			std::filesystem::create_directories(Directory);
			return Directory.wstring();
		}
		catch(...)
		{
			return {};
		}
	}

	std::string LyricFileToken(const std::string &MediaId, const std::string &Title)
	{
		std::string Token;
		for(const char C : MediaId)
		{
			if((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_')
				Token.push_back(C);
		}
		if(Token.empty())
		{
			uint32_t Hash = 2166136261U;
			for(const char C : Title)
				Hash = (Hash ^ (uint8_t)C) * 16777619U;
			char aBuf[16];
			std::snprintf(aBuf, sizeof(aBuf), "%08X", Hash);
			Token = aBuf;
		}
		return Token;
	}

	bool WriteFileAtomically(const std::wstring &Path, const std::string &Data)
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

	std::string JsonEscape(std::string_view Value)
	{
		std::string Result;
		Result.reserve(Value.size() + 8);
		for(const unsigned char Byte : Value)
		{
			switch(Byte)
			{
			case '"': Result += "\\\""; break;
			case '\\': Result += "\\\\"; break;
			case '\b': Result += "\\b"; break;
			case '\f': Result += "\\f"; break;
			case '\n': Result += "\\n"; break;
			case '\r': Result += "\\r"; break;
			case '\t': Result += "\\t"; break;
			default:
				if(Byte < 0x20)
				{
					char aHex[7];
					std::snprintf(aHex, sizeof(aHex), "\\u%04x", Byte);
					Result += aHex;
				}
				else
					Result.push_back((char)Byte);
				break;
			}
		}
		return Result;
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

	// 把播放态快照序列化为 JSON 歌词数据文件,返回文件路径(UTF-8)。
	// 结构: {"mediaId","title","artist","album","coverUrl","durationMs","positionMs",
	//         "isPlaying","lyricType","lyricContent","translationLrc"}
	std::string WriteLyricDataFile(const QmSodaProbe::SPlaybackSnapshot &Snapshot)
	{
		const std::wstring Directory = LyricDataDirectory();
		if(Directory.empty())
			return {};
		const std::string Token = LyricFileToken(Snapshot.m_MediaId, Snapshot.m_Name);
		const std::wstring Path = (std::filesystem::path(Directory) / (L"lyrics-" + std::wstring(Token.begin(), Token.end()) + L".json")).wstring();
		std::string Utf8Path = WideStringToUtf8(Path);
		// 客户端必须收到可打开的完整路径。
		if(Utf8Path.empty() || Utf8Path.size() >= QmSodaHook::MAX_LYRIC_FILE_PATH_BYTES)
			return {};
		// 字符串字段统一带引号;JsonEscape 只处理内容转义。
		const auto Str = [](std::string_view Value) { return "\"" + JsonEscape(Value) + "\""; };
		std::string Json = "{\"mediaId\":" + Str(Snapshot.m_MediaId) +
				   ",\"title\":" + Str(Snapshot.m_Name) +
				   ",\"artist\":" + Str(Snapshot.m_Artist) +
				   ",\"album\":" + Str(Snapshot.m_Album) +
				   ",\"coverUrl\":" + Str(Snapshot.m_CoverUrl) +
				   ",\"durationMs\":" + std::to_string(Snapshot.DurationMs()) +
				   ",\"positionMs\":" + std::to_string(Snapshot.PositionMs()) +
				   ",\"isPlaying\":" + (Snapshot.m_IsPlaying ? "true" : "false") +
				   ",\"lyricType\":" + Str(Snapshot.m_LyricType) +
				   ",\"lyricContent\":" + Str(Snapshot.m_LyricContent) +
				   ",\"translationLrc\":" + Str(Snapshot.m_TranslationLrc) +
				   "}";
		if(!WriteFileAtomically(Path, Json))
			return {};
		return Utf8Path;
	}

	bool HttpGetJsonList(std::string &Body)
	{
		using namespace QmNeteaseCdp;
		const CIoBudget Budget(GetTickCount64(), 1500, nullptr);
		const auto Allowed = [&] { return Budget.Remaining(GetTickCount64()) > 0; };
		const TInternetHandle Session(WinHttpOpen(L"QmClient-Soda/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0), WinHttpCloseHandle);
		if(!Session)
			return false;
		const TInternetHandle Connect(WinHttpConnect(Session.get(), L"127.0.0.1", QmSodaWatchdog::INSPECTOR_PORT, 0), WinHttpCloseHandle);
		const TInternetHandle Request(Connect ? WinHttpOpenRequest(Connect.get(), L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr, WinHttpCloseHandle);
		if(!Request)
			return false;
		const auto PrepareWait = [&] {
			const int Remaining = Budget.Remaining(GetTickCount64());
			return Remaining > 0 && WinHttpSetTimeouts(Request.get(), std::min(250, Remaining), std::min(250, Remaining), std::min(500, Remaining), std::min(500, Remaining));
		};
		DWORD RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		if(!WinHttpSetOption(Request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &RedirectPolicy, sizeof(RedirectPolicy)) ||
			!PrepareWait() || !WinHttpSendRequest(Request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!PrepareWait() || !WinHttpReceiveResponse(Request.get(), nullptr))
			return false;
		DWORD Status = 0, StatusSize = sizeof(Status);
		if(!Allowed() || !WinHttpQueryHeaders(Request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX) || Status != 200)
			return false;
		return ReadCdpHttpBody(Body, MAX_TARGET_JSON_BYTES, Allowed, [&](void *pOutput, size_t Capacity) -> SIoChunk {
			DWORD Read = 0;
			if(!PrepareWait() || !WinHttpReadData(Request.get(), pOutput, static_cast<DWORD>(Capacity), &Read))
				return {EChunkStatus::FAILED, 0};
			return {Read == 0 ? EChunkStatus::END : EChunkStatus::DATA, Read};
		});
	}

	void PrintSnapshot(const QmSodaProbe::SPlaybackSnapshot &Snapshot)
	{
		Log("[soda] ok=%d mediaId=%s name=%s playing=%d progress=%.1f duration=%.1f lyricType=%s lyricBytes=%zu translationBytes=%zu\n",
			Snapshot.m_Ok ? 1 : 0,
			Snapshot.m_MediaId.c_str(),
			Snapshot.m_Name.c_str(),
			Snapshot.m_IsPlaying ? 1 : 0,
			Snapshot.m_ProgressSeconds,
			Snapshot.m_DurationSeconds,
			Snapshot.m_LyricType.c_str(),
			Snapshot.m_LyricContent.size(),
			Snapshot.m_TranslationLrc.size());
	}

	bool ParentRunning(HANDLE hParent)
	{
		return hParent == nullptr || WaitForSingleObject(hParent, 0) == WAIT_TIMEOUT;
	}

	void PublishPlayback(const QmSodaProbe::SPlaybackSnapshot &Snapshot, uint32_t Pid, QmSodaHook::CSodaWriter &Writer, QmSodaProbe::CLyricFilePublication &Lyrics)
	{
		using namespace QmSodaHook;
		SSnapshot Shared{};
		Shared.m_SodaMusicPid = Pid;
		if(Snapshot.m_IsPlaying)
			Shared.m_Flags |= FLAG_PLAYING;
		if(Snapshot.m_IsLoading)
			Shared.m_Flags |= FLAG_LOADING;
		if(Snapshot.m_Throttled)
			Shared.m_Flags |= FLAG_THROTTLED;
		const int64_t PositionMs = Snapshot.PositionMs();
		if(PositionMs >= 0)
		{
			Shared.m_PositionMs = PositionMs;
			Shared.m_Flags |= FLAG_POSITION_VALID;
		}
		Shared.m_DurationMs = Snapshot.DurationMs();
		CopyUtf8Truncated(Shared.m_aMediaId, sizeof(Shared.m_aMediaId), Snapshot.m_MediaId.data(), Snapshot.m_MediaId.size());
		CopyUtf8Truncated(Shared.m_aTitle, sizeof(Shared.m_aTitle), Snapshot.m_Name.data(), Snapshot.m_Name.size());
		CopyUtf8Truncated(Shared.m_aAlbum, sizeof(Shared.m_aAlbum), Snapshot.m_Album.data(), Snapshot.m_Album.size());
		CopyUtf8Truncated(Shared.m_aCoverUrl, sizeof(Shared.m_aCoverUrl), Snapshot.m_CoverUrl.data(), Snapshot.m_CoverUrl.size());
		if(Shared.m_aMediaId[0] != '\0')
		{
			Shared.m_Flags |= FLAG_HAS_SONG;
			Lyrics.Update(Snapshot, GetTickCount64(), WriteLyricDataFile);
			const auto &Path = Lyrics.Path();
			if(!Path.empty())
			{
				CopyUtf8Truncated(Shared.m_aLyricFilePath, sizeof(Shared.m_aLyricFilePath), Path.data(), Path.size());
				Shared.m_Flags |= FLAG_HAS_LYRIC_FILE;
			}
		}
		Shared.m_Generation = Lyrics.Generation();
		if(Shared.m_Flags != 0 && (Writer.IsOpen() || Writer.Open(true)))
		{
			Shared.m_UpdatedAtTick = GetTickCount64();
			Writer.Publish(Shared);
		}
	}

	int Watch(DWORD ParentPid)
	{
		const THandle Parent(ParentPid != 0 ? OpenProcess(SYNCHRONIZE, FALSE, ParentPid) : nullptr, CloseHandle);
		if(ParentPid != 0 && !Parent)
			return 4;
		QmSodaHook::CSodaWriter Writer;
		if(!Writer.Open(true))
			Log("[soda] shared memory writer unavailable\n");
		uint32_t ActivePid = 0;
		QmSodaProbe::CLyricFilePublication Lyrics;
		bool LoggedNoProcess = false;
		while(ParentRunning(Parent.get()))
		{
			try
			{
				const uint32_t Pid = QmSodaWatchdog::FindMainPid();
				if(Pid == 0)
				{
					if(ActivePid != 0)
					{
						Log("[soda] SodaMusic exited\n");
						ActivePid = 0;
						Lyrics.Reset();
					}
					else if(!LoggedNoProcess)
					{
						Log("[soda] no SodaMusic main process found\n");
						LoggedNoProcess = true;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(3000));
					continue;
				}
				LoggedNoProcess = false;
				if(ActivePid != Pid)
				{
					Log("[soda] found SodaMusic pid=%u\n", Pid);
					ActivePid = Pid;
					Lyrics.Reset();
				}
				uint32_t ListenerPid = QmSodaWatchdog::InspectorListenerPid();
				if(ListenerPid == 0)
				{
					if(!QmSodaWatchdog::EnsureInspector(Pid))
					{
						Log("[soda] inspector activation failed for pid=%u (SodaMusic 以管理员权限运行? 请用普通权限启动)\n", Pid);
						std::this_thread::sleep_for(std::chrono::milliseconds(3000));
						continue;
					}
					const uint64_t Deadline = GetTickCount64() + 3000;
					do
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(150));
						ListenerPid = QmSodaWatchdog::InspectorListenerPid();
					} while(ListenerPid == 0 && GetTickCount64() < Deadline && ParentRunning(Parent.get()));
					if(ListenerPid == Pid)
						Log("[soda] inspector ready on port %u\n", (unsigned)QmSodaWatchdog::INSPECTOR_PORT);
				}
				if(ListenerPid != Pid)
				{
					// 同端口上的其它 Node 进程不属于汽水。
					std::this_thread::sleep_for(std::chrono::milliseconds(3000));
					continue;
				}
				if(!ParentRunning(Parent.get()))
					break;
				std::string ListJson;
				if(!HttpGetJsonList(ListJson))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				const std::string WsUrl = QmSodaProbe::PickNodeTarget(ListJson, QmSodaWatchdog::INSPECTOR_PORT);
				if(WsUrl.empty() || QmSodaWatchdog::InspectorListenerPid() != Pid)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				if(!ParentRunning(Parent.get()))
					break;
				QmNeteaseCdp::CCdpSession Session;
				if(!Session.Connect(WsUrl) || QmSodaWatchdog::InspectorListenerPid() != Pid)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				Log("[soda] CDP connected\n");
				// 会话期内持续提取;连接断开后重连。
				while(ParentRunning(Parent.get()))
				{
					std::string Value;
					if(!Session.EvaluateAwaitPromise(QmSodaProbe::BuildBridgeExpression(), &Value, EXTRACT_TIMEOUT_MS))
						break;
					QmSodaProbe::SPlaybackSnapshot Snapshot;
					if(QmSodaProbe::ParseExtractionJson(Value, &Snapshot))
					{
						PrintSnapshot(Snapshot);
						PublishPlayback(Snapshot, Pid, Writer, Lyrics);
					}
					else if(!Snapshot.m_Error.empty() && Snapshot.m_Error != "no-port")
						Log("[soda] extract: %s\n", Snapshot.m_Error.c_str());
					std::this_thread::sleep_for(std::chrono::milliseconds(EXTRACT_INTERVAL_MS));
				}
				Session.Close();
				Writer.Close();
				if(ParentRunning(Parent.get()) && !Writer.Open(true))
					Log("[soda] shared memory writer unavailable (reopen)\n");
			}
			catch(const std::bad_alloc &)
			{
				// 会话与系统句柄已释放，保留曲目缓存并重新发现本地目标。
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
			catch(const std::length_error &)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
		}
		return 0;
	}
}

int wmain(int argc, wchar_t **argv)
try
{
	// 重定向到文件/管道时 printf 默认全缓冲,真机诊断看不到进度;改无缓冲。
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	Log("[soda] helper start argc=%d\n", argc);
	const THandle Mutex(CreateMutexW(nullptr, FALSE, HELPER_MUTEX_NAME), CloseHandle);
	Log("[soda] mutex handle=%p lastError=%lu\n", Mutex.get(), (unsigned long)GetLastError());
	if(!Mutex)
		return 2;
	// 命名 mutex 在持有者崩溃后会变成 abandoned,但对象仍存在:此时
	// GetLastError 是 ERROR_ALREADY_EXISTS,句柄却可正常获取。因此用
	// WaitForSingleObject 判断是否有活跃实例,而不是只看 ERROR_ALREADY_EXISTS。
	const DWORD MutexWait = WaitForSingleObject(Mutex.get(), 0);
	if(MutexWait == WAIT_TIMEOUT)
	{
		Log("[soda] another helper instance is running\n");
		return 0;
	}
	if(MutexWait != WAIT_OBJECT_0 && MutexWait != WAIT_ABANDONED)
		return 2;
	struct SReleaseMutex
	{
		HANDLE m_Handle;
		~SReleaseMutex() { ReleaseMutex(m_Handle); }
	};
	const SReleaseMutex Release{Mutex.get()};
	DWORD ParentPid = 0;
	for(int i = 1; i + 1 < argc; ++i)
	{
		if(std::wcscmp(argv[i], L"--parent-pid") == 0)
			ParentPid = (DWORD)_wtoi(argv[i + 1]);
	}
	Log("[soda] parent-pid=%lu\n", (unsigned long)ParentPid);
	return Watch(ParentPid);
}
catch(const std::bad_alloc &)
{
	return 3;
}
catch(const std::length_error &)
{
	return 3;
}
