#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_TRANSLATE_BUTTON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_TRANSLATE_BUTTON_H

#include <engine/input.h>
#include <engine/keys.h>

// 左键只打开设置，右键只切换自动翻译；配对按下/松开，避免拖出或弹层切换后误触。
class CQmChatTranslateButton
{
	bool m_LeftPressed = false;

public:
	enum class EAction
	{
		NONE,
		CONSUME,
		OPEN_SETTINGS,
		TOGGLE_AUTO
	};
	void Reset() { m_LeftPressed = false; }
	EAction Update(int Key, int Flags, bool Inside, bool Available)
	{
		if(!Available)
		{
			Reset();
			return EAction::NONE;
		}
		if(Key == KEY_MOUSE_1)
		{
			if(Flags & IInput::FLAG_PRESS)
			{
				m_LeftPressed = Inside;
				return Inside ? EAction::CONSUME : EAction::NONE;
			}
			if(Flags & IInput::FLAG_RELEASE)
			{
				const bool Pressed = m_LeftPressed;
				Reset();
				return Pressed ? (Inside ? EAction::OPEN_SETTINGS : EAction::CONSUME) : EAction::NONE;
			}
		}
		if(Key == KEY_MOUSE_2 && (Flags & IInput::FLAG_PRESS) && Inside)
			return EAction::TOGGLE_AUTO;
		return EAction::NONE;
	}
};

#endif
