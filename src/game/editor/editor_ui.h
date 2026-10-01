#ifndef GAME_EDITOR_EDITOR_UI_H
#define GAME_EDITOR_EDITOR_UI_H

#include <game/client/lineinput.h>
#include <game/client/ui_listbox.h>

#include <vector>

struct SEditBoxDropdownContext
{
	bool m_Visible = false;
	int m_Selected = -1;
	CListBox m_ListBox;
	bool m_ShortcutUsed = false;
	bool m_DidBecomeVisible = false;
	bool m_MousePressedInside = false;
	bool m_ShouldHide = false;
	int m_Width = 0;
};

struct SEditorLayerListState
{
	enum
	{
		OP_NONE = 0,
		OP_CLICK,
		OP_LAYER_DRAG,
		OP_GROUP_DRAG
	};

	int m_Operation = 0;
	int m_PreviousOperation = 0;
	const void *m_pDraggedButton = nullptr;
	float m_InitialMouseY = 0.0f;
	float m_InitialCutHeight = 0.0f;
	bool m_ScrollToSelectionNext = false;
	std::vector<int> m_vButtonsPerGroup;

	void SetOperation(int NewOperation)
	{
		if(m_Operation == NewOperation)
			return;
		m_PreviousOperation = m_Operation;
		m_Operation = NewOperation;
		if(NewOperation == OP_NONE)
			m_pDraggedButton = nullptr;
	}

	void ResetDrag()
	{
		m_Operation = 0;
		m_PreviousOperation = 0;
		m_pDraggedButton = nullptr;
		m_InitialMouseY = 0.0f;
		m_InitialCutHeight = 0.0f;
		m_ScrollToSelectionNext = false;
	}
};

struct SEditorValueSelectorState
{
	bool m_DidScroll = false;
	float m_ScrollValue = 0.0f;
	CLineInputNumber m_NumberInput;
	int m_ButtonUsed = -1;
	const void *m_pLastTextId = nullptr;
	const void *m_pEditing = nullptr;

	void Reset()
	{
		if(m_NumberInput.IsActive())
			m_NumberInput.Deactivate();
		m_DidScroll = false;
		m_ScrollValue = 0.0f;
		m_ButtonUsed = -1;
		m_pLastTextId = nullptr;
		m_pEditing = nullptr;
	}
};

class CEditorUiElements
{
public:
	CScrollRegion m_LayersScrollRegion;
	CScrollRegion m_ImagesScrollRegion;
	CScrollRegion m_SoundsScrollRegion;
	CScrollRegion m_InspectorScrollRegion;
	bool m_InspectGroup = false;
	SEditorLayerListState m_LayerListState;
};

// TODO: add and use constants for other special Checked-values in CEditor::GetButtonColor
namespace EditorButtonChecked
{
	[[maybe_unused]] static constexpr int DANGEROUS_ACTION = 9;
}

namespace EditorFontSizes
{
	[[maybe_unused]] static constexpr float MENU = 10.0f;
}

#endif
