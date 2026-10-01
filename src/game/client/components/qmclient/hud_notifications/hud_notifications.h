// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATIONS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATIONS_H

#include "hud_notification_rules.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/component.h>
#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>

#include <algorithm>
#include <deque>

namespace QmHudNotifications
{
	enum class EHorizontalFlow
	{
		LeftToRight,
		RightToLeft,
	};

	enum class ETextSource
	{
		System,
		Echo,
	};

	struct STextColorConfig
	{
		unsigned m_Color = 0;
		bool m_HasAlpha = false;
	};

	struct SEchoNotificationPayload
	{
		char m_aText[256] = {};
		unsigned m_Color = 0;
	};

	// Echo 的颜色要在入队时固化，否则后续 bind 恢复颜色后会把旧通知一起改掉。
	inline SEchoNotificationPayload BuildEchoNotificationPayload(const char *pMessage, unsigned FallbackColor)
	{
		SEchoNotificationPayload Payload;
		Payload.m_Color = FallbackColor;
		if(pMessage == nullptr || pMessage[0] == '\0')
			return Payload;

		CColoredParts ColoredParts(pMessage, true);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Payload.m_Color = color_cast<ColorHSLA>(ColoredParts.Colors()[0].m_Color).Pack(false);
		str_copy(Payload.m_aText, ColoredParts.Text(), sizeof(Payload.m_aText));
		return Payload;
	}

	inline int ClampVisibleCount(int Value)
	{
		return std::clamp(Value, 1, 8);
	}

	inline int ClampHoldMs(int Value)
	{
		return std::clamp(Value, 500, 10000);
	}

	inline int ClampAnimationMs(int Value)
	{
		return std::clamp(Value, 0, 2000);
	}

	inline int ClampTextSize(int Value)
	{
		return std::clamp(Value, 1, 24);
	}

	inline float SmallTextScale(float FontSize)
	{
		return std::clamp(FontSize / 8.0f, 0.33f, 1.0f);
	}

	inline float PaddingX(float FontSize)
	{
		return 4.0f * SmallTextScale(FontSize);
	}

	inline float PaddingY(float FontSize)
	{
		return 2.5f * SmallTextScale(FontSize);
	}

	inline float MinBoxWidth(float FontSize)
	{
		return 82.0f * SmallTextScale(FontSize);
	}

	inline float RepeatCountElasticScale(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float Overshoot = std::sin(t * pi) * 0.28f;
		return 1.0f + Overshoot * (1.0f - t);
	}

	inline EHorizontalFlow ResolveHorizontalFlow(bool AnchoredLeft, bool AnchoredRight, float VisibleCenterX, float ScreenCenterX)
	{
		if(AnchoredLeft)
			return EHorizontalFlow::LeftToRight;
		if(AnchoredRight)
			return EHorizontalFlow::RightToLeft;
		return VisibleCenterX <= ScreenCenterX ? EHorizontalFlow::LeftToRight : EHorizontalFlow::RightToLeft;
	}

	inline float NotificationBoxX(const CUIRect &BaseRect, float BoxW, EHorizontalFlow Flow, float SlideOffset)
	{
		if(Flow == EHorizontalFlow::LeftToRight)
			return BaseRect.x - SlideOffset;
		return BaseRect.x + BaseRect.w - BoxW + SlideOffset;
	}

	inline float StackedVisibleHeight(float BoxHeight, float Gap, int VisibleCount)
	{
		if(VisibleCount <= 0)
			return 0.0f;
		return BoxHeight * VisibleCount + Gap * (VisibleCount - 1);
	}

	inline float NotificationBoxWidth(const CUIRect &BaseRect, float NaturalBoxWidth)
	{
		return minimum(BaseRect.w, NaturalBoxWidth);
	}

	inline CUIRect NotificationVisibleRect(const CUIRect &BaseRect, float VisibleWidth, float UsedHeight, EHorizontalFlow Flow, float MaxSlideOffset = 0.0f)
	{
		const float Width = maximum(0.0f, VisibleWidth) + maximum(0.0f, MaxSlideOffset);
		const float Height = maximum(0.0f, UsedHeight);
		const float X = Flow == EHorizontalFlow::LeftToRight ? BaseRect.x - maximum(0.0f, MaxSlideOffset) : BaseRect.x + BaseRect.w - maximum(0.0f, VisibleWidth);
		return {X, BaseRect.y, Width, Height};
	}

	inline CUIRect EditorPreviewVisibleRect(const CUIRect &BaseRect, float StableWidth, float BoxHeight, float Gap, int VisibleCount, EHorizontalFlow Flow)
	{
		return NotificationVisibleRect(BaseRect, StableWidth, StackedVisibleHeight(BoxHeight, Gap, VisibleCount), Flow);
	}

	inline CUIRect EditorPreviewDragRect(const CUIRect &BaseRect, float StableWidth, float BoxHeight, float Gap, int VisibleCount)
	{
		return {BaseRect.x, BaseRect.y, maximum(0.0f, StableWidth), StackedVisibleHeight(BoxHeight, Gap, VisibleCount)};
	}

	inline CUIRect EditorPreviewRenderBaseRect(const CUIRect &AnchorRect, float RenderWidth, EHorizontalFlow Flow)
	{
		const float Width = maximum(AnchorRect.w, RenderWidth);
		const float X = Flow == EHorizontalFlow::LeftToRight ? AnchorRect.x : AnchorRect.x + AnchorRect.w - Width;
		return {X, AnchorRect.y, Width, AnchorRect.h};
	}

	inline CUIRect InsetAnchoredRect(const CUIRect &Rect, float Margin, bool AnchoredLeft, bool AnchoredRight, bool AnchoredTop, bool AnchoredBottom)
	{
		return QmHudEditor::ApplyEdgeMargin(Rect, QmHudEditor::SEdgeMargin::Uniform(Margin), AnchoredLeft, AnchoredRight, AnchoredTop, AnchoredBottom);
	}

	inline STextColorConfig TextColorConfig(ETextSource Source, int EchoInheritChatColor, unsigned SystemColor, unsigned EchoOverrideColor, unsigned ChatEchoColor)
	{
		if(Source == ETextSource::Echo && EchoInheritChatColor)
			return {ChatEchoColor, false};
		if(Source == ETextSource::Echo)
			return {EchoOverrideColor, true};
		return {SystemColor, true};
	}
} // namespace QmHudNotifications

class CQmHudNotifications : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRelease() override;
	void OnNewSnapshot() override;
	void OnRender() override;

	static bool HandleServerChatCoreForTests(QmHudNotifications::ESoloPrompt &PendingCompatPrompt, int64_t &PendingCompatUntil, bool CompatSoloEnabled, const char *pMessage, const QmHudNotifications::SServerMessageRouteConfig &RouteConfig, QmHudNotifications::SServerMessageAnalysis *pAnalysisResult = nullptr, QmHudNotifications::SServerMessageEntryDecision *pDecisionResult = nullptr)
	{
		if(CompatSoloEnabled && PendingCompatPrompt != QmHudNotifications::ESoloPrompt::None && time_get() > PendingCompatUntil)
			PendingCompatPrompt = QmHudNotifications::ESoloPrompt::None;

		const QmHudNotifications::SServerMessageAnalysis Analysis = QmHudNotifications::AnalyzeServerMessage(pMessage, PendingCompatPrompt);
		const QmHudNotifications::SServerMessageEntryDecision Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, RouteConfig);
		if(Decision.m_ClearPendingCompatPrompt)
			PendingCompatPrompt = QmHudNotifications::ESoloPrompt::None;
		if(pAnalysisResult != nullptr)
			*pAnalysisResult = Analysis;
		if(pDecisionResult != nullptr)
			*pDecisionResult = Decision;
		return Decision.m_QueueNotification;
	}

	bool QueueEcho(const char *pMessage, unsigned EchoColor);
	bool HandleServerChat(const char *pMessage, const QmHudNotifications::SServerMessageRouteConfig &RouteConfig, QmHudNotifications::SServerMessageAnalysis *pAnalysisResult = nullptr)
	{
		QmHudNotifications::SServerMessageAnalysis Analysis;
		QmHudNotifications::SServerMessageEntryDecision Decision;
		if(!HandleServerChatCoreForTests(m_PendingCompatPrompt, m_PendingCompatUntil, g_Config.m_QmHudNotificationsCompatSolo != 0, pMessage, RouteConfig, &Analysis, &Decision))
		{
			if(pAnalysisResult != nullptr)
				*pAnalysisResult = Analysis;
			return false;
		}
		if(pAnalysisResult != nullptr)
			*pAnalysisResult = Analysis;
		if(Analysis.m_Route == QmHudNotifications::EServerMessageRoute::Solo)
		{
			QueueSolo(Analysis.m_SoloPrompt);
			return true;
		}
		if(Decision.m_UseFallbackNotification)
		{
			Queue(EKind::System, Localize(pMessage));
			return true;
		}
		if(Analysis.m_aLocalizedText[0] != '\0')
		{
			Queue(EKind::System, Analysis.m_aLocalizedText);
			return true;
		}
		return false;
	}
	bool HandleServerChat(const char *pMessage, bool RouteSystemMessages, QmHudNotifications::SServerMessageAnalysis *pAnalysisResult = nullptr)
	{
		QmHudNotifications::SServerMessageRouteConfig RouteConfig;
		RouteConfig.m_RouteSystemMessages = RouteSystemMessages;
		return HandleServerChat(pMessage, RouteConfig, pAnalysisResult);
	}
	// 这些测试接口只暴露入口级副作用，避免测试重新退回 helper 层，确保能直接验证 HandleServerChat 的真实行为。
	int NotificationCountForTests() const
	{
		return (int)m_vNotifications.size();
	}
	const char *LastNotificationTextForTests() const
	{
		return m_vNotifications.empty() ? "" : m_vNotifications.back().m_aText;
	}
	int LastNotificationRepeatCountForTests() const
	{
		return m_vNotifications.empty() ? 0 : m_vNotifications.back().m_RepeatCount;
	}
	const char *LastNotificationRepeatTextForTests() const
	{
		return m_vNotifications.empty() ? "" : m_vNotifications.back().m_aRepeatText;
	}
	QmHudNotifications::ESoloPrompt PendingCompatPromptForTests() const
	{
		return m_PendingCompatPrompt;
	}
	void SetPendingCompatPromptForTests(QmHudNotifications::ESoloPrompt Prompt, int64_t PendingUntil)
	{
		m_PendingCompatPrompt = Prompt;
		m_PendingCompatUntil = PendingUntil;
	}
	bool ShouldSuppressServerChat(const char *pMessage);

private:
	enum class EKind
	{
		SoloEnter,
		SoloLeave,
		System,
		Echo,
	};

	struct SNotification
	{
		EKind m_Kind = EKind::Echo;
		char m_aText[256] = {};
		char m_aCollapseText[256] = {};
		char m_aRepeatText[32] = {};
		int64_t m_StartTime = 0;
		int64_t m_RepeatAnimTime = 0;
		unsigned m_EchoColor = 0;
		bool m_HasEchoColor = false;
		int m_RepeatCount = 1;
		float m_VisualY = -1.0f;
	};

	struct SEditorPreviewMetrics
	{
		float m_BoxWidth = 0.0f;
		float m_BoxHeight = 0.0f;
		float m_Gap = 0.0f;
		int m_VisibleCount = 0;
		bool m_Valid = false;
	};

	std::deque<SNotification> m_vNotifications;
	bool m_HasLastSolo = false;
	bool m_LastSolo = false;
	QmHudNotifications::ESoloPrompt m_PendingCompatPrompt = QmHudNotifications::ESoloPrompt::None;
	int64_t m_PendingCompatUntil = 0;

	static QmHudNotifications::SServerMessageRouteConfig CurrentRouteConfig()
	{
		QmHudNotifications::SServerMessageRouteConfig Config;
		Config.m_RouteSystemMessages = g_Config.m_QmHudNotificationsSystem != 0;
		Config.m_UseCategoryFilters = g_Config.m_QmHudNotificationsUseCategoryFilters != 0;
		Config.m_ShowBasicInfo = g_Config.m_QmHudNotificationsShowBasicInfo != 0;
		Config.m_ShowHelpInfo = g_Config.m_QmHudNotificationsShowHelpInfo != 0;
		Config.m_ShowPrompts = g_Config.m_QmHudNotificationsShowPrompts != 0;
		Config.m_ShowUnknown = g_Config.m_QmHudNotificationsShowUnknown != 0;
		return Config;
	}

	int BuildVisibleNotificationList(bool EditorPreview, const SNotification *(&apVisible)[8], SNotification &PreviewNotification);
	CUIRect MeasureVisibleRect(const CUIRect &BaseRect, bool EditorPreview, QmHudNotifications::EHorizontalFlow Flow, bool StableEditorGeometry);
	SEditorPreviewMetrics MeasureEditorPreviewMetrics(const CUIRect &BaseRect);
	CUIRect MeasureEditorPreviewRect(const CUIRect &BaseRect, QmHudNotifications::EHorizontalFlow Flow);
	CUIRect MeasureEditorPreviewDragRect(const CUIRect &BaseRect);

	void Queue(EKind Kind, const char *pText, unsigned EchoColor = 0, bool HasEchoColor = false)
	{
		if(pText == nullptr || pText[0] == '\0')
			return;

		if(!m_vNotifications.empty())
		{
			SNotification &Last = m_vNotifications.back();
			if(Last.m_Kind == Kind &&
				Last.m_HasEchoColor == HasEchoColor &&
				Last.m_EchoColor == EchoColor &&
				str_comp(Last.m_aCollapseText, pText) == 0)
			{
				Last.m_RepeatCount += 1;
				Last.m_RepeatAnimTime = time_get();
				str_format(Last.m_aRepeatText, sizeof(Last.m_aRepeatText), "x%d", Last.m_RepeatCount);
				return;
			}
		}

		SNotification Notification;
		Notification.m_Kind = Kind;
		str_copy(Notification.m_aText, pText);
		str_copy(Notification.m_aCollapseText, pText);
		Notification.m_StartTime = time_get();
		Notification.m_RepeatAnimTime = 0;
		Notification.m_EchoColor = EchoColor;
		Notification.m_HasEchoColor = HasEchoColor;
		m_vNotifications.push_back(Notification);
		while((int)m_vNotifications.size() > QmHudNotifications::ClampVisibleCount(g_Config.m_QmHudNotificationsMaxVisible))
			m_vNotifications.pop_front();
	}

	void QueueSolo(QmHudNotifications::ESoloPrompt Prompt)
	{
		if(Prompt == QmHudNotifications::ESoloPrompt::Enter)
			Queue(EKind::SoloEnter, Localize("You are now in a solo part"));
		else if(Prompt == QmHudNotifications::ESoloPrompt::Leave)
			Queue(EKind::SoloLeave, Localize("You are now out of the solo part"));
	}
	bool LocalSoloState(bool &Solo) const;
	void RenderNotifications(const CUIRect &BaseRect, const CUIRect &VisibleRect, bool Preview, bool StableEditorGeometry, QmHudNotifications::EHorizontalFlow Flow);
};

#endif
