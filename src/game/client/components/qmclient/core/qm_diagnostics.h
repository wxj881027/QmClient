/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_H

#include <base/types.h>
#include <base/lock.h>

#include <cstdint>
#include <vector>

class IGraphics;
class IStorage;

class CQmDiagnostics final
{
public:
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

private:
	void RecordEventImpl(const char *pName, const char *pDetails, bool NonBlocking);
	void WriteJsonLine(const char *pJson, bool NonBlocking = false);
	void WriteSessionStart();
	void WriteWindowSummary();
	void WriteReport();
	void CheckAsyncWriteError();
	void MarkWriteFailure(const char *pOperation);
	void PushSample(std::vector<int64_t> &vSamples, int64_t Sample);

	IStorage *m_pStorage = nullptr;
	IGraphics *m_pGraphics = nullptr;
	ASYNCIO *m_pAsyncSession = nullptr;
	mutable CLock m_SessionLock;
	bool m_WriteFailed = false;
	char m_aSessionName[128]{};
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
};

#endif
