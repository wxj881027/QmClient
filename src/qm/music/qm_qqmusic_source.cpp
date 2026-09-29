#include "qm_qqmusic_source.h"

#include "qm_qqmusic_protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>
#include <winhttp.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <vector>

namespace QmMusicHook
{
	namespace
	{
		class CHandle
		{
		public:
			HANDLE m_Value = nullptr;
			explicit CHandle(HANDLE Value = nullptr) :
				m_Value(Value) {}
			~CHandle() { Reset(); }
			void Reset(HANDLE Value = nullptr)
			{
				if(m_Value && m_Value != INVALID_HANDLE_VALUE)
					CloseHandle(m_Value);
				m_Value = Value;
			}
		};

		class CInternet
		{
		public:
			HINTERNET m_Value;
			explicit CInternet(HINTERNET Value) :
				m_Value(Value) {}
			~CInternet()
			{
				if(m_Value)
					WinHttpCloseHandle(m_Value);
			}
		};

		int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		struct SFetchResult
		{
			uint64_t m_Generation = 0;
			bool m_Success = false;
			QQMusic::SLyrics m_Lyrics;
		};

		SFetchResult FetchLyrics(uint64_t Generation, uint32_t SongId, const std::string &SongMid)
		{
			SFetchResult Result;
			Result.m_Generation = Generation;
			const bool MidRequest = !SongMid.empty();
			const wchar_t *pHost = MidRequest ? L"c.y.qq.com" : L"u.y.qq.com";
			std::wstring Path = L"/cgi-bin/musicu.fcg";
			std::string Payload;
			if(MidRequest)
				Path = L"/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" + std::wstring(SongMid.begin(), SongMid.end()) + L"&g_tk=5381&format=json&nobase64=1";
			else
				Payload = "{\"comm\":{\"ct\":19,\"cv\":2216},\"req_0\":{\"module\":\"music.musichallSong.PlayLyricInfo\",\"method\":\"GetPlayLyricInfo\",\"param\":{\"songId\":" + std::to_string(SongId) + ",\"crypt\":1,\"lrc_t\":0,\"qrc\":1,\"qrc_t\":0,\"trans\":1,\"trans_t\":0,\"type\":1,\"ct\":19,\"cv\":2216}}}";
			CInternet Session(WinHttpOpen(L"QQMusic/2216", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
			if(!Session.m_Value)
				return Result;
			WinHttpSetTimeouts(Session.m_Value, 2000, 2000, 2000, 2000);
			CInternet Connection(WinHttpConnect(Session.m_Value, pHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
			if(!Connection.m_Value)
				return Result;
			CInternet Request(WinHttpOpenRequest(Connection.m_Value, MidRequest ? L"GET" : L"POST", Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
			if(!Request.m_Value)
				return Result;
			// 请求只含应用里的精确歌曲 ID，禁止系统凭据、Cookie 和跨站重定向。
			DWORD Disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS;
			DWORD Autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
			WinHttpSetOption(Request.m_Value, WINHTTP_OPTION_DISABLE_FEATURE, &Disable, sizeof(Disable));
			WinHttpSetOption(Request.m_Value, WINHTTP_OPTION_AUTOLOGON_POLICY, &Autologon, sizeof(Autologon));
			const wchar_t *pHeaders = L"Referer: https://y.qq.com/\r\nContent-Type: application/json\r\n";
			if(!WinHttpSendRequest(Request.m_Value, pHeaders, static_cast<DWORD>(-1), Payload.empty() ? WINHTTP_NO_REQUEST_DATA : Payload.data(), static_cast<DWORD>(Payload.size()), static_cast<DWORD>(Payload.size()), 0) || !WinHttpReceiveResponse(Request.m_Value, nullptr))
				return Result;
			DWORD Status = 0;
			DWORD StatusSize = sizeof(Status);
			if(!WinHttpQueryHeaders(Request.m_Value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX) || Status != 200)
				return Result;
			std::string Response;
			std::array<char, 16384> aBuffer;
			const int64_t Deadline = NowMs() + 6000;
			while(NowMs() < Deadline)
			{
				DWORD Read = 0;
				if(!WinHttpReadData(Request.m_Value, aBuffer.data(), static_cast<DWORD>(aBuffer.size()), &Read))
					return Result;
				if(Read == 0)
				{
					Result.m_Success = QQMusic::ParseLyricsResponse(Response, SongId, MidRequest, Result.m_Lyrics);
					return Result;
				}
				if(Response.size() + Read > 4 * 1024 * 1024)
					return Result;
				Response.append(aBuffer.data(), Read);
			}
			return Result;
		}

		struct SSample
		{
			std::string m_Title;
			std::string m_Artist;
			std::string m_Album;
			std::string m_Mid;
			uint32_t m_SongId = 0;
			uint32_t m_Duration = 0;
			uint32_t m_Progress = 0;

			std::string Metadata() const
			{
				return m_Title + '\n' + m_Artist + '\n' + std::to_string(m_Duration);
			}
			std::string Key() const
			{
				if(!m_Mid.empty())
					return "mid:" + m_Mid;
				return m_SongId ? "id:" + std::to_string(m_SongId) : "";
			}
		};
	}

	class CQQMusicSource::CImpl
	{
		CHandle m_Process;
		DWORD m_ProcessId = 0;
		uintptr_t m_Module = 0;
		DWORD m_ModuleSize = 0;
		const QQMusic::SOffsets *m_pOffsets = nullptr;
		std::string m_AttachError;
		int64_t m_NextAttach = 0;
		QQMusic::CPlaybackClock m_Clock;
		QQMusic::CSongIdentity m_Identity;
		std::string m_CurrentIdentity;
		uint64_t m_Generation = 0;
		QQMusic::SLyrics m_Lyrics;
		std::future<SFetchResult> m_Fetch;
		int m_Attempts = 0;
		int64_t m_NextFetch = 0;
		int64_t m_SongSince = 0;
		bool m_Fetched = false;

		bool Read(uintptr_t Address, void *pOut, size_t Size) const
		{
			SIZE_T ReadSize = 0;
			return Address >= 0x10000 && ReadProcessMemory(m_Process.m_Value, reinterpret_cast<const void *>(Address), pOut, Size, &ReadSize) && ReadSize == Size;
		}

		bool Read32(uintptr_t Address, uint32_t &Out) const
		{
			return Read(Address, &Out, sizeof(Out));
		}

		std::string WideString(uint32_t Pointer) const
		{
			if(Pointer < 0x10000)
				return {};
			std::array<wchar_t, 512> aWide{};
			SIZE_T BytesRead = 0;
			ReadProcessMemory(m_Process.m_Value, reinterpret_cast<const void *>(static_cast<uintptr_t>(Pointer)), aWide.data(), sizeof(aWide), &BytesRead);
			const auto End = std::find(aWide.begin(), aWide.begin() + BytesRead / sizeof(wchar_t), L'\0');
			if(End == aWide.begin() || End == aWide.begin() + BytesRead / sizeof(wchar_t))
				return {};
			const int Length = static_cast<int>(End - aWide.begin());
			const int Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, aWide.data(), Length, nullptr, 0, nullptr, nullptr);
			if(Size <= 0)
				return {};
			std::string Result(Size, '\0');
			WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, aWide.data(), Length, Result.data(), Size, nullptr, nullptr);
			return Result;
		}

		std::string ReadString(uintptr_t Address) const
		{
			if(m_pOffsets->m_Wide)
			{
				uint32_t Pointer;
				return Read32(Address, Pointer) ? WideString(Pointer) : std::string();
			}
			std::array<unsigned char, 24> aData{};
			QQMusic::SSsoLayout Layout;
			if(!Read(Address, aData.data(), aData.size()) || !QQMusic::DecodeSsoLayout(aData.data(), aData.size(), Layout))
				return {};
			std::string Result(Layout.m_Length, '\0');
			if(Layout.m_Pointer)
			{
				if(!Read(Layout.m_Pointer, Result.data(), Result.size()))
					return {};
			}
			else
				std::memcpy(Result.data(), aData.data(), Result.size());
			if(Result.find('\0') != std::string::npos || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Result.data(), static_cast<int>(Result.size()), nullptr, 0) <= 0)
				return {};
			return Result;
		}

		void Disconnect()
		{
			m_Process.Reset();
			m_ProcessId = 0;
			m_Module = 0;
			m_pOffsets = nullptr;
			m_CurrentIdentity.clear();
			m_Lyrics = {};
			m_Identity = {};
			m_Clock = {};
			m_Attempts = 0;
			m_Fetched = false;
			++m_Generation;
		}

		void Attach()
		{
			CHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
			if(Snapshot.m_Value == INVALID_HANDLE_VALUE)
				return;
			PROCESSENTRY32W Entry{};
			Entry.dwSize = sizeof(Entry);
			for(BOOL Ok = Process32FirstW(Snapshot.m_Value, &Entry); Ok; Ok = Process32NextW(Snapshot.m_Value, &Entry))
			{
				if(_wcsicmp(Entry.szExeFile, L"QQMusic.exe") != 0)
					continue;
				m_Process.Reset(OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID));
				if(!m_Process.m_Value)
					continue;
				CHandle Modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, Entry.th32ProcessID));
				MODULEENTRY32W Module{};
				Module.dwSize = sizeof(Module);
				if(Modules.m_Value != INVALID_HANDLE_VALUE)
				{
					for(BOOL HasModule = Module32FirstW(Modules.m_Value, &Module); HasModule; HasModule = Module32NextW(Modules.m_Value, &Module))
					{
						if(_wcsicmp(Module.szModule, L"QQMusic.dll") == 0)
						{
							m_Module = reinterpret_cast<uintptr_t>(Module.modBaseAddr);
							m_ModuleSize = Module.modBaseSize;
							break;
						}
					}
				}
				if(!m_Module)
				{
					m_Process.Reset();
					continue;
				}
				m_ProcessId = Entry.th32ProcessID;
				m_AttachError = "QQ Music version is unsupported";
				IMAGE_DOS_HEADER Dos{};
				IMAGE_NT_HEADERS32 Pe{};
				if(m_ModuleSize < sizeof(Pe) || !Read(m_Module, &Dos, sizeof(Dos)) || Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew <= 0 || static_cast<uint32_t>(Dos.e_lfanew) > m_ModuleSize - sizeof(Pe) || !Read(m_Module + Dos.e_lfanew, &Pe, sizeof(Pe)) || Pe.Signature != IMAGE_NT_SIGNATURE || Pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
					return;
				std::array<wchar_t, 32768> aPath{};
				DWORD PathSize = static_cast<DWORD>(aPath.size());
				if(!QueryFullProcessImageNameW(m_Process.m_Value, 0, aPath.data(), &PathSize))
					return;
				DWORD Ignored;
				const DWORD VersionSize = GetFileVersionInfoSizeW(aPath.data(), &Ignored);
				if(VersionSize == 0 || VersionSize > 1024 * 1024)
					return;
				std::vector<unsigned char> Version(VersionSize);
				VS_FIXEDFILEINFO *pInfo = nullptr;
				UINT InfoSize = 0;
				if(!GetFileVersionInfoW(aPath.data(), 0, VersionSize, Version.data()) || !VerQueryValueW(Version.data(), L"\\", reinterpret_cast<void **>(&pInfo), &InfoSize) || InfoSize < sizeof(VS_FIXEDFILEINFO) || pInfo->dwSignature != 0xFEEF04BD)
					return;
				const int Major = HIWORD(pInfo->dwProductVersionMS);
				const int Minor = LOWORD(pInfo->dwProductVersionMS);
				m_pOffsets = QQMusic::FindOffsets(Major, Minor);
				if(!m_pOffsets)
				{
					m_AttachError += " (" + std::to_string(Major) + "." + std::to_string(Minor) + ")";
					return;
				}
				const uint32_t LargestField = std::max({m_pOffsets->m_SongId, m_pOffsets->m_Duration, m_pOffsets->m_SessionDuration, m_pOffsets->m_Progress});
				if(m_pOffsets->m_Struct + LargestField + 4 >= m_ModuleSize || m_pOffsets->m_TimerPointer >= m_ModuleSize || m_pOffsets->m_AbsoluteProgress >= m_ModuleSize)
					m_pOffsets = nullptr;
				return;
			}
		}

		bool ReadSample(SSample &Out) const
		{
			Out = {};
			const auto &Offsets = *m_pOffsets;
			const uintptr_t Base = m_Module + Offsets.m_Struct;
			Out.m_Title = ReadString(Base + Offsets.m_Name);
			Out.m_Artist = ReadString(Base + Offsets.m_Singer);
			Out.m_Album = ReadString(Base + Offsets.m_Album);
			if(!Read32(Base + Offsets.m_Duration, Out.m_Duration) || !Read32(Base + Offsets.m_SongId, Out.m_SongId))
				return false;
			if(Offsets.m_DurationSeconds)
			{
				if(Out.m_Duration > 86400)
					return false;
				Out.m_Duration *= 1000;
				// 20.05 的数字字段不是服务端 songId，只使用应用的流 URL/参数中的 MID。
				Out.m_SongId = 0;
				uint32_t Url = 0, Parameters = 0;
				Read32(Base + 0x80, Url);
				Read32(Base + 0xAC, Parameters);
				Out.m_Mid = QQMusic::ExtractSongMid(WideString(Url), WideString(Parameters));
			}
			if(Out.m_SongId >= 0x3F000000)
				Out.m_SongId = 0;
			if(Offsets.m_SessionDuration)
			{
				uint32_t SessionDuration;
				if(!Read32(Base + Offsets.m_SessionDuration, SessionDuration) || SessionDuration != Out.m_Duration)
					Out.m_SongId = 0;
			}
			uintptr_t ProgressAddress = Base + Offsets.m_Progress;
			if(Offsets.m_AbsoluteProgress)
				ProgressAddress = m_Module + Offsets.m_AbsoluteProgress;
			if(Offsets.m_TimerPointer)
			{
				uint32_t Pointer;
				if(Read32(m_Module + Offsets.m_TimerPointer, Pointer) && Pointer >= 0x10000)
					ProgressAddress = static_cast<uintptr_t>(Pointer) + Offsets.m_TimerField;
			}
			if(!Read32(ProgressAddress, Out.m_Progress))
				return false;
			return !Out.m_Title.empty() && Out.m_Title != "QQ音乐" && Out.m_Title.find("QQ音乐 听我想听") != 0 && Out.m_Duration > 0 && Out.m_Duration <= 86400000 && Out.m_Progress <= Out.m_Duration + 2000;
		}

	public:
		bool Poll(SPlayback &Out)
		{
			Out = {};
			const int64_t Now = NowMs();
			if(m_Process.m_Value)
			{
				DWORD ExitCode = 0;
				if(!GetExitCodeProcess(m_Process.m_Value, &ExitCode) || ExitCode != STILL_ACTIVE)
					Disconnect();
			}
			if(!m_Process.m_Value && Now >= m_NextAttach)
			{
				m_NextAttach = Now + 2000;
				Attach();
			}
			if(!m_Process.m_Value)
			{
				Out.m_Error = "Waiting for QQ Music";
				return false;
			}
			Out.m_ProcessId = m_ProcessId;
			if(!m_pOffsets)
			{
				Out.m_Error = m_AttachError;
				return true;
			}
			SSample Sample, Confirm;
			if(!ReadSample(Sample) || !ReadSample(Confirm) || Sample.Metadata() != Confirm.Metadata() || Sample.Key() != Confirm.Key())
			{
				if(!m_CurrentIdentity.empty())
				{
					m_CurrentIdentity.clear();
					m_Lyrics = {};
					m_Identity.Suspend();
					m_Clock = {};
					++m_Generation;
				}
				Out.m_Error = "Waiting for QQ Music playback";
				return true;
			}
			const std::string Key = m_Identity.Update(Sample.m_Title, Sample.m_Artist, Sample.m_Duration, Sample.Key(), Now);
			// 已确认的同首歌补齐歌手时保留取词任务；真实切歌会先退到 pending 并清空旧歌词。
			const std::string Identity = Key.empty() ? "pending:" + Sample.Metadata() : Key;
			if(m_CurrentIdentity != Identity)
			{
				m_CurrentIdentity = Identity;
				m_Lyrics = {};
				m_Attempts = 0;
				m_NextFetch = Now;
				m_SongSince = Now;
				m_Fetched = false;
				++m_Generation;
			}
			if(m_Fetch.valid() && m_Fetch.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
			{
				SFetchResult Result = m_Fetch.get();
				if(Result.m_Generation == m_Generation)
				{
					m_Fetched = Result.m_Success;
					if(Result.m_Success)
						m_Lyrics = std::move(Result.m_Lyrics);
					else
						m_NextFetch = Now + 2000 * m_Attempts;
				}
			}
			if(!Key.empty() && !m_Fetch.valid() && !m_Fetched && m_Attempts < 3 && Now >= m_NextFetch)
			{
				++m_Attempts;
				m_Fetch = std::async(std::launch::async, FetchLyrics, m_Generation, Sample.m_SongId, Sample.m_Mid);
			}
			Out.m_HasSong = true;
			Out.m_MediaId = "qqmusic:" + (Key.empty() ? "pending:" + Sample.Metadata() : Key);
			Out.m_Title = Sample.m_Title;
			Out.m_Artist = Sample.m_Artist;
			Out.m_Album = Sample.m_Album;
			Out.m_DurationMs = Sample.m_Duration;
			Out.m_PositionMs = std::min(Sample.m_Progress, Sample.m_Duration);
			Out.m_PositionValid = true;
			Out.m_Playing = m_Clock.Update(Identity, Out.m_PositionMs, Now);
			Out.m_Loading = !m_Fetched && (Key.empty() ? Now - m_SongSince < 8000 : m_Attempts < 3 || m_Fetch.valid());
			Out.m_LyricType = m_Lyrics.m_Type;
			Out.m_LyricContent = m_Lyrics.m_Content;
			Out.m_TranslationLrc = m_Lyrics.m_Translation;
			if(Key.empty())
				Out.m_Error = "Waiting for QQ Music song identity";
			else if(!m_Fetched && m_Attempts >= 3 && !m_Fetch.valid())
				Out.m_Error = "QQ Music lyrics are unavailable";
			return true;
		}
	};

	CQQMusicSource::CQQMusicSource() :
		m_pImpl(std::make_unique<CImpl>()) {}
	CQQMusicSource::~CQQMusicSource() = default;

	bool CQQMusicSource::Poll(SPlayback &Out)
	{
		return m_pImpl->Poll(Out);
	}
}
