#ifndef GAME_CLIENT_QMUI_SETTINGSFONTSELECTION_H
#define GAME_CLIENT_QMUI_SETTINGSFONTSELECTION_H

#include <string>
#include <vector>

// 配置可能来自旧版 PostScript 名称；分隔符差异不能让可用字体显示为空。
inline bool QmFontFamilyMatchesConfig(const char *pConfig, const char *pFamily)
{
	if(pConfig == nullptr || pFamily == nullptr || pFamily[0] == '\0')
		return false;
	bool Matched = false;
	const auto Separator = [](char Chr) { return Chr == ' ' || Chr == '-'; };
	const auto Fold = [](unsigned char Chr) { return Chr >= 'A' && Chr <= 'Z' ? Chr + ('a' - 'A') : Chr; };
	while(*pFamily)
	{
		if(Separator(*pFamily))
		{
			++pFamily;
			continue;
		}
		while(Separator(*pConfig))
			++pConfig;
		if(Fold(static_cast<unsigned char>(*pConfig)) != Fold(static_cast<unsigned char>(*pFamily)))
			return false;
		Matched = true;
		++pConfig;
		++pFamily;
	}
	return Matched && (*pConfig == '\0' || Separator(*pConfig));
}

inline int QmFontFamilySelection(const char *pConfig, const std::vector<std::string> &vFamilies)
{
	int Selected = -1;
	size_t BestLength = 0;
	for(size_t i = 0; i < vFamilies.size(); ++i)
	{
		if(vFamilies[i].size() > BestLength && QmFontFamilyMatchesConfig(pConfig, vFamilies[i].c_str()))
		{
			Selected = static_cast<int>(i);
			BestLength = vFamilies[i].size();
		}
	}
	return Selected;
}

// 自持有名称和指针，目录刷新、配置变化与语言切换时统一重建；未安装选择保留显示，
// 不在渲染阶段改写配置，字体重新出现后自动回到真实候选项。
class CSettingsFontSelection
{
	std::vector<std::string> m_vFamilies;
	std::string m_Config;
	std::string m_Prefix;
	std::string m_EmptyLabel;
	bool m_HasPrefix = false;
	std::vector<std::string> m_vNamesOwned;
	std::vector<const char *> m_vNames;
	int m_Selected = -1;
	bool m_Initialized = false;

public:
	CSettingsFontSelection() = default;
	CSettingsFontSelection(const CSettingsFontSelection &) = delete;
	CSettingsFontSelection &operator=(const CSettingsFontSelection &) = delete;
	CSettingsFontSelection(CSettingsFontSelection &&) = delete;
	CSettingsFontSelection &operator=(CSettingsFontSelection &&) = delete;
	bool Update(const std::vector<std::string> &vFamilies, const char *pConfig, const char *pPrefix, const char *pEmptyLabel)
	{
		const char *pConfigValue = pConfig != nullptr ? pConfig : "";
		const char *pPrefixValue = pPrefix != nullptr ? pPrefix : "";
		const char *pEmptyValue = pEmptyLabel != nullptr ? pEmptyLabel : "";
		if(m_Initialized && m_vFamilies == vFamilies && m_Config == pConfigValue && m_Prefix == pPrefixValue && m_EmptyLabel == pEmptyValue && m_HasPrefix == (pPrefix != nullptr))
			return false;
		m_Initialized = true;
		m_vFamilies = vFamilies;
		m_Config = pConfigValue;
		m_Prefix = pPrefixValue;
		m_EmptyLabel = pEmptyValue;
		m_HasPrefix = pPrefix != nullptr;
		m_vNamesOwned.clear();
		if(pPrefix != nullptr)
			m_vNamesOwned.push_back(m_Prefix);
		m_vNamesOwned.insert(m_vNamesOwned.end(), vFamilies.begin(), vFamilies.end());
		const int Family = QmFontFamilySelection(m_Config.c_str(), vFamilies);
		m_Selected = Family >= 0 ? Family + (pPrefix != nullptr ? 1 : 0) : -1;
		if(m_Config.empty() && pPrefix != nullptr)
			m_Selected = 0;
		if(m_Selected < 0)
		{
			m_Selected = static_cast<int>(m_vNamesOwned.size());
			m_vNamesOwned.push_back(m_Config.empty() ? m_EmptyLabel : m_Config);
		}
		m_vNames.clear();
		m_vNames.reserve(m_vNamesOwned.size());
		for(const auto &Name : m_vNamesOwned)
			m_vNames.push_back(Name.c_str());
		return true;
	}
	int Selected() const { return m_Selected; }
	const std::vector<const char *> &Names() const { return m_vNames; }
	const std::vector<std::string> &Families() const { return m_vFamilies; }
	bool IsFamilySelection(int Index) const
	{
		const int Offset = m_HasPrefix ? 1 : 0;
		return Index >= Offset && static_cast<size_t>(Index - Offset) < m_vFamilies.size();
	}
};

#endif
