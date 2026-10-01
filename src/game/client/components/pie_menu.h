// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* Q1menG Client - Pie Menu Component */
/* Provides quick access to player interaction operations through a circular menu */

#ifndef GAME_CLIENT_COMPONENTS_PIE_MENU_H
#define GAME_CLIENT_COMPONENTS_PIE_MENU_H

#include "pie_menu_logic.h"
#include "qmclient/pie_menu_points.h"

#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>

#include <array>
#include <string>
#include <vector>

class CPieMenu : public CComponent
{
public:
	using EMenuState = qm_pie_menu::CMenuLifecycle::EState;

	using EMenuOption = qm_pie_menu::EOption;

private:
	// Menu state
	qm_pie_menu::CMenuLifecycle m_Lifecycle;
	int m_TargetClientId;
	std::string m_TargetName;
	std::string m_TargetClan;
	int m_SelectedOption;
	int m_SelectedRenameIndex;
	int m_CommittedOption = -1;
	int m_CommittedRenameIndex = -1;
	float m_AnimationProgress;
	vec2 m_MenuCenter;
	int64_t m_OpenTime;
	int64_t m_CloseTime = 0;
	bool m_WasPressed;
	vec2 m_SelectorMouse; // Mouse position for selection
	std::vector<std::string> m_vRenameQueue;
	std::array<EMenuOption, qm_pie_menu::OPTION_COUNT> m_vVisibleOptions{};
	int m_VisibleOptionCount = 0;
	qm_pie_menu::SFollowState m_FollowState;
	double m_NextFollowScan = 0.0;
	double m_NextFollowRefresh = 0.0;
	qm_pie_menu::CPointsRequest m_PointsRequest;

	// Menu parameters (in screen pixels) - scaled 1.8x
	static constexpr float INNER_RADIUS = 108.0f; // 60 * 1.8
	static constexpr float OUTER_RADIUS = 288.0f; // 160 * 1.8
	static constexpr float SECONDARY_INNER_RADIUS = OUTER_RADIUS + 12.0f;
	static constexpr float SECONDARY_OUTER_RADIUS = OUTER_RADIUS + 108.0f;
	static constexpr float START_ANGLE = -90.0f; // Start from 12 o'clock
	static constexpr float SECTOR_GAP = 3.6f; // 2 * 1.8

	// Animation parameters
	static constexpr float ANIMATION_DURATION = 0.16f; // seconds (虹膜光圈开合耗时 160ms，兼具急速响应与清晰视觉轨迹)
	static constexpr float CLOSE_DURATION = 0.08f; // seconds
	static constexpr float MIN_SCALE = 0.85f;
	static constexpr float MAX_SCALE = 1.0f;
	static constexpr float HIGHLIGHT_SCALE = 1.25f; // 25% larger when highlighted

	// Console command handlers
	static void ConKeyPieMenu(IConsole::IResult *pResult, void *pUserData);
	static void ConStopFollow(IConsole::IResult *pResult, void *pUserData);

	// Helper methods
	int FindNearestPlayer();
	void OpenMenu();
	void CloseMenu(bool HasExecuted = false);
	void RefreshVisibleOptions();
	void UpdateFollowState();
	void ToggleTargetFollow();
	void RequestTargetPoints();
	void UpdatePointsRequest();
	void ExecuteTeamAction(EMenuOption Option);
	void UpdateSelection();
	void RefreshRenameQueue();
	void ExecuteOption(EMenuOption Option);
	void ExecuteRenameOption(int RenameIndex);

	// Rendering helpers
	void RenderOverlay();
	void RenderSector(int Index, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha, float AngleOffset = 0.0f, float SpanFactor = 1.0f, float BladeEdgeAlpha = 0.0f);
	void RenderRenameSector(int Index, int SectorCount, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha, float AngleOffset = 0.0f, float SpanFactor = 1.0f);
	void RenderCenterInfo(float Alpha = 1.0f);
	float MenuScale() const;
	vec2 GetSectorPosition(int Index, float Radius) const;
	float GetSectorAngle(int Index) const;
	const char *GetOptionName(EMenuOption Option) const;
	const char *GetOptionIcon(EMenuOption Option) const;
	ColorRGBA GetOptionColor(EMenuOption Option, bool Highlighted) const;
	int VisibleOptionCount() const { return m_VisibleOptionCount; }
	EMenuOption VisibleOption(int Index) const { return m_vVisibleOptions[Index]; }
	bool IsFollowingTarget() const;
	bool FormatTargetScore(char *pBuffer, size_t BufferSize) const;

	// Input helpers
	bool IsMouseInCenter() const;
	bool HasTargetPlayer() const;
	int GetHoveredOption() const;
	int GetHoveredRenameOption() const;

public:
	CPieMenu();
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnReset() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRender() override;
	void OnRelease() override;

	bool IsActive() const { return m_Lifecycle.IsVisible(); }
	bool IsFollowing() const { return m_FollowState.m_Active; }
	bool IsFollowingPlayer(const char *pName, const char *pClan) const;
	void ToggleFollowPlayer(int ClientId);
	void CancelFollow();
};

#endif
