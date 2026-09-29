#include "UiDiscreteSlider.h"

#include "UiMotion.h"
#include "UiSurface.h"

#include <engine/shared/config.h>

#include <game/client/ui.h>

#include <algorithm>

namespace ui_widget
{
	namespace
	{
		void DrawSliderFill(const IUiContext &Ctx, const CUIRect &Fill, const SDiscreteSliderStyle &Style, float Alpha)
		{
			const float Radius = Fill.h * 0.5f;
			const ColorRGBA BaseColor = Style.m_Gradient ? Style.m_GradientEnd : Style.m_Color;
			DrawRoundedSurface(Ctx, Fill, BaseColor.WithAlpha(Alpha), ColorRGBA(), Radius);
			if(Style.m_Gradient && Fill.w > Fill.h)
			{
				CUIRect Cap = Fill;
				Cap.w = Fill.h;
				DrawRoundedSurface(Ctx, Cap, Style.m_GradientStart.WithAlpha(Alpha), ColorRGBA(), Radius);
				// 渐变矩形只落在胶囊中段，左右圆帽仍走统一圆角绘制。
				CUIRect Middle = Fill;
				Middle.HMargin(Radius, &Middle);
				CUIRect Left, Right;
				Middle.VSplitMid(&Left, &Right);
				Left.Draw4(Style.m_GradientStart.WithAlpha(Alpha), Style.m_GradientMiddle.WithAlpha(Alpha), Style.m_GradientStart.WithAlpha(Alpha), Style.m_GradientMiddle.WithAlpha(Alpha), IGraphics::CORNER_NONE, 0.0f);
				Right.Draw4(Style.m_GradientMiddle.WithAlpha(Alpha), Style.m_GradientEnd.WithAlpha(Alpha), Style.m_GradientMiddle.WithAlpha(Alpha), Style.m_GradientEnd.WithAlpha(Alpha), IGraphics::CORNER_NONE, 0.0f);
			}

			const float Time = Ctx.m_pAnim != nullptr && g_Config.m_QmUiMotionLevel != 0 ? Ctx.m_pAnim->TimeSec() : 0.0f;
			for(int Index = 0; Index < Style.m_ParticleCount; ++Index)
			{
				const SDiscreteSliderParticle Particle = ResolveDiscreteSliderParticle(Fill, Index, Time);
				if(Particle.m_Alpha <= 0.0f)
					continue;
				DrawRoundedSurface(Ctx, Particle.m_Rect, ui_token::color::TEXT_PRIMARY.WithAlpha(Particle.m_Alpha * Alpha), ColorRGBA(), Particle.m_Rect.w * 0.5f);
			}
		}
	}

	bool DiscreteSlider(const IUiContext &Ctx, const void *pId, SDiscreteSliderState &State, int *pValue, int Min, int Max, const CUIRect &Rect)
	{
		if(Ctx.m_pUi == nullptr || pValue == nullptr || pId == nullptr)
			return false;

		CUi *pUi = Ctx.m_pUi;
		const SDiscreteSliderGeometry Geometry = ResolveDiscreteSliderGeometry(Rect, Ctx.m_UiScale);
		if(!Geometry.IsUsable() || Max <= Min)
		{
			if(!pUi->RenderOnly() && pUi->IsActiveItem(pId))
				pUi->SetActiveItem(nullptr);
			return false;
		}

		bool Changed = false;
		bool Active = false;
		bool Hovered = false;
		if(!pUi->RenderOnly())
		{
			SDiscreteSliderInput Input;
			Input.m_MouseX = pUi->MouseX();
			Input.m_Pressed = pUi->MouseButtonClicked(0);
			Input.m_Down = pUi->MouseButton(0);
			Input.m_Hovered = pUi->MouseHovered(&Rect);
			Input.m_Active = pUi->CheckActiveItem(pId);
			Input.m_CanActivate = pUi->ActiveItem() == nullptr;
			Input.m_Enabled = pUi->Enabled();
			const SDiscreteSliderResult Result = UpdateDiscreteSlider(State, Geometry, Input, *pValue, Min, Max);
			*pValue = Result.m_Value;
			Changed = Result.m_Changed;
			Active = Result.m_Active;
			Hovered = Input.m_Hovered && Input.m_Enabled;
			if(Active)
				pUi->SetActiveItem(pId);
			else if(pUi->IsActiveItem(pId))
				pUi->SetActiveItem(nullptr);
			if(Hovered && (Active || !Input.m_Down))
				pUi->SetHotItem(pId);
		}
		// 本控件未渲染时（页面切走、调用点提前返回）active item 由 CUi::FinishCheck 当帧清理，
		// 因此不需要在这里额外兜底；抓取偏移在任一非拖动帧都会归零。

		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		const float Alpha = pUi->Enabled() ? 1.0f : 0.45f;
		// 槽底固定用中性灰：填充与标签按难度档位取色，槽底不跟随主题强调色，否则同档位颜色会互相干扰。
		const ColorRGBA TrackColor = ui_token::color::SLIDER_TRACK.WithAlpha(Alpha);
		DrawRoundedSurface(Ctx, Geometry.m_Track, TrackColor, ColorRGBA(), Geometry.m_Track.h * 0.5f);

		const bool HasSelection = *pValue >= Min && *pValue <= Max;
		const SDiscreteSliderStyle Style = ResolveDiscreteSliderStyle(*pValue, Min, Max);
		const float CenterX = Geometry.Position(DiscreteSliderNormalizedValue(*pValue, Min, Max));
		if(HasSelection && *pValue > Min)
		{
			CUIRect Fill = Geometry.m_Track;
			Fill.w = CenterX - Fill.x;
			DrawSliderFill(Ctx, Fill, Style, Alpha);
		}

		// 档位圆点数量与量程同阶：正常量程逐档绘制，异常大的量程直接跳过，
		// 避免 O(量程) 次绘制，也避免 int 递增在 Max == INT_MAX 时溢出。
		constexpr int64_t MAX_DRAWN_STOPS = 64;
		const int64_t NumStops = static_cast<int64_t>(Max) - static_cast<int64_t>(Min) + 1;
		if(NumStops <= MAX_DRAWN_STOPS)
		{
			for(int64_t Index = 0; Index < NumStops; ++Index)
			{
				const int Stop = Min + static_cast<int>(Index);
				CUIRect Dot;
				Dot.w = Dot.h = Geometry.m_DotSize;
				Dot.x = Geometry.Position(DiscreteSliderNormalizedValue(Stop, Min, Max)) - Dot.w * 0.5f;
				Dot.y = Rect.y + (Rect.h - Dot.h) * 0.5f;
				const float DotAlpha = HasSelection && Stop <= *pValue ? 0.42f : 0.30f;
				DrawRoundedSurface(Ctx, Dot, ui_token::color::TEXT_PRIMARY.WithAlpha(DotAlpha * Alpha), ColorRGBA(), Dot.w * 0.5f);
			}
		}

		float Emphasis = Active ? 1.0f : (Hovered ? 0.5f : 0.0f);
		if(Ctx.m_pAnim != nullptr && !pUi->RenderOnly())
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
			Emphasis = std::clamp(ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::SCALE, Emphasis, ui_token::motion::SLIDER_KNOB_SPRING, 2), 0.0f, 1.0f);
		}
		if(HasSelection)
		{
			// 位置直接吸附当前档位，只对悬停大小做动画，避免旋钮滞后于已选档位。
			const CUIRect Knob = Geometry.KnobRect(DiscreteSliderNormalizedValue(*pValue, Min, Max), Emphasis);
			DrawRoundedSurface(Ctx, Knob, ui_token::color::TEXT_PRIMARY.WithAlpha(Alpha), ColorRGBA(), Knob.w * 0.5f);
		}

		return Changed;
	}
}
