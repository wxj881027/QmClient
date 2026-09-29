#ifndef QM_HOOKS_NETEASE_QM_NETEASE_CDP_CLIENT_H
#define QM_HOOKS_NETEASE_QM_NETEASE_CDP_CLIENT_H

#include "qm_netease_cdp.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace QmNeteaseCdp
{
	class CCdpSession
	{
	public:
		using TNotificationCallback = std::function<void(std::string_view)>;

		CCdpSession();
		~CCdpSession();
		CCdpSession(const CCdpSession &) = delete;
		CCdpSession &operator=(const CCdpSession &) = delete;

		bool Connect(std::string_view WebSocketUrl);
		void Close();
		bool IsConnected() const;
		void SetNotificationCallback(TNotificationCallback Callback);
		bool Command(std::string_view Method, std::string_view ParametersJson, std::string *pResultJson, int TimeoutMs = 1500);
		bool Evaluate(std::string_view Expression, std::string *pValueJson, int TimeoutMs = 1500);
		// awaitPromise 用于 Node/Electron 主进程 inspector 的 executeJavaScript 桥
		// (返回 Promise 的异步调用);普通页面 evaluate 保持默认 false。
		bool EvaluateAwaitPromise(std::string_view Expression, std::string *pValueJson, int TimeoutMs = 1500);
		bool Pump(int TimeoutMs = 250);

	private:
		bool EvaluateImpl(std::string_view Expression, std::string *pValueJson, int TimeoutMs, bool AwaitPromise);
		struct SImpl;
		std::unique_ptr<SImpl> m_pImpl;
	};

	// 独立 worker：网络、WebSocket、重连和 JS hook 生命周期都不占用游戏
	// render thread。回调只携带不可变的 JSON 文本。
	class CFrontendBridgeWorker
	{
	public:
		using TReportCallback = std::function<void(std::string_view)>;

		CFrontendBridgeWorker();
		~CFrontendBridgeWorker();
		CFrontendBridgeWorker(const CFrontendBridgeWorker &) = delete;
		CFrontendBridgeWorker &operator=(const CFrontendBridgeWorker &) = delete;

		bool Start(uint32_t CloudMusicPid, std::wstring CommandLine, TReportCallback Callback);
		void Stop();
		bool IsConnected() const;

	private:
		void Run();
		struct SImpl;
		std::unique_ptr<SImpl> m_pImpl;
	};

	// 可由测试和日志使用的幂等 JS payload。payload 不包含 cookie、header
	// 或任何用户 session 数据。
	std::string BuildInstallHookScript();

} // namespace QmNeteaseCdp

#endif
