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
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
	class CHandle
	{
		HANDLE m_Handle;

	public:
		explicit CHandle(HANDLE Handle) :
			m_Handle(Handle) {}
		~CHandle()
		{
			if(Valid())
				CloseHandle(m_Handle);
		}
		CHandle(const CHandle &) = delete;
		CHandle &operator=(const CHandle &) = delete;
		bool Valid() const { return m_Handle != nullptr && m_Handle != INVALID_HANDLE_VALUE; }
		HANDLE Get() const { return m_Handle; }
	};

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

	class CCollectorJoin
	{
		SCollected &m_Shared;
		std::thread &m_Worker;

	public:
		CCollectorJoin(SCollected &Shared, std::thread &Worker) noexcept :
			m_Shared(Shared), m_Worker(Worker) {}
		~CCollectorJoin() { Stop(); }
		CCollectorJoin(const CCollectorJoin &) = delete;
		CCollectorJoin &operator=(const CCollectorJoin &) = delete;
		void Stop()
		{
			m_Shared.m_Running = false;
			if(m_Worker.joinable())
				m_Worker.join();
		}
	};

	void CollectionFailed(QmMusicHook::SPlayback &Playback)
	{
		Playback = {};
		try
		{
			Playback.m_Error = "音乐应用采集失败，稍后重试";
		}
		catch(const std::bad_alloc &)
		{
			// 诊断也可能申请失败，仍发布空采集结果而不再次逃出线程。
		}
		catch(const std::length_error &)
		{
		}
	}

	void Collect(bool Kugou, SCollected &Shared)
	{
		std::unique_ptr<QmMusicHook::CKugouSource> pKugou;
		std::unique_ptr<QmMusicHook::CQQMusicSource> pQQ;
		while(Shared.m_Running)
		{
			QmMusicHook::SPlayback Playback;
			bool SourceReady = true;
			try
			{
				if(Kugou && !pKugou)
					pKugou = std::make_unique<QmMusicHook::CKugouSource>();
				else if(!Kugou && !pQQ)
					pQQ = std::make_unique<QmMusicHook::CQQMusicSource>();
			}
			catch(const std::bad_alloc &)
			{
				SourceReady = false;
			}
			catch(const std::length_error &)
			{
				SourceReady = false;
			}
			catch(const std::system_error &)
			{
				// 酷狗来源的构造包含线程创建；创建拒收后保留空实例供下轮重试。
				SourceReady = false;
			}
			if(!SourceReady)
				CollectionFailed(Playback);
			else
			{
				try
				{
					if(Kugou)
						pKugou->Poll(Playback);
					else
						pQQ->Poll(Playback);
				}
				catch(...)
				{
					// 沿用 Poll 的失败策略；未知构造异常仍不在此吞掉。
					CollectionFailed(Playback);
				}
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
try
{
	bool Kugou = false;
	bool SourceGiven = false;
	DWORD ParentPid = 0;
	for(int Index = 1; Index < argc; ++Index)
	{
		if(wcscmp(argv[Index], L"--kugou-setup") == 0 || wcscmp(argv[Index], L"--kugou-restore") == 0)
		{
			const auto Result = QmMusicHook::ConfigureKugou(wcscmp(argv[Index], L"--kugou-restore") == 0);
			MessageBoxW(nullptr, Result.m_pMessage, L"QmClient 酷狗歌词", MB_OK | (Result.m_Code == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
			return Result.m_Code;
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
	const CHandle Parent(OpenProcess(SYNCHRONIZE, FALSE, ParentPid));
	if(!Parent.Valid())
		return 1;
	const CHandle Instance(CreateMutexW(nullptr, TRUE, Kugou ? L"Local\\QmClient.KugouHook.Helper.v1" : L"Local\\QmClient.QQMusicHook.Helper.v1"));
	if(!Instance.Valid() || GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;
	QmSodaHook::CSodaWriter Writer;
	if(!Writer.Open(true, Kugou ? L"Local\\QmClient.KugouHook.v1" : L"Local\\QmClient.QQMusicHook.v1",
		   Kugou ? L"Local\\QmClient.KugouHook.v1.Writer" : L"Local\\QmClient.QQMusicHook.v1.Writer"))
		return 1;
	const std::filesystem::path Directory = LyricDirectory(Kugou);
	const std::wstring Prefix = L"lyrics-" + std::to_wstring(GetCurrentProcessId()) + L"-";
	// helper 重启后继续使用新的版本空间，旧映射中的同歌结果不能复用。
	QmMusicHook::CPublication Publication(GetTickCount64());
	SCollected Shared;
	std::thread Worker;
	try
	{
		Worker = std::thread(Collect, Kugou, std::ref(Shared));
	}
	catch(const std::system_error &)
	{
		return 1;
	}
	CCollectorJoin CollectorJoin(Shared, Worker);
	uint64_t Version = 0;
	uint64_t LastCollectedTick = GetTickCount64();
	uint64_t NextFileRetry = 0;
	std::filesystem::path LastPath;
	std::string LyricPath;
	bool TimedOut = false;
	while(WaitForSingleObject(Parent.Get(), 0) == WAIT_TIMEOUT)
	{
		const uint64_t Now = GetTickCount64();
		try
		{
			{
				std::lock_guard<std::mutex> Lock(Shared.m_Mutex);
				if(Version != Shared.m_Version)
				{
					if(Publication.Update(Shared.m_Playback))
					{
						LyricPath.clear();
						NextFileRetry = 0;
					}
					Version = Shared.m_Version;
					LastCollectedTick = Shared.m_Tick;
					TimedOut = false;
				}
			}
		}
		catch(const std::bad_alloc &)
		{
			// 失败的 Update 不消费本次采集版本；此前成功的阶段不回滚。
		}
		catch(const std::length_error &)
		{
		}
		// 新采集准备失败也必须检查超时，不能以新心跳持续保鲜旧歌。
		if(!TimedOut && Now >= LastCollectedTick && Now - LastCollectedTick > 1500)
		{
			QmMusicHook::SPlayback Stale;
			Stale.m_Loading = true;
			const auto ApplyStale = [&]() {
				if(Publication.Update(Stale))
				{
					LyricPath.clear();
					NextFileRetry = 0;
				}
				TimedOut = true;
			};
			try
			{
				Stale.m_Error = "音乐应用响应超时，正在重新连接";
				ApplyStale();
			}
			catch(const std::bad_alloc &)
			{
				// 诊断准备或发布失败都不阻断清歌；全部空字符串的候选不申请内存。
				Stale.m_Error.clear();
				ApplyStale();
			}
			catch(const std::length_error &)
			{
				Stale.m_Error.clear();
				ApplyStale();
			}
		}
		if(Publication.HasLyrics() && LyricPath.empty() && Now >= NextFileRetry)
		{
			NextFileRetry = Now + 1000;
			try
			{
				if(!Directory.empty())
				{
					std::filesystem::path Path = Directory / (Prefix + std::to_wstring(Publication.Generation()) + L".json");
					const auto Utf8Path = Path.u8string();
					std::string PreparedLyricPath(reinterpret_cast<const char *>(Utf8Path.data()), Utf8Path.size());
					const std::string Json = Publication.LyricJson();
					if(WriteAtomically(Path, Json))
					{
						// 两个候选都已拥有存储，提交后只交换，再清理旧文件。
						LastPath.swap(Path);
						LyricPath.swap(PreparedLyricPath);
						if(!Path.empty() && Path != LastPath)
							DeleteFileW(Path.c_str());
					}
				}
			}
			catch(const std::bad_alloc &)
			{
				// 写入前准备失败保留已登记路径；真实 I/O 失败沿原一秒间隔重试。
			}
			catch(const std::length_error &)
			{
			}
		}
		auto Snapshot = Publication.Snapshot(Now, LyricPath);
		if(Publication.HasLyrics() && LyricPath.empty())
		{
			constexpr char aError[] = "无法写入歌词缓存，请检查本地应用数据目录";
			QmSodaHook::CopyUtf8Truncated(Snapshot.m_aError, sizeof(Snapshot.m_aError), aError, sizeof(aError) - 1);
		}
		Writer.Publish(Snapshot);
		if(WaitForSingleObject(Parent.Get(), 200) != WAIT_TIMEOUT)
			break;
	}
	CollectorJoin.Stop();
	Publication.Update({});
	Writer.Publish(Publication.Snapshot(GetTickCount64(), ""));
	if(!LastPath.empty())
		DeleteFileW(LastPath.c_str());
	return 0;
}
catch(const std::bad_alloc &)
{
	return 1;
}
catch(const std::length_error &)
{
	return 1;
}
catch(...)
{
	// 先经局部 RAII 停止并等待采集线程，再沿原未知异常政策外传。
	throw;
}
