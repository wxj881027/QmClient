#ifndef GAME_CLIENT_QMUI_QMPOPCONFIRM_H
#define GAME_CLIENT_QMUI_QMPOPCONFIRM_H

#include <game/client/ui.h>

#include "QmPopconfirmGeometry.h"

// 气泡确认的上下文。必须比弹层活得久（调用点通常用 static）。
struct SQmPopconfirmContext : public SPopupMenuId
{
	CUi *m_pUI = nullptr;
	char m_aMessage[256] = "";
	char m_aConfirm[64] = "";
	char m_aCancel[64] = "";
	// 危险操作：确认键使用危险色，与普通确认区分。
	bool m_ToneDanger = false;
	// 结果：确认置 true；点外部 / Esc / 再次关闭置 canceled。
	bool m_Confirmed = false;
	bool m_Canceled = false;

	CButtonContainer m_CancelButton;
	CButtonContainer m_ConfirmButton;

	void Reset();
	void DefaultButtons();
};

// 锚定在触发控件旁的轻确认：无全屏遮罩、不锁页面滚动、不抢焦点，
// 点外部或 Esc 即取消。用于单条、可恢复、代价低的动作（下载、单条删除）。
// 不可逆或跨范围的操作仍应使用 CUi::ShowPopupConfirm。
void QmShowPopconfirm(CUi *pUi, SQmPopconfirmContext *pContext, const CUIRect &Anchor);

#endif // GAME_CLIENT_QMUI_QMPOPCONFIRM_H
