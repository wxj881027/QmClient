#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PENDING_CHAT_MESSAGES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PENDING_CHAT_MESSAGES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// 待发消息独立持有正文与本地原文，不能从输入历史倒推发送队列。
class CPendingChatMessages
{
public:
	struct SMessage
	{
		int m_Team = 0;
		int m_Connection = -1;
		int m_ClientId = -1;
		std::string m_Text;
		std::string m_Original;
	};

	// 前三项顺序与 CChat::EChatSendResult 一致。
	enum class EAdmission
	{
		REJECTED,
		SENT,
		QUEUED,
	};

	static constexpr size_t CAPACITY = 3;

private:
	// 容量固定为三条，接管已准备正文和弹出队首都不再分配容器节点。
	std::array<SMessage, CAPACITY> m_aMessages{};
	size_t m_Size = 0;
	int64_t m_LastObservedTime = -1;

	static bool ValidTime(int64_t LastSend, int64_t Now, int64_t Frequency)
	{
		return LastSend >= 0 && Now >= LastSend && Frequency > 0;
	}

	static bool Ready(int64_t LastSend, int64_t Now, int64_t Frequency)
	{
		// 非负且有序的时间先作差，避免 LastSend + Frequency 溢出。
		return Now - LastSend > Frequency;
	}

	bool ObserveTime(int64_t LastSend, int64_t Now, int64_t Frequency)
	{
		if(!ValidTime(LastSend, Now, Frequency) || Now < m_LastObservedTime)
		{
			// 时钟异常不能补发旧消息；清空后允许后续正常调用重新接纳。
			Clear();
			return false;
		}
		m_LastObservedTime = Now;
		return true;
	}

public:
	// 无实例的额度入口共用接纳策略；跨调用时钟回退由实例入口检测。
	static EAdmission Reserve(int64_t LastSend, int64_t Now, int64_t Frequency, int &PendingCount)
	{
		if(!ValidTime(LastSend, Now, Frequency) || PendingCount < 0 || PendingCount > static_cast<int>(CAPACITY))
			return EAdmission::REJECTED;
		if(PendingCount == 0 && Ready(LastSend, Now, Frequency))
			return EAdmission::SENT;
		if(PendingCount == static_cast<int>(CAPACITY))
			return EAdmission::REJECTED;
		++PendingCount;
		return EAdmission::QUEUED;
	}

	// 立即发送失败也保留消息，不能把底层拒绝入队当作发送成功。
	template<typename TSend>
	EAdmission Admit(int64_t LastSend, int64_t Now, int64_t Frequency, SMessage Message, TSend &&Send)
	{
		return PrepareAndAdmit(LastSend, Now, Frequency, [&] { return std::move(Message); }, std::forward<TSend>(Send));
	}

	// 准备与发送回调仅同步借用且不能重入本队列；只回退准备异常。
	template<typename TPrepare, typename TSend>
	EAdmission PrepareAndAdmit(int64_t LastSend, int64_t Now, int64_t Frequency, TPrepare &&Prepare, TSend &&Send)
	{
		if(!ObserveTime(LastSend, Now, Frequency))
			return EAdmission::REJECTED;
		int PendingCount = static_cast<int>(m_Size);
		EAdmission Admission = Reserve(LastSend, Now, Frequency, PendingCount);
		if(Admission == EAdmission::REJECTED)
			return Admission;
		std::optional<SMessage> Message;
		try
		{
			Message.emplace(Prepare());
		}
		catch(...)
		{
			return EAdmission::REJECTED;
		}
		// 发送可能已有外部副作用，不能把它的异常伪装成正文准备失败。
		if(Admission == EAdmission::SENT && !Send(std::as_const(*Message)))
			Admission = EAdmission::QUEUED;
		if(Admission == EAdmission::QUEUED)
			m_aMessages[m_Size++] = std::move(*Message);
		return Admission;
	}

	// 回调确认已发送或应丢弃后才移除；失败保留队首，调用者仅在发送成功时更新 LastSend。
	template<typename TProcess>
	bool ProcessReady(int64_t LastSend, int64_t Now, int64_t Frequency, TProcess &&Process)
	{
		if(!ObserveTime(LastSend, Now, Frequency) || m_Size == 0 || !Ready(LastSend, Now, Frequency))
			return false;
		if(!Process(std::as_const(m_aMessages[0])))
			return false;
		for(size_t Index = 1; Index < m_Size; ++Index)
			m_aMessages[Index - 1] = std::move(m_aMessages[Index]);
		m_aMessages[--m_Size] = {};
		return true;
	}

	void Clear()
	{
		for(size_t Index = 0; Index < m_Size; ++Index)
			m_aMessages[Index] = {};
		m_Size = 0;
		m_LastObservedTime = -1;
	}

	size_t Size() const { return m_Size; }

	void InvalidateConnection(int Connection)
	{
		size_t Write = 0;
		for(size_t Read = 0; Read < m_Size; ++Read)
		{
			if(m_aMessages[Read].m_Connection == Connection)
				continue;
			if(Write != Read)
				m_aMessages[Write] = std::move(m_aMessages[Read]);
			++Write;
		}
		for(size_t Index = Write; Index < m_Size; ++Index)
			m_aMessages[Index] = {};
		m_Size = Write;
	}
};

#endif
