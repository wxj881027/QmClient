#ifndef GAME_CLIENT_COMPONENTS_SETTINGS_RUNTIME_CACHE_H
#define GAME_CLIENT_COMPONENTS_SETTINGS_RUNTIME_CACHE_H

#include <game/client/components/settings_warmup.h>

#include <cstdint>
#include <string>
#include <vector>

enum class ESettingsWarmupCost
{
	TEXT_CONTAINER,
	GPU_UPLOAD,
	JOB_RESULT_MERGE,
};

enum class ESettingsWarmupStopReason
{
	NONE,
	TEXT_BUDGET,
	GPU_UPLOAD_BUDGET,
	MERGE_BUDGET,
	ACTIVE_ITEM,
};

enum class ESettingsWarmupMissReason
{
	NONE,
	DEPENDENCY_NOT_READY,
	RESOURCE_PLAN_PENDING,
	JOB_RESULT_PENDING,
	GPU_UPLOAD_BUDGET,
	SHARED_HEAVY_BUDGET,
	UPLOAD_BYTES_BUDGET,
	OVERSIZED_UPLOAD_DEFERRED,
	TEXT_BUDGET,
	ACTIVE_ITEM,
	INVALID_RUNTIME_KEY,
};

enum class ETClientSettingsPerfStage
{
	TAB_SHELL,
	SECTION_LAYOUT,
	TEXT_CACHE,
	RESOURCE_PRETRIGGER,
	STATIC_LAYER,
	INTERACTIVE_LAYER,
};

enum class ESettingsInvalidationReason
{
	LANGUAGE_CHANGED,
	FONT_CHANGED,
	BACKEND_CHANGED,
	WINDOW_OR_SCALE_CHANGED,
	DPI_CHANGED,
	UI_SCALE_CHANGED,
	CONFIG_HASH_CHANGED,
	SECTION_SIZE_CHANGED,
	RESOURCE_DIRECTORY_CHANGED,
};

enum class ESettingsCacheDirtyReason : uint8_t
{
	NONE,
	CONFIG,
	LANGUAGE,
	WINDOW_SIZE,
	UI_SCALE,
	FONT,
	ACTIVE_INTERACTION,
	GRAPHICS_RESET,
	UNKNOWN,
};

struct SSettingsWarmupFrameBudget
{
	int m_MaxTextContainers = 8;
	int m_MaxGpuUploads = 14;
	int m_MaxJobResultMerges = 1;
	ESettingsWarmupStopReason m_StopReason = ESettingsWarmupStopReason::NONE;
};

struct SSettingsRuntimeCacheKey
{
	uint64_t m_LanguageHash = 0;
	uint64_t m_FontGeneration = 0;
	uint64_t m_BackendGeneration = 0;
	int m_WindowWidth = 0;
	int m_WindowHeight = 0;
	int m_UiScale = 100;
	uint64_t m_ConfigHash = 0;
	uint64_t m_WindowHash = 0;
};

int SettingsCanonicalPage(int Page);
bool SettingsPageVisibleInRightTabBar(int Page);
bool SettingsRuntimeCacheKeyMatches(const SSettingsRuntimeCacheKey &A, const SSettingsRuntimeCacheKey &B);
ESettingsCacheDirtyReason SettingsRuntimeKeyMismatchDirtyReason(const SSettingsSectionCacheRuntimeKey &Current, const SSettingsSectionCacheRuntimeKey &Next);
bool SettingsWarmupConsumeBudget(SSettingsWarmupFrameBudget &Budget, ESettingsWarmupCost Cost);
const char *SettingsWarmupMissReasonName(ESettingsWarmupMissReason Reason);
const char *SettingsTClientPerfStageName(ETClientSettingsPerfStage Stage);
const char *SettingsInvalidationReasonName(ESettingsInvalidationReason Reason);
const char *SettingsCacheDirtyReasonName(ESettingsCacheDirtyReason Reason);
bool SettingsRuntimeCacheAllowsVisibleCompactText(const char *pRenderName);
void LogSettingsResourcePerf(int Page, const char *pJob, int Count, int Budget, int Remaining, ESettingsWarmupMissReason Reason, double DurationMs);
bool SettingsInvalidationClearsTextPool(ESettingsInvalidationReason Reason);
bool SettingsTextCacheNeedsBuild(const std::string &CachedText, const char *pText, int StrLen, bool ContainerValid, bool SizeChanged, bool CursorChanged);
bool SettingsInvalidationClearsResourcePlan(ESettingsInvalidationReason Reason);
std::string SettingsPageCacheKey(int Page, int Tab);
std::string SettingsSectionCacheKey(int Page, int Tab, const char *pSection);
std::string SettingsTextCacheKey(int Page, int Tab, const char *pTextId);
std::string SettingsResourceCacheKey(int Page, const char *pResourceId);

#endif // GAME_CLIENT_COMPONENTS_SETTINGS_RUNTIME_CACHE_H
