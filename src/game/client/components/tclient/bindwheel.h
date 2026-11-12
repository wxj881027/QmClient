#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BINDWHEEL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BINDWHEEL_H

#include <base/str.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/bindwheel_state.h>

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
	float m_aAnimationTimeItems[BINDWHEEL_MAX_BINDS] = {0};

	CQmBindWheelState m_State;

	int m_SelectedBind;

	static void ConOpenBindwheel(IConsole::IResult *pResult, void *pUserData);
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

private:
	struct SPage
	{
		char m_aName[BINDWHEEL_MAX_NAME] = "";
		std::vector<CBind> m_vBinds;
	};

	// 页 0 永远是用户自己的 bind 列表；内置页在每次打开时按上下文重建，不写配置。
	std::vector<SPage> m_vBuiltinPages;
	int m_CurrentPage = 0;
	int m_ListScroll = 0;

	// 面板布局每帧更新的命中几何；释放按键时同一次交互复用。
	float m_ListRowsLeft = 0.0f;
	float m_ListRowsTop = 0.0f;
	float m_ListRowsWidth = 0.0f;
	float m_ListRowHeight = 0.0f;
	int m_ListVisibleRows = 0;
	float m_TabsLeft = 0.0f;
	float m_TabsTop = 0.0f;
	float m_TabsWidth = 0.0f;
	float m_TabsHeight = 0.0f;

	void RebuildMenuPages();
	bool InNormalPractice() const;
	void SwitchPage(int Index);
	void ClampListScroll();
	const std::vector<CBind> &CurrentBinds() const;
	const char *PageName(int Index) const;
	int PageCount() const { return 1 + static_cast<int>(m_vBuiltinPages.size()); }
	int CurrentSelection() const;
	int HitTestPage(vec2 Mouse) const;
	void RenderListLayout(float ScreenW, float ScreenH, float Alpha);
};

#endif
