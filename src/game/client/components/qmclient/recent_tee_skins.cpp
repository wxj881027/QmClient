#include "recent_tee_skins.h"

#include <engine/shared/config.h>

#include <game/client/components/skins.h>

std::optional<CSkins::CSkinListEntry> CSkins::RecentSkinListEntry(const SQmRecentTeeSkin &Skin)
{
	// 记录保留原始名字；列表按可见优先级加载，不能在枚举时套前缀或立即下载全部记录。
	const CSkinContainer *pContainer = FindContainerImpl(Skin.m_Name.c_str(), false);
	if(pContainer == nullptr || pContainer->IsSpecial() || (g_Config.m_ClVanillaSkinsOnly && !pContainer->IsVanilla()))
		return std::nullopt;
	return MakeSkinListEntry(pContainer, CSkinListEntry::SColorKey{Skin.m_UseCustomColor, static_cast<int>(Skin.m_ColorBody), static_cast<int>(Skin.m_ColorFeet)});
}

void CSkins::RecordRecentSkin(int Dummy)
{
	m_RecentSkins.Record(QmCurrentTeeSkin(g_Config, Dummy != 0));
}

void CSkins::ConRecentSkin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->m_RecentSkins.Record({pResult->GetString(0), pResult->GetInteger(1) != 0,
		static_cast<unsigned>(pResult->GetInteger(2)), static_cast<unsigned>(pResult->GetInteger(3))});
}

void CSkins::ConfigSaveRecentSkinsCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->m_RecentSkins.CommitPending();
	pSelf->m_RecentSkins.ForEachSavedEntry([pConfigManager](const SQmRecentTeeSkin &Entry) {
		char aEscaped[MAX_SKIN_LENGTH * 2];
		char *pDst = aEscaped;
		str_escape(&pDst, Entry.m_Name.c_str(), aEscaped + sizeof(aEscaped));
		char aLine[MAX_SKIN_LENGTH * 2 + 96];
		str_format(aLine, sizeof(aLine), "qm_recent_tee_skin \"%s\" %d %u %u", aEscaped, Entry.m_UseCustomColor ? 1 : 0, Entry.m_ColorBody, Entry.m_ColorFeet);
		pConfigManager->WriteLine(aLine, ConfigDomain::QMCLIENT);
	});
}
