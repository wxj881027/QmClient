#ifndef ENGINE_CLIENT_IOS_SOCKET_RECOVERY_H
#define ENGINE_CLIENT_IOS_SOCKET_RECOVERY_H

// 调用方在断开前保存重连地址。空指针表示只重建 socket，不恢复连接。
template<typename TClient, typename TSockets, typename TQueueDummy>
bool RestoreIosSockets(TClient &Client, TSockets &Sockets, const char *pReconnectAddress, bool ReconnectDummy, TQueueDummy QueueDummy)
{
	Client.DisconnectInternal();
	for(auto &Socket : Sockets)
		Socket.Close();
	if(!Client.ResetSocket())
		return false;

	// 新 socket 需要重新加载 STUN 服务器。
	Client.LoadDDNetInfo();
	if(pReconnectAddress != nullptr)
	{
		Client.ConnectInternal(pReconnectAddress);
		if(ReconnectDummy)
			QueueDummy();
	}
	return true;
}

#endif // ENGINE_CLIENT_IOS_SOCKET_RECOVERY_H
