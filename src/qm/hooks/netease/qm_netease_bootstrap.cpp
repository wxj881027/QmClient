#include "qm_netease_bootstrap.h"

#include "qm_netease_cdp.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <windows.h>

#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace QmNeteaseBootstrap
{
	namespace
	{
		constexpr size_t MAX_BOOTSTRAP_FILE_BYTES = 2 * 1024 * 1024;

		std::wstring JoinPath(const std::wstring &Directory, const wchar_t *pName)
		{
			if(Directory.empty() || pName == nullptr || pName[0] == L'\0')
				return {};
			std::wstring Result = Directory;
			if(Result.back() != L'\\' && Result.back() != L'/')
				Result.push_back(L'\\');
			Result.append(pName);
			return Result;
		}

		bool ReadFileBytes(const std::wstring &Path, std::string *pBytes)
		{
			if(pBytes == nullptr || Path.empty())
				return false;
			pBytes->clear();
			HANDLE hFile = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if(hFile == INVALID_HANDLE_VALUE)
				return false;
			LARGE_INTEGER Size{};
			bool Success = GetFileSizeEx(hFile, &Size) != FALSE && Size.QuadPart > 0 && (uint64_t)Size.QuadPart <= MAX_BOOTSTRAP_FILE_BYTES;
			if(Success)
			{
				pBytes->assign((size_t)Size.QuadPart, '\0');
				size_t Offset = 0;
				while(Offset < pBytes->size())
				{
					const DWORD Chunk = (DWORD)std::min<size_t>(pBytes->size() - Offset, 64 * 1024);
					DWORD Read = 0;
					if(!ReadFile(hFile, pBytes->data() + Offset, Chunk, &Read, nullptr) || Read == 0)
					{
						Success = false;
						break;
					}
					Offset += Read;
				}
				Success = Success && Offset == pBytes->size();
			}
			CloseHandle(hFile);
			if(!Success)
				pBytes->clear();
			return Success;
		}

		bool IsPathFile(const std::wstring &Path)
		{
			const DWORD Attributes = GetFileAttributesW(Path.c_str());
			return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		bool IsAccessFailure(DWORD Error)
		{
			return Error == ERROR_ACCESS_DENIED || Error == ERROR_PRIVILEGE_NOT_HELD;
		}

		bool FilesEqual(const std::wstring &FirstPath, const std::wstring &SecondPath)
		{
			std::string First;
			std::string Second;
			return ReadFileBytes(FirstPath, &First) && ReadFileBytes(SecondPath, &Second) && First == Second;
		}

		wchar_t LowerAscii(wchar_t Character)
		{
			return Character >= L'A' && Character <= L'Z' ? Character - L'A' + L'a' : Character;
		}

		bool ContainsInsensitive(std::wstring_view Value, std::wstring_view Needle)
		{
			if(Needle.empty() || Value.size() < Needle.size())
				return false;
			for(size_t Offset = 0; Offset <= Value.size() - Needle.size(); ++Offset)
			{
				bool Matches = true;
				for(size_t Index = 0; Index < Needle.size(); ++Index)
				{
					if(LowerAscii(Value[Offset + Index]) != LowerAscii(Needle[Index]))
					{
						Matches = false;
						break;
					}
				}
				if(Matches)
					return true;
			}
			return false;
		}

		bool HasCloudMusicFileName(std::wstring_view Path)
		{
			constexpr std::wstring_view FILE_NAME = L"cloudmusic.exe";
			size_t FileNameOffset = 0;
			for(size_t Index = 0; Index < Path.size(); ++Index)
			{
				if(Path[Index] == L'\\' || Path[Index] == L'/')
					FileNameOffset = Index + 1;
			}
			const std::wstring_view FileName = Path.substr(FileNameOffset);
			if(FileName.size() != FILE_NAME.size())
				return false;
			for(size_t Index = 0; Index < FILE_NAME.size(); ++Index)
			{
				if(LowerAscii(FileName[Index]) != FILE_NAME[Index])
					return false;
			}
			return true;
		}

		struct SUnicodeString
		{
			USHORT Length = 0;
			USHORT MaximumLength = 0;
			PWSTR Buffer = nullptr;
		};

		struct SProcessParametersPrefix
		{
			BYTE Reserved1[16]{};
			PVOID Reserved2[10]{};
			SUnicodeString ImagePathName;
			SUnicodeString CommandLine;
		};

		struct SPebPrefix
		{
			BYTE Reserved1[2]{};
			BYTE BeingDebugged = 0;
			BYTE Reserved2[1]{};
			PVOID Reserved3[2]{};
			PVOID Ldr = nullptr;
			SProcessParametersPrefix *ProcessParameters = nullptr;
		};

		SPebPrefix *CurrentPeb()
		{
#if defined(_M_X64) || defined(__x86_64__)
#if defined(_MSC_VER)
			return reinterpret_cast<SPebPrefix *>(__readgsqword(0x60));
#else
			return nullptr;
#endif
#elif defined(_M_IX86) || defined(__i386__)
#if defined(_MSC_VER)
			return reinterpret_cast<SPebPrefix *>(__readfsdword(0x30));
#else
			return nullptr;
#endif
#else
			return nullptr;
#endif
		}

		bool HasCommandLineOption(std::wstring_view CommandLine, const wchar_t *pOption)
		{
			return pOption != nullptr && ContainsInsensitive(CommandLine, pOption);
		}

		bool IsChildCefProcess(std::wstring_view CommandLine)
		{
			return HasCommandLineOption(CommandLine, L"--type=") || HasCommandLineOption(CommandLine, L"--type ");
		}

		constexpr wchar_t LOOPBACK_DEBUGGING_ARGUMENTS[] = L"--remote-debugging-address=127.0.0.1 --remote-debugging-port=";
		constexpr size_t LOOPBACK_DEBUGGING_ARGUMENTS_LENGTH = std::size(LOOPBACK_DEBUGGING_ARGUMENTS) - 1;

		size_t DecimalLength(uint16_t Value)
		{
			size_t Length = 1;
			while(Value >= 10)
			{
				Value = (uint16_t)(Value / 10);
				++Length;
			}
			return Length;
		}

		void AppendDecimal(wchar_t *pBuffer, size_t *pOffset, uint16_t Value)
		{
			wchar_t aDigits[5] = {};
			size_t DigitCount = 0;
			do
			{
				aDigits[DigitCount++] = (wchar_t)(L'0' + Value % 10);
				Value = (uint16_t)(Value / 10);
			} while(Value != 0);
			while(DigitCount > 0)
				pBuffer[(*pOffset)++] = aDigits[--DigitCount];
		}

		bool HasDebuggingPort(std::wstring_view CommandLine)
		{
			return HasCommandLineOption(CommandLine, L"--remote-debugging-port=") || HasCommandLineOption(CommandLine, L"--remote-debugging-port ");
		}

		bool WriteCommandLine(SProcessParametersPrefix *pParameters, const std::wstring &CommandLine)
		{
			if(pParameters == nullptr || CommandLine.size() > (std::numeric_limits<USHORT>::max() / sizeof(wchar_t)) - 1)
				return false;
			const USHORT Length = (USHORT)(CommandLine.size() * sizeof(wchar_t));
			const USHORT Required = (USHORT)(Length + sizeof(wchar_t));
			if(pParameters->CommandLine.Buffer != nullptr && Required <= pParameters->CommandLine.MaximumLength)
			{
				std::memcpy(pParameters->CommandLine.Buffer, CommandLine.data(), Length);
				pParameters->CommandLine.Buffer[CommandLine.size()] = L'\0';
				pParameters->CommandLine.Length = Length;
				return true;
			}

			// PEB 中的命令行由进程生命周期持有；容量不足时分配一份永久缓冲，
			// 避免写越界或依赖不可写的原始缓冲区。
			PWSTR pBuffer = static_cast<PWSTR>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required));
			if(pBuffer == nullptr)
				return false;
			std::memcpy(pBuffer, CommandLine.data(), Length);
			pBuffer[CommandLine.size()] = L'\0';
			pParameters->CommandLine.Buffer = pBuffer;
			pParameters->CommandLine.Length = Length;
			pParameters->CommandLine.MaximumLength = Required;
			return true;
		}
	}

	std::wstring SystemVersionDllPath()
	{
		std::vector<wchar_t> Buffer(MAX_PATH);
		for(;;)
		{
			const UINT Length = GetSystemDirectoryW(Buffer.data(), (UINT)Buffer.size());
			if(Length == 0)
				return {};
			if(Length < Buffer.size())
				return JoinPath(std::wstring(Buffer.data(), Length), BOOTSTRAP_TARGET_NAME);
			Buffer.resize((size_t)Length + 1);
		}
	}

	EFileKind InspectFile(const std::wstring &Path)
	{
		if(!IsPathFile(Path))
			return GetFileAttributesW(Path.c_str()) == INVALID_FILE_ATTRIBUTES ? EFileKind::Missing : EFileKind::Unreadable;
		std::string Bytes;
		if(!ReadFileBytes(Path, &Bytes))
			return EFileKind::Unreadable;
		if(Bytes.find(BOOTSTRAP_MARKER) != std::string::npos)
			return EFileKind::CurrentQmClient;
		if(Bytes.find("QmClient.NeteaseBootstrap.") != std::string::npos)
			return EFileKind::OlderQmClient;
		return EFileKind::Foreign;
	}

	EInstallResult EnsureInstalled(const std::wstring &CloudMusicDirectory, const std::wstring &SourcePath, std::wstring *pInstalledPath)
	{
		if(pInstalledPath != nullptr)
			*pInstalledPath = {};
		if(CloudMusicDirectory.empty() || SourcePath.empty())
			return EInstallResult::Failed;
		const EFileKind SourceKind = InspectFile(SourcePath);
		if(SourceKind != EFileKind::CurrentQmClient)
			return EInstallResult::MissingSource;
		const std::wstring TargetPath = JoinPath(CloudMusicDirectory, BOOTSTRAP_TARGET_NAME);
		if(pInstalledPath != nullptr)
			*pInstalledPath = TargetPath;
		const EFileKind TargetKind = InspectFile(TargetPath);
		if(TargetKind == EFileKind::CurrentQmClient && FilesEqual(SourcePath, TargetPath))
			return EInstallResult::AlreadyInstalled;
		if(TargetKind == EFileKind::Foreign || TargetKind == EFileKind::Unreadable)
			return EInstallResult::Conflict;

		std::wstring TemporaryPath = TargetPath;
		TemporaryPath.append(L".qmclient-");
		TemporaryPath.append(std::to_wstring(GetCurrentProcessId()));
		TemporaryPath.push_back(L'-');
		TemporaryPath.append(std::to_wstring(GetTickCount64()));
		TemporaryPath.append(L".tmp");
		if(!CopyFileW(SourcePath.c_str(), TemporaryPath.c_str(), TRUE))
			return IsAccessFailure(GetLastError()) ? EInstallResult::AccessDenied : EInstallResult::Failed;
		const bool ReplacingOwnFile = TargetKind == EFileKind::CurrentQmClient || TargetKind == EFileKind::OlderQmClient;
		const DWORD MoveFlags = MOVEFILE_WRITE_THROUGH | (ReplacingOwnFile ? MOVEFILE_REPLACE_EXISTING : 0);
		if(!MoveFileExW(TemporaryPath.c_str(), TargetPath.c_str(), MoveFlags))
		{
			const DWORD Error = GetLastError();
			DeleteFileW(TemporaryPath.c_str());
			if(Error == ERROR_FILE_EXISTS || Error == ERROR_ALREADY_EXISTS)
				return InspectFile(TargetPath) == EFileKind::CurrentQmClient ? EInstallResult::AlreadyInstalled : EInstallResult::Conflict;
			return IsAccessFailure(Error) ? EInstallResult::AccessDenied : EInstallResult::Failed;
		}
		if(InspectFile(TargetPath) != EFileKind::CurrentQmClient || !FilesEqual(SourcePath, TargetPath))
		{
			// 目标文件可能在校验前被其他插件替换；失败时保留它，避免竞态删除 foreign DLL。
			return EInstallResult::Failed;
		}
		return ReplacingOwnFile ? EInstallResult::Updated : EInstallResult::Installed;
	}

	bool IsCloudMusicExecutable(const std::wstring &ExecutablePath)
	{
		return HasCloudMusicFileName(ExecutablePath);
	}

	uint16_t ChooseLoopbackPort(uint32_t ProcessId)
	{
		WSADATA Data{};
		if(WSAStartup(MAKEWORD(2, 2), &Data) != 0)
			return 0;
		uint16_t Result = 0;
		for(uint32_t Offset = 0; Offset < QmNeteaseCdp::DYNAMIC_PORT_CANDIDATE_COUNT; ++Offset)
		{
			const uint16_t Port = QmNeteaseCdp::DynamicPortCandidate(ProcessId, Offset);
			const SOCKET Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(Socket == INVALID_SOCKET)
				continue;
			sockaddr_in Address{};
			Address.sin_family = AF_INET;
			Address.sin_port = htons(Port);
			inet_pton(AF_INET, "127.0.0.1", &Address.sin_addr);
			if(bind(Socket, reinterpret_cast<const sockaddr *>(&Address), sizeof(Address)) == 0)
				Result = Port;
			closesocket(Socket);
			if(Result != 0)
				break;
		}
		WSACleanup();
		return Result;
	}

	uint16_t EarlyLoopbackPort(uint32_t ProcessId)
	{
		return QmNeteaseCdp::DynamicPortCandidate(ProcessId, 0);
	}

	bool BuildLoopbackDebuggingCommandLine(std::wstring_view ExecutablePath, std::wstring_view CommandLine, uint16_t Port, std::wstring *pPatchedCommandLine)
	{
		if(pPatchedCommandLine == nullptr || Port == 0 || !HasCloudMusicFileName(ExecutablePath) || IsChildCefProcess(CommandLine))
			return false;
		pPatchedCommandLine->assign(CommandLine.data(), CommandLine.size());
		if(HasDebuggingPort(CommandLine))
			return true;
		if(!CommandLine.empty())
			pPatchedCommandLine->push_back(L' ');
		pPatchedCommandLine->append(LOOPBACK_DEBUGGING_ARGUMENTS);
		pPatchedCommandLine->append(std::to_wstring(Port));
		return true;
	}

	bool PatchCurrentProcessCommandLineEarly(uint16_t Port)
	{
		if(Port == 0)
			return false;
		SPebPrefix *pPeb = CurrentPeb();
		if(pPeb == nullptr || pPeb->ProcessParameters == nullptr)
			return false;
		SProcessParametersPrefix *pParameters = pPeb->ProcessParameters;
		const SUnicodeString &ImagePath = pParameters->ImagePathName;
		const SUnicodeString &CommandLine = pParameters->CommandLine;
		if(ImagePath.Buffer == nullptr || CommandLine.Buffer == nullptr || ImagePath.Length % sizeof(wchar_t) != 0 || CommandLine.Length % sizeof(wchar_t) != 0)
			return false;
		const std::wstring_view ExecutablePath(ImagePath.Buffer, ImagePath.Length / sizeof(wchar_t));
		const std::wstring_view CurrentCommandLine(CommandLine.Buffer, CommandLine.Length / sizeof(wchar_t));
		if(!HasCloudMusicFileName(ExecutablePath) || IsChildCefProcess(CurrentCommandLine))
			return false;
		if(HasDebuggingPort(CurrentCommandLine))
			return true;

		const size_t NewLength = CurrentCommandLine.size() + (CurrentCommandLine.empty() ? 0 : 1) + LOOPBACK_DEBUGGING_ARGUMENTS_LENGTH + DecimalLength(Port);
		if(NewLength > (std::numeric_limits<USHORT>::max() / sizeof(wchar_t)) - 1)
			return false;
		const size_t RequiredBytes = (NewLength + 1) * sizeof(wchar_t);
		// 进程堆在 loader lock 下已可用；不复用原始 PEB 缓冲可避免容量不足和越界。
		PWSTR pBuffer = static_cast<PWSTR>(HeapAlloc(GetProcessHeap(), 0, RequiredBytes));
		if(pBuffer == nullptr)
			return false;
		size_t Offset = 0;
		for(const wchar_t Character : CurrentCommandLine)
			pBuffer[Offset++] = Character;
		if(!CurrentCommandLine.empty())
			pBuffer[Offset++] = L' ';
		for(size_t Index = 0; Index < LOOPBACK_DEBUGGING_ARGUMENTS_LENGTH; ++Index)
			pBuffer[Offset++] = LOOPBACK_DEBUGGING_ARGUMENTS[Index];
		AppendDecimal(pBuffer, &Offset, Port);
		pBuffer[Offset] = L'\0';
		pParameters->CommandLine.Buffer = pBuffer;
		pParameters->CommandLine.Length = (USHORT)(NewLength * sizeof(wchar_t));
		pParameters->CommandLine.MaximumLength = (USHORT)RequiredBytes;
		return true;
	}

	bool PatchCurrentProcessCommandLine(uint16_t Port)
	{
		if(Port == 0)
			return false;
		wchar_t aExecutable[MAX_PATH] = {};
		const DWORD ExecutableLength = GetModuleFileNameW(nullptr, aExecutable, (DWORD)std::size(aExecutable));
		if(ExecutableLength == 0 || ExecutableLength >= std::size(aExecutable))
			return false;
		const wchar_t *pCurrent = GetCommandLineW();
		if(pCurrent == nullptr)
			return false;
		std::wstring Current(pCurrent);
		std::wstring Appended;
		if(!BuildLoopbackDebuggingCommandLine(std::wstring_view(aExecutable, ExecutableLength), Current, Port, &Appended))
			return false;
		if(Appended == Current)
			return true;
		SPebPrefix *pPeb = CurrentPeb();
		if(pPeb == nullptr || pPeb->ProcessParameters == nullptr)
			return false;
		return WriteCommandLine(pPeb->ProcessParameters, Appended);
	}

} // namespace QmNeteaseBootstrap
