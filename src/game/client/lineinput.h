/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_LINEINPUT_H
#define GAME_CLIENT_LINEINPUT_H

#include "QmUi/QmLineInputMotion.h"

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/textrender.h>

#include <game/client/ui_rect.h>

#include <chrono>

enum class EInputPriority
{
	NONE = 0,
	UI,
	CHAT,
	CONSOLE,
};

namespace qm_ime_overlay
{
	// 常见 IME 一页有 9～10 项，不能额外固定为七项而隐藏本页尾部候选。
	inline constexpr int FIXED_CANDIDATE_VIEWPORT_SIZE = 16;

	struct SQmImeCandidateViewport
	{
		int m_Start = 0;
		int m_Count = 0;
	};

	inline int NormalizeSelectedCandidateIndex(int SelectedIndex, int CandidateCount)
	{
		if(CandidateCount <= 0)
			return -1;
		if(SelectedIndex < 0 || SelectedIndex >= CandidateCount)
			return 0;
		return SelectedIndex;
	}
	inline SQmImeCandidateViewport BuildCandidateViewport(int CandidateCount, int SelectedIndex, int PreviousStart)
	{
		SQmImeCandidateViewport Viewport;
		if(CandidateCount <= 0)
			return Viewport;

		Viewport.m_Count = minimum(CandidateCount, FIXED_CANDIDATE_VIEWPORT_SIZE);
		const int MaxStart = maximum(0, CandidateCount - Viewport.m_Count);
		const int Selected = NormalizeSelectedCandidateIndex(SelectedIndex, CandidateCount);
		int Start = std::clamp(PreviousStart, 0, MaxStart);
		if(Selected < Start)
			Start = Selected;
		else if(Selected >= Start + Viewport.m_Count)
			Start = Selected - Viewport.m_Count + 1;
		Viewport.m_Start = std::clamp(Start, 0, MaxStart);
		return Viewport;
	}
} // namespace qm_ime_overlay

namespace qm_lineinput
{
	inline bool CaretVisibleForElapsed(std::chrono::nanoseconds Elapsed)
	{
		using namespace std::chrono_literals;
		if(Elapsed < 0ns)
			return true;
		return Elapsed % 1s < 500ms;
	}
} // namespace qm_lineinput

// line input helper
class CLineInput
{
public:
	struct SMouseSelection
	{
		bool m_Selecting = false;
		vec2 m_PressMouse = vec2(0.0f, 0.0f);
		vec2 m_ReleaseMouse = vec2(0.0f, 0.0f);
		vec2 m_Offset = vec2(0.0f, 0.0f);
	};

	typedef std::function<void(const char *pLine)> FClipboardLineCallback;
	typedef std::function<const char *(char *pCurrentText, size_t NumChars)> FDisplayTextCallback;
	typedef std::function<bool()> FCalculateOffsetCallback;

private:
	static IClient *ms_pClient;
	static IGraphics *ms_pGraphics;
	static IInput *ms_pInput;
	static ITextRender *ms_pTextRender;

	static IClient *Client() { return ms_pClient; }
	static IGraphics *Graphics() { return ms_pGraphics; }
	static IInput *Input() { return ms_pInput; }
	static ITextRender *TextRender() { return ms_pTextRender; }

	static CLineInput *ms_pActiveInput;
	static EInputPriority ms_ActiveInputPriority;
	static bool ms_TextInputAutoManaged;

	static vec2 ms_CompositionWindowPosition;
	static float ms_CompositionLineHeight;

	static char ms_aStars[128];

	char *m_pStr = nullptr; // explicitly set to nullptr outside of constructor, so SetBuffer works in this case
	size_t m_MaxSize;
	size_t m_MaxChars;
	size_t m_Len;
	size_t m_NumChars;

	size_t m_CursorPos;
	size_t m_SelectionStart;
	size_t m_SelectionEnd;

	float m_ScrollOffset;
	float m_ScrollOffsetChange;
	vec2 m_CaretPosition;
	std::chrono::nanoseconds m_CaretBlinkStartTime{};
	CQmLineInputMotion m_InputMotion;
	SMouseSelection m_MouseSelection;
	size_t m_LastCompositionCursorPos;

	bool m_Hidden;
	const char *m_pEmptyText;
	FClipboardLineCallback m_pfnClipboardLineCallback;
	FDisplayTextCallback m_pfnDisplayTextCallback;
	FCalculateOffsetCallback m_pfnCalculateOffsetCallback;
	bool m_WasChanged;
	bool m_WasCursorChanged;
	bool m_WasRendered;

	char m_ClearButtonId;

	void UpdateStrData();
	enum EMoveDirection
	{
		FORWARD,
		REWIND
	};
	static void MoveCursor(EMoveDirection Direction, bool MoveWord, const char *pStr, size_t MaxSize, size_t *pCursorPos);
	static void SetCompositionWindowPosition(vec2 Anchor, float LineHeight);
	void RenderCaret(const CTextCursor &Cursor, bool ForceVisible, ColorRGBA TextColor, ColorRGBA TextOutlineColor);
	void RenderSelection(const CTextCursor &Cursor, ColorRGBA SelectionColor);

	void OnActivate();
	void OnDeactivate();

public:
	static void Init(IClient *pClient, IGraphics *pGraphics, IInput *pInput, ITextRender *pTextRender)
	{
		ms_pClient = pClient;
		ms_pGraphics = pGraphics;
		ms_pInput = pInput;
		ms_pTextRender = pTextRender;
	}
	static bool RenderLegacyCandidates();
	static bool ValidateActiveInputRenderedThisFrame();

	static CLineInput *GetActiveInput() { return ms_pActiveInput; }
	static EInputPriority GetActiveInputPriority() { return ms_ActiveInputPriority; }
	static void SetTextInputAutoManaged(bool Managed) { ms_TextInputAutoManaged = Managed; }
	static bool TextInputAutoManaged() { return ms_TextInputAutoManaged; }
	static vec2 GetCompositionWindowPosition() { return ms_CompositionWindowPosition; }
	static float GetCompositionLineHeight() { return ms_CompositionLineHeight; }

	CLineInput()
	{
		SetBuffer(nullptr, 0, 0);
	}

	CLineInput(char *pStr, size_t MaxSize)
	{
		SetBuffer(pStr, MaxSize);
	}

	CLineInput(char *pStr, size_t MaxSize, size_t MaxChars)
	{
		SetBuffer(pStr, MaxSize, MaxChars);
	}

	void SetBuffer(char *pStr, size_t MaxSize) { SetBuffer(pStr, MaxSize, MaxSize); }
	void SetBuffer(char *pStr, size_t MaxSize, size_t MaxChars);

	void Clear();
	void Set(const char *pString);
	void SetRange(const char *pString, size_t Begin, size_t End);
	void Insert(const char *pString, size_t Begin);
	void Append(const char *pString);

	const char *GetString() const { return m_pStr; }
	const char *GetDisplayedString();
	size_t GetMaxSize() const { return m_MaxSize; }
	size_t GetMaxChars() const { return m_MaxChars; }
	size_t GetLength() const { return m_Len; }
	size_t GetNumChars() const { return m_NumChars; }
	bool IsEmpty() const { return GetLength() == 0; }

	size_t GetCursorOffset() const { return m_CursorPos; }
	void SetCursorOffset(size_t Offset);
	size_t GetSelectionStart() const { return m_SelectionStart; }
	size_t GetSelectionEnd() const { return m_SelectionEnd; }
	size_t GetSelectionLength() const { return m_SelectionEnd - m_SelectionStart; }
	bool HasSelection() const { return GetSelectionLength() > 0; }
	void SetSelection(size_t Start, size_t End);
	void SelectNothing() { SetSelection(GetCursorOffset(), GetCursorOffset()); }
	void SelectAll()
	{
		SetCursorOffset(GetLength());
		SetSelection(0, GetLength());
	}

	size_t OffsetFromActualToDisplay(size_t ActualOffset);
	size_t OffsetFromDisplayToActual(size_t DisplayOffset);

	// used either for vertical or horizontal scrolling
	float GetScrollOffset() const { return m_ScrollOffset; }
	void SetScrollOffset(float ScrollOffset) { m_ScrollOffset = ScrollOffset; }
	float GetScrollOffsetChange() const { return m_ScrollOffsetChange; }
	void SetScrollOffsetChange(float ScrollOffsetChange) { m_ScrollOffsetChange = ScrollOffsetChange; }

	vec2 GetCaretPosition() const { return m_CaretPosition; } // only updated while the input is active

	bool IsHidden() const { return m_Hidden; }
	void SetHidden(bool Hidden) { m_Hidden = Hidden; }

	const char *GetEmptyText() const { return m_pEmptyText; }
	void SetEmptyText(const char *pText) { m_pEmptyText = pText; }

	void SetClipboardLineCallback(const FClipboardLineCallback &pfnClipboardLineCallback) { m_pfnClipboardLineCallback = pfnClipboardLineCallback; }
	void SetDisplayTextCallback(const FDisplayTextCallback &pfnDisplayTextCallback) { m_pfnDisplayTextCallback = pfnDisplayTextCallback; }
	void SetCalculateOffsetCallback(const FCalculateOffsetCallback &pfnCalculateOffsetCallback) { m_pfnCalculateOffsetCallback = pfnCalculateOffsetCallback; }

	bool ProcessInput(const IInput::CEvent &Event);
	bool WasChanged()
	{
		const bool Changed = m_WasChanged;
		m_WasChanged = false;
		return Changed;
	}
	bool WasCursorChanged()
	{
		const bool Changed = m_WasCursorChanged;
		m_WasCursorChanged = false;
		return Changed;
	}

	STextBoundingBox Render(const CUIRect *pRect, float FontSize, int Align, bool Changed, float LineWidth, float LineSpacing, const std::vector<STextColorSplit> &vColorSplits = {});
	SMouseSelection *GetMouseSelection() { return &m_MouseSelection; }

	const void *GetClearButtonId() const { return &m_ClearButtonId; }

	bool IsActive() const { return GetActiveInput() == this; }
	void Activate(EInputPriority Priority);
	void Deactivate() const;
};

template<size_t MaxSize, size_t MaxChars = MaxSize>
class CLineInputBuffered : public CLineInput
{
	char m_aBuffer[MaxSize];

public:
	CLineInputBuffered() :
		CLineInput()
	{
		m_aBuffer[0] = '\0';
		SetBuffer(m_aBuffer, MaxSize, MaxChars);
	}
};

class CLineInputNumber : public CLineInputBuffered<32>
{
public:
	CLineInputNumber() = default;

	CLineInputNumber(int Number)
	{
		SetInteger(Number);
	}

	CLineInputNumber(float Number)
	{
		SetFloat(Number);
	}

	void SetInteger(int Number, int Base = 10, int HexPrefix = 6);
	int GetInteger(int Base = 10) const;

	void SetInteger64(int64_t Number, int Base = 10, int HexPrefix = 6);
	int64_t GetInteger64(int Base = 10) const;

	void SetFloat(float Number);
	float GetFloat() const;
};

#endif
