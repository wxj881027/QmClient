#ifndef GAME_CLIENT_QMUI_QMUILIFETIME_H
#define GAME_CLIENT_QMUI_QMUILIFETIME_H

#include <memory>

// UI 线程上的借用标记：不延长元素寿命，仅在首次排队时分配。
class CQmUiLifetime
{
	std::shared_ptr<const char> m_pToken;

public:
	class CWeakRef
	{
		friend class CQmUiLifetime;
		std::weak_ptr<const char> m_pToken;

		explicit CWeakRef(const std::shared_ptr<const char> &pToken) :
			m_pToken(pToken)
		{
		}

	public:
		CWeakRef() = default;
		bool IsAlive() const { return !m_pToken.expired(); }
	};

	CQmUiLifetime() = default;
	CQmUiLifetime(const CQmUiLifetime &) = delete;
	CQmUiLifetime &operator=(const CQmUiLifetime &) = delete;
	CQmUiLifetime(CQmUiLifetime &&) = delete;
	CQmUiLifetime &operator=(CQmUiLifetime &&) = delete;

	CWeakRef WeakRef()
	{
		if(m_pToken == nullptr)
			m_pToken = std::make_shared<const char>(0);
		return CWeakRef(m_pToken);
	}
};

#endif
