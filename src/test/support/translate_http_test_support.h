#ifndef TEST_SUPPORT_TRANSLATE_HTTP_TEST_SUPPORT_H
#define TEST_SUPPORT_TRANSLATE_HTTP_TEST_SUPPORT_H

#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/translate/translate_backend.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

// 请求由生产构造逻辑填写；fake 只提供传输状态和服务响应。
class CTranslateTestRequest : public IHttpRequest
{
public:
	explicit CTranslateTestRequest(const char *pUrl) : IHttpRequest(pUrl) {}
	std::vector<std::string> m_vHeaders;
	void Header(const char *pHeader) override { m_vHeaders.emplace_back(pHeader); }
	const char *Method() const { return GetRequestType(m_Type); }
	std::string Body() const { return m_pBody ? std::string(reinterpret_cast<const char *>(m_pBody), m_BodyLength) : ""; }
	const CTimeout &TimeoutSettings() const { return m_Timeout; }
	void Finish(const char *pResponse, int Status = 200)
	{
		free(m_pBuffer);
		m_ResponseLength = str_length(pResponse);
		m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength + 1));
		mem_copy(m_pBuffer, pResponse, m_ResponseLength + 1);
		m_BufferSize = m_ResponseLength + 1;
		m_StatusCode = Status;
		m_State = EHttpState::DONE;
	}
	void SetState(EHttpState State) { m_State = State; }
};

class CTranslateTestHttp : public IHttp
{
public:
	struct SSubmission
	{
		std::shared_ptr<CTranslateTestRequest> m_pRequest;
		std::string m_Method;
		std::string m_Body;
	};
	std::vector<SSubmission> m_vSubmissions;
	void Run(std::shared_ptr<IHttpRequest> pRequest) override
	{
		auto pTest = std::static_pointer_cast<CTranslateTestRequest>(pRequest);
		m_vSubmissions.push_back({pTest, pTest->Method(), pTest->Body()});
	}
	bool HasIpresolveBug() const override { return false; }
};

inline std::unique_ptr<IHttpRequest> CreateTranslateTestRequest(const char *pUrl)
{
	return std::make_unique<CTranslateTestRequest>(pUrl);
}

class CTranslateBackendTest : public ::testing::Test
{
protected:
	CConfig m_SavedConfig = g_Config;
	CTranslateTestHttp m_Http;
	void SetUp() override
	{
		str_copy(g_Config.m_QmTranslateBackend, "llm");
		g_Config.m_QmTranslateLlmProvider = 3;
		str_copy(g_Config.m_QmTranslateLlmKeyCustom, "test-key");
		str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "https://translate.test/v1");
		g_Config.m_QmTranslateLlmConcurrency = 0;
	}
	void TearDown() override { g_Config = m_SavedConfig; }
	std::unique_ptr<ITranslateBackend> Create(const char *pText = "hello", const char *pTarget = "zh", const char *pSource = "auto")
	{
		return CreateTranslateBackend(m_Http, pText, pTarget, pSource, CreateTranslateTestRequest);
	}
};

#endif
