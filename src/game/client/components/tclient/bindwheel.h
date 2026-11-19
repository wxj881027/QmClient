#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BINDWHEEL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BINDWHEEL_H

#include <base/str.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/bind_menu_preview.h>
#include <game/client/components/qmclient/bindwheel_state.h>
#include <game/client/components/qmclient/wheel_presets.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

class IConfigManager;

enum
{
	BINDWHEEL_MAX_NAME = 64,
	BINDWHEEL_MAX_CMD = 1024,
	BINDWHEEL_MAX_BINDS = 64
};

class CBindWheel : public CComponent
{
	float m_AnimationTime = 0.0f;

	CQmBindWheelState m_State;

	int m_SelectedBind = -1;
	int m_ListScroll = 0;
	bool m_JustOpened = false;
	qm_bind_menu_preview::SLayout m_LastLayout{};
	std::vector<int> m_vVisibleBindIndices;
	struct SResolvedBind
	{
		qm_bind_menu::EMenu m_Menu = qm_bind_menu::EMenu::CUSTOM;
		std::optional<qm_wheel_presets::SPreset> m_Preset;
		bool m_LegacyScript = false;
	};
	std::map<std::string, SResolvedBind> m_ResolvedBinds;

	static void ConOpenBindwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConOpenBindMenu(IConsole::IResult *pResult, void *pUserData);
	static void ConAddBindwheelLegacy(IConsole::IResult *pResult, void *pUserData);
	static void ConAddBindwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveBindwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveAllBindwheelBinds(IConsole::IResult *pResult, void *pUserData);
	static void ConBindwheelExecuteHover(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	class CBind
	{
	public:
		char m_aName[BINDWHEEL_MAX_NAME] = "EMPTY";
		char m_aCommand[BINDWHEEL_MAX_CMD] = "";

		bool operator==(const CBind &Other) const
		{
			return str_comp(m_aName, Other.m_aName) == 0 && str_comp(m_aCommand, Other.m_aCommand) == 0;
		}
	};

	std::vector<CBind> m_vBinds;

	CBindWheel();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void AddBind(const char *Name, const char *Command);
	bool SetBind(int Index, const char *pName, const char *pCommand);
	void RemoveBind(const char *Name, const char *Command);
	void RemoveBind(int Index);
	void RemoveAllBinds();

	void ExecuteHoveredBind();
	void ExecuteBind(int Bind);

	bool IsActive() const { return m_State.IsActive(); }
	qm_bind_menu::EMenu MenuForBind(int Index);
	const qm_wheel_presets::SPreset *PresetForBind(int Index);

private:
	void OpenMenu(qm_bind_menu::EMenu Menu);
	void ReleaseActiveMenu();
	const SResolvedBind &ResolveBind(int Index);
	std::optional<std::string> ReadExecConfig(const char *pPath);
	void RebuildVisibleBinds();
	void ClampListScroll();
	int CurrentSelection() const;
	int CurrentSourceIndex() const;
};

#endif
