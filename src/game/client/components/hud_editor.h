/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_HUD_EDITOR_H

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace QmHudEditor
{
	// 对齐参考线（屏幕中线、其它 HUD 模块）的邻近吸附半径。
	inline constexpr float SNAP_DISTANCE = 6.0f;
	inline constexpr float EPSILON = 0.001f;
	// 屏幕边吸附没有邻近半径：只有 HUD 边与窗口边真正重合时才吸附，
	// 未重合时保留拖拽后的原始位置与样式。容差只用于吸收浮点误差。
	inline constexpr float EDGE_COINCIDENCE_DISTANCE = EPSILON;

	// 绘制后报告的可见范围可能小于预估范围（例如聊天只显示底部几行）。
	// 保存源坐标中的实际范围，让下一帧恢复、缩放和拖动使用同一个边界。
	class CVisibleBounds
	{
		bool m_Valid = false;
		bool m_ReportedThisFrame = false;
		CUIRect m_TransformRect{};
		CUIRect m_DeclaredRect{};
		CUIRect m_ObservedRect{};

		static bool SameRect(const CUIRect &Left, const CUIRect &Right)
		{
			return Left.x == Right.x && Left.y == Right.y && Left.w == Right.w && Left.h == Right.h;
		}

	public:
		// 仅在绘制帧开始轮换，避免逻辑更新使聊天边界来回失效。
		void BeginRenderFrame()
		{
			if(!m_ReportedThisFrame)
				m_Valid = false;
			m_ReportedThisFrame = false;
		}

		CUIRect Resolve(const CUIRect &TransformRect, const CUIRect &DeclaredRect) const
		{
			const CUIRect RelativeDeclared{DeclaredRect.x - TransformRect.x, DeclaredRect.y - TransformRect.y, DeclaredRect.w, DeclaredRect.h};
			const CUIRect PreviousDeclared{m_DeclaredRect.x - m_TransformRect.x, m_DeclaredRect.y - m_TransformRect.y, m_DeclaredRect.w, m_DeclaredRect.h};
			if(!m_Valid || TransformRect.w != m_TransformRect.w || TransformRect.h != m_TransformRect.h || !SameRect(RelativeDeclared, PreviousDeclared))
				return DeclaredRect;
			return {m_ObservedRect.x + TransformRect.x - m_TransformRect.x, m_ObservedRect.y + TransformRect.y - m_TransformRect.y, m_ObservedRect.w, m_ObservedRect.h};
		}

		void Observe(const CUIRect &TransformRect, const CUIRect &DeclaredRect, const CUIRect &TargetUiRect, const CUIRect &RenderedUiRect)
		{
			if(TransformRect.w <= EPSILON || TransformRect.h <= EPSILON || TargetUiRect.w <= EPSILON || TargetUiRect.h <= EPSILON || RenderedUiRect.w <= 0.0f || RenderedUiRect.h <= 0.0f)
				return;
			const float ToSourceX = TransformRect.w / TargetUiRect.w;
			const float ToSourceY = TransformRect.h / TargetUiRect.h;
			m_TransformRect = TransformRect;
			m_DeclaredRect = DeclaredRect;
			m_ObservedRect = {
				TransformRect.x + (RenderedUiRect.x - TargetUiRect.x) * ToSourceX,
				TransformRect.y + (RenderedUiRect.y - TargetUiRect.y) * ToSourceY,
				RenderedUiRect.w * ToSourceX,
				RenderedUiRect.h * ToSourceY};
			m_Valid = true;
			m_ReportedThisFrame = true;
		}
	};

	struct SAxisReference
	{
		float m_Position = 0.0f;
		float m_Size = 0.0f;
	};

	enum class ESnapGuideKind
	{
		None,
		ScreenStart,
		ScreenCenter,
		ScreenEnd,
		ReferenceStart,
		ReferenceCenter,
		ReferenceEnd,
	};

	struct SSnapAxisResult
	{
		float m_Position = 0.0f;
		bool m_HasGuide = false;
		float m_GuidePosition = 0.0f;
		ESnapGuideKind m_GuideKind = ESnapGuideKind::None;
	};

	// 与屏幕边吸附：Position 是锚点（变换空间的最小坐标），VisibleEdgeOffset 是可见矩形相对锚点的位移。
	// 约束按可见矩形取，保证可见边不会被推出屏幕；可见边没有贴到窗口边时不改动传入位置。
	inline float SnapAxisToScreenEdgesEx(float Position, float Size, float ScreenStart, float ScreenSize, float VisibleEdgeOffset = 0.0f)
	{
		const float ScreenEnd = ScreenStart + ScreenSize;
		const float MinPosition = ScreenStart - VisibleEdgeOffset;
		const float MaxPosition = Size >= ScreenSize ? MinPosition : ScreenEnd - Size - VisibleEdgeOffset;
		float SnappedPosition = std::clamp(Position, MinPosition, MaxPosition);

		if(std::fabs(SnappedPosition + VisibleEdgeOffset - ScreenStart) <= EDGE_COINCIDENCE_DISTANCE)
			return ScreenStart - VisibleEdgeOffset;
		if(std::fabs(SnappedPosition + VisibleEdgeOffset + Size - ScreenEnd) <= EDGE_COINCIDENCE_DISTANCE)
			return ScreenEnd - Size - VisibleEdgeOffset;
		return SnappedPosition;
	}

	inline float SnapAxisToScreenEdges(float Position, float Size, float ScreenStart, float ScreenSize)
	{
		return SnapAxisToScreenEdgesEx(Position, Size, ScreenStart, ScreenSize);
	}

	// 沿用布局字段：0/1 表示可见边贴边，旧的正值仍保存变换锚点比例。
	// 锚点越出屏幕时，以 [-2,-1] 保存可见边比例，避免把合法内容位置误当成贴边。
	inline float ClampStoredAxisPosition(float Position)
	{
		return std::clamp(Position, -2.0f, 1.0f);
	}

	inline float RestoreAxisAnchor(float NormalizedPosition, float Size, float ScreenStart, float ScreenSize, float VisibleEdgeOffset)
	{
		float Position = ScreenStart + NormalizedPosition * ScreenSize;
		if(NormalizedPosition <= -1.0f)
			Position = ScreenStart + (-NormalizedPosition - 1.0f) * ScreenSize - VisibleEdgeOffset;
		else if(NormalizedPosition <= 0.0f)
			Position = ScreenStart - VisibleEdgeOffset;
		else if(NormalizedPosition >= 1.0f)
			Position = ScreenStart + std::max(0.0f, ScreenSize - Size) - VisibleEdgeOffset;
		return SnapAxisToScreenEdgesEx(Position, Size, ScreenStart, ScreenSize, VisibleEdgeOffset);
	}

	inline float StoreAxisAnchor(float Position, float Size, float ScreenStart, float ScreenSize, float VisibleEdgeOffset)
	{
		if(ScreenSize <= EPSILON || std::fabs(Position + VisibleEdgeOffset - ScreenStart) <= EDGE_COINCIDENCE_DISTANCE)
			return 0.0f;
		if(std::fabs(Position + VisibleEdgeOffset + Size - ScreenStart - ScreenSize) <= EDGE_COINCIDENCE_DISTANCE)
			return 1.0f;
		const float AnchorFraction = (Position - ScreenStart) / ScreenSize;
		// 也避开整数布局精度会舍入成 0/1 的保留值。
		if(AnchorFraction <= 0.0001f || AnchorFraction >= 0.9999f)
			return -1.0f - std::clamp((Position + VisibleEdgeOffset - ScreenStart) / ScreenSize, 0.0f, 1.0f);
		return AnchorFraction;
	}

	// 一个轴上的最终落点：先试屏幕边重合吸附，再试屏幕中线与其它 HUD 模块的对齐参考线。
	// 屏幕边只认重合，对齐参考线仍保留 SNAP_DISTANCE 邻近吸附。
	inline SSnapAxisResult ResolveAxisSnapEx(float Position, float Size, float ScreenStart, float ScreenSize, const SAxisReference *pReferences, int ReferenceCount, float VisibleEdgeOffset = 0.0f)
	{
		const float ScreenEnd = ScreenStart + ScreenSize;
		const float ScreenCenter = ScreenStart + ScreenSize * 0.5f;
		const float MinPosition = ScreenStart - VisibleEdgeOffset;
		const float MaxPosition = Size >= ScreenSize ? MinPosition : ScreenEnd - Size - VisibleEdgeOffset;
		SSnapAxisResult Result;
		Result.m_Position = SnapAxisToScreenEdgesEx(Position, Size, ScreenStart, ScreenSize, VisibleEdgeOffset);
		float BestDistance = SNAP_DISTANCE + EPSILON;

		const auto TrySnap = [&](float Candidate, float Distance, float GuidePosition, ESnapGuideKind GuideKind) {
			if(Distance <= SNAP_DISTANCE && Distance < BestDistance && Candidate >= MinPosition && Candidate <= MaxPosition)
			{
				Result.m_Position = std::clamp(Candidate, MinPosition, MaxPosition);
				Result.m_HasGuide = true;
				Result.m_GuidePosition = GuidePosition;
				Result.m_GuideKind = GuideKind;
				BestDistance = Distance;
			}
		};

		if(std::fabs(Result.m_Position + VisibleEdgeOffset - ScreenStart) <= EDGE_COINCIDENCE_DISTANCE)
		{
			Result.m_HasGuide = true;
			Result.m_GuidePosition = ScreenStart;
			Result.m_GuideKind = ESnapGuideKind::ScreenStart;
			return Result;
		}
		else if(std::fabs(Result.m_Position + VisibleEdgeOffset + Size - ScreenEnd) <= EDGE_COINCIDENCE_DISTANCE)
		{
			Result.m_HasGuide = true;
			Result.m_GuidePosition = ScreenEnd;
			Result.m_GuideKind = ESnapGuideKind::ScreenEnd;
			return Result;
		}

		const float VisiblePosition = Result.m_Position + VisibleEdgeOffset;
		TrySnap(ScreenCenter - Size * 0.5f - VisibleEdgeOffset, std::fabs(VisiblePosition + Size * 0.5f - ScreenCenter), ScreenCenter, ESnapGuideKind::ScreenCenter);

		if(pReferences != nullptr)
		{
			for(int i = 0; i < ReferenceCount; ++i)
			{
				const float ReferenceStart = pReferences[i].m_Position;
				const float ReferenceEnd = pReferences[i].m_Position + pReferences[i].m_Size;
				const float ReferenceCenter = pReferences[i].m_Position + pReferences[i].m_Size * 0.5f;
				TrySnap(ReferenceStart - VisibleEdgeOffset, std::fabs(VisiblePosition - ReferenceStart), ReferenceStart, ESnapGuideKind::ReferenceStart);
				TrySnap(ReferenceCenter - Size * 0.5f - VisibleEdgeOffset, std::fabs(VisiblePosition + Size * 0.5f - ReferenceCenter), ReferenceCenter, ESnapGuideKind::ReferenceCenter);
				TrySnap(ReferenceEnd - Size - VisibleEdgeOffset, std::fabs(VisiblePosition + Size - ReferenceEnd), ReferenceEnd, ESnapGuideKind::ReferenceEnd);
			}
		}
		return Result;
	}

	inline float SnapAxisToGuides(float Position, float Size, float ScreenStart, float ScreenSize, const SAxisReference *pReferences, int ReferenceCount)
	{
		return ResolveAxisSnapEx(Position, Size, ScreenStart, ScreenSize, pReferences, ReferenceCount).m_Position;
	}

	inline float SnapAxisToScreenGuides(float Position, float Size, float ScreenStart, float ScreenSize)
	{
		return SnapAxisToGuides(Position, Size, ScreenStart, ScreenSize, nullptr, 0);
	}

	struct SEdgeMargin
	{
		float m_Left = 0.0f;
		float m_Right = 0.0f;
		float m_Top = 0.0f;
		float m_Bottom = 0.0f;
		bool IsZero() const { return m_Left == 0.0f && m_Right == 0.0f && m_Top == 0.0f && m_Bottom == 0.0f; }
		static SEdgeMargin Uniform(float Value) { return {Value, Value, Value, Value}; }
	};

	enum class EHorizontalFlow
	{
		LeftToRight,
		RightToLeft,
	};

	// 在变换前空间施加边距，向屏幕内侧推：贴左 +Left，贴右 -Right；非贴边方向不动。
	// 宽高不变，与通知栏旧 InsetAnchoredRect 行为一致。
	inline CUIRect ApplyEdgeMargin(const CUIRect &Rect, const SEdgeMargin &Margin,
		bool AnchoredLeft, bool AnchoredRight, bool AnchoredTop, bool AnchoredBottom)
	{
		const float SafeLeft = maximum(0.0f, Margin.m_Left);
		const float SafeRight = maximum(0.0f, Margin.m_Right);
		const float SafeTop = maximum(0.0f, Margin.m_Top);
		const float SafeBottom = maximum(0.0f, Margin.m_Bottom);
		return {
			Rect.x + (AnchoredLeft ? SafeLeft : (AnchoredRight ? -SafeRight : 0.0f)),
			Rect.y + (AnchoredTop ? SafeTop : (AnchoredBottom ? -SafeBottom : 0.0f)),
			Rect.w,
			Rect.h};
	}

	inline CUIRect ChatEdgeBaseRect(float ScreenWidth, float ChatWidth, float EdgeMargin, bool AnchoredRight)
	{
		const float Width = std::min(ScreenWidth, std::max(190.0f, ChatWidth + 32.0f));
		const float SafeMargin = std::max(0.0f, EdgeMargin);
		const float X = AnchoredRight ? std::max(0.0f, ScreenWidth - Width - SafeMargin) : SafeMargin;
		return {X, 50.0f, Width, 250.0f};
	}

	// 通用版：从可见矩形位置推导贴左/贴右。
	// 与 anchor 距离 < EPSILON 视为贴边；非贴边按中心和屏幕中心比较。
	inline EHorizontalFlow ResolveHorizontalFlow(const CUIRect &VisibleRect, float ScreenStartX, float ScreenWidth)
	{
		const float ScreenEndX = ScreenStartX + ScreenWidth;
		if(std::fabs(VisibleRect.x - ScreenStartX) <= EPSILON)
			return EHorizontalFlow::LeftToRight;
		if(std::fabs((VisibleRect.x + VisibleRect.w) - ScreenEndX) <= EPSILON)
			return EHorizontalFlow::RightToLeft;
		const float VisibleCenterX = VisibleRect.x + VisibleRect.w * 0.5f;
		const float ScreenCenterX = ScreenStartX + ScreenWidth * 0.5f;
		return VisibleCenterX <= ScreenCenterX ? EHorizontalFlow::LeftToRight : EHorizontalFlow::RightToLeft;
	}
} // namespace QmHudEditor

enum class EHudEditorElement
{
	HudMain,
	HudPlayerState,
	GameTimer,
	PauseNotification,
	SuddenDeath,
	ScoreHud,
	WarmupTimer,
	DummyActions,
	DummyMiniMap,
	TextInfo,
	SpectatorCount,
	MovementInfo,
	JumpHint,
	MapProgressBar,
	SpectatorHud,
	LocalTime,
	LegacyMediaInfo,
	MediaIsland,
	Lyrics,
	Voting,
	Chat,
	VoiceOverlay,
	InputOverlay,
	HudNotifications,
	GoresDrownBoard,

	Count,
};

namespace QmHudEditor
{
	inline const char *ElementToken(EHudEditorElement Element)
	{
		switch(Element)
		{
		case EHudEditorElement::HudMain: return "hud_main";
		case EHudEditorElement::HudPlayerState: return "hud_player_state";
		case EHudEditorElement::GameTimer: return "game_timer";
		case EHudEditorElement::PauseNotification: return "pause_notification";
		case EHudEditorElement::SuddenDeath: return "sudden_death";
		case EHudEditorElement::ScoreHud: return "score_hud";
		case EHudEditorElement::WarmupTimer: return "warmup_timer";
		case EHudEditorElement::DummyActions: return "dummy_actions";
		case EHudEditorElement::DummyMiniMap: return "dummy_minimap";
		case EHudEditorElement::TextInfo: return "text_info";
		case EHudEditorElement::SpectatorCount: return "spectator_count";
		case EHudEditorElement::MovementInfo: return "movement_info";
		case EHudEditorElement::JumpHint: return "jump_hint";
		case EHudEditorElement::MapProgressBar: return "map_progress_bar";
		case EHudEditorElement::SpectatorHud: return "spectator_hud";
		case EHudEditorElement::LocalTime: return "local_time";
		case EHudEditorElement::LegacyMediaInfo: return "legacy_media_info";
		case EHudEditorElement::MediaIsland: return "media_island";
		case EHudEditorElement::Lyrics: return "lyrics";
		case EHudEditorElement::Voting: return "voting";
		case EHudEditorElement::Chat: return "chat";
		case EHudEditorElement::VoiceOverlay: return "voice_overlay";
		case EHudEditorElement::InputOverlay: return "input_overlay";
		case EHudEditorElement::HudNotifications: return "hud_notifications";
		case EHudEditorElement::GoresDrownBoard: return "gores_drown_board";
		case EHudEditorElement::Count: break;
		}
		return "";
	}

	inline int ElementFromToken(const char *pToken)
	{
		for(int i = 0; i < static_cast<int>(EHudEditorElement::Count); ++i)
		{
			const auto Element = static_cast<EHudEditorElement>(i);
			if(std::strcmp(ElementToken(Element), pToken) == 0)
				return i;
		}
		return -1;
	}
} // namespace QmHudEditor

class CHudEditor : public CComponent
{
public:
	struct STransformScope
	{
		bool m_Applied = false;
		float m_ScreenX0 = 0.0f;
		float m_ScreenY0 = 0.0f;
		float m_ScreenX1 = 0.0f;
		float m_ScreenY1 = 0.0f;
		CUIRect m_TargetRect{};
		CUIRect m_VisibleRect{};
		int m_Corners = IGraphics::CORNER_ALL;
		bool m_AnchoredLeft = false;
		bool m_AnchoredRight = false;
		bool m_AnchoredTop = false;
		bool m_AnchoredBottom = false;
		QmHudEditor::SEdgeMargin m_EdgeMargin{};
	};

	CHudEditor();
	int Sizeof() const override { return sizeof(*this); }

	void OnRender() override;
	void OnReset() override;
	void OnRelease() override;
	void OnStateChange(int NewState, int OldState) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void SetActive(bool Active);
	bool IsActive() const { return m_Active; }
	void BeginRenderFrame();
	void UpdateVisibleRect(EHudEditorElement Element, const CUIRect &RenderedRect);

	STransformScope PreviewTransform(EHudEditorElement Element, const CUIRect &DefaultRect, bool Scalable = true);
	STransformScope PreviewTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable = true);
	STransformScope PreviewTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, const QmHudEditor::SEdgeMargin &EdgeMargin, bool Scalable = true);
	STransformScope BeginTransform(EHudEditorElement Element, const CUIRect &DefaultRect, bool Scalable = true, bool ApplyMapScreen = true);
	STransformScope BeginTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable = true, bool ApplyMapScreen = true);
	STransformScope BeginTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, const QmHudEditor::SEdgeMargin &EdgeMargin, bool Scalable = true, bool ApplyMapScreen = true);
	void EndTransform(const STransformScope &Scope);

private:
	struct SElementState
	{
		bool m_HasCustom = false;
		int m_PosXPermille = 0;
		int m_PosYPermille = 0;
		int m_ScalePercent = 100;
	};

	struct SVisibleElement
	{
		EHudEditorElement m_Element = EHudEditorElement::HudMain;
		CUIRect m_Rect{};
		CUIRect m_TransformRect{};
		CUIRect m_DeclaredVisibleRect{};
		CUIRect m_TargetUiRect{};
		float m_BaseWidth = 0.0f;
		float m_BaseHeight = 0.0f;
		float m_StateOffsetX = 0.0f;
		float m_StateOffsetY = 0.0f;
		bool m_Scalable = true;
	};

	struct SAlignmentReferences
	{
		std::array<QmHudEditor::SAxisReference, static_cast<int>(EHudEditorElement::Count)> m_aXReferences{};
		std::array<QmHudEditor::SAxisReference, static_cast<int>(EHudEditorElement::Count)> m_aYReferences{};
		int m_XCount = 0;
		int m_YCount = 0;
	};

	static constexpr int ELEMENT_COUNT = static_cast<int>(EHudEditorElement::Count);
	static constexpr int POSITION_SCALE = 10000;
	static constexpr int MIN_SCALE_PERCENT = 25;
	static constexpr int MAX_SCALE_PERCENT = 400;

	bool m_Active = false;
	bool m_DirtyLayout = false;
	bool m_LayoutLoaded = false;
	int m_DraggingElement = -1;
	vec2 m_DragGrabOffset = vec2(0.0f, 0.0f);
	char m_aLayoutCache[2048] = {};
	std::array<SElementState, ELEMENT_COUNT> m_aElementStates{};
	std::array<QmHudEditor::CVisibleBounds, ELEMENT_COUNT> m_aVisibleBounds{};
	std::vector<SVisibleElement> m_vVisibleElements;
	bool m_InteractionUiActive = false;
	bool m_JumpHintTextEditorActive = false;
	bool m_JumpHintTextEditorNeedsFocus = false;
	CLineInputBuffered<512> m_JumpHintTextInput;

	void ResetRuntimeState();
	void SyncLayoutConfig();
	void ParseLayoutConfig(const char *pConfig);
	void SaveLayoutConfig();
	void ResetLayoutConfig();
	void ClampStateToScreen(SElementState &State, float BaseWidth, float BaseHeight, float StateOffsetX, float StateOffsetY) const;
	SElementState &EnsureState(EHudEditorElement Element);
	const SElementState &State(EHudEditorElement Element) const;
	int FindHoveredVisibleElement() const;
	int FindVisibleElementIndex(EHudEditorElement Element) const;
	void UpdateInteractionUi();
	void OpenJumpHintTextEditor();
	void SaveJumpHintTextEditor();
	void CloseJumpHintTextEditor();
	bool HandleElementDoubleClick(EHudEditorElement Element);
	bool DoJumpHintTextArea(CLineInput *pLineInput, const CUIRect *pRect, float FontSize);
	void RenderJumpHintTextEditor(const CUIRect &Screen);
	SAlignmentReferences BuildAlignmentReferences(EHudEditorElement DraggingElement) const;
	bool ComputeTransformPlacement(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable, STransformScope &Scope, SVisibleElement *pVisible, const QmHudEditor::SEdgeMargin &EdgeMargin);
	static const char *ElementToken(EHudEditorElement Element);
	static int ElementFromToken(const char *pToken);
};

#endif
