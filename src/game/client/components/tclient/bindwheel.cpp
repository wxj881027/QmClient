#include "bindwheel.h"

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_menu.h>
#include <game/client/components/qmclient/bind_menu_preview.h>
#include <game/client/components/qmclient/bounded_file_reader.h>
#include <game/client/components/qmclient/wheel_presets.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <vector>

CBindWheel::CBindWheel()
{
	OnReset();
}

void CBindWheel::ConBindwheelExecuteHover(IConsole::IResult *pResult, void *pUserData)
{
	CBindWheel *pThis = (CBindWheel *)pUserData;
	if(pResult->GetInteger(0) != 0)
		pThis->ExecuteHoveredBind();
}

void CBindWheel::ConOpenBindwheel(IConsole::IResult *pResult, void *pUserData)
{
	CBindWheel *pThis = (CBindWheel *)pUserData;
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(!Pressed)
	{
		// 松开触发键必须无条件收尾，不能依赖菜单参数或当前菜单名。
		pThis->ReleaseActiveMenu();
		return;
	}
	if(pThis->Client()->State() != IClient::STATE_ONLINE)
	{
		pThis->OnRelease();
		return;
	}
	// 按键类浮层互斥：打开绑定菜单时取消表情轮盘，但不提交当前悬停表情。
	if(pThis->GameClient()->m_Emoticon.IsActive())
		pThis->GameClient()->m_Emoticon.Cancel();
	if(!pThis->IsActive())
		pThis->GameClient()->m_Emoticon.m_TouchPressedOutside = false;
	pThis->OpenMenu(qm_bind_menu::EMenu::CUSTOM);
}

void CBindWheel::ConOpenBindMenu(IConsole::IResult *pResult, void *pUserData)
{
	CBindWheel *pThis = (CBindWheel *)pUserData;
	// 控制台会先为 + 命令写入 stroke，再解析绑定参数：0 = 按下/释放，1 = 菜单名。
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(!Pressed)
	{
		// 任何释放都必须收尾当前菜单，避免 BindWheel 永久 Active 后挡住 +emote。
		pThis->ReleaseActiveMenu();
		return;
	}
	const auto Menu = qm_bind_menu::Parse(pResult->GetString(1));
	if(!Menu)
		return;
	if(pThis->Client()->State() != IClient::STATE_ONLINE)
	{
		pThis->OnRelease();
		return;
	}
	if(pThis->GameClient()->m_Emoticon.IsActive())
		pThis->GameClient()->m_Emoticon.Cancel();
	if(!pThis->IsActive())
		pThis->GameClient()->m_Emoticon.m_TouchPressedOutside = false;
	pThis->OpenMenu(*Menu);
}

void CBindWheel::ConAddBindwheelLegacy(IConsole::IResult *pResult, void *pUserData)
{
	int BindPos = pResult->GetInteger(0);
	if(BindPos < 0 || BindPos >= BINDWHEEL_MAX_BINDS)
		return;

	const char *aName = pResult->GetString(1);
	const char *aCommand = pResult->GetString(2);

	CBindWheel *pThis = static_cast<CBindWheel *>(pUserData);
	if(pThis->m_vBinds.size() <= (size_t)BindPos)
		pThis->m_vBinds.resize((size_t)BindPos + 1);

	str_copy(pThis->m_vBinds[BindPos].m_aName, aName);
	str_copy(pThis->m_vBinds[BindPos].m_aCommand, aCommand);
	pThis->m_State.InvalidateSelection();
}

void CBindWheel::ConAddBindwheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CBindWheel *pThis = static_cast<CBindWheel *>(pUserData);
	pThis->AddBind(aName, aCommand);
}

void CBindWheel::ConRemoveBindwheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CBindWheel *pThis = static_cast<CBindWheel *>(pUserData);
	pThis->RemoveBind(aName, aCommand);
}

void CBindWheel::ConRemoveAllBindwheelBinds(IConsole::IResult *pResult, void *pUserData)
{
	CBindWheel *pThis = static_cast<CBindWheel *>(pUserData);
	pThis->RemoveAllBinds();
}

void CBindWheel::AddBind(const char *pName, const char *pCommand)
{
	if((pName[0] == '\0' && pCommand[0] == '\0') || m_vBinds.size() >= BINDWHEEL_MAX_BINDS)
		return;

	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aCommand, pCommand);
	m_vBinds.push_back(Bind);
	m_ResolvedBinds.clear();
	m_State.InvalidateSelection();
}

void CBindWheel::RemoveBind(const char *pName, const char *pCommand)
{
	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aCommand, pCommand);
	auto Iter = std::find(m_vBinds.begin(), m_vBinds.end(), Bind);
	if(Iter != m_vBinds.end())
	{
		m_vBinds.erase(Iter);
		m_ResolvedBinds.clear();
		m_State.InvalidateSelection();
	}
}

bool CBindWheel::SetBind(int Index, const char *pName, const char *pCommand)
{
	if(Index < -1 || Index >= static_cast<int>(m_vBinds.size()) || pName[0] == '\0' || pCommand[0] == '\0' ||
		str_length(pName) >= BINDWHEEL_MAX_NAME || str_length(pCommand) >= BINDWHEEL_MAX_CMD ||
		(Index == -1 && m_vBinds.size() >= BINDWHEEL_MAX_BINDS))
		return false;
	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aCommand, pCommand);
	if(Index == -1)
		m_vBinds.push_back(Bind);
	else
		m_vBinds[Index] = Bind;
	m_ResolvedBinds.clear();
	m_State.InvalidateSelection();
	return true;
}

void CBindWheel::RemoveBind(int Index)
{
	if(Index >= static_cast<int>(m_vBinds.size()) || Index < 0)
		return;
	auto Pos = m_vBinds.begin() + Index;
	m_vBinds.erase(Pos);
	m_ResolvedBinds.clear();
	m_State.InvalidateSelection();
}

void CBindWheel::RemoveAllBinds()
{
	m_vBinds.clear();
	m_ResolvedBinds.clear();
	m_State.InvalidateSelection();
}

void CBindWheel::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::QMCLIENT);

	Console()->Register("+bindwheel", "", CFGFLAG_CLIENT, ConOpenBindwheel, this, "Open the custom bind menu");
	Console()->Register("+bindmenu", "s[menu]", CFGFLAG_CLIENT, ConOpenBindMenu, this, "Open a bind menu (identity, chat, assets, custom)");
	Console()->Register("+bindwheel_execute_hover", "", CFGFLAG_CLIENT, ConBindwheelExecuteHover, this, "Execute hovered bindwheel bind");

	Console()->Register("bindwheel", "i[index] s[name] s[command]", CFGFLAG_CLIENT, ConAddBindwheelLegacy, this, "DONT USE THIS! USE add_bindwheel INSTEAD!");
	Console()->Register("add_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConAddBindwheel, this, "Add a bind to the bindwheel");
	Console()->Register("remove_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConRemoveBindwheel, this, "Remove a bind from the bindwheel");
	Console()->Register("delete_all_bindwheel_binds", "", CFGFLAG_CLIENT, ConRemoveAllBindwheelBinds, this, "Removes all bindwheel binds");
}

void CBindWheel::OnReset()
{
	m_State.Cancel();
	m_AnimationTime = 0.0f;
	m_SelectedBind = -1;
	m_ListScroll = 0;
	m_JustOpened = false;
	m_LastLayout = {};
	m_vVisibleBindIndices.clear();
	m_ResolvedBinds.clear();
}

void CBindWheel::OnRelease()
{
	m_State.Cancel();
	if(g_Config.m_QmResetBindWheelMouse && !GameClient()->m_Emoticon.IsActive())
		GameClient()->m_Emoticon.m_SelectorMouse = vec2(0.0f, 0.0f);
}

bool CBindWheel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;

	// 轮盘与面板共用游戏内灵敏度，避免同一按键在两个布局下手感不同。
	const float Factor = CursorType == IInput::CURSOR_JOYSTICK ? g_Config.m_InpControllerSens / 100.0f :
								     g_Config.m_InpMousesens / 100.0f;
	x *= Factor;
	y *= Factor;
	GameClient()->m_Emoticon.m_SelectorMouse += vec2(x, y);
	return true;
}

bool CBindWheel::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;
	// 释放事件必须继续下发，触发键的 "-bindmenu" 才能到达按键系统。
	if(Event.m_Flags & IInput::FLAG_RELEASE)
		return false;
	// 按下事件不能在这里截断：绑定分发位于输入栈末尾，吞掉它会让同一帧的
	// "+emote" 等浮层按键完全收不到按下。游戏操作由 Controls::SnapInput
	// 在 BindWheel::IsActive() 时统一归零并标记 PLAYERFLAG_IN_MENU。
	if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
	{
		if((EQmBindWheelLayout)g_Config.m_QmBindWheelLayout == EQmBindWheelLayout::LIST)
		{
			const int Direction = Event.m_Key == KEY_MOUSE_WHEEL_UP ? -1 : 1;
			m_ListScroll = std::max(0, m_ListScroll + Direction * ui_token::bind_menu::PANEL_SCROLL_ROWS_PER_STEP);
			ClampListScroll();
		}
		return true;
	}
	return false;
}

void CBindWheel::OpenMenu(qm_bind_menu::EMenu Menu)
{
	const bool WasOpen = IsActive() && m_State.Menu() == Menu;
	m_State.Open(Menu);
	RebuildVisibleBinds();
	if(!WasOpen)
	{
		m_ListScroll = 0;
		m_JustOpened = true;
		m_AnimationTime = 0.0f;
	}
}

void CBindWheel::ReleaseActiveMenu()
{
	if(!IsActive())
		return;
	m_State.ReleaseActive(CurrentSourceIndex());
}

const CBindWheel::SResolvedBind &CBindWheel::ResolveBind(int Index)
{
	static const SResolvedBind s_Empty;
	if(Index < 0 || Index >= (int)m_vBinds.size())
		return s_Empty;

	const std::string Command = m_vBinds[Index].m_aCommand;
	const auto Cached = m_ResolvedBinds.find(Command);
	if(Cached != m_ResolvedBinds.end())
		return Cached->second;

	SResolvedBind Resolved;
	if(const auto Preset = qm_wheel_presets::ParseCommand(Command))
	{
		Resolved.m_Menu = qm_wheel_presets::MenuForType(Preset->m_Type);
		Resolved.m_Preset = Preset;
	}
	else if(const auto Path = qm_wheel_presets::ExecPath(Command))
	{
		Resolved.m_LegacyScript = true;
		if(const auto Text = ReadExecConfig(Path->c_str()))
		{
			if(const auto Legacy = qm_wheel_presets::ParseLegacyConfig(*Text))
			{
				Resolved.m_Menu = qm_wheel_presets::MenuForType(Legacy->m_Type);
				Resolved.m_Preset = Legacy;
			}
		}
	}
	return m_ResolvedBinds.emplace(Command, std::move(Resolved)).first->second;
}

std::optional<std::string> CBindWheel::ReadExecConfig(const char *pPath)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return std::nullopt;
	std::vector<std::string> vCandidates{pPath};
	const std::string Path = pPath;
	if(Path.size() > 6 && str_comp_nocase_num(Path.c_str(), "Binds\\", 6) == 0)
		vCandidates.push_back("Bind" + Path.substr(5));
	else if(Path.size() > 6 && str_comp_nocase_num(Path.c_str(), "Binds/", 6) == 0)
		vCandidates.push_back("Bind" + Path.substr(5));
	for(const std::string &Candidate : vCandidates)
	{
		std::string Text;
		if(QmReadFileBounded(Storage()->OpenFile(Candidate.c_str(), IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE), 256 * 1024, Text))
			return Text;
	}
	return std::nullopt;
}

qm_bind_menu::EMenu CBindWheel::MenuForBind(int Index)
{
	return ResolveBind(Index).m_Menu;
}

const qm_wheel_presets::SPreset *CBindWheel::PresetForBind(int Index)
{
	const auto &Resolved = ResolveBind(Index);
	return Resolved.m_Preset ? &*Resolved.m_Preset : nullptr;
}

void CBindWheel::RebuildVisibleBinds()
{
	m_vVisibleBindIndices.clear();
	for(int Index = 0; Index < static_cast<int>(m_vBinds.size()); ++Index)
	{
		if(MenuForBind(Index) == m_State.Menu())
			m_vVisibleBindIndices.push_back(Index);
	}
}

void CBindWheel::ClampListScroll()
{
	const int Count = static_cast<int>(m_vVisibleBindIndices.size());
	const int VisibleRows = std::max(1, m_LastLayout.m_VisibleRows);
	m_ListScroll = std::clamp(m_ListScroll, 0, std::max(0, Count - VisibleRows));
}

int CBindWheel::CurrentSelection() const
{
	const CUIRect Screen = *Ui()->Screen();
	const vec2 Mouse = Screen.Center() + GameClient()->m_Emoticon.m_SelectorMouse;
	return qm_bind_menu_preview::HitTest(m_LastLayout, Mouse, (int)m_vVisibleBindIndices.size(), m_ListScroll);
}

int CBindWheel::CurrentSourceIndex() const
{
	const int Selected = CurrentSelection();
	if(Selected < 0 || Selected >= (int)m_vVisibleBindIndices.size())
		return -1;
	return m_vVisibleBindIndices[Selected];
}

void CBindWheel::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(IsActive() || m_AnimationTime > 0.0f)
			OnReset();
		else
			m_State.Cancel();
		return;
	}
	// 其它浮层接管输入时立即收尾，避免触发键在浮层切换或窗口失焦时
	// 没有收到释放事件而把 BindWheel 永久留在 Active 状态。
	const bool CompetingOverlay = GameClient()->m_Emoticon.IsActive() || GameClient()->m_Menus.IsActive() ||
				      GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() ||
				      GameClient()->m_PieMenu.IsActive() || GameClient()->m_KeyBinder.IsActive();
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	if(CompetingOverlay || !WindowActive)
	{
		if(IsActive() || m_AnimationTime > 0.0f)
			OnReset();
		else
			m_State.Cancel();
		return;
	}

	RebuildVisibleBinds();
	const auto PendingBind = m_State.TakePendingBind();
	if(PendingBind.has_value())
	{
		if(g_Config.m_QmResetBindWheelMouse)
			GameClient()->m_Emoticon.m_SelectorMouse = vec2(0.0f, 0.0f);
		if(*PendingBind >= 0)
			ExecuteBind(*PendingBind);
	}

	static const auto QuadEaseInOut = [](float t) -> float {
		if(t == 0.0f)
			return 0.0f;
		if(t == 1.0f)
			return 1.0f;
		return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
	};

	const float AnimationTime = (float)g_Config.m_QmAnimateWheelTime / 1000.0f;
	if(!IsActive() && m_AnimationTime <= 0.0f)
		return;

	if(!IsActive())
	{
		if(AnimationTime == 0.0f)
			return;
		m_AnimationTime -= Client()->RenderFrameTime() * 3.0f;
		if(m_AnimationTime <= 0.0f)
		{
			m_AnimationTime = 0.0f;
			return;
		}
	}
	else
	{
		m_AnimationTime += Client()->RenderFrameTime();
		if(m_AnimationTime > AnimationTime)
			m_AnimationTime = AnimationTime;
	}

	const CUIRect Screen = *Ui()->Screen();
	const EQmBindWheelLayout WheelLayout = (EQmBindWheelLayout)g_Config.m_QmBindWheelLayout;
	const qm_bind_menu::ELayout LayoutKind = WheelLayout == EQmBindWheelLayout::LIST ? qm_bind_menu::ELayout::PANEL : qm_bind_menu::ELayout::WHEEL;
	const float UiScale = std::max(0.75f, ResolveSettingsUiScale(Screen.w));
	const int OptionCount = (int)m_vVisibleBindIndices.size();

	const bool WasTouchPressed = GameClient()->m_Emoticon.m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(GameClient()->m_Emoticon.m_TouchState, false);
	if(GameClient()->m_Emoticon.m_TouchState.m_AnyPressed)
	{
		GameClient()->m_Emoticon.m_SelectorMouse = (GameClient()->m_Emoticon.m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
	}
	else if(WasTouchPressed)
	{
		if(IsActive())
			m_State.TriggerRelease(m_State.Menu(), CurrentSourceIndex());
	}

	const float Reveal = AnimationTime == 0.0f ? 1.0f : QuadEaseInOut(m_AnimationTime / AnimationTime);

	if(WheelLayout == EQmBindWheelLayout::WHEEL)
	{
		const float MouseLimit = std::max(1.0f, std::min(Screen.w, Screen.h) * 0.48f);
		if(length(GameClient()->m_Emoticon.m_SelectorMouse) > MouseLimit)
			GameClient()->m_Emoticon.m_SelectorMouse = normalize(GameClient()->m_Emoticon.m_SelectorMouse) * MouseLimit;
	}
	else
	{
		// 面板仍然可以移出面板外取消，但不能让光标甩出屏幕后找不回来。
		const float Margin = std::max(1.0f, UiScale * ui_token::spacing::SM);
		const vec2 Limit = Screen.Size() * 0.5f - vec2(Margin, Margin);
		GameClient()->m_Emoticon.m_SelectorMouse.x = std::clamp(GameClient()->m_Emoticon.m_SelectorMouse.x, -Limit.x, Limit.x);
		GameClient()->m_Emoticon.m_SelectorMouse.y = std::clamp(GameClient()->m_Emoticon.m_SelectorMouse.y, -Limit.y, Limit.y);
	}

	m_LastLayout = qm_bind_menu_preview::BuildLayout(Screen, LayoutKind, OptionCount, m_ListScroll, UiScale, true, m_State.Menu());
	ClampListScroll();
	m_LastLayout = qm_bind_menu_preview::BuildLayout(Screen, LayoutKind, OptionCount, m_ListScroll, UiScale, true, m_State.Menu());
	if(IsActive() && m_JustOpened && LayoutKind == qm_bind_menu::ELayout::PANEL)
		// 打开瞬间把光标放在标题区而不是条目区：直接点按触发键松开只关闭，不会误应用中间条目。
		GameClient()->m_Emoticon.m_SelectorMouse = m_LastLayout.m_Title.Center() - Screen.Center();

	const vec2 Mouse = Screen.Center() + GameClient()->m_Emoticon.m_SelectorMouse;
	m_SelectedBind = qm_bind_menu_preview::HitTest(m_LastLayout, Mouse, OptionCount, m_ListScroll);

	if(IsActive() && m_JustOpened)
		m_JustOpened = false;
	else if(IsActive() && Input()->KeyPress(KEY_MOUSE_1))
	{
		if(m_SelectedBind >= 0)
			m_State.ApplyClick(CurrentSourceIndex());
		else
			m_State.Cancel();
	}

	std::vector<qm_bind_menu_preview::SOption> vOptions;
	vOptions.reserve(m_vVisibleBindIndices.size());
	for(const int SourceIndex : m_vVisibleBindIndices)
	{
		const auto &Bind = m_vBinds[SourceIndex];
		vOptions.push_back({Bind.m_aName, Bind.m_aCommand, PresetForBind(SourceIndex), ResolveBind(SourceIndex).m_LegacyScript});
	}

	if(IsActive() || m_AnimationTime > 0.0f)
	{
		qm_bind_menu_preview::Render(Ui(), GameClient(), m_LastLayout, m_State.Menu(), vOptions.data(), (int)vOptions.size(),
			m_SelectedBind, m_ListScroll, UiScale, Reveal);
	}

	RenderTools()->RenderCursor(Screen.Center() + GameClient()->m_Emoticon.m_SelectorMouse, 24.0f, Reveal);
}

void CBindWheel::ExecuteBind(int Bind)
{
	if(Bind < 0 || Bind >= static_cast<int>(m_vBinds.size()))
		return;
	// 命令可以删除自己的条目，执行期间不能借用 vector 内的字符串。
	char aCommand[BINDWHEEL_MAX_CMD];
	str_copy(aCommand, m_vBinds[Bind].m_aCommand);
	Console()->ExecuteLine(aCommand);
}

void CBindWheel::ExecuteHoveredBind()
{
	if(IsActive())
		ExecuteBind(CurrentSourceIndex());
}

void CBindWheel::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CBindWheel *pThis = (CBindWheel *)pUserData;

	for(const CBind &Bind : pThis->m_vBinds)
	{
		const std::string Line = qm_bind_editor::WheelConfigLine(Bind.m_aName, Bind.m_aCommand);
		pConfigManager->WriteLine(Line.c_str(), ConfigDomain::QMCLIENT);
	}
}
