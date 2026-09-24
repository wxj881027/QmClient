/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "lineinput.h"

#include "QmUi/QmTheme.h"
#include "ui.h"

#include <engine/external/tinyexpr.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

IInput *CLineInput::ms_pInput = nullptr;
ITextRender *CLineInput::ms_pTextRender = nullptr;
IGraphics *CLineInput::ms_pGraphics = nullptr;
IClient *CLineInput::ms_pClient = nullptr;

CLineInput *CLineInput::ms_pActiveInput = nullptr;
EInputPriority CLineInput::ms_ActiveInputPriority = EInputPriority::NONE;
bool CLineInput::ms_TextInputAutoManaged = false;

vec2 CLineInput::ms_CompositionWindowPosition = vec2(0.0f, 0.0f);
float CLineInput::ms_CompositionLineHeight = 0.0f;

char CLineInput::ms_aStars[128] = "";

void CLineInput::SetBuffer(char *pStr, size_t MaxSize, size_t MaxChars)
{
	if(m_pStr && m_pStr == pStr)
		return;
	const char *pLastStr = m_pStr;
	m_pStr = pStr;
	m_MaxSize = MaxSize;
	m_MaxChars = MaxChars;
	m_WasChanged = m_pStr && pLastStr && m_WasChanged;
	m_WasCursorChanged = m_pStr && pLastStr && m_WasCursorChanged;
	if(!pLastStr)
	{
		m_CursorPos = m_SelectionStart = m_SelectionEnd = m_LastCompositionCursorPos = 0;
		m_ScrollOffset = m_ScrollOffsetChange = 0.0f;
		m_CaretPosition = vec2(0.0f, 0.0f);
		m_CaretBlinkStartTime = std::chrono::nanoseconds::zero();
		m_MouseSelection.m_Selecting = false;
		m_Hidden = false;
		m_pEmptyText = nullptr;
		m_WasRendered = false;
	}
	if(m_pStr && m_pStr != pLastStr)
		UpdateStrData();
}

void CLineInput::Clear()
{
	mem_zero(m_pStr, m_MaxSize);
	UpdateStrData();
}

void CLineInput::Set(const char *pString)
{
	str_copy(m_pStr, pString, m_MaxSize);
	UpdateStrData();
	SetCursorOffset(m_Len);
}

void CLineInput::SetRange(const char *pString, size_t Begin, size_t End)
{
	if(Begin > End)
		std::swap(Begin, End);
	Begin = std::clamp<size_t>(Begin, 0, m_Len);
	End = std::clamp<size_t>(End, 0, m_Len);

	size_t RemovedCharSize, RemovedCharCount;
	str_utf8_stats(m_pStr + Begin, End - Begin + 1, m_MaxChars, &RemovedCharSize, &RemovedCharCount);

	size_t AddedCharSize, AddedCharCount;
	str_utf8_stats(pString, m_MaxSize - m_Len + RemovedCharSize, m_MaxChars - m_NumChars + RemovedCharCount, &AddedCharSize, &AddedCharCount);

	if(RemovedCharSize || AddedCharSize)
	{
		if(AddedCharSize < RemovedCharSize)
		{
			if(AddedCharSize)
				mem_copy(m_pStr + Begin, pString, AddedCharSize);
			mem_move(m_pStr + Begin + AddedCharSize, m_pStr + Begin + RemovedCharSize, m_Len - Begin - AddedCharSize);
		}
		else if(AddedCharSize > RemovedCharSize)
		{
			mem_move(m_pStr + End + AddedCharSize - RemovedCharSize, m_pStr + End, m_Len - End);
		}

		if(AddedCharSize >= RemovedCharSize)
			mem_copy(m_pStr + Begin, pString, AddedCharSize);

		m_CursorPos = End - RemovedCharSize + AddedCharSize;
		m_Len += AddedCharSize - RemovedCharSize;
		m_NumChars += AddedCharCount - RemovedCharCount;
		m_WasChanged = true;
		m_WasCursorChanged = true;
		m_pStr[m_Len] = '\0';
		m_SelectionStart = m_SelectionEnd = m_CursorPos;
	}
}

void CLineInput::Insert(const char *pString, size_t Begin)
{
	SetRange(pString, Begin, Begin);
}

void CLineInput::Append(const char *pString)
{
	Insert(pString, m_Len);
}

void CLineInput::UpdateStrData()
{
	str_utf8_stats(m_pStr, m_MaxSize, m_MaxChars, &m_Len, &m_NumChars);
	if(!in_range<size_t>(m_CursorPos, 0, m_Len))
		SetCursorOffset(m_CursorPos);
	if(!in_range<size_t>(m_SelectionStart, 0, m_Len) || !in_range<size_t>(m_SelectionEnd, 0, m_Len))
		SetSelection(m_SelectionStart, m_SelectionEnd);
}

const char *CLineInput::GetDisplayedString()
{
	if(m_pfnDisplayTextCallback)
		return m_pfnDisplayTextCallback(m_pStr, GetNumChars());

	if(!IsHidden())
		return m_pStr;

	const size_t NumStars = minimum(GetNumChars(), sizeof(ms_aStars) - 1);
	for(size_t i = 0; i < NumStars; ++i)
		ms_aStars[i] = '*';
	ms_aStars[NumStars] = '\0';
	return ms_aStars;
}

void CLineInput::MoveCursor(EMoveDirection Direction, bool MoveWord, const char *pStr, size_t MaxSize, size_t *pCursorPos)
{
	// Check whether cursor position is initially on space or non-space character.
	// When forwarding, check character to the right of the cursor position.
	// When rewinding, check character to the left of the cursor position (rewind first).
	size_t PeekCursorPos = Direction == FORWARD ? *pCursorPos : str_utf8_rewind(pStr, *pCursorPos);
	const char *pTemp = pStr + PeekCursorPos;
	bool AnySpace = str_utf8_isspace(str_utf8_decode(&pTemp));
	bool AnyWord = !AnySpace;
	while(true)
	{
		if(Direction == FORWARD)
			*pCursorPos = str_utf8_forward(pStr, *pCursorPos);
		else
			*pCursorPos = str_utf8_rewind(pStr, *pCursorPos);
		if(!MoveWord || *pCursorPos <= 0 || *pCursorPos >= MaxSize)
			break;
		PeekCursorPos = Direction == FORWARD ? *pCursorPos : str_utf8_rewind(pStr, *pCursorPos);
		pTemp = pStr + PeekCursorPos;
		const bool CurrentSpace = str_utf8_isspace(str_utf8_decode(&pTemp));
		const bool CurrentWord = !CurrentSpace;
		if(Direction == FORWARD && AnySpace && !CurrentSpace)
			break; // Forward: Stop when next (right) character is non-space after seeing at least one space character.
		else if(Direction == REWIND && AnyWord && !CurrentWord)
			break; // Rewind: Stop when next (left) character is space after seeing at least one non-space character.
		AnySpace |= CurrentSpace;
		AnyWord |= CurrentWord;
	}
}

void CLineInput::SetCursorOffset(size_t Offset)
{
	m_SelectionStart = m_SelectionEnd = m_LastCompositionCursorPos = m_CursorPos = std::clamp<size_t>(Offset, 0, m_Len);
	m_WasCursorChanged = true;
}

void CLineInput::SetSelection(size_t Start, size_t End)
{
	dbg_assert(m_CursorPos == Start || m_CursorPos == End, "Selection and cursor offset got desynchronized");
	if(Start > End)
		std::swap(Start, End);
	m_SelectionStart = std::clamp<size_t>(Start, 0, m_Len);
	m_SelectionEnd = std::clamp<size_t>(End, 0, m_Len);
	m_WasCursorChanged = true;
}

size_t CLineInput::OffsetFromActualToDisplay(size_t ActualOffset)
{
	if(IsHidden() || (m_pfnCalculateOffsetCallback && m_pfnCalculateOffsetCallback()))
		return str_utf8_offset_bytes_to_chars(m_pStr, ActualOffset);
	return ActualOffset;
}

size_t CLineInput::OffsetFromDisplayToActual(size_t DisplayOffset)
{
	if(IsHidden() || (m_pfnCalculateOffsetCallback && m_pfnCalculateOffsetCallback()))
		return str_utf8_offset_bytes_to_chars(m_pStr, DisplayOffset);
	return DisplayOffset;
}

bool CLineInput::ProcessInput(const IInput::CEvent &Event)
{
	// update derived attributes to handle external changes to the buffer
	UpdateStrData();

	const size_t OldCursorPos = m_CursorPos;
	const bool Selecting = Input()->ShiftIsPressed();
	const size_t SelectionLength = GetSelectionLength();
	bool KeyHandled = false;

	if(Event.m_Flags & IInput::FLAG_TEXT)
	{
		SetRange(Event.m_aText, m_SelectionStart, m_SelectionEnd);
		KeyHandled = true;
	}

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		const bool ModPressed = Input()->ModifierIsPressed();
		const bool AltPressed = Input()->AltIsPressed();

#ifdef CONF_PLATFORM_MACOSX
		const bool MoveWord = AltPressed && !ModPressed;
#else
		const bool MoveWord = ModPressed && !AltPressed;
#endif

		if(Event.m_Key == KEY_BACKSPACE)
		{
			if(SelectionLength)
			{
				SetRange("", m_SelectionStart, m_SelectionEnd);
			}
			else
			{
				// If in MoveWord-mode, backspace will delete the word before the selection
				if(SelectionLength)
					m_SelectionEnd = m_CursorPos = m_SelectionStart;
				if(m_CursorPos > 0)
				{
					size_t NewCursorPos = m_CursorPos;
					MoveCursor(REWIND, MoveWord, m_pStr, m_Len, &NewCursorPos);
					SetRange("", NewCursorPos, m_CursorPos);
				}
				m_SelectionStart = m_SelectionEnd = m_CursorPos;
			}
			KeyHandled = true;
		}
		else if(Event.m_Key == KEY_DELETE)
		{
			if(SelectionLength)
			{
				SetRange("", m_SelectionStart, m_SelectionEnd);
			}
			else
			{
				// If in MoveWord-mode, delete will delete the word after the selection
				if(SelectionLength)
					m_SelectionStart = m_CursorPos = m_SelectionEnd;
				if(m_CursorPos < m_Len)
				{
					size_t EndCursorPos = m_CursorPos;
					MoveCursor(FORWARD, MoveWord, m_pStr, m_Len, &EndCursorPos);
					SetRange("", m_CursorPos, EndCursorPos);
				}
				m_SelectionStart = m_SelectionEnd = m_CursorPos;
			}
			KeyHandled = true;
		}
		else if(Event.m_Key == KEY_LEFT)
		{
			if(SelectionLength && !Selecting)
			{
				m_CursorPos = m_SelectionStart;
			}
			else if(m_CursorPos > 0)
			{
				MoveCursor(REWIND, MoveWord, m_pStr, m_Len, &m_CursorPos);
				if(Selecting)
				{
					if(m_SelectionStart == OldCursorPos) // expand start first
						m_SelectionStart = m_CursorPos;
					else if(m_SelectionEnd == OldCursorPos)
						m_SelectionEnd = m_CursorPos;
					if(m_SelectionStart > m_SelectionEnd)
						std::swap(m_SelectionStart, m_SelectionEnd);
				}
			}

			if(!Selecting)
			{
				m_SelectionStart = m_SelectionEnd = m_CursorPos;
			}
			KeyHandled = true;
		}
		else if(Event.m_Key == KEY_RIGHT)
		{
			if(SelectionLength && !Selecting)
			{
				m_CursorPos = m_SelectionEnd;
			}
			else if(m_CursorPos < m_Len)
			{
				MoveCursor(FORWARD, MoveWord, m_pStr, m_Len, &m_CursorPos);
				if(Selecting)
				{
					if(m_SelectionEnd == OldCursorPos) // expand end first
						m_SelectionEnd = m_CursorPos;
					else if(m_SelectionStart == OldCursorPos)
						m_SelectionStart = m_CursorPos;
					if(m_SelectionStart > m_SelectionEnd)
						std::swap(m_SelectionStart, m_SelectionEnd);
				}
			}

			if(!Selecting)
			{
				m_SelectionStart = m_SelectionEnd = m_CursorPos;
			}
			KeyHandled = true;
		}
		else if(Event.m_Key == KEY_HOME)
		{
			if(Selecting)
			{
				if(SelectionLength && m_CursorPos == m_SelectionEnd)
					m_SelectionEnd = m_SelectionStart;
			}
			else
			{
				m_SelectionEnd = 0;
			}
			m_CursorPos = 0;
			m_SelectionStart = 0;
			KeyHandled = true;
		}
		else if(Event.m_Key == KEY_END)
		{
			if(Selecting)
			{
				if(SelectionLength && m_CursorPos == m_SelectionStart)
					m_SelectionStart = m_SelectionEnd;
			}
			else
			{
				m_SelectionStart = m_Len;
			}
			m_CursorPos = m_Len;
			m_SelectionEnd = m_Len;
			KeyHandled = true;
		}
		else if(ModPressed && !AltPressed && Event.m_Key == KEY_V)
		{
			std::string ClipboardText = Input()->GetClipboardText();
			if(!ClipboardText.empty())
			{
				if(m_pfnClipboardLineCallback)
				{
					// Split clipboard text into multiple lines. Send all complete lines to callback.
					// The lineinput is set to the last clipboard line.
					bool FirstLine = true;
					size_t i, Begin = 0;
					for(i = 0; i < ClipboardText.length(); i++)
					{
						if(ClipboardText[i] == '\n')
						{
							size_t End = i;
							if(End > 0 && ClipboardText[End - 1] == '\r')
							{
								--End;
							}
							std::string Line = ClipboardText.substr(Begin, End - Begin);
							str_sanitize_cc(Line.data());
							if(FirstLine)
							{
								SetRange(Line.c_str(), m_SelectionStart, m_SelectionEnd);
								FirstLine = false;
							}
							else
							{
								Set(Line.c_str());
							}
							Begin = i + 1;
							m_pfnClipboardLineCallback(GetString());
						}
					}
					std::string Line = ClipboardText.substr(Begin);
					str_sanitize_cc(Line.data());
					if(FirstLine)
						SetRange(Line.c_str(), m_SelectionStart, m_SelectionEnd);
					else
						Set(Line.c_str());
				}
				else
				{
					str_sanitize_cc(ClipboardText.data());
					SetRange(ClipboardText.c_str(), m_SelectionStart, m_SelectionEnd);
				}
			}
			KeyHandled = true;
		}
		else if(ModPressed && !AltPressed && (Event.m_Key == KEY_C || Event.m_Key == KEY_X) && SelectionLength)
		{
			char *pSelection = m_pStr + m_SelectionStart;
			const char TempChar = pSelection[SelectionLength];
			pSelection[SelectionLength] = '\0';
			Input()->SetClipboardText(pSelection);
			pSelection[SelectionLength] = TempChar;
			if(Event.m_Key == KEY_X)
				SetRange("", m_SelectionStart, m_SelectionEnd);
			KeyHandled = true;
		}
		else if(ModPressed && !AltPressed && Event.m_Key == KEY_A)
		{
			m_SelectionStart = 0;
			m_SelectionEnd = m_CursorPos = m_Len;
			KeyHandled = true;
		}
	}

	m_WasCursorChanged |= OldCursorPos != m_CursorPos;
	m_WasCursorChanged |= SelectionLength != GetSelectionLength();
	return KeyHandled;
}

STextBoundingBox CLineInput::Render(const CUIRect *pRect, float FontSize, int Align, bool Changed, float LineWidth, float LineSpacing, const std::vector<STextColorSplit> &vColorSplits)
{
	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();
	const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();

	// update derived attributes to handle external changes to the buffer
	UpdateStrData();

	m_WasRendered = true;

	const char *pDisplayStr = GetDisplayedString();
	const bool HasComposition = Input()->HasComposition();

	if(pDisplayStr[0] == '\0' && !HasComposition && m_pEmptyText != nullptr)
	{
		pDisplayStr = m_pEmptyText;
		m_MouseSelection.m_Selecting = false;
		ColorRGBA PlaceholderColor = PreviousTextColor;
		PlaceholderColor.a *= 0.75f;
		TextRender()->TextColor(PlaceholderColor);
	}

	CTextCursor Cursor;
	// 光标和选区都不进入文本容器。选区先通过无渲染预遍历计算并绘制，
	// 再绘制文字，避免全选覆盖文字或污染后续 UI 文本的容器命令。
	Cursor.m_RenderCursor = false;
	Cursor.m_RenderSelection = false;
	if(IsActive())
	{
		const size_t CursorOffset = GetCursorOffset();
		const size_t DisplayCursorOffset = OffsetFromActualToDisplay(CursorOffset);
		const size_t CompositionStart = CursorOffset + Input()->GetCompositionCursor();
		const size_t DisplayCompositionStart = OffsetFromActualToDisplay(CompositionStart);
		const size_t CaretOffset = HasComposition ? DisplayCompositionStart : DisplayCursorOffset;

		std::string DisplayStrBuffer;
		if(HasComposition)
		{
			const std::string DisplayStr = std::string(pDisplayStr);
			DisplayStrBuffer = DisplayStr.substr(0, DisplayCursorOffset) + Input()->GetComposition() + DisplayStr.substr(DisplayCursorOffset);
			pDisplayStr = DisplayStrBuffer.c_str();
		}

		const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, pDisplayStr, -1, LineWidth, LineSpacing);
		const vec2 CursorPos = CUi::CalcAlignedCursorPos(pRect, BoundingBox.Size(), Align);

		Cursor.SetPosition(CursorPos);
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = LineWidth;
		Cursor.m_ForceCursorRendering = Changed;
		Cursor.m_LineSpacing = LineSpacing;
		Cursor.m_PressMouse.x = m_MouseSelection.m_PressMouse.x;
		Cursor.m_ReleaseMouse.x = m_MouseSelection.m_ReleaseMouse.x;
		Cursor.m_vColorSplits = vColorSplits;
		if(LineWidth < 0.0f)
		{
			// Using a Y position that's always inside the line input makes it so the selection does not reset when
			// the mouse is moved outside the line input while selecting, which would otherwise be very inconvenient.
			// This is a single line cursor, so we don't need the Y position to support selection over multiple lines.
			Cursor.m_PressMouse.y = CursorPos.y + BoundingBox.m_H / 2.0f;
			Cursor.m_ReleaseMouse.y = CursorPos.y + BoundingBox.m_H / 2.0f;
		}
		else
		{
			Cursor.m_PressMouse.y = m_MouseSelection.m_PressMouse.y;
			Cursor.m_ReleaseMouse.y = m_MouseSelection.m_ReleaseMouse.y;
		}

		auto RenderTextWithSelectionUnderlay = [&]() {
			if(Cursor.m_CalculateSelectionMode != TEXT_CURSOR_SELECTION_MODE_NONE)
			{
				CTextCursor SelectionCursor = Cursor;
				SelectionCursor.m_Flags &= ~TEXTFLAG_RENDER;
				SelectionCursor.m_RenderCursor = false;
				SelectionCursor.m_RenderSelection = false;
				TextRender()->TextEx(&SelectionCursor, pDisplayStr);

				if(SelectionCursor.m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE && SelectionCursor.m_CursorCharacter >= 0)
				{
					const size_t NewCursorOffset = str_utf8_offset_chars_to_bytes(pDisplayStr, SelectionCursor.m_CursorCharacter);
					SetCursorOffset(OffsetFromDisplayToActual(NewCursorOffset));
				}
				if(SelectionCursor.m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE && SelectionCursor.m_SelectionStart >= 0 && SelectionCursor.m_SelectionEnd >= 0)
				{
					const size_t NewSelectionStart = str_utf8_offset_chars_to_bytes(pDisplayStr, SelectionCursor.m_SelectionStart);
					const size_t NewSelectionEnd = str_utf8_offset_chars_to_bytes(pDisplayStr, SelectionCursor.m_SelectionEnd);
					SetSelection(OffsetFromDisplayToActual(NewSelectionStart), OffsetFromDisplayToActual(NewSelectionEnd));
				}

				RenderSelection(SelectionCursor, TextRender()->GetTextSelectionColor());
				Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
				Cursor.m_CursorCharacter = SelectionCursor.m_CursorCharacter;
				Cursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_NONE;
			}
			TextRender()->TextEx(&Cursor, pDisplayStr);
		};

		if(HasComposition)
		{
			// We need to track the last composition cursor position separately, because the composition
			// cursor movement does not cause an input event that would set the Changed variable.
			Cursor.m_ForceCursorRendering |= m_LastCompositionCursorPos != CaretOffset;
			m_LastCompositionCursorPos = CaretOffset;
			const size_t DisplayCompositionEnd = DisplayCursorOffset + Input()->GetCompositionLength();
			Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
			Cursor.m_CursorCharacter = str_utf8_offset_bytes_to_chars(pDisplayStr, CaretOffset);
			Cursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_SET;
			Cursor.m_SelectionHeightFactor = 0.1f;
			Cursor.m_SelectionStart = str_utf8_offset_bytes_to_chars(pDisplayStr, DisplayCursorOffset);
			Cursor.m_SelectionEnd = str_utf8_offset_bytes_to_chars(pDisplayStr, DisplayCompositionEnd);
			TextRender()->TextSelectionColor(qm_theme::IME.m_CompositionSelection);
			RenderTextWithSelectionUnderlay();
			TextRender()->TextSelectionColor(PreviousTextSelectionColor);
		}
		else if(GetSelectionLength())
		{
			const size_t Start = OffsetFromActualToDisplay(GetSelectionStart());
			const size_t End = OffsetFromActualToDisplay(GetSelectionEnd());
			Cursor.m_CursorMode = m_MouseSelection.m_Selecting ? TEXT_CURSOR_CURSOR_MODE_CALCULATE : TEXT_CURSOR_CURSOR_MODE_SET;
			Cursor.m_CursorCharacter = str_utf8_offset_bytes_to_chars(pDisplayStr, CaretOffset);
			Cursor.m_CalculateSelectionMode = m_MouseSelection.m_Selecting ? TEXT_CURSOR_SELECTION_MODE_CALCULATE : TEXT_CURSOR_SELECTION_MODE_SET;
			Cursor.m_SelectionStart = str_utf8_offset_bytes_to_chars(pDisplayStr, Start);
			Cursor.m_SelectionEnd = str_utf8_offset_bytes_to_chars(pDisplayStr, End);
			RenderTextWithSelectionUnderlay();
		}
		else
		{
			Cursor.m_CursorMode = m_MouseSelection.m_Selecting ? TEXT_CURSOR_CURSOR_MODE_CALCULATE : TEXT_CURSOR_CURSOR_MODE_SET;
			Cursor.m_CursorCharacter = str_utf8_offset_bytes_to_chars(pDisplayStr, CaretOffset);
			Cursor.m_CalculateSelectionMode = m_MouseSelection.m_Selecting ? TEXT_CURSOR_SELECTION_MODE_CALCULATE : TEXT_CURSOR_SELECTION_MODE_NONE;
			RenderTextWithSelectionUnderlay();
		}

		if(Cursor.m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE && Cursor.m_CursorCharacter >= 0)
		{
			const size_t NewCursorOffset = str_utf8_offset_chars_to_bytes(pDisplayStr, Cursor.m_CursorCharacter);
			SetCursorOffset(OffsetFromDisplayToActual(NewCursorOffset));
		}
		if(Cursor.m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE && Cursor.m_SelectionStart >= 0 && Cursor.m_SelectionEnd >= 0)
		{
			const size_t NewSelectionStart = str_utf8_offset_chars_to_bytes(pDisplayStr, Cursor.m_SelectionStart);
			const size_t NewSelectionEnd = str_utf8_offset_chars_to_bytes(pDisplayStr, Cursor.m_SelectionEnd);
			SetSelection(OffsetFromDisplayToActual(NewSelectionStart), OffsetFromDisplayToActual(NewSelectionEnd));
		}

		if(Cursor.m_HasCursorRenderedPosition)
		{
			m_CaretPosition = Cursor.m_CursorRenderedPosition;
			RenderCaret(Cursor, Cursor.m_ForceCursorRendering, TextRender()->GetTextColor(), TextRender()->GetTextOutlineColor());
			// 主 Cursor 已完成文本、选择区、光标和 IME 锚点位置计算，不能再创建一套
			// 重复文字容器。第二次 TextEx 会为同一输入额外插入图形命令，并在 caret
			// 闪烁边界与主容器交错。
			SetCompositionWindowPosition(m_CaretPosition + vec2(0.0f, Cursor.m_AlignedFontSize / 2.0f), Cursor.m_AlignedFontSize);
		}
	}
	else
	{
		const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, pDisplayStr, -1, LineWidth, LineSpacing);
		Cursor.SetPosition(CUi::CalcAlignedCursorPos(pRect, BoundingBox.Size(), Align));
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = LineWidth;
		Cursor.m_LineSpacing = LineSpacing;
		Cursor.m_vColorSplits = vColorSplits;
		TextRender()->TextEx(&Cursor, pDisplayStr);
	}

	TextRender()->SetRenderFlags(PreviousRenderFlags);
	TextRender()->SetFontPreset(PreviousFontPreset);
	TextRender()->TextOutlineColor(PreviousTextOutlineColor);
	TextRender()->TextSelectionColor(PreviousTextSelectionColor);
	TextRender()->TextColor(PreviousTextColor);

	return Cursor.BoundingBox();
}

void CLineInput::RenderSelection(const CTextCursor &Cursor, ColorRGBA SelectionColor)
{
	if(Cursor.m_vSelectionQuads.empty())
		return;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(SelectionColor);
	Graphics()->QuadsDrawTL(Cursor.m_vSelectionQuads.data(), Cursor.m_vSelectionQuads.size());
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CLineInput::RenderCaret(const CTextCursor &Cursor, bool ForceVisible, ColorRGBA TextColor, ColorRGBA TextOutlineColor)
{
	if(!Cursor.m_HasCursorRenderedPosition)
		return;

	const auto Now = time_get_nanoseconds();
	if(ForceVisible || m_CaretBlinkStartTime == std::chrono::nanoseconds::zero())
		m_CaretBlinkStartTime = Now;
	Graphics()->TextureClear();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	if(!ForceVisible && !qm_lineinput::CaretVisibleForElapsed(Now - m_CaretBlinkStartTime))
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float CursorInnerWidth = ((ScreenX1 - ScreenX0) / maximum(Graphics()->ScreenWidth(), 1)) * 2.0f;
	const float CursorOuterWidth = CursorInnerWidth * 2.0f;
	const float CursorOuterInnerDiff = (CursorOuterWidth - CursorInnerWidth) / 2.0f;
	const float CursorHeight = maximum(0.0f, Cursor.m_AlignedFontSize);
	if(CursorHeight <= 0.0f)
		return;

	const IGraphics::CQuadItem OuterCaret(
		Cursor.m_CursorRenderedPosition.x - CursorOuterInnerDiff,
		Cursor.m_CursorRenderedPosition.y,
		CursorOuterWidth,
		CursorHeight);
	const IGraphics::CQuadItem InnerCaret(
		Cursor.m_CursorRenderedPosition.x,
		Cursor.m_CursorRenderedPosition.y + CursorOuterInnerDiff,
		CursorInnerWidth,
		maximum(0.0f, CursorHeight - CursorOuterInnerDiff * 2.0f));

	Graphics()->QuadsBegin();
	Graphics()->SetColor(TextOutlineColor);
	Graphics()->QuadsDrawTL(&OuterCaret, 1);
	Graphics()->SetColor(TextColor);
	Graphics()->QuadsDrawTL(&InnerCaret, 1);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

bool CLineInput::ValidateActiveInputRenderedThisFrame()
{
	// Check if the active line input was not rendered and deactivate it in that case.
	// This can happen e.g. when an input in the ingame menu is active and the menu is
	// closed or when switching between menu and editor with an active input.
	CLineInput *pActiveInput = GetActiveInput();
	if(pActiveInput == nullptr)
		return false;
	if(pActiveInput->m_WasRendered)
	{
		pActiveInput->m_WasRendered = false;
		return true;
	}
	pActiveInput->Deactivate();
	return false;
}

bool CLineInput::RenderLegacyCandidates()
{
	if(!ValidateActiveInputRenderedThisFrame())
		return false;

	const int CandidateCount = Input()->GetCandidateCount();
	if(!Input()->HasComposition() || CandidateCount <= 0)
		return true;

	const float FontSize = 7.0f;
	const float Padding = 1.0f;
	const float Margin = 4.0f;
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const int ScreenWidth = maximum(Graphics()->ScreenWidth(), 1);
	const int ScreenHeight = maximum(Graphics()->ScreenHeight(), 1);

	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	float LongestCandidateWidth = 0.0f;
	for(int i = 0; i < CandidateCount; ++i)
		LongestCandidateWidth = maximum(LongestCandidateWidth, TextRender()->TextWidth(FontSize, Input()->GetCandidate(i)));

	const float NumOffset = 8.0f;
	const float RectWidth = LongestCandidateWidth + Margin + NumOffset + 2.0f * Padding;
	const float RectHeight = CandidateCount * (FontSize + 2.0f * Padding) + Margin;

	vec2 Position = ms_CompositionWindowPosition / vec2(ScreenWidth, ScreenHeight) * vec2(Width, Height);
	Position.y += Margin;

	if(Position.x + RectWidth + Margin > Width)
		Position.x -= Position.x + RectWidth + Margin - Width;

	if(Position.y + RectHeight + Margin > Height)
		Position.y -= RectHeight + ms_CompositionLineHeight / ScreenHeight * Height + 2.0f * Margin;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.8f);
	IGraphics::CQuadItem Quad(Position.x + 0.75f, Position.y + 0.75f, RectWidth, RectHeight);
	Graphics()->QuadsDrawTL(&Quad, 1);

	Graphics()->SetColor(0.15f, 0.15f, 0.15f, 1.0f);
	Quad = IGraphics::CQuadItem(Position.x, Position.y, RectWidth, RectHeight);
	Graphics()->QuadsDrawTL(&Quad, 1);

	const int SelectedIndex = qm_ime_overlay::NormalizeSelectedCandidateIndex(Input()->GetCandidateSelectedIndex(), CandidateCount);
	if(SelectedIndex >= 0)
	{
		Graphics()->SetColor(0.1f, 0.4f, 0.8f, 1.0f);
		Quad = IGraphics::CQuadItem(Position.x + Margin / 4.0f, Position.y + Margin / 2.0f + SelectedIndex * (FontSize + 2.0f * Padding), RectWidth - Margin / 2.0f, FontSize + 2.0f * Padding);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	for(int i = 0; i < CandidateCount; ++i)
	{
		char aBuf[3];
		str_format(aBuf, sizeof(aBuf), "%d.", (i + 1) % 10);

		const float PosX = Position.x + Margin / 2.0f + Padding;
		const float PosY = Position.y + Margin / 2.0f + i * (FontSize + 2.0f * Padding) + Padding;
		TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
		TextRender()->Text(PosX, PosY, FontSize, aBuf);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(PosX + NumOffset, PosY, FontSize, Input()->GetCandidate(i));
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	return true;
}

void CLineInput::SetCompositionWindowPosition(vec2 Anchor, float LineHeight)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	const int ScreenWidth = maximum(Graphics()->ScreenWidth(), 1);
	const int ScreenHeight = maximum(Graphics()->ScreenHeight(), 1);
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const vec2 ScreenScale = vec2(ScreenWidth / std::max(1.0f, ScreenX1 - ScreenX0),
		ScreenHeight / std::max(1.0f, ScreenY1 - ScreenY0));
	ms_CompositionWindowPosition = Anchor * ScreenScale;
	ms_CompositionLineHeight = LineHeight * ScreenScale.y;
	Input()->SetCompositionWindowPosition(ms_CompositionWindowPosition.x, ms_CompositionWindowPosition.y, ms_CompositionLineHeight);
}

void CLineInput::Activate(EInputPriority Priority)
{
	if(IsActive())
		return;
	if(ms_ActiveInputPriority != EInputPriority::NONE && Priority < ms_ActiveInputPriority)
		return; // do not replace a higher priority input
	if(ms_pActiveInput)
		ms_pActiveInput->OnDeactivate();
	ms_pActiveInput = this;
	ms_pActiveInput->OnActivate();
	ms_ActiveInputPriority = Priority;
}

void CLineInput::Deactivate() const
{
	if(!IsActive())
		return;
	ms_pActiveInput->OnDeactivate();
	ms_pActiveInput = nullptr;
	ms_ActiveInputPriority = EInputPriority::NONE;
}

void CLineInput::OnActivate()
{
	m_CaretBlinkStartTime = time_get_nanoseconds();
	if(!TextInputAutoManaged())
		Input()->StartTextInput();
}

void CLineInput::OnDeactivate()
{
	if(!TextInputAutoManaged())
		Input()->StopTextInput();
	m_MouseSelection.m_Selecting = false;
}

void CLineInputNumber::SetInteger(int Number, int Base, int HexPrefix)
{
	char aBuf[32];
	switch(Base)
	{
	case 10:
		str_format(aBuf, sizeof(aBuf), "%d", Number);
		break;
	case 16:
		str_format(aBuf, sizeof(aBuf), "%0*X", HexPrefix, Number);
		break;
	default:
		dbg_assert_failed("Base %d unsupported", Base);
	}
	if(str_comp(aBuf, GetString()) != 0)
		Set(aBuf);
}

int CLineInputNumber::GetInteger(int Base) const
{
	if(Base == 10)
	{
		double Result = te_interp(GetString(), nullptr);
		if(std::isfinite(Result))
			return (int)std::round(Result);
	}
	return str_toint_base(GetString(), Base);
}

void CLineInputNumber::SetInteger64(int64_t Number, int Base, int HexPrefix)
{
	char aBuf[64];
	switch(Base)
	{
	case 10:
		str_format(aBuf, sizeof(aBuf), "%" PRId64, Number);
		break;
	case 16:
		str_format(aBuf, sizeof(aBuf), "%0*" PRIX64, HexPrefix, Number);
		break;
	default:
		dbg_assert_failed("Base %d unsupported", Base);
	}
	if(str_comp(aBuf, GetString()) != 0)
		Set(aBuf);
}

int64_t CLineInputNumber::GetInteger64(int Base) const
{
	if(Base == 10)
	{
		double Result = te_interp(GetString(), nullptr);
		if(std::isfinite(Result))
			return (int64_t)std::round(Result);
	}
	return str_toint64_base(GetString(), Base);
}

void CLineInputNumber::SetFloat(float Number)
{
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.3f", Number);
	if(str_comp(aBuf, GetString()) != 0)
		Set(aBuf);
}

float CLineInputNumber::GetFloat() const
{
	// return str_tofloat(GetString());
	return (float)te_interp(GetString(), nullptr);
}
