#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_PROXY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_PROXY_H

#include <engine/shared/jobs.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace qm_update
{
	std::string SelectSystemProxy(std::string_view ProxyList);
	bool ProxyBypassed(std::string_view Host, std::string_view BypassList);
	bool HasEnvironmentProxy();

	// 只读取设置与解析 PAC，不持有客户端/HTTP 指针，取消后不发布结果。
	class CSystemProxyJob : public IJob
	{
		std::string m_Url;
		std::string m_Proxy;
		bool m_HasDecision = false;
		void Run() override;

	public:
		explicit CSystemProxyJob(std::string Url) : m_Url(std::move(Url)) {}
		const std::string &Proxy() const { return m_Proxy; }
		bool HasDecision() const { return m_HasDecision; }
	};
}
#endif
