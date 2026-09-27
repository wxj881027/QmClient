#include "settings_runtime_cache.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>

#include <algorithm>
#include <cstdio>

static std::string BuildRuntimeCacheKey(const char *pPrefix, const char *pPageName, int Tab, const char *pSuffix, const char *pId)
{
	std::string Key = pPrefix;
	Key += pPageName;
	if(Tab >= 0)
	{
		char aTab[32];
		std::snprintf(aTab, sizeof(aTab), ":tab:%d", Tab);
		Key += aTab;
	}
	Key += pSuffix;
	Key += pId != nullptr ? pId : "";
	return Key;
}

int SettingsCanonicalPage(int Page)
{
	switch(Page)
	{
	case CMenus::SETTINGS_LANGUAGE: return CMenus::SETTINGS_GENERAL;
	case CMenus::SETTINGS_PLAYER: return CMenus::SETTINGS_TEE;
	case CMenus::SETTINGS_CONFIGS:
		return CMenus::SETTINGS_QMCLIENT;
	default:
		return Page;
	}
}

bool SettingsPageVisibleInRightTabBar(int Page)
{
	switch(Page)
	{
	case CMenus::SETTINGS_LANGUAGE:
	case CMenus::SETTINGS_PLAYER:
	case CMenus::SETTINGS_PROFILES:
	case CMenus::SETTINGS_CONFIGS:
		return false;
	default:
		return Page >= 0 && Page < CMenus::SETTINGS_LENGTH;
	}
}

bool SettingsRuntimeCacheKeyMatches(const SSettingsRuntimeCacheKey &A, const SSettingsRuntimeCacheKey &B)
{
	return A.m_LanguageHash == B.m_LanguageHash &&
	       A.m_FontGeneration == B.m_FontGeneration &&
	       A.m_BackendGeneration == B.m_BackendGeneration &&
	       A.m_WindowWidth == B.m_WindowWidth &&
	       A.m_WindowHeight == B.m_WindowHeight &&
	       A.m_UiScale == B.m_UiScale &&
	       A.m_ConfigHash == B.m_ConfigHash;
}

ESettingsCacheDirtyReason SettingsRuntimeKeyMismatchDirtyReason(const SSettingsSectionCacheRuntimeKey &Current, const SSettingsSectionCacheRuntimeKey &Next)
{
	if(Current == Next)
		return ESettingsCacheDirtyReason::NONE;
	if(Current.m_ViewportWidth != Next.m_ViewportWidth ||
		Current.m_ViewportHeight != Next.m_ViewportHeight ||
		Current.m_WindowHash != Next.m_WindowHash)
	{
		return ESettingsCacheDirtyReason::WINDOW_SIZE;
	}
	if(Current.m_UiScale != Next.m_UiScale)
		return ESettingsCacheDirtyReason::UI_SCALE;
	if(Current.m_LanguageHash != Next.m_LanguageHash)
		return ESettingsCacheDirtyReason::LANGUAGE;
	if(Current.m_FontHash != Next.m_FontHash)
		return ESettingsCacheDirtyReason::FONT;
	if(Current.m_BackendHash != Next.m_BackendHash)
		return ESettingsCacheDirtyReason::GRAPHICS_RESET;
	if(Current.m_ConfigHash != Next.m_ConfigHash)
		return ESettingsCacheDirtyReason::CONFIG;
	return ESettingsCacheDirtyReason::UNKNOWN;
}

bool SettingsWarmupConsumeBudget(SSettingsWarmupFrameBudget &Budget, ESettingsWarmupCost Cost)
{
	int *pBudgetCounter = nullptr;
	ESettingsWarmupStopReason StopReason = ESettingsWarmupStopReason::NONE;
	switch(Cost)
	{
	case ESettingsWarmupCost::TEXT_CONTAINER:
		pBudgetCounter = &Budget.m_MaxTextContainers;
		StopReason = ESettingsWarmupStopReason::TEXT_BUDGET;
		break;
	case ESettingsWarmupCost::GPU_UPLOAD:
		pBudgetCounter = &Budget.m_MaxGpuUploads;
		StopReason = ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET;
		break;
	case ESettingsWarmupCost::JOB_RESULT_MERGE:
		pBudgetCounter = &Budget.m_MaxJobResultMerges;
		StopReason = ESettingsWarmupStopReason::MERGE_BUDGET;
		break;
	}

	if(pBudgetCounter == nullptr)
		return false;
	if(*pBudgetCounter <= 0)
	{
		Budget.m_StopReason = StopReason;
		return false;
	}

	--(*pBudgetCounter);
	return true;
}

const char *SettingsWarmupMissReasonName(ESettingsWarmupMissReason Reason)
{
	switch(Reason)
	{
	case ESettingsWarmupMissReason::NONE: return "none";
	case ESettingsWarmupMissReason::DEPENDENCY_NOT_READY: return "dependency_not_ready";
	case ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING: return "resource_plan_pending";
	case ESettingsWarmupMissReason::JOB_RESULT_PENDING: return "job_result_pending";
	case ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET: return "gpu_upload_budget";
	case ESettingsWarmupMissReason::SHARED_HEAVY_BUDGET: return "shared_heavy_budget";
	case ESettingsWarmupMissReason::UPLOAD_BYTES_BUDGET: return "upload_bytes_budget";
	case ESettingsWarmupMissReason::OVERSIZED_UPLOAD_DEFERRED: return "oversized_upload_deferred";
	case ESettingsWarmupMissReason::TEXT_BUDGET: return "text_budget";
	case ESettingsWarmupMissReason::ACTIVE_ITEM: return "active_item";
	case ESettingsWarmupMissReason::INVALID_RUNTIME_KEY: return "invalid_runtime_key";
	}
	return "unknown";
}

const char *SettingsTClientPerfStageName(ETClientSettingsPerfStage Stage)
{
	switch(Stage)
	{
	case ETClientSettingsPerfStage::TAB_SHELL: return "tclient_tab_shell";
	case ETClientSettingsPerfStage::SECTION_LAYOUT: return "tclient_section_layout";
	case ETClientSettingsPerfStage::TEXT_CACHE: return "tclient_text_cache";
	case ETClientSettingsPerfStage::RESOURCE_PRETRIGGER: return "tclient_resource_pretrigger";
	case ETClientSettingsPerfStage::STATIC_LAYER: return "tclient_static_layer";
	case ETClientSettingsPerfStage::INTERACTIVE_LAYER: return "tclient_interactive_layer";
	}
	return "tclient_unknown";
}

const char *SettingsInvalidationReasonName(ESettingsInvalidationReason Reason)
{
	switch(Reason)
	{
	case ESettingsInvalidationReason::LANGUAGE_CHANGED: return "language_changed";
	case ESettingsInvalidationReason::FONT_CHANGED: return "font_changed";
	case ESettingsInvalidationReason::BACKEND_CHANGED: return "backend_changed";
	case ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED: return "window_or_scale_changed";
	case ESettingsInvalidationReason::DPI_CHANGED: return "dpi_changed";
	case ESettingsInvalidationReason::UI_SCALE_CHANGED: return "ui_scale_changed";
	case ESettingsInvalidationReason::CONFIG_HASH_CHANGED: return "config_hash_changed";
	case ESettingsInvalidationReason::SECTION_SIZE_CHANGED: return "section_size_changed";
	case ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED: return "resource_directory_changed";
	}
	return "unknown";
}

const char *SettingsCacheDirtyReasonName(ESettingsCacheDirtyReason Reason)
{
	switch(Reason)
	{
	case ESettingsCacheDirtyReason::NONE: return "none";
	case ESettingsCacheDirtyReason::CONFIG: return "config";
	case ESettingsCacheDirtyReason::LANGUAGE: return "language";
	case ESettingsCacheDirtyReason::WINDOW_SIZE: return "window_size";
	case ESettingsCacheDirtyReason::UI_SCALE: return "ui_scale";
	case ESettingsCacheDirtyReason::FONT: return "font";
	case ESettingsCacheDirtyReason::ACTIVE_INTERACTION: return "active_interaction";
	case ESettingsCacheDirtyReason::GRAPHICS_RESET: return "graphics_reset";
	case ESettingsCacheDirtyReason::UNKNOWN: return "unknown";
	}
	return "unknown";
}

bool SettingsRuntimeCacheAllowsVisibleCompactText(const char *pRenderName)
{
	(void)pRenderName;
	return false;
}

void LogSettingsResourcePerf(int Page, const char *pJob, int Count, int Budget, int Remaining, ESettingsWarmupMissReason Reason, double DurationMs)
{
	if(!QmPerfEnabled())
		return;
	const std::string PageName = SettingsPageCacheKey(Page, -1);
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "job=%s count=%d budget=%d remaining=%d reason=%s cost_ms=%.3f",
		pJob != nullptr ? pJob : "unknown", Count, Budget, Remaining, SettingsWarmupMissReasonName(Reason), DurationMs);
	QmPerfLogPayload("perf/settings-resource", aPayload, nullptr, PageName.c_str());
}

bool SettingsInvalidationClearsTextPool(ESettingsInvalidationReason Reason)
{
	switch(Reason)
	{
	// 只有真正改变 label 文字内容、字形或渲染后端的 reason 才需要全池失效。
	// 这些场景不频繁（语言/字体/后端切换），全池重建成本可接受，且避免
	// 单 entry 容器有效性检测的潜在漏洞（后端切换后旧 TextContainerIndex 失效）。
	case ESettingsInvalidationReason::LANGUAGE_CHANGED:
	case ESettingsInvalidationReason::FONT_CHANGED:
	case ESettingsInvalidationReason::BACKEND_CHANGED:
		return true;
	// 以下 reason 只影响布局尺寸或控件配置状态，不影响 label 的文字内容或字形。
	// DoMenuLabelStreamed 已在每个 entry 级别检测 SizeChanged / TextChanged /
	// ColorChanged 并按需重建（见 menus.cpp DoMenuLabelStreamed 的 NeedsBuild），
	// 因此不需要全池失效。全池失效会让 rect 未变的 entry 也强制重建，是 ingame ESC
	// 打开设置时“闪 + 卡 + 重加载文本池”的直接根因：
	//   OnReset -> CONFIG_HASH_CHANGED -> 旧逻辑 ClearsText=true -> 全池 m_Generation=0
	//   -> ESC 回菜单首帧全 text_new。
	case ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED:
	case ESettingsInvalidationReason::DPI_CHANGED:
	case ESettingsInvalidationReason::UI_SCALE_CHANGED:
	case ESettingsInvalidationReason::CONFIG_HASH_CHANGED:
	case ESettingsInvalidationReason::SECTION_SIZE_CHANGED:
	case ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED:
		return false;
	}
	return true;
}

bool SettingsTextCacheNeedsBuild(const std::string &CachedText, const char *pText, int StrLen, bool ContainerValid, bool SizeChanged, bool CursorChanged)
{
	const bool TextChanged = StrLen >= 0 ?
					 CachedText.size() != (size_t)StrLen || str_comp_num(CachedText.c_str(), pText, StrLen) != 0 :
					 str_comp(CachedText.c_str(), pText) != 0;
	return (!ContainerValid && pText[0] != '\0' && StrLen != 0) || TextChanged || SizeChanged || CursorChanged;
}

bool SettingsInvalidationClearsResourcePlan(ESettingsInvalidationReason Reason)
{
	return Reason == ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED;
}

static std::string SettingsRuntimePageName(int Page)
{
	switch(Page)
	{
	case CMenus::SETTINGS_LANGUAGE: return "language";
	case CMenus::SETTINGS_GENERAL: return "general";
	case CMenus::SETTINGS_PLAYER: return "player";
	case CMenus::SETTINGS_TEE: return "tee";
	case CMenus::SETTINGS_APPEARANCE: return "appearance";
	case CMenus::SETTINGS_CONTROLS: return "controls";
	case CMenus::SETTINGS_GRAPHICS: return "graphics";
	case CMenus::SETTINGS_SOUND: return "sound";
	case CMenus::SETTINGS_DDNET: return "ddnet";
	case CMenus::SETTINGS_ASSETS: return "assets";
	case CMenus::SETTINGS_TCLIENT: return "tclient";
	case CMenus::SETTINGS_QMCLIENT: return "qmclient";
	case CMenus::SETTINGS_SEARCH: return "search";
	case CMenus::SETTINGS_CONTRIBUTORS: return "contributors";
	default: return "unknown";
	}
}

std::string SettingsPageCacheKey(int Page, int Tab)
{
	const std::string PageName = SettingsRuntimePageName(Page);
	if(Tab >= 0)
		return BuildRuntimeCacheKey("settings:", PageName.c_str(), Tab, "", "");
	return std::string("settings:") + PageName;
}

std::string SettingsSectionCacheKey(int Page, int Tab, const char *pSection)
{
	return BuildRuntimeCacheKey("settings:", SettingsRuntimePageName(Page).c_str(), Tab, ":section:", pSection);
}

std::string SettingsTextCacheKey(int Page, int Tab, const char *pTextId)
{
	return BuildRuntimeCacheKey("settings:", SettingsRuntimePageName(Page).c_str(), Tab, ":text:", pTextId);
}

std::string SettingsResourceCacheKey(int Page, const char *pResourceId)
{
	return BuildRuntimeCacheKey("settings:", SettingsRuntimePageName(Page).c_str(), -1, ":resource:", pResourceId);
}
