#ifndef QM_HOOKS_NETEASE_QM_NETEASE_CDP_H
#define QM_HOOKS_NETEASE_QM_NETEASE_CDP_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace QmNeteaseCdp
{
	struct SCdpTarget
	{
		std::string m_Url;
		std::string m_WebSocketDebuggerUrl;
		uint16_t m_Port = 0;
		uint32_t m_ProcessId = 0;
	};

	// 使用仓库现有 json-parser 解析 /json 响应；不通过正则或字符串切割
	// 选择 Chromium target。
	bool ParseTargetList(std::string_view Json, std::vector<SCdpTarget> *pTargets, std::string *pError = nullptr);
	bool IsOrpheusTarget(std::string_view Url);
	bool ParseLoopbackWebSocketUrl(std::string_view Url, uint16_t *pPort = nullptr);
	// /json 通常不带 processId，因此还必须核对监听端口的系统 owner。
	bool IsTrustedTargetForProcess(const SCdpTarget &Target, uint16_t DiscoveryPort, uint32_t ListenerProcessId, uint32_t ExpectedProcessId);

	// 从网易云启动参数读取显式 CDP 端口。只接受 localhost/127.0.0.1
	// 绑定，缺少参数时返回 false，由调用方选择动态高端口。
	bool ParseDebuggingPort(std::wstring_view CommandLine, uint16_t *pPort);
	std::wstring AddLoopbackDebuggingPort(std::wstring_view CommandLine, uint16_t Port);

	constexpr uint16_t DYNAMIC_PORT_MIN = 40000;
	constexpr uint16_t DYNAMIC_PORT_MAX = 49999;
	constexpr uint32_t DYNAMIC_PORT_RANGE = (uint32_t)DYNAMIC_PORT_MAX - DYNAMIC_PORT_MIN + 1;
	// Bootstrap 与无显式端口时的 helper fallback 使用同一组候选，避免
	// 一个模块选择了另一个模块永远不会扫描的端口。
	constexpr uint32_t DYNAMIC_PORT_CANDIDATE_COUNT = 128;
	constexpr uint16_t DynamicPortCandidate(uint32_t ProcessId, uint32_t Offset)
	{
		return (uint16_t)(DYNAMIC_PORT_MIN + ((ProcessId % DYNAMIC_PORT_RANGE + Offset) % DYNAMIC_PORT_RANGE));
	}
	constexpr uint32_t MAX_TARGET_JSON_BYTES = 4 * 1024 * 1024;

} // namespace QmNeteaseCdp

#endif
