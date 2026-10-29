#ifndef ENGINE_CLIENT_STEAM_PROBE_H
#define ENGINE_CLIENT_STEAM_PROBE_H

#include <base/detect.h>

enum class ESteamClientSource;

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <cstddef>
#include <optional>
#include <string>

// 容量按wchar_t元素计，注册表返回长度按字节计；不能借用缓冲外的终止符。
inline std::optional<std::string> SteamProbeDecodeRegistryString(const wchar_t *pValue, std::size_t Capacity, std::size_t Bytes)
{
	if(!pValue || Bytes == 0 || Bytes % sizeof(wchar_t) != 0 || Bytes / sizeof(wchar_t) > Capacity)
		return std::nullopt;
	return windows_wide_to_utf8_bounded(pValue, pValue + Bytes / sizeof(wchar_t));
}

// Steam 客户端探测链的内部实现（仅 Windows），供 steam.cpp 与 qm-benchmarks 使用。
// 所有函数只读注册表、文件系统与进程快照，不启动任何进程、不弹任何窗口，
// 可安全地在启动路径与基准中重复调用。

// 完整探测链：注册表（HKCU SteamExe/SteamPath → HKLM 64/32 位视图 InstallPath
// → App Paths 默认值）→ 运行中的 steam.exe 进程 → 常见安装目录 → PATH 搜索。
// 所有候选都经 fs_is_file 确认存在；找到时把 steam.exe 完整路径写入 pBuffer
// 并返回 true，未安装或无法确定时返回 false。
bool SteamProbeFindClientWindows(char *pBuffer, int BufferSize, ESteamClientSource *pSource = nullptr);
bool SteamProbeValidateManualPath(const char *pCandidate, char *pBuffer, int BufferSize);

// 单层探测：枚举运行中的 steam.exe 进程并取其映像路径。这是探测链中最贵的
// 一层（全系统进程快照），单独暴露供基准测量未安装 Steam 时冷路径的主导项。
bool SteamProbePathFromRunningProcess(char *pBuffer, int BufferSize);

#endif

#endif // ENGINE_CLIENT_STEAM_PROBE_H
