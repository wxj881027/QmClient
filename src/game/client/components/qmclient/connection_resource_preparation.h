#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H

#include <new>
#include <stdexcept>
#include <utility>

// 连接的图形资源准备完成后只放行一次；断开或重新开始时撤销旧连接的放行资格。
class CConnectionResourcePreparation
{
	enum class EState
	{
		IDLE,
		PENDING,
		FAILED,
	};
	EState m_State = EState::IDLE;

public:
	void Begin()
	{
		if(!Failed())
			m_State = EState::PENDING;
	}
	void Cancel() { m_State = EState::IDLE; }
	bool Pending() const { return m_State == EState::PENDING; }
	bool Failed() const { return m_State == EState::FAILED; }
	bool NeedsCleanup() const { return m_State != EState::IDLE; }

	template<typename TPrepare>
	bool TryPrepare(TPrepare &&Prepare, bool RecoverFailure = true)
	{
		if(Failed())
			return false;
		if(!RecoverFailure)
		{
			std::forward<TPrepare>(Prepare)();
			return true;
		}
		try
		{
			std::forward<TPrepare>(Prepare)();
			return true;
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
		// 即使在 Begin 之前失败，也保留部分资源的清理资格。
		m_State = EState::FAILED;
		return false;
	}

	bool CompleteWhenReady(bool MapDataReady, bool BackgroundReady, bool ForegroundReady)
	{
		if(!Pending() || !MapDataReady || !BackgroundReady || !ForegroundReady)
			return false;
		m_State = EState::IDLE;
		return true;
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H
