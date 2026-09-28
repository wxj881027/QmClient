// 酷狗 CDP 调用与补丁指纹参考 Metabox-Nexus-PlayerCap（MIT），授权见 license.txt。
#include "qm_kugou_source.h"

#include "qm_kugou_protocol.h"

#include <engine/external/json-parser/json.h>

#include <qm/hooks/netease/qm_netease_cdp_client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>
#include <tlhelp32.h>
#include <winhttp.h>
#endif

namespace QmMusicHook
{
	namespace
	{
		using TClock = std::chrono::steady_clock;
#if defined(_WIN32)
		constexpr uint16_t CDP_PORT = 12233;
		constexpr size_t MAX_HTTP_BYTES = 4 * 1024 * 1024;

		class CHandle
		{
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
			bool Valid() const { return m_Handle && m_Handle != INVALID_HANDLE_VALUE; }
			HANDLE Get() const { return m_Handle; }

		private:
			HANDLE m_Handle;
		};

		class CInternet
		{
		public:
			explicit CInternet(HINTERNET Handle) :
				m_Handle(Handle) {}
			~CInternet()
			{
				if(m_Handle)
					WinHttpCloseHandle(m_Handle);
			}
			CInternet(const CInternet &) = delete;
			CInternet &operator=(const CInternet &) = delete;
			HINTERNET Get() const { return m_Handle; }

		private:
			HINTERNET m_Handle;
		};

		std::string Utf8(std::wstring_view Value)
		{
			if(Value.empty())
				return {};
			const int Size = WideCharToMultiByte(CP_UTF8, 0, Value.data(), (int)Value.size(), nullptr, 0, nullptr, nullptr);
			std::string Result(Size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, Value.data(), (int)Value.size(), Result.data(), Size, nullptr, nullptr);
			return Result;
		}

		std::wstring ParentPath(std::wstring_view Path)
		{
			const auto End = Path.find_last_of(L"\\/");
			return End == std::wstring_view::npos ? std::wstring() : std::wstring(Path.substr(0, End));
		}

		bool IsFile(const std::wstring &Path)
		{
			const DWORD Attributes = GetFileAttributesW(Path.c_str());
			return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
		}

		std::wstring ProcessPath(DWORD ProcessId)
		{
			const CHandle Process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId));
			std::array<wchar_t, 32768> Buffer{};
			DWORD Length = (DWORD)Buffer.size();
			return Process.Valid() && QueryFullProcessImageNameW(Process.Get(), 0, Buffer.data(), &Length) ? std::wstring(Buffer.data(), Length) : std::wstring();
		}

		std::vector<DWORD> KugouProcesses()
		{
			std::vector<DWORD> Result;
			const CHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
			PROCESSENTRY32W Entry{};
			Entry.dwSize = sizeof(Entry);
			if(Snapshot.Valid() && Process32FirstW(Snapshot.Get(), &Entry))
			{
				do
				{
					if(_wcsicmp(Entry.szExeFile, L"KuGou.exe") == 0)
						Result.push_back(Entry.th32ProcessID);
				} while(Process32NextW(Snapshot.Get(), &Entry));
			}
			return Result;
		}

		DWORD ListenerProcess()
		{
			DWORD Size = 0;
			GetExtendedTcpTable(nullptr, &Size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
			if(Size == 0)
				return 0;
			std::vector<unsigned char> Buffer(Size);
			if(GetExtendedTcpTable(Buffer.data(), &Size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
				return 0;
			const auto *pTable = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(Buffer.data());
			for(DWORD Index = 0; Index < pTable->dwNumEntries; ++Index)
			{
				const auto &Row = pTable->table[Index];
				if(ntohs((u_short)Row.dwLocalPort) == CDP_PORT && (Row.dwLocalAddr == htonl(INADDR_LOOPBACK)))
					return Row.dwOwningPid;
			}
			return 0;
		}

		bool HttpGet(const wchar_t *pHost, INTERNET_PORT Port, const std::wstring &Path, bool Secure, std::string *pBody)
		{
			pBody->clear();
			// 单次读取超时不能限制持续慢速响应，总时限保证切歌后仍能释放取词任务。
			const auto Deadline = TClock::now() + std::chrono::seconds(8);
			const CInternet Session(WinHttpOpen(L"QmClient-Kugou/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
			if(!Session.Get())
				return false;
			WinHttpSetTimeouts(Session.Get(), 1200, 1200, 1200, 1200);
			const CInternet Connection(WinHttpConnect(Session.Get(), pHost, Port, 0));
			if(!Connection.Get())
				return false;
			const CInternet Request(WinHttpOpenRequest(Connection.Get(), L"GET", Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Secure ? WINHTTP_FLAG_SECURE : 0));
			if(!Request.Get())
				return false;
			const DWORD Redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			WinHttpSetOption(Request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, (void *)&Redirects, sizeof(Redirects));
			if(!WinHttpSendRequest(Request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(Request.Get(), nullptr))
				return false;
			DWORD Status = 0, StatusSize = sizeof(Status);
			if(!WinHttpQueryHeaders(Request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX) || Status != 200)
				return false;
			std::array<char, 16384> Buffer{};
			while(TClock::now() < Deadline)
			{
				DWORD Read = 0;
				if(!WinHttpReadData(Request.Get(), Buffer.data(), (DWORD)Buffer.size(), &Read))
					return false;
				if(Read == 0)
					return true;
				if(pBody->size() + Read > MAX_HTTP_BYTES)
					return false;
				pBody->append(Buffer.data(), Read);
			}
			return false;
		}

		std::string JsonText(const json_value *pRoot, std::string_view Name)
		{
			if(!pRoot || pRoot->type != json_object)
				return {};
			for(unsigned int Index = 0; Index < pRoot->u.object.length; ++Index)
			{
				const auto &Entry = pRoot->u.object.values[Index];
				if(Entry.name && Name == std::string_view(Entry.name, Entry.name_length) && Entry.value->type == json_string)
					return {Entry.value->u.string.ptr, Entry.value->u.string.length};
			}
			return {};
		}

		bool ConnectPage(QmNeteaseCdp::CCdpSession &Session, DWORD ExpectedPid)
		{
			std::string Json;
			if(!HttpGet(L"127.0.0.1", CDP_PORT, L"/json", false, &Json))
				return false;
			const std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Json.data(), Json.size()), json_value_free);
			if(!Root || Root->type != json_array)
				return false;
			std::vector<std::pair<bool, std::string>> Targets;
			for(unsigned int Index = 0; Index < Root->u.array.length; ++Index)
			{
				const auto *pTarget = Root->u.array.values[Index];
				const auto Url = JsonText(pTarget, "webSocketDebuggerUrl");
				uint16_t Port = 0;
				if(JsonText(pTarget, "type") != "page" || !QmNeteaseCdp::ParseLoopbackWebSocketUrl(Url, &Port) || Port != CDP_PORT)
					continue;
				Targets.emplace_back(JsonText(pTarget, "url").find("desktop-popup") != std::string::npos, Url);
			}
			std::stable_sort(Targets.begin(), Targets.end(), [](const auto &A, const auto &B) { return A.first > B.first; });
			for(const auto &Target : Targets)
			{
				if(ListenerProcess() != ExpectedPid)
					return false;
				std::string Available;
				if(Session.Connect(Target.second) && Session.Evaluate("JSON.stringify(!!(window.external && typeof window.external.SuperCall === 'function'))", &Available, 600) && Available == "true")
					return true;
				Session.Close();
			}
			return false;
		}

		constexpr const char *PLAY_INFO_SCRIPT = R"JS(new Promise(function(resolve) {
var name = '__qm_kugou_' + Date.now();
var finished = false;
var timer;
function finish(value) {
  if (finished) return;
  finished = true;
  clearTimeout(timer);
  delete window[name];
  resolve(typeof value === 'string' ? value : JSON.stringify(value || {}));
}
window[name] = finish;
timer = setTimeout(function() { finish(''); }, 650);
try { external.SuperCall(864, JSON.stringify({callback:name})); }
catch (_) { finish(''); }
}))JS";

		std::wstring UrlEncode(std::string_view Value)
		{
			constexpr wchar_t HEX[] = L"0123456789ABCDEF";
			std::wstring Encoded;
			for(const unsigned char Byte : Value)
			{
				if((Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' || Byte == '.' || Byte == '~')
					Encoded.push_back(Byte);
				else
				{
					Encoded.push_back('%');
					Encoded.push_back(HEX[Byte >> 4]);
					Encoded.push_back(HEX[Byte & 15]);
				}
			}
			return Encoded;
		}

		struct SLyricResult
		{
			std::string m_MediaId;
			std::string m_Type;
			std::string m_Text;
			bool m_Success = false;
		};

		SLyricResult FetchLyrics(std::string MediaId, int64_t DurationMs)
		{
			SLyricResult Result;
			Result.m_MediaId = std::move(MediaId);
			std::string Json, Id, Key;
			const std::wstring Search = L"/search?ver=1&man=yes&client=mobi&keyword=&duration=" + std::to_wstring(DurationMs) + L"&hash=" + UrlEncode(Result.m_MediaId);
			if(!HttpGet(L"krcs.kugou.com", INTERNET_DEFAULT_HTTPS_PORT, Search, true, &Json) || !SelectKugouLyricCandidate(Json, &Id, &Key))
				return Result;
			if(Id.empty())
			{
				Result.m_Success = true;
				return Result;
			}
			for(const bool Krc : {true, false})
			{
				const std::wstring Path = L"/download?ver=1&client=pc&id=" + UrlEncode(Id) + L"&accesskey=" + UrlEncode(Key) + L"&fmt=" + (Krc ? L"krc" : L"lrc") + L"&charset=utf8";
				if(HttpGet(L"lyrics.kugou.com", INTERNET_DEFAULT_HTTPS_PORT, Path, true, &Json) && DecodeKugouLyricResponse(Json, Krc, &Result.m_Text))
				{
					Result.m_Type = Krc ? "krc" : "lrc";
					Result.m_Success = true;
					break;
				}
			}
			return Result;
		}

		std::wstring RegistryString(const wchar_t *pName)
		{
			std::array<wchar_t, 32768> Buffer{};
			DWORD Size = (DWORD)(Buffer.size() * sizeof(wchar_t));
			if(RegGetValueW(HKEY_CURRENT_USER, L"Software\\KuGou", pName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, Buffer.data(), &Size) != ERROR_SUCCESS)
				return {};
			return Buffer.data();
		}

		std::wstring FileVersion(const std::wstring &Path)
		{
			DWORD Ignored = 0;
			const DWORD Size = GetFileVersionInfoSizeW(Path.c_str(), &Ignored);
			if(Size == 0)
				return {};
			std::vector<unsigned char> Bytes(Size);
			VS_FIXEDFILEINFO *pInfo = nullptr;
			UINT InfoSize = 0;
			if(!GetFileVersionInfoW(Path.c_str(), 0, Size, Bytes.data()) || !VerQueryValueW(Bytes.data(), L"\\", reinterpret_cast<void **>(&pInfo), &InfoSize) || InfoSize < sizeof(*pInfo) || pInfo->dwSignature != 0xFEEF04BD)
				return {};
			return std::to_wstring(HIWORD(pInfo->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(pInfo->dwFileVersionMS)) + L"." + std::to_wstring(HIWORD(pInfo->dwFileVersionLS)) + L"." + std::to_wstring(LOWORD(pInfo->dwFileVersionLS));
		}

		std::wstring FindLibcef()
		{
			std::vector<std::wstring> Roots;
			const auto AppPath = RegistryString(L"AppPath");
			if(!AppPath.empty())
				Roots.push_back(IsFile(AppPath) ? ParentPath(AppPath) : AppPath);
			Roots.emplace_back(L"C:\\Program Files\\KuGou\\KGMusic");
			Roots.emplace_back(L"C:\\Program Files (x86)\\KuGou\\KGMusic");
			for(auto Root : Roots)
			{
				while(!Root.empty() && (Root.back() == L'\\' || Root.back() == L'/'))
					Root.pop_back();
				const auto Exe = Root + L"\\KuGou.exe";
				if(!IsFile(Exe))
					continue;
				const auto Version = FileVersion(Exe);
				if(Version.empty() || Version.rfind(L"10.", 0) == 0)
					continue;
				const auto RegisteredDir = RegistryString(L"KuGou8");
				const std::wstring Prefix = Root + L"\\";
				if(RegisteredDir.size() >= Prefix.size() && _wcsnicmp(RegisteredDir.c_str(), Prefix.c_str(), Prefix.size()) == 0 && IsFile(RegisteredDir + L"\\libcef.dll"))
					return RegisteredDir + L"\\libcef.dll";
				const auto Matched = Root + L"\\" + Version + L"\\libcef.dll";
				if(IsFile(Matched))
					return Matched;
				if(IsFile(Root + L"\\libcef.dll"))
					return Root + L"\\libcef.dll";
			}
			return {};
		}

		bool ReadAt(HANDLE File, uint64_t Offset, void *pData, size_t Size)
		{
			LARGE_INTEGER Position{};
			Position.QuadPart = (LONGLONG)Offset;
			DWORD Read = 0;
			return SetFilePointerEx(File, Position, nullptr, FILE_BEGIN) && ReadFile(File, pData, (DWORD)Size, &Read, nullptr) && Read == Size;
		}

		bool WriteAt(HANDLE File, uint64_t Offset, const void *pData, size_t Size)
		{
			LARGE_INTEGER Position{};
			Position.QuadPart = (LONGLONG)Offset;
			DWORD Written = 0;
			return SetFilePointerEx(File, Position, nullptr, FILE_BEGIN) && WriteFile(File, pData, (DWORD)Size, &Written, nullptr) && Written == Size;
		}

		bool CompareBackup(HANDLE Backup, HANDLE Current, bool Patched, bool AllowPartial = false)
		{
			LARGE_INTEGER BackupSize{}, CurrentSize{};
			if(!GetFileSizeEx(Backup, &BackupSize) || !GetFileSizeEx(Current, &CurrentSize) || BackupSize.QuadPart != CurrentSize.QuadPart || BackupSize.QuadPart <= 0)
				return false;
			std::array<char, 65536> Original{}, Actual{};
			for(uint64_t Offset = 0; Offset < (uint64_t)BackupSize.QuadPart; Offset += Original.size())
			{
				const size_t Size = (size_t)std::min<uint64_t>(Original.size(), BackupSize.QuadPart - Offset);
				if(!ReadAt(Backup, Offset, Original.data(), Size) || !ReadAt(Current, Offset, Actual.data(), Size))
					return false;
				const std::string_view Before(Original.data(), Size), After(Actual.data(), Size);
				if(Patched ? !IsKugouRestoreChunk(Offset, Before, After, AllowPartial) : Before != After)
					return false;
			}
			return true;
		}

		bool CreateBackup(HANDLE Current, const std::wstring &Path)
		{
			bool Success = false;
			{
				const CHandle Backup(CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
				if(!Backup.Valid())
					return false;
				LARGE_INTEGER Size{};
				std::array<char, 65536> Buffer{};
				Success = GetFileSizeEx(Current, &Size) != FALSE;
				for(uint64_t Offset = 0; Success && Offset < (uint64_t)Size.QuadPart; Offset += Buffer.size())
				{
					const size_t Bytes = (size_t)std::min<uint64_t>(Buffer.size(), Size.QuadPart - Offset);
					Success = ReadAt(Current, Offset, Buffer.data(), Bytes) && WriteAt(Backup.Get(), Offset, Buffer.data(), Bytes);
				}
				Success = Success && FlushFileBuffers(Backup.Get());
			}
			if(!Success)
				DeleteFileW(Path.c_str());
			return Success;
		}
#endif
	}

	struct CKugouSource::SImpl
	{
		std::atomic_bool m_Stop{false};
		std::thread m_Thread;
		std::mutex m_Mutex;
		SPlayback m_State;
		TClock::time_point m_Updated{};

		void Publish(SPlayback State)
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_State = std::move(State);
			m_Updated = TClock::now();
		}

		void Run()
		{
#if defined(_WIN32)
			QmNeteaseCdp::CCdpSession Session;
			DWORD SessionPid = 0;
			std::future<SLyricResult> Pending;
			SLyricResult Lyrics;
			std::string CurrentId;
			TClock::time_point RetryAt{};
			while(!m_Stop)
			{
				SPlayback State;
				const auto Processes = KugouProcesses();
				const DWORD Listener = Processes.empty() ? 0 : ListenerProcess();
				const bool KnownProcess = Listener != 0 && std::find(Processes.begin(), Processes.end(), Listener) != Processes.end() && !ProcessPath(Listener).empty();
				State.m_ProcessId = Processes.empty() ? 0 : Processes.front();
				if(!KnownProcess)
				{
					Session.Close();
					State.m_Error = Processes.empty() ? "Kugou is not running" : "Kugou CDP is unavailable; use Enable Kugou integration, then restart Kugou";
				}
				else
				{
					if(SessionPid != Listener)
						Session.Close();
					SessionPid = Listener;
					std::string Json;
					if((Session.IsConnected() || ConnectPage(Session, Listener)) && Session.EvaluateAwaitPromise(PLAY_INFO_SCRIPT, &Json, 1000) && !Json.empty() && ParseKugouPlayback(Json, &State))
						State.m_ProcessId = Listener;
					else
					{
						Session.Close();
						State = {};
						State.m_ProcessId = Listener;
						State.m_Error = "Kugou playback data is unavailable";
					}
				}
				if(CurrentId != State.m_MediaId)
				{
					CurrentId = State.m_MediaId;
					Lyrics = {};
					RetryAt = {};
				}
				if(Pending.valid() && Pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
				{
					auto Result = Pending.get();
					if(Result.m_MediaId == CurrentId)
					{
						Lyrics = std::move(Result);
						RetryAt = TClock::now() + std::chrono::seconds(Lyrics.m_Success ? 60 : 10);
					}
				}
				if(State.m_HasSong && !Pending.valid() && !Lyrics.m_Success && TClock::now() >= RetryAt)
					Pending = std::async(std::launch::async, FetchLyrics, CurrentId, State.m_DurationMs);
				if(State.m_HasSong)
				{
					State.m_LyricType = Lyrics.m_Type;
					State.m_LyricContent = Lyrics.m_Text;
					State.m_Loading = !Lyrics.m_Success;
					if(!Lyrics.m_Success && Lyrics.m_MediaId == CurrentId)
						State.m_Error = "Kugou lyric request failed; retrying";
				}
				Publish(std::move(State));
				for(int Step = 0; Step < 2 && !m_Stop; ++Step)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
#endif
		}
	};

	CKugouSource::CKugouSource() :
		m_pImpl(std::make_unique<SImpl>())
	{
		m_pImpl->m_Thread = std::thread([this] { m_pImpl->Run(); });
	}

	CKugouSource::~CKugouSource()
	{
		m_pImpl->m_Stop = true;
		if(m_pImpl->m_Thread.joinable())
			m_pImpl->m_Thread.join();
	}

	bool CKugouSource::Poll(SPlayback &State)
	{
		std::lock_guard<std::mutex> Lock(m_pImpl->m_Mutex);
		State = m_pImpl->m_State;
		if(TClock::now() - m_pImpl->m_Updated > std::chrono::milliseconds(1500))
		{
			const auto ProcessId = State.m_ProcessId;
			State = {};
			State.m_ProcessId = ProcessId;
			State.m_Error = "Waiting for Kugou playback data";
		}
		return State.m_ProcessId != 0;
	}

	int ConfigureKugou(bool Restore, std::string *pMessage)
	{
#if defined(_WIN32)
		const auto Finish = [pMessage](int Code, const std::wstring &Message) {
			if(pMessage)
				*pMessage = Utf8(Message);
			MessageBoxW(nullptr, Message.c_str(), L"QmClient · 酷狗歌词", MB_OK | (Code == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
			return Code;
		};
		if(!KugouProcesses().empty())
			return Finish(1, L"请先完全退出酷狗音乐，再执行启用或恢复。操作完成后请手动重新启动酷狗。");
		const auto Dll = FindLibcef();
		if(Dll.empty())
			return Finish(1, L"未找到受支持的酷狗安装目录。已知补丁来自酷狗 20.1.22.27795 / CEF 89.20.0。");
		const auto BackupPath = Dll + L".qmclient-original";
		const auto Prompt = (Restore ? std::wstring(L"将恢复酷狗原始 libcef.dll 并停用歌词采集。\n") : std::wstring(L"将备份并修改酷狗 libcef.dll，启用本机调试端口 12233 以读取播放信息。\n")) +
				    L"目标：" + Dll + L"\n备份：" + BackupPath + L"\n已有原始备份不会被覆盖。操作后请手动重启酷狗。是否继续？";
		if(MessageBoxW(nullptr, Prompt.c_str(), L"QmClient · 酷狗歌词", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		{
			if(pMessage)
				*pMessage = "Cancelled";
			return ERROR_CANCELLED;
		}
		if(!KugouProcesses().empty())
			return Finish(1, L"酷狗已启动，请退出后重试。");
		// 独占句柄阻止酷狗在校验与写入之间重新加载 DLL。
		const CHandle Current(CreateFileW(Dll.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if(!Current.Valid())
			return Finish(1, L"无法独占写入 libcef.dll。请完全退出酷狗；安装目录需要管理员权限时，请以管理员身份执行 helper 的 --kugou-setup 或 --kugou-restore。");
		const auto Classify = [](HANDLE File) {
			return ClassifyKugouPatch([File](uint64_t Offset, void *pData, size_t Size) { return ReadAt(File, Offset, pData, Size); });
		};
		const auto State = Classify(Current.Get());
		if(State == EKugouPatchState::UNSUPPORTED && !Restore)
			return Finish(1, L"libcef.dll 的九处字节指纹不匹配，或文件已被部分修改。为避免破坏未知版本，未写入任何补丁。");
		if(!IsFile(BackupPath))
		{
			if(Restore || State != EKugouPatchState::ORIGINAL)
				return Finish(1, L"缺少原始备份，无法安全恢复或接管已修改的 DLL。");
			if(!CreateBackup(Current.Get(), BackupPath))
				return Finish(1, L"无法创建完整原始备份，未修改 libcef.dll。已有备份不会被覆盖。");
		}
		const CHandle Backup(CreateFileW(BackupPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if(!Backup.Valid() || Classify(Backup.Get()) != EKugouPatchState::ORIGINAL || !CompareBackup(Backup.Get(), Current.Get(), State != EKugouPatchState::ORIGINAL, Restore))
			return Finish(1, L"当前 libcef.dll 与原始备份不匹配，可能已经升级。未覆盖当前文件或已有备份。");
		if((Restore && State == EKugouPatchState::ORIGINAL) || (!Restore && State == EKugouPatchState::PATCHED))
			return Finish(0, Restore ? L"酷狗已恢复原始状态。请手动启动酷狗。" : L"酷狗歌词接入已启用。请手动启动酷狗。");
		std::vector<std::string> Before;
		for(const auto &Patch : KugouPatches())
		{
			std::string Bytes(Patch.m_Original.size(), '\0');
			if(!ReadAt(Current.Get(), Patch.m_Offset, Bytes.data(), Bytes.size()))
				return Finish(1, L"无法读取补丁前状态，未写入文件。");
			Before.push_back(std::move(Bytes));
		}
		bool Success = true;
		for(const auto &Patch : KugouPatches())
		{
			const auto &Bytes = Restore ? Patch.m_Original : Patch.m_Patched;
			if(!WriteAt(Current.Get(), Patch.m_Offset, Bytes.data(), Bytes.size()))
			{
				Success = false;
				break;
			}
		}
		Success = Success && FlushFileBuffers(Current.Get()) && Classify(Current.Get()) == (Restore ? EKugouPatchState::ORIGINAL : EKugouPatchState::PATCHED);
		if(!Success)
		{
			bool RolledBack = true;
			for(size_t Index = 0; Index < KugouPatches().size(); ++Index)
			{
				const auto &Bytes = Before[Index];
				RolledBack &= WriteAt(Current.Get(), KugouPatches()[Index].m_Offset, Bytes.data(), Bytes.size());
			}
			RolledBack = RolledBack && FlushFileBuffers(Current.Get());
			for(size_t Index = 0; RolledBack && Index < KugouPatches().size(); ++Index)
			{
				std::string Actual(Before[Index].size(), '\0');
				RolledBack = ReadAt(Current.Get(), KugouPatches()[Index].m_Offset, Actual.data(), Actual.size()) && Actual == Before[Index];
			}
			return Finish(1, RolledBack ? L"写入失败，已恢复操作前状态。原始备份仍保留。" : L"写入失败且无法完整回退。请勿启动酷狗；原始备份仍保留，请退出所有相关进程后恢复安装。");
		}
		return Finish(0, Restore ? L"已恢复酷狗原始 libcef.dll，备份保留。请手动启动酷狗。" : L"已备份原始 DLL 并启用酷狗歌词接入。请手动启动酷狗；升级后需重新检查兼容性。");
#else
		(void)Restore;
		if(pMessage)
			*pMessage = "Kugou integration requires Windows";
		return 1;
#endif
	}
}
