#ifndef ENGINE_SHARED_QM_DEFAULT_PROFILE_H
#define ENGINE_SHARED_QM_DEFAULT_PROFILE_H

#include <base/str.h>

#include <engine/shared/config.h>

// 默认值迁移版本：每加一次「老用户也要跟随的新默认」，版本号 +1。
// 1 = ui_color / br_filter_login / qm_custom_font；2 = br_demo_fetch_info 默认改为常开。
inline constexpr int QM_DEFAULTS_PROFILE_VERSION = 2;

inline bool QmConfigValueWasExplicitlySet(IConfigManager &Manager, const char *pName)
{
	struct SQuery
	{
		const char *m_pName;
		bool m_Explicit = false;
	} Query{pName};
	Manager.PossibleConfigVariables(pName, CFGFLAG_CLIENT, [](const SConfigVariable *pVariable, void *pUser) {
		auto &Query = *static_cast<SQuery *>(pUser);
		if(str_comp(pVariable->m_pScriptName, Query.m_pName) == 0)
			Query.m_Explicit = pVariable->m_HasExplicitValue; }, &Query);
	return Query.m_Explicit;
}

template<typename TWasExplicitlySet>
bool QmInitializeDefaultProfile(CConfig &Config, bool HadQmConfig, TWasExplicitlySet &&WasExplicitlySet)
{
	if(Config.m_QmDefaultsProfileVersion >= QM_DEFAULTS_PROFILE_VERSION)
		return false;
	if(HadQmConfig)
	{
		if(Config.m_QmDefaultsProfileVersion < 1)
		{
			// 旧版省略了等于旧默认值的项目；升级时将这些值显式保留下来。
			if(!WasExplicitlySet("ui_color"))
				Config.m_UiColor = 0x4D000000;
			if(!WasExplicitlySet("br_filter_login"))
				Config.m_BrFilterLogin = 0;
			if(!WasExplicitlySet("qm_custom_font"))
				str_copy(Config.m_QmCustomFont, "Source Han Sans SC");
		}
	}
	Config.m_QmDefaultsProfileVersion = QM_DEFAULTS_PROFILE_VERSION;
	return true;
}

#endif
