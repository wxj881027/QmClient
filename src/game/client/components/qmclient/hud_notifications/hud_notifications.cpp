// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "hud_notifications.h"

#include <base/color.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/hud_editor.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

namespace
{
	float SmoothStep(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	ColorRGBA ApplyAlpha(ColorRGBA Color, float Alpha)
	{
		Color.a *= std::clamp(Alpha, 0.0f, 1.0f);
		return Color;
	}
} // namespace

int CQmHudNotifications::BuildVisibleNotificationList(bool PreviewContent, const SNotification *(&apVisible)[8], SNotification &PreviewNotification)
{
	int NumVisible = 0;
	if(PreviewContent)
	{
		PreviewNotification.m_Kind = EKind::SoloEnter;
		str_copy(PreviewNotification.m_aText, Localize("You are now in a solo part"));
		PreviewNotification.m_StartTime = time_get();
		apVisible[NumVisible++] = &PreviewNotification;
	}
	else
	{
		const int MaxVisible = QmHudNotifications::ClampVisibleCount(g_Config.m_QmHudNotificationsMaxVisible);
		for(int SourceIndex = (int)m_vNotifications.size() - 1; SourceIndex >= 0 && NumVisible < MaxVisible; --SourceIndex)
			apVisible[NumVisible++] = &m_vNotifications[SourceIndex];
	}
	return NumVisible;
}

CUIRect CQmHudNotifications::MeasureVisibleRect(const CUIRect &BaseRect, bool PreviewContent, QmHudNotifications::EHorizontalFlow Flow, bool StableEditorGeometry)
{
	const float FontSize = (float)QmHudNotifications::ClampTextSize(g_Config.m_QmHudNotificationsTextSize);
	const float PaddingX = QmHudNotifications::PaddingX(FontSize);
	const float PaddingY = QmHudNotifications::PaddingY(FontSize);
	const float MinBoxWidth = QmHudNotifications::MinBoxWidth(FontSize);
	const float Gap = 4.0f * QmHudNotifications::SmallTextScale(FontSize);
	const float TextMaxWidth = maximum(1.0f, BaseRect.w - PaddingX * 2.0f);
	(void)StableEditorGeometry;

	SNotification PreviewNotification;
	const SNotification *apVisible[8] = {};
	const int NumVisible = BuildVisibleNotificationList(PreviewContent, apVisible, PreviewNotification);
	if(NumVisible == 0)
		return {BaseRect.x, BaseRect.y, 0.0f, 0.0f};

	float MaxWidth = 0.0f;
	float UsedHeight = 0.0f;
	for(int i = 0; i < NumVisible; ++i)
	{
		const STextBoundingBox TextBox = TextRender()->TextBoundingBox(FontSize, apVisible[i]->m_aText, -1, TextMaxWidth);
		const float BoxW = minimum(BaseRect.w, maximum(MinBoxWidth, TextBox.m_W + PaddingX * 2.0f));
		const float BoxH = maximum(FontSize + PaddingY * 2.0f, TextBox.m_H + PaddingY * 2.0f);
		MaxWidth = maximum(MaxWidth, BoxW);
		UsedHeight += BoxH + (i + 1 < NumVisible ? Gap : 0.0f);
	}

	return QmHudNotifications::NotificationVisibleRect(BaseRect, MaxWidth, UsedHeight, Flow);
}

CQmHudNotifications::SEditorPreviewMetrics CQmHudNotifications::MeasureEditorPreviewMetrics(const CUIRect &BaseRect)
{
	const float FontSize = (float)QmHudNotifications::ClampTextSize(g_Config.m_QmHudNotificationsTextSize);
	const float PaddingX = QmHudNotifications::PaddingX(FontSize);
	const float PaddingY = QmHudNotifications::PaddingY(FontSize);
	const float MinBoxWidth = QmHudNotifications::MinBoxWidth(FontSize);
	const float Gap = 4.0f * QmHudNotifications::SmallTextScale(FontSize);
	const float TextMaxWidth = maximum(1.0f, BaseRect.w - PaddingX * 2.0f);
	const int PreviewCount = QmHudNotifications::ClampVisibleCount(g_Config.m_QmHudNotificationsMaxVisible);

	SNotification PreviewNotification;
	const SNotification *apVisible[8] = {};
	const int NumVisible = BuildVisibleNotificationList(true, apVisible, PreviewNotification);
	if(NumVisible == 0)
		return {};

	const STextBoundingBox TextBox = TextRender()->TextBoundingBox(FontSize, apVisible[0]->m_aText, -1, TextMaxWidth);
	const float BoxW = minimum(BaseRect.w, maximum(MinBoxWidth, TextBox.m_W + PaddingX * 2.0f));
	const float BoxH = maximum(FontSize + PaddingY * 2.0f, TextBox.m_H + PaddingY * 2.0f);
	return {BoxW, BoxH, Gap, PreviewCount, true};
}

CUIRect CQmHudNotifications::MeasureEditorPreviewRect(const CUIRect &BaseRect, QmHudNotifications::EHorizontalFlow Flow)
{
	const SEditorPreviewMetrics Metrics = MeasureEditorPreviewMetrics(BaseRect);
	if(!Metrics.m_Valid)
		return {BaseRect.x, BaseRect.y, 0.0f, 0.0f};
	return QmHudNotifications::EditorPreviewVisibleRect(BaseRect, Metrics.m_BoxWidth, Metrics.m_BoxHeight, Metrics.m_Gap, Metrics.m_VisibleCount, Flow);
}

CUIRect CQmHudNotifications::MeasureEditorPreviewDragRect(const CUIRect &BaseRect)
{
	const SEditorPreviewMetrics Metrics = MeasureEditorPreviewMetrics(BaseRect);
	if(!Metrics.m_Valid)
		return {BaseRect.x, BaseRect.y, 0.0f, 0.0f};
	return QmHudNotifications::EditorPreviewDragRect(BaseRect, Metrics.m_BoxWidth, Metrics.m_BoxHeight, Metrics.m_Gap, Metrics.m_VisibleCount);
}

void CQmHudNotifications::OnReset()
{
	m_vNotifications.clear();
	m_HasLastSolo = false;
	m_LastSolo = false;
	m_PendingCompatPrompt = QmHudNotifications::ESoloPrompt::None;
	m_PendingCompatUntil = 0;
}

void CQmHudNotifications::OnRelease()
{
	OnReset();
}

void CQmHudNotifications::OnNewSnapshot()
{
	bool Solo = false;
	if(!LocalSoloState(Solo))
	{
		m_HasLastSolo = false;
		return;
	}

	if(!m_HasLastSolo)
	{
		m_HasLastSolo = true;
		m_LastSolo = Solo;
		return;
	}

	if(Solo == m_LastSolo)
		return;

	const auto Prompt = Solo ? QmHudNotifications::ESoloPrompt::Enter : QmHudNotifications::ESoloPrompt::Leave;
	if(g_Config.m_QmHudNotificationsCompatSolo)
	{
		m_PendingCompatPrompt = Prompt;
		m_PendingCompatUntil = time_get() + time_freq() / 2;
	}
	m_LastSolo = Solo;
}

void CQmHudNotifications::OnRender()
{
	const bool HudEditorPreview = GameClient()->m_HudEditor.IsActive();
	const bool PreviewContent = HudEditorPreview && m_vNotifications.empty();
	if(!HudEditorPreview && m_vNotifications.empty())
		return;
	if(!HudEditorPreview && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
#if defined(CONF_VIDEORECORDER)
	// 通知内容来自聊天消息（被路由成通知的聊天行会被抑制），因此渲染（录制）视频时
	// 跟随聊天框的 cl_video_showchat，避免出现「聊天被抑制但通知又不录」的丢消息
	if(!HudEditorPreview && IVideo::Current() && !g_Config.m_ClVideoShowChat)
		return;
#endif

	const int HoldMs = QmHudNotifications::ClampHoldMs(g_Config.m_QmHudNotificationsHoldMs);
	const int AnimMs = QmHudNotifications::ClampAnimationMs(g_Config.m_QmHudNotificationsAnimMs);
	const int64_t Now = time_get();
	const int64_t Lifetime = (int64_t)(HoldMs + 2 * AnimMs) * time_freq() / 1000;
	while(!m_vNotifications.empty() && Now - m_vNotifications.front().m_StartTime > Lifetime)
		m_vNotifications.pop_front();
	if(!HudEditorPreview && m_vNotifications.empty())
		return;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	float SavedScreenX0 = 0.0f;
	float SavedScreenY0 = 0.0f;
	float SavedScreenX1 = 0.0f;
	float SavedScreenY1 = 0.0f;
	Graphics()->GetScreen(&SavedScreenX0, &SavedScreenY0, &SavedScreenX1, &SavedScreenY1);
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	CUIRect DefaultRect;
	DefaultRect.w = 172.0f;
	DefaultRect.h = 68.0f;
	DefaultRect.x = Width - DefaultRect.w - 8.0f;
	DefaultRect.y = 96.0f;

	const CUIRect AnchorRect = MeasureEditorPreviewDragRect(DefaultRect);
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform((float)std::clamp(g_Config.m_QmHudNotificationsEdgeMargin, 0, 32));
	const auto PreviewScope = GameClient()->m_HudEditor.PreviewTransform(EHudEditorElement::HudNotifications, AnchorRect, AnchorRect, Margin);
	auto Flow = QmHudNotifications::ResolveHorizontalFlow(
		PreviewScope.m_AnchoredLeft,
		PreviewScope.m_AnchoredRight,
		PreviewScope.m_VisibleRect.x + PreviewScope.m_VisibleRect.w * 0.5f,
		Width * 0.5f);
	CUIRect RenderAnchorRect = QmHudEditor::ApplyEdgeMargin(
		AnchorRect,
		Margin,
		PreviewScope.m_AnchoredLeft,
		PreviewScope.m_AnchoredRight,
		PreviewScope.m_AnchoredTop,
		PreviewScope.m_AnchoredBottom);
	CUIRect RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(RenderAnchorRect, DefaultRect.w, Flow);
	CUIRect ReportedVisibleRect = MeasureVisibleRect(RenderBaseRect, PreviewContent, Flow, HudEditorPreview);
	if(ReportedVisibleRect.w <= 0.0f || ReportedVisibleRect.h <= 0.0f)
		ReportedVisibleRect = AnchorRect;
	const auto VisiblePreviewScope = GameClient()->m_HudEditor.PreviewTransform(EHudEditorElement::HudNotifications, AnchorRect, ReportedVisibleRect, Margin);
	const auto VisibleFlow = QmHudNotifications::ResolveHorizontalFlow(
		VisiblePreviewScope.m_AnchoredLeft,
		VisiblePreviewScope.m_AnchoredRight,
		VisiblePreviewScope.m_VisibleRect.x + VisiblePreviewScope.m_VisibleRect.w * 0.5f,
		Width * 0.5f);
	Flow = VisibleFlow;
	RenderAnchorRect = QmHudEditor::ApplyEdgeMargin(
		AnchorRect,
		Margin,
		VisiblePreviewScope.m_AnchoredLeft,
		VisiblePreviewScope.m_AnchoredRight,
		VisiblePreviewScope.m_AnchoredTop,
		VisiblePreviewScope.m_AnchoredBottom);
	RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(RenderAnchorRect, DefaultRect.w, Flow);
	ReportedVisibleRect = MeasureVisibleRect(RenderBaseRect, PreviewContent, Flow, HudEditorPreview);
	if(ReportedVisibleRect.w <= 0.0f || ReportedVisibleRect.h <= 0.0f)
		ReportedVisibleRect = RenderAnchorRect;
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::HudNotifications, AnchorRect, ReportedVisibleRect, Margin);
	RenderNotifications(RenderBaseRect, ReportedVisibleRect, PreviewContent, HudEditorPreview, Flow);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
	Graphics()->MapScreen(SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1);
}

bool CQmHudNotifications::QueueEcho(const char *pMessage, unsigned EchoColor)
{
	if(!g_Config.m_QmHudNotificationsEcho)
		return false;
	if(pMessage == nullptr || pMessage[0] == '\0')
		return false;

	// 队列里只保存已经解析好的文本和颜色，渲染阶段不再依赖后续配置状态。
	const QmHudNotifications::SEchoNotificationPayload Payload = QmHudNotifications::BuildEchoNotificationPayload(pMessage, EchoColor);
	if(Payload.m_aText[0] == '\0')
		return false;
	Queue(EKind::Echo, Payload.m_aText, Payload.m_Color, true);
	return true;
}

bool CQmHudNotifications::ShouldSuppressServerChat(const char *pMessage)
{
	return HandleServerChat(pMessage, CurrentRouteConfig());
}

bool CQmHudNotifications::LocalSoloState(bool &Solo) const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return false;
	if(!GameClient()->m_aClients[LocalId].m_Active)
		return false;
	Solo = GameClient()->m_aClients[LocalId].m_Solo;
	return true;
}

void CQmHudNotifications::RenderNotifications(const CUIRect &BaseRect, const CUIRect &VisibleRect, bool PreviewContent, bool StableEditorGeometry, QmHudNotifications::EHorizontalFlow Flow)
{
	const int HoldMs = QmHudNotifications::ClampHoldMs(g_Config.m_QmHudNotificationsHoldMs);
	const int AnimMs = QmHudNotifications::ClampAnimationMs(g_Config.m_QmHudNotificationsAnimMs);
	const int AnimType = std::clamp(g_Config.m_QmHudNotificationsAnimType, 0, 2);
	const float FontSize = (float)QmHudNotifications::ClampTextSize(g_Config.m_QmHudNotificationsTextSize);
	const float PaddingX = QmHudNotifications::PaddingX(FontSize);
	const float PaddingY = QmHudNotifications::PaddingY(FontSize);
	const float MinBoxWidth = QmHudNotifications::MinBoxWidth(FontSize);
	const float Gap = 4.0f * QmHudNotifications::SmallTextScale(FontSize);
	const float TextMaxWidth = maximum(1.0f, BaseRect.w - PaddingX * 2.0f);
	const int64_t Now = time_get();

	SNotification PreviewNotification;
	const SNotification *apVisible[8] = {};
	const int NumVisible = BuildVisibleNotificationList(PreviewContent, apVisible, PreviewNotification);

	if(NumVisible == 0)
		return;

	ColorRGBA BgColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudNotificationsBgColor, true));
	float Y = BaseRect.y;

	for(int i = 0; i < NumVisible; ++i)
	{
		const SNotification &Notification = *apVisible[i];
		const int ElapsedMs = (PreviewContent || StableEditorGeometry) ? AnimMs : (int)((Now - Notification.m_StartTime) * 1000 / time_freq());
		float Alpha = 1.0f;
		float OffsetX = 0.0f;
		if(!StableEditorGeometry && AnimType != 2 && AnimMs > 0)
		{
			if(ElapsedMs < AnimMs)
				Alpha = SmoothStep(ElapsedMs / (float)AnimMs);
			else if(ElapsedMs > AnimMs + HoldMs)
				Alpha = 1.0f - SmoothStep((ElapsedMs - AnimMs - HoldMs) / (float)AnimMs);
			if(AnimType == 0)
				OffsetX = (1.0f - Alpha) * 14.0f * QmHudNotifications::SmallTextScale(FontSize);
		}

		const STextBoundingBox TextBox = TextRender()->TextBoundingBox(FontSize, Notification.m_aText, -1, TextMaxWidth);
		const bool HasRepeatText = Notification.m_aRepeatText[0] != '\0';
		const STextBoundingBox RepeatTextBox = HasRepeatText ? TextRender()->TextBoundingBox(FontSize, Notification.m_aRepeatText, -1, TextMaxWidth) : STextBoundingBox{};
		const float RepeatGap = HasRepeatText ? 4.0f * QmHudNotifications::SmallTextScale(FontSize) : 0.0f;
		const float NaturalBoxW = maximum(MinBoxWidth, TextBox.m_W + RepeatGap + RepeatTextBox.m_W + PaddingX * 2.0f);
		const float BoxW = QmHudNotifications::NotificationBoxWidth(BaseRect, NaturalBoxW);
		const float BoxH = maximum(FontSize + PaddingY * 2.0f, TextBox.m_H + PaddingY * 2.0f);
		CUIRect Box = {QmHudNotifications::NotificationBoxX(BaseRect, BoxW, Flow, OffsetX), Y, BoxW, BoxH};
		const float CornerRadius = minimum(6.0f, BoxH / 2.0f);
		Box.Draw(ApplyAlpha(BgColor, Alpha), IGraphics::CORNER_ALL, CornerRadius);

		const QmHudNotifications::ETextSource TextSource = Notification.m_Kind == EKind::Echo ? QmHudNotifications::ETextSource::Echo : QmHudNotifications::ETextSource::System;
		const unsigned EchoColor = Notification.m_HasEchoColor ? Notification.m_EchoColor : g_Config.m_ClMessageClientColor;
		const QmHudNotifications::STextColorConfig TextColorConfig = QmHudNotifications::TextColorConfig(TextSource, g_Config.m_QmHudNotificationsEchoInheritColor, g_Config.m_QmHudNotificationsTextColor, g_Config.m_QmHudNotificationsEchoTextColor, EchoColor);
		const ColorRGBA TextColor = color_cast<ColorRGBA>(ColorHSLA(TextColorConfig.m_Color, TextColorConfig.m_HasAlpha));
		TextRender()->TextColor(ApplyAlpha(TextColor, Alpha));
		TextRender()->Text(Box.x + PaddingX, Box.y + PaddingY, FontSize, Notification.m_aText, TextMaxWidth);
		if(HasRepeatText)
		{
			const int RepeatElapsedMs = Notification.m_RepeatAnimTime > 0 ? (int)((Now - Notification.m_RepeatAnimTime) * 1000 / time_freq()) : AnimMs;
			float RepeatScale = QmHudNotifications::RepeatCountElasticScale(AnimMs > 0 ? RepeatElapsedMs / (float)AnimMs : 1.0f);
			const float RepeatX = Box.x + PaddingX + TextBox.m_W + RepeatGap;
			TextRender()->Text(RepeatX, Box.y + PaddingY, FontSize * RepeatScale, Notification.m_aRepeatText, TextMaxWidth);
		}

		Y += BoxH + Gap;
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::HudNotifications, VisibleRect);
}
