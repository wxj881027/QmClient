/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_SETTINGS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_MENUS_SETTINGS_CONTROLS_H

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/component.h>
#include <game/client/components/binds.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <cstdint>
#include <vector>

struct SSettingsCardDefinition;
namespace qm_card_catalog
{
	struct SQmCardBuildContext;
	struct QmCardRenderHook;
}

enum class EBindOptionGroup
{
	MOVEMENT,
	WEAPON,
	VOTING,
	CHAT,
	DUMMY,
	MISCELLANEOUS,
	CUSTOM,
	NUM,
};

class CBindSlotUiElement
{
public:
	CBindSlot m_Bind;
	CButtonContainer m_KeyReaderButton;
	CButtonContainer m_KeyResetButton;
	bool m_ToBeDeleted = false;

	CBindSlotUiElement(const CBindSlot &Bind) :
		m_Bind(Bind) {}

	bool operator<(const CBindSlotUiElement &Other) const;
};

class CBindOption
{
public:
	EBindOptionGroup m_Group;
	const char *m_pLabel;
	std::string m_Command;
	std::vector<CBindSlotUiElement> m_vCurrentBinds;
	CButtonContainer m_AddBindButtonContainer;
	char m_TooltipButtonId;
	bool m_AddNewBind = false;
	bool m_AddNewBindActivate = false;
	bool m_ToBeDeleted = false;

	std::vector<CBindSlotUiElement>::iterator GetBindSlotElement(const CBindSlot &BindSlot);
	bool MatchesSearch(const char *pSearch) const;
};

class CMenusSettingsControls : public CComponentInterfaces
{
public:
	void OnInterfacesInit(CGameClient *pClient) override;
	void Render(CUIRect MainView);

private:
	friend struct qm_card_catalog::QmCardRenderHook;
	SSettingsContentMetrics m_CardMetrics{};
	CScrollRegion *m_pCardScrollRegion = nullptr;
	void PrepareSettingsCards(const SSettingsContentMetrics &Metrics, bool ReadOnly, CScrollRegion *pScrollRegion);
	uint64_t SettingsCardsRevision(bool ReadOnly) const;
	bool BuildSettingsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out);
	static const char *BindGroupCardStableId(EBindOptionGroup Group);
	static void SyncBindGroupExpanded(bool *pExpanded);
	bool m_aBindGroupExpanded[(int)EBindOptionGroup::NUM];
	CButtonContainer m_aBindGroupExpandButtons[(int)EBindOptionGroup::NUM];
	std::vector<CBindOption> m_vBindOptions;
	size_t m_NumPredefinedBindOptions;
	bool m_BindOptionsDirty = true;
	uint64_t m_BindOptionsRevision = 0;
	uint64_t m_BindLayoutRevision = 1;
	void UpdateBindOptions();

	CScrollRegion m_SettingsScrollRegion;
	CButtonContainer m_ResetToDefaultButton;
	CLineInputBuffered<128> m_FilterInput;
	int m_CurrentSearchMatch = 0;
	std::vector<int> m_vSearchMatches;
	bool m_SearchMatchReveal = false;
	void UpdateSearchMatches();

	float MeasureSettingsBindsHeight(EBindOptionGroup Group) const;
	void RenderSettingsBinds(EBindOptionGroup Group, CUIRect View, bool ReadOnly);

	float MeasureSettingsMouseHeight() const;
	void RenderSettingsMouse(CUIRect View);
	std::vector<CButtonContainer> m_vJoystickIngameModeButtonContainers = {{}, {}};
	char m_aaJoystickAxisCheckboxIds[NUM_JOYSTICK_AXES][2]; // 2 for X and Y buttons
	CScrollRegion m_JoystickDropDownScrollRegion;
	CUi::SDropDownState m_JoystickDropDownState;
	float MeasureSettingsJoystickHeight(float ContentWidth) const;
	void RenderSettingsJoystick(CUIRect View, bool ReadOnly);
	void RenderJoystickAxisPicker(CUIRect View, bool ReadOnly);
	void RenderJoystickBar(const CUIRect *pRect, float Current, float Tolerance, bool Active);
	bool DoSettingsControlsNumericField(const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u);

	void DoSettingsControlsLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {}) const;
	void DoSettingsControlsMenuLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &Props = {}, int MaxWidth = -1) const;
	int DoSettingsControlsCheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect) const;
};

#endif
