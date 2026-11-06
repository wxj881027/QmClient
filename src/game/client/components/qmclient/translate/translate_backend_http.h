#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_BACKEND_HTTP_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_BACKEND_HTTP_H

#include "translate_backend.h"

#include <engine/shared/json.h>

#include <string>
#include <string_view>

// 所有 HTTP 后端共用传输生命周期与完整结果校验，服务类只处理协议差异。
class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	FTranslateRequestFactory m_pCreateRequest;
	std::shared_ptr<IHttpRequest> m_pHttpRequest;
	char m_aInitError[256] = "";
	ETranslateNotice m_InitNotice = ETranslateNotice::INVALID_CONFIGURATION;
	explicit ITranslateBackendHttp(FTranslateRequestFactory pCreateRequest);
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }
	void SetInitError(const char *pError, ETranslateNotice Notice = ETranslateNotice::INVALID_CONFIGURATION);
	void PrepareHttpRequest(const char *pUrl);

public:
	std::optional<bool> Update(CTranslateResponse &Out) override;
	~ITranslateBackendHttp() override;
};

std::string EncodeTranslateUrl(std::string_view Text);
bool CopyTranslateText(const json_value *pText, CTranslateResponse &Out);
// 语言是可选元数据：不截断，损坏时仅清空语言，不修改正文或错误分类。
bool CopyTranslateLanguage(const json_value *pLanguage, CTranslateResponse &Out);
enum class ETranslateLanguageMetadata
{
	AUTOMATIC,
	LIBRE,
	TENCENT,
	FTAPI,
	DEEPL,
	BAIDU,
};
const json_value *TranslateResponseLanguage(const json_value *pRoot, ETranslateLanguageMetadata Metadata);
// 仅排除本服务的可选语言元数据；JSON结构、其它字符串及成员名仍保持严格校验。
json_value *ParseTranslateResponseJson(const IHttpRequest &Request, ETranslateLanguageMetadata Metadata);
// 仅接受完整、可表示的整数码；失败不修改输出，服务方另行约束允许的JSON类型。
bool TryParseTranslateServiceCode(const json_value *pValue, int &Out);
std::string BuildBaiduTranslateForm(const char *pText, const char *pSource, const char *pTarget, const char *pAppId, const char *pKey, const char *pSalt);
std::unique_ptr<ITranslateBackend> CreateBaiduTranslateBackend(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest);

#endif
