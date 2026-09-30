#include "section_loader.h"

#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/settings_runtime_cache.h>

#include <cstdlib>

static uint64_t ParseSessionCacheU64(const char *pValue)
{
	if(pValue == nullptr || pValue[0] == '\0')
		return 0;
#if defined(CONF_FAMILY_WINDOWS)
	return _strtoui64(pValue, nullptr, 10);
#else
	return strtoull(pValue, nullptr, 10);
#endif
}

CSectionLoader::CSectionLoader() = default;

CSectionLoader::~CSectionLoader() = default;

bool CSectionLoader::IsVisibleSummarySectionName(const char *pName)
{
	return pName != nullptr &&
	       str_find(pName, "DeferredSummary") == nullptr &&
	       str_find(pName, "CompactSummary") == nullptr &&
	       str_find(pName, "SummaryBlock") == nullptr;
}

static void ClearSectionCallbacks(std::vector<SSettingsSection> &vSections)
{
	for(auto &Section : vSections)
	{
		Section.m_MeasureFn = nullptr;
		Section.m_RenderCompactFn = nullptr;
		Section.m_RenderFullFn = nullptr;
	}
}

void CSectionLoader::Register(std::vector<SSettingsSection> vSections)
{
	std::vector<bool> vTransferred(m_vSections.size(), false);
	for(auto &NewSection : vSections)
	{
		for(size_t OldIndex = 0; OldIndex < m_vSections.size(); ++OldIndex)
		{
			const auto &OldSection = m_vSections[OldIndex];
			if(str_comp(NewSection.m_pName, OldSection.m_pName) != 0)
				continue;

			NewSection.m_State = OldSection.m_State;
			NewSection.m_CachedHeight = OldSection.m_CachedHeight;
			NewSection.m_HasCachedHeight = OldSection.m_HasCachedHeight;
			NewSection.m_LastConfigHash = OldSection.m_LastConfigHash;
			NewSection.m_Dirty = OldSection.m_Dirty;
			vTransferred[OldIndex] = true;
			if(ComputeConfigHash(NewSection) != NewSection.m_LastConfigHash)
				NewSection.m_Dirty = true;
			break;
		}
	}
	m_vSections = std::move(vSections);
}

void CSectionLoader::SetRuntimeKey(const SSettingsSectionCacheRuntimeKey &RuntimeKey)
{
	if(m_RuntimeKey == RuntimeKey)
		return;
	const ESettingsCacheDirtyReason DirtyReason = SettingsRuntimeKeyMismatchDirtyReason(m_RuntimeKey, RuntimeKey);
	m_RuntimeKey = RuntimeKey;
	InvalidateCache(DirtyReason);
}

void CSectionLoader::SetProgressiveEnabled(bool Enabled)
{
	m_ProgressiveEnabled = Enabled;
}

void CSectionLoader::SetDeferredFarMeasurementEnabled(bool Enabled)
{
	m_DeferredFarMeasurementEnabled = Enabled;
}

void CSectionLoader::SetMaxSectionsPerFrame(int MaxSectionsPerFrame)
{
	m_MaxSectionsPerFrame = maximum(1, MaxSectionsPerFrame);
}

void CSectionLoader::Begin(CUIRect MainView, float TimeBudgetMs)
{
	Begin(MainView, MainView, TimeBudgetMs);
}

void CSectionLoader::Begin(CUIRect MainView, CUIRect Viewport, float TimeBudgetMs)
{
	m_MainView = MainView;
	m_Viewport = Viewport;
	m_BudgetPerFrameMs = (double)TimeBudgetMs;
	m_CurrentIndex = 0;

	m_Complete = false;
	m_TotalFrameTimeMs = 0.0;
}

bool CSectionLoader::Process(bool RenderSections)
{
	m_LastFrameStats = {};
	m_LastFrameStats.m_SectionsTotal = (int)m_vSections.size();
	m_LastFrameStats.m_DirtyReason = ESettingsCacheDirtyReason::NONE;

	if(!m_Initialized)
	{
		for(auto &Section : m_vSections)
		{
			Section.m_State = ESettingsSectionState::UNINITIALIZED;
			Section.m_CachedHeight = 0.0f;
			Section.m_HasCachedHeight = false;
			Section.m_Dirty = true;
		}
		m_Initialized = true;
		m_CurrentIndex = 0;
	}

	m_RunningColumn = m_MainView;

	CPerfTimer FrameTimer;
	int UnlockedThisFrame = 0;
	const int MaxUnlockPerFrame = m_MaxSectionsPerFrame;
	const auto RecordSectionVisibility = [this](float SectionStartY, const SSettingsSection &Section, SSectionLoaderFrameStats &Stats) {
		const float ActualHeight = maximum(Section.m_CachedHeight, m_RunningColumn.y - SectionStartY);
		const CUIRect ActualSectionRect{m_MainView.x, SectionStartY, m_MainView.w, ActualHeight};
		if(ComputeViewportPriority(ActualSectionRect) <= 1)
			++Stats.m_SectionsVisible;
		else
			++Stats.m_SectionsSkipped;
	};

	while(m_CurrentIndex < (int)m_vSections.size())
	{
		SSettingsSection &Section = m_vSections[m_CurrentIndex];
		const float SectionStartY = m_RunningColumn.y;
		const bool SectionDirty = Section.m_Dirty;
		const CUIRect EstimatedSectionRect{m_MainView.x, SectionStartY, m_MainView.w, Section.m_CachedHeight};
		const int Priority = ComputeViewportPriority(EstimatedSectionRect);
		if(SectionDirty)
		{
			++m_LastFrameStats.m_LayoutDirtySections;
			m_LastFrameStats.m_DirtyReason = m_LastDirtyReason;
		}

		if(!m_ProgressiveEnabled && Section.m_State != ESettingsSectionState::FULL)
		{
			const bool CanDeferFarMeasurement =
				m_DeferredFarMeasurementEnabled &&
				Section.m_HasCachedHeight &&
				!Section.m_Dirty &&
				Priority > 1;
			if(CanDeferFarMeasurement)
			{
				Section.m_State = ESettingsSectionState::FULL;
				m_RunningColumn.y += Section.m_CachedHeight;
				RecordSectionVisibility(SectionStartY, Section, m_LastFrameStats);
				++m_CurrentIndex;
				continue;
			}

			CUIRect MeasureColumn = m_RunningColumn;
			if(Section.m_MeasureFn)
			{
				Section.m_CachedHeight = Section.m_MeasureFn(MeasureColumn);
				Section.m_HasCachedHeight = true;
			}
			else
			{
				Section.m_CachedHeight = 0.0f;
				Section.m_HasCachedHeight = true;
			}
			Section.m_State = ESettingsSectionState::FULL;
			Section.m_LastConfigHash = ComputeConfigHash(Section);
			Section.m_Dirty = false;
		}
		const bool BudgetAvailable = FrameTimer.ElapsedMs() < m_BudgetPerFrameMs;

		switch(Section.m_State)
		{
		case ESettingsSectionState::UNINITIALIZED:
		{
			if(Section.m_MeasureFn)
			{
				Section.m_CachedHeight = Section.m_MeasureFn(m_RunningColumn);
				Section.m_HasCachedHeight = true;
			}
			else
			{
				Section.m_CachedHeight = 0.0f;
				Section.m_HasCachedHeight = true;
			}
			Section.m_State = ESettingsSectionState::MEASURING;
			Section.m_LastConfigHash = ComputeConfigHash(Section);
			Section.m_Dirty = false;
			++m_CurrentIndex;
			break;
		}
		case ESettingsSectionState::MEASURING:
		{
			if(Priority <= 1)
			{
				Section.m_State = ESettingsSectionState::COMPACT;
				if(RenderSections && Section.m_RenderCompactFn)
				{
					Section.m_CachedHeight = Section.m_RenderCompactFn(m_RunningColumn);
					Section.m_HasCachedHeight = true;
				}
				else
					m_RunningColumn.y += Section.m_CachedHeight;
			}
			else
			{
				m_RunningColumn.y += Section.m_CachedHeight;
			}
			++m_CurrentIndex;
			break;
		}
		case ESettingsSectionState::COMPACT:
		{
			if(Priority > 1)
			{
				m_RunningColumn.y += Section.m_CachedHeight;
				++m_CurrentIndex;
				break;
			}
			if(BudgetAvailable && UnlockedThisFrame < MaxUnlockPerFrame && Priority <= 1)
			{
				Section.m_State = ESettingsSectionState::FULL;
				if(RenderSections && Section.m_RenderFullFn)
				{
					Section.m_CachedHeight = Section.m_RenderFullFn(m_RunningColumn);
					Section.m_HasCachedHeight = true;
				}
				else
				{
					m_RunningColumn.y += Section.m_CachedHeight;
				}
				Section.m_LastConfigHash = ComputeConfigHash(Section);
				Section.m_Dirty = false;
				++UnlockedThisFrame;
				++m_CurrentIndex;
				break;
			}
			if(RenderSections && Section.m_RenderCompactFn)
			{
				Section.m_CachedHeight = Section.m_RenderCompactFn(m_RunningColumn);
				Section.m_HasCachedHeight = true;
			}
			else
				m_RunningColumn.y += Section.m_CachedHeight;
			++m_CurrentIndex;
			break;
		}
		case ESettingsSectionState::FULL:
		{
			if(Section.m_Dirty && Section.m_MeasureFn)
			{
				CUIRect MeasureColumn = m_RunningColumn;
				Section.m_CachedHeight = Section.m_MeasureFn(MeasureColumn);
				Section.m_HasCachedHeight = true;
				Section.m_LastConfigHash = ComputeConfigHash(Section);
			}
			if(Priority > 1)
			{
				m_RunningColumn.y += Section.m_CachedHeight;
				Section.m_Dirty = false;
				++m_CurrentIndex;
				break;
			}
			if(RenderSections && Section.m_RenderFullFn)
			{
				Section.m_CachedHeight = Section.m_RenderFullFn(m_RunningColumn);
				Section.m_HasCachedHeight = true;
			}
			else
			{
				m_RunningColumn.y += Section.m_CachedHeight;
			}
			if(Section.m_Dirty)
				Section.m_LastConfigHash = ComputeConfigHash(Section);
			Section.m_Dirty = false;
			++m_CurrentIndex;
			break;
		}
		}

		RecordSectionVisibility(SectionStartY, Section, m_LastFrameStats);
	}

	if(m_CurrentIndex >= (int)m_vSections.size())
	{
		m_Complete = true;
		for(const auto &Sect : m_vSections)
		{
			if(Sect.m_State != ESettingsSectionState::FULL)
			{
				m_Complete = false;
				m_CurrentIndex = 0;
				break;
			}
		}
	}

	m_TotalFrameTimeMs += FrameTimer.ElapsedMs();
	ClearSectionCallbacks(m_vSections);

	// Profiling: log when budget is exceeded and perf-debug is enabled
	if(g_Config.m_QmPerfDebug && m_TotalFrameTimeMs > 1.0)
	{
		char aPayload[320];
		str_format(aPayload, sizeof(aPayload),
			"event=section_loader sections_total=%d sections_visible=%d sections_skipped=%d layout_dirty=%d dirty_reason=%s budget_ms=%.1f actual_ms=%.1f complete=%d",
			m_LastFrameStats.m_SectionsTotal,
			m_LastFrameStats.m_SectionsVisible,
			m_LastFrameStats.m_SectionsSkipped,
			m_LastFrameStats.m_LayoutDirtySections,
			SettingsCacheDirtyReasonName(m_LastFrameStats.m_DirtyReason),
			m_BudgetPerFrameMs, m_TotalFrameTimeMs,
			m_Complete ? 1 : 0);
		QmPerfLogPayload("perf/section_loader", aPayload);
	}

	return !m_Complete;
}

bool CSectionLoader::IsComplete() const
{
	return m_Complete;
}

void CSectionLoader::Reset()
{
	m_Initialized = false;
	m_Complete = false;
	m_CurrentIndex = 0;
	m_TotalFrameTimeMs = 0.0;
	InvalidateCache(ESettingsCacheDirtyReason::CONFIG);
}

// -- Pre-warming --

bool CSectionLoader::Warmup(const SSessionUiCache *pCache, float TimeBudgetMs)
{
	if(!pCache || !pCache->m_Valid)
	{
		m_WarmupActive = false;
		ClearSectionCallbacks(m_vSections);
		return true;
	}

	if(!m_WarmupActive)
	{
		m_WarmupActive = true;
		m_WarmupIndex = 0;
		m_WarmupBudgetMs = TimeBudgetMs;
		m_pWarmupCache = pCache;
		for(auto &Section : m_vSections)
		{
			Section.m_State = ESettingsSectionState::UNINITIALIZED;
			Section.m_CachedHeight = 0.0f;
			Section.m_HasCachedHeight = false;
		}
	}

	CPerfTimer WarmupTimer;
	while(m_WarmupIndex < (int)m_vSections.size())
	{
		if(WarmupTimer.ElapsedMs() >= (double)m_WarmupBudgetMs)
			break;

		SSettingsSection &Section = m_vSections[m_WarmupIndex];

		const CUIRect SectionRect{m_MainView.x, m_MainView.y, m_MainView.w, Section.m_CachedHeight};
		const int Priority = ComputeViewportPriority(SectionRect);

		if(Priority > 1)
		{
			// Far from viewport: measure only
			if(Section.m_State == ESettingsSectionState::UNINITIALIZED)
			{
				if(Section.m_MeasureFn)
				{
					CUIRect MeasureRect = m_MainView;
					Section.m_CachedHeight = Section.m_MeasureFn(MeasureRect);
					Section.m_HasCachedHeight = true;
				}
				Section.m_State = ESettingsSectionState::MEASURING;
			}
			++m_WarmupIndex;
			continue;
		}

		// In or near viewport: render the registered real warmup path to populate glyphs/cache.
		const CPerfTimer SectTimer;
		if(Section.m_RenderCompactFn)
		{
			Section.m_CachedHeight = Section.m_RenderCompactFn(m_MainView);
			Section.m_HasCachedHeight = true;
		}
		Section.m_State = ESettingsSectionState::COMPACT;

		const double Elapsed = SectTimer.ElapsedMs();
		++m_WarmupIndex;

		if(Elapsed > 1.0)
			break; // expensive section; leave rest for next frame
	}

	if(m_WarmupIndex >= (int)m_vSections.size())
	{
		m_WarmupActive = false;
		ClearSectionCallbacks(m_vSections);
		return true;
	}
	ClearSectionCallbacks(m_vSections);
	return false;
}

bool CSectionLoader::IsWarmupComplete() const
{
	return !m_WarmupActive;
}

// -- Cache invalidation --

void CSectionLoader::InvalidateCache(ESettingsCacheDirtyReason Reason)
{
	m_LastDirtyReason = Reason;
	for(auto &Section : m_vSections)
		Section.m_Dirty = true;
}

void CSectionLoader::SetDirtyByConfig(const void *pConfigVar)
{
	for(auto &Section : m_vSections)
	{
		for(const int *pInt : Section.m_DependencyConfigInts)
		{
			if(static_cast<const void *>(pInt) == pConfigVar)
			{
				m_LastDirtyReason = ESettingsCacheDirtyReason::CONFIG;
				Section.m_Dirty = true;
				return;
			}
		}
		for(const unsigned *pCol : Section.m_DependencyConfigCols)
		{
			if(static_cast<const void *>(pCol) == pConfigVar)
			{
				m_LastDirtyReason = ESettingsCacheDirtyReason::CONFIG;
				Section.m_Dirty = true;
				return;
			}
		}
	}
}

// -- Session cache I/O --

bool CSectionLoader::LoadSessionCache(SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage)
{
	const IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aBuf[256];
	const unsigned Read = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);
	if(Read == 0)
		return false;

	aBuf[Read] = '\0';

	// Parse simple key=value lines
	const char *p = aBuf;
	while(*p)
	{
		// Skip whitespace
		while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			++p;
		if(*p == '\0')
			break;

		// Read key
		const char *pKeyEnd = p;
		while(*pKeyEnd && *pKeyEnd != '=' && *pKeyEnd != '\r' && *pKeyEnd != '\n')
			++pKeyEnd;
		const int KeyLen = (int)(pKeyEnd - p);
		if(KeyLen <= 0 || *pKeyEnd != '=')
		{
			// Advance past this line
			while(*p && *p != '\n')
				++p;
			if(*p == '\n')
				++p;
			continue;
		}

		const char *pVal = pKeyEnd + 1;
		const char *pValEnd = pVal;
		while(*pValEnd && *pValEnd != '\r' && *pValEnd != '\n')
			++pValEnd;
		if(KeyLen == 13 && strncmp(p, "settings_page", 13) == 0)
			Cache.m_LastSettingsPage = str_toint(pVal);
		else if(KeyLen == 11 && strncmp(p, "tab_tclient", 11) == 0)
			Cache.m_LastTClientTab = str_toint(pVal);
		else if(KeyLen == 6 && strncmp(p, "tab_qm", 6) == 0)
			Cache.m_LastQmTab = str_toint(pVal);
		else if(KeyLen == 8 && strncmp(p, "scroll_y", 8) == 0)
			Cache.m_LastScrollY = str_tofloat(pVal);
		else if(KeyLen == 14 && strncmp(p, "viewport_width", 14) == 0)
			Cache.m_RuntimeKey.m_ViewportWidth = str_toint(pVal);
		else if(KeyLen == 15 && strncmp(p, "viewport_height", 15) == 0)
			Cache.m_RuntimeKey.m_ViewportHeight = str_toint(pVal);
		else if(KeyLen == 8 && strncmp(p, "ui_scale", 8) == 0)
			Cache.m_RuntimeKey.m_UiScale = str_toint(pVal);
		else if(KeyLen == 11 && strncmp(p, "config_hash", 11) == 0)
			Cache.m_RuntimeKey.m_ConfigHash = ParseSessionCacheU64(pVal);
		else if(KeyLen == 13 && strncmp(p, "language_hash", 13) == 0)
			Cache.m_RuntimeKey.m_LanguageHash = ParseSessionCacheU64(pVal);
		else if(KeyLen == 9 && strncmp(p, "font_hash", 9) == 0)
			Cache.m_RuntimeKey.m_FontHash = ParseSessionCacheU64(pVal);
		else if(KeyLen == 12 && strncmp(p, "backend_hash", 12) == 0)
			Cache.m_RuntimeKey.m_BackendHash = ParseSessionCacheU64(pVal);
		else if(KeyLen == 11 && strncmp(p, "window_hash", 11) == 0)
			Cache.m_RuntimeKey.m_WindowHash = ParseSessionCacheU64(pVal);

		p = pValEnd;
		if(*p == '\n')
			++p;
	}

	Cache.m_Valid = (Cache.m_LastSettingsPage >= 0 || Cache.m_LastTClientTab >= 0 || Cache.m_LastQmTab >= 0);
	return Cache.m_Valid;
}

void CSectionLoader::SaveSessionCache(const SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage)
{
	if(!Cache.m_Valid)
		return;

	pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	const IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	char aLine[128];
	int Len;

	Len = str_format(aLine, sizeof(aLine), "settings_page=%d\n", Cache.m_LastSettingsPage);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "tab_tclient=%d\n", Cache.m_LastTClientTab);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "tab_qm=%d\n", Cache.m_LastQmTab);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "scroll_y=%f\n", Cache.m_LastScrollY);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "viewport_width=%d\n", Cache.m_RuntimeKey.m_ViewportWidth);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "viewport_height=%d\n", Cache.m_RuntimeKey.m_ViewportHeight);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "ui_scale=%d\n", Cache.m_RuntimeKey.m_UiScale);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "config_hash=%llu\n", (unsigned long long)Cache.m_RuntimeKey.m_ConfigHash);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "language_hash=%llu\n", (unsigned long long)Cache.m_RuntimeKey.m_LanguageHash);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "font_hash=%llu\n", (unsigned long long)Cache.m_RuntimeKey.m_FontHash);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "backend_hash=%llu\n", (unsigned long long)Cache.m_RuntimeKey.m_BackendHash);
	io_write(File, aLine, (unsigned)Len);

	Len = str_format(aLine, sizeof(aLine), "window_hash=%llu\n", (unsigned long long)Cache.m_RuntimeKey.m_WindowHash);
	io_write(File, aLine, (unsigned)Len);

	io_close(File);
}

// -- Profiling --

const char *CSectionLoader::GetPerfReport() const
{
	return ""; // Reporting is inline via dbg_msg in Process()
}

int CSectionLoader::ComputeViewportPriority(const CUIRect &SectionRect) const
{
	const float ViewportTop = m_Viewport.y;
	const float ViewportBottom = m_Viewport.y + m_Viewport.h;
	const float PrefetchMargin = 200.0f;

	if(SectionRect.y + SectionRect.h >= ViewportTop - PrefetchMargin &&
		SectionRect.y <= ViewportBottom + PrefetchMargin)
	{
		if(SectionRect.y + SectionRect.h >= ViewportTop &&
			SectionRect.y <= ViewportBottom)
			return 0; // In viewport
		return 1; // Near viewport
	}
	return 2; // Far from viewport
}

uint64_t CSectionLoader::ComputeConfigHash(const SSettingsSection &Section)
{
	// FNV-1a 64-bit
	uint64_t Hash = 14695981039346656037ull;
	for(const int *pVal : Section.m_DependencyConfigInts)
	{
		const uint8_t *pBytes = reinterpret_cast<const uint8_t *>(pVal);
		for(size_t i = 0; i < sizeof(int); ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}
	}
	for(const unsigned *pVal : Section.m_DependencyConfigCols)
	{
		const uint8_t *pBytes = reinterpret_cast<const uint8_t *>(pVal);
		for(size_t i = 0; i < sizeof(unsigned); ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}
	}
	return Hash;
}
