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

#include <cinttypes>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr size_t DIAGNOSTICS_WINDOW_SIZE = 600;
constexpr size_t JSON_EVENT_OVERHEAD = 64;
constexpr size_t NON_BLOCKING_EVENT_NAME_SIZE = 256;
constexpr size_t NON_BLOCKING_EVENT_DETAILS_SIZE = 16 * 1024;
constexpr size_t NON_BLOCKING_EVENT_JSON_SIZE = NON_BLOCKING_EVENT_NAME_SIZE + NON_BLOCKING_EVENT_DETAILS_SIZE + JSON_EVENT_OVERHEAD + 64;

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
	m_vUpdateSamples.clear();
	m_vFrameSamples.clear();
	m_vRenderSamples.clear();
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

	WriteSessionStart();
	log_info("qm/diagnostics", "automatic diagnostics session started: %s", m_aSessionName);
}

void CQmDiagnostics::RecordGraphicsInitBegin()
{
	RecordEvent("graphics_init_begin", g_Config.m_GfxBackend);
}

void CQmDiagnostics::RecordGraphicsInitFailed(const char *pDetails)
{
	RecordEvent("graphics_init_failed", pDetails);
	Shutdown();
}

void CQmDiagnostics::Shutdown()
{
	CLockScope SessionLock(m_SessionLock);
	if(!m_pAsyncSession)
		return;

	WriteWindowSummary();
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

void CQmDiagnostics::RecordEvent(const char *pName, const char *pDetails)
{
	CLockScope SessionLock(m_SessionLock);
	RecordEventImpl(pName, pDetails, false);
}

void CQmDiagnostics::RecordEventNonBlocking(const char *pName, const char *pDetails)
{
	if(!m_SessionLock.try_lock())
		return;
	std::unique_lock<CLock> SessionLock(m_SessionLock, std::adopt_lock);
	if(!m_pAsyncSession || m_WriteFailed)
		return;

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
		return;
	WriteJsonLine(aJson, true);
}

void CQmDiagnostics::RecordEventImpl(const char *pName, const char *pDetails, bool NonBlocking)
{
	if(!m_pAsyncSession)
		return;
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

void CQmDiagnostics::WriteJsonLine(const char *pJson, bool NonBlocking)
{
	if(!m_pAsyncSession || m_WriteFailed)
		return;
	const size_t Length = std::strlen(pJson);
	if(Length > std::numeric_limits<unsigned>::max())
		return;
	// 同一行必须在一次锁区间内入队，避免未来多线程事件把内容和换行交错。
	// Graphics callbacks use try-lock so diagnostics cannot stall the render path.
	if(NonBlocking)
	{
		if(!aio_try_lock(m_pAsyncSession))
			return;
		constexpr unsigned NewlineLength =
#if defined(CONF_FAMILY_WINDOWS)
			2;
#else
			1;
#endif
		if(Length > std::numeric_limits<unsigned>::max() - NewlineLength || !aio_write_would_fit_unlocked(m_pAsyncSession, static_cast<unsigned>(Length) + NewlineLength))
		{
			aio_unlock(m_pAsyncSession);
			return;
		}
	}
	else
		aio_lock(m_pAsyncSession);
	aio_write_unlocked(m_pAsyncSession, pJson, static_cast<unsigned>(Length));
	aio_write_newline_unlocked(m_pAsyncSession);
	aio_unlock(m_pAsyncSession);
}

void CQmDiagnostics::CheckAsyncWriteError()
{
	if(m_pAsyncSession && aio_error(m_pAsyncSession) != 0)
		MarkWriteFailure("session write");
}

void CQmDiagnostics::MarkWriteFailure(const char *pOperation)
{
	if(!m_WriteFailed)
	{
		m_WriteFailed = true;
		log_warn("qm/diagnostics", "automatic diagnostics %s failed", pOperation ? pOperation : "unknown operation");
	}
}

void CQmDiagnostics::PushSample(std::vector<int64_t> &vSamples, int64_t Sample)
{
	if(vSamples.size() >= DIAGNOSTICS_WINDOW_SIZE)
		vSamples.erase(vSamples.begin());
	vSamples.push_back(Sample);
}

void CQmDiagnostics::WriteSessionStart()
{
	char aTimestamp[64];
	char aJson[1024];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	str_format(aJson, sizeof(aJson), "{\"type\":\"session_start\",\"timestamp\":\"%s\",\"monotonic_ns\":%" PRId64 "}", aTimestamp, m_SessionStart);
	WriteJsonLine(aJson);
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
		const bool HasActiveApi = m_pGraphics->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, Major, Minor, Patch, pReportedApiName, BACKEND_TYPE_AUTO);
		if(pReportedApiName)
			pActiveApiName = pReportedApiName;

		std::string Json = "{\"type\":\"graphics_info\"";
		if(!AppendJsonStringField(Json, "backend_config", g_Config.m_GfxBackend) || !AppendJsonStringField(Json, "active_api_name", pActiveApiName) ||
			!AppendJsonStringField(Json, "vendor", m_pGraphics->GetVendorString()) || !AppendJsonStringField(Json, "renderer", m_pGraphics->GetRendererString()) ||
			!AppendJsonStringField(Json, "version", m_pGraphics->GetVersionString()))
		{
			log_warn("qm/diagnostics", "failed to serialize graphics diagnostic strings");
			return;
		}

		AppendJsonRawField(Json, "active_api_available", HasActiveApi ? "true" : "false");
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
		WriteJsonLine(Json.c_str());
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

void CQmDiagnostics::WriteWindowSummary()
{
	if(!m_pAsyncSession || m_vFrameSamples.empty())
		return;

	char aJson[1024];
	str_format(aJson, sizeof(aJson), "{\"type\":\"frame_window\",\"frames\":%u,\"frame_interval_avg_ms\":%.3f,\"frame_interval_p95_ms\":%.3f,\"frame_interval_p99_ms\":%.3f,\"frame_interval_max_ms\":%.3f,\"frame_interval_1percent_low_fps\":%.3f,\"update_cpu_avg_ms\":%.3f,\"update_cpu_p95_ms\":%.3f,\"update_cpu_p99_ms\":%.3f,\"update_cpu_max_ms\":%.3f,\"render_cpu_avg_ms\":%.3f,\"render_cpu_p95_ms\":%.3f,\"render_cpu_p99_ms\":%.3f,\"render_cpu_max_ms\":%.3f,\"texture_bytes\":%" PRIu64 ",\"buffer_bytes\":%" PRIu64 ",\"write_failed\":%s}", m_WindowFrameCount, QmDiagnostics::Average(m_vFrameSamples), QmDiagnostics::Percentile(m_vFrameSamples, 0.95), QmDiagnostics::Percentile(m_vFrameSamples, 0.99), QmDiagnostics::Percentile(m_vFrameSamples, 1.0), QmDiagnostics::OnePercentLow(m_vFrameSamples), QmDiagnostics::Average(m_vUpdateSamples), QmDiagnostics::Percentile(m_vUpdateSamples, 0.95), QmDiagnostics::Percentile(m_vUpdateSamples, 0.99), QmDiagnostics::Percentile(m_vUpdateSamples, 1.0), QmDiagnostics::Average(m_vRenderSamples), QmDiagnostics::Percentile(m_vRenderSamples, 0.95), QmDiagnostics::Percentile(m_vRenderSamples, 0.99), QmDiagnostics::Percentile(m_vRenderSamples, 1.0), m_pGraphics ? m_pGraphics->TextureMemoryUsage() : 0, m_pGraphics ? m_pGraphics->BufferMemoryUsage() : 0, m_WriteFailed ? "true" : "false");
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
	char aJson[256];
	char aSessionName[256];
	QmDiagnostics::EscapeJson(aSessionName, sizeof(aSessionName), m_aSessionName);
	str_format(aJson, sizeof(aJson), "{\"type\":\"report\",\"session\":\"%s\",\"frames\":%u,\"write_failed\":%s}", aSessionName, m_FrameCount, m_WriteFailed ? "true" : "false");
	const bool WriteOk = io_write(Report, aJson, str_length(aJson)) == str_length(aJson) && io_write_newline(Report);
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
