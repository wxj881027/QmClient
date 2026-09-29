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

#include <engine/external/json-parser/json.h>

#include <windows.h>

#include <qm/hooks/netease/qm_netease_cdp_client.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	constexpr wchar_t HELPER_MUTEX_NAME[] = L"Local\\QmClient.SodaHook.Helper.v1";
	constexpr int EXTRACT_INTERVAL_MS = 300;
	constexpr int EXTRACT_TIMEOUT_MS = 8000;

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
	std::wstring WriteLyricDataFile(const QmSodaProbe::SPlaybackSnapshot &Snapshot)
	{
		const std::wstring Directory = LyricDataDirectory();
		if(Directory.empty())
			return {};
		const std::string Token = LyricFileToken(Snapshot.m_MediaId, Snapshot.m_Name);
		const std::wstring Path = (std::filesystem::path(Directory) / (L"lyrics-" + std::wstring(Token.begin(), Token.end()) + L".json")).wstring();
		// 字符串字段统一带引号;JsonEscape 只处理内容转义。
		const auto Str = [](std::string_view Value) { return "\"" + JsonEscape(Value) + "\""; };
		std::string Json = "{\"mediaId\":" + Str(Snapshot.m_MediaId) +
				   ",\"title\":" + Str(Snapshot.m_Name) +
				   ",\"artist\":" + Str(Snapshot.m_Artist) +
				   ",\"album\":" + Str(Snapshot.m_Album) +
				   ",\"coverUrl\":" + Str(Snapshot.m_CoverUrl) +
				   ",\"durationMs\":" + std::to_string((long long)(Snapshot.m_DurationSeconds * 1000)) +
				   ",\"positionMs\":" + std::to_string((long long)(Snapshot.m_ProgressSeconds * 1000)) +
				   ",\"isPlaying\":" + (Snapshot.m_IsPlaying ? "true" : "false") +
				   ",\"lyricType\":" + Str(Snapshot.m_LyricType) +
				   ",\"lyricContent\":" + Str(Snapshot.m_LyricContent) +
				   ",\"translationLrc\":" + Str(Snapshot.m_TranslationLrc) +
				   "}";
		if(!WriteFileAtomically(Path, Json))
			return {};
		return Path;
	}

	bool HttpGetJsonList(std::string *pBody)
	{
		if(pBody == nullptr)
			return false;
		pBody->clear();
		HINTERNET hSession = WinHttpOpen(L"QmClient-Soda/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
		if(hSession == nullptr)
			return false;
		WinHttpSetTimeouts(hSession, 250, 250, 500, 500);
		HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", QmSodaWatchdog::INSPECTOR_PORT, 0);
		HINTERNET hRequest = hConnect != nullptr ? WinHttpOpenRequest(hConnect, L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
		bool Success = false;
		if(hRequest != nullptr && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, nullptr))
		{
			DWORD Status = 0;
			DWORD StatusSize = sizeof(Status);
			if(WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX) && Status == 200)
			{
				std::array<char, 8192> Buffer{};
				for(;;)
				{
					DWORD Available = 0;
					if(!WinHttpQueryDataAvailable(hRequest, &Available) || Available == 0)
						break;
					if(pBody->size() + Available > 4 * 1024 * 1024)
						break;
					DWORD Read = 0;
					const DWORD Chunk = std::min<DWORD>(Available, (DWORD)Buffer.size());
					if(!WinHttpReadData(hRequest, Buffer.data(), Chunk, &Read) || Read == 0)
						break;
					pBody->append(Buffer.data(), Read);
				}
				Success = !pBody->empty();
			}
		}
		if(hRequest != nullptr)
			WinHttpCloseHandle(hRequest);
		if(hConnect != nullptr)
			WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return Success;
	}

	const json_value *ArrayField(const json_value *pObject, const char *pName)
	{
		if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
			return nullptr;
		for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
		{
			const auto &Entry = pObject->u.object.values[Index];
			if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
				return Entry.value;
		}
		return nullptr;
	}

	// 从 /json/list 响应中选 node 型目标(兜底第 0 个),返回 WebSocket URL。
	std::string PickNodeTarget(std::string_view Json)
	{
		json_value *pRoot = json_parse(Json.data(), Json.size());
		if(pRoot == nullptr || pRoot->type != json_array)
		{
			if(pRoot != nullptr)
				json_value_free(pRoot);
			return {};
		}
		std::string Fallback;
		for(unsigned int Index = 0; Index < pRoot->u.array.length; ++Index)
		{
			const json_value *pEntry = pRoot->u.array.values[Index];
			if(pEntry == nullptr || pEntry->type != json_object)
				continue;
			std::string WsUrl;
			for(unsigned int FieldIndex = 0; FieldIndex < pEntry->u.object.length; ++FieldIndex)
			{
				const auto &Entry = pEntry->u.object.values[FieldIndex];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == "webSocketDebuggerUrl" && Entry.value != nullptr && Entry.value->type == json_string)
					WsUrl = std::string(Entry.value->u.string.ptr, Entry.value->u.string.length);
			}
			if(WsUrl.empty())
				continue;
			std::string Type;
			for(unsigned int FieldIndex = 0; FieldIndex < pEntry->u.object.length; ++FieldIndex)
			{
				const auto &Entry = pEntry->u.object.values[FieldIndex];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == "type" && Entry.value != nullptr && Entry.value->type == json_string)
					Type = std::string(Entry.value->u.string.ptr, Entry.value->u.string.length);
			}
			if(Fallback.empty())
				Fallback = WsUrl;
			if(Type == "node")
			{
				json_value_free(pRoot);
				return WsUrl;
			}
		}
		json_value_free(pRoot);
		return Fallback;
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

	int Watch(DWORD ParentPid)
	{
		HANDLE hParent = nullptr;
		if(ParentPid != 0)
		{
			hParent = OpenProcess(SYNCHRONIZE, FALSE, ParentPid);
			if(hParent == nullptr)
				return 4;
		}
		QmSodaHook::CSodaWriter Writer;
		if(!Writer.Open(true))
			Log("[soda] shared memory writer unavailable\n");
		int Result = 0;
		uint32_t ActivePid = 0;
		uint64_t LastPublishedGeneration = 0;
		std::string LastPublishedMediaId;
		std::string LastPublishedLyricPath;
		bool LoggedNoProcess = false;
		for(;;)
		{
			if(hParent != nullptr && WaitForSingleObject(hParent, 0) == WAIT_OBJECT_0)
				break;
			const uint32_t Pid = QmSodaWatchdog::FindMainPid();
			if(Pid == 0)
			{
				if(ActivePid != 0)
				{
					Log("[soda] SodaMusic exited\n");
					ActivePid = 0;
					LastPublishedGeneration = 0;
					LastPublishedMediaId.clear();
					LastPublishedLyricPath.clear();
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
				LastPublishedGeneration = 0;
				LastPublishedMediaId.clear();
				LastPublishedLyricPath.clear();
			}
			if(!QmSodaWatchdog::IsInspectorUp())
			{
				if(!QmSodaWatchdog::EnsureInspector(Pid))
				{
					// OpenProcess/CreateRemoteThread 在目标以更高权限(如管理员)运行时
					// 会失败。提示用户,不要无限静默重试。
					Log("[soda] inspector activation failed for pid=%u (SodaMusic 以管理员权限运行? 请用普通权限启动)\n", Pid);
					std::this_thread::sleep_for(std::chrono::milliseconds(3000));
					continue;
				}
				// 激活是异步的,轮询等 9229 就绪。
				const uint64_t Deadline = GetTickCount64() + 3000;
				while(!QmSodaWatchdog::IsInspectorUp() && GetTickCount64() < Deadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(150));
				if(!QmSodaWatchdog::IsInspectorUp())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					continue;
				}
				Log("[soda] inspector ready on port %u\n", (unsigned)QmSodaWatchdog::INSPECTOR_PORT);
			}
			std::string ListJson;
			if(!HttpGetJsonList(&ListJson))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			const std::string WsUrl = PickNodeTarget(ListJson);
			if(WsUrl.empty())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			QmNeteaseCdp::CCdpSession Session;
			if(!Session.Connect(WsUrl))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			Log("[soda] CDP connected\n");
			// 会话期内持续提取;连接断开后重连。
			while(hParent == nullptr || WaitForSingleObject(hParent, 0) != WAIT_OBJECT_0)
			{
				std::string Value;
				if(!Session.EvaluateAwaitPromise(QmSodaProbe::BuildBridgeExpression(), &Value, EXTRACT_TIMEOUT_MS))
					break;
				QmSodaProbe::SPlaybackSnapshot Snapshot;
				if(QmSodaProbe::ParseExtractionJson(Value, &Snapshot))
				{
					PrintSnapshot(Snapshot);
					// 构建共享内存快照。
					QmSodaHook::SSnapshot Shared{};
					Shared.m_SodaMusicPid = Pid;
					Shared.m_UpdatedAtTick = GetTickCount64();
					Shared.m_Flags = 0;
					if(Snapshot.m_IsPlaying)
						Shared.m_Flags |= QmSodaHook::FLAG_PLAYING;
					if(Snapshot.m_IsLoading)
						Shared.m_Flags |= QmSodaHook::FLAG_LOADING;
					if(Snapshot.m_Throttled)
						Shared.m_Flags |= QmSodaHook::FLAG_THROTTLED;
					if(Snapshot.m_ProgressSeconds >= 0)
					{
						Shared.m_PositionMs = (int64_t)(Snapshot.m_ProgressSeconds * 1000);
						Shared.m_Flags |= QmSodaHook::FLAG_POSITION_VALID;
					}
					Shared.m_DurationMs = (int64_t)(Snapshot.m_DurationSeconds * 1000);
					QmSodaHook::CopyUtf8Truncated(Shared.m_aMediaId, sizeof(Shared.m_aMediaId), Snapshot.m_MediaId.data(), Snapshot.m_MediaId.size());
					QmSodaHook::CopyUtf8Truncated(Shared.m_aTitle, sizeof(Shared.m_aTitle), Snapshot.m_Name.data(), Snapshot.m_Name.size());
					QmSodaHook::CopyUtf8Truncated(Shared.m_aAlbum, sizeof(Shared.m_aAlbum), Snapshot.m_Album.data(), Snapshot.m_Album.size());
					QmSodaHook::CopyUtf8Truncated(Shared.m_aCoverUrl, sizeof(Shared.m_aCoverUrl), Snapshot.m_CoverUrl.data(), Snapshot.m_CoverUrl.size());
					if(!Snapshot.m_MediaId.empty())
					{
						Shared.m_Flags |= QmSodaHook::FLAG_HAS_SONG;
						// 歌曲变化时推进 generation 并重写歌词文件;同一首歌持续
						// 播放时复用已写好的路径。歌词路径必须每次发布都带上:
						// 客户端可能在任何时刻首次读快照,若快照里没有路径就永远
						// 不会加载歌词(它只在歌曲变化时读路径)。
						if(Snapshot.m_MediaId != LastPublishedMediaId)
						{
							++LastPublishedGeneration;
							if(LastPublishedGeneration == 0)
								LastPublishedGeneration = 1;
							LastPublishedMediaId = Snapshot.m_MediaId;
							LastPublishedLyricPath.clear();
							const std::wstring LyricPath = WriteLyricDataFile(Snapshot);
							if(!LyricPath.empty())
								LastPublishedLyricPath = WideStringToUtf8(LyricPath);
						}
						if(!LastPublishedLyricPath.empty())
						{
							QmSodaHook::CopyUtf8Truncated(Shared.m_aLyricFilePath, sizeof(Shared.m_aLyricFilePath), LastPublishedLyricPath.data(), LastPublishedLyricPath.size());
							Shared.m_Flags |= QmSodaHook::FLAG_HAS_LYRIC_FILE;
						}
					}
					Shared.m_Generation = LastPublishedGeneration;
					if(Shared.m_Flags != 0)
						Writer.Publish(Shared);
				}
				else if(!Snapshot.m_Error.empty() && Snapshot.m_Error != "no-port")
					Log("[soda] extract: %s\n", Snapshot.m_Error.c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(EXTRACT_INTERVAL_MS));
			}
			Session.Close();
			Writer.Close();
			if(!Writer.Open(true))
				Log("[soda] shared memory writer unavailable (reopen)\n");
		}
		if(hParent != nullptr)
			CloseHandle(hParent);
		return Result;
	}
}

int wmain(int argc, wchar_t **argv)
{
	// 重定向到文件/管道时 printf 默认全缓冲,真机诊断看不到进度;改无缓冲。
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	Log("[soda] helper start argc=%d\n", argc);
	HANDLE hMutex = CreateMutexW(nullptr, TRUE, HELPER_MUTEX_NAME);
	Log("[soda] mutex handle=%p lastError=%lu\n", (void *)hMutex, (unsigned long)GetLastError());
	if(hMutex == nullptr)
		return 2;
	// 命名 mutex 在持有者崩溃后会变成 abandoned,但对象仍存在:此时
	// GetLastError 是 ERROR_ALREADY_EXISTS,句柄却可正常获取。因此用
	// WaitForSingleObject 判断是否有活跃实例,而不是只看 ERROR_ALREADY_EXISTS。
	const DWORD MutexWait = WaitForSingleObject(hMutex, 0);
	if(MutexWait == WAIT_TIMEOUT)
	{
		Log("[soda] another helper instance is running\n");
		CloseHandle(hMutex);
		return 0;
	}
	DWORD ParentPid = 0;
	for(int i = 1; i + 1 < argc; ++i)
	{
		if(std::wstring(argv[i]) == L"--parent-pid")
			ParentPid = (DWORD)_wtoi(argv[i + 1]);
	}
	Log("[soda] parent-pid=%lu\n", (unsigned long)ParentPid);
	const int Result = Watch(ParentPid);
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	return Result;
}
