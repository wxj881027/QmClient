/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H

#include <base/types.h>
#include <base/lock.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <vector>

class IGraphics;
class IStorage;

class CQmDiagnostics final
{
public:
	using TFeatureTimingId = uint8_t;
	static constexpr TFeatureTimingId INVALID_FEATURE_TIMING = UINT8_MAX;

	enum class EFeatureTimingPhase
	{
		UPDATE,
		RENDER,
	};

	struct SNonBlockingDropStats
	{
		uint64_t m_EventAttempts = 0;
		uint64_t m_EventEnqueued = 0;
		uint64_t m_EventDropped = 0;
		uint64_t m_SessionLockBusy = 0;
		uint64_t m_SessionInactive = 0;
		uint64_t m_StatsGateBusy = 0;
		uint64_t m_WriterLockBusy = 0;
		uint64_t m_BufferCapacity = 0;
		uint64_t m_SerializationFailures = 0;
	};

	void Init(IStorage *pStorage, IGraphics *pGraphics);
	void Shutdown();
	void RecordGraphicsInitBegin();
	void RecordGraphicsInitFailed(const char *pDetails);

	void BeginGameUpdate();
	void EndGameUpdate();
	void BeginFrame();
	void EndFrame();
	void ResetFramePacing();
	void BeginGameRender();
	void EndGameRender();
	TFeatureTimingId RegisterFeatureTiming(const char *pFeatureId);
	void BeginFeatureTiming(TFeatureTimingId FeatureTimingId, EFeatureTimingPhase Phase);
	void EndFeatureTiming(TFeatureTimingId FeatureTimingId, EFeatureTimingPhase Phase);

	void RecordEvent(const char *pName, const char *pDetails = nullptr);
	void RecordEventNonBlocking(uint32_t SessionGeneration, const char *pName, const char *pDetails = nullptr);
	uint32_t SessionGeneration() const;
	void RecordGraphicsInfo();
	bool IsActive() const;
	SNonBlockingDropStats NonBlockingDropStats() const;

private:
	enum class EBackendFallbackResult
	{
		UNKNOWN,
		SUCCESS,
		FAILED,
		NOT_APPLIED,
	};

	struct SRecentEvent
	{
		int64_t m_MonotonicNs = 0;
		char m_aName[96]{};
		char m_aDetails[512]{};
		bool m_NameTruncated = false;
		bool m_DetailsTruncated = false;
	};

	struct SFeatureTiming
	{
		char m_aId[64]{};
		std::vector<int64_t> m_vUpdateSamples;
		std::vector<int64_t> m_vRenderSamples;
		int64_t m_UpdateStart = 0;
		int64_t m_RenderStart = 0;
	};

	enum class ENonBlockingWriteResult
	{
		WRITTEN,
		SESSION_LOCK_BUSY,
		SESSION_INACTIVE,
		STATS_GATE_BUSY,
		WRITER_LOCK_BUSY,
		BUFFER_CAPACITY,
	};

	void RecordEventImpl(const char *pName, const char *pDetails, bool NonBlocking);
	ENonBlockingWriteResult WriteJsonLine(const char *pJson, bool NonBlocking = false);
	void WriteSessionStart();
	void WriteWindowSummary();
	void WriteFeatureWindowSummaries();
	void WriteDiagnosticsSummary();
	void WriteReport();
	void CheckAsyncWriteError();
	void MarkWriteFailure(const char *pOperation);
	void PushSample(std::vector<int64_t> &vSamples, int64_t Sample);
	void RecordNonBlockingDrop(ENonBlockingWriteResult Reason);
	void RecordRecentEvent(const char *pName, const char *pDetails);
	bool IsSessionGenerationActive(uint32_t EventSessionGeneration) const;
	void UpdateBackendFallbackState(uint32_t SessionGeneration, const char *pName, const char *pDetails);
	void UpdateGraphicsSelection(const char *pName, const char *pDetails);
	static const char *BackendFallbackResultName(EBackendFallbackResult Result);
	static constexpr uint64_t BACKEND_FALLBACK_ACTIVE = 1ULL << 0;
	static constexpr uint64_t BACKEND_FALLBACK_ATTEMPTED = 1ULL << 1;
	static constexpr uint64_t BACKEND_FALLBACK_APPLIED = 1ULL << 2;
	static constexpr uint64_t BACKEND_FALLBACK_RESULT_SHIFT = 3;
	static constexpr uint64_t BACKEND_FALLBACK_RESULT_MASK = 3ULL << BACKEND_FALLBACK_RESULT_SHIFT;
	static constexpr uint64_t BACKEND_FALLBACK_GENERATION_SHIFT = 32;
	static constexpr uint64_t BACKEND_FALLBACK_GENERATION_MASK = 0xffffffffULL << BACKEND_FALLBACK_GENERATION_SHIFT;

	IStorage *m_pStorage = nullptr;
	IGraphics *m_pGraphics = nullptr;
	ASYNCIO *m_pAsyncSession = nullptr;
	mutable CLock m_SessionLock;
	bool m_WriteFailed = false;
	char m_aSessionName[128]{};
	char m_aRequestedBackend[256]{};
	char m_aBackendConfig[256]{};
	char m_aBackendSelectionSource[32]{};
	char m_aActiveApiName[64]{};
	bool m_GraphicsInfoRecorded = false;
	bool m_ActiveApiAvailable = false;
	// 单个原子字同时保存 session generation、生命周期闸门和 fallback 摘要，避免
	// report 读取到互相矛盾的字段，也拒绝旧 graphics listener 的迟到事件。
	std::atomic<uint64_t> m_BackendFallbackState{0};
	std::atomic<uint32_t> m_SessionGeneration{0};
	int64_t m_SessionStart = 0;
	int64_t m_UpdateStart = 0;
	int64_t m_FrameStart = 0;
	int64_t m_LastFrameStart = 0;
	int64_t m_RenderStart = 0;
	unsigned m_FrameCount = 0;
	unsigned m_WindowFrameCount = 0;
	std::vector<int64_t> m_vUpdateSamples;
	std::vector<int64_t> m_vFrameSamples;
	std::vector<int64_t> m_vRenderSamples;
	static constexpr size_t MAX_FEATURE_TIMINGS = 32;
	std::array<SFeatureTiming, MAX_FEATURE_TIMINGS> m_aFeatureTimings;
	size_t m_FeatureTimingCount = 0;
	// 计时只由游戏主线程采样；原子闸门只负责跨 session/写入失败时停用无锁热路径。
	std::atomic<bool> m_FeatureTimingActive{false};
	static constexpr size_t RECENT_EVENT_RING_SIZE = 64;
	std::array<SRecentEvent, RECENT_EVENT_RING_SIZE> m_aRecentEvents;
	size_t m_RecentEventNext = 0;
	size_t m_RecentEventCount = 0;
	bool m_RecentEventRingWrapped = false;
	mutable std::shared_mutex m_NonBlockingStatsLock;

	std::atomic<uint64_t> m_NonBlockingEventAttempts{0};
	std::atomic<uint64_t> m_NonBlockingEventEnqueued{0};
	std::atomic<uint64_t> m_NonBlockingEventDropped{0};
	std::atomic<uint64_t> m_NonBlockingSessionLockBusy{0};
	std::atomic<uint64_t> m_NonBlockingSessionInactive{0};
	std::atomic<uint64_t> m_NonBlockingStatsGateBusy{0};
	std::atomic<uint64_t> m_NonBlockingWriterLockBusy{0};
	std::atomic<uint64_t> m_NonBlockingBufferCapacity{0};
	std::atomic<uint64_t> m_NonBlockingSerializationFailures{0};
};

#endif
