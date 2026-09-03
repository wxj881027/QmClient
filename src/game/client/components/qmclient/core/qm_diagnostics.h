/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H

#include <base/types.h>
#include <base/lock.h>

#include <atomic>
#include <cstdint>
#include <vector>

class IGraphics;
class IStorage;

class CQmDiagnostics final
{
public:
	struct SNonBlockingDropStats
	{
		uint64_t m_EventAttempts = 0;
		uint64_t m_EventEnqueued = 0;
		uint64_t m_EventDropped = 0;
		uint64_t m_SessionLockBusy = 0;
		uint64_t m_SessionInactive = 0;
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

	void RecordEvent(const char *pName, const char *pDetails = nullptr);
	void RecordEventNonBlocking(const char *pName, const char *pDetails = nullptr);
	void RecordGraphicsInfo();
	bool IsActive() const;
	SNonBlockingDropStats NonBlockingDropStats() const;

private:
	enum class ENonBlockingWriteResult
	{
		WRITTEN,
		SESSION_LOCK_BUSY,
		SESSION_INACTIVE,
		WRITER_LOCK_BUSY,
		BUFFER_CAPACITY,
	};

	void RecordEventImpl(const char *pName, const char *pDetails, bool NonBlocking);
	ENonBlockingWriteResult WriteJsonLine(const char *pJson, bool NonBlocking = false);
	void WriteSessionStart();
	void WriteWindowSummary();
	void WriteDiagnosticsSummary();
	void WriteReport();
	void CheckAsyncWriteError();
	void MarkWriteFailure(const char *pOperation);
	void PushSample(std::vector<int64_t> &vSamples, int64_t Sample);
	void RecordNonBlockingDrop(ENonBlockingWriteResult Reason);

	IStorage *m_pStorage = nullptr;
	IGraphics *m_pGraphics = nullptr;
	ASYNCIO *m_pAsyncSession = nullptr;
	mutable CLock m_SessionLock;
	bool m_WriteFailed = false;
	char m_aSessionName[128]{};
	char m_aBackendConfig[256]{};
	char m_aActiveApiName[64]{};
	bool m_GraphicsInfoRecorded = false;
	bool m_ActiveApiAvailable = false;
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

	std::atomic<uint64_t> m_NonBlockingEventAttempts{0};
	std::atomic<uint64_t> m_NonBlockingEventEnqueued{0};
	std::atomic<uint64_t> m_NonBlockingEventDropped{0};
	std::atomic<uint64_t> m_NonBlockingSessionLockBusy{0};
	std::atomic<uint64_t> m_NonBlockingSessionInactive{0};
	std::atomic<uint64_t> m_NonBlockingWriterLockBusy{0};
	std::atomic<uint64_t> m_NonBlockingBufferCapacity{0};
	std::atomic<uint64_t> m_NonBlockingSerializationFailures{0};
};

#endif
