#ifndef ENGINE_SHARED_WEBSOCKET_CLIENT_RUNTIME_H
#define ENGINE_SHARED_WEBSOCKET_CLIENT_RUNTIME_H

#include "websocket_client.h"

#include <thread>

// 通道状态与队列由客户端管理；网络和线程创建通过这组外部边界提供。
// 所有入口必须有效，pUser 由调用方保有，存活到客户端析构完成。
// Connect 返回的句柄只由 worker 使用，Close 必须释放它且不得抛出异常。
struct SQmWebSocketRuntime
{
	void *m_pUser;
	void *(*m_pConnect)(void *pUser, const char *pUrl, const char *pProtocol, const char *pHeaders, uint32_t TimeoutMs, size_t Limit, char *pError, size_t ErrorSize, bool (*pCanceled)(void *), void *pCancelUser);
	bool (*m_pSend)(void *pUser, void *pHandle, int Kind, const unsigned char *pData, size_t Size, char *pError, size_t ErrorSize);
	int (*m_pRead)(void *pUser, void *pHandle, const unsigned char **ppData, size_t *pSize, char *pError, size_t ErrorSize);
	void (*m_pClose)(void *pUser, void *pHandle) noexcept;
	std::thread (*m_pStartThread)(void *pUser, std::function<void()> Run);
};

std::unique_ptr<IQmWebSocketClient> CreateQmWebSocketClient(IQmWebSocketClient::SCallbacks Callbacks, const SQmWebSocketRuntime &Runtime);

#endif // ENGINE_SHARED_WEBSOCKET_CLIENT_RUNTIME_H
