/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>

class CControls : public CComponent
{
public:
	float GetMinMouseDistance() const;
	float GetMaxMouseDistance() const;

	enum class EMouseInputType
	{
		ABSOLUTE,
		RELATIVE,
		AUTOMATED,
	};

	enum class EHookReset
	{
		KEEP_HELD,
		RELEASE_HELD,
	};

	// 本帧子 tick 瞄准的鼠标位置是否需要在本帧采样（逐 dummy 记录）。
	bool m_aMouseOnAction[NUM_DUMMIES];
	vec2 m_aMousePos[NUM_DUMMIES];
	vec2 m_aMousePosOnAction[NUM_DUMMIES];
	vec2 m_aTargetPos[NUM_DUMMIES];

	EMouseInputType m_aMouseInputType[NUM_DUMMIES];

	int m_aAmmoCount[NUM_WEAPONS];

	int64_t m_LastSendTime;
	CNetObj_PlayerInput m_aInputData[NUM_DUMMIES];
	CNetObj_PlayerInput m_aLastData[NUM_DUMMIES];
	int m_aInputDirectionLeft[NUM_DUMMIES];
	int m_aInputDirectionRight[NUM_DUMMIES];
	int m_aShowHookColl[NUM_DUMMIES];
	int m_aShowWeaponTrajectory[NUM_DUMMIES];

	CNetObj_PlayerInput m_aFastInput[NUM_DUMMIES];
	bool m_FastInputHookAction = false;
	bool m_FastInputFireAction = false;

	CControls();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnConsoleInit() override;
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	static void ResetHeldInput(CNetObj_PlayerInput &LastInput, CNetObj_PlayerInput &Input, EHookReset HookReset)
	{
		LastInput.m_Direction = 0;
		// 模拟松开开火键，保留输入计数器。
		if((LastInput.m_Fire & 1) != 0)
			LastInput.m_Fire++;
		LastInput.m_Fire &= INPUT_STATE_MASK;
		LastInput.m_Jump = 0;
		if(HookReset == EHookReset::RELEASE_HELD)
			LastInput.m_Hook = 0;
		Input = LastInput;
	}

	void ResetInput(int Dummy, EHookReset HookReset = EHookReset::KEEP_HELD);
	bool CheckNewInput();

private:
	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConTpToCursor(IConsole::IResult *pResult, void *pUserData);
	static void ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData);
	vec2 CursorWorldPos() const;
	void SendPracticeTeleportToCursor(int Conn);
};
#endif
