#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_BACKEND_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_BACKEND_H

#include <engine/http.h>

#include <memory>
#include <optional>

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

class ITranslateBackend
{
public:
	virtual ~ITranslateBackend() = default;
	virtual const char *EncodeTarget(const char *pTarget) const;
	virtual bool CompareTargets(const char *pA, const char *pB) const;
	virtual const char *Name() const = 0;
	virtual std::optional<bool> Update(CTranslateResponse &Out) = 0;
};

using FTranslateRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);
std::unique_ptr<ITranslateBackend> CreateTranslateBackend(IHttp &Http, const char *pText, const char *pTarget, const char *pSource = "auto", FTranslateRequestFactory pCreateRequest = CreateHttpRequest);
// 设置提示与请求使用同一份当前 Provider 配置。
const char *GetSelectedTranslateLlmKey();
int GetTranslateConcurrency();
const char *NormalizeTranslateSource(const char *pSource);

#endif
