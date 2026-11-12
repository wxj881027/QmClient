#include "bindwheel.h"

#include <base/system.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>

#include <generated/protocol.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/ghost.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/practice_menu.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>

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
	if(pThis->Client()->State() != IClient::STATE_ONLINE || pThis->GameClient()->m_Emoticon.IsActive())
	{
		pThis->OnRelease();
		return;
	}
	if(pResult->GetInteger(0) != 0)
	{
		if(!pThis->IsActive())
		{
			pThis->GameClient()->m_Emoticon.m_TouchPressedOutside = false;
			pThis->RebuildMenuPages();
		}
		pThis->m_State.Open();
	}
	else
		pThis->m_State.Release(pThis->CurrentSelection());
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
	m_State.InvalidateSelection();
	return true;
}

void CBindWheel::RemoveBind(int Index)
{
	if(Index >= static_cast<int>(m_vBinds.size()) || Index < 0)
		return;
	auto Pos = m_vBinds.begin() + Index;
	m_vBinds.erase(Pos);
	m_State.InvalidateSelection();
}

void CBindWheel::RemoveAllBinds()
{
	m_vBinds.clear();
	m_State.InvalidateSelection();
}

void CBindWheel::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::QMCLIENT);

	Console()->Register("+bindwheel", "", CFGFLAG_CLIENT, ConOpenBindwheel, this, "Open bindwheel selector");
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
	std::fill(std::begin(m_aAnimationTimeItems), std::end(m_aAnimationTimeItems), 0.0f);
	m_SelectedBind = -1;
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

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	GameClient()->m_Emoticon.m_SelectorMouse += vec2(x, y);
	return true;
}

bool CBindWheel::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;
	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			OnRelease();
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			const int Delta = Event.m_Key == KEY_MOUSE_WHEEL_UP ? -1 : 1;
			if((EQmBindWheelLayout)g_Config.m_QmBindWheelLayout != EQmBindWheelLayout::LIST)
			{
				SwitchPage(m_CurrentPage + Delta);
				return true;
			}
			// 面板布局：滚轮在页签上切页，在条目区滚动列表。
			const CUIRect Screen = *Ui()->Screen();
			const vec2 Mouse = vec2(Screen.w, Screen.h) / 2.0f + GameClient()->m_Emoticon.m_SelectorMouse;
			if(HitTestPage(Mouse) >= 0)
				SwitchPage(m_CurrentPage + Delta);
			else
			{
				m_ListScroll += Delta;
				ClampListScroll();
			}
			return true;
		}
	}
	return false;
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
	if(GameClient()->m_Emoticon.IsActive())
	{
		OnReset();
		return;
	}
	const auto PendingBind = m_State.TakePendingBind();
	if(PendingBind.has_value())
	{
		if(g_Config.m_QmResetBindWheelMouse)
			GameClient()->m_Emoticon.m_SelectorMouse = vec2(0.0f, 0.0f);
		ExecuteBind(*PendingBind);
	}

	static const auto QuadEaseInOut = [](float t) -> float {
		if(t == 0.0f)
			return 0.0f;
		if(t == 1.0f)
			return 1.0f;
		return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
	};
	static const float s_OuterMouseLimitRadius = 170.0f;
	static const float s_OuterItemRadius = 140.0f; // 10.0f less than emoticons for extra text space
	static const float s_OuterCircleRadius = 190.0f;
	// static const float s_InnerCircleRadius = 100.0f;
	static const float s_FontSize = 12.0f;
	static const float s_FontSizeSelected = 18.0f;

	const float AnimationTime = (float)g_Config.m_QmAnimateWheelTime / 1000.0f;
	const float ItemAnimationTime = AnimationTime / 2.0f;
	if(!IsActive() && m_AnimationTime <= 0.0f)
		return;

	if(AnimationTime != 0.0f)
	{
		for(float &Time : m_aAnimationTimeItems)
			Time = std::max(0.0f, Time - Client()->RenderFrameTime());
	}

	if(!IsActive())
	{
		if(AnimationTime == 0.0f)
			return;

		m_AnimationTime -= Client()->RenderFrameTime() * 3.0f; // Close animation 3x faster
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
	const EQmBindWheelLayout Layout = (EQmBindWheelLayout)g_Config.m_QmBindWheelLayout;

	const bool WasTouchPressed = GameClient()->m_Emoticon.m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(GameClient()->m_Emoticon.m_TouchState, false);
	if(GameClient()->m_Emoticon.m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (GameClient()->m_Emoticon.m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
		if(Layout == EQmBindWheelLayout::LIST)
		{
			// 面板布局用绝对位置命中条目，不再按轮盘半径限制。
			GameClient()->m_Emoticon.m_SelectorMouse = TouchPos;
		}
		else
		{
			const float TouchCenterDistance = length(TouchPos);
			if(TouchCenterDistance <= s_OuterMouseLimitRadius)
			{
				GameClient()->m_Emoticon.m_SelectorMouse = TouchPos;
			}
			else if(TouchCenterDistance > s_OuterCircleRadius)
			{
				GameClient()->m_Emoticon.m_TouchPressedOutside = true;
			}
		}
	}
	else if(WasTouchPressed)
	{
		if(Layout == EQmBindWheelLayout::WHEEL && GameClient()->m_Emoticon.m_TouchPressedOutside)
			OnRelease();
		else
			m_State.Release(CurrentSelection());
	}

	std::array<float, 2> aAnimationPhase;
	if(AnimationTime == 0.0f)
	{
		aAnimationPhase.fill(1.0f);
	}
	else
	{
		aAnimationPhase[0] = QuadEaseInOut(m_AnimationTime / AnimationTime);
		aAnimationPhase[1] = aAnimationPhase[0] * aAnimationPhase[0];
	}

	if(Layout == EQmBindWheelLayout::WHEEL &&
		length(GameClient()->m_Emoticon.m_SelectorMouse) > s_OuterMouseLimitRadius)
		GameClient()->m_Emoticon.m_SelectorMouse = normalize(GameClient()->m_Emoticon.m_SelectorMouse) * s_OuterMouseLimitRadius;

	m_SelectedBind = CurrentSelection();

	// 内置页条目数不再受用户绑定上限约束（影子页会随磁盘上的组增长），
	// 动画时间数组仍是定长的，越界索引要挡住。
	if(m_SelectedBind >= 0 && m_SelectedBind < BINDWHEEL_MAX_BINDS)
	{
		m_aAnimationTimeItems[m_SelectedBind] += Client()->RenderFrameTime() * 2.0f; // To counteract earlier decrement
		if(m_aAnimationTimeItems[m_SelectedBind] >= ItemAnimationTime)
			m_aAnimationTimeItems[m_SelectedBind] = ItemAnimationTime;
	}

	if(Layout == EQmBindWheelLayout::LIST)
	{
		RenderListLayout(Screen.w, Screen.h, aAnimationPhase[0]);
	}
	else
	{
		const auto &Binds = CurrentBinds();
		Ui()->MapScreen();

		Graphics()->BlendNormal();
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f * aAnimationPhase[0]);
		Graphics()->DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_OuterCircleRadius * aAnimationPhase[0], 64);
		Graphics()->QuadsEnd();

		Graphics()->WrapClamp();
		const float Theta = pi * 2.0f / std::max<float>(1.0f, Binds.size()); // Prevent divide by 0
		for(int i = 0; i < static_cast<int>(Binds.size()); i++)
		{
			const CBind &Bind = Binds[i];
			const float Angle = Theta * i;
			const float ItemTime = i < BINDWHEEL_MAX_BINDS ? m_aAnimationTimeItems[i] : 0.0f;
			const float Phase = ItemAnimationTime == 0.0f ? (i == m_SelectedBind ? 1.0f : 0.0f) : QuadEaseInOut(ItemTime / ItemAnimationTime);
			const float FontSize = (s_FontSize + Phase * (s_FontSizeSelected - s_FontSize)) * aAnimationPhase[1];
			const char *pName = Bind.m_aName;
			if(pName[0] == '\0')
			{
				pName = "Empty";
				TextRender()->TextColor(0.7f, 0.7f, 0.7f, aAnimationPhase[1]);
			}
			else
			{
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, aAnimationPhase[1]);
			}
			const vec2 Pos = vec2(Screen.x, Screen.y) + vec2(Screen.w, Screen.h) / 2.0f + direction(Angle) * s_OuterItemRadius * aAnimationPhase[1];
			const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
			Ui()->DoLabel(&Rect, pName, FontSize, TEXTALIGN_MC);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Graphics()->WrapNormal();
	}

	// For future middle circle usage
	// Graphics()->TextureClear();
	// Graphics()->QuadsBegin();
	// Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.3f * AnimationPhase3);
	// DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_InnerCircleRadius * AnimationPhase3, 64);
	// Graphics()->QuadsEnd();

	RenderTools()->RenderCursor(GameClient()->m_Emoticon.m_SelectorMouse + vec2(Screen.w, Screen.h) / 2.0f, 24.0f, aAnimationPhase[0]);
}

void CBindWheel::ExecuteBind(int Bind)
{
	const auto &Binds = CurrentBinds();
	if(Bind < 0 || Bind >= static_cast<int>(Binds.size()))
		return;
	// 命令可以删除自己的条目，执行期间不能借用 vector 内的字符串。
	char aCommand[BINDWHEEL_MAX_CMD];
	str_copy(aCommand, Binds[Bind].m_aCommand);
	Console()->ExecuteLine(aCommand);
}
void CBindWheel::ExecuteHoveredBind()
{
	if(IsActive())
		ExecuteBind(CurrentSelection());
}

void CBindWheel::RebuildMenuPages()
{
	// 内置页只在打开菜单时按上下文生成，不写入配置：快速练习优先，其次是普通练习队伍。
	m_vBuiltinPages.clear();
	int ContextPage = 0;
	if(GameClient()->m_FastPractice.Enabled())
	{
		SPage Page;
		str_copy(Page.m_aName, Localizable("Fast practice"));
		for(const auto &Entry : qm_practice_menu::s_aFastPracticeEntries)
		{
			CBind Bind;
			str_copy(Bind.m_aName, Entry.m_pName);
			str_copy(Bind.m_aCommand, Entry.m_pCommand);
			Page.m_vBinds.push_back(Bind);
		}
		m_vBuiltinPages.push_back(Page);
		ContextPage = 1;
	}
	else if(InNormalPractice())
	{
		SPage Page;
		str_copy(Page.m_aName, Localizable("Practice"));
		for(const auto &Entry : qm_practice_menu::s_aPracticeEntries)
		{
			CBind Bind;
			str_copy(Bind.m_aName, Entry.m_pName);
			str_copy(Bind.m_aCommand, Entry.m_pCommand);
			Page.m_vBinds.push_back(Bind);
		}
		m_vBuiltinPages.push_back(Page);
		ContextPage = 1;
	}
	// 已保存的练习影子组：直接给"播放"入口，不必先去影子页翻目录。
	// 与是否处于快速练习无关——录下来的线路退出练习后仍然值得回看。
	std::vector<CGhost::SPracticeGroup> vGroups;
	GameClient()->m_Ghost.ListPracticeGroups(vGroups);
	if(!vGroups.empty())
	{
		SPage Page;
		str_copy(Page.m_aName, Localizable("Practice ghosts"));
		for(const auto &Group : vGroups)
		{
			char aTime[32];
			str_time(std::max(Group.m_Time, 0) / 10, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			CBind Bind;
			str_format(Bind.m_aName, sizeof(Bind.m_aName), "%s %s", Group.m_aOwner, aTime);
			str_format(Bind.m_aCommand, sizeof(Bind.m_aCommand), "practice_view_ghost \"%s\"", Group.m_aDir);
			Page.m_vBinds.push_back(Bind);
		}
		m_vBuiltinPages.push_back(Page);
	}
	SwitchPage(ContextPage);
}

bool CBindWheel::InNormalPractice() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return false;
	const auto &Character = GameClient()->m_Snap.m_aCharacters[LocalId];
	return Character.m_HasExtendedDisplayInfo && (Character.m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) != 0;
}

void CBindWheel::SwitchPage(int Index)
{
	const int Pages = PageCount();
	if(Pages <= 0)
		return;
	m_CurrentPage = ((Index % Pages) + Pages) % Pages;
	m_ListScroll = 0;
	m_State.InvalidateSelection();
}

void CBindWheel::ClampListScroll()
{
	const int Count = static_cast<int>(CurrentBinds().size());
	const int MaxScroll = std::max(0, Count - std::max(1, m_ListVisibleRows));
	m_ListScroll = std::clamp(m_ListScroll, 0, MaxScroll);
}

const std::vector<CBindWheel::CBind> &CBindWheel::CurrentBinds() const
{
	if(m_CurrentPage <= 0 || m_CurrentPage > static_cast<int>(m_vBuiltinPages.size()))
		return m_vBinds;
	return m_vBuiltinPages[m_CurrentPage - 1].m_vBinds;
}

const char *CBindWheel::PageName(int Index) const
{
	if(Index <= 0)
		return Localizable("My binds");
	if(Index > static_cast<int>(m_vBuiltinPages.size()))
		return "";
	return m_vBuiltinPages[Index - 1].m_aName;
}

int CBindWheel::CurrentSelection() const
{
	const int Count = static_cast<int>(CurrentBinds().size());
	if((EQmBindWheelLayout)g_Config.m_QmBindWheelLayout == EQmBindWheelLayout::WHEEL)
		return CQmBindWheelState::SelectedBind(GameClient()->m_Emoticon.m_SelectorMouse, Count);
	const CUIRect Screen = *Ui()->Screen();
	const vec2 Mouse = vec2(Screen.w, Screen.h) / 2.0f + GameClient()->m_Emoticon.m_SelectorMouse;
	return CQmBindWheelState::SelectedListBind(Mouse, m_ListRowsLeft, m_ListRowsTop, m_ListRowsWidth, m_ListRowHeight, Count, m_ListScroll);
}

int CBindWheel::HitTestPage(vec2 Mouse) const
{
	if(m_TabsWidth <= 0.0f || m_TabsHeight <= 0.0f)
		return -1;
	if(Mouse.y < m_TabsTop || Mouse.y > m_TabsTop + m_TabsHeight)
		return -1;
	if(Mouse.x < m_TabsLeft || Mouse.x > m_TabsLeft + m_TabsWidth)
		return -1;
	const int Pages = PageCount();
	const int Index = static_cast<int>((Mouse.x - m_TabsLeft) / (m_TabsWidth / Pages));
	return Index >= 0 && Index < Pages ? Index : -1;
}

void CBindWheel::RenderListLayout(float ScreenW, float ScreenH, float Alpha)
{
	const auto &Binds = CurrentBinds();
	const int Count = static_cast<int>(Binds.size());
	const int Pages = std::max(1, PageCount());
	const bool BuiltinPage = m_CurrentPage > 0;

	const float PanelWidth = std::min(ScreenW * 0.46f, 520.0f);
	const float Padding = ui_token::spacing::MD;
	const float HeaderHeight = 40.0f;
	const float TabsHeight = Pages > 1 ? 24.0f : 0.0f;
	const float RowHeight = 28.0f;
	const float MaxHeight = ScreenH * 0.82f;
	const int MaxRows = std::max(1, static_cast<int>((MaxHeight - HeaderHeight - TabsHeight - Padding * 2.0f) / RowHeight));
	const int VisibleRows = std::min(std::max(1, Count), MaxRows);
	m_ListVisibleRows = VisibleRows;
	ClampListScroll();

	const float RowsHeight = RowHeight * VisibleRows;
	const float PanelHeight = HeaderHeight + TabsHeight + RowsHeight + Padding * 2.0f;
	const CUIRect Panel = CUIRect{(ScreenW - PanelWidth) * 0.5f, (ScreenH - PanelHeight) * 0.5f, PanelWidth, PanelHeight};

	Ui()->MapScreen();

	SRoundedSurfaceParams PanelParams;
	PanelParams.m_Radius = ui_token::radius::CARD;
	DrawRoundedSurface(Graphics(), Panel, ColorRGBA(0.05f, 0.05f, 0.07f, 0.82f * Alpha),
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Alpha), PanelParams);

	CUIRect HeaderRect = Panel;
	HeaderRect.h = HeaderHeight;
	HeaderRect.y += Padding * 0.5f;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	Ui()->DoLabel(&HeaderRect, Localize(PageName(m_CurrentPage)), 14.0f, TEXTALIGN_MC);

	if(Pages > 1)
	{
		CUIRect TabsRect = Panel;
		TabsRect.y += HeaderHeight;
		TabsRect.h = TabsHeight;
		TabsRect.x += Padding;
		TabsRect.w -= Padding * 2.0f;
		m_TabsLeft = TabsRect.x;
		m_TabsTop = TabsRect.y;
		m_TabsWidth = TabsRect.w;
		m_TabsHeight = TabsRect.h;
		const float TabWidth = TabsRect.w / Pages;
		for(int i = 0; i < Pages; i++)
		{
			const CUIRect Tab = CUIRect{TabsRect.x + TabWidth * i, TabsRect.y, TabWidth, TabsRect.h};
			if(i == m_CurrentPage)
			{
				CUIRect Pill = Tab;
				Pill.x += 4.0f;
				Pill.w -= 8.0f;
				Pill.y += 2.0f;
				Pill.h -= 4.0f;
				SRoundedSurfaceParams PillParams;
				PillParams.m_Radius = ui_token::radius::PILL;
				DrawRoundedSurface(Graphics(), Pill, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha), ColorRGBA(), PillParams);
			}
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, (i == m_CurrentPage ? 1.0f : 0.55f) * Alpha);
			Ui()->DoLabel(&Tab, Localize(PageName(i)), 9.0f, TEXTALIGN_MC);
		}
	}
	else
	{
		m_TabsWidth = 0.0f;
		m_TabsHeight = 0.0f;
	}

	const float RowsTop = Panel.y + HeaderHeight + TabsHeight + Padding * 0.5f;
	m_ListRowsLeft = Panel.x + Padding;
	m_ListRowsTop = RowsTop;
	m_ListRowsWidth = Panel.w - Padding * 2.0f;
	m_ListRowHeight = RowHeight;

	const vec2 Mouse = vec2(ScreenW, ScreenH) / 2.0f + GameClient()->m_Emoticon.m_SelectorMouse;
	m_SelectedBind = CQmBindWheelState::SelectedListBind(Mouse, m_ListRowsLeft, m_ListRowsTop, m_ListRowsWidth, RowHeight, Count, m_ListScroll);

	for(int i = m_ListScroll; i < std::min(Count, m_ListScroll + VisibleRows); i++)
	{
		const CBind &Bind = Binds[i];
		const int Visible = i - m_ListScroll;
		const CUIRect Row = CUIRect{m_ListRowsLeft, RowsTop + Visible * RowHeight, m_ListRowsWidth, RowHeight};
		if(i == m_SelectedBind)
		{
			CUIRect Highlight = Row;
			Highlight.x += 2.0f;
			Highlight.w -= 4.0f;
			Highlight.y += 1.0f;
			Highlight.h -= 2.0f;
			SRoundedSurfaceParams HighlightParams;
			HighlightParams.m_Radius = ui_token::radius::BASE;
			DrawRoundedSurface(Graphics(), Highlight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha), ColorRGBA(), HighlightParams);
		}

		CUIRect NameRect = Row;
		NameRect.x += 10.0f;
		NameRect.w -= 140.0f;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
		Ui()->DoLabel(&NameRect, BuiltinPage ? Localize(Bind.m_aName) : Bind.m_aName, 11.0f, TEXTALIGN_ML);

		char aKey[128];
		GameClient()->m_Binds.GetKey(Bind.m_aCommand, aKey, sizeof(aKey));
		CUIRect KeyRect = Row;
		KeyRect.x = Row.x + Row.w - 120.0f;
		KeyRect.w = 110.0f;
		TextRender()->TextColor(0.75f, 0.75f, 0.75f, Alpha);
		Ui()->DoLabel(&KeyRect, aKey[0] ? aKey : "—", 10.0f, TEXTALIGN_MR);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	if(Count > VisibleRows)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d/%d", m_ListScroll + 1, Count);
		CUIRect ScrollRect = Panel;
		ScrollRect.h = 14.0f;
		ScrollRect.y = Panel.y + Panel.h - 14.0f;
		TextRender()->TextColor(0.7f, 0.7f, 0.7f, Alpha);
		Ui()->DoLabel(&ScrollRect, aBuf, 8.0f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
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
