#ifndef GAME_CLIENT_QMUI_QMPOPUPPOINTER_H
#define GAME_CLIENT_QMUI_QMPOPUPPOINTER_H

// 将弹层指针决策与绘制分离，活动层独占输入，关闭后仍由调用方配对清理输入深度。
struct SQmPopupPointerInput
{
	bool m_Active = false;
	bool m_BlockUnderlying = false;
	bool m_Inside = false;
	bool m_Pressed = false;
	bool m_Held = false;
	bool m_Captured = false;
	bool m_Hot = false;
};

enum class EQmPopupPointerAction
{
	NONE,
	CAPTURE,
	RELEASE,
	CLOSE,
};

inline EQmPopupPointerAction QmResolvePopupPointerAction(const SQmPopupPointerInput &Input)
{
	if(!Input.m_Active)
		return EQmPopupPointerAction::NONE;
	if(Input.m_BlockUnderlying && Input.m_Pressed && !Input.m_Inside)
		return EQmPopupPointerAction::CLOSE;
	if(Input.m_Captured)
	{
		if(!Input.m_Held)
			return Input.m_Inside ? EQmPopupPointerAction::RELEASE : EQmPopupPointerAction::CLOSE;
	}
	else if(Input.m_Hot && Input.m_Held)
		return EQmPopupPointerAction::CAPTURE;
	return EQmPopupPointerAction::NONE;
}

// 即使回调提前返回，输入深度也必须释放；显式 Release 允许关闭回调在屏蔽解除后运行。
class CQmPopupInputScope
{
	int *m_pDepth = nullptr;

public:
	CQmPopupInputScope(int &Depth, bool Enabled)
	{
		if(Enabled)
		{
			m_pDepth = &Depth;
			++Depth;
		}
	}
	CQmPopupInputScope(const CQmPopupInputScope &) = delete;
	CQmPopupInputScope &operator=(const CQmPopupInputScope &) = delete;
	~CQmPopupInputScope() { Release(); }

	void Release()
	{
		if(m_pDepth != nullptr)
		{
			--*m_pDepth;
			m_pDepth = nullptr;
		}
	}
};

#endif // GAME_CLIENT_QMUI_QMPOPUPPOINTER_H
