#ifndef GAME_CLIENT_QMUI_QMPOPUPPOINTER_H
#define GAME_CLIENT_QMUI_QMPOPUPPOINTER_H

// 公共鼠标读取同时屏蔽当前与上一帧，避免底层误认释放并提交拖放。
struct SQmPointerButtons
{
	unsigned m_Current = 0;
	unsigned m_Previous = 0;
	int Held(int Index) const { return (m_Current >> Index) & 1; }
	int Previous(int Index) const { return (m_Previous >> Index) & 1; }
	int Pressed(int Index) const { return Held(Index) && !Previous(Index); }
};

inline SQmPointerButtons QmResolvePointerButtons(unsigned Current, unsigned Previous, bool Blocked)
{
	return Blocked ? SQmPointerButtons{} : SQmPointerButtons{Current, Previous};
}

inline int QmButtonCurrentPress(const SQmPointerButtons &Buttons, unsigned ButtonMask, bool Inside)
{
	if(Inside)
	{
		for(int Button = 0; Button < 3; ++Button)
		{
			if((ButtonMask & (1u << Button)) && Buttons.Pressed(Button))
				return Button;
		}
	}
	return -1;
}

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
	bool m_InsideGroup = true;
};

enum class EQmPopupPointerAction
{
	NONE,
	CAPTURE,
	RELEASE,
	CLOSE,
	CLOSE_GROUP,
};

inline EQmPopupPointerAction QmResolvePopupPointerAction(const SQmPopupPointerInput &Input)
{
	if(!Input.m_Active)
		return EQmPopupPointerAction::NONE;
	const auto Close = Input.m_InsideGroup ? EQmPopupPointerAction::CLOSE : EQmPopupPointerAction::CLOSE_GROUP;
	if(Input.m_BlockUnderlying && Input.m_Pressed && !Input.m_Inside)
		return Close;
	if(Input.m_Captured)
	{
		if(!Input.m_Held)
			return Input.m_Inside ? EQmPopupPointerAction::RELEASE : Close;
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
