#ifndef GAME_EDITOR_EDITOR_SERVER_SETTINGS_COMPLETION_H
#define GAME_EDITOR_EDITOR_SERVER_SETTINGS_COMPLETION_H

#include <engine/keys.h>

#include <game/client/lineinput.h>

// 按事件顺序记录修饰键与请求，避免同一帧松开 Ctrl 后丢失补全。
class CQmEditorSettingsCompletion
{
	unsigned m_Modifiers = 0;
	bool m_ModifiersObserved = false;
	CLineInput *m_pRequestedInput = nullptr;
	CLineInput *m_pSpaceInput = nullptr;
	uint32_t m_SpaceInputCount = 0;

public:
	bool OnInput(CLineInput *pLineInput, const IInput &Input, const IInput::CEvent &Event)
	{
		unsigned Modifier = 0;
		switch(Event.m_Key)
		{
		case KEY_LCTRL:
			Modifier = 1;
			break;
		case KEY_RCTRL:
			Modifier = 2;
			break;
		case KEY_LGUI:
			Modifier = 4;
			break;
		case KEY_RGUI:
			Modifier = 8;
			break;
		}
		if(Modifier && (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)))
		{
			m_ModifiersObserved = true;
			if(Event.m_Flags & IInput::FLAG_PRESS)
				m_Modifiers |= Modifier;
			if(Event.m_Flags & IInput::FLAG_RELEASE)
				m_Modifiers &= ~Modifier;
		}

		if(!pLineInput || CLineInput::GetActiveInput() != pLineInput || Input.HasComposition())
		{
			m_pRequestedInput = nullptr;
			m_pSpaceInput = nullptr;
			return false;
		}

		// 文本事件只与已收到的 Space 按下事件关联，不根据帧末键位猜测。
		if(m_pSpaceInput != pLineInput || m_SpaceInputCount != Event.m_InputCount)
			m_pSpaceInput = nullptr;
		if((Event.m_Flags & IInput::FLAG_TEXT) && m_pSpaceInput && str_comp(Event.m_aText, " ") == 0)
		{
			m_pSpaceInput = nullptr;
			return true;
		}

		const bool Modified = m_ModifiersObserved ? m_Modifiers != 0 : Input.ModifierIsPressed();
		if(!(Event.m_Flags & IInput::FLAG_PRESS) || !Modified || Input.AltIsPressed() ||
			Event.m_Key != KEY_SPACE)
			return false;
		if(!(Event.m_Flags & IInput::FLAG_REPEAT))
			m_pRequestedInput = pLineInput;
		m_pSpaceInput = pLineInput;
		m_SpaceInputCount = Event.m_InputCount;
		return true;
	}

	bool Request(CLineInput &LineInput, const IInput &Input, bool Clicked)
	{
		if(m_pRequestedInput != CLineInput::GetActiveInput() || Input.HasComposition())
			m_pRequestedInput = nullptr;
		const bool Requested = m_pRequestedInput == &LineInput;
		if(Requested || Clicked)
			m_pRequestedInput = nullptr;
		if(m_pSpaceInput == &LineInput)
			m_pSpaceInput = nullptr;
		// 窗口失焦可能没有松键事件，在绘制边界同步已释放的修饰键。
		if(!Input.ModifierIsPressed())
		{
			m_Modifiers = 0;
			m_ModifiersObserved = false;
		}
		if(Input.HasComposition() || (!Clicked && !(Requested && LineInput.IsActive())))
			return false;
		LineInput.Activate(EInputPriority::UI);
		return true;
	}
};

#endif
