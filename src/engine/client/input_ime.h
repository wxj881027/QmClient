#ifndef ENGINE_CLIENT_INPUT_IME_H
#define ENGINE_CLIENT_INPUT_IME_H

#include <engine/shared/qm_ime_policy.h>

#include <memory>
#include <optional>

// 原生消息与 SDL 通知成对入队；失败时不留下没有通知的消息。
template<typename TQueue, typename TNotify>
bool QueueNativeImeMessage(TQueue &Queue, const typename TQueue::value_type &Message, TNotify Notify) noexcept
{
	try
	{
		Queue.push_back(Message);
	}
	catch(...)
	{
		return false;
	}
	try
	{
		if(Notify())
			return true;
	}
	catch(...)
	{
	}
	Queue.pop_back();
	return false;
}

// 先接管上下文再读取；读取回调处理空上下文，释放回调不得抛出异常。
template<typename TContext, typename TRelease, typename TRead>
std::optional<SQmImeCandidateList> ReadNativeImeCandidates(TContext *pContext, TRelease Release, TRead Read) noexcept
{
	std::unique_ptr<TContext, TRelease> pOwnedContext(pContext, Release);
	try
	{
		return Read(pOwnedContext.get());
	}
	catch(...)
	{
		return std::nullopt;
	}
}

#endif // ENGINE_CLIENT_INPUT_IME_H
