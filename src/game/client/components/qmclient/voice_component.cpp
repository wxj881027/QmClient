#include "voice_component.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
static constexpr int VOICE_OVERLAY_MEMBER_VISIBLE_MS = 7000;
static constexpr int VOICE_OVERLAY_MAX_SPEAKERS = MAX_CLIENTS;

void DrawVoiceOverlay(CGameClient *pGameClient, IGraphics *pGraphics, ITextRender *pTextRender, std::array<SVoiceOverlayEntry, MAX_CLIENTS> &aEntries, int EntryCount)
{
	if(!pGameClient || !pGraphics || !pTextRender)
		return;

	const bool HudEditorPreview = pGameClient->m_HudEditor.IsActive();
	if(EntryCount == 0 && !HudEditorPreview)
		return;

	if(EntryCount == 0)
	{
		SVoiceOverlayEntry &LocalEntry = aEntries[EntryCount++];
		LocalEntry.m_ClientId = 0;
		LocalEntry.m_Order = 1;
		LocalEntry.m_IsLocal = true;
		LocalEntry.m_Level = 0.82f;
		str_copy(LocalEntry.m_aName, "You", sizeof(LocalEntry.m_aName));

		SVoiceOverlayEntry &TeammateEntry = aEntries[EntryCount++];
		TeammateEntry.m_ClientId = 1;
		TeammateEntry.m_Order = 2;
		TeammateEntry.m_IsLocal = false;
		TeammateEntry.m_Level = 0.46f;
		str_copy(TeammateEntry.m_aName, "Teammate", sizeof(TeammateEntry.m_aName));
	}

	std::array<float, MAX_CLIENTS> aFullNameWidths;
	std::array<float, MAX_CLIENTS> aNameWidths;
	std::array<float, MAX_CLIENTS> aRowWidths;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float HudWidth = 300.0f * pGraphics->ScreenAspect();
	const float HudHeight = 300.0f;
	pGraphics->MapScreen(0.0f, 0.0f, HudWidth, HudHeight);

	constexpr float PanelX = 6.0f;
	constexpr float PanelY = 74.0f;
	constexpr float BaseRowHeight = 12.0f;
	constexpr float BaseRowGap = 2.0f;
	constexpr float BaseRowRadius = 5.0f;
	constexpr float BaseRowPaddingX = 4.0f;
	constexpr float BaseRowInnerInset = 0.8f;
	constexpr float BaseRowMinWidth = 20.0f;
	constexpr float BaseNameFontSize = 5.5f;
	constexpr float BaseMaxNameWidth = 56.0f;
	constexpr float BaseActivityExtraWidth = 18.0f;

	float LayoutScale = 1.0f;
	if(EntryCount > 5)
		LayoutScale = std::clamp(1.0f - 0.045f * (EntryCount - 5), 0.72f, 1.0f);

	const float MaxPanelHeight = HudHeight - PanelY - 8.0f;
	if(EntryCount > 0)
	{
		const float EstimatedPanelHeight = EntryCount * (BaseRowHeight * LayoutScale) + (EntryCount - 1) * (BaseRowGap * LayoutScale);
		if(EstimatedPanelHeight > MaxPanelHeight && EstimatedPanelHeight > 0.0f)
			LayoutScale = std::max(0.58f, LayoutScale * (MaxPanelHeight / EstimatedPanelHeight));
	}

	const float RowHeight = BaseRowHeight * LayoutScale;
	const float RowGap = BaseRowGap * LayoutScale;
	const float RowRadius = BaseRowRadius * LayoutScale;
	const float RowPaddingX = BaseRowPaddingX * LayoutScale;
	const float RowInnerInset = BaseRowInnerInset * LayoutScale;
	const float RowMinWidth = BaseRowMinWidth * LayoutScale;
	const float NameFontSize = BaseNameFontSize * LayoutScale;
	const float MaxNameWidth = BaseMaxNameWidth * LayoutScale;
	const float ActivityExtraWidth = BaseActivityExtraWidth * LayoutScale;

	const unsigned int PrevFlags = pTextRender->GetRenderFlags();
	const ColorRGBA PrevTextColor = pTextRender->GetTextColor();
	const ColorRGBA PrevOutlineColor = pTextRender->GetTextOutlineColor();
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.40f);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);

	float PanelWidth = 0.0f;
	for(int EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const SVoiceOverlayEntry &Entry = aEntries[EntryIndex];
		aFullNameWidths[EntryIndex] = std::round(pTextRender->TextBoundingBox(NameFontSize, Entry.m_aName).m_W);
		aNameWidths[EntryIndex] = std::min(aFullNameWidths[EntryIndex], MaxNameWidth);
		const float Activity = std::clamp(Entry.m_Level, 0.0f, 1.0f);
		const float BaseRowWidth = std::max(RowMinWidth, RowPaddingX + aNameWidths[EntryIndex] + RowPaddingX);
		aRowWidths[EntryIndex] = BaseRowWidth + ActivityExtraWidth * Activity;
		PanelWidth = std::max(PanelWidth, aRowWidths[EntryIndex]);
	}

	const float PanelHeight = EntryCount == 0 ? 0.0f : EntryCount * RowHeight + (EntryCount - 1) * RowGap;
	const CUIRect PanelRect = {PanelX, PanelY, PanelWidth, PanelHeight};
	const auto HudEditorScope = pGameClient->m_HudEditor.BeginTransform(EHudEditorElement::VoiceOverlay, PanelRect);

	for(int Index = 0; Index < EntryCount; ++Index)
	{
		const SVoiceOverlayEntry &Entry = aEntries[Index];
		const float RowY = PanelY + Index * (RowHeight + RowGap);

		ColorRGBA RowColor(0.10f, 0.11f, 0.14f, 0.82f);
		if(Entry.m_IsLocal)
			RowColor = ColorRGBA(0.12f, 0.13f, 0.17f, 0.88f);
		pGraphics->DrawRect(PanelX, RowY, aRowWidths[Index], RowHeight, RowColor, IGraphics::CORNER_ALL, RowRadius);

		const float Activity = std::clamp(Entry.m_Level, 0.0f, 1.0f);
		if(Activity > 0.001f)
		{
			const float FillWidth = std::max(1.0f, aRowWidths[Index] - RowInnerInset * 2.0f);
			const bool FillFullRow = FillWidth + RowInnerInset * 2.0f >= aRowWidths[Index] - 0.01f;
			const int FillCorners = FillFullRow ? IGraphics::CORNER_ALL : IGraphics::CORNER_L;
			ColorRGBA FillColor(0.36f, 0.70f, 0.98f, 0.24f + 0.22f * Activity);
			if(Entry.m_IsLocal)
				FillColor = ColorRGBA(0.48f, 0.82f, 1.0f, 0.28f + 0.24f * Activity);
			pGraphics->DrawRect(PanelX + RowInnerInset, RowY + RowInnerInset, FillWidth, RowHeight - RowInnerInset * 2.0f, FillColor, FillCorners, RowRadius - RowInnerInset);

			const float HighlightWidth = std::max(1.0f, FillWidth * (0.34f + 0.18f * Activity));
			const ColorRGBA HighlightColor(1.0f, 1.0f, 1.0f, 0.05f + 0.10f * Activity);
			pGraphics->DrawRect(PanelX + RowInnerInset, RowY + RowInnerInset, HighlightWidth, std::max(0.8f, 1.1f * LayoutScale), HighlightColor, FillCorners, 0.55f * LayoutScale);
		}

		const float NameX = PanelX + RowPaddingX;
		const float NameY = RowY + (RowHeight - NameFontSize) * 0.5f - 0.5f;
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->TextColor(0.97f, 0.98f, 1.0f, 0.94f);
		if(aNameWidths[Index] + 0.01f < aFullNameWidths[Index])
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = NameFontSize;
			Cursor.m_LineWidth = MaxNameWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(NameX, NameY));
			pTextRender->TextEx(&Cursor, Entry.m_aName);
		}
		else
		{
			pTextRender->Text(NameX, NameY, NameFontSize, Entry.m_aName, -1.0f);
		}
	}

	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(PrevTextColor);
	pTextRender->TextOutlineColor(PrevOutlineColor);
	pTextRender->SetRenderFlags(PrevFlags);
	pGameClient->m_HudEditor.UpdateVisibleRect(EHudEditorElement::VoiceOverlay, PanelRect);
	pGameClient->m_HudEditor.EndTransform(HudEditorScope);
	pGraphics->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}
} // namespace

void CVoiceComponent::ConVoicePtt(IConsole::IResult *pResult, void *pUserData)
{
	CVoiceComponent *pThis = static_cast<CVoiceComponent *>(pUserData);
	pThis->m_Voice.SetPttActive(pResult->GetInteger(0) != 0);
}

void CVoiceComponent::ConVoiceListDevices(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CVoiceComponent *pThis = static_cast<CVoiceComponent *>(pUserData);
	pThis->m_Voice.ListDevices();
}

void CVoiceComponent::ConVoiceSetInputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	str_copy(g_Config.m_QmVoiceInputDevice, pResult->GetString(0), sizeof(g_Config.m_QmVoiceInputDevice));
}

void CVoiceComponent::ConVoiceSetOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	str_copy(g_Config.m_QmVoiceOutputDevice, pResult->GetString(0), sizeof(g_Config.m_QmVoiceOutputDevice));
}

void CVoiceComponent::ConVoiceClearInputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceInputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceClearOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceOutputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceToggleMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceMicMute = g_Config.m_QmVoiceMicMute ? 0 : 1;
}

void CVoiceComponent::ConVoiceSetMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	g_Config.m_QmVoiceMicMute = pResult->GetInteger(0) != 0 ? 1 : 0;
}

void CVoiceComponent::OnInit()
{
	m_OverlayState.Reset();
	m_Voice.Init(GameClient(), Client(), Console());
}

void CVoiceComponent::OnShutdown()
{
	m_Voice.OnShutdown();
	m_OverlayState.Reset();
}

void CVoiceComponent::OnRender()
{
	if(!g_Config.m_QmVoiceEnable)
	{
		m_Voice.Shutdown();
		m_OverlayState.Reset();
		return;
	}

	m_Voice.OnFrame();
	m_Voice.ExportOverlayState(m_OverlayState);
}

void CVoiceComponent::RenderOverlay()
{
	if(!GameClient() || !Graphics() || !TextRender() || !g_Config.m_QmVoiceEnable)
		return;

	const int64_t VisibleWindow = (int64_t)time_freq() * VOICE_OVERLAY_MEMBER_VISIBLE_MS / 1000;
	std::array<SVoiceOverlayEntry, MAX_CLIENTS> aEntries;
	const int EntryCount = m_OverlayState.CollectVisible(
		time_get(),
		VisibleWindow,
		true,
		VOICE_OVERLAY_MAX_SPEAKERS,
		aEntries);
	DrawVoiceOverlay(GameClient(), Graphics(), TextRender(), aEntries, EntryCount);
}

void CVoiceComponent::OnConsoleInit()
{
	Console()->Register("+qm_voice_ptt", "", CFGFLAG_CLIENT, ConVoicePtt, this, "Push-to-talk for voice chat");
	Console()->Register("qm_voice_list_devices", "", CFGFLAG_CLIENT, ConVoiceListDevices, this, "List available voice devices");
	Console()->Register("qm_voice_set_input_device", "s[name]", CFGFLAG_CLIENT, ConVoiceSetInputDevice, this, "Set voice input device");
	Console()->Register("qm_voice_set_output_device", "s[name]", CFGFLAG_CLIENT, ConVoiceSetOutputDevice, this, "Set voice output device");
	Console()->Register("qm_voice_clear_input_device", "", CFGFLAG_CLIENT, ConVoiceClearInputDevice, this, "Use default voice input device");
	Console()->Register("qm_voice_clear_output_device", "", CFGFLAG_CLIENT, ConVoiceClearOutputDevice, this, "Use default voice output device");
	Console()->Register("qm_voice_toggle_mic", "", CFGFLAG_CLIENT, ConVoiceToggleMicMute, this, "Toggle microphone mute");
	Console()->Register("qm_voice_set_mic", "i[state]", CFGFLAG_CLIENT, ConVoiceSetMicMute, this, "Set microphone mute state (0=on, 1=mute)");
}
