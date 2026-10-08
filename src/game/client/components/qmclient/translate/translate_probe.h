#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_PROBE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_PROBE_H

#include "translate_backend.h"

#include <cstdint>

// 设置页只有一个探测任务；取消、离页和配置变化均销毁旧 owner。
class CTranslateProbe
{
	std::unique_ptr<ITranslateBackend> m_pBackend;
	CTranslateResponse m_Response;
	char m_aService[32] = "";
	uint64_t m_Configuration = 0;
	bool m_Touched = false;
	bool m_HasResult = false;

public:
	bool Start(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest = CreateHttpRequest);
	void Touch();
	void AdvanceFrame();
	void Cancel();
	bool Pending() const { return m_pBackend != nullptr; }
	bool HasResult() const { return m_HasResult; }
	const CTranslateResponse &Response() const { return m_Response; }
	const char *Service() const { return m_aService; }
};

uint64_t TranslateConfigurationRevision();

#endif
