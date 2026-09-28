#ifndef QM_HOOKS_NETEASE_QM_NETEASE_BOOTSTRAP_H
#define QM_HOOKS_NETEASE_QM_NETEASE_BOOTSTRAP_H

#include <cstdint>
#include <string>
#include <string_view>

namespace QmNeteaseBootstrap
{
	// 该标记同时用于部署冲突检测和版本升级判断；它不是安全凭据。
	constexpr char BOOTSTRAP_MARKER[] = "QmClient.NeteaseBootstrap.v1";
	constexpr wchar_t BOOTSTRAP_TARGET_NAME[] = L"version.dll";
	constexpr wchar_t BOOTSTRAP_SOURCE_NAME[] = L"qm-nmt-bootstrap.dll";

	enum class EFileKind : uint8_t
	{
		Missing = 0,
		CurrentQmClient = 1,
		OlderQmClient = 2,
		Foreign = 3,
		Unreadable = 4,
	};

	enum class EInstallResult : uint8_t
	{
		AlreadyInstalled = 0,
		Installed = 1,
		Updated = 2,
		MissingSource = 3,
		Conflict = 4,
		AccessDenied = 5,
		Failed = 6,
	};

	// 使用系统目录 API 获取真实系统 version.dll 路径，禁止依赖固定盘符。
	std::wstring SystemVersionDllPath();
	EFileKind InspectFile(const std::wstring &Path);
	EInstallResult EnsureInstalled(const std::wstring &CloudMusicDirectory, const std::wstring &SourcePath, std::wstring *pInstalledPath = nullptr);

	bool IsCloudMusicExecutable(const std::wstring &ExecutablePath);
	uint16_t ChooseLoopbackPort(uint32_t ProcessId);
	// DLL_PROCESS_ATTACH 使用首个确定性候选，避免在 loader lock 下执行 Winsock 探测。
	uint16_t EarlyLoopbackPort(uint32_t ProcessId);
	// 仅构造启动参数，供单元测试和常规代码复用；不会读取或修改当前进程。
	bool BuildLoopbackDebuggingCommandLine(std::wstring_view ExecutablePath, std::wstring_view CommandLine, uint16_t Port, std::wstring *pPatchedCommandLine);
	// 仅在 DLL_PROCESS_ATTACH 中调用：不进行网络、文件、线程或 CDP 操作。
	bool PatchCurrentProcessCommandLineEarly(uint16_t Port);
	// 代理加载后由普通代码调用；不会在 DllMain 中执行。
	bool PatchCurrentProcessCommandLine(uint16_t Port);

} // namespace QmNeteaseBootstrap

#endif
