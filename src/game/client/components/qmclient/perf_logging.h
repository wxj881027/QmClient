// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_LOGGING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_LOGGING_H

#include "stutter_diagnostics.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <mutex>

inline bool QmPerfEnabled()
{
	return g_Config.m_QmPerfDebug != 0;
}

inline int QmGraphicsTraceLevel()
{
	const int Configured = g_Config.m_QmGraphicsTrace < 0 ? 0 : (g_Config.m_QmGraphicsTrace > 3 ? 3 : g_Config.m_QmGraphicsTrace);
	return g_Config.m_QmMacosGraphicsDiagnostics != 0 && Configured < 1 ? 1 : Configured;
}

inline bool QmGraphicsTraceEnabled(int MinimumLevel = 1)
{
	return QmGraphicsTraceLevel() >= MinimumLevel;
}

inline bool QmMacosGraphicsDiagnosticsEnabled()
{
	return QmGraphicsTraceEnabled();
}

inline double QmPerfThresholdMs()
{
	return QmStutterFrameBudgetMs();
}

inline bool QmPerfShouldLogDuration(double DurationMs, bool Force = false)
{
	return Force || DurationMs >= QmPerfThresholdMs();
}

// 明细限流：每会话每秒至多记录 LIMIT 条非关键事件，其余计入 dropped 并
// 在秒边界或会话收尾时汇报；完整帧统计、交互窗口与卡顿汇总不参与限流。
class CQmPerfDetailBudget
{
	uint64_t m_Second = 0;
	int m_Count = 0;
	uint64_t m_Dropped = 0;

public:
	static constexpr int LIMIT = 1000;
	bool Allow(uint64_t Second)
	{
		if(Second != m_Second)
		{
			m_Second = Second;
			m_Count = 0;
		}
		if(m_Count < LIMIT)
		{
			++m_Count;
			return true;
		}
		++m_Dropped;
		return false;
	}
	uint64_t TakeDropped()
	{
		const uint64_t Dropped = m_Dropped;
		m_Dropped = 0;
		return Dropped;
	}
};

struct SQmPerfLogBudget
{
	std::mutex m_Mutex;
	CQmPerfDetailBudget m_Budget;
	uint64_t m_Session = 0;
	uint64_t m_LastReportSecond = 0;
};

inline SQmPerfLogBudget &QmPerfLogBudget()
{
	static SQmPerfLogBudget s_Budget;
	return s_Budget;
}

inline std::atomic<uint64_t> &QmPerfSessionStorage()
{
	static std::atomic<uint64_t> s_SessionId{(uint64_t)time_timestamp() * 1000000};
	return s_SessionId;
}

inline uint64_t QmPerfSessionId()
{
	return QmPerfSessionStorage().load(std::memory_order_relaxed);
}

// 每打开一次性能日志文件就开一个诊断会话：同一进程内重开会话可被区分。
inline void QmPerfBeginSession()
{
	QmPerfSessionStorage().fetch_add(1, std::memory_order_relaxed);
}

inline uint64_t QmPerfFrameId(const IClient *pClient)
{
	return pClient != nullptr ? pClient->PerfFrame() : 0;
}

inline bool QmPerfTokenLooksNumeric(const char *pValue)
{
	if(pValue == nullptr || pValue[0] == '\0')
		return false;
	int Pos = (pValue[0] == '-' || pValue[0] == '+') ? 1 : 0;
	bool HasDigit = false;
	for(; pValue[Pos] != '\0'; ++Pos)
	{
		const char c = pValue[Pos];
		if(c >= '0' && c <= '9')
		{
			HasDigit = true;
			continue;
		}
		if(c == '.')
			continue;
		return false;
	}
	return HasDigit;
}

inline void QmPerfAppendCommonKeyValue(char *pBuf, int BufSize, const IClient *pClient, const char *pPage = nullptr, const char *pTab = nullptr)
{
	char aCommon[128];
	str_format(aCommon, sizeof(aCommon), " frame=%" PRIu64 " session=%" PRIu64, QmPerfFrameId(pClient), QmPerfSessionId());
	str_append(pBuf, aCommon, BufSize);
	if(pPage != nullptr && pPage[0] != '\0')
	{
		char aPage[128];
		str_format(aPage, sizeof(aPage), " page=%s", pPage);
		str_append(pBuf, aPage, BufSize);
	}
	if(pTab != nullptr && pTab[0] != '\0')
	{
		char aTab[128];
		str_format(aTab, sizeof(aTab), " tab=%s", pTab);
		str_append(pBuf, aTab, BufSize);
	}
}

inline void QmPerfAppendJsonField(char *pBuf, int BufSize, bool &First, const char *pKey, const char *pValue)
{
	if(BufSize <= 0 || pKey == nullptr || pKey[0] == '\0' || pValue == nullptr)
		return;
	// 从尾部追加，避免每段文本重复扫描已有 JSON 前缀。
	const int PrefixLength = str_length(pBuf);
	char *pTail = pBuf + PrefixLength;
	int Remaining = BufSize - PrefixLength;
	const auto Append = [&](const char *pText) {
		str_append(pTail, pText, Remaining);
		const int Added = str_length(pTail);
		pTail += Added;
		Remaining -= Added;
	};
	if(!First)
		Append(",");
	First = false;

	Append("\"");
	Append(pKey);
	Append("\":");
	if(QmPerfTokenLooksNumeric(pValue))
		Append(pValue);
	else
	{
		char aEscaped[512];
		EscapeJson(aEscaped, sizeof(aEscaped), pValue);
		Append("\"");
		Append(aEscaped);
		Append("\"");
	}
}

inline bool QmPerfPayloadLooksLikeKeyValueStart(const char *pTokenStart)
{
	if(pTokenStart == nullptr || pTokenStart[0] == '\0')
		return false;
	bool HasKeyChar = false;
	for(int Pos = 0; pTokenStart[Pos] != '\0'; ++Pos)
	{
		const char c = pTokenStart[Pos];
		if(c == '=')
			return HasKeyChar;
		if((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '_')
		{
			HasKeyChar = true;
			continue;
		}
		return false;
	}
	return false;
}

inline void QmPerfAppendPayloadJsonFields(char *pBuf, int BufSize, bool &First, const char *pPayload)
{
	if(BufSize <= 0 || pPayload == nullptr || pPayload[0] == '\0')
		return;

	const int PrefixLength = str_length(pBuf);
	char *pTail = pBuf + PrefixLength;
	int Remaining = BufSize - PrefixLength;
	for(const char *pCursor = pPayload; pCursor != nullptr && pCursor[0] != '\0';)
	{
		while(*pCursor == ' ')
			++pCursor;
		if(*pCursor == '\0')
			break;

		const char *pEqual = std::strchr(pCursor, '=');
		if(pEqual == nullptr)
			break;

		const ptrdiff_t KeyLength = pEqual - pCursor;
		if(KeyLength <= 0)
		{
			pCursor = pEqual + 1;
			continue;
		}

		char aKey[128];
		const int KeyCopyLength = minimum((int)KeyLength, (int)sizeof(aKey) - 1);
		mem_copy(aKey, pCursor, KeyCopyLength);
		aKey[KeyCopyLength] = '\0';

		const char *pValueStart = pEqual + 1;
		const char *pValueEnd = pValueStart;
		char aValue[512];
		if(*pValueStart == '"')
		{
			++pValueStart;
			pValueEnd = pValueStart;
			while(*pValueEnd != '\0' && *pValueEnd != '"')
				++pValueEnd;
		}
		else
		{
			while(*pValueEnd != '\0')
			{
				if(*pValueEnd == ' ')
				{
					const char *pNextToken = pValueEnd + 1;
					while(*pNextToken == ' ')
						++pNextToken;
					if(QmPerfPayloadLooksLikeKeyValueStart(pNextToken))
						break;
				}
				++pValueEnd;
			}
		}

		const ptrdiff_t ValueLength = pValueEnd - pValueStart;
		const int ValueCopyLength = maximum(0, minimum((int)ValueLength, (int)sizeof(aValue) - 1));
		mem_copy(aValue, pValueStart, ValueCopyLength);
		aValue[ValueCopyLength] = '\0';
		QmPerfAppendJsonField(pTail, Remaining, First, aKey, aValue);
		const int Added = str_length(pTail);
		pTail += Added;
		Remaining -= Added;

		pCursor = pValueEnd;
		if(*pCursor == '"')
			++pCursor;
	}
}

inline void QmPerfLogPayloadUnchecked(const char *pSystem, const char *pPayload, const IClient *pClient = nullptr, const char *pPage = nullptr, const char *pTab = nullptr)
{
	char aJson[2048];
	bool First = true;
	str_copy(aJson, "{", sizeof(aJson));
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "system", pSystem != nullptr ? pSystem : "");
	char aFrame[64];
	char aSession[64];
	str_format(aFrame, sizeof(aFrame), "%" PRIu64, QmPerfFrameId(pClient));
	str_format(aSession, sizeof(aSession), "%" PRIu64, QmPerfSessionId());
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "frame", aFrame);
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "session", aSession);
	if(pPage != nullptr && pPage[0] != '\0')
		QmPerfAppendJsonField(aJson, sizeof(aJson), First, "page", pPage);
	if(pTab != nullptr && pTab[0] != '\0')
		QmPerfAppendJsonField(aJson, sizeof(aJson), First, "tab", pTab);
	QmPerfAppendPayloadJsonFields(aJson, sizeof(aJson), First, pPayload);
	str_append(aJson, "}", sizeof(aJson));
	dbg_msg(pSystem, "%s", aJson);
}

inline void QmPerfLogPayload(const char *pSystem, const char *pPayload, const IClient *pClient = nullptr, const char *pPage = nullptr, const char *pTab = nullptr)
{
	if(!QmPerfEnabled())
		return;
	if(pSystem == nullptr)
		pSystem = "";
	// 完整帧统计、交互窗口及卡顿汇总不参与明细限流。
	const bool Essential = str_comp(pSystem, "perf/stutter") == 0 || str_comp(pSystem, "perf/fps") == 0 || str_comp(pSystem, "perf/session") == 0;
	if(!Essential)
	{
		auto &State = QmPerfLogBudget();
		bool Allowed;
		uint64_t Dropped = 0;
		{
			const std::lock_guard<std::mutex> Lock(State.m_Mutex);
			if(State.m_Session != QmPerfSessionId())
			{
				State.m_Budget = CQmPerfDetailBudget();
				State.m_Session = QmPerfSessionId();
			}
			const uint64_t Second = (uint64_t)time_timestamp();
			if(Second != State.m_LastReportSecond)
			{
				Dropped = State.m_Budget.TakeDropped();
				State.m_LastReportSecond = Second;
			}
			Allowed = State.m_Budget.Allow(Second);
		}
		if(Dropped != 0)
		{
			char aDropped[128];
			str_format(aDropped, sizeof(aDropped), "event=detail_sampling dropped=%" PRIu64, Dropped);
			QmPerfLogPayloadUnchecked("perf/session", aDropped, pClient);
		}
		if(!Allowed)
			return;
	}
	QmPerfLogPayloadUnchecked(pSystem, pPayload, pClient, pPage, pTab);
}

inline void QmPerfFlushDropped(const IClient *pClient)
{
	auto &State = QmPerfLogBudget();
	uint64_t Dropped;
	{
		const std::lock_guard<std::mutex> Lock(State.m_Mutex);
		Dropped = State.m_Budget.TakeDropped();
	}
	if(Dropped != 0)
	{
		char aPayload[128];
		str_format(aPayload, sizeof(aPayload), "event=detail_sampling dropped=%" PRIu64, Dropped);
		QmPerfLogPayloadUnchecked("perf/session", aPayload, pClient);
	}
}

inline void QmPerfLogPayloadForce(const char *pSystem, const char *pPayload, const IClient *pClient = nullptr, const char *pPage = nullptr, const char *pTab = nullptr)
{
	QmPerfLogPayloadUnchecked(pSystem, pPayload, pClient, pPage, pTab);
}

inline void QmMacosGraphicsDiagnosticsLogPayload(const char *pSystem, const char *pPayload, const IClient *pClient = nullptr)
{
	if(!QmMacosGraphicsDiagnosticsEnabled())
		return;
	QmPerfLogPayloadUnchecked(pSystem, pPayload, pClient);
}

inline void QmPerfLogStage(const char *pSystem, const char *pStage, double DurationMs, bool Force = false, const IClient *pClient = nullptr, const char *pPage = nullptr, const char *pTab = nullptr, const char *pExtra = nullptr)
{
	if(!QmPerfEnabled())
		return;
	if(!QmPerfShouldLogDuration(DurationMs, Force))
		return;

	char aPayload[1024];
	if(pExtra != nullptr && pExtra[0] != '\0')
		str_format(aPayload, sizeof(aPayload), "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		str_format(aPayload, sizeof(aPayload), "stage=%s duration_ms=%.3f", pStage, DurationMs);
	if(Force)
		QmPerfLogPayloadForce(pSystem, aPayload, pClient, pPage, pTab);
	else
		QmPerfLogPayload(pSystem, aPayload, pClient, pPage, pTab);
}

inline void QmPerfLogStageForce(const char *pSystem, const char *pStage, double DurationMs, const IClient *pClient = nullptr, const char *pPage = nullptr, const char *pTab = nullptr, const char *pExtra = nullptr)
{
	if(!QmPerfShouldLogDuration(DurationMs))
		return;

	char aPayload[1024];
	if(pExtra != nullptr && pExtra[0] != '\0')
		str_format(aPayload, sizeof(aPayload), "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		str_format(aPayload, sizeof(aPayload), "stage=%s duration_ms=%.3f", pStage, DurationMs);
	QmPerfLogPayloadForce(pSystem, aPayload, pClient, pPage, pTab);
}

#endif
