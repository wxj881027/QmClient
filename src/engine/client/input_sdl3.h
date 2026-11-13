#ifndef ENGINE_CLIENT_INPUT_SDL3_H
#define ENGINE_CLIENT_INPUT_SDL3_H

#include <base/dbg.h>
#include <base/str.h>

#include <engine/input.h>
#include <engine/keys.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
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

// 一次预留完整输入批次；保留增长余量，避免突发输入退化为逐项扩容。
template<typename TEvents>
void ReserveSdlInputEvents(TEvents &vEvents, size_t Additional)
{
	static_assert(std::is_nothrow_copy_constructible_v<typename TEvents::value_type>);
	const size_t Maximum = vEvents.max_size();
	if(vEvents.size() > Maximum || Additional > Maximum - vEvents.size())
		throw std::length_error("Input event capacity exceeded");
	if(Additional <= vEvents.capacity() - vEvents.size())
		return;
	const size_t Growth = std::min(std::max(vEvents.capacity() / 2, size_t{1}), Maximum - vEvents.capacity());
	vEvents.reserve(std::max(vEvents.size() + Additional, vEvents.capacity() + Growth));
}

struct SSdlKeyEvent
{
	int m_Key;
	int m_Flags;
};

// 队列完整提交后才更新状态；失败时仅回滚本批，已接收事件不重放。
template<typename TEvents>
void AppendSdlKeyEvents(TEvents &vEvents, const SSdlKeyEvent *pKeys, size_t NumKeys, uint32_t InputCount,
	bool (&aCurrent)[KEY_LAST], bool (&aFrame)[KEY_LAST])
{
	for(size_t i = 0; i < NumKeys; ++i)
	{
		const auto &Key = pKeys[i];
		dbg_assert(Key.m_Key >= KEY_FIRST && Key.m_Key < KEY_LAST, "Key invalid: %d", Key.m_Key);
		dbg_assert((Key.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)) != 0 &&
				   (Key.m_Flags & ~(IInput::FLAG_PRESS | IInput::FLAG_RELEASE | IInput::FLAG_REPEAT)) == 0,
			"Key flags invalid");
		dbg_assert((Key.m_Flags & IInput::FLAG_REPEAT) == 0 || (Key.m_Flags & IInput::FLAG_PRESS) != 0, "Key repeat requires press");
	}
	ReserveSdlInputEvents(vEvents, NumKeys);
	const size_t OriginalSize = vEvents.size();
	try
	{
		for(size_t i = 0; i < NumKeys; ++i)
		{
			IInput::CEvent Event{};
			Event.m_Key = pKeys[i].m_Key;
			Event.m_Flags = pKeys[i].m_Flags;
			Event.m_InputCount = InputCount;
			vEvents.emplace_back(Event);
		}
	}
	catch(...)
	{
		vEvents.resize(OriginalSize);
		throw;
	}
	for(size_t i = 0; i < NumKeys; ++i)
	{
		const auto &Key = pKeys[i];
		if(Key.m_Flags & IInput::FLAG_PRESS)
		{
			aCurrent[Key.m_Key] = true;
			aFrame[Key.m_Key] = true;
		}
		if(Key.m_Flags & IInput::FLAG_RELEASE)
			aCurrent[Key.m_Key] = false;
	}
}

// SDL3文本按完整UTF-8分块；同一访问器用于预计算和实际提交，源文本须在期间稳定。
template<typename TEmit>
void VisitSdlTextEvents(const char *pText, uint32_t InputCount, TEmit &&Emit)
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
		Emit(Event);
		pText += Bytes;
	} while(*pText);
}

// 整份提交先预留容量，分配失败不能把长文本前缀留给消费者。
template<typename TEvents>
void AppendSdlTextEvents(TEvents &vEvents, const char *pText, uint32_t InputCount)
{
	const size_t OriginalSize = vEvents.size();
	const size_t Maximum = vEvents.max_size();
	if(OriginalSize > Maximum)
		throw std::length_error("Input event capacity exceeded");
	size_t Additional = 0;
	IInput::CEvent SingleEvent{};
	VisitSdlTextEvents(pText, InputCount, [&](const IInput::CEvent &Event) {
		if(Additional == Maximum - OriginalSize)
			throw std::length_error("Input text event capacity exceeded");
		if(Additional == 0)
			SingleEvent = Event;
		++Additional;
	});
	ReserveSdlInputEvents(vEvents, Additional);
	try
	{
		if(Additional == 1)
			vEvents.emplace_back(SingleEvent);
		else
			VisitSdlTextEvents(pText, InputCount, [&](const IInput::CEvent &Event) { vEvents.emplace_back(Event); });
	}
	catch(...)
	{
		vEvents.resize(OriginalSize);
		throw;
	}
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
