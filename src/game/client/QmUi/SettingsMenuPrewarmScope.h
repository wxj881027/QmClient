#ifndef GAME_CLIENT_QMUI_SETTINGSMENUPREWARMSCOPE_H
#define GAME_CLIENT_QMUI_SETTINGSMENUPREWARMSCOPE_H

#include <game/client/ui.h>

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// 测量退出本身仍可抛错，显式收尾避免在异常展开中的析构再次抛出而终止进程。
template<typename FRender>
void RunSettingsTextPlanMeasurement(CUi *pUi, FRender &&Render)
{
	pUi->BeginRenderOnly();
	try
	{
		std::forward<FRender>(Render)();
	}
	catch(...)
	{
		pUi->EndRenderOnly();
		throw;
	}
	pUi->EndRenderOnly();
}

// 预热临时切换的页面、页签和文本上下文，正常或异常退出均恢复。
template<typename T>
class CSettingsTextPlanValueRestore
{
	static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>);
	T &m_Value;
	const T m_Previous;

public:
	explicit CSettingsTextPlanValueRestore(T &Value) noexcept :
		m_Value(Value), m_Previous(Value) {}
	~CSettingsTextPlanValueRestore() noexcept { m_Value = m_Previous; }
	CSettingsTextPlanValueRestore(const CSettingsTextPlanValueRestore &) = delete;
	CSettingsTextPlanValueRestore &operator=(const CSettingsTextPlanValueRestore &) = delete;
};

// 保存待收集项可能分配，必须在发布临时收集上下文前完成。
template<typename TItem>
class CSettingsTextPlanCollectionScope
{
	static_assert(std::is_nothrow_move_assignable_v<TItem> && std::is_nothrow_destructible_v<TItem>);
	bool &m_Collecting;
	std::vector<TItem> *&m_pCollection;
	bool &m_PendingActive;
	TItem &m_PendingItem;
	const bool m_PreviousCollecting;
	std::vector<TItem> *const m_pPreviousCollection;
	const bool m_PreviousPendingActive;
	std::optional<TItem> m_PreviousPendingItem;

public:
	CSettingsTextPlanCollectionScope(bool &Collecting, std::vector<TItem> *&pCollection, bool &PendingActive, TItem &PendingItem, std::vector<TItem> &vItems) :
		m_Collecting(Collecting), m_pCollection(pCollection), m_PendingActive(PendingActive), m_PendingItem(PendingItem), m_PreviousCollecting(Collecting), m_pPreviousCollection(pCollection), m_PreviousPendingActive(PendingActive)
	{
		if(m_PreviousPendingActive)
			m_PreviousPendingItem.emplace(PendingItem);
		m_Collecting = true;
		m_pCollection = &vItems;
		m_PendingActive = false;
	}
	~CSettingsTextPlanCollectionScope() noexcept
	{
		// 移动恢复字符串和颜色分段，内存不足时不再复制分配。
		if(m_PreviousPendingActive)
			m_PendingItem = std::move(*m_PreviousPendingItem);
		m_PendingActive = m_PreviousPendingActive;
		m_pCollection = m_pPreviousCollection;
		m_Collecting = m_PreviousCollecting;
	}
	CSettingsTextPlanCollectionScope(const CSettingsTextPlanCollectionScope &) = delete;
	CSettingsTextPlanCollectionScope &operator=(const CSettingsTextPlanCollectionScope &) = delete;
};

#endif
