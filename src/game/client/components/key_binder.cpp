/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "key_binder.h"

#include <base/color.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/binds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>
#include <game/localization.h>

using namespace FontIcons;

bool CKeyBinder::OnInput(const IInput::CEvent &Event)
{
	if(!m_TakeKey)
	{
		return false;
	}

	if(Event.m_Flags & IInput::FLAG_RELEASE)
	{
		int ModifierCombination = CBinds::GetModifierMask(Input());
		if(ModifierCombination == CBinds::GetModifierMaskOfKey(Event.m_Key))
		{
			ModifierCombination = KeyModifier::NONE;
		}
		m_Key = {Event.m_Key, ModifierCombination};
		m_TakeKey = false;
	}
	return true;
}

CKeyBinder::CKeyReaderResult CKeyBinder::DoKeyReader(CButtonContainer *pReaderButton, CButtonContainer *pClearButton, const CUIRect *pRect, const CBindSlot &CurrentBind, bool Activate, float FontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	CKeyReaderResult Result = {CurrentBind, false};
	const bool ReadOnly = Ui()->RenderOnly();

	CUIRect KeyReaderButton, ClearButton;
	pRect->VSplitRight(pRect->h, &KeyReaderButton, &ClearButton);
	const ColorRGBA ReaderBaseColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Ui()->ButtonColorMul(pReaderButton));
	DrawRoundedSurface(Ui(), *pRect, ReaderBaseColor, ColorRGBA(), 5.0f);

	const int ClearChecked = Result.m_Bind == CBindSlot(KEY_UNKNOWN, KeyModifier::NONE) ? 1 : 0;
	if(ClearChecked == 0)
	{
		const float ClearSurfaceAlpha = 0.22f * Ui()->ButtonColorMul(pClearButton);
		DrawRoundedSurface(Ui(), ClearButton, ColorRGBA(1.0f, 1.0f, 1.0f, ClearSurfaceAlpha), ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_R);
	}
	const int ClearButtonResult = Ui()->DoButton_QmIcon(
		pClearButton, EQmIcon::TRASH, FONT_ICON_TRASH,
		ClearChecked, &ClearButton, BUTTONFLAG_LEFT, IGraphics::CORNER_R, true, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));

	const int ButtonResult = Ui()->DoButtonLogic(pReaderButton, 0, &KeyReaderButton, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(!ReadOnly && (ButtonResult == 1 || Activate))
	{
		m_pKeyReaderId = pReaderButton;
		m_TakeKey = true;
		m_Key = std::nullopt;
	}
	else if(!ReadOnly && (ButtonResult == 2 || ClearButtonResult != 0))
	{
		Result.m_Bind = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
	}

	if(!ReadOnly && m_pKeyReaderId == pReaderButton && m_Key.has_value())
	{
		if(m_Key.value().m_Key == KEY_ESCAPE)
		{
			Result.m_Aborted = true;
		}
		else
		{
			Result.m_Bind = m_Key.value();
		}
		m_pKeyReaderId = nullptr;
		m_Key = std::nullopt;
		Ui()->SetActiveItem(nullptr);
	}

	char aBuf[64];
	if(m_pKeyReaderId == pReaderButton && m_TakeKey)
	{
		str_copy(aBuf, Localize("Press a key…"));
	}
	else if(Result.m_Bind.m_Key == KEY_UNKNOWN)
	{
		aBuf[0] = '\0';
	}
	else
	{
		GameClient()->m_Binds.GetKeyBindName(Result.m_Bind.m_Key, Result.m_Bind.m_ModifierMask, aBuf, sizeof(aBuf));
	}

	if(m_pKeyReaderId == pReaderButton && m_TakeKey)
		DrawRoundedSurface(Ui(), KeyReaderButton, ColorRGBA(0.0f, 1.0f, 0.0f, 0.4f), ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_L);
	CUIRect Label;
	KeyReaderButton.HMargin(1.0f, &Label);
	if(FontSize > 0.0f)
	{
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_MinimumFontSize = FontSize;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_MC, Props);
	}
	else
		Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	return Result;
}

bool CKeyBinder::IsActive() const
{
	return m_TakeKey;
}

bool CKeyBinder::AbortPendingKey()
{
	if(m_pKeyReaderId == nullptr)
		return false;
	m_Key = CBindSlot(KEY_ESCAPE, KeyModifier::NONE);
	m_TakeKey = false;
	return true;
}
