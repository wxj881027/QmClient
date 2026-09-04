#include "qm_diagnostics.h"

#include "qm_diagnostics_json.h"
#include "qm_diagnostics_metrics.h"

#include <base/aio.h>
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace
{
constexpr size_t DIAGNOSTICS_WINDOW_SIZE = 600;
constexpr size_t MAX_RETAINED_SESSION_FILES = 32;
constexpr size_t MAX_RETAINED_REPORT_FILES = 32;
constexpr size_t JSON_EVENT_OVERHEAD = 64;
constexpr size_t NON_BLOCKING_EVENT_NAME_SIZE = 256;
constexpr size_t NON_BLOCKING_EVENT_DETAILS_SIZE = 16 * 1024;
constexpr size_t NON_BLOCKING_EVENT_JSON_SIZE = NON_BLOCKING_EVENT_NAME_SIZE + NON_BLOCKING_EVENT_DETAILS_SIZE + JSON_EVENT_OVERHEAD + 64;

struct SDiagnosticFileEntry
{
	std::string m_Name;
	time_t m_TimeModified = 0;
};

struct SDiagnosticFileScan
{
	const char *m_pPrefix;
	const char *m_pSuffix;
	std::vector<SDiagnosticFileEntry> *m_pEntries;
};

int ScanDiagnosticFile(const CFsFileInfo *pInfo, int IsDir, int, void *pUser)
{
	if(IsDir || pInfo == nullptr || pInfo->m_pName == nullptr)
		return 0;
	auto *pScan = static_cast<SDiagnosticFileScan *>(pUser);
	if(str_startswith(pInfo->m_pName, pScan->m_pPrefix) == nullptr || str_endswith(pInfo->m_pName, pScan->m_pSuffix) == nullptr)
		return 0;
	pScan->m_pEntries->push_back({pInfo->m_pName, pInfo->m_TimeModified});
	return 0;
}

size_t RotateDiagnosticFiles(IStorage *pStorage, const char *pDirectory, const char *pPrefix, const char *pSuffix, size_t MaxFiles)
{
	try
	{
		std::vector<SDiagnosticFileEntry> vEntries;
		SDiagnosticFileScan Scan{pPrefix, pSuffix, &vEntries};
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pDirectory, ScanDiagnosticFile, &Scan);
		std::sort(vEntries.begin(), vEntries.end(), [](const SDiagnosticFileEntry &Lhs, const SDiagnosticFileEntry &Rhs) {
			if(Lhs.m_TimeModified != Rhs.m_TimeModified)
				return Lhs.m_TimeModified > Rhs.m_TimeModified;
			return Lhs.m_Name > Rhs.m_Name;
		});

		size_t RemovedFiles = 0;
		for(size_t i = MaxFiles; i < vEntries.size(); ++i)
		{
			char aFilename[IO_MAX_PATH_LENGTH];
			str_format(aFilename, sizeof(aFilename), "%s/%s", pDirectory, vEntries[i].m_Name.c_str());
			if(pStorage->RemoveFile(aFilename, IStorage::TYPE_SAVE))
				++RemovedFiles;
			else
				log_debug("qm/diagnostics", "failed to rotate diagnostics file '%s'", aFilename);
		}
		return RemovedFiles;
	}
	catch(...)
	{
		log_warn("qm/diagnostics", "failed to rotate automatic diagnostics files");
		return 0;
	}
}

bool CalculateJsonEscapeCapacity(const char *pString, size_t &Capacity)
{
	const size_t Length = static_cast<size_t>(str_length(pString ? pString : ""));
	if(Length > (std::numeric_limits<size_t>::max() - 1) / 6)
		return false;
	Capacity = Length * 6 + 1;
	return true;
}

bool EscapeJsonString(const char *pString, std::string &Result)
{
	size_t Capacity;
	if(!CalculateJsonEscapeCapacity(pString, Capacity))
		return false;

	std::vector<char> vEscaped(Capacity);
	if(!QmDiagnostics::EscapeJson(vEscaped.data(), vEscaped.size(), pString))
		return false;
	Result.assign(vEscaped.data());
	return true;
}

bool AppendJsonStringField(std::string &Json, const char *pName, const char *pValue)
{
	std::string EscapedValue;
	if(!EscapeJsonString(pValue, EscapedValue))
		return false;
	Json += ",\"";
	Json += pName;
	Json += "\":\"";
	Json += EscapedValue;
	Json += '"';
	return true;
}

void AppendJsonRawField(std::string &Json, const char *pName, const std::string &Value)
{
	Json += ",\"";
	Json += pName;
	Json += "\":";
	Json += Value;
}

bool BackendNamesMatch(const char *pConfiguredBackend, const char *pActiveApi)
{
	if(!pConfiguredBackend || !pActiveApi)
		return false;
	if(str_comp_nocase(pConfiguredBackend, "vulkan") == 0)
		return str_comp_nocase(pActiveApi, "vulkan") == 0;
	if(str_comp_nocase(pConfiguredBackend, "opengl") == 0)
		return str_comp_nocase(pActiveApi, "opengl") == 0;
	if(str_comp_nocase(pConfiguredBackend, "gles") == 0)
		return str_comp_nocase(pActiveApi, "opengl es") == 0 || str_comp_nocase(pActiveApi, "gles") == 0;
	return str_comp_nocase(pConfiguredBackend, pActiveApi) == 0;
}

bool CopyEventField(const char *pDetails, const char *pFieldName, char *pDestination, size_t DestinationSize)
{
	if(!pDetails || !pFieldName || !pDestination || DestinationSize == 0)
		return false;
	const int FieldNameLength = str_length(pFieldName);
	const char *pField = str_find(pDetails, pFieldName);
	while(pField != nullptr)
	{
		if((pField == pDetails || pField[-1] == ';') && pField[FieldNameLength] == '=')
		{
			const char *pValue = pField + FieldNameLength + 1;
			size_t ValueLength = 0;
			while(pValue[ValueLength] != '\0' && pValue[ValueLength] != ';')
				++ValueLength;
			if(ValueLength >= DestinationSize)
				ValueLength = DestinationSize - 1;
			std::memcpy(pDestination, pValue, ValueLength);
			pDestination[ValueLength] = '\0';
			return true;
		}
		pField = str_find(pField + FieldNameLength, pFieldName);
	}
	return false;
}

}

void CQmDiagnostics::Init(IStorage *pStorage, IGraphics *pGraphics)
{
	CLockScope SessionLock(m_SessionLock);
	if(m_pAsyncSession)
	{
		m_pGraphics = pGraphics;
		return;
	}
	if(!pStorage)
		return;
	m_FeatureTimingActive.store(false, std::memory_order_relaxed);
	const uint32_t PreviousGeneration = m_SessionGeneration.load(std::memory_order_relaxed);
	const uint32_t NewGeneration = PreviousGeneration == std::numeric_limits<uint32_t>::max() ? 1 : PreviousGeneration + 1;
	m_SessionGeneration.store(NewGeneration, std::memory_order_relaxed);

	m_pStorage = pStorage;
	m_pGraphics = pGraphics;
	m_WriteFailed = false;
	m_SessionStart = time_get_nanoseconds().count();
	m_UpdateStart = 0;
	m_FrameStart = 0;
	m_LastFrameStart = 0;
	m_RenderStart = 0;
	m_FrameCount = 0;
	m_WindowFrameCount = 0;
	m_aRequestedBackend[0] = '\0';
	m_aBackendConfig[0] = '\0';
	str_copy(m_aBackendSelectionSource, "unknown", sizeof(m_aBackendSelectionSource));
	m_aActiveApiName[0] = '\0';
	m_GraphicsInfoRecorded = false;
	m_ActiveApiAvailable = false;
	m_BackendFallbackState.store(0, std::memory_order_relaxed);
	m_vUpdateSamples.clear();
	m_vFrameSamples.clear();
	m_vRenderSamples.clear();
	for(size_t i = 0; i < m_FeatureTimingCount; ++i)
	{
		m_aFeatureTimings[i].m_vUpdateSamples.clear();
		m_aFeatureTimings[i].m_vRenderSamples.clear();
		m_aFeatureTimings[i].m_UpdateStart = 0;
		m_aFeatureTimings[i].m_RenderStart = 0;
	}
	m_RecentEventNext = 0;
	m_RecentEventCount = 0;
	m_RecentEventRingWrapped = false;
	m_NonBlockingEventAttempts.store(0, std::memory_order_relaxed);
	m_NonBlockingEventEnqueued.store(0, std::memory_order_relaxed);
	m_NonBlockingEventDropped.store(0, std::memory_order_relaxed);
	m_NonBlockingSessionLockBusy.store(0, std::memory_order_relaxed);
	m_NonBlockingSessionInactive.store(0, std::memory_order_relaxed);
	m_NonBlockingStatsGateBusy.store(0, std::memory_order_relaxed);
	m_NonBlockingWriterLockBusy.store(0, std::memory_order_relaxed);
	m_NonBlockingBufferCapacity.store(0, std::memory_order_relaxed);
	m_NonBlockingSerializationFailures.store(0, std::memory_order_relaxed);
	m_vUpdateSamples.reserve(DIAGNOSTICS_WINDOW_SIZE);
	m_vFrameSamples.reserve(DIAGNOSTICS_WINDOW_SIZE);
	m_vRenderSamples.reserve(DIAGNOSTICS_WINDOW_SIZE);

	if(!m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE) || !m_pStorage->CreateFolder("qmclient/diagnostics", IStorage::TYPE_SAVE))
	{
		log_warn("qm/diagnostics", "failed to create diagnostics directory");
		m_pStorage = nullptr;
		m_pGraphics = nullptr;
		return;
	}
	const size_t RemovedSessionFiles = RotateDiagnosticFiles(m_pStorage, "qmclient/diagnostics", "session-", ".jsonl", MAX_RETAINED_SESSION_FILES - 1);
	const size_t RemovedReportFiles = RotateDiagnosticFiles(m_pStorage, "qmclient/diagnostics", "report-", ".json", MAX_RETAINED_REPORT_FILES - 1);

	char aTimestamp[64];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	str_format(m_aSessionName, sizeof(m_aSessionName), "qmclient/diagnostics/session-%s-%" PRId64 ".jsonl", aTimestamp, m_SessionStart);
	IOHANDLE SessionFile = m_pStorage->OpenFile(m_aSessionName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!SessionFile)
	{
		log_warn("qm/diagnostics", "failed to open automatic session log '%s'", m_aSessionName);
		m_pStorage = nullptr;
		m_pGraphics = nullptr;
		return;
	}
	m_pAsyncSession = aio_new(SessionFile);
	if(!m_pAsyncSession)
	{
		io_close(SessionFile);
		log_warn("qm/diagnostics", "failed to initialize asynchronous session writer '%s'", m_aSessionName);
		m_pStorage = nullptr;
		m_pGraphics = nullptr;
		return;
	}
	m_FeatureTimingActive.store(true, std::memory_order_release);
	const uint64_t GenerationBits = static_cast<uint64_t>(NewGeneration) << BACKEND_FALLBACK_GENERATION_SHIFT;
	m_BackendFallbackState.store(GenerationBits | BACKEND_FALLBACK_ACTIVE, std::memory_order_release);

	WriteSessionStart();
	if(RemovedSessionFiles > 0 || RemovedReportFiles > 0)
	{
		char aDetails[128];
		str_format(aDetails, sizeof(aDetails), "session_files_removed=%" PRIu64 ";report_files_removed=%" PRIu64 ";session_file_limit=%" PRIu64 ";report_file_limit=%" PRIu64, static_cast<uint64_t>(RemovedSessionFiles), static_cast<uint64_t>(RemovedReportFiles), static_cast<uint64_t>(MAX_RETAINED_SESSION_FILES), static_cast<uint64_t>(MAX_RETAINED_REPORT_FILES));
		RecordEvent("diagnostics.retention", aDetails);
	}
	log_info("qm/diagnostics", "automatic diagnostics session started: %s", m_aSessionName);
}

void CQmDiagnostics::RecordGraphicsInitBegin()
{
	{
		CLockScope SessionLock(m_SessionLock);
		str_copy(m_aRequestedBackend, g_Config.m_GfxBackend, sizeof(m_aRequestedBackend));
	}
	RecordEvent("graphics_init_begin", g_Config.m_GfxBackend);
}

void CQmDiagnostics::RecordGraphicsInitFailed(const char *pDetails)
{
	RecordEvent("graphics_init_failed", pDetails);
	Shutdown();
}

void CQmDiagnostics::Shutdown()
{
	// 先关闭无锁状态更新入口，再等待已进入 observer 的回调退出，避免迟到事件污染 report 或下一 session。
	m_BackendFallbackState.fetch_and(~BACKEND_FALLBACK_ACTIVE, std::memory_order_acq_rel);
	m_FeatureTimingActive.store(false, std::memory_order_release);
	std::unique_lock<std::shared_mutex> NonBlockingStatsLock(m_NonBlockingStatsLock);
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession)
		return;

	WriteWindowSummary();
	WriteDiagnosticsSummary();
	WriteJsonLine("{\"type\":\"session_end\"}");
	aio_close(m_pAsyncSession);
	aio_wait(m_pAsyncSession);
	if(aio_error(m_pAsyncSession) != 0)
		MarkWriteFailure("session write");
	aio_free(m_pAsyncSession);
	m_pAsyncSession = nullptr;
	WriteReport();
	m_pStorage = nullptr;
	m_pGraphics = nullptr;
}

void CQmDiagnostics::BeginFrame()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession || m_WriteFailed)
		return;
	CheckAsyncWriteError();
	if(m_WriteFailed)
		return;

	const int64_t Now = time_get_nanoseconds().count();
	if(m_LastFrameStart != 0)
	{
		PushSample(m_vFrameSamples, Now - m_LastFrameStart);
		++m_WindowFrameCount;
		if(m_WindowFrameCount >= DIAGNOSTICS_WINDOW_SIZE)
		{
			WriteWindowSummary();
			m_vUpdateSamples.clear();
			m_vFrameSamples.clear();
			m_vRenderSamples.clear();
			for(size_t i = 0; i < m_FeatureTimingCount; ++i)
			{
				m_aFeatureTimings[i].m_vUpdateSamples.clear();
				m_aFeatureTimings[i].m_vRenderSamples.clear();
				m_aFeatureTimings[i].m_UpdateStart = 0;
				m_aFeatureTimings[i].m_RenderStart = 0;
			}
			m_WindowFrameCount = 0;
		}
	}
	m_LastFrameStart = Now;
	m_FrameStart = Now;
}

void CQmDiagnostics::EndFrame()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession || m_WriteFailed || m_FrameStart == 0)
		return;

	m_FrameStart = 0;
	++m_FrameCount;
}

void CQmDiagnostics::BeginGameUpdate()
{
	CLockScope SessionLock(m_SessionLock);
	if(m_pAsyncSession)
		m_UpdateStart = time_get_nanoseconds().count();
}

void CQmDiagnostics::EndGameUpdate()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession || m_UpdateStart == 0)
		return;

	PushSample(m_vUpdateSamples, time_get_nanoseconds().count() - m_UpdateStart);
	m_UpdateStart = 0;
}

void CQmDiagnostics::BeginGameRender()
{
	CLockScope SessionLock(m_SessionLock);
	if(m_pAsyncSession)
		m_RenderStart = time_get_nanoseconds().count();
}

void CQmDiagnostics::EndGameRender()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession || m_RenderStart == 0)
		return;

	PushSample(m_vRenderSamples, time_get_nanoseconds().count() - m_RenderStart);
	m_RenderStart = 0;
}

CQmDiagnostics::TFeatureTimingId CQmDiagnostics::RegisterFeatureTiming(const char *pFeatureId)
{
	if(!pFeatureId || pFeatureId[0] == '\0')
		return INVALID_FEATURE_TIMING;

	CLockScope SessionLock(m_SessionLock);
	for(size_t i = 0; i < m_FeatureTimingCount; ++i)
	{
		if(str_comp(m_aFeatureTimings[i].m_aId, pFeatureId) == 0)
			return static_cast<TFeatureTimingId>(i);
	}
	if(m_FeatureTimingCount >= m_aFeatureTimings.size() || str_length(pFeatureId) >= sizeof(m_aFeatureTimings[0].m_aId))
		return INVALID_FEATURE_TIMING;

	SFeatureTiming &Timing = m_aFeatureTimings[m_FeatureTimingCount];
	try
	{
		Timing.m_vUpdateSamples.reserve(DIAGNOSTICS_WINDOW_SIZE);
		Timing.m_vRenderSamples.reserve(DIAGNOSTICS_WINDOW_SIZE);
	}
	catch(...)
	{
		Timing.m_vUpdateSamples.clear();
		Timing.m_vRenderSamples.clear();
		log_warn("qm/diagnostics", "failed to reserve feature timing samples for '%s'", pFeatureId);
		return INVALID_FEATURE_TIMING;
	}
	str_copy(Timing.m_aId, pFeatureId, sizeof(Timing.m_aId));
	const TFeatureTimingId TimingId = static_cast<TFeatureTimingId>(m_FeatureTimingCount);
	++m_FeatureTimingCount;
	return TimingId;
}

void CQmDiagnostics::BeginFeatureTiming(TFeatureTimingId FeatureTimingId, EFeatureTimingPhase Phase)
{
	if(!m_FeatureTimingActive.load(std::memory_order_acquire) || FeatureTimingId == INVALID_FEATURE_TIMING || FeatureTimingId >= m_FeatureTimingCount)
		return;
	SFeatureTiming &Timing = m_aFeatureTimings[FeatureTimingId];
	int64_t &Start = Phase == EFeatureTimingPhase::UPDATE ? Timing.m_UpdateStart : Timing.m_RenderStart;
	if(Start == 0)
		Start = time_get_nanoseconds().count();
}

void CQmDiagnostics::EndFeatureTiming(TFeatureTimingId FeatureTimingId, EFeatureTimingPhase Phase)
{
	if(!m_FeatureTimingActive.load(std::memory_order_acquire) || FeatureTimingId == INVALID_FEATURE_TIMING || FeatureTimingId >= m_FeatureTimingCount)
		return;
	SFeatureTiming &Timing = m_aFeatureTimings[FeatureTimingId];
	int64_t &Start = Phase == EFeatureTimingPhase::UPDATE ? Timing.m_UpdateStart : Timing.m_RenderStart;
	if(Start == 0)
		return;
	PushSample(Phase == EFeatureTimingPhase::UPDATE ? Timing.m_vUpdateSamples : Timing.m_vRenderSamples, time_get_nanoseconds().count() - Start);
	Start = 0;
}

void CQmDiagnostics::RecordEvent(const char *pName, const char *pDetails)
{
	CLockScope SessionLock(m_SessionLock);
	RecordEventImpl(pName, pDetails, false);
}

void CQmDiagnostics::RecordEventNonBlocking(uint32_t EventSessionGeneration, const char *pName, const char *pDetails)
{
	m_NonBlockingEventAttempts.fetch_add(1, std::memory_order_relaxed);
	if(!IsSessionGenerationActive(EventSessionGeneration))
	{
		RecordNonBlockingDrop(ENonBlockingWriteResult::SESSION_INACTIVE);
		return;
	}
	UpdateBackendFallbackState(EventSessionGeneration, pName, pDetails);
	if(!m_NonBlockingStatsLock.try_lock_shared())
	{
		RecordNonBlockingDrop(ENonBlockingWriteResult::STATS_GATE_BUSY);
		return;
	}
	std::shared_lock<std::shared_mutex> NonBlockingStatsLock(m_NonBlockingStatsLock, std::adopt_lock);
	if(!m_SessionLock.try_lock())
	{
		RecordNonBlockingDrop(ENonBlockingWriteResult::SESSION_LOCK_BUSY);
		return;
	}
	RecordRecentEvent(pName, pDetails);
	std::unique_lock<CLock> SessionLock(m_SessionLock, std::adopt_lock);
	UpdateGraphicsSelection(pName, pDetails);
	if(!m_pAsyncSession || m_WriteFailed)
	{
		m_NonBlockingSessionInactive.fetch_add(1, std::memory_order_relaxed);
		m_NonBlockingEventDropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	// Graphics callbacks are observers of a real-time path. Keep this path
	// allocation-free and do not wait for the asynchronous writer lock.
	char aName[NON_BLOCKING_EVENT_NAME_SIZE];
	char aDetails[NON_BLOCKING_EVENT_DETAILS_SIZE];
	char aJson[NON_BLOCKING_EVENT_JSON_SIZE];
	const bool NameComplete = QmDiagnostics::EscapeJson(aName, sizeof(aName), pName);
	const bool DetailsComplete = QmDiagnostics::EscapeJson(aDetails, sizeof(aDetails), pDetails);
	if(!NameComplete)
		str_copy(aName, "graphics.event", sizeof(aName));
	const int Written = str_format(aJson, sizeof(aJson), "{\"type\":\"event\",\"name\":\"%s\",\"details\":\"%s\",\"details_truncated\":%s}", aName, aDetails, DetailsComplete ? "false" : "true");
	if(Written < 0 || static_cast<size_t>(Written) >= sizeof(aJson))
	{
		m_NonBlockingSerializationFailures.fetch_add(1, std::memory_order_relaxed);
		m_NonBlockingEventDropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	const ENonBlockingWriteResult Result = WriteJsonLine(aJson, true);
	if(Result == ENonBlockingWriteResult::WRITTEN)
		m_NonBlockingEventEnqueued.fetch_add(1, std::memory_order_relaxed);
	else
		RecordNonBlockingDrop(Result);
}

bool CQmDiagnostics::IsSessionGenerationActive(uint32_t EventSessionGeneration) const
{
	const uint64_t State = m_BackendFallbackState.load(std::memory_order_acquire);
	if((State & BACKEND_FALLBACK_ACTIVE) == 0)
		return false;
	const uint32_t StateGeneration = static_cast<uint32_t>((State & BACKEND_FALLBACK_GENERATION_MASK) >> BACKEND_FALLBACK_GENERATION_SHIFT);
	return StateGeneration == EventSessionGeneration;
}

uint32_t CQmDiagnostics::SessionGeneration() const
{
	return m_SessionGeneration.load(std::memory_order_acquire);
}

void CQmDiagnostics::RecordEventImpl(const char *pName, const char *pDetails, bool NonBlocking)
{
	if(!m_pAsyncSession)
		return;
	UpdateBackendFallbackState(SessionGeneration(), pName, pDetails);
	RecordRecentEvent(pName, pDetails);
	UpdateGraphicsSelection(pName, pDetails);
	try
	{
		if(!NonBlocking)
			CheckAsyncWriteError();
		if(m_WriteFailed)
			return;

		// 图形驱动扩展列表等诊断字段可能远大于固定的小日志缓冲区。
		// 事件不是每帧路径，按输入长度分配空间，避免合法 JSON 中静默丢失诊断内容。
		size_t NameCapacity;
		size_t DetailsCapacity;
		if(!CalculateJsonEscapeCapacity(pName, NameCapacity) || !CalculateJsonEscapeCapacity(pDetails, DetailsCapacity) ||
			NameCapacity > std::numeric_limits<size_t>::max() - JSON_EVENT_OVERHEAD ||
			DetailsCapacity > std::numeric_limits<size_t>::max() - JSON_EVENT_OVERHEAD - NameCapacity)
		{
			log_warn("qm/diagnostics", "diagnostic event is too large to serialize");
			return;
		}
		std::vector<char> aName(NameCapacity);
		std::vector<char> aDetails(DetailsCapacity);
		const bool NameComplete = QmDiagnostics::EscapeJson(aName.data(), aName.size(), pName);
		const bool DetailsComplete = QmDiagnostics::EscapeJson(aDetails.data(), aDetails.size(), pDetails);
		if(!NameComplete || !DetailsComplete)
		{
			// 这些容量按最坏情况计算；保留显式检查，避免未来修改转义规则后重新引入静默截断。
			log_warn("qm/diagnostics", "failed to fully serialize diagnostic event '%s'", pName ? pName : "(unnamed)");
			return;
		}

		std::vector<char> aJson(NameCapacity + DetailsCapacity + JSON_EVENT_OVERHEAD);
		str_format(aJson.data(), aJson.size(), "{\"type\":\"event\",\"name\":\"%s\",\"details\":\"%s\"}", aName.data(), aDetails.data());
		WriteJsonLine(aJson.data(), NonBlocking);
	}
	catch(...)
	{
		// 诊断是旁路能力，分配失败不能改变客户端或图形后端的控制流。
		log_warn("qm/diagnostics", "failed to allocate diagnostic event '%s'", pName ? pName : "(unnamed)");
	}
}

void CQmDiagnostics::UpdateGraphicsSelection(const char *pName, const char *pDetails)
{
	if(!pName || !pDetails)
		return;
	const bool IsSelectionEvent = str_comp(pName, "graphics.backend_selection") == 0 || str_comp(pName, "graphics.backend_fallback_attempt") == 0 || str_comp(pName, "graphics.backend_fallback_result") == 0;
	if(!IsSelectionEvent)
		return;

	char aValue[32];
	if(CopyEventField(pDetails, "selection_source", aValue, sizeof(aValue)))
		str_copy(m_aBackendSelectionSource, aValue, sizeof(m_aBackendSelectionSource));
}

void CQmDiagnostics::UpdateBackendFallbackState(uint32_t SessionGeneration, const char *pName, const char *pDetails)
{
	if(!pName || !pDetails)
		return;

	char aValue[32];
	uint64_t SetBits = 0;
	uint64_t ResultBits = 0;
	bool HasResult = false;
	if(str_comp(pName, "graphics.backend_fallback_attempt") == 0)
	{
		SetBits |= BACKEND_FALLBACK_ATTEMPTED;
		if(CopyEventField(pDetails, "applied", aValue, sizeof(aValue)) && str_comp(aValue, "true") == 0)
			SetBits |= BACKEND_FALLBACK_APPLIED;
	}
	else if(str_comp(pName, "graphics.backend_fallback_result") == 0 && CopyEventField(pDetails, "result", aValue, sizeof(aValue)))
	{
		EBackendFallbackResult Result = EBackendFallbackResult::UNKNOWN;
		if(str_comp(aValue, "success") == 0)
			Result = EBackendFallbackResult::SUCCESS;
		else if(str_comp(aValue, "failed") == 0)
			Result = EBackendFallbackResult::FAILED;
		else if(str_comp(aValue, "not_applied") == 0)
			Result = EBackendFallbackResult::NOT_APPLIED;
		if(Result != EBackendFallbackResult::UNKNOWN)
		{
			ResultBits = static_cast<uint64_t>(Result) << BACKEND_FALLBACK_RESULT_SHIFT;
			HasResult = true;
		}
	}
	else
		return;

	uint64_t State = m_BackendFallbackState.load(std::memory_order_acquire);
	for(;;)
	{
		if((State & BACKEND_FALLBACK_ACTIVE) == 0)
			return;
		const uint32_t StateGeneration = static_cast<uint32_t>((State & BACKEND_FALLBACK_GENERATION_MASK) >> BACKEND_FALLBACK_GENERATION_SHIFT);
		if(StateGeneration != SessionGeneration)
			return;
		uint64_t NewState = State | SetBits;
		if(HasResult)
			NewState = (NewState & ~BACKEND_FALLBACK_RESULT_MASK) | ResultBits;
		if(m_BackendFallbackState.compare_exchange_weak(State, NewState, std::memory_order_relaxed, std::memory_order_relaxed))
			return;
	}
}

const char *CQmDiagnostics::BackendFallbackResultName(EBackendFallbackResult Result)
{
	switch(Result)
	{
	case EBackendFallbackResult::SUCCESS: return "success";
	case EBackendFallbackResult::FAILED: return "failed";
	case EBackendFallbackResult::NOT_APPLIED: return "not_applied";
	default: return "unknown";
	}
}

CQmDiagnostics::ENonBlockingWriteResult CQmDiagnostics::WriteJsonLine(const char *pJson, bool NonBlocking)
{
	if(!m_pAsyncSession || m_WriteFailed)
		return ENonBlockingWriteResult::SESSION_INACTIVE;
	const size_t Length = std::strlen(pJson);
	if(Length > std::numeric_limits<unsigned>::max())
		return ENonBlockingWriteResult::BUFFER_CAPACITY;
	// 同一行必须在一次锁区间内入队，避免未来多线程事件把内容和换行交错。
	// Graphics callbacks use try-lock so diagnostics cannot stall the render path.
	if(NonBlocking)
	{
		if(!aio_try_lock(m_pAsyncSession))
			return ENonBlockingWriteResult::WRITER_LOCK_BUSY;
		constexpr unsigned NewlineLength =
#if defined(CONF_FAMILY_WINDOWS)
			2;
#else
			1;
#endif
		if(Length > std::numeric_limits<unsigned>::max() - NewlineLength || !aio_write_would_fit_unlocked(m_pAsyncSession, static_cast<unsigned>(Length) + NewlineLength))
		{
			aio_unlock(m_pAsyncSession);
			return ENonBlockingWriteResult::BUFFER_CAPACITY;
		}
	}
	else
		aio_lock(m_pAsyncSession);
	aio_write_unlocked(m_pAsyncSession, pJson, static_cast<unsigned>(Length));
	aio_write_newline_unlocked(m_pAsyncSession);
	aio_unlock(m_pAsyncSession);
	return ENonBlockingWriteResult::WRITTEN;
}

void CQmDiagnostics::RecordNonBlockingDrop(ENonBlockingWriteResult Reason)
{
	m_NonBlockingEventDropped.fetch_add(1, std::memory_order_relaxed);
	switch(Reason)
	{
	case ENonBlockingWriteResult::SESSION_LOCK_BUSY:
		m_NonBlockingSessionLockBusy.fetch_add(1, std::memory_order_relaxed);
		break;
	case ENonBlockingWriteResult::SESSION_INACTIVE:
		m_NonBlockingSessionInactive.fetch_add(1, std::memory_order_relaxed);
		break;
	case ENonBlockingWriteResult::STATS_GATE_BUSY:
		m_NonBlockingStatsGateBusy.fetch_add(1, std::memory_order_relaxed);
		break;
	case ENonBlockingWriteResult::WRITER_LOCK_BUSY:
		m_NonBlockingWriterLockBusy.fetch_add(1, std::memory_order_relaxed);
		break;
	case ENonBlockingWriteResult::BUFFER_CAPACITY:
		m_NonBlockingBufferCapacity.fetch_add(1, std::memory_order_relaxed);
		break;
	case ENonBlockingWriteResult::WRITTEN:
		break;
	}
}

void CQmDiagnostics::CheckAsyncWriteError()
{
	if(m_pAsyncSession && aio_error(m_pAsyncSession) != 0)
		MarkWriteFailure("session write");
}

void CQmDiagnostics::MarkWriteFailure(const char *pOperation)
{
	m_FeatureTimingActive.store(false, std::memory_order_release);
	if(!m_WriteFailed)
	{
		m_WriteFailed = true;
		RecordRecentEvent("diagnostics.write_failure", pOperation);
		log_warn("qm/diagnostics", "automatic diagnostics %s failed", pOperation ? pOperation : "unknown operation");
	}
}

void CQmDiagnostics::PushSample(std::vector<int64_t> &vSamples, int64_t Sample)
{
	if(vSamples.size() >= DIAGNOSTICS_WINDOW_SIZE)
		vSamples.erase(vSamples.begin());
	vSamples.push_back(Sample);
}

void CQmDiagnostics::RecordRecentEvent(const char *pName, const char *pDetails)
{
	SRecentEvent &Event = m_aRecentEvents[m_RecentEventNext];
	const char *pSafeName = pName ? pName : "";
	const char *pSafeDetails = pDetails ? pDetails : "";
	Event.m_MonotonicNs = time_get_nanoseconds().count();
	Event.m_NameTruncated = str_length(pSafeName) >= static_cast<int>(sizeof(Event.m_aName));
	Event.m_DetailsTruncated = str_length(pSafeDetails) >= static_cast<int>(sizeof(Event.m_aDetails));
	str_copy(Event.m_aName, pSafeName, sizeof(Event.m_aName));
	str_copy(Event.m_aDetails, pSafeDetails, sizeof(Event.m_aDetails));
	m_RecentEventNext = (m_RecentEventNext + 1) % m_aRecentEvents.size();
	if(m_RecentEventCount < m_aRecentEvents.size())
		++m_RecentEventCount;
	else
		m_RecentEventRingWrapped = true;
}

void CQmDiagnostics::WriteSessionStart()
{
	char aTimestamp[64];
	char aJson[1024];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	str_format(aJson, sizeof(aJson), "{\"type\":\"session_start\",\"schema_version\":2,\"timestamp\":\"%s\",\"monotonic_ns\":%" PRId64 ",\"non_blocking_event_policy\":\"try_lock_bounded_buffer\"}", aTimestamp, m_SessionStart);
	WriteJsonLine(aJson);
}

void CQmDiagnostics::WriteFeatureWindowSummaries()
{
	if(!m_pAsyncSession)
		return;
	for(size_t i = 0; i < m_FeatureTimingCount; ++i)
	{
		const SFeatureTiming &Timing = m_aFeatureTimings[i];
		char aFeatureId[sizeof(Timing.m_aId) * 2 + 1];
		if(!QmDiagnostics::EscapeJson(aFeatureId, sizeof(aFeatureId), Timing.m_aId))
			continue;
		char aJson[2048];
		const int Written = str_format(aJson, sizeof(aJson), "{\"type\":\"feature_window\",\"feature\":\"%s\",\"frames\":%u,\"update_samples\":%u,\"update_avg_ms\":%.3f,\"update_p95_ms\":%.3f,\"update_p99_ms\":%.3f,\"update_max_ms\":%.3f,\"render_samples\":%u,\"render_avg_ms\":%.3f,\"render_p95_ms\":%.3f,\"render_p99_ms\":%.3f,\"render_max_ms\":%.3f}", aFeatureId, m_WindowFrameCount, static_cast<unsigned>(Timing.m_vUpdateSamples.size()), QmDiagnostics::Average(Timing.m_vUpdateSamples), QmDiagnostics::Percentile(Timing.m_vUpdateSamples, 0.95), QmDiagnostics::Percentile(Timing.m_vUpdateSamples, 0.99), QmDiagnostics::Percentile(Timing.m_vUpdateSamples, 1.0), static_cast<unsigned>(Timing.m_vRenderSamples.size()), QmDiagnostics::Average(Timing.m_vRenderSamples), QmDiagnostics::Percentile(Timing.m_vRenderSamples, 0.95), QmDiagnostics::Percentile(Timing.m_vRenderSamples, 0.99), QmDiagnostics::Percentile(Timing.m_vRenderSamples, 1.0));
		if(Written < 0 || static_cast<size_t>(Written) >= sizeof(aJson))
		{
			log_warn("qm/diagnostics", "feature timing summary is too large for '%s'", Timing.m_aId);
			continue;
		}
		WriteJsonLine(aJson);
	}
}

void CQmDiagnostics::RecordGraphicsInfo()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pGraphics)
		return;
	try
	{
		const char *pActiveApiName = "unknown";
		int Major = 0;
		int Minor = 0;
		int Patch = 0;
		const char *pReportedApiName = nullptr;
		const bool HasActiveApiVersion = m_pGraphics->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, Major, Minor, Patch, pReportedApiName, BACKEND_TYPE_AUTO);
		if(pReportedApiName)
			pActiveApiName = pReportedApiName;
		const bool ActiveApiAvailable = pActiveApiName[0] != '\0' && str_comp_nocase(pActiveApiName, "unknown") != 0;

		std::string Json = "{\"type\":\"graphics_info\"";
		if(!AppendJsonStringField(Json, "backend_config", g_Config.m_GfxBackend) || !AppendJsonStringField(Json, "backend_selection_source", m_aBackendSelectionSource) || !AppendJsonStringField(Json, "effective_backend", pActiveApiName) || !AppendJsonStringField(Json, "active_api_name", pActiveApiName) ||
			!AppendJsonStringField(Json, "vendor", m_pGraphics->GetVendorString()) || !AppendJsonStringField(Json, "renderer", m_pGraphics->GetRendererString()) ||
			!AppendJsonStringField(Json, "version", m_pGraphics->GetVersionString()))
		{
			log_warn("qm/diagnostics", "failed to serialize graphics diagnostic strings");
			return;
		}
		str_copy(m_aBackendConfig, g_Config.m_GfxBackend, sizeof(m_aBackendConfig));
		str_copy(m_aActiveApiName, pActiveApiName, sizeof(m_aActiveApiName));

		AppendJsonRawField(Json, "active_api_available", ActiveApiAvailable ? "true" : "false");
		AppendJsonRawField(Json, "active_api_version_available", HasActiveApiVersion ? "true" : "false");
		AppendJsonRawField(Json, "active_api_major", std::to_string(Major));
		AppendJsonRawField(Json, "active_api_minor", std::to_string(Minor));
		AppendJsonRawField(Json, "active_api_patch", std::to_string(Patch));
		AppendJsonRawField(Json, "modern_api", m_pGraphics->IsConfigModernAPI() ? "true" : "false");
		AppendJsonRawField(Json, "tile_buffering", m_pGraphics->IsTileBufferingEnabled() ? "true" : "false");
		AppendJsonRawField(Json, "quad_buffering", m_pGraphics->IsQuadBufferingEnabled() ? "true" : "false");
		AppendJsonRawField(Json, "text_buffering", m_pGraphics->IsTextBufferingEnabled() ? "true" : "false");
		AppendJsonRawField(Json, "quad_container_buffering", m_pGraphics->IsQuadContainerBufferingEnabled() ? "true" : "false");
		AppendJsonRawField(Json, "texture_bytes", std::to_string(m_pGraphics->TextureMemoryUsage()));
		AppendJsonRawField(Json, "buffer_bytes", std::to_string(m_pGraphics->BufferMemoryUsage()));
		AppendJsonRawField(Json, "streamed_bytes", std::to_string(m_pGraphics->StreamedMemoryUsage()));
		AppendJsonRawField(Json, "staging_bytes", std::to_string(m_pGraphics->StagingMemoryUsage()));
		AppendJsonRawField(Json, "width", std::to_string(m_pGraphics->ScreenWidth()));
		AppendJsonRawField(Json, "height", std::to_string(m_pGraphics->ScreenHeight()));
		AppendJsonRawField(Json, "hidpi", std::to_string(m_pGraphics->ScreenHiDPIScale()));
		Json += '}';
		const bool GraphicsInfoEnqueued = WriteJsonLine(Json.c_str()) == ENonBlockingWriteResult::WRITTEN;
		if(GraphicsInfoEnqueued)
		{
			m_GraphicsInfoRecorded = true;
			m_ActiveApiAvailable = ActiveApiAvailable;
			RecordRecentEvent("graphics_info", pActiveApiName);
		}
		else
			log_warn("qm/diagnostics", "failed to enqueue graphics diagnostic record");
	}
	catch(...)
	{
		log_warn("qm/diagnostics", "failed to allocate graphics diagnostic record");
	}
}

void CQmDiagnostics::ResetFramePacing()
{
	CLockScope SessionLock(m_SessionLock);
	m_LastFrameStart = 0;
	m_FrameStart = 0;
}

bool CQmDiagnostics::IsActive() const
{
	CLockScope SessionLock(m_SessionLock);
	return m_pAsyncSession != nullptr && !m_WriteFailed;
}

CQmDiagnostics::SNonBlockingDropStats CQmDiagnostics::NonBlockingDropStats() const
{
	return {
		.m_EventAttempts = m_NonBlockingEventAttempts.load(std::memory_order_relaxed),
		.m_EventEnqueued = m_NonBlockingEventEnqueued.load(std::memory_order_relaxed),
		.m_EventDropped = m_NonBlockingEventDropped.load(std::memory_order_relaxed),
		.m_SessionLockBusy = m_NonBlockingSessionLockBusy.load(std::memory_order_relaxed),
		.m_SessionInactive = m_NonBlockingSessionInactive.load(std::memory_order_relaxed),
		.m_StatsGateBusy = m_NonBlockingStatsGateBusy.load(std::memory_order_relaxed),
		.m_WriterLockBusy = m_NonBlockingWriterLockBusy.load(std::memory_order_relaxed),
		.m_BufferCapacity = m_NonBlockingBufferCapacity.load(std::memory_order_relaxed),
		.m_SerializationFailures = m_NonBlockingSerializationFailures.load(std::memory_order_relaxed),
	};
}

void CQmDiagnostics::WriteWindowSummary()
{
	if(!m_pAsyncSession || m_vFrameSamples.empty())
		return;

	char aJson[1024];
	str_format(aJson, sizeof(aJson), "{\"type\":\"frame_window\",\"frames\":%u,\"frame_interval_avg_ms\":%.3f,\"frame_interval_p95_ms\":%.3f,\"frame_interval_p99_ms\":%.3f,\"frame_interval_max_ms\":%.3f,\"frame_interval_1percent_low_fps\":%.3f,\"update_cpu_avg_ms\":%.3f,\"update_cpu_p95_ms\":%.3f,\"update_cpu_p99_ms\":%.3f,\"update_cpu_max_ms\":%.3f,\"render_cpu_avg_ms\":%.3f,\"render_cpu_p95_ms\":%.3f,\"render_cpu_p99_ms\":%.3f,\"render_cpu_max_ms\":%.3f,\"texture_bytes\":%" PRIu64 ",\"buffer_bytes\":%" PRIu64 ",\"write_failed\":%s}", m_WindowFrameCount, QmDiagnostics::Average(m_vFrameSamples), QmDiagnostics::Percentile(m_vFrameSamples, 0.95), QmDiagnostics::Percentile(m_vFrameSamples, 0.99), QmDiagnostics::Percentile(m_vFrameSamples, 1.0), QmDiagnostics::OnePercentLow(m_vFrameSamples), QmDiagnostics::Average(m_vUpdateSamples), QmDiagnostics::Percentile(m_vUpdateSamples, 0.95), QmDiagnostics::Percentile(m_vUpdateSamples, 0.99), QmDiagnostics::Percentile(m_vUpdateSamples, 1.0), QmDiagnostics::Average(m_vRenderSamples), QmDiagnostics::Percentile(m_vRenderSamples, 0.95), QmDiagnostics::Percentile(m_vRenderSamples, 0.99), QmDiagnostics::Percentile(m_vRenderSamples, 1.0), m_pGraphics ? m_pGraphics->TextureMemoryUsage() : 0, m_pGraphics ? m_pGraphics->BufferMemoryUsage() : 0, m_WriteFailed ? "true" : "false");
	WriteJsonLine(aJson);
	WriteFeatureWindowSummaries();
}

void CQmDiagnostics::WriteDiagnosticsSummary()
{
	if(!m_pAsyncSession)
		return;

	const SNonBlockingDropStats Stats = NonBlockingDropStats();
	char aJson[1024];
	str_format(aJson, sizeof(aJson), "{\"type\":\"diagnostics_summary\",\"non_blocking_event_attempts\":%" PRIu64 ",\"non_blocking_event_enqueued\":%" PRIu64 ",\"non_blocking_event_dropped\":%" PRIu64 ",\"drop_session_lock_busy\":%" PRIu64 ",\"drop_session_inactive\":%" PRIu64 ",\"drop_stats_gate_busy\":%" PRIu64 ",\"drop_writer_lock_busy\":%" PRIu64 ",\"drop_buffer_capacity\":%" PRIu64 ",\"drop_serialization_failure\":%" PRIu64 "}", Stats.m_EventAttempts, Stats.m_EventEnqueued, Stats.m_EventDropped, Stats.m_SessionLockBusy, Stats.m_SessionInactive, Stats.m_StatsGateBusy, Stats.m_WriterLockBusy, Stats.m_BufferCapacity, Stats.m_SerializationFailures);
	WriteJsonLine(aJson);
}

void CQmDiagnostics::WriteReport()
{
	if(!m_pStorage)
		return;

	char aReportName[128];
	char aReportTmpName[128];
	const int64_t ReportTimestamp = time_timestamp();
	str_format(aReportName, sizeof(aReportName), "qmclient/diagnostics/report-%" PRId64 "-%" PRId64 ".json", ReportTimestamp, m_SessionStart);
	str_format(aReportTmpName, sizeof(aReportTmpName), "qmclient/diagnostics/report-%" PRId64 "-%" PRId64 ".tmp", ReportTimestamp, m_SessionStart);
	IOHANDLE Report = m_pStorage->OpenFile(aReportTmpName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!Report)
	{
		log_warn("qm/diagnostics", "failed to open report '%s'", aReportName);
		return;
	}
	std::string Json;
	try
	{
		Json = "{\"type\":\"report\"";
		if(!AppendJsonStringField(Json, "session", m_aSessionName) || !AppendJsonStringField(Json, "requested_backend", m_aRequestedBackend) || !AppendJsonStringField(Json, "requested_backend_config", m_aRequestedBackend) || !AppendJsonStringField(Json, "backend_config", m_aBackendConfig) || !AppendJsonStringField(Json, "backend_selection_source", m_aBackendSelectionSource) || !AppendJsonStringField(Json, "effective_backend", m_aActiveApiName) || !AppendJsonStringField(Json, "active_api_name", m_aActiveApiName))
		{
			log_warn("qm/diagnostics", "failed to serialize automatic diagnostics report");
			io_close(Report);
			m_pStorage->RemoveFile(aReportTmpName, IStorage::TYPE_SAVE);
			return;
		}
		const bool ConfigBackendMismatch = str_comp_nocase(m_aRequestedBackend, "auto") != 0 && m_aRequestedBackend[0] != '\0' && !BackendNamesMatch(m_aRequestedBackend, m_aActiveApiName);
		const uint64_t BackendFallbackState = m_BackendFallbackState.load(std::memory_order_acquire);
		const bool BackendFallbackAttempted = (BackendFallbackState & BACKEND_FALLBACK_ATTEMPTED) != 0;
		const bool BackendFallbackApplied = (BackendFallbackState & BACKEND_FALLBACK_APPLIED) != 0;
		const EBackendFallbackResult BackendFallbackResult = static_cast<EBackendFallbackResult>((BackendFallbackState & BACKEND_FALLBACK_RESULT_MASK) >> BACKEND_FALLBACK_RESULT_SHIFT);
		const char *pBackendFallbackResult = BackendFallbackResultName(BackendFallbackResult);
		const bool BackendFallback = m_GraphicsInfoRecorded && m_ActiveApiAvailable && (BackendFallbackApplied || (str_comp(m_aBackendSelectionSource, "config") == 0 && ConfigBackendMismatch));
		const SNonBlockingDropStats Stats = NonBlockingDropStats();
		AppendJsonRawField(Json, "frames", std::to_string(m_FrameCount));
		AppendJsonRawField(Json, "write_failed", m_WriteFailed ? "true" : "false");
		AppendJsonRawField(Json, "graphics_info_recorded", m_GraphicsInfoRecorded ? "true" : "false");
		AppendJsonRawField(Json, "active_api_available", m_ActiveApiAvailable ? "true" : "false");
		AppendJsonRawField(Json, "backend_fallback_attempted", BackendFallbackAttempted ? "true" : "false");
		AppendJsonRawField(Json, "backend_fallback_applied", BackendFallbackApplied ? "true" : "false");
		if(!AppendJsonStringField(Json, "backend_fallback_result", pBackendFallbackResult))
			throw std::bad_alloc();
		AppendJsonRawField(Json, "backend_fallback", BackendFallback ? "true" : "false");
		AppendJsonRawField(Json, "non_blocking_event_attempts", std::to_string(Stats.m_EventAttempts));
		AppendJsonRawField(Json, "non_blocking_event_enqueued", std::to_string(Stats.m_EventEnqueued));
		AppendJsonRawField(Json, "non_blocking_event_dropped", std::to_string(Stats.m_EventDropped));
		AppendJsonRawField(Json, "drop_session_lock_busy", std::to_string(Stats.m_SessionLockBusy));
		AppendJsonRawField(Json, "drop_session_inactive", std::to_string(Stats.m_SessionInactive));
		AppendJsonRawField(Json, "drop_stats_gate_busy", std::to_string(Stats.m_StatsGateBusy));
		AppendJsonRawField(Json, "drop_writer_lock_busy", std::to_string(Stats.m_WriterLockBusy));
		AppendJsonRawField(Json, "drop_buffer_capacity", std::to_string(Stats.m_BufferCapacity));
		AppendJsonRawField(Json, "drop_serialization_failure", std::to_string(Stats.m_SerializationFailures));
		AppendJsonRawField(Json, "recent_event_count", std::to_string(m_RecentEventCount));
		AppendJsonRawField(Json, "recent_event_ring_capacity", std::to_string(m_aRecentEvents.size()));
		AppendJsonRawField(Json, "recent_event_ring_wrapped", m_RecentEventRingWrapped ? "true" : "false");
		Json += ",\"recent_events\":[";
		const size_t FirstEvent = (m_RecentEventNext + m_aRecentEvents.size() - m_RecentEventCount) % m_aRecentEvents.size();
		for(size_t i = 0; i < m_RecentEventCount; ++i)
		{
			if(i != 0)
				Json += ',';
			const SRecentEvent &Event = m_aRecentEvents[(FirstEvent + i) % m_aRecentEvents.size()];
			Json += "{\"monotonic_ns\":" + std::to_string(Event.m_MonotonicNs);
			if(!AppendJsonStringField(Json, "name", Event.m_aName) || !AppendJsonStringField(Json, "details", Event.m_aDetails))
				throw std::bad_alloc();
			AppendJsonRawField(Json, "name_truncated", Event.m_NameTruncated ? "true" : "false");
			AppendJsonRawField(Json, "details_truncated", Event.m_DetailsTruncated ? "true" : "false");
			Json += '}';
		}
		Json += ']';
		Json += '}';
	}
	catch(...)
	{
		log_warn("qm/diagnostics", "failed to allocate automatic diagnostics report");
		io_close(Report);
		m_pStorage->RemoveFile(aReportTmpName, IStorage::TYPE_SAVE);
		return;
	}
	const size_t JsonLength = Json.size();
	const bool WriteOk = JsonLength <= std::numeric_limits<unsigned>::max() && io_write(Report, Json.data(), static_cast<unsigned>(JsonLength)) == JsonLength && io_write_newline(Report);
	const int CloseResult = io_close(Report);
	if(!WriteOk || CloseResult != 0)
	{
		log_warn("qm/diagnostics", "automatic diagnostics report write failed: %s", aReportName);
		m_pStorage->RemoveFile(aReportTmpName, IStorage::TYPE_SAVE);
		return;
	}
	if(!m_pStorage->RenameFile(aReportTmpName, aReportName, IStorage::TYPE_SAVE))
	{
		log_warn("qm/diagnostics", "automatic diagnostics report rename failed: %s", aReportName);
		m_pStorage->RemoveFile(aReportTmpName, IStorage::TYPE_SAVE);
	}
}
