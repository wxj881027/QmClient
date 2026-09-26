/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SPECTATOR_H
#define GAME_CLIENT_COMPONENTS_SPECTATOR_H
#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>
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
	void RenderTeleSearch(vec2 Center, float Alpha, bool MousePressed);
	// QmClient：查看模式底部播放控制条（布局对齐 demo 播放器的控制条：
	// 视角切换 + 进度定位 + 秒级/tick 级快进快退 + 倍速），原生 CUI 交互。
	// 显隐与 demo 播放一致：ESC 开关（见 OnInput 的单击/双击语义）
	void RenderGhostControlBar();
	// 控制条矩形（UI 坐标），渲染与命中判定共用同一布局
	CUIRect GhostControlBarRect() const;
	// 自由视角鼠标平移的前置条件：查看模式 + 自由视角 + 没有其它 UI 占用鼠标增量
	bool GhostFreeCameraCanPan() const;
	// 查看模式下鼠标是否归 UI 光标（控制条可点）。控制条隐藏时鼠标归镜头，
	// 原生选择器打开时归选择器
	bool GhostUiCursorActive() const;

	// QmClient：查看模式控制面板显隐（ESC 开关）
	bool m_GhostPanelOpen = false;
	// ESC 单击/双击判定：单击切面板、双击交给游戏菜单
	bool m_GhostEscapeArmed = false;
	float m_GhostEscapeLastTime = -1.0f;
	bool m_GhostPanelOpenBeforeEscape = false;

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

	bool IsActive() const { return m_Active; }
};

#endif
