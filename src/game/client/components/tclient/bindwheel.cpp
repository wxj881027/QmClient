#include "bindwheel.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

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
			pThis->GameClient()->m_Emoticon.m_TouchPressedOutside = false;
		pThis->m_State.Open();
	}
	else
		pThis->m_State.Release(CQmBindWheelState::SelectedBind(pThis->GameClient()->m_Emoticon.m_SelectorMouse, pThis->m_vBinds.size()));
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
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		return true;
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

	const bool WasTouchPressed = GameClient()->m_Emoticon.m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(GameClient()->m_Emoticon.m_TouchState);
	if(GameClient()->m_Emoticon.m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (GameClient()->m_Emoticon.m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
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
	else if(WasTouchPressed)
	{
		if(GameClient()->m_Emoticon.m_TouchPressedOutside)
			OnRelease();
		else
			m_State.Release(CQmBindWheelState::SelectedBind(GameClient()->m_Emoticon.m_SelectorMouse, m_vBinds.size()));
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

	if(length(GameClient()->m_Emoticon.m_SelectorMouse) > s_OuterMouseLimitRadius)
		GameClient()->m_Emoticon.m_SelectorMouse = normalize(GameClient()->m_Emoticon.m_SelectorMouse) * s_OuterMouseLimitRadius;

	m_SelectedBind = CQmBindWheelState::SelectedBind(GameClient()->m_Emoticon.m_SelectorMouse, m_vBinds.size());

	if(m_SelectedBind != -1)
	{
		m_aAnimationTimeItems[m_SelectedBind] += Client()->RenderFrameTime() * 2.0f; // To counteract earlier decrement
		if(m_aAnimationTimeItems[m_SelectedBind] >= ItemAnimationTime)
			m_aAnimationTimeItems[m_SelectedBind] = ItemAnimationTime;
	}

	Ui()->MapScreen();

	Graphics()->BlendNormal();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f * aAnimationPhase[0]);
	Graphics()->DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_OuterCircleRadius * aAnimationPhase[0], 64);
	Graphics()->QuadsEnd();

	Graphics()->WrapClamp();
	const float Theta = pi * 2.0f / std::max<float>(1.0f, m_vBinds.size()); // Prevent divide by 0
	for(int i = 0; i < static_cast<int>(m_vBinds.size()); i++)
	{
		const CBind &Bind = m_vBinds[i];
		const float Angle = Theta * i;
		const float Phase = ItemAnimationTime == 0.0f ? (i == m_SelectedBind ? 1.0f : 0.0f) : QuadEaseInOut(m_aAnimationTimeItems[i] / ItemAnimationTime);
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
		ExecuteBind(CQmBindWheelState::SelectedBind(GameClient()->m_Emoticon.m_SelectorMouse, m_vBinds.size()));
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
