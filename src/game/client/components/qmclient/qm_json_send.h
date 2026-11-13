#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_JSON_SEND_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_JSON_SEND_H

#include <engine/shared/jsonwriter.h>

#include <new>
#include <string>

// 消息完整序列化后才交给发送队列；准备失败不发送半份内容。
// Send 沿传输层合同自行返回失败，不能在接受消息后再抛出异常。
template<typename TWrite, typename TSend>
bool TrySendQmJsonMessage(TWrite &&Write, TSend &&Send)
{
	std::string Body;
	try
	{
		CJsonStringWriter Writer;
		Write(Writer);
		Body = Writer.GetOutputString();
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	return Send(Body.data(), Body.size());
}

#endif
