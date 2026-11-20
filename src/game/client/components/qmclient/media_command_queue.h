#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_COMMAND_QUEUE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_COMMAND_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 调用方沿用共享媒体状态的锁；命令及批次只拥有值，不借用播放器或 UI。
class CQmMediaCommandQueue
{
public:
	enum class ECommand
	{
		Prev,
		PlayPause,
		Next,
	};
	struct SCommand
	{
		ECommand m_Type;
		std::string m_SourceAppId;
		uint64_t m_Generation;
	};
	using TBatch = std::vector<SCommand>;

	// 只替换真实字符串复制与容量准备，借用对象须覆盖队列使用期。
	class CPreparationOperations
	{
	public:
		virtual ~CPreparationOperations() = default;
		virtual void CopySource(std::string &Source, const char *pSource) { Source = pSource; }
		virtual void Reserve(TBatch &Commands, size_t Count) { Commands.reserve(Count); }
	};

	CQmMediaCommandQueue() = default;
	explicit CQmMediaCommandQueue(CPreparationOperations *pOperations) :
		m_pPreparationOperations(pOperations) {}

	bool TryEnqueue(ECommand Type, const char *pSource, uint64_t Generation) noexcept
	{
		try
		{
			const size_t Maximum = m_Commands.max_size();
			if(m_Commands.size() == Maximum)
				return false;
			CPreparationOperations DefaultOperations;
			auto &Operations = m_pPreparationOperations ? *m_pPreparationOperations : DefaultOperations;
			std::string Source;
			Operations.CopySource(Source, pSource);
			if(m_Commands.size() == m_Commands.capacity())
			{
				const size_t Capacity = m_Commands.capacity();
				const size_t Next = Capacity == 0 ? 1 : Capacity > Maximum - Capacity ? Maximum :
													Capacity * 2;
				Operations.Reserve(m_Commands, Next);
			}
			m_Commands.push_back({Type, std::move(Source), Generation});
			return true;
		}
		catch(...)
		{
			// 只丢弃未接收的新命令；原队列与顺序保持，下次点击可以重新提交。
			return false;
		}
	}

	void Take(TBatch &Commands) noexcept
	{
		// 重用批次也不能把上次已经取走的命令交换回待执行队列。
		Commands.clear();
		Commands.swap(m_Commands);
	}
	void Clear() noexcept { m_Commands.clear(); }
	bool Empty() const { return m_Commands.empty(); }
	size_t Count() const { return m_Commands.size(); }

private:
	TBatch m_Commands;
	CPreparationOperations *m_pPreparationOperations = nullptr;
};

#endif
