// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_SHARED_WEBSOCKET_CLIENT_H
#define ENGINE_SHARED_WEBSOCKET_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 通用 WebSocket 客户端通道（QmClient 实时通道底座）。
//
// 设计约定：
// - TLS 和 WebSocket 帧由 Rust 后端处理。
// - 网络收发跑在独立 worker 线程，游戏线程只做队列收发。
// - 事件回调在 worker 线程触发，回调里不要碰 UI/引擎状态。
// - 客户端始终支持 WS/WSS，与服务端 WEBSOCKETS 构建选项无关。

enum class EQmWebSocketState
{
	IDLE = 0,
	CONNECTING,
	CONNECTED,
	RECONNECTING,
};

enum class EQmWebSocketMessageType
{
	TEXT = 0,
	BINARY,
};

struct SQmWebSocketMessage
{
	EQmWebSocketMessageType m_Type = EQmWebSocketMessageType::TEXT;
	std::string m_Data;
};

struct SQmWebSocketConnectConfig
{
	bool m_UseTls = false;
	// 主机名（不带方括号），同时用于 Host 头与 TLS SNI/证书校验。
	std::string m_Host;
	int m_Port = 80;
	// 含查询串的路径，例如 "/api/v1/ws?ver=1"。
	std::string m_Path = "/";
	// 子协议名，空串表示不协商任何子协议。
	std::string m_Protocol;
	// 附加请求头，逐条为完整的 "Name: Value"。
	std::vector<std::string> m_vHeaders;
	// 保留配置兼容；新后端拒绝跳过证书校验。
	// 证书链与域名由 rustls 使用 Mozilla 信任根校验。
	bool m_AllowInsecureTls = false;
	// 握手超时（毫秒）。<=0 表示用实现默认值。
	int m_HandshakeTimeoutMs = 0;
	// 普通业务限制 1 MiB；编辑器地图协作可使用 32 MiB。
	size_t m_MaxMessageSize = 1024 * 1024;
};

class IQmWebSocketClient
{
public:
	// 返回 false 表示该地址不可用（实现会在状态里给出具体原因）。
	using FConnect = std::function<bool(const SQmWebSocketConnectConfig &Config, std::string &Error)>;
	using FDisconnected = std::function<void(const std::string &Reason, bool Clean)>;
	using FError = std::function<void(const std::string &Message)>;
	using FMessage = std::function<void(EQmWebSocketMessageType Type, const char *pData, size_t Size)>;

	// 事件回调（worker 线程触发）。
	// - Open：握手完成（已收到服务端 101）。
	// - Disconnected：任何原因断开，Reason 为可读描述，Clean 表示对端正常关闭。
	// - Error：握手或传输错误，仅是诊断信息，断开会另外走 Disconnected。
	// - Message：一帧完整消息。使用无拷贝回调，回调期间数据有效，回调返回后失效。
	struct SCallbacks
	{
		std::function<void()> m_Open;
		FDisconnected m_Disconnected;
		FError m_Error;
		FMessage m_Message;
	};

	virtual ~IQmWebSocketClient() = default;

	// 该构建是否真的编译了 WebSocket 支持。
	virtual bool Available() const = 0;
	// 不可用时给出的原因（"built without websockets support" 等）。
	virtual const char *UnavailableReason() const = 0;

	// 调试用：在 Connect 之前调整心跳与重连退避参数（<=0 表示沿用默认值）。
	// 未实现该钩子的后端可以忽略；测试用它把心跳缩短到秒级。
	struct STuning
	{
		int m_HeartbeatMs = 0;
		int m_BackoffBaseMs = 0;
		int m_BackoffMaxMs = 0;
		// 发送队列上限；实时语音使用较小值，避免慢连接积压过期音频。
		size_t m_OutgoingQueueCapacity = 32;
	};
	virtual void SetTuning(const STuning &Tuning) { (void)Tuning; }

	// 启动/停止连接意图。Connect 只置位，实际连接在 worker 线程进行。
	// 配置或线程创建失败返回 false；Error 尽力填充，LastError 始终保留原因。
	virtual bool Connect(const SQmWebSocketConnectConfig &Config, std::string &Error) = 0;
	// 主动断开并停止重连。
	virtual void Disconnect() = 0;
	// 是否处于“应当连接”的意图状态（含正在重连）。
	virtual bool Desired() const = 0;
	virtual EQmWebSocketState State() const = 0;
	virtual const char *StateName() const = 0;

	// 主线程 -> worker：入队一条待发送消息，未连接时丢弃并返回 false。
	virtual bool SendText(const char *pData, size_t Size) = 0;
	virtual bool SendBinary(const char *pData, size_t Size) = 0;

	// 取出最早收到的消息；无消息返回 false。
	virtual bool PollMessage(SQmWebSocketMessage &Out) = 0;
	virtual size_t PendingMessages() const = 0;

	// 统计信息，供日志与设置界面展示。
	virtual int64_t LastConnectedTick() const = 0;
	virtual int64_t LastMessageTick() const = 0;
	virtual int64_t SendCount() const = 0;
	virtual int64_t RecvCount() const = 0;
	virtual int64_t DroppedIncomingCount() const = 0;
	virtual int64_t DroppedOutgoingCount() const = 0;
	virtual int64_t ReconnectCount() const = 0;
	// 最近一次往返时延（毫秒），无有效测量返回 -1。
	virtual int LastPingRttMs() const = 0;
	// 最近一次失败原因（含握手失败与断开），永不返回 nullptr。
	virtual const char *LastError() const = 0;
};

// 创建客户端实例。多次调用相互独立，各自持有 worker 线程。
std::unique_ptr<IQmWebSocketClient> CreateQmWebSocketClient(IQmWebSocketClient::SCallbacks Callbacks);

// 解析 ws:// 或 wss:// URL 为连接配置。成功返回空串，失败返回中文/英文原因。
std::string ParseQmWebSocketUrl(const char *pUrl, SQmWebSocketConnectConfig &Out);

// 断线重连退避：指数增长、带抖动、上限封顶。返回下一次等待毫秒数。
int QmWebSocketBackoffDelayMs(int Attempt, int BaseMs, int MaxMs);

#endif // ENGINE_SHARED_WEBSOCKET_CLIENT_H
