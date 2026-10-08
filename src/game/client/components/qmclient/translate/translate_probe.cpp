#include "translate_probe.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <algorithm>

uint64_t TranslateConfigurationRevision()
{
	// 不保存凭据副本；指纹仅用于本机配置变更的 owner 失效，不参与认证。
	uint64_t Revision = 14695981039346656037ull;
	const auto Mix = [&Revision](const char *pText) {
		for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pText); *p; ++p)
			Revision = (Revision ^ *p) * 1099511628211ull;
		Revision = (Revision ^ 0xffu) * 1099511628211ull;
	};
	Mix(g_Config.m_QmTranslateBackend);
	Mix(g_Config.m_QmTranslateTarget);
	Mix(g_Config.m_QmTranslateSource);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
	{
		Mix(GetSelectedTranslateLlmKey());
		const int Provider = g_Config.m_QmTranslateLlmProvider;
		const char *apEndpoints[] = {g_Config.m_QmTranslateLlmEndpointZhipu, g_Config.m_QmTranslateLlmEndpointDeepseek, g_Config.m_QmTranslateLlmEndpointOpenai, g_Config.m_QmTranslateLlmEndpointCustom};
		const char *apModels[] = {g_Config.m_QmTranslateLlmModelZhipu, g_Config.m_QmTranslateLlmModelDeepseek, g_Config.m_QmTranslateLlmModelOpenai, g_Config.m_QmTranslateLlmModelCustom};
		const int Selected = std::clamp(Provider, 0, 3);
		Mix(apEndpoints[Selected]);
		Mix(apModels[Selected]);
		Mix(g_Config.m_QmTranslateSystemPrompt);
		Revision ^= static_cast<uint64_t>(Selected | (g_Config.m_QmTranslateLlmCustomAuth << 3) | (g_Config.m_QmTranslateLlmCustomThinking << 4) | (g_Config.m_QmTranslateLlmCustomParameters << 7) | (g_Config.m_QmTranslateLlmEnableThinking << 8));
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "baidu") == 0)
	{
		Mix(g_Config.m_QmTranslateBaiduAppId);
		Mix(g_Config.m_QmTranslateBaiduKey);
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0)
		Mix(g_Config.m_QmTranslateDeeplKey);
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
	{
		Mix(g_Config.m_QmTranslateTcEndpoint);
		Mix(g_Config.m_QmTranslateTcRegion);
		Mix(g_Config.m_QmTranslateTcSecretId);
		Mix(g_Config.m_QmTranslateTcSecretKey);
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
	{
		Mix(g_Config.m_QmTranslateLibreEndpoint);
		Mix(g_Config.m_QmTranslateLibreKey);
	}
	return Revision;
}

bool CTranslateProbe::Start(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest)
{
	if(Pending())
		return false;
	Cancel();
	m_Configuration = TranslateConfigurationRevision();
	m_Touched = true;
	m_pBackend = CreateTranslateBackend(Http, pText, pTarget, pSource, pCreateRequest);
	if(!m_pBackend)
	{
		m_Response.m_Error = true;
		m_Response.m_Notice = ETranslateNotice::INVALID_CONFIGURATION;
		m_HasResult = true;
		return false;
	}
	str_copy(m_aService, m_pBackend->Name());
	return true;
}

void CTranslateProbe::Touch()
{
	if((Pending() || HasResult()) && m_Configuration != TranslateConfigurationRevision())
		Cancel();
	m_Touched = true;
}

void CTranslateProbe::AdvanceFrame()
{
	if(!m_Touched || ((Pending() || HasResult()) && m_Configuration != TranslateConfigurationRevision()))
		Cancel();
	m_Touched = false;
	if(!m_pBackend)
		return;
	const std::optional<bool> Done = m_pBackend->Update(m_Response);
	if(!Done)
		return;
	m_Response.m_Error = !*Done;
	// 诊断只保留稳定分类和状态；服务原文不留在设置任务中。
	if(m_Response.m_Error)
		m_Response.m_Text[0] = '\0';
	m_HasResult = true;
	m_pBackend.reset();
}

void CTranslateProbe::Cancel()
{
	m_pBackend.reset();
	m_Response = {};
	m_aService[0] = '\0';
	m_HasResult = false;
	m_Touched = false;
}
