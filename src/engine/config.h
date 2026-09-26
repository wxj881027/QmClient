/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include "kernel.h"

#define CONFIG_DOMAIN(Name, ConfigPath, PreviousConfigPath, LegacyConfigPath, HasVars) Name,
enum ConfigDomain // NOLINT(readability-enum-initial-value)
{
#include "shared/config_domains.h"
	NUM,
	START = 0
};
#undef CONFIG_DOMAIN

static inline ConfigDomain &operator++(ConfigDomain &Domain)
{
	return Domain = static_cast<ConfigDomain>(static_cast<int>(Domain) + 1);
}

class CConfigDomain
{
public:
	const char *m_aConfigPath;
	const char *m_aPreviousConfigPath;
	const char *m_aLegacyConfigPath;
	bool m_HasVars;
};
#define CONFIG_DOMAIN(Name, ConfigPath, PreviousConfigPath, LegacyConfigPath, HasVars) {ConfigPath, PreviousConfigPath, LegacyConfigPath, HasVars},
static const CConfigDomain s_aConfigDomains[ConfigDomain::NUM] = {
#include "shared/config_domains.h"
};
#undef CONFIG_DOMAIN

enum class EColorInputAlphaMode
{
	PACKED,
	SIGNED_PACKED,
	OMITTED,
	EXPLICIT,
};

class IConfigManager : public IInterface
{
	MACRO_INTERFACE("config")
public:
	typedef void (*SAVECALLBACKFUNC)(IConfigManager *pConfig, void *pUserData);
	typedef void (*POSSIBLECFGFUNC)(const struct SConfigVariable *, void *pUserData);

	virtual void Init() = 0;
	virtual void Reset(const char *pScriptName) = 0;
	virtual void ResetGameSettings() = 0;
	virtual void SetReadOnly(const char *pScriptName, bool ReadOnly) = 0;
	virtual void SetGameSettingsReadOnly(bool ReadOnly) = 0;
	// 临时写盘覆盖：程序在运行时临时改写某个整数配置项（例如禅模式临时隐藏 HUD/名字板）时，
	// 登记用户真实值，使 Save() 写出该值而不是运行时值，配置文件不会记录临时状态。
	// pOwnerId 标识接管来源（如 "qm_zen_mode"），供设置页显示"由谁接管"。
	virtual void SetSaveValueOverride(const char *pScriptName, bool Active, int Value = 0, const char *pOwnerId = nullptr) = 0;
	// 查询某个整数配置项是否正被临时接管；返回接管来源标识，未被接管返回 nullptr。
	virtual const char *SaveValueOverrideOwner(const int *pValue) const = 0;
	// 返回某整数配置项的"用户真实值"：被临时接管时为接管前保存的值，未接管时等于运行时值。
	// 供录制等"不应受临时接管影响"的路径读取。
	virtual int RealValue(const int *pValue) const = 0;
	virtual bool Save(bool Force = false) = 0;
	virtual class CConfig *Values() = 0;

	virtual void RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) = 0;

	virtual void WriteLine(const char *pLine, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) = 0;

	virtual void StoreUnknownCommand(const char *pCommand, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) = 0;

	virtual void PossibleConfigVariables(const char *pStr, int FlagMask, POSSIBLECFGFUNC pfnCallback, void *pUserData) = 0;
	virtual EColorInputAlphaMode ColorValueInputAlphaMode(const char *pScriptName) const = 0;
};

extern IConfigManager *CreateConfigManager();

#endif
