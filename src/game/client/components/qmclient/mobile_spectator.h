#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_H

#include "mobile_spectator_controls.h"
#include "mobile_vote_bar.h"

#include <game/client/component.h>

class CQmMobileSpectator : public CComponent
{
	qm_mobile_spectator::CTouchState m_Touch;
	qm_mobile_spectator::CJoinState m_Join;

	// 聊天按钮最近一次打开聊天的时间；同一根手指不能立刻又被判为关闭手势。
	std::chrono::nanoseconds m_ChatToggleActivationTime{};
	// 计分板最近一次被按钮打开的时间；同一根手指不能立刻又把面板关掉。
	std::chrono::nanoseconds m_ScoreboardToggleActivationTime{};
	// 基础可用性：窗口活跃、在服务器/回放里、没有其它全屏界面。
	bool BaseControlsAvailable() const;
	// 完整触控层：基础可用且聊天、计分板都未占用输入。
	bool ControlsAvailable() const;
	// 计分板打开时只保留“关闭计分板”这一个按钮。
	bool ScoreboardOnlyControls() const;
	// 手机端顶部投票条：在线投票且没有全屏界面遮挡时显示。
	bool MobileVoteBarVisible() const;
	void RenderMobileVoteBar(const SQmMobileVoteBarLayout &Layout);
	// 落在投票条上的手指归它所有，不参与镜头拖动；只在新按下时投一次。
	std::vector<IInput::CTouchFinger> m_vVoteBarFingers;
	// 计分板打开时的下滑关闭：从面板顶部区域起手、向下滑过阈值即关闭。
	std::optional<IInput::CTouchFinger> m_ScoreboardSwipeFinger;
	// 面板外的候选点击：抬起时才判定，滑动不关闭（避免吃掉三指截图等系统手势）。
	std::optional<IInput::CTouchFinger> m_ScoreboardOutsideFinger;
	vec2 m_ScoreboardOutsideStart{};
	std::chrono::nanoseconds m_ScoreboardOutsidePressTime{};
	float m_ScoreboardSwipeStartY = 0.0f;
	// 左边缘右滑 = 返回；800ms 内再滑一次 = 退出确认。
	void PerformBack();
	// 按钮按下后的顶部提示：说明这次生效了什么（1.8s 后淡出）。
	void ShowToast(const char *pText);
	// 灵动岛点击：回到自由视角（原「自由视角」按钮的职责）。
	void PerformFreeView();
	void RenderToast(const vec2 &Screen);
	// 底部灵动岛：常驻取景状态，同时是 HUD 编辑器里的一个元素。
	void RenderIsland(const vec2 &Screen);
	// 按钮底板：先模糊身后的画面，再叠半透明色，避免按钮直接压在场景上看不清。
	void DrawButtonSurface(const CUIRect &Rect, const ColorRGBA &Color, float Rounding);
	char m_aToastText[128] = "";
	std::chrono::nanoseconds m_ToastUntil{};
	std::chrono::nanoseconds m_LastBackTime{};
	std::optional<IInput::CTouchFinger> m_EdgeSwipeFinger;
	float m_EdgeSwipeStartX = 0.0f;
	// 底部灵动岛：一次按下只触发一次，松手才能再点。
	std::optional<IInput::CTouchFinger> m_IslandFinger;
	// 上一帧按文字量出来的岛体矩形：宽度自适应，两侧不留空白。
	CUIRect m_IslandRect{};

	bool FreeViewActive() const;
	vec2 ScreenSize() const;
	void PerformAction(int Action);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRelease() override { m_Touch.Cancel(); }
	void OnWindowResize() override { m_Touch.Cancel(); }
	void OnStateChange(int NewState, int OldState) override { OnReset(); }
	void OnNewSnapshot() override;
	bool OnTouchState(std::vector<IInput::CTouchFingerState> &vFingers) override;
	void OnRender() override;
	void ShowMediaFiles(bool Videos);
	// 把单个 demo 直接导出到系统「下载/QmClient」，不走目录浏览。
	void ExportFile(const char *pPath);
};

#endif
