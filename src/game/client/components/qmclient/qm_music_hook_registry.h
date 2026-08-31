#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MUSIC_HOOK_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MUSIC_HOOK_REGISTRY_H

#include <engine/shared/config.h>

#include <cstddef>

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

inline const SQmMusicHookEntry *QmMusicHookRegistry(size_t *pCount)
{
	static const SQmMusicHookEntry aEntries[] = {
		{&g_Config.m_QmNeteaseHookEnable, "Enable Netease music Hook", "Enable Netease music Hook", L"cloudmusic.exe"},
		{&g_Config.m_QmSodaHookEnable, "Enable SodaMusic Hook", "Enable SodaMusic Hook", L"SodaMusic.exe"},
		{&g_Config.m_QmSpotifyEnable, "Enable Spotify lyrics", "Enable Spotify lyrics", L"Spotify.exe"},
	};
	if(pCount != nullptr)
		*pCount = sizeof(aEntries) / sizeof(aEntries[0]);
	return aEntries;
}

#endif
