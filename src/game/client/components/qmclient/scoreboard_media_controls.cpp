#include "scoreboard_media_controls.h"

#include <base/time.h>
#include <game/client/components/system_media_controls.h>
#include <game/client/components/tooltips.h>
#include <game/localization.h>

void CQmScoreboardMediaControls::Cancel(CUi &Ui)
{
	if(Ui.ActiveItem() == &m_VolumeId)
		Ui.SetActiveItem(nullptr);
	for(auto &Button : m_aButtons)
		if(Ui.ActiveItem() == &Button)
			Ui.SetActiveItem(nullptr);
	m_Generation = 0;
	m_LastRequest = 0;
}

void CQmScoreboardMediaControls::Render(CUi &Ui, ITextRender &TextRender, CTooltips &Tooltips, CSystemMediaControls &Media, const CUIRect &Scoreboard, bool Interactive, float Alpha, ColorRGBA Background)
{
	CSystemMediaControls::SState State;
	Interactive = Interactive && !Ui.IsPopupOpen();
	if(!Media.GetStateSnapshot(State))
	{
		Cancel(Ui);
		return;
	}
	if(!Interactive || State.m_VolumeGeneration != m_Generation)
		Cancel(Ui);
	m_Generation = State.m_VolumeGeneration;
	const CUIRect Rail = QmScoreboardMediaRail(Scoreboard, *Ui.Screen());
	const ColorRGBA PreviousColor = TextRender.GetTextColor();
	const ColorRGBA PreviousOutline = TextRender.GetTextOutlineColor();
	const auto PreviousFont = TextRender.GetFontPreset();
	const auto PreviousFlags = TextRender.GetRenderFlags();
	const char *apIcons[] = {FontIcons::FONT_ICON_BACKWARD_STEP, State.m_Playing ? FontIcons::FONT_ICON_PAUSE : FontIcons::FONT_ICON_PLAY, FontIcons::FONT_ICON_FORWARD_STEP};
	const char *apLabels[] = {Localize("Previous track"), Localize("Play/Pause"), Localize("Next track")};
	const bool aEnabled[] = {State.m_CanPrev, State.m_Playing ? State.m_CanPause : State.m_CanPlay, State.m_CanNext};
	for(int Index = 0; Index < 3; ++Index)
	{
		CUIRect Button{Rail.x, Rail.y + Index * 32.0f, Rail.w, 28.0f};
		const bool Hovered = Interactive && Ui.MouseHovered(&Button);
		Button.Draw(Ui.ScaleBackgroundAlpha(Background.WithMultipliedAlpha(Hovered ? 1.0f : 0.8f)), IGraphics::CORNER_ALL, 5.0f);
		TextRender.SetFontPreset(EFontPreset::ICON_FONT);
		TextRender.SetRenderFlags(TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING);
		TextRender.TextColor(TextRender.DefaultTextColor().WithMultipliedAlpha(Alpha * (aEnabled[Index] ? 1.0f : 0.3f)));
		TextRender.TextOutlineColor(TextRender.DefaultTextOutlineColor().WithMultipliedAlpha(Alpha));
		Ui.DoLabel(&Button, apIcons[Index], 16.0f, TEXTALIGN_MC);
		TextRender.SetFontPreset(PreviousFont);
		TextRender.SetRenderFlags(PreviousFlags);
		if(Interactive)
		{
			Tooltips.DoSmallToolTip(&m_aButtons[Index], &Button, apLabels[Index], 10.0f);
			if(aEnabled[Index] && Ui.DoButtonLogic(&m_aButtons[Index], 0, &Button, BUTTONFLAG_LEFT))
			{
				if(Index == 0)
					Media.Previous();
				else if(Index == 1)
					Media.PlayPause();
				else
					Media.Next();
			}
		}
	}

	CUIRect VolumeArea{Rail.x, Rail.y + 96.0f, Rail.w, 124.0f};
	VolumeArea.Draw(Ui.ScaleBackgroundAlpha(Background.WithMultipliedAlpha(0.8f)), IGraphics::CORNER_ALL, 5.0f);
	CUIRect Track{VolumeArea.x + VolumeArea.w * 0.5f - 2.0f, VolumeArea.y + 10.0f, 4.0f, 86.0f};
	CUIRect Hit{VolumeArea.x, Track.y - 5.0f, VolumeArea.w, Track.h + 10.0f};
	if(!Ui.IsActiveItem(&m_VolumeId) && (m_LastRequest == 0 || time_get() - m_LastRequest > time_freq() / 2))
		m_LocalVolume = State.m_Volume;
	if(Interactive && State.m_CanSetVolume)
	{
		if(Ui.MouseHovered(&Hit))
		{
			Ui.SetHotItem(&m_VolumeId);
			if(Ui.MouseButtonClicked(0) && Ui.ActiveItem() == nullptr && !Ui.IsPopupOpen())
				Ui.SetActiveItem(&m_VolumeId);
		}
		if(Ui.CheckActiveItem(&m_VolumeId))
		{
			m_LocalVolume = QmScoreboardMediaVolumeAt(Ui.MouseY(), Track);
			Media.SetVolume(m_Generation, m_LocalVolume);
			m_LastRequest = time_get();
			if(!Ui.MouseButton(0))
				Ui.SetActiveItem(nullptr);
		}
	}
	const float VolumeAlpha = Alpha * (State.m_CanSetVolume ? 1.0f : 0.3f);
	Track.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f * VolumeAlpha), IGraphics::CORNER_ALL, 2.0f);
	CUIRect Fill = Track;
	Fill.y += (1.0f - m_LocalVolume) * Track.h;
	Fill.h = m_LocalVolume * Track.h;
	Fill.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f * VolumeAlpha), IGraphics::CORNER_ALL, 2.0f);
	CUIRect Thumb{Track.x - 3.0f, Fill.y - 4.0f, 10.0f, 8.0f};
	Thumb.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, VolumeAlpha), IGraphics::CORNER_ALL, 4.0f);
	CUIRect Label{VolumeArea.x, VolumeArea.y + 102.0f, VolumeArea.w, 16.0f};
	char aVolume[16];
	if(State.m_CanSetVolume)
		str_format(aVolume, sizeof(aVolume), "%d%%", round_to_int(m_LocalVolume * 100.0f));
	else
		str_copy(aVolume, "--");
	TextRender.TextColor(TextRender.DefaultTextColor().WithMultipliedAlpha(VolumeAlpha));
	Ui.DoLabel(&Label, aVolume, 9.0f, TEXTALIGN_MC);
	if(Interactive)
		Tooltips.DoSmallToolTip(&m_VolumeId, &VolumeArea, State.m_CanSetVolume ? Localize("Music player volume") : Localize("Volume unavailable for this player"), 10.0f);
	TextRender.SetFontPreset(PreviousFont);
	TextRender.SetRenderFlags(PreviousFlags);
	TextRender.TextColor(PreviousColor);
	TextRender.TextOutlineColor(PreviousOutline);
}
