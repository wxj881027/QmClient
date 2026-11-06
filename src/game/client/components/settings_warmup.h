// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H
#define GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

constexpr bool IsSettingsWarmupStageReady(int CurrentStage, int RequiredStage)
{
	return CurrentStage < 0 || CurrentStage >= RequiredStage;
}

constexpr int AdvanceSettingsWarmupStage(int CurrentStage, int LastStage)
{
	if(CurrentStage < 0 || LastStage < 0)
		return -1;
	return CurrentStage < LastStage ? CurrentStage + 1 : -1;
}

constexpr int SettingsLoadingRuntimeCacheWarmupSteps(int TClientCacheSlots)
{
	return TClientCacheSlots + 6;
}

inline int SettingsRuntimeCacheRoundedKey(float Value, int NonFiniteFallback = 0)
{
	if(!std::isfinite(Value))
		return NonFiniteFallback;
	if(Value >= static_cast<float>(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();
	if(Value <= static_cast<float>(std::numeric_limits<int>::min()))
		return std::numeric_limits<int>::min();
	return static_cast<int>(std::round(Value));
}

inline int SettingsRuntimeCachePositiveRoundedKey(float Value)
{
	return std::max(1, SettingsRuntimeCacheRoundedKey(Value, 1));
}

inline int SettingsRuntimeCacheDimensionKey(float Value)
{
	if(!std::isfinite(Value) || Value <= 1.0f)
		return 1;
	if(Value >= static_cast<float>(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();
	return std::max(1, static_cast<int>(Value));
}

enum class EClassicSettingsPage
{
	GENERAL,
	PLAYER,
	TEE,
	CONTROLS,
	GRAPHICS,
	SOUND,
	DDNET,
	ASSETS,
	TCLIENT,
	QMCLIENT,
};

struct SSettingsSectionCacheRuntimeKey
{
	int m_ViewportWidth = 0;
	int m_ViewportHeight = 0;
	int m_UiScale = 0;
	uint64_t m_ConfigHash = 0;
	uint64_t m_LanguageHash = 0;
	uint64_t m_FontHash = 0;
	uint64_t m_BackendHash = 0;
	uint64_t m_WindowHash = 0;

	bool operator==(const SSettingsSectionCacheRuntimeKey &Other) const
	{
		return m_ViewportWidth == Other.m_ViewportWidth &&
		       m_ViewportHeight == Other.m_ViewportHeight &&
		       m_UiScale == Other.m_UiScale &&
		       m_ConfigHash == Other.m_ConfigHash &&
		       m_LanguageHash == Other.m_LanguageHash &&
		       m_FontHash == Other.m_FontHash &&
		       m_BackendHash == Other.m_BackendHash &&
		       m_WindowHash == Other.m_WindowHash;
	}
};

struct SSettingsSectionCacheMetadata
{
	EClassicSettingsPage m_LastPage = EClassicSettingsPage::GENERAL;
	int m_LastTab = -1;
	float m_LastScrollY = 0.0f;
	uint64_t m_SectionNameHash = 0;
	float m_SectionHeight = 0.0f;
	SSettingsSectionCacheRuntimeKey m_RuntimeKey;

	bool Matches(const SSettingsSectionCacheRuntimeKey &RuntimeKey) const
	{
		return m_SectionNameHash != 0 &&
		       m_SectionHeight > 0.0f &&
		       m_RuntimeKey == RuntimeKey;
	}
};

struct SSettingsWarmupSection
{
	EClassicSettingsPage m_Page = EClassicSettingsPage::GENERAL;
	const char *m_pName = nullptr;
	int m_Priority = 0;
	std::function<double()> m_WarmupFn;
	bool m_Warmed = false;
};

class CSettingsWarmupScheduler
{
public:
	void RegisterSection(SSettingsWarmupSection Section);
	void SetLastSessionPage(EClassicSettingsPage Page);
	void SetEnabled(bool Enabled);
	bool WarmupFrame(double BudgetMs);
	void Reset();

private:
	std::vector<SSettingsWarmupSection> m_vSections;
	EClassicSettingsPage m_LastSessionPage = EClassicSettingsPage::GENERAL;
	bool m_Enabled = true;
	bool m_Sorted = false;
};

#endif // GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H
