/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "input.h"

#include "input_ime.h"
#include "input_sdl3.h"
#include "keynames.h"

#include <base/perf_timer.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/qm_ime_policy.h>

#include <game/client/components/qmclient/perf_logging.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
// windows.h must be included before imm.h, but clang-format requires includes to be sorted alphabetically, hence this comment.
#include <imm.h>
#endif

namespace
{
	void LogPerfStage(IClient *pClient, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/input", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

void CInput::AddKeyEvent(int Key, int Flags)
{
	const SSdlKeyEvent Request{Key, Flags};
	AppendSdlKeyEvents(m_vInputEvents, &Request, 1, m_InputCounter, m_aCurrentKeyStates, m_aFrameKeyStates);
}

void CInput::AddTextEvent(const char *pText)
{
	AppendSdlTextEvents(m_vInputEvents, pText, m_InputCounter);
}

CInput::CInput()
{
	std::fill(std::begin(m_aCurrentKeyStates), std::end(m_aCurrentKeyStates), false);
	std::fill(std::begin(m_aFrameKeyStates), std::end(m_aFrameKeyStates), false);
	std::fill(std::begin(m_aGlobalKeyStates), std::end(m_aGlobalKeyStates), false);

	m_vInputEvents.reserve(32);
	m_LastUpdate = 0;
	m_UpdateTime = 0.0f;

	m_InputCounter = 1;
	m_InputGrabbed = false;
	m_GlobalMousePos = vec2(0.0f, 0.0f);
	m_GlobalMouseDelta = vec2(0.0f, 0.0f);
	m_HasGlobalMousePos = false;

	m_MouseFocus = true;

	m_CompositionCursor = 0;

	m_aDropFile[0] = '\0';
}

void CInput::Init()
{
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pClient = Kernel()->RequestInterface<IClient>();
#if defined(CONF_FAMILY_WINDOWS)
	m_WindowsMessageEvent = SDL_RegisterEvents(1);
#endif

	SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, QmImeShouldUseSystemCandidateUi() ? "none" : "composition,candidates");
	StopTextInput();
	if(Window() != nullptr)
		MouseModeRelative();

	InitJoysticks();
}

SDL_Window *CInput::Window() const
{
	// IME 管理器在 OnConsoleInit 阶段会关闭输入，早于输入模块取得图形接口。
	return Graphics() == nullptr ? nullptr : SDL_GetWindowFromID(Graphics()->GetWindowId());
}

void CInput::Shutdown()
{
	StopTextInput();
	CloseJoysticks();
}

void CInput::InitJoysticks()
{
	if(!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
	{
		dbg_msg("joystick", "Unable to init SDL joystick system: %s", SDL_GetError());
		return;
	}
	m_JoystickInitialized = true;
	int NumJoysticks = 0;
	std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> pIds(SDL_GetJoysticks(&NumJoysticks), SDL_free);
	dbg_msg("joystick", "%d joystick(s) found", NumJoysticks);
	for(int i = 0; pIds != nullptr && i < NumJoysticks; ++i)
		OpenJoystick(pIds.get()[i]);
	UpdateActiveJoystick();

	Console()->Chain("inp_controller_guid", ConchainJoystickGuidChanged, this);
}

bool CInput::OpenJoystick(int JoystickIndex)
{
	SDL_Joystick *pJoystick = SDL_OpenJoystick(JoystickIndex);
	if(!pJoystick)
	{
		dbg_msg("joystick", "Could not open joystick %d: '%s'", JoystickIndex, SDL_GetError());
		return false;
	}
	const bool Added = AttachSdlJoystick(pJoystick, SDL_CloseJoystick, [&](SDL_Joystick *pOpened) {
		if(std::find_if(m_vJoysticks.begin(), m_vJoysticks.end(), [pOpened](const CJoystick &Joystick) { return Joystick.m_pDelegate == pOpened; }) != m_vJoysticks.end())
			return false;
		m_vJoysticks.emplace_back(this, m_vJoysticks.size(), pOpened);
		return true;
	});
	if(!Added)
		return false;
	// vector扩容后先恢复借用指针，再执行可能失败的诊断。
	UpdateActiveJoystick();
	const CJoystick &Joystick = m_vJoysticks[m_vJoysticks.size() - 1];
	dbg_msg("joystick", "Opened joystick %d '%s' (%d axes, %d buttons, %d balls, %d hats)", JoystickIndex, Joystick.GetName(),
		Joystick.GetNumAxes(), Joystick.GetNumButtons(), Joystick.GetNumBalls(), Joystick.GetNumHats());
	return true;
}

void CInput::UpdateActiveJoystick()
{
	m_pActiveJoystick = nullptr;
	if(m_vJoysticks.empty())
		return;
	for(auto &Joystick : m_vJoysticks)
	{
		if(str_comp(Joystick.GetGUID(), g_Config.m_InpControllerGUID) == 0)
		{
			m_pActiveJoystick = &Joystick;
			return;
		}
	}
	// Fall back to first joystick if no match was found
	if(!m_pActiveJoystick)
		m_pActiveJoystick = &m_vJoysticks.front();
}

void CInput::ConchainJoystickGuidChanged(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		static_cast<CInput *>(pUserData)->UpdateActiveJoystick();
	}
}

float CInput::GetJoystickDeadzone()
{
	return minimum(g_Config.m_InpControllerTolerance / 50.0f, 0.995f);
}

CInput::CJoystick::CJoystick(CInput *pInput, int Index, SDL_Joystick *pDelegate)
{
	m_pInput = pInput;
	m_Index = Index;
	m_pDelegate = pDelegate;
	m_NumAxes = SDL_GetNumJoystickAxes(pDelegate);
	m_NumButtons = SDL_GetNumJoystickButtons(pDelegate);
	m_NumBalls = SDL_GetNumJoystickBalls(pDelegate);
	m_NumHats = SDL_GetNumJoystickHats(pDelegate);
	const char *pName = SDL_GetJoystickName(pDelegate);
	str_copy(m_aName, pName != nullptr ? pName : "");
	SDL_GUIDToString(SDL_GetJoystickGUID(pDelegate), m_aGUID, sizeof(m_aGUID));
	m_InstanceId = SDL_GetJoystickID(pDelegate);
}

void CInput::CloseJoysticks()
{
	m_pActiveJoystick = nullptr;
	for(auto &Joystick : m_vJoysticks)
		SDL_CloseJoystick(Joystick.m_pDelegate);
	m_vJoysticks.clear();
	if(m_JoystickInitialized)
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	m_JoystickInitialized = false;
}

void CInput::SetActiveJoystick(size_t Index)
{
	m_pActiveJoystick = &m_vJoysticks[Index];
	str_copy(g_Config.m_InpControllerGUID, m_pActiveJoystick->GetGUID());
}

float CInput::CJoystick::GetAxisValue(int Axis)
{
	return (SDL_GetJoystickAxis(m_pDelegate, Axis) - SDL_JOYSTICK_AXIS_MIN) / (float)(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN) * 2.0f - 1.0f;
}

void CInput::CJoystick::GetJoystickHatKeys(int Hat, int HatValue, int (&aHatKeys)[2])
{
	if(HatValue & SDL_HAT_UP)
		aHatKeys[0] = KEY_JOY_HAT0_UP + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else if(HatValue & SDL_HAT_DOWN)
		aHatKeys[0] = KEY_JOY_HAT0_DOWN + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else
		aHatKeys[0] = KEY_UNKNOWN;

	if(HatValue & SDL_HAT_LEFT)
		aHatKeys[1] = KEY_JOY_HAT0_LEFT + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else if(HatValue & SDL_HAT_RIGHT)
		aHatKeys[1] = KEY_JOY_HAT0_RIGHT + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else
		aHatKeys[1] = KEY_UNKNOWN;
}

void CInput::CJoystick::GetHatValue(int Hat, int (&aHatKeys)[2])
{
	GetJoystickHatKeys(Hat, SDL_GetJoystickHat(m_pDelegate, Hat), aHatKeys);
}

bool CInput::CJoystick::Relative(float *pX, float *pY)
{
	if(!Input()->m_MouseFocus || !Input()->m_InputGrabbed || !g_Config.m_InpControllerEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(g_Config.m_InpControllerX), GetAxisValue(g_Config.m_InpControllerY));
	const float Len = length(RawJoystickPos);
	const float DeadZone = Input()->GetJoystickDeadzone();
	if(Len > DeadZone)
	{
		const float Factor = 2500.0f * Input()->GetUpdateTime() * maximum((Len - DeadZone) / (1.0f - DeadZone), 0.001f) / Len;
		*pX = RawJoystickPos.x * Factor;
		*pY = RawJoystickPos.y * Factor;
		return true;
	}
	return false;
}

bool CInput::CJoystick::Absolute(float *pX, float *pY)
{
	if(!Input()->m_MouseFocus || !Input()->m_InputGrabbed || !g_Config.m_InpControllerEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(g_Config.m_InpControllerX), GetAxisValue(g_Config.m_InpControllerY));
	const float DeadZone = Input()->GetJoystickDeadzone();
	if(dot(RawJoystickPos, RawJoystickPos) > DeadZone * DeadZone)
	{
		*pX = RawJoystickPos.x;
		*pY = RawJoystickPos.y;
		return true;
	}
	return false;
}

bool CInput::MouseRelative(float *pX, float *pY)
{
	if(!m_MouseFocus || !m_InputGrabbed)
		return false;

	vec2 Relative;
	SDL_GetRelativeMouseState(&Relative.x, &Relative.y);

	*pX = Relative.x;
	*pY = Relative.y;
	return *pX != 0.0f || *pY != 0.0f;
}

void CInput::MouseModeAbsolute()
{
	m_InputGrabbed = false;
	if(Window() != nullptr)
		SDL_SetWindowRelativeMouseMode(Window(), false);
	Graphics()->SetWindowGrab(false);
}

void CInput::MouseModeRelative()
{
	m_InputGrabbed = true;
	if(Window() != nullptr)
		SDL_SetWindowRelativeMouseMode(Window(), true);
	Graphics()->SetWindowGrab(true);
	// Clear pending relative mouse motion
	SDL_GetRelativeMouseState(nullptr, nullptr);
}

vec2 CInput::NativeMousePos() const
{
	vec2 Position;
	SDL_GetMouseState(&Position.x, &Position.y);
	return vec2(Position.x, Position.y);
}

bool CInput::NativeMousePressed(int Index) const
{
	const uint32_t Buttons = SDL_GetMouseState(nullptr, nullptr);
	return SdlMouseButtonPressed(Buttons, Index);
}

bool CInput::GamepadButtonIsPressed(int Button) const
{
	if(Button < 0 || Button >= NUM_JOYSTICK_BUTTONS || m_pActiveJoystick == nullptr)
		return false;
	return m_aCurrentKeyStates[KEY_JOYSTICK_BUTTON_0 + Button];
}

float CInput::GamepadAxisValue(int Axis) const
{
	if(Axis < 0 || Axis >= NUM_JOYSTICK_AXES || m_pActiveJoystick == nullptr)
		return 0.0f;
	return std::clamp(m_pActiveJoystick->GetAxisValue(Axis), -1.0f, 1.0f);
}

int CInput::GamepadPlayerIndex() const
{
	return m_pActiveJoystick == nullptr ? 0 : std::clamp(m_pActiveJoystick->GetIndex(), 0, 2);
}

vec2 CInput::GlobalMousePos() const
{
	return m_GlobalMousePos;
}

bool CInput::GlobalMousePressed(int Index) const
{
#if SDL_VERSION_ATLEAST(2, 0, 4)
	const Uint32 Buttons = SDL_GetGlobalMouseState(nullptr, nullptr);
	return SdlMouseButtonPressed(Buttons, Index);
#else
	return NativeMousePressed(Index);
#endif
}

vec2 CInput::GlobalMouseDelta() const
{
	return m_GlobalMouseDelta;
}

const std::vector<IInput::CTouchFingerState> &CInput::TouchFingerStates() const
{
	return m_vTouchFingerStates;
}

void CInput::ClearTouchDeltas()
{
	for(CTouchFingerState &TouchFingerState : m_vTouchFingerStates)
	{
		TouchFingerState.m_Delta = vec2(0.0f, 0.0f);
	}
}

std::string CInput::GetClipboardText()
{
	return TakeSdlClipboardText(SDL_GetClipboardText(), SDL_free,
		[](const char *pText) { return std::string(pText); });
}

void CInput::SetClipboardText(const char *pText)
{
	SDL_SetClipboardText(pText);
}

void CInput::ClearImeCandidates()
{
	m_ImeCandidates.Clear();
}

void CInput::ClearImeState()
{
	m_CompositionString = "";
	m_CompositionCursor = 0;
	ClearImeCandidates();
}

void CInput::StartTextInput()
{
#if defined(CONF_FAMILY_WINDOWS)
	SDL_SetWindowsMessageHook(WindowsMessageHook, this);
#endif
	if(Window() != nullptr)
		SDL_StartTextInput(Window());
	m_ImeCandidates.ResumeComposition();
}

void CInput::StopTextInput()
{
#if defined(CONF_FAMILY_WINDOWS)
	SDL_SetWindowsMessageHook(nullptr, nullptr);
	// 清掉旧标记，避免重新启用后由旧事件提前消费新的 IME 通知。
	if(m_WindowsMessageEvent != 0)
		SDL_FlushEvent(m_WindowsMessageEvent);
	m_QueuedWindowsMessages.clear();
#endif
	if(Window() != nullptr)
		SDL_StopTextInput(Window());
	ClearImeState();
}

void CInput::EnsureScreenKeyboardShown()
{
	if(Window() == nullptr || !SDL_HasScreenKeyboardSupport() ||
		Graphics()->IsScreenKeyboardShown())
	{
		return;
	}
	SDL_StopTextInput(Window());
	SDL_StartTextInput(Window());
}

void CInput::ConsumeEvents(std::function<void(const CEvent &Event)> Consumer) const
{
	ConsumeSdlInputEvents(m_vInputEvents, m_InputCounter, Consumer);
}

void CInput::Clear()
{
	std::fill(std::begin(m_aFrameKeyStates), std::end(m_aFrameKeyStates), false);
	m_vInputEvents.clear();
	ClearTouchDeltas();
}

float CInput::GetUpdateTime() const
{
	return m_UpdateTime;
}

bool CInput::KeyIsPressed(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return m_aCurrentKeyStates[Key];
}

bool CInput::GlobalKeyIsPressed(int Key) const
{
	if(Key < KEY_FIRST || Key >= KEY_LAST)
		return false;
	return m_aGlobalKeyStates[Key];
}

bool CInput::KeyPress(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return m_aFrameKeyStates[Key];
}

const char *CInput::KeyName(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return g_aaKeyStrings[Key];
}

int CInput::FindKeyByName(const char *pKeyName) const
{
	// check for numeric
	if(pKeyName[0] == '&')
	{
		int Key = str_toint(pKeyName + 1);
		if(Key > KEY_FIRST && Key < KEY_LAST)
			return Key; // numeric
	}

	// search for key
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
	{
		if(str_comp_nocase(pKeyName, KeyName(Key)) == 0)
			return Key;
	}

	return KEY_UNKNOWN;
}

void CInput::HandleJoystickAxisMotionEvent(const SDL_JoyAxisEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.axis >= NUM_JOYSTICK_AXES)
		return;

	const int LeftKey = KEY_JOY_AXIS_0_LEFT + 2 * Event.axis;
	const int RightKey = LeftKey + 1;
	const float DeadZone = GetJoystickDeadzone();
	std::array<SSdlKeyEvent, 2> aKeys;
	size_t NumKeys = 0;

	if(Event.value <= SDL_JOYSTICK_AXIS_MIN * DeadZone && !m_aCurrentKeyStates[LeftKey])
	{
		aKeys[NumKeys++] = {LeftKey, IInput::FLAG_PRESS};
	}
	else if(Event.value > SDL_JOYSTICK_AXIS_MIN * DeadZone && m_aCurrentKeyStates[LeftKey])
	{
		aKeys[NumKeys++] = {LeftKey, IInput::FLAG_RELEASE};
	}

	if(Event.value >= SDL_JOYSTICK_AXIS_MAX * DeadZone && !m_aCurrentKeyStates[RightKey])
	{
		aKeys[NumKeys++] = {RightKey, IInput::FLAG_PRESS};
	}
	else if(Event.value < SDL_JOYSTICK_AXIS_MAX * DeadZone && m_aCurrentKeyStates[RightKey])
	{
		aKeys[NumKeys++] = {RightKey, IInput::FLAG_RELEASE};
	}
	AppendSdlKeyEvents(m_vInputEvents, aKeys.data(), NumKeys, m_InputCounter, m_aCurrentKeyStates, m_aFrameKeyStates);
}

void CInput::HandleJoystickButtonEvent(const SDL_JoyButtonEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.button >= NUM_JOYSTICK_BUTTONS)
		return;

	const int Key = Event.button + KEY_JOYSTICK_BUTTON_0;

	if(Event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN)
	{
		AddKeyEvent(Key, IInput::FLAG_PRESS);
	}
	else if(Event.type == SDL_EVENT_JOYSTICK_BUTTON_UP)
	{
		AddKeyEvent(Key, IInput::FLAG_RELEASE);
	}
}

void CInput::HandleJoystickHatMotionEvent(const SDL_JoyHatEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.hat >= NUM_JOYSTICK_HATS)
		return;

	int aHatKeys[2];
	std::array<SSdlKeyEvent, NUM_JOYSTICK_BUTTONS_PER_HAT> aKeys;
	size_t NumKeys = 0;
	CJoystick::GetJoystickHatKeys(Event.hat, Event.value, aHatKeys);

	for(int Key = KEY_JOY_HAT0_UP + Event.hat * NUM_JOYSTICK_BUTTONS_PER_HAT; Key <= KEY_JOY_HAT0_RIGHT + Event.hat * NUM_JOYSTICK_BUTTONS_PER_HAT; Key++)
	{
		if(Key != aHatKeys[0] && Key != aHatKeys[1] && m_aCurrentKeyStates[Key])
		{
			aKeys[NumKeys++] = {Key, IInput::FLAG_RELEASE};
		}
	}

	for(int CurrentKey : aHatKeys)
	{
		if(CurrentKey != KEY_UNKNOWN && !m_aCurrentKeyStates[CurrentKey])
		{
			aKeys[NumKeys++] = {CurrentKey, IInput::FLAG_PRESS};
		}
	}
	AppendSdlKeyEvents(m_vInputEvents, aKeys.data(), NumKeys, m_InputCounter, m_aCurrentKeyStates, m_aFrameKeyStates);
}

void CInput::HandleJoystickAddedEvent(const SDL_JoyDeviceEvent &Event)
{
	OpenJoystick(Event.which);
}

void CInput::HandleJoystickRemovedEvent(const SDL_JoyDeviceEvent &Event)
{
	auto RemovedJoystick = std::find_if(m_vJoysticks.begin(), m_vJoysticks.end(), [Event](const CJoystick &Joystick) -> bool { return Joystick.GetInstanceId() == Event.which; });
	if(RemovedJoystick != m_vJoysticks.end())
	{
		dbg_msg("joystick", "Closed joystick %d '%s'", (*RemovedJoystick).GetIndex(), (*RemovedJoystick).GetName());
		SDL_CloseJoystick(RemovedJoystick->m_pDelegate);
		auto NextJoystick = m_vJoysticks.erase(RemovedJoystick);
		// Adjust indices of following joysticks
		while(NextJoystick != m_vJoysticks.end())
		{
			(*NextJoystick).m_Index--;
			++NextJoystick;
		}
		UpdateActiveJoystick();
	}
}

void CInput::HandleTouchDownEvent(const SDL_TouchFingerEvent &Event)
{
	CTouchFingerState TouchFingerState;
	TouchFingerState.m_Finger.m_DeviceId = Event.touchID;
	TouchFingerState.m_Finger.m_FingerId = Event.fingerID;
	TouchFingerState.m_Position = TouchPositionToViewport(vec2(Event.x, Event.y));
	TouchFingerState.m_Delta = TouchDeltaToViewport(vec2(Event.dx, Event.dy));
	TouchFingerState.m_PressTime = time_get_nanoseconds();
	m_vTouchFingerStates.emplace_back(TouchFingerState);
}

vec2 CInput::TouchPositionToViewport(vec2 Position) const
{
	return graphics_viewport::MapTouchPosition(Position, Graphics()->DrawableSize(), Graphics()->ScreenSize(), Graphics()->ViewportX());
}

vec2 CInput::TouchDeltaToViewport(vec2 Delta) const
{
	return graphics_viewport::MapTouchDelta(Delta, Graphics()->DrawableSize(), Graphics()->ScreenSize());
}

void CInput::HandleTouchUpEvent(const SDL_TouchFingerEvent &Event)
{
	auto FoundState = std::find_if(m_vTouchFingerStates.begin(), m_vTouchFingerStates.end(), [Event](const CTouchFingerState &State) {
		return State.m_Finger.m_DeviceId == Event.touchID && State.m_Finger.m_FingerId == Event.fingerID;
	});
	if(FoundState != m_vTouchFingerStates.end())
	{
		m_vTouchFingerStates.erase(FoundState);
	}
}

void CInput::HandleTouchMotionEvent(const SDL_TouchFingerEvent &Event)
{
	auto FoundState = std::find_if(m_vTouchFingerStates.begin(), m_vTouchFingerStates.end(), [Event](const CTouchFingerState &State) {
		return State.m_Finger.m_DeviceId == Event.touchID && State.m_Finger.m_FingerId == Event.fingerID;
	});
	if(FoundState != m_vTouchFingerStates.end())
	{
		FoundState->m_Position = TouchPositionToViewport(vec2(Event.x, Event.y));
		FoundState->m_Delta += TouchDeltaToViewport(vec2(Event.dx, Event.dy));
	}
}

void CInput::HandleTextEditingEvent(const char *pText, int Start, int Length)
{
	if(pText[0] != '\0')
	{
		ReserveSdlInputEvents(m_vInputEvents, 1);
		m_CompositionString = pText;
		m_CompositionCursor = SdlCompositionCursor(m_CompositionString.c_str(), Start);
		// Length is currently unused on Windows and will always be 0, so we don't support selecting composition text
		m_ImeCandidates.ResumeComposition();
		AddTextEvent("");
	}
	else
	{
		// 搜狗等 IME 在页边界/高亮第 5 个候选时可能短暂发送空 TEXTEDITING；
		// 仅清组合串，保留候选列表，避免弹窗隐现闪烁
		if(!QmImeEmptyTextEditingShouldClearCandidates())
		{
			m_CompositionString = "";
			m_CompositionCursor = 0;
		}
		else
		{
			ClearImeState();
		}
	}
}

void CInput::SetCompositionWindowPosition(float X, float Y, float H)
{
	SDL_Rect Rect;
	Rect.x = X / m_pGraphics->ScreenHiDPIScale();
	Rect.y = Y / m_pGraphics->ScreenHiDPIScale();
	Rect.h = H / m_pGraphics->ScreenHiDPIScale();
	Rect.w = 0;
	if(Window() != nullptr)
		SDL_SetTextInputArea(Window(), &Rect, 0);
}

static int TranslateKeyEventKey(const SDL_KeyboardEvent &KeyEvent)
{
	// See SDL_Keymod for possible modifiers:
	// NONE   =     0
	// LSHIFT =     1
	// RSHIFT =     2
	// LCTRL  =    64
	// RCTRL  =   128
	// LALT   =   256
	// RALT   =   512
	// LGUI   =  1024
	// RGUI   =  2048
	// NUM    =  4096
	// CAPS   =  8192
	// MODE   = 16384
	// Sum if you want to ignore multiple modifiers.
	if(KeyEvent.mod & g_Config.m_InpIgnoredModifiers)
	{
		return KEY_UNKNOWN;
	}

	int Key = SdlScancodeToKey(g_Config.m_InpTranslatedKeys ? SDL_GetScancodeFromKey(KeyEvent.key, nullptr) : KeyEvent.scancode);

#if defined(CONF_PLATFORM_ANDROID)
	// Translate the Android back-button to the escape-key so it can be used to open/close the menu, close popups etc.
	if(Key == KEY_AC_BACK)
	{
		Key = KEY_ESCAPE;
	}
#endif

	return Key;
}

static int TranslateMouseButtonEventKey(const SDL_MouseButtonEvent &MouseButtonEvent)
{
	switch(MouseButtonEvent.button)
	{
	case SDL_BUTTON_LEFT:
		return KEY_MOUSE_1;
	case SDL_BUTTON_RIGHT:
		return KEY_MOUSE_2;
	case SDL_BUTTON_MIDDLE:
		return KEY_MOUSE_3;
	case SDL_BUTTON_X1:
		return KEY_MOUSE_4;
	case SDL_BUTTON_X2:
		return KEY_MOUSE_5;
	case 6:
		return KEY_MOUSE_6;
	case 7:
		return KEY_MOUSE_7;
	case 8:
		return KEY_MOUSE_8;
	case 9:
		return KEY_MOUSE_9;
	default:
		return KEY_UNKNOWN;
	}
}

static int TranslateMouseWheelEventKey(const SDL_MouseWheelEvent &MouseWheelEvent)
{
	if(MouseWheelEvent.integer_y > 0)
	{
		return KEY_MOUSE_WHEEL_UP;
	}
	else if(MouseWheelEvent.integer_y < 0)
	{
		return KEY_MOUSE_WHEEL_DOWN;
	}
	else if(MouseWheelEvent.integer_x > 0)
	{
		return KEY_MOUSE_WHEEL_RIGHT;
	}
	else if(MouseWheelEvent.integer_x < 0)
	{
		return KEY_MOUSE_WHEEL_LEFT;
	}
	else
	{
		return KEY_UNKNOWN;
	}
}

#if defined(CONF_PLATFORM_EMSCRIPTEN)
extern "C" {

// This will be called from Emscripten JS code
bool EmscriptenQuit = false;
void EmscriptenCallbackQuit()
{
	EmscriptenQuit = true;
}
}
#endif

int CInput::Update()
{
	const int64_t Now = time_get();
	if(m_LastUpdate != 0)
	{
		const float Diff = (Now - m_LastUpdate) / (float)time_freq();
		m_UpdateTime = m_UpdateTime == 0.0f ? Diff : (m_UpdateTime * 0.8f + Diff * 0.2f);
	}
	m_LastUpdate = Now;

	// keep the counter between 1..0xFFFFFFFF, 0 means not pressed
	m_InputCounter = (m_InputCounter % std::numeric_limits<decltype(m_InputCounter)>::max()) + 1;

	SDL_Event Event;
	bool IgnoreKeys = false;
	CPerfTimer EventPumpTimer;
	int EventCount = 0;
	const bool TrackPerf = QmPerfEnabled();

	const auto &&AddKeyEventChecked = [&](int Key, int Flags) {
		if(Key != KEY_UNKNOWN && !IgnoreKeys && (!(Flags & IInput::FLAG_PRESS) || !HasComposition()))
		{
			// SDL3 合并了两个静音扫描码，两个旧键名必须成组提交。
			const SSdlKeyEvent aKeys[] = {{Key, Flags}, {KEY_AUDIOMUTE, Flags}};
			AppendSdlKeyEvents(m_vInputEvents, aKeys, Key == KEY_MUTE ? 2 : 1, m_InputCounter, m_aCurrentKeyStates, m_aFrameKeyStates);
		}
	};

#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(EmscriptenQuit)
	{
		return 1;
	}
#endif

	while(true)
	{
		const int64_t PollEventStartNs = TrackPerf ? time_get_nanoseconds().count() : 0;
		const int PollResult = SDL_PollEvent(&Event);
		if(TrackPerf)
		{
			const double PollEventDurationMs = (time_get_nanoseconds().count() - PollEventStartNs) / 1000000.0;
			if(QmPerfShouldLogDuration(PollEventDurationMs))
			{
				char aPollExtra[192];
				str_format(aPollExtra, sizeof(aPollExtra), "result=%d event_type=%d", PollResult, PollResult != 0 ? (int)Event.type : -1);
				LogPerfStage(m_pClient, "sdl_poll_event_call", PollEventDurationMs, false, aPollExtra);
			}
		}
		if(PollResult == 0)
			break;

#if defined(CONF_FAMILY_WINDOWS)
		// 钩子把 Win32 通知排入 SDL 队列，保持它与组合、提交事件的原有顺序。
		if(m_WindowsMessageEvent != 0 && Event.type == m_WindowsMessageEvent && Event.user.data1 == this)
		{
			if(!m_QueuedWindowsMessages.empty())
			{
				const auto Message = m_QueuedWindowsMessages.front();
				m_QueuedWindowsMessages.pop_front();
				MSG NativeMessage{};
				NativeMessage.hwnd = static_cast<HWND>(Message.m_pWindow);
				NativeMessage.message = Message.m_Message;
				NativeMessage.wParam = Message.m_WParam;
				NativeMessage.lParam = Message.m_LParam;
				CPerfTimer Timer;
				ProcessSystemMessage(&NativeMessage);
				LogPerfStage(m_pClient, "process_system_message", Timer.ElapsedMs());
			}
			continue;
		}
#endif
		if(Event.type >= SDL_EVENT_WINDOW_FIRST && Event.type <= SDL_EVENT_WINDOW_LAST && Event.window.windowID != Graphics()->GetWindowId())
			continue;
		++EventCount;
		switch(Event.type)
		{
		case SDL_EVENT_TEXT_EDITING:
			HandleTextEditingEvent(Event.edit.text, Event.edit.start, Event.edit.length);
			break;

		case SDL_EVENT_TEXT_EDITING_CANDIDATES:
		{
			std::optional<SQmImeCandidateList> NativeCandidates;
#if defined(CONF_FAMILY_WINDOWS)
			// SDL 的窗口过程也能收到同步 IME 通知，消息循环钩子可能收不到。
			// 优先补读 IMM 分页；尚无列表时直接使用本次 SDL 候选快照。
			if(Event.edit_candidates.num_candidates > 0 && m_ImeCandidates.AcceptsSdlCandidates() && Window() != nullptr)
			{
				void *pWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(Window()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
				NativeCandidates = ReadWindowsImeCandidates(pWindow, 0);
			}
			constexpr bool WindowsNumberedCandidates = true;
#else
			constexpr bool WindowsNumberedCandidates = false;
#endif
			m_ImeCandidates.UpdateFromSdl(Event.edit_candidates.candidates, Event.edit_candidates.num_candidates,
				Event.edit_candidates.selected_candidate, WindowsNumberedCandidates, std::move(NativeCandidates));
			break;
		}

		case SDL_EVENT_TEXT_INPUT:
			AddTextEvent(Event.text.text);
			ClearImeState();
			// 提交后抑制迟到候选，新的组合串或打开通知恢复接收。
			m_ImeCandidates.EndComposition();
			break;

		// handle keys
		case SDL_EVENT_KEY_DOWN:
			AddKeyEventChecked(TranslateKeyEventKey(Event.key), IInput::FLAG_PRESS | (Event.key.repeat != 0 ? FLAG_REPEAT : 0));
			break;

		case SDL_EVENT_KEY_UP:
			if(g_Config.m_QmGraphicsTrace > 0 && !Graphics()->WindowActive())
			{
				// 截图工具抢焦点时记录 SDL 松键事件，区分修饰键冲突与失焦重置。
				dbg_msg("input/focus", "event=unfocused_key_release key=%d scancode=%d", TranslateKeyEventKey(Event.key), Event.key.scancode);
			}
			AddKeyEventChecked(TranslateKeyEventKey(Event.key), IInput::FLAG_RELEASE);
			break;

		// handle the joystick events
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
			HandleJoystickAxisMotionEvent(Event.jaxis);
			break;

		case SDL_EVENT_JOYSTICK_BUTTON_UP:
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			HandleJoystickButtonEvent(Event.jbutton);
			break;

		case SDL_EVENT_JOYSTICK_HAT_MOTION:
			HandleJoystickHatMotionEvent(Event.jhat);
			break;

		case SDL_EVENT_JOYSTICK_ADDED:
			HandleJoystickAddedEvent(Event.jdevice);
			break;

		case SDL_EVENT_JOYSTICK_REMOVED:
			HandleJoystickRemovedEvent(Event.jdevice);
			break;

		// handle mouse buttons
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			AddKeyEventChecked(TranslateMouseButtonEventKey(Event.button), IInput::FLAG_PRESS);
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			AddKeyEventChecked(TranslateMouseButtonEventKey(Event.button), IInput::FLAG_RELEASE);
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			AddKeyEventChecked(TranslateMouseWheelEventKey(Event.wheel), IInput::FLAG_PRESS | IInput::FLAG_RELEASE);
			break;

		case SDL_EVENT_FINGER_DOWN:
			HandleTouchDownEvent(Event.tfinger);
			break;

		case SDL_EVENT_FINGER_CANCELED:
		case SDL_EVENT_FINGER_UP:
			HandleTouchUpEvent(Event.tfinger);
			break;

		case SDL_EVENT_FINGER_MOTION:
			HandleTouchMotionEvent(Event.tfinger);
			break;

		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
			Graphics()->SwitchWindowScreen(Graphics()->GetWindowScreen(), false);
			break;
		case SDL_EVENT_WINDOW_MOVED:
			Graphics()->Move(Event.window.data1, Event.window.data2);
			break;
		// listen to size changes, this includes our manual changes and the ones by the window manager
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		{
			int Width = 0, Height = 0;
			if(Window() != nullptr && SDL_GetWindowSize(Window(), &Width, &Height))
				Graphics()->GotResized(Width, Height, -1);
			break;
		}
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			if(m_InputGrabbed)
			{
#if defined(CONF_PLATFORM_MACOS) // Todo: remove this when fixed in SDL: https://github.com/libsdl-org/SDL/issues/13920
				MouseModeAbsolute();
#endif
				MouseModeRelative();
				// Clear pending relative mouse motion
				SDL_GetRelativeMouseState(nullptr, nullptr);
			}
			m_MouseFocus = true;
			IgnoreKeys = true;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			if(g_Config.m_QmGraphicsTrace > 0)
				dbg_msg("input/focus", "event=focus_lost tab=%d left_shift=%d right_shift=%d left_ctrl=%d right_ctrl=%d queued_events=%zu", m_aCurrentKeyStates[KEY_TAB], m_aCurrentKeyStates[KEY_LSHIFT], m_aCurrentKeyStates[KEY_RSHIFT], m_aCurrentKeyStates[KEY_LCTRL], m_aCurrentKeyStates[KEY_RCTRL], m_vInputEvents.size());
			std::fill(std::begin(m_aCurrentKeyStates), std::end(m_aCurrentKeyStates), false);
			m_MouseFocus = false;
			IgnoreKeys = true;
			if(m_InputGrabbed)
			{
				MouseModeAbsolute();
				// Remember that we had relative mouse
				m_InputGrabbed = true;
			}
			break;
		case SDL_EVENT_WINDOW_MINIMIZED:
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
			m_pConfigManager->Save();
#endif
			Graphics()->WindowDestroyNtf(Event.window.windowID);
			break;

		case SDL_EVENT_WINDOW_MAXIMIZED:
#if defined(CONF_PLATFORM_MACOS) // Todo: remove this when fixed in SDL
			MouseModeAbsolute();
			MouseModeRelative();
#endif
			[[fallthrough]];
		case SDL_EVENT_WINDOW_RESTORED:
			Graphics()->WindowCreateNtf(Event.window.windowID);
			break;

		case SDL_EVENT_DISPLAY_ADDED:
		case SDL_EVENT_DISPLAY_REMOVED:
			Graphics()->Move(0, 0);
			break;

#if defined(CONF_PLATFORM_IOS)
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
			m_pConfigManager->Save();
			break;
#endif

		// other messages
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		case SDL_EVENT_QUIT:
		{
			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "events=%d quit=1", EventCount);
			LogPerfStage(m_pClient, "sdl_poll_events", EventPumpTimer.ElapsedMs(), true, aExtra);
			return 1;
		}

		case SDL_EVENT_DROP_FILE:
			str_copy(m_aDropFile, Event.drop.data);
			break;
		}
	}

	char aExtra[128];
	str_format(aExtra, sizeof(aExtra), "events=%d has_composition=%d", EventCount, HasComposition() ? 1 : 0);
	LogPerfStage(m_pClient, "sdl_poll_events", EventPumpTimer.ElapsedMs(), EventCount > 64, aExtra);
	UpdateGlobalInputState();

	return 0;
}

void CInput::UpdateGlobalInputState()
{
	int NumKeys = 0;
	const bool *pKeyboardState = SDL_GetKeyboardState(&NumKeys);
	std::fill(std::begin(m_aGlobalKeyStates), std::end(m_aGlobalKeyStates), false);
	for(int Scan = 0; pKeyboardState != nullptr && Scan < NumKeys; ++Scan)
	{
		const int Key = SdlScancodeToKey(static_cast<SDL_Scancode>(Scan));
		if(Key != KEY_UNKNOWN && Key < KEY_LAST)
			m_aGlobalKeyStates[Key] = m_aGlobalKeyStates[Key] || pKeyboardState[Scan];
	}
	m_aGlobalKeyStates[KEY_AUDIOMUTE] = m_aGlobalKeyStates[KEY_MUTE];

#if SDL_VERSION_ATLEAST(2, 0, 4)
	float X = 0;
	float Y = 0;
	SDL_GetGlobalMouseState(&X, &Y);
	const vec2 NewPosition((float)X, (float)Y);
	m_GlobalMouseDelta = m_HasGlobalMousePos ? NewPosition - m_GlobalMousePos : vec2(0.0f, 0.0f);
	m_GlobalMousePos = NewPosition;
	m_HasGlobalMousePos = true;
#else
	m_GlobalMouseDelta = vec2(0.0f, 0.0f);
#endif
}

#if defined(CONF_FAMILY_WINDOWS)
bool SDLCALL CInput::WindowsMessageHook(void *pUser, MSG *pMsg)
{
	auto *pInput = static_cast<CInput *>(pUser);
	if(pMsg == nullptr || (pMsg->message != WM_IME_NOTIFY && pMsg->message != WM_IME_ENDCOMPOSITION) || pInput->m_WindowsMessageEvent == 0)
		return true;
	SDL_Window *pWindow = pInput->Window();
	if(pWindow == nullptr || pMsg->hwnd != SDL_GetPointerProperty(SDL_GetWindowProperties(pWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
		return true;
	QueueNativeImeMessage(pInput->m_QueuedWindowsMessages, {pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam}, [&] {
		SDL_Event Event{};
		Event.type = pInput->m_WindowsMessageEvent;
		Event.user.data1 = pInput;
		return SDL_PushEvent(&Event);
	});
	return true;
}

std::optional<SQmImeCandidateList> CInput::ReadWindowsImeCandidates(void *pWindow, unsigned CandidateListFlags)
{
	if(pWindow == nullptr)
		return std::nullopt;
	HWND WindowHandle = static_cast<HWND>(pWindow);
	const bool TrackPerf = QmPerfEnabled();
	const int64_t GetContextStartNs = TrackPerf ? time_get_nanoseconds().count() : 0;
	const auto ReleaseContext = [&](HIMC ImeContext) noexcept {
		const int64_t ReleaseContextStartNs = TrackPerf ? time_get_nanoseconds().count() : 0;
		const BOOL ReleaseContextResult = ImmReleaseContext(WindowHandle, ImeContext);
		try
		{
			if(TrackPerf)
			{
				const double ReleaseContextDurationMs = (time_get_nanoseconds().count() - ReleaseContextStartNs) / 1000000.0;
				if(QmPerfShouldLogDuration(ReleaseContextDurationMs))
				{
					char aExtra[192];
					str_format(aExtra, sizeof(aExtra), "result=%d", ReleaseContextResult != FALSE ? 1 : 0);
					LogPerfStage(m_pClient, "ime_release_context", ReleaseContextDurationMs, false, aExtra);
				}
			}
		}
		catch(...)
		{
			// 诊断失败不能中断上下文释放或覆盖读取结果。
		}
	};
	return ReadNativeImeCandidates(ImmGetContext(WindowHandle), ReleaseContext, [&](HIMC ImeContext) -> std::optional<SQmImeCandidateList> {
		if(TrackPerf)
		{
			const double GetContextDurationMs = (time_get_nanoseconds().count() - GetContextStartNs) / 1000000.0;
			if(QmPerfShouldLogDuration(GetContextDurationMs))
			{
				char aExtra[192];
				str_format(aExtra, sizeof(aExtra), "has_context=%d", ImeContext != nullptr ? 1 : 0);
				LogPerfStage(m_pClient, "ime_get_context", GetContextDurationMs, false, aExtra);
			}
		}
		if(ImeContext == nullptr)
			return std::nullopt;

		using TCandidateList = std::unique_ptr<CANDIDATELIST, decltype(&std::free)>;
		TCandidateList pCandidateList(nullptr, std::free);
		DWORD Size = 0;
		const auto LoadCandidateList = [&](DWORD CandidateListIndex) {
			const int64_t SizeQueryStartNs = TrackPerf ? time_get_nanoseconds().count() : 0;
			DWORD CandidateListSize = ImmGetCandidateListW(ImeContext, CandidateListIndex, nullptr, 0);
			if(TrackPerf)
			{
				const double SizeQueryDurationMs = (time_get_nanoseconds().count() - SizeQueryStartNs) / 1000000.0;
				if(QmPerfShouldLogDuration(SizeQueryDurationMs))
				{
					char aExtra[256];
					str_format(aExtra, sizeof(aExtra), "candidate_flags=%u candidate_index=%u returned_size=%u", (unsigned)CandidateListFlags, (unsigned)CandidateListIndex, (unsigned)CandidateListSize);
					LogPerfStage(m_pClient, "ime_candidate_size_query", SizeQueryDurationMs, false, aExtra);
				}
			}
			if(CandidateListSize == 0)
				return false;

			TCandidateList pLoadedCandidateList(static_cast<LPCANDIDATELIST>(std::malloc(CandidateListSize)), std::free);
			if(pLoadedCandidateList == nullptr)
				return false;

			const DWORD RequestedSize = CandidateListSize;
			const int64_t DataQueryStartNs = TrackPerf ? time_get_nanoseconds().count() : 0;
			CandidateListSize = ImmGetCandidateListW(ImeContext, CandidateListIndex, pLoadedCandidateList.get(), RequestedSize);
			if(TrackPerf)
			{
				const double DataQueryDurationMs = (time_get_nanoseconds().count() - DataQueryStartNs) / 1000000.0;
				if(QmPerfShouldLogDuration(DataQueryDurationMs))
				{
					char aExtra[256];
					str_format(aExtra, sizeof(aExtra), "candidate_flags=%u candidate_index=%u requested_size=%u returned_size=%u", (unsigned)CandidateListFlags, (unsigned)CandidateListIndex, (unsigned)RequestedSize, (unsigned)CandidateListSize);
					LogPerfStage(m_pClient, "ime_candidate_data_query", DataQueryDurationMs, false, aExtra);
				}
			}
			if(CandidateListSize == 0 || CandidateListSize > RequestedSize)
				return false;

			pCandidateList = std::move(pLoadedCandidateList);
			Size = CandidateListSize;
			return true;
		};
		for(DWORD CandidateListIndex = 0; CandidateListIndex < 32; ++CandidateListIndex)
		{
			if(!QmImeNotifyFlagsIncludeCandidateList(CandidateListFlags, CandidateListIndex))
				continue;
			if(LoadCandidateList(CandidateListIndex))
				break;
		}
		if(pCandidateList == nullptr && CandidateListFlags != 0 && !QmImeNotifyFlagsIncludeCandidateList(CandidateListFlags, 0))
			LoadCandidateList(0);

		SQmImeCandidateList LoadedCandidates;
		bool LoadSucceeded = false;
		if(pCandidateList && Size >= offsetof(CANDIDATELIST, dwOffset))
		{
			const size_t BufferSize = Size;
			const size_t HeaderSize = offsetof(CANDIDATELIST, dwOffset);
			const size_t AvailableOffsets = QmImeCandidateOffsetCapacity(BufferSize, HeaderSize);
			const DWORD CandidateCount = std::min<DWORD>(pCandidateList->dwCount, (DWORD)AvailableOffsets);
			const DWORD PageStart = std::min(pCandidateList->dwPageStart, CandidateCount);
			const DWORD PageSize = (DWORD)QmImeCandidatePageSizeOrCount(pCandidateList->dwPageSize, CandidateCount);
			const DWORD PageEnd = PageStart + std::min(PageSize, CandidateCount - PageStart);
			const auto *pBufferBegin = reinterpret_cast<const unsigned char *>(pCandidateList.get());
			LoadedCandidates.m_PageStart = (int)PageStart;
			LoadedCandidates.m_PageSize = (int)PageSize;
			LoadedCandidates.m_TotalCount = (int)CandidateCount;
			LoadedCandidates.m_vCandidates.reserve(PageEnd - PageStart);
			for(DWORD i = PageStart; i < PageEnd; i++)
			{
				const size_t Offset = pCandidateList->dwOffset[i];
				const std::optional<size_t> CandidateLength = QmImeBoundedUtf16Length(pBufferBegin, BufferSize, Offset);
				if(!CandidateLength.has_value())
					continue;
				const auto *pCandidate = reinterpret_cast<const wchar_t *>(pBufferBegin + Offset);
				const auto Candidate = windows_wide_to_utf8_bounded(pCandidate, pCandidate + *CandidateLength + 1);
				if(Candidate.has_value())
					LoadedCandidates.m_vCandidates.push_back(*Candidate);
			}
			if(pCandidateList->dwSelection >= PageStart && pCandidateList->dwSelection < PageEnd)
				LoadedCandidates.m_SelectedIndex = (int)pCandidateList->dwSelection - (int)PageStart;
			LoadSucceeded = true;
		}

		if(LoadSucceeded)
			return LoadedCandidates;
		return std::nullopt;
	});
}

void CInput::ProcessSystemMessage(MSG *pMsg)
{
	if(pMsg == nullptr)
		return;

	if(pMsg->message == WM_IME_ENDCOMPOSITION)
	{
		// 删除或取消组合输入时也要清理旧候选，不能只依赖文本提交或关闭候选通知。
		ClearImeState();
		m_ImeCandidates.EndComposition();
		return;
	}

	if(pMsg->message == WM_IME_NOTIFY)
	{
		switch(pMsg->wParam)
		{
		case IMN_OPENCANDIDATE:
		case IMN_CHANGECANDIDATE:
			m_ImeCandidates.UpdateFromNative(ReadWindowsImeCandidates(pMsg->hwnd, (unsigned)pMsg->lParam), pMsg->wParam == IMN_OPENCANDIDATE);
			break;
		case IMN_CLOSECANDIDATE:
			ClearImeCandidates();
			break;
		}
	}
}
#endif

#if defined(CONF_PLATFORM_EMSCRIPTEN)
extern "C" {

// This will be called from Emscripten JS code
char aEmscriptenDropFile[IO_MAX_PATH_LENGTH] = "";
void EmscriptenCallbackDropFile(const char *pFile)
{
	str_copy(aEmscriptenDropFile, pFile);
}
}
#endif

bool CInput::GetDropFile(char *aBuf, int Len)
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(aEmscriptenDropFile[0] != '\0')
	{
		str_copy(aBuf, aEmscriptenDropFile, Len);
		aEmscriptenDropFile[0] = '\0';
		return true;
	}
#endif
	if(m_aDropFile[0] != '\0')
	{
		str_copy(aBuf, m_aDropFile, Len);
		m_aDropFile[0] = '\0';
		return true;
	}
	return false;
}

IEngineInput *CreateEngineInput()
{
	return new CInput;
}
