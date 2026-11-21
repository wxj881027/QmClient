/* Copyright © 2026 BestProject Team */
// 快速练习的输入处理：真实服务器输入锁、缓冲输入清理、分身/本体输入过滤。
// 与预测循环分开，输入规则只在这里维护。

#include "fast_practice.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

#include <algorithm>

void CFastPractice::ReleaseBufferedInputState()
{
	// Hard invalidation path for reconnects/disconnects where stale movement
	// buffers should not survive into a different server-side input stream.
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		QmNeutralizePracticeInput(GameClient()->m_Controls.m_aInputData[Conn]);
		QmNeutralizePracticeInput(GameClient()->m_Controls.m_aLastData[Conn]);
		GameClient()->m_Controls.m_aInputData[Conn].m_Fire = QmPracticeReleasedFireState(GameClient()->m_Controls.m_aInputData[Conn].m_Fire);
		GameClient()->m_Controls.m_aLastData[Conn].m_Fire = QmPracticeReleasedFireState(GameClient()->m_Controls.m_aLastData[Conn].m_Fire);
	}
	QmNeutralizePracticeInput(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy]);
	GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire = QmPracticeReleasedFireState(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire);

	QmNeutralizePracticeInput(GameClient()->m_DummyInput);
	GameClient()->m_DummyInput.m_Fire = QmPracticeReleasedFireState(GameClient()->m_DummyInput.m_Fire);
	QmNeutralizePracticeInput(GameClient()->m_HammerInput);
	GameClient()->m_HammerInput.m_Fire = QmPracticeReleasedFireState(GameClient()->m_HammerInput.m_Fire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::CaptureServerReleasedFireStates()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		int FireState = GameClient()->m_DummyInput.m_Fire;
		if(Slot == g_Config.m_ClDummy)
			FireState = GameClient()->m_Controls.m_aLastData[Slot].m_Fire;
		else if(g_Config.m_ClDummyHammer)
			FireState = GameClient()->m_HammerInput.m_Fire;
		m_InputLock.SetReleasedFireState(Slot, FireState);
	}
}

void CFastPractice::ReleaseBufferedActionInputState()
{
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		const int ReleasedFire = m_InputLock.ReleasedFireState(Conn);
		QmPracticeSuppressActionInput(GameClient()->m_Controls.m_aInputData[Conn], ReleasedFire);
		QmPracticeSuppressActionInput(GameClient()->m_Controls.m_aLastData[Conn], ReleasedFire);
		QmPracticeSuppressActionInput(GameClient()->m_Controls.m_aFastInput[Conn], ReleasedFire);
	}

	const int DummySlot = !g_Config.m_ClDummy;
	const int DummyReleasedFire = m_InputLock.ReleasedFireState(DummySlot);
	QmPracticeSuppressActionInput(GameClient()->m_DummyInput, DummyReleasedFire);
	QmPracticeSuppressActionInput(GameClient()->m_HammerInput, DummyReleasedFire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::CapturePracticeInputFilterStates(bool PreserveHeldHook)
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		CNetObj_PlayerInput Input = GameClient()->m_Controls.m_aInputData[Slot];
		int Left = 0;
		int Right = 0;
		if(Slot == g_Config.m_ClDummy)
		{
			Left = GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
			Right = GameClient()->m_Controls.m_aInputDirectionRight[Slot];
		}
		else if(Input.m_Direction < 0)
		{
			Left = 1;
		}
		else if(Input.m_Direction > 0)
		{
			Right = 1;
		}

		SPracticeInputFilterState State;
		State.m_LeftHeld = Left != 0;
		State.m_LeftReleased = !State.m_LeftHeld;
		State.m_RightHeld = Right != 0;
		State.m_RightReleased = !State.m_RightHeld;
		State.m_JumpReleased = Input.m_Jump == 0;
		const int ClientId = GameClient()->m_aLocalIds[Slot];
		CCharacter *pCharacter = ClientId >= 0 ? m_PracticeBaseWorld.GetCharacterById(ClientId) : nullptr;
		const bool KeepAttachedHook = PreserveHeldHook && pCharacter && pCharacter->Core()->m_HookState == HOOK_GRABBED;
		State.m_Hook.Capture(Input.m_Hook, KeepAttachedHook);
		m_aPracticeInputFilterStates[Slot] = State;
	}
}

void CFastPractice::PreserveCurrentPracticeInputFilterStates()
{
	for(auto &State : m_aPracticeInputFilterStates)
	{
		State.m_LeftReleased = true;
		State.m_RightReleased = true;
		State.m_JumpReleased = true;
		State.m_Hook.Reset();
	}
}

void CFastPractice::FilterPracticeInput(CNetObj_PlayerInput &Input, int InputConn, bool Commit)
{
	const int Slot = InputConn ^ g_Config.m_ClDummy;
	if(Slot < 0 || Slot >= NUM_DUMMIES)
		return;

	SPracticeInputFilterState State = m_aPracticeInputFilterStates[Slot];
	int LiveLeft = 0;
	int LiveRight = 0;
	if(Slot == g_Config.m_ClDummy)
	{
		LiveLeft = GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
		LiveRight = GameClient()->m_Controls.m_aInputDirectionRight[Slot];
	}
	else if(GameClient()->m_Controls.m_aInputData[Slot].m_Direction < 0)
	{
		LiveLeft = 1;
	}
	else if(GameClient()->m_Controls.m_aInputData[Slot].m_Direction > 0)
	{
		LiveRight = 1;
	}

	if(State.m_LeftHeld && !State.m_LeftReleased)
	{
		if(LiveLeft == 0)
			State.m_LeftReleased = true;
		else
			LiveLeft = 0;
	}
	if(State.m_RightHeld && !State.m_RightReleased)
	{
		if(LiveRight == 0)
			State.m_RightReleased = true;
		else
			LiveRight = 0;
	}
	if(Slot == g_Config.m_ClDummy || (State.m_LeftHeld && !State.m_LeftReleased) || (State.m_RightHeld && !State.m_RightReleased))
		Input.m_Direction = LiveRight - LiveLeft;

	if(!State.m_JumpReleased)
	{
		if(GameClient()->m_Controls.m_aInputData[Slot].m_Jump == 0)
			State.m_JumpReleased = true;
	}
	if(!State.m_JumpReleased)
	{
		if(Input.m_Jump != 0)
			Input.m_Jump = 0;
	}
	Input.m_Hook = State.m_Hook.Filter(Input.m_Hook, GameClient()->m_Controls.m_aInputData[Slot].m_Hook);

	if(Commit)
		m_aPracticeInputFilterStates[Slot] = State;
}

void CFastPractice::InvalidateBufferedInputState()
{
	ReleaseBufferedInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
}

void CFastPractice::CaptureServerLockedInputs()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		// 练习中接入分身时，主角色继续沿用进入练习前锁定的输入。
		if(!QmPracticeShouldCaptureServerLockedInput(m_Enabled, m_InputLock.HasLockedInput(Slot), GameClient()->m_aLocalIds[Slot], m_EnableLocalClientId))
			continue;

		CNetObj_PlayerInput Input = {};
		if(Slot == g_Config.m_ClDummy)
		{
			Input = GameClient()->m_Controls.m_aInputData[Slot];
			Input.m_Direction = GameClient()->m_Controls.m_aInputDirectionRight[Slot] - GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
		}
		else
		{
			Input = g_Config.m_ClDummyHammer ? GameClient()->m_HammerInput : GameClient()->m_DummyInput;
		}

		m_InputLock.Capture(Slot, Input);
	}
}

void CFastPractice::PrepareInputForSend(int *pData, int Size, bool Dummy)
{
	if(!pData || Size < (int)sizeof(CNetObj_PlayerInput))
		return;
	auto *pInput = reinterpret_cast<CNetObj_PlayerInput *>(pData);
	const int Slot = g_Config.m_ClDummy ^ (int)Dummy;
	// Keep the real server stream locked to the exact input state that was
	// active when fast practice was opened. Practice inputs must not move it.
	m_InputLock.PrepareForSend(*pInput, Slot, m_Enabled);
}
