#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_PRESENCE_SEND_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_PRESENCE_SEND_H

#include <new>
#include <string>
#include <utility>

enum class EQmPresenceSendResult
{
	FAILED,
	UNCHANGED,
	SENT,
};

// 先准备线上消息及握手后的常规缓存；发送拒收或准备失败均保留旧缓存。
// Send 负责将自身失败转为 false；成功后仅移动已备好的字符串，不再分配。
template<typename TBuild, typename TSend>
EQmPresenceSendResult TrySendQmPresence(std::string &LastBody, bool Hello, bool ForceSend, TBuild &&Build, TSend &&Send)
{
	std::string Body;
	std::string PresenceBody;
	try
	{
		Body = Build(Hello);
		if(!Hello && !ForceSend && Body == LastBody)
			return EQmPresenceSendResult::UNCHANGED;
		if(Hello)
			PresenceBody = Build(false);
	}
	catch(const std::bad_alloc &)
	{
		return EQmPresenceSendResult::FAILED;
	}
	if(!Send(Body.data(), Body.size()))
		return EQmPresenceSendResult::FAILED;
	LastBody = Hello ? std::move(PresenceBody) : std::move(Body);
	return EQmPresenceSendResult::SENT;
}

#endif
