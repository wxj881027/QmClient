#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MUSIC_HOOK_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MUSIC_HOOK_REGISTRY_H

#include <engine/shared/config.h>

#include <cstddef>
#include <cstdint>

// 音乐 Hook 注册表:一个 Hook 一行。未来新增音乐客户端 Hook 时在此追加一行,
// 设置页的互斥开关与「跟随启动应用」的自动切换都会自动覆盖新条目。
// 注意:设置页文案需要像现有条目一样在 translations/i18n 源文件中登记
// (extract_strings 只扫描字面量 Localize 调用)。
struct SQmMusicHookEntry
{
	// 启用开关配置项指针(0=关闭,非 0=启用)。
	int *m_pEnableConfig;
	// 设置页按钮 id 与显示文案(显示文案为英文源串,运行时经 Localize 翻译)。
	const char *m_pSettingsTextId;
	const char *m_pSettingsText;
	// Windows 下目标音乐应用主进程名(如 cloudmusic.exe);非 Windows 平台不使用。
	const wchar_t *m_pProcessName;
};

// 对一条进程快照同时匹配所有已注册 Hook，避免为每个应用重复遍历系统进程表。
inline uint64_t QmMusicHookMaskForProcess(const wchar_t *pProcessName, const SQmMusicHookEntry *pHooks, size_t Count)
{
	if(pProcessName == nullptr || pHooks == nullptr)
		return 0;
	const auto EqualsIgnoreCase = [](const wchar_t *pLeft, const wchar_t *pRight) {
		for(;;)
		{
			const wchar_t Left = *pLeft++;
			const wchar_t Right = *pRight++;
			const wchar_t LowerLeft = Left >= L'A' && Left <= L'Z' ? (wchar_t)(Left - L'A' + L'a') : Left;
			const wchar_t LowerRight = Right >= L'A' && Right <= L'Z' ? (wchar_t)(Right - L'A' + L'a') : Right;
			if(LowerLeft != LowerRight)
				return false;
			if(LowerLeft == L'\0')
				return true;
		}
	};
	uint64_t Mask = 0;
	for(size_t i = 0; i < Count && i < 64; ++i)
		if(pHooks[i].m_pProcessName != nullptr && EqualsIgnoreCase(pProcessName, pHooks[i].m_pProcessName))
			Mask |= uint64_t(1) << i;
	return Mask;
}

inline const SQmMusicHookEntry *QmMusicHookRegistry(size_t *pCount)
{
	static const SQmMusicHookEntry aEntries[] = {
		{&g_Config.m_QmNeteaseHookEnable, "Enable Netease music Hook", "Enable Netease music Hook", L"cloudmusic.exe"},
		{&g_Config.m_QmSodaHookEnable, "Enable SodaMusic Hook", "Enable SodaMusic Hook", L"SodaMusic.exe"},
		{&g_Config.m_QmSpotifyEnable, "Enable Spotify lyrics", "Enable Spotify lyrics", L"Spotify.exe"},
		{&g_Config.m_QmKugouHookEnable, "Enable Kugou Music lyrics", "Enable Kugou Music lyrics", L"KuGou.exe"},
		{&g_Config.m_QmQQMusicHookEnable, "Enable QQ Music lyrics", "Enable QQ Music lyrics", L"QQMusic.exe"},
	};
	if(pCount != nullptr)
		*pCount = sizeof(aEntries) / sizeof(aEntries[0]);
	return aEntries;
}

#endif
