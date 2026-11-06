#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_QUEUE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_QUEUE_H

#include <algorithm>
#include <iterator>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

template<typename TQueue>
void SettingsAssetQueuePromote(TQueue &Queue, typename TQueue::iterator It) noexcept
{
	using TEntry = typename TQueue::value_type;
	static_assert(std::is_nothrow_move_constructible_v<TEntry> && std::is_nothrow_move_assignable_v<TEntry>);
	if(It != Queue.end())
		std::rotate(Queue.begin(), It, std::next(It));
}

// 仅由主线程操作队列。Start 不得修改队列，接收任务后只能无异常地返回所有者。
// 先准备完整句柄和队列节点；任务被拒绝或准备失败时撤销新节点，保留旧记录。
template<typename TQueue, typename FMakeEntry, typename FMatches, typename FStart>
auto SettingsAssetQueueTryStart(TQueue &Queue, bool HighPriority, FMakeEntry &&MakeEntry, FMatches &&Matches, FStart &&Start, bool PromoteExisting = true) -> decltype(Start())
{
	using TResult = decltype(Start());
	static_assert(std::is_nothrow_move_constructible_v<TResult>);
	try
	{
		auto Entry = MakeEntry();
		auto It = std::find_if(Queue.begin(), Queue.end(), [&](const auto &Queued) { return Matches(Queued, Entry); });
		const bool Inserted = It == Queue.end();
		if(Inserted)
		{
			if(HighPriority)
				Queue.push_front(std::move(Entry));
			else
				Queue.push_back(std::move(Entry));
		}
		auto Rollback = [&] {
			if(Inserted)
			{
				if(HighPriority)
					Queue.pop_front();
				else
					Queue.pop_back();
			}
		};
		try
		{
			auto Result = Start();
			if(!Result)
				Rollback();
			else if(!Inserted && HighPriority && PromoteExisting)
				SettingsAssetQueuePromote(Queue, It);
			return Result;
		}
		catch(...)
		{
			Rollback();
			throw;
		}
	}
	catch(const std::bad_alloc &)
	{
		return {};
	}
	catch(const std::length_error &)
	{
		return {};
	}
}

// 去重标记与节点一起提交，不能留下会阻止后续重试的单独标记。
template<typename TQueue, typename TSet, typename FMakeEntry, typename FMakeKey, typename FMatches, typename FStart>
auto SettingsAssetQueueTryStartKeyed(TQueue &Queue, TSet &Queued, bool HighPriority, FMakeEntry &&MakeEntry, FMakeKey &&MakeKey, FMatches &&Matches, FStart &&Start, bool PromoteExisting = true) -> decltype(Start())
{
	try
	{
		auto Entry = MakeEntry();
		auto [Marker, Inserted] = Queued.insert(MakeKey(Entry));
		try
		{
			auto Result = SettingsAssetQueueTryStart(Queue, HighPriority, [&] { return std::move(Entry); }, Matches, Start, PromoteExisting);
			if(!Result && Inserted)
				Queued.erase(Marker);
			return Result;
		}
		catch(...)
		{
			if(Inserted)
				Queued.erase(Marker);
			throw;
		}
	}
	catch(const std::bad_alloc &)
	{
		return {};
	}
	catch(const std::length_error &)
	{
		return {};
	}
}

// Consume 只能在无异常的交接提交后返回 true，不得重入修改队列或标记。
// 预算不足、准备失败或拒收时保留原队首，下帧继续；成功后只删除现有节点。
template<typename TQueue, typename TSet, typename FMakeKey, typename FConsume>
bool SettingsAssetQueueTryConsumeFront(TQueue &Queue, TSet &Queued, FMakeKey &&MakeKey, FConsume &&Consume)
{
	if(Queue.empty())
		return false;
	try
	{
		auto Marker = Queued.find(MakeKey(Queue.front()));
		if(!Consume(Queue.front()))
			return false;
		if(Marker != Queued.end())
			Queued.erase(Marker);
		Queue.pop_front();
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

template<typename TQueue>
bool SettingsAssetQueueDeferFront(TQueue &Queue)
{
	using TEntry = typename TQueue::value_type;
	static_assert(std::is_nothrow_move_constructible_v<TEntry>);
	if(Queue.size() < 2)
		return true;
	try
	{
		// deque 在分配失败时保留原队首；移动成功后才移除原节点。
		Queue.push_back(std::move(Queue.front()));
		Queue.pop_front();
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

#endif
