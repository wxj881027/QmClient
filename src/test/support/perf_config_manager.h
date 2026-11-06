#ifndef TEST_SUPPORT_PERF_CONFIG_MANAGER_H
#define TEST_SUPPORT_PERF_CONFIG_MANAGER_H

#include <engine/config.h>

#include <new>
#include <vector>

// 仅替换配置枚举的外部边界，数值与默认值仍由生产配置变量提供。
class CPerfConfigManager : public IConfigManager
{
public:
	std::vector<const SConfigVariable *> m_vVariables;
	int m_CollectionCalls = 0;
	int m_FailAfter = -1;

	void Init() override {}
	void Reset(const char *) override {}
	void ResetGameSettings() override {}
	void SetReadOnly(const char *, bool) override {}
	void SetGameSettingsReadOnly(bool) override {}
	void SetSaveValueOverride(const char *, bool, int, const char *) override {}
	const char *SaveValueOverrideOwner(const int *) const override { return nullptr; }
	int RealValue(const int *pValue) const override { return *pValue; }
	bool Save(bool) override { return true; }
	CConfig *Values() override { return nullptr; }
	void RegisterCallback(SAVECALLBACKFUNC, void *, ConfigDomain) override {}
	void WriteLine(const char *, ConfigDomain) override {}
	void MarkSaveFailed(ConfigDomain) override {}
	void StoreUnknownCommand(const char *, ConfigDomain) override {}
	EColorInputAlphaMode ColorValueInputAlphaMode(const char *) const override { return EColorInputAlphaMode::PACKED; }

	void PossibleConfigVariables(const char *, int, POSSIBLECFGFUNC pfnCallback, void *pUserData) override
	{
		++m_CollectionCalls;
		if(m_FailAfter == 0)
			throw std::bad_alloc();
		int Count = 0;
		for(const auto *pVariable : m_vVariables)
		{
			pfnCallback(pVariable, pUserData);
			if(++Count == m_FailAfter)
				throw std::bad_alloc();
		}
	}
};

#endif
