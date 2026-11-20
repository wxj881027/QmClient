#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_PRESENCE_PREPARE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_PRESENCE_PREPARE_H

#include <new>
#include <type_traits>
#include <utility>

// 解析失败返回空结果；候选完整准备后才移交，分配失败不暴露半份名单。
template<typename TResult, typename TPrepare>
bool TryPrepareQmPresenceResult(TResult &Out, TPrepare &&Prepare)
{
	static_assert(std::is_nothrow_default_constructible_v<TResult>);
	static_assert(std::is_nothrow_move_assignable_v<TResult>);
	Out = {};
	try
	{
		TResult Candidate;
		if(!std::forward<TPrepare>(Prepare)(Candidate))
			return false;
		Out = std::move(Candidate);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}

#endif
