/* Q1menG Client - Pie Menu Component */
/* Provides quick access to player interaction operations through a circular menu */

#ifndef GAME_CLIENT_COMPONENTS_PIE_MENU_H
#define GAME_CLIENT_COMPONENTS_PIE_MENU_H

#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>

#include <string>
#include <vector>

class CPieMenu : public CComponent
{
public:
	enum class EMenuState
	{
		INACTIVE = 0,
		OPENING,
		ACTIVE,
		CLOSING
	};

	enum class EMenuOption
	{
		FRIEND = 0,
		WHISPER,
		MENTION,
		COPY_SKIN,
		SWAP,
		SPECTATE,
		NUM_OPTIONS
	};

private:
	// Menu state
	EMenuState m_State;
	bool m_Active;
	int m_TargetClientId;
	int m_SelectedOption;
	int m_SelectedRenameIndex;
	float m_AnimationProgress;
	vec2 m_MenuCenter;
	int64_t m_OpenTime;
	bool m_WasPressed;
	vec2 m_SelectorMouse; // Mouse position for selection
	std::vector<std::string> m_vRenameQueue;

	// Menu parameters (in screen pixels) - scaled 1.8x
	static constexpr float INNER_RADIUS = 108.0f;  // 60 * 1.8
	static constexpr float OUTER_RADIUS = 288.0f;  // 160 * 1.8
	static constexpr float SECONDARY_INNER_RADIUS = OUTER_RADIUS + 12.0f;
	static constexpr float SECONDARY_OUTER_RADIUS = OUTER_RADIUS + 108.0f;
	static constexpr float START_ANGLE = -90.0f; // Start from 12 o'clock
	static constexpr float SECTOR_GAP = 3.6f; // 2 * 1.8

	// Animation parameters
	static constexpr float ANIMATION_DURATION = 0.08f; // seconds
	static constexpr float MIN_SCALE = 0.85f;
	static constexpr float MAX_SCALE = 1.0f;
	static constexpr float HIGHLIGHT_SCALE = 1.25f; // 25% larger when highlighted

	// Console command handlers
	static void ConKeyPieMenu(IConsole::IResult *pResult, void *pUserData);

	// Helper methods
	int FindNearestPlayer();
	void OpenMenu();
	void CloseMenu();
	void UpdateSelection();
	void RefreshRenameQueue();
	void ExecuteOption(EMenuOption Option);
	void ExecuteRenameOption(int RenameIndex);

	// Rendering helpers
	void RenderOverlay();
	void RenderSector(int Index, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha);
	void RenderRenameSector(int Index, int SectorCount, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha);
	void RenderCenterInfo();
	vec2 GetSectorPosition(int Index, float Radius) const;
	float GetSectorAngle(int Index) const;
	const char *GetOptionName(EMenuOption Option) const;
	const char *GetOptionIcon(EMenuOption Option) const;
	ColorRGBA GetOptionColor(EMenuOption Option, bool Highlighted) const;

	// Input helpers
	bool IsMouseInCenter() const;
	int GetHoveredOption() const;
	int GetHoveredRenameOption() const;

public:
	CPieMenu();
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnReset() override;
	void OnConsoleInit() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRender() override;
	void OnRelease() override;

	bool IsActive() const { return m_Active; }
};

#endif
