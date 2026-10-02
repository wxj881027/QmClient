/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SPECTATOR_H
#define GAME_CLIENT_COMPONENTS_SPECTATOR_H
#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/online_replay_player.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <optional>

class CSpectator : public CComponent
{
	enum
	{
		MULTI_VIEW = -4,
		NO_SELECTION = -3,
	};

	// QmClient：按传送点编号查找的输入状态。
	enum class ETeleSearchStatus
	{
		IDLE,
		INVALID_NUMBER,
		NOT_FOUND,
		FOUND,
	};

	bool m_Active;
	bool m_WasActive;
	bool m_PresentationInitialized;

	int m_SelectedSpectatorId;
	vec2 m_SelectorMouse;

	CUi::CTouchState m_TouchState;

	float m_MultiViewActivateDelay;

	// QmClient：输入传送点编号后跳到该编号的传送点；同编号再次查找会循环到下一处。
	CLineInputBuffered<4> m_TeleNumberInput;
	// 数字既经按键录入又会产生文本事件，用该标志丢掉随后那一次重复文本事件。
	bool m_IgnoreTeleNumberTextEvent = false;
	ETeleSearchStatus m_TeleSearchStatus = ETeleSearchStatus::IDLE;
	int m_LastTeleNumber = 0;
	int m_LastTeleIndex = -1;
	// 自由视角切换需要一帧才生效，位置留到生效后再设置，避免被跟随镜头覆盖。
	bool m_TeleSearchPending = false;
	vec2 m_TeleSearchPosition = vec2(0.0f, 0.0f);

	bool CanChangeSpectatorId();
	void SpectateNext(bool Reverse);
	// QmClient：按编号查找传送点，以及承载它的输入行 UI。
	void FindTele();
	void RenderTeleSearch(vec2 Center, const CUIRect &RowRect, const CUIRect &StatusRect, float Alpha, bool MousePressed);
	// 在线回放使用正常旁观选择器与共用 demo 播放 HUD。
	bool GhostFreeCameraCanPan() const;
	COnlineReplayUiState m_ReplayUi;

	static void ConKeySpectator(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectate(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectateNext(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectatePrevious(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectateClosest(IConsole::IResult *pResult, void *pUserData);
	static void ConMultiView(IConsole::IResult *pResult, void *pUserData);

public:
	CSpectator();
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRender() override;
	void OnRelease() override;
	void OnReset() override;

	void Spectate(int SpectatorId);
	void SpectateClosest();
	// QmClient：正在输入传送点编号时，demo 快捷键等其它按键语义必须让位。
	bool IsEditingTeleNumber() const { return m_Active && m_TeleNumberInput.IsActive(); }

	bool PlaybackControlsActive() const;
	bool IsActive() const { return m_Active; }
};

#endif
