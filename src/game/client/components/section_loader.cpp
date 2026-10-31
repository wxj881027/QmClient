#include "section_loader.h"

#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/settings_runtime_cache.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

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

namespace
{
	template<typename T>
	bool ParseSessionCacheNumber(const char *pBegin, const char *pEnd, T &Value)
	{
		if(pBegin != pEnd && *pBegin == '+')
		{
			++pBegin;
			if(pBegin == pEnd || *pBegin < '0' || *pBegin > '9')
				return false;
		}
		const auto Result = std::from_chars(pBegin, pEnd, Value);
		return Result.ec == std::errc() && Result.ptr == pEnd;
	}

	bool ParseSessionCacheNumber(const char *pBegin, const char *pEnd, float &Value)
	{
		if(pBegin == pEnd)
			return false;
		char *pParsedEnd;
		errno = 0;
		Value = std::strtof(pBegin, &pParsedEnd);
		return pParsedEnd == pEnd && errno != ERANGE && std::isfinite(Value);
	}

	struct SSessionCacheFileCloser
	{
		CSectionLoader::CSessionCacheFileOperations &m_Operations;
		void operator()(void *pFile) const noexcept { m_Operations.Close(pFile); }
	};

	struct SSessionCacheCandidate
	{
		IStorage &m_Storage;
		const std::string &m_Path;
		bool m_Owned = false;
		~SSessionCacheCandidate()
		{
			if(m_Owned)
			{
				try
				{
					m_Storage.RemoveFile(m_Path.c_str(), IStorage::TYPE_SAVE);
				}
				catch(...)
				{
				}
			}
		}
	};
}

IOHANDLE CSectionLoader::CSessionCacheFileOperations::Open(IStorage &Storage, const char *pFilename, int Flags)
{
	return Storage.OpenFile(pFilename, Flags, IStorage::TYPE_SAVE);
}

bool CSectionLoader::CSessionCacheFileOperations::Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
{
	return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
}

bool CSectionLoader::LoadSessionCache(SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage, CSessionCacheFileOperations *pOperations) noexcept
{
	try
	{
		if(!pStorage || !pFilename || !pFilename[0])
			return false;
		CSessionCacheFileOperations Default;
		auto &Operations = pOperations ? *pOperations : Default;
		std::unique_ptr<void, SSessionCacheFileCloser> File(Operations.Open(*pStorage, pFilename, IOFLAG_READ), {Operations});
		if(!File)
			return false;
		// 当前12个字段最长368字节；超出缓冲或含NUL的数据不能作为完整快照发布。
		char aBuf[512];
		const unsigned Read = Operations.Read(File.get(), aBuf, sizeof(aBuf) - 1);
		char Extra;
		const bool TooLong = Read == sizeof(aBuf) - 1 && Operations.Read(File.get(), &Extra, 1) != 0;
		const bool Clean = Operations.Error(File.get()) == 0;
		const bool Closed = Operations.Close(File.release()) == 0;
		if(Read == 0 || TooLong || !Clean || !Closed || std::memchr(aBuf, '\0', Read) != nullptr)
			return false;
		aBuf[Read] = '\0';

		SSessionUiCache Candidate;
		char *p = aBuf;
		while(*p)
		{
			while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
				++p;
			if(!*p)
				break;
			char *pLineEnd = p;
			while(*pLineEnd && *pLineEnd != '\r' && *pLineEnd != '\n')
				++pLineEnd;
			const bool LastLine = *pLineEnd == '\0';
			*pLineEnd = '\0';
			char *pEquals = std::strchr(p, '=');
			if(!pEquals)
				return false;
			const std::string_view Key(p, pEquals - p);
			char *pValue = pEquals + 1;
			while(*pValue == ' ' || *pValue == '\t')
				++pValue;
			char *pEnd = pLineEnd;
			while(pEnd > pValue && (pEnd[-1] == ' ' || pEnd[-1] == '\t'))
				--pEnd;
			*pEnd = '\0';
			bool Valid = true;
			if(Key == "settings_page")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_LastSettingsPage);
			else if(Key == "tab_tclient")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_LastTClientTab);
			else if(Key == "tab_qm")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_LastQmTab);
			else if(Key == "scroll_y")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_LastScrollY);
			else if(Key == "viewport_width")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_ViewportWidth);
			else if(Key == "viewport_height")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_ViewportHeight);
			else if(Key == "ui_scale")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_UiScale);
			else if(Key == "config_hash")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_ConfigHash);
			else if(Key == "language_hash")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_LanguageHash);
			else if(Key == "font_hash")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_FontHash);
			else if(Key == "backend_hash")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_BackendHash);
			else if(Key == "window_hash")
				Valid = ParseSessionCacheNumber(pValue, pEnd, Candidate.m_RuntimeKey.m_WindowHash);
			if(!Valid)
				return false;
			if(LastLine)
				break;
			p = pLineEnd + 1;
		}
		Candidate.m_Valid = Candidate.m_LastSettingsPage >= 0 || Candidate.m_LastTClientTab >= 0 || Candidate.m_LastQmTab >= 0;
		if(!Candidate.m_Valid)
			return false;
		Cache = Candidate;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool CSectionLoader::SaveSessionCache(const SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage, CSessionCacheFileOperations *pOperations) noexcept
{
	try
	{
		if(!pStorage || !pFilename || !pFilename[0] || !Cache.m_Valid || !std::isfinite(Cache.m_LastScrollY) ||
			(Cache.m_LastSettingsPage < 0 && Cache.m_LastTClientTab < 0 && Cache.m_LastQmTab < 0))
			return false;
		char aData[512];
		const int Length = str_format(aData, sizeof(aData),
			"settings_page=%d\ntab_tclient=%d\ntab_qm=%d\nscroll_y=%f\nviewport_width=%d\nviewport_height=%d\nui_scale=%d\nconfig_hash=%llu\nlanguage_hash=%llu\nfont_hash=%llu\nbackend_hash=%llu\nwindow_hash=%llu\n",
			Cache.m_LastSettingsPage, Cache.m_LastTClientTab, Cache.m_LastQmTab, Cache.m_LastScrollY,
			Cache.m_RuntimeKey.m_ViewportWidth, Cache.m_RuntimeKey.m_ViewportHeight, Cache.m_RuntimeKey.m_UiScale,
			static_cast<unsigned long long>(Cache.m_RuntimeKey.m_ConfigHash), static_cast<unsigned long long>(Cache.m_RuntimeKey.m_LanguageHash),
			static_cast<unsigned long long>(Cache.m_RuntimeKey.m_FontHash), static_cast<unsigned long long>(Cache.m_RuntimeKey.m_BackendHash), static_cast<unsigned long long>(Cache.m_RuntimeKey.m_WindowHash));
		if(Length <= 0 || Length >= static_cast<int>(sizeof(aData)) - 1)
			return false;
		const std::string Target(pFilename);
		const std::string Temporary = Target + ".qm-save." + std::to_string(pid()) + ".tmp";
		char aRoot[IO_MAX_PATH_LENGTH] = "";
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
		if(!aRoot[0] || str_length(aRoot) + Temporary.size() >= IO_MAX_PATH_LENGTH ||
			pStorage->FileExists(Temporary.c_str(), IStorage::TYPE_SAVE) || pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE))
			return false;
		pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		CSessionCacheFileOperations Default;
		auto &Operations = pOperations ? *pOperations : Default;
		SSessionCacheCandidate Candidate{*pStorage, Temporary};
		std::unique_ptr<void, SSessionCacheFileCloser> File(Operations.Open(*pStorage, Temporary.c_str(), IOFLAG_WRITE), {Operations});
		if(!File)
			return false;
		Candidate.m_Owned = true;
		if(Operations.Write(File.get(), aData, Length) != static_cast<unsigned>(Length) || Operations.Sync(File.get()) != 0 || Operations.Error(File.get()) != 0)
			return false;
		if(Operations.Close(File.release()) != 0 || !Operations.Publish(*pStorage, Temporary.c_str(), Target.c_str()))
			return false;
		Candidate.m_Owned = false;
		return true;
	}
	catch(...)
	{
		return false;
	}
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
