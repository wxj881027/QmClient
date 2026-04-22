#include "voice_component.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
static constexpr int VOICE_OVERLAY_VISIBLE_MS = 180;
static constexpr int VOICE_OVERLAY_MAX_SPEAKERS = 5;
static constexpr const char *s_pVoiceOverlayMicIcon = "\xEF\x84\xB0";

void DrawVoiceOverlay(CGameClient *pGameClient, IGraphics *pGraphics, ITextRender *pTextRender, std::vector<SVoiceOverlayEntry> vEntries)
{
	if(!pGameClient || !pGraphics || !pTextRender)
		return;

	const bool HudEditorPreview = pGameClient->m_HudEditor.IsActive();
	if(vEntries.empty() && !HudEditorPreview)
		return;

	if(vEntries.empty())
	{
		SVoiceOverlayEntry &LocalEntry = vEntries.emplace_back();
		LocalEntry.m_ClientId = 0;
		LocalEntry.m_Order = 1;
		LocalEntry.m_IsLocal = true;
		str_copy(LocalEntry.m_aName, "You", sizeof(LocalEntry.m_aName));

		SVoiceOverlayEntry &TeammateEntry = vEntries.emplace_back();
		TeammateEntry.m_ClientId = 1;
		TeammateEntry.m_Order = 2;
		TeammateEntry.m_IsLocal = false;
		str_copy(TeammateEntry.m_aName, "Teammate", sizeof(TeammateEntry.m_aName));
	}

	struct SRenderEntry
	{
		SVoiceOverlayEntry m_Entry;
		float m_FullNameWidth = 0.0f;
		float m_NameWidth = 0.0f;
		float m_RowWidth = 0.0f;
	};

	std::vector<SRenderEntry> vRenderEntries;
	vRenderEntries.reserve(vEntries.size());
	for(const SVoiceOverlayEntry &Entry : vEntries)
	{
		SRenderEntry &RenderEntry = vRenderEntries.emplace_back();
		RenderEntry.m_Entry = Entry;
	}

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float HudWidth = 300.0f * pGraphics->ScreenAspect();
	const float HudHeight = 300.0f;
	pGraphics->MapScreen(0.0f, 0.0f, HudWidth, HudHeight);

	constexpr float PanelX = 6.0f;
	constexpr float PanelY = 74.0f;
	constexpr float RowHeight = 12.0f;
	constexpr float RowGap = 2.0f;
	constexpr float RowRadius = 5.0f;
	constexpr float RowPaddingX = 3.0f;
	constexpr float UserBoxWidth = 11.0f;
	constexpr float UserToNameGap = 2.5f;
	constexpr float NameToMicGap = 3.0f;
	constexpr float NameFontSize = 5.5f;
	constexpr float IconFontSize = 5.4f;
	constexpr float UserIconFontSize = 5.1f;
	constexpr float MaxNameWidth = 52.0f;

	const unsigned int PrevFlags = pTextRender->GetRenderFlags();
	const ColorRGBA PrevTextColor = pTextRender->GetTextColor();
	const ColorRGBA PrevOutlineColor = pTextRender->GetTextOutlineColor();
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.40f);

	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	const float UserIconWidth = pTextRender->TextWidth(UserIconFontSize, FontIcons::FONT_ICON_USERS);
	const float MicIconWidth = pTextRender->TextWidth(IconFontSize, s_pVoiceOverlayMicIcon);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);

	float PanelWidth = 0.0f;
	for(SRenderEntry &Entry : vRenderEntries)
	{
		Entry.m_FullNameWidth = std::round(pTextRender->TextBoundingBox(NameFontSize, Entry.m_Entry.m_aName).m_W);
		Entry.m_NameWidth = std::min(Entry.m_FullNameWidth, MaxNameWidth);
		Entry.m_RowWidth = RowPaddingX + UserBoxWidth + UserToNameGap + Entry.m_NameWidth + NameToMicGap + MicIconWidth + RowPaddingX;
		PanelWidth = std::max(PanelWidth, Entry.m_RowWidth);
	}

	const float PanelHeight = vRenderEntries.empty() ? 0.0f : vRenderEntries.size() * RowHeight + (vRenderEntries.size() - 1) * RowGap;
	const CUIRect PanelRect = {PanelX, PanelY, PanelWidth, PanelHeight};
	const auto HudEditorScope = pGameClient->m_HudEditor.BeginTransform(EHudEditorElement::VoiceOverlay, PanelRect);

	for(size_t Index = 0; Index < vRenderEntries.size(); ++Index)
	{
		const SRenderEntry &Entry = vRenderEntries[Index];
		const float RowY = PanelY + Index * (RowHeight + RowGap);

		ColorRGBA RowColor(0.10f, 0.11f, 0.14f, 0.82f);
		if(Entry.m_Entry.m_IsLocal)
			RowColor = ColorRGBA(0.12f, 0.13f, 0.17f, 0.88f);
		pGraphics->DrawRect(PanelX, RowY, Entry.m_RowWidth, RowHeight, RowColor, IGraphics::CORNER_ALL, RowRadius);

		const ColorRGBA UserBoxColor(1.0f, 1.0f, 1.0f, 0.10f);
		pGraphics->DrawRect(PanelX + 1.0f, RowY + 1.0f, UserBoxWidth, RowHeight - 2.0f, UserBoxColor, IGraphics::CORNER_ALL, RowRadius - 1.0f);

		const float UserIconX = PanelX + 1.0f + (UserBoxWidth - UserIconWidth) * 0.5f;
		const float UserIconY = RowY + (RowHeight - UserIconFontSize) * 0.5f - 0.5f;
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.82f);
		pTextRender->Text(UserIconX, UserIconY, UserIconFontSize, FontIcons::FONT_ICON_USERS, -1.0f);

		const float MicIconX = PanelX + Entry.m_RowWidth - RowPaddingX - MicIconWidth;
		const float MicIconY = RowY + (RowHeight - IconFontSize) * 0.5f - 0.5f;
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.90f);
		pTextRender->Text(MicIconX, MicIconY, IconFontSize, s_pVoiceOverlayMicIcon, -1.0f);

		const float NameX = PanelX + RowPaddingX + UserBoxWidth + UserToNameGap;
		const float NameY = RowY + (RowHeight - NameFontSize) * 0.5f - 0.5f;
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->TextColor(0.97f, 0.98f, 1.0f, 0.94f);
		if(Entry.m_NameWidth + 0.01f < Entry.m_FullNameWidth)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = NameFontSize;
			Cursor.m_LineWidth = MaxNameWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(NameX, NameY));
			pTextRender->TextEx(&Cursor, Entry.m_Entry.m_aName);
		}
		else
		{
			pTextRender->Text(NameX, NameY, NameFontSize, Entry.m_Entry.m_aName, -1.0f);
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
	if(!GameClient() || !Graphics() || !TextRender() || !g_Config.m_QmVoiceEnable || !g_Config.m_QmVoiceShowOverlay)
		return;

	const int64_t VisibleWindow = (int64_t)time_freq() * VOICE_OVERLAY_VISIBLE_MS / 1000;
	auto vEntries = m_OverlayState.CollectVisible(
		time_get(),
		VisibleWindow,
		g_Config.m_QmVoiceShowWhenActive != 0,
		VOICE_OVERLAY_MAX_SPEAKERS);
	DrawVoiceOverlay(GameClient(), Graphics(), TextRender(), std::move(vEntries));
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
