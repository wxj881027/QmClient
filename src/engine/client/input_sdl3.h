#ifndef ENGINE_CLIENT_INPUT_SDL3_H
#define ENGINE_CLIENT_INPUT_SDL3_H

#include <base/str.h>

#include <engine/input.h>
#include <engine/keys.h>

#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// 回调可清空事件或触发新一轮Update；当前事件的值在回调结束前保持有效。
template<typename TConsumer>
void ConsumeSdlInputEvents(const std::vector<IInput::CEvent> &vEvents, const uint32_t &InputCount, TConsumer &&Consumer)
{
	const uint32_t CurrentCount = InputCount;
	const size_t OriginalSize = vEvents.size();
	for(size_t i = 0; i < OriginalSize && i < vEvents.size() && InputCount == CurrentCount; ++i)
	{
		const IInput::CEvent Event = vEvents[i];
		if(Event.m_InputCount == CurrentCount)
			Consumer(Event);
	}
}

// Attach仅在成功时接管引用，失败或异常必须尚未发布；释放回调不得抛出。
template<typename TJoystick, typename TRelease, typename TAttach>
bool AttachSdlJoystick(TJoystick *pJoystick, TRelease Release, TAttach Attach)
{
	std::unique_ptr<TJoystick, TRelease> pOwnedJoystick(pJoystick, Release);
	if(!pOwnedJoystick || !Attach(pOwnedJoystick.get()))
		return false;
	pOwnedJoystick.release();
	return true;
}

inline int SdlCompositionCursor(const char *pText, int CharacterOffset)
{
	int Cursor = 0;
	if(pText != nullptr)
		for(int i = 0; i < CharacterOffset && pText[Cursor] != '\0'; ++i)
		{
			const int Next = str_utf8_forward(pText, Cursor);
			if(Next <= Cursor)
				break;
			Cursor = Next;
		}
	return Cursor;
}

inline bool SdlMouseButtonPressed(uint32_t Buttons, int Index)
{
	return Index > 0 && Index <= std::numeric_limits<uint32_t>::digits &&
	       (Buttons & (uint32_t{1} << (Index - 1))) != 0;
}

// 接管 SDL 剪贴板缓冲；复制失败时仍释放原缓冲，沿用空串失败结果。
template<typename TRelease, typename TCopy>
std::string TakeSdlClipboardText(char *pText, TRelease Release, TCopy Copy) noexcept
{
	std::unique_ptr<char, TRelease> pOwnedText(pText, Release);
	if(!pOwnedText)
		return {};
	try
	{
		return Copy(pOwnedText.get());
	}
	catch(...)
	{
		return {};
	}
}

// SDL3 不再替应用拆分长文本，保留现有事件容量并按完整 UTF-8 字符分块。
inline void AppendSdlTextEvents(std::vector<IInput::CEvent> &vEvents, const char *pText, uint32_t InputCount)
{
	do
	{
		IInput::CEvent Event{};
		Event.m_Key = KEY_UNKNOWN;
		Event.m_Flags = IInput::FLAG_TEXT;
		Event.m_InputCount = InputCount;
		int Bytes = 0;
		while(pText[Bytes])
		{
			const int Next = str_utf8_forward(pText, Bytes);
			if(Next >= static_cast<int>(sizeof(Event.m_aText)))
				break;
			Bytes = Next;
		}
		str_copy(Event.m_aText, pText, Bytes + 1);
		vEvents.emplace_back(Event);
		pText += Bytes;
	} while(*pText);
}

// 配置中的数字键位和键名沿用旧编号，不能直接把 SDL3 扫描码当作游戏键位。
// 使用整数参数拒绝越界编号，避免先构造无效枚举而触发未定义行为。
inline int SdlScancodeToKey(int Scancode)
{
	if(Scancode >= SDL_SCANCODE_UNKNOWN && Scancode <= SDL_SCANCODE_MODE)
		return Scancode;
	if(Scancode >= SDL_SCANCODE_SOFTLEFT && Scancode <= SDL_SCANCODE_ENDCALL)
		return KEY_SOFTLEFT + Scancode - SDL_SCANCODE_SOFTLEFT;
	if(Scancode >= SDL_SCANCODE_AC_SEARCH && Scancode <= SDL_SCANCODE_AC_BOOKMARKS)
		return KEY_AC_SEARCH + Scancode - SDL_SCANCODE_AC_SEARCH;
	switch(Scancode)
	{
	case SDL_SCANCODE_SLEEP: return KEY_SLEEP;
	case SDL_SCANCODE_MEDIA_PLAY:
	case SDL_SCANCODE_MEDIA_PLAY_PAUSE: return KEY_AUDIOPLAY;
	case SDL_SCANCODE_MEDIA_NEXT_TRACK: return KEY_AUDIONEXT;
	case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK: return KEY_AUDIOPREV;
	case SDL_SCANCODE_MEDIA_STOP: return KEY_AUDIOSTOP;
	case SDL_SCANCODE_MEDIA_SELECT: return KEY_MEDIASELECT;
	case SDL_SCANCODE_MEDIA_EJECT: return KEY_EJECT;
	case SDL_SCANCODE_MEDIA_REWIND: return KEY_AUDIOREWIND;
	case SDL_SCANCODE_MEDIA_FAST_FORWARD: return KEY_AUDIOFASTFORWARD;
	default: return KEY_UNKNOWN;
	}
}

#endif // ENGINE_CLIENT_INPUT_SDL3_H
