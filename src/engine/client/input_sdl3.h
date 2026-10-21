#ifndef ENGINE_CLIENT_INPUT_SDL3_H
#define ENGINE_CLIENT_INPUT_SDL3_H

#include <base/str.h>

#include <engine/input.h>
#include <engine/keys.h>

#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
