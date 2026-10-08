#ifndef GAME_CLIENT_QMUI_QMPOPUPSOURCE_H
#define GAME_CLIENT_QMUI_QMPOPUPSOURCE_H

#include <cstdint>
#include <string_view>

class CQmPopupSourceClock
{
	uint64_t m_Frame = 0;
	uint64_t m_LastPerfFrame = 0;
	bool m_Initialized = false;

public:
	void Update(uint64_t PerfFrame)
	{
		// 主循环可以跳过 UI 绘制，同一主循环也可能多次更新 UI；只计实际 UI 帧。
		if(!m_Initialized || PerfFrame != m_LastPerfFrame)
		{
			m_Initialized = true;
			m_LastPerfFrame = PerfFrame;
			++m_Frame;
		}
	}

	uint64_t Frame() const { return m_Frame; }
};

// 卡片 ID 的存储由卡片目录持有；Context 区分原页面与搜索页等不同容器。
// 只跟踪来源是否仍参与本帧交互，不保存或修改控件的业务状态。
class CQmUiInteractionSource
{
	const void *m_pContext = nullptr;
	const char *m_pCardId = nullptr;
	uint64_t m_Frame = 0;

public:
	void Assign(const void *pContext, const char *pCardId, uint64_t Frame)
	{
		m_pContext = pContext;
		m_pCardId = pCardId;
		m_Frame = Frame;
	}
	const void *Context() const { return m_pContext; }
	const char *CardId() const { return m_pCardId; }
	bool Matches(const void *pContext, const char *pCardId) const
	{
		return m_pContext == pContext && m_pCardId != nullptr && pCardId != nullptr &&
		       std::string_view(m_pCardId) == pCardId;
	}
	void Refresh(const void *pContext, const char *pCardId, uint64_t Frame)
	{
		if(Matches(pContext, pCardId))
			m_Frame = Frame;
	}
	bool Expired(uint64_t Frame, bool AllowPreviousFrame) const
	{
		return m_pCardId != nullptr && Frame > m_Frame && Frame - m_Frame > (AllowPreviousFrame ? 1u : 0u);
	}
};

// 刷新弹窗栈实际持有的来源信息，而非控件中打开弹窗时的属性副本。
// 不重开弹层，不覆盖已选择的条目、几何或入场动画。
template<typename TPopupRange, typename TId>
bool QmRefreshPopupSource(TPopupRange &Popups, const TId *pId, bool RequireRefresh, uint64_t Frame)
{
	for(auto &Popup : Popups)
	{
		if(Popup.m_pId == pId && !Popup.m_Closing)
		{
			Popup.m_Props.m_RequireSourceRefresh = RequireRefresh;
			Popup.m_Props.m_SourceFrame = Frame;
			return true;
		}
	}
	return false;
}

#endif
