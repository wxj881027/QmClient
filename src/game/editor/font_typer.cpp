#include "font_typer.h"

#include "editor.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/keys.h>

#include <game/editor/editor_actions.h>

#include <algorithm>

using namespace std::chrono_literals;

void CFontTyper::CState::Reset()
{
	m_Active = false;
	m_TextIndex = ivec2(0, 0);
	m_LineStart = std::nullopt;
	m_pLastLayer = nullptr;
	m_TilesPlacedSinceActivate = 0;
}

bool CFontTyper::IsActive() const
{
	return Map()->m_FontTyperState.m_Active;
}

void CFontTyper::OnInit(CEditor *pEditor)
{
	CEditorComponent::OnInit(pEditor);

	m_CursorTextTexture = pEditor->Graphics()->LoadTexture("editor/cursor_text.png", IStorage::TYPE_ALL, 0);
}

void CFontTyper::SetTile(ivec2 Pos, unsigned char Index, const std::shared_ptr<CLayerTiles> &pLayer)
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	CTile Tile = {
		Index, // index
		0, // flags
		0, // skip
		0, // reserved
	};
	pLayer->SetTile(Pos.x, Pos.y, Tile);
	Map()->OnModify();
	State.m_TilesPlacedSinceActivate++;
}

void CFontTyper::PlaceTile(unsigned char Index, const std::shared_ptr<CLayerTiles> &pLayer)
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	// 光标位于右边界列时换行
	if(State.m_TextIndex.x == pLayer->m_Width)
	{
		if(Index == 0)
			return;
		State.m_TextIndex.x = State.m_LineStart.value_or(0);
		State.m_TextIndex.y++;

		// 角落情况：已到图层底部，把光标停在最后一行末尾
		if(State.m_TextIndex.y >= pLayer->m_Height)
		{
			State.m_TextIndex.x = pLayer->m_Width;
			State.m_TextIndex.y = pLayer->m_Height - 1;
			return;
		}
	}

	// 处理在行首左侧输入，同时记录尚未确定的行首
	if(Index != 0 && (!State.m_LineStart.has_value() || State.m_TextIndex.x < State.m_LineStart.value()))
		State.m_LineStart = State.m_TextIndex.x;
	SetTile(State.m_TextIndex, Index, pLayer);
	State.m_TextIndex.x++;
}

bool CFontTyper::OnInput(const IInput::CEvent &Event)
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	const std::shared_ptr<CLayerTiles> pLayer = SelectedTextLayer();
	if(!pLayer || (State.m_Active && State.m_pLastLayer != pLayer))
	{
		if(IsActive())
			TextModeOff();
		return false;
	}

	if(!IsActive())
	{
		if(Event.m_Key == KEY_T && Input()->ModifierIsPressed() && !Ui()->IsPopupOpen() && Editor()->m_Dialog == DIALOG_NONE)
		{
			if(pLayer->m_KnownTextModeLayer)
			{
				TextModeOn();
			}
			else
			{
				m_ConfirmActivatePopupContext.Reset();
				m_ConfirmActivatePopupContext.YesNoButtons();
				str_copy(m_ConfirmActivatePopupContext.m_aMessage, Localize("Enable text mode? Pressing letters and numbers on your keyboard will place tiles.", "Editor"));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &m_ConfirmActivatePopupContext);
			}
		}
		return false;
	}

	// only handle key down and not also key up
	if(!(Event.m_Flags & IInput::FLAG_PRESS))
		return false;

	State.ClampCursor(pLayer->m_Width, pLayer->m_Height);

	// 官方 39c675977：Ctrl+S 交给编辑器保存，不要写成字母 S
	if(Input()->ModifierIsPressed() && Input()->KeyIsPressed(KEY_S))
	{
		TextModeOff();
		return false;
	}

	// 官方 39c675977：Ctrl+V 粘贴剪贴板文本，超长内容改为提示而不是卡死
	if(Input()->ModifierIsPressed() && Input()->KeyIsPressed(KEY_V))
	{
		std::string Clipboard = Input()->GetClipboardText();
		if(!Clipboard.empty())
		{
			if(Clipboard.size() > 10000)
			{
				Editor()->ShowFileDialogError("The clipboard contains %" PRIzu " characters, please post it in chunks", Clipboard.size());
				return false;
			}
			str_sanitize(Clipboard.data());
			for(auto &Char : Clipboard)
			{
				if(Char == '\r')
					continue;
				if(Char == '\t')
					Char = ' ';

				// 空格
				if(Char == ' ')
				{
					PlaceTile(0, pLayer);
				}
				// 换行
				else if(Char == '\n')
				{
					if(State.m_TextIndex.y < pLayer->m_Height - 1)
					{
						State.m_TextIndex.y++;
						if(State.m_LineStart.has_value())
							State.m_TextIndex.x = State.m_LineStart.value();
					}
				}
				// 数字
				else if(str_isnum(Char))
				{
					if(Char == '0')
						PlaceTile(KEY_0 - KEY_1 + NUMBER_OFFSET, pLayer);
					else
						PlaceTile(Char - '1' + NUMBER_OFFSET, pLayer);
				}
				else
				{
					Char = str_uppercase(Char);
					if(Char >= 'A' && Char <= 'Z')
					{
						PlaceTile(Char - 'A' + LETTER_OFFSET, pLayer);
					}
				}
			}
		}
		return false;
	}

	// letters
	if(Event.m_Key >= KEY_A && Event.m_Key <= KEY_Z)
		PlaceTile(Event.m_Key - KEY_A + LETTER_OFFSET, pLayer);
	// numbers
	if(Event.m_Key >= KEY_1 && Event.m_Key <= KEY_0)
		PlaceTile(Event.m_Key - KEY_1 + NUMBER_OFFSET, pLayer);
	if(Event.m_Key >= KEY_KP_1 && Event.m_Key <= KEY_KP_0)
		PlaceTile(Event.m_Key - KEY_KP_1 + NUMBER_OFFSET, pLayer);

	// deletion
	if(Event.m_Key == KEY_BACKSPACE && State.m_TextIndex.x > 0)
	{
		State.m_TextIndex.x--;
		SetTile(State.m_TextIndex, 0, pLayer);
	}
	// 官方 39c675977：Delete 清除光标所在格的字符
	else if(Event.m_Key == KEY_DELETE)
	{
		if(State.m_TextIndex.x < pLayer->m_Width)
			SetTile(State.m_TextIndex, 0, pLayer);
	}
	// space
	if(Event.m_Key == KEY_SPACE)
		PlaceTile(0, pLayer);
	// newline
	if(Event.m_Key == KEY_RETURN)
	{
		State.m_TextIndex.y++;
		if(State.m_LineStart.has_value())
			State.m_TextIndex.x = State.m_LineStart.value();
	}

	// 官方 39c675977：Home/End 在行首行尾之间跳转
	if(Event.m_Key == KEY_HOME)
	{
		for(int StartIndex = State.m_LineStart.value_or(0); StartIndex < pLayer->m_Width; ++StartIndex)
		{
			State.m_TextIndex.x = StartIndex;
			if(pLayer->GetTile(StartIndex, State.m_TextIndex.y).m_Index != 0)
				break;
		}
		// 行内没有字符时回到行首
		if(pLayer->GetTile(State.m_TextIndex.x, State.m_TextIndex.y).m_Index == 0)
			State.m_TextIndex.x = State.m_LineStart.value_or(0);
	}
	else if(Event.m_Key == KEY_END)
	{
		int LastIndex = -1;
		for(int EndIndex = State.m_LineStart.value_or(0); EndIndex < pLayer->m_Width; ++EndIndex)
		{
			if(pLayer->GetTile(EndIndex, State.m_TextIndex.y).m_Index)
				LastIndex = EndIndex;
		}
		State.m_TextIndex.x = LastIndex >= 0 ? LastIndex + 1 : State.m_LineStart.value_or(0);
	}

	// arrow key navigation
	if(Event.m_Key == KEY_LEFT)
	{
		State.m_TextIndex.x--;
		if(Input()->ModifierIsPressed())
		{
			while(State.m_TextIndex.x >= 1 && State.m_TextIndex.x <= pLayer->m_Width - 2 && pLayer->GetTile(State.m_TextIndex.x, State.m_TextIndex.y).m_Index)
			{
				State.m_TextIndex.x--;
			}
		}
	}
	if(Event.m_Key == KEY_RIGHT)
	{
		State.m_TextIndex.x++;
		if(Input()->ModifierIsPressed())
		{
			while(State.m_TextIndex.x >= 1 && State.m_TextIndex.x <= pLayer->m_Width - 2 && pLayer->GetTile(State.m_TextIndex.x, State.m_TextIndex.y).m_Index)
			{
				State.m_TextIndex.x++;
			}
		}
	}
	if(Event.m_Key == KEY_UP)
		State.m_TextIndex.y--;
	if(Event.m_Key == KEY_DOWN)
		State.m_TextIndex.y++;
	State.ClampCursor(pLayer->m_Width, pLayer->m_Height);
	m_CursorRenderTime = time_get_nanoseconds() - 501ms;
	float Dist = distance(
		vec2(State.m_TextIndex.x, State.m_TextIndex.y),
		(Editor()->MapView()->GetWorldOffset() + Editor()->MapView()->GetEditorOffset()) / 32);
	Dist /= Editor()->MapView()->GetWorldZoom();
	if(Dist > 10.0f)
	{
		Editor()->MapView()->SetWorldOffset(vec2(State.m_TextIndex.x, State.m_TextIndex.y) * 32 - Editor()->MapView()->GetEditorOffset());
	}

	return false;
}

void CFontTyper::TextModeOn()
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	const std::shared_ptr<CLayerTiles> pLayer = SelectedTextLayer();
	if(!pLayer)
		return;

	State.SetCursorFromWorld(Editor()->MapView()->MouseWorldPos(), pLayer->m_Width, pLayer->m_Height);
	m_CursorRenderTime = time_get_nanoseconds() - 501ms;
	State.m_TilesPlacedSinceActivate = 0;
	State.m_pLastLayer = pLayer;
	State.m_Active = true;
	pLayer->m_KnownTextModeLayer = true;

	// hack to not show picker when pressing space
	Editor()->m_Dialog = DIALOG_PSEUDO_FONT_TYPER;
}

void CFontTyper::TextModeOff()
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	if(Editor()->m_Dialog == DIALOG_PSEUDO_FONT_TYPER)
		Editor()->m_Dialog = DIALOG_NONE;
	if(State.m_TilesPlacedSinceActivate)
	{
		const auto &vpGroups = Map()->m_vpGroups;
		const auto It = std::find_if(vpGroups.begin(), vpGroups.end(), [&](const auto &pGroup) {
			return std::find(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.end(), State.m_pLastLayer) != pGroup->m_vpLayers.end();
		});
		if(It != vpGroups.end())
			Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorBrushDrawAction>(Map(), It - vpGroups.begin()), Localize("Font typer", "Editor"));
	}
	State.Reset();
}

std::shared_ptr<CLayerTiles> CFontTyper::SelectedTextLayer() const
{
	auto pLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
	if(!pLayer || pLayer->m_Image == -1 || pLayer->m_Width <= 0 || pLayer->m_Height <= 0)
		return nullptr;
	return pLayer;
}

void CFontTyper::Render()
{
	CFontTyper::CState &State = Map()->m_FontTyperState;
	if(m_ConfirmActivatePopupContext.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
	{
		TextModeOn();
		m_ConfirmActivatePopupContext.Reset();
	}
	if(m_ConfirmActivatePopupContext.m_Result != CUi::SConfirmPopupContext::UNSET)
		m_ConfirmActivatePopupContext.Reset();

	if(!IsActive())
		return;

	const std::shared_ptr<CLayerTiles> pLayer = SelectedTextLayer();
	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || !pLayer || State.m_pLastLayer != pLayer ||
		Editor()->m_Dialog != DIALOG_PSEUDO_FONT_TYPER || CLineInput::GetActiveInput())
	{
		TextModeOff();
		return;
	}

	str_copy(Editor()->m_aTooltip, Localize("Type on your keyboard to insert letters and numbers. Press Escape to end text mode.", "Editor"));
	State.ClampCursor(pLayer->m_Width, pLayer->m_Height);

	const auto CurTime = time_get_nanoseconds();
	if((CurTime - m_CursorRenderTime) > 1s)
		m_CursorRenderTime = time_get_nanoseconds();
	if((CurTime - m_CursorRenderTime) > 500ms)
	{
		std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();
		pGroup->MapScreen();
		Graphics()->WrapClamp();
		Graphics()->TextureSet(m_CursorTextTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem QuadItem(State.m_TextIndex.x * 32, State.m_TextIndex.y * 32, 32.0f, 32.0f);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
		Ui()->MapScreen();
	}
}
