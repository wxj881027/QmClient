/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SettingsCardGeometry.h"

#include "UiTokens.h"

#include <algorithm>

SSettingsCardFrame BuildSettingsCardFrame(const CUIRect &Slot, const SSettingsCardSpec &Spec, const float ContentHeight, const float UiScale)
{
	const float Scale = std::isfinite(UiScale) && UiScale > 0.0f ? UiScale : 1.0f;
	const float Padding = ui_token::settings::CARD_PADDING * Scale;
	const float TitleHeight = ui_token::settings::CARD_HEADER_TITLE_HEIGHT * Scale;
	// 为悬浮副标题固定预留空间，鼠标进入或离开时不移动正文和后续卡片。
	const float SubtitleHeight = Spec.m_pSubtitle != nullptr && Spec.m_pSubtitle[0] != '\0' ? ui_token::settings::CARD_HEADER_SUBTITLE_HEIGHT * Scale : 0.0f;
	const float SummaryGap = 0.0f;
	const float HeaderGap = ui_token::settings::CARD_HEADER_GAP * Scale;
	const float HeaderHeight = TitleHeight + SummaryGap + SubtitleHeight;
	const float SafeContentHeight = std::max(0.0f, ContentHeight);

	SSettingsCardFrame Frame{};
	Frame.m_Rect = {Slot.x, Slot.y, std::max(0.0f, Slot.w), Padding + HeaderHeight + HeaderGap + SafeContentHeight + Padding};
	Frame.m_HeaderRect = {Frame.m_Rect.x + Padding, Frame.m_Rect.y + Padding, std::max(0.0f, Frame.m_Rect.w - Padding * 2.0f), HeaderHeight};
	const float HandleSize = std::min({ui_token::settings::CARD_HANDLE_SIZE * Scale, TitleHeight, Frame.m_HeaderRect.w});
	Frame.m_HandleRect = {Frame.m_HeaderRect.x + Frame.m_HeaderRect.w - HandleSize, Frame.m_HeaderRect.y, HandleSize, HandleSize};
	const float TextWidth = std::max(0.0f, Frame.m_HeaderRect.w - HandleSize - Padding);
	Frame.m_TitleRect = {Frame.m_HeaderRect.x, Frame.m_HeaderRect.y, TextWidth, TitleHeight};
	// 副标题独占标题下方的一行，绘制时沿用限宽和省略兜底。
	Frame.m_SubtitleRect = {Frame.m_HeaderRect.x, Frame.m_HeaderRect.y + TitleHeight + SummaryGap, Frame.m_HeaderRect.w, SubtitleHeight};
	Frame.m_ContentRect = {Frame.m_Rect.x + Padding, Frame.m_HeaderRect.y + HeaderHeight + HeaderGap, Frame.m_HeaderRect.w, SafeContentHeight};
	return Frame;
}

SCardMotionSpec ResolveCardMotionSpec(const int MotionLevel, const bool ListEntryAnimations, const bool CardHeightAnimations, const bool CardReflowAnimations, const bool PresentationAnimations)
{
	const int Level = std::clamp(MotionLevel, 0, 2);
	SCardMotionSpec Spec{};
	if(Level == 0)
		return Spec;

	if(Level == 1)
	{
		if(ListEntryAnimations)
		{
			Spec.m_EntryDistance = 8.0f;
			Spec.m_EntryDuration = 0.14f;
		}
		if(CardHeightAnimations)
			Spec.m_ContentHeightDuration = 0.12f;
		if(CardReflowAnimations)
			Spec.m_ReflowDuration = 0.12f;
		return Spec;
	}

	if(ListEntryAnimations)
	{
		Spec.m_EntryDistance = 16.0f;
		Spec.m_EntryDuration = 0.22f;
	}
	if(CardHeightAnimations)
		Spec.m_ContentHeightDuration = 0.18f;
	if(CardReflowAnimations)
		Spec.m_ReflowDuration = 0.18f;
	Spec.m_DecorativeMotion = PresentationAnimations;
	return Spec;
}
