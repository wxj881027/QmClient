#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_RESOURCE_LIFECYCLE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_RESOURCE_LIFECYCLE_H

#include <engine/client.h>

// 服务器列表中出现 0.7 地址不会启用资源；只有实际的 0.7 连接或回放需要它们。
inline bool QmNeedsSkin7Resources(int ClientState, bool Sixup)
{
	return Sixup && (ClientState == IClient::STATE_CONNECTING || ClientState == IClient::STATE_LOADING || ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_DEMOPLAYBACK);
}

class CQmSkin7ResourceLifecycle
{
	bool m_Requested = false;
	bool m_Ready = false;

public:
	enum class EAction
	{
		NONE,
		LOAD,
		CANCEL,
	};

	EAction Update(int ClientState, bool Sixup)
	{
		if(!QmNeedsSkin7Resources(ClientState, Sixup))
		{
			if(m_Requested && !m_Ready)
			{
				Reset();
				return EAction::CANCEL;
			}
			// 已完成的纹理保留为只读缓存，避免失效现有部件引用；离线不执行加载工作。
			return EAction::NONE;
		}
		if(m_Requested)
			return EAction::NONE;
		m_Requested = true;
		return EAction::LOAD;
	}

	// 失败待重试的请求仍需在离开连接时取消，但不能提前丢弃已就绪缓存状态。
	bool RequestRefresh(int ClientState, bool Sixup)
	{
		if(!QmNeedsSkin7Resources(ClientState, Sixup))
			return false;
		m_Requested = true;
		return true;
	}

	bool BeginRefresh(int ClientState, bool Sixup)
	{
		if(!RequestRefresh(ClientState, Sixup))
			return false;
		m_Ready = false;
		return true;
	}

	bool CompleteLoad()
	{
		if(!m_Requested)
			return false;
		m_Ready = true;
		return true;
	}

	void Reset()
	{
		m_Requested = false;
		m_Ready = false;
	}
};

#endif
