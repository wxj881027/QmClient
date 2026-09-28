#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "qm_kugou_source.h"
#include "qm_music_publication.h"
#include "qm_qqmusic_source.h"

#include <windows.h>

#include <qm/hooks/soda/qm_soda_writer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace
{
	std::wstring Utf8ToWide(const std::string &Text)
	{
		const int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), (int)Text.size(), nullptr, 0);
		if(Length <= 0)
			return {};
		std::wstring Result((size_t)Length, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), (int)Text.size(), Result.data(), Length);
		return Result;
	}

	std::filesystem::path LyricDirectory(bool Kugou)
	{
		wchar_t aPath[32768];
		const DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
			return {};
		std::filesystem::path Directory = std::filesystem::path(aPath) / L"QmClient" / (Kugou ? L"kugou-hook" : L"qqmusic-hook");
		std::error_code Error;
		std::filesystem::create_directories(Directory, Error);
		return Error ? std::filesystem::path() : Directory;
	}

	bool WriteAtomically(const std::filesystem::path &Path, const std::string &Json)
	{
		const std::wstring Temporary = Path.wstring() + L".tmp";
		HANDLE hFile = CreateFileW(Temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
		if(hFile == INVALID_HANDLE_VALUE)
			return false;
		size_t Offset = 0;
		while(Offset < Json.size())
		{
			DWORD Written = 0;
			const DWORD Chunk = (DWORD)std::min<size_t>(Json.size() - Offset, 1024 * 1024);
			if(!WriteFile(hFile, Json.data() + Offset, Chunk, &Written, nullptr) || Written != Chunk)
				break;
			Offset += Written;
		}
		const bool Complete = Offset == Json.size() && FlushFileBuffers(hFile) != FALSE;
		CloseHandle(hFile);
		if(Complete && MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		DeleteFileW(Temporary.c_str());
		return false;
	}

	struct SCollected
	{
		std::mutex m_Mutex;
		QmMusicHook::SPlayback m_Playback;
		uint64_t m_Version = 0;
		uint64_t m_Tick = 0;
		std::atomic<bool> m_Running{true};
	};

	void Collect(bool Kugou, SCollected &Shared)
	{
		std::unique_ptr<QmMusicHook::CKugouSource> pKugou;
		std::unique_ptr<QmMusicHook::CQQMusicSource> pQQ;
		if(Kugou)
			pKugou = std::make_unique<QmMusicHook::CKugouSource>();
		else
			pQQ = std::make_unique<QmMusicHook::CQQMusicSource>();
		while(Shared.m_Running)
		{
			QmMusicHook::SPlayback Playback;
			try
			{
				if(Kugou)
					pKugou->Poll(Playback);
				else
					pQQ->Poll(Playback);
			}
			catch(...)
			{
				Playback = {};
				Playback.m_Error = "音乐应用采集失败，稍后重试";
			}
			{
				std::lock_guard<std::mutex> Lock(Shared.m_Mutex);
				Shared.m_Playback = std::move(Playback);
				Shared.m_Tick = GetTickCount64();
				++Shared.m_Version;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
}

int wmain(int argc, wchar_t **argv)
{
	bool Kugou = false;
	bool SourceGiven = false;
	DWORD ParentPid = 0;
	for(int Index = 1; Index < argc; ++Index)
	{
		if(wcscmp(argv[Index], L"--kugou-setup") == 0 || wcscmp(argv[Index], L"--kugou-restore") == 0)
		{
			std::string Message;
			const int Result = QmMusicHook::ConfigureKugou(wcscmp(argv[Index], L"--kugou-restore") == 0, &Message);
			if(!Message.empty())
				MessageBoxW(nullptr, Utf8ToWide(Message).c_str(), L"QmClient 酷狗歌词", MB_OK | (Result == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
			return Result;
		}
		if(wcscmp(argv[Index], L"--source") == 0 && Index + 1 < argc)
		{
			++Index;
			Kugou = wcscmp(argv[Index], L"kugou") == 0;
			SourceGiven = Kugou || wcscmp(argv[Index], L"qqmusic") == 0;
		}
		else if(wcscmp(argv[Index], L"--parent-pid") == 0 && Index + 1 < argc)
			ParentPid = wcstoul(argv[++Index], nullptr, 10);
	}
	if(!SourceGiven || ParentPid == 0)
		return 1;
	HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, ParentPid);
	if(hParent == nullptr)
		return 1;
	HANDLE hInstance = CreateMutexW(nullptr, TRUE, Kugou ? L"Local\\QmClient.KugouHook.Helper.v1" : L"Local\\QmClient.QQMusicHook.Helper.v1");
	if(hInstance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		if(hInstance != nullptr)
			CloseHandle(hInstance);
		CloseHandle(hParent);
		return 0;
	}
	QmSodaHook::CSodaWriter Writer;
	if(!Writer.Open(true, Kugou ? L"Local\\QmClient.KugouHook.v1" : L"Local\\QmClient.QQMusicHook.v1",
		   Kugou ? L"Local\\QmClient.KugouHook.v1.Writer" : L"Local\\QmClient.QQMusicHook.v1.Writer"))
	{
		CloseHandle(hInstance);
		CloseHandle(hParent);
		return 1;
	}
	const std::filesystem::path Directory = LyricDirectory(Kugou);
	const std::wstring Prefix = L"lyrics-" + std::to_wstring(GetCurrentProcessId()) + L"-";
	// helper 重启后继续使用新的版本空间，旧映射中的同歌结果不能复用。
	QmMusicHook::CPublication Publication(GetTickCount64());
	SCollected Shared;
	std::thread Worker(Collect, Kugou, std::ref(Shared));
	uint64_t Version = 0;
	uint64_t LastCollectedTick = GetTickCount64();
	uint64_t NextFileRetry = 0;
	std::filesystem::path LastPath;
	std::string LyricPath;
	bool TimedOut = false;
	while(WaitForSingleObject(hParent, 0) == WAIT_TIMEOUT)
	{
		const uint64_t Now = GetTickCount64();
		bool Changed = false;
		{
			std::lock_guard<std::mutex> Lock(Shared.m_Mutex);
			if(Version != Shared.m_Version)
			{
				Changed = Publication.Update(Shared.m_Playback);
				Version = Shared.m_Version;
				LastCollectedTick = Shared.m_Tick;
				TimedOut = false;
			}
		}
		// 心跳不依赖应用扫描或网络；采集停滞时主动清歌，不能继续发布旧歌词。
		if(!TimedOut && Now >= LastCollectedTick && Now - LastCollectedTick > 1500)
		{
			QmMusicHook::SPlayback Stale;
			Stale.m_Loading = true;
			Stale.m_Error = "音乐应用响应超时，正在重新连接";
			Changed = Publication.Update(Stale);
			TimedOut = true;
		}
		if(Changed)
		{
			LyricPath.clear();
			NextFileRetry = 0;
		}
		if(Publication.HasLyrics() && LyricPath.empty() && Now >= NextFileRetry)
		{
			NextFileRetry = Now + 1000;
			const std::filesystem::path Path = Directory / (Prefix + std::to_wstring(Publication.Generation()) + L".json");
			if(!Directory.empty() && WriteAtomically(Path, Publication.LyricJson()))
			{
				const auto Utf8Path = Path.u8string();
				LyricPath.assign(reinterpret_cast<const char *>(Utf8Path.data()), Utf8Path.size());
				if(!LastPath.empty() && LastPath != Path)
					DeleteFileW(LastPath.c_str());
				LastPath = Path;
			}
		}
		auto Snapshot = Publication.Snapshot(Now, LyricPath);
		if(Publication.HasLyrics() && LyricPath.empty())
		{
			const std::string Error = "无法写入歌词缓存，请检查本地应用数据目录";
			QmSodaHook::CopyUtf8Truncated(Snapshot.m_aError, sizeof(Snapshot.m_aError), Error.data(), Error.size());
		}
		Writer.Publish(Snapshot);
		if(WaitForSingleObject(hParent, 200) != WAIT_TIMEOUT)
			break;
	}
	Shared.m_Running = false;
	Worker.join();
	Publication.Update({});
	Writer.Publish(Publication.Snapshot(GetTickCount64(), ""));
	if(!LastPath.empty())
		DeleteFileW(LastPath.c_str());
	CloseHandle(hInstance);
	CloseHandle(hParent);
	return 0;
}
