#ifndef GAME_EDITOR_EDITOR_SERVER_SETTINGS_COMPLETION_H
#define GAME_EDITOR_EDITOR_SERVER_SETTINGS_COMPLETION_H

#include <engine/keys.h>

#include <game/client/lineinput.h>

// 补全请求只管理输入焦点，不假定按键一定生成了空格文本事件。
inline bool QmRequestEditorSettingsCompletion(CLineInput &LineInput, const IInput &Input, bool Clicked)
{
	if(Input.HasComposition())
		return false;
	if(!Clicked && !(LineInput.IsActive() && Input.KeyPress(KEY_SPACE) && Input.ModifierIsPressed()))
		return false;
	LineInput.Activate(EInputPriority::UI);
	return true;
}

// 只吞掉本次补全快捷键及其实际生成的空格文本，其他文本仍交给输入框。
inline bool QmConsumeEditorSettingsCompletionEvent(const CLineInput &LineInput, const IInput &Input, const IInput::CEvent &Event)
{
	if(!LineInput.IsActive() || Input.HasComposition() || !Input.ModifierIsPressed())
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_SPACE)
		return true;
	return (Event.m_Flags & IInput::FLAG_TEXT) && Input.KeyPress(KEY_SPACE) && str_comp(Event.m_aText, " ") == 0;
}

#endif
