#ifndef GAME_CLIENT_QMUI_QMPOPUPSOURCE_H
#define GAME_CLIENT_QMUI_QMPOPUPSOURCE_H

#include <cstdint>

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
