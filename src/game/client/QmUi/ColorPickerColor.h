#ifndef GAME_CLIENT_QMUI_COLORPICKERCOLOR_H
#define GAME_CLIENT_QMUI_COLORPICKERCOLOR_H

#include <base/color.h>
#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>

// 取色器的颜色空间与通道轨道。
//
// 颜色值仍以 sRGB 的 ColorRGBA 交给渲染后端；OKLCH 只作为取色器的工作空间。
// 这样不会改变配置、皮肤和现有渲染管线对 8 位 HSLA/RGBA 的约定。
namespace qm_picker_color
{
	// OKLCH 的彩度上限取 CSS Color 4 常用的 sRGB 参考范围 0.4。
	// 超出当前 L/H 的 sRGB 色域时，ToRgba 会在保持 L 与 H 的前提下降低 C。
	inline constexpr float OKLCH_MAX_CHROMA = 0.4f;
	// sRGB 无法线性插值 OKLab 的恒定 L/C 色相弧，8 段时最坏约 ΔE(OK) 1.5~5；
	// 32 段已低于可感知阈值，轨道只有几十个四边形，代价可以忽略。
	inline constexpr int TRACK_MAX_STOPS = 33;
	// 色环与 H 轨共用的参考色：色相是“标尺”，不能跟着当前 L/C 一起变黑。
	inline constexpr float OKLCH_HUE_REFERENCE_LIGHTNESS = 0.72f;
	inline constexpr float OKLCH_HUE_REFERENCE_CHROMA = 0.15f;
	// C 轨在 L 过低时按该亮度预览，保证灰度→彩色的轴始终可读；
	// 实际输出仍由当前 L/C/H 决定，不会改变取到的颜色。
	inline constexpr float OKLCH_CHROMA_TRACK_MIN_LIGHTNESS = 0.5f;

	struct SColorOKLCH
	{
		float m_L = 0.0f;
		float m_C = 0.0f;
		float m_H = 0.0f; // 归一化到 [0,1)，与 ColorHSLA/ColorHSVA 的 H 一致
		float m_A = 1.0f;
	};

	struct SChannelTrack
	{
		std::array<ColorRGBA, TRACK_MAX_STOPS> m_aStops{};
		int m_NumStops = 0;
	};

	enum class ETrackSpace
	{
		HSVA,
		RGBA,
		HSLA,
		OKLCH,
	};

	struct STrackOptions
	{
		// 皮肤颜色会把内部亮度再映射到 [Darkest, 1]；取色器弹窗保持 0。
		float m_DarkestLight = 0.0f;
		bool m_ApplyDarkestLight = false;
	};

	inline float SrgbToLinear(float Channel)
	{
		Channel = std::clamp(Channel, 0.0f, 1.0f);
		return Channel <= 0.04045f ? Channel / 12.92f : std::pow((Channel + 0.055f) / 1.055f, 2.4f);
	}

	inline float LinearToSrgb(float Channel)
	{
		Channel = std::clamp(Channel, 0.0f, 1.0f);
		return Channel <= 0.0031308f ? Channel * 12.92f : 1.055f * std::pow(Channel, 1.0f / 2.4f) - 0.055f;
	}

	inline float NormalizeHue(float Hue)
	{
		Hue -= std::floor(Hue);
		return Hue < 0.0f ? Hue + 1.0f : Hue;
	}

	inline std::array<float, 3> OklchToLinearSrgb(const SColorOKLCH &Color)
	{
		const float Angle = NormalizeHue(Color.m_H) * 2.0f * (float)pi;
		const float A = Color.m_C * std::cos(Angle);
		const float B = Color.m_C * std::sin(Angle);

		const float L_ = Color.m_L + 0.3963377774f * A + 0.2158037573f * B;
		const float M_ = Color.m_L - 0.1055613458f * A - 0.0638541728f * B;
		const float S_ = Color.m_L - 0.0894841775f * A - 1.2914855480f * B;

		const float L = L_ * L_ * L_;
		const float M = M_ * M_ * M_;
		const float S = S_ * S_ * S_;

		return {
			4.0767416621f * L - 3.3077115913f * M + 0.2309699292f * S,
			-1.2684380046f * L + 2.6097574011f * M - 0.3413193965f * S,
			-0.0041960863f * L - 0.7034186147f * M + 1.7076147010f * S,
		};
	}

	inline bool LinearSrgbInGamut(const std::array<float, 3> &Rgb)
	{
		constexpr float Epsilon = 1.0e-5f;
		for(const float Channel : Rgb)
		{
			if(Channel < -Epsilon || Channel > 1.0f + Epsilon)
				return false;
		}
		return true;
	}

	inline bool IsInSrgbGamut(const SColorOKLCH &Color)
	{
		return Color.m_L >= 0.0f && Color.m_L <= 1.0f && Color.m_C >= 0.0f && LinearSrgbInGamut(OklchToLinearSrgb(Color));
	}

	inline SColorOKLCH FromRgba(const ColorRGBA &Color)
	{
		const float R = SrgbToLinear(Color.r);
		const float G = SrgbToLinear(Color.g);
		const float B = SrgbToLinear(Color.b);

		const float L = 0.4122214708f * R + 0.5363325363f * G + 0.0514459929f * B;
		const float M = 0.2119034982f * R + 0.6806995451f * G + 0.1073969566f * B;
		const float S = 0.0883024619f * R + 0.2817188376f * G + 0.6299787005f * B;

		const float L_ = std::cbrt(L);
		const float M_ = std::cbrt(M);
		const float S_ = std::cbrt(S);

		const float LabL = 0.2104542553f * L_ + 0.7936177850f * M_ - 0.0040720468f * S_;
		const float LabA = 1.9779984951f * L_ - 2.4285922050f * M_ + 0.4505937099f * S_;
		const float LabB = 0.0259040371f * L_ + 0.7827717662f * M_ - 0.8086757660f * S_;

		const float Chroma = std::sqrt(LabA * LabA + LabB * LabB);
		float Hue = Chroma > 1.0e-7f ? std::atan2(LabB, LabA) / (2.0f * (float)pi) : 0.0f;
		Hue = NormalizeHue(Hue);

		return {LabL, Chroma, Hue, Color.a};
	}

	inline ColorRGBA ToRgba(const SColorOKLCH &Input)
	{
		SColorOKLCH Color = Input;
		Color.m_L = std::clamp(Color.m_L, 0.0f, 1.0f);
		Color.m_C = std::max(Color.m_C, 0.0f);
		Color.m_H = NormalizeHue(Color.m_H);

		if(!IsInSrgbGamut(Color))
		{
			// 保 L/H 降 C：这是 CSS Color 4 的局部色域映射思路，避免直接裁 RGB
			// 导致色相旋转和明度漂移。二分足够稳定，且只在超出 sRGB 时运行。
			float Low = 0.0f;
			float High = Color.m_C;
			for(int Iteration = 0; Iteration < 24; Iteration++)
			{
				const float Mid = (Low + High) * 0.5f;
				SColorOKLCH Candidate = Color;
				Candidate.m_C = Mid;
				if(IsInSrgbGamut(Candidate))
					Low = Mid;
				else
					High = Mid;
			}
			Color.m_C = Low;
		}

		const std::array<float, 3> Linear = OklchToLinearSrgb(Color);
		return ColorRGBA(
			LinearToSrgb(Linear[0]),
			LinearToSrgb(Linear[1]),
			LinearToSrgb(Linear[2]),
			Color.m_A);
	}

	inline void AppendTrackStop(SChannelTrack &Track, const ColorRGBA &Color)
	{
		dbg_assert(Track.m_NumStops < TRACK_MAX_STOPS, "color picker track exceeds stop capacity");
		Track.m_aStops[Track.m_NumStops++] = Color;
	}

	inline void AppendHueTrack(SChannelTrack &Track)
	{
		// sRGB 色相轨用满饱和、满明度的标准色相谱；它只是 H 的可视标尺，
		// 不代表当前 S/V 或 L/C 下的实际颜色。
		constexpr int HueStops = 7;
		for(int Index = 0; Index < HueStops; Index++)
		{
			const float Hue = Index / (float)(HueStops - 1);
			AppendTrackStop(Track, color_cast<ColorRGBA>(ColorHSVA(Hue, 1.0f, 1.0f, 1.0f)));
		}
	}

	inline void AppendAlphaTrack(SChannelTrack &Track, const ColorRGBA &Rgb)
	{
		AppendTrackStop(Track, ColorRGBA(Rgb.r, Rgb.g, Rgb.b, 0.0f));
		AppendTrackStop(Track, ColorRGBA(Rgb.r, Rgb.g, Rgb.b, 1.0f));
	}

	inline ColorRGBA ApplyTrackLighting(const ColorHSLA &Color, const STrackOptions &Options)
	{
		return color_cast<ColorRGBA>(Options.m_ApplyDarkestLight ? Color.UnclampLighting(Options.m_DarkestLight) : Color);
	}

	// 轨道端点必须使用“当前其它通道已生效”的颜色，不能拿黑→纯色或灰→纯色代替。
	// 例如 HSV 的 S 轨在 V=0.5 时右端是 (H,1,0.5)，不是 V=1 的纯色。
	inline SChannelTrack ResolveChannelTrack(
		ETrackSpace Space,
		int Channel,
		const ColorHSVA &Hsv,
		const ColorRGBA &Rgb,
		const ColorHSLA &Hsl,
		const SColorOKLCH &Oklch,
		const STrackOptions &Options = {})
	{
		SChannelTrack Track;
		switch(Space)
		{
		case ETrackSpace::HSVA:
			if(Channel == 0)
			{
				AppendHueTrack(Track);
			}
			else if(Channel == 1)
			{
				AppendTrackStop(Track, color_cast<ColorRGBA>(ColorHSVA(Hsv.h, 0.0f, Hsv.v, 1.0f)));
				AppendTrackStop(Track, color_cast<ColorRGBA>(ColorHSVA(Hsv.h, 1.0f, Hsv.v, 1.0f)));
			}
			else if(Channel == 2)
			{
				AppendTrackStop(Track, color_cast<ColorRGBA>(ColorHSVA(Hsv.h, Hsv.s, 0.0f, 1.0f)));
				AppendTrackStop(Track, color_cast<ColorRGBA>(ColorHSVA(Hsv.h, Hsv.s, 1.0f, 1.0f)));
			}
			else
			{
				AppendAlphaTrack(Track, Rgb);
			}
			break;
		case ETrackSpace::RGBA:
			if(Channel >= 0 && Channel < 3)
			{
				ColorRGBA Left = Rgb;
				ColorRGBA Right = Rgb;
				Left.a = Right.a = 1.0f;
				Left[Channel] = 0.0f;
				Right[Channel] = 1.0f;
				AppendTrackStop(Track, Left);
				AppendTrackStop(Track, Right);
			}
			else
			{
				AppendAlphaTrack(Track, Rgb);
			}
			break;
		case ETrackSpace::HSLA:
			if(Channel == 0)
			{
				AppendHueTrack(Track);
			}
			else if(Channel == 1)
			{
				AppendTrackStop(Track, ApplyTrackLighting(ColorHSLA(Hsl.h, 0.0f, Hsl.l, 1.0f), Options));
				AppendTrackStop(Track, ApplyTrackLighting(ColorHSLA(Hsl.h, 1.0f, Hsl.l, 1.0f), Options));
			}
			else if(Channel == 2)
			{
				// HSL 的 L 轴在黑→纯色→白之间折返，两个端点直接插值会漏掉中间色相。
				AppendTrackStop(Track, ApplyTrackLighting(ColorHSLA(Hsl.h, Hsl.s, 0.0f, 1.0f), Options));
				AppendTrackStop(Track, ApplyTrackLighting(ColorHSLA(Hsl.h, Hsl.s, 0.5f, 1.0f), Options));
				AppendTrackStop(Track, ApplyTrackLighting(ColorHSLA(Hsl.h, Hsl.s, 1.0f, 1.0f), Options));
			}
			else
			{
				AppendAlphaTrack(Track, Rgb);
			}
			break;
		case ETrackSpace::OKLCH:
		{
			// L/C/H 轨只表达颜色轴，透明度由 A 轨单独表达；否则当前 alpha
			// 会让轨道透出弹窗底色，看起来像脏灰条。
			SColorOKLCH TrackColor = Oklch;
			TrackColor.m_A = 1.0f;
			if(Channel == 0)
			{
				for(int Index = 0; Index < TRACK_MAX_STOPS; Index++)
				{
					SColorOKLCH Stop = TrackColor;
					Stop.m_L = Index / (float)(TRACK_MAX_STOPS - 1);
					AppendTrackStop(Track, ToRgba(Stop));
				}
			}
			else if(Channel == 1)
			{
				for(int Index = 0; Index < TRACK_MAX_STOPS; Index++)
				{
					SColorOKLCH Stop = TrackColor;
					Stop.m_L = std::max(Stop.m_L, OKLCH_CHROMA_TRACK_MIN_LIGHTNESS);
					Stop.m_C = OKLCH_MAX_CHROMA * Index / (float)(TRACK_MAX_STOPS - 1);
					AppendTrackStop(Track, ToRgba(Stop));
				}
			}
			else if(Channel == 2)
			{
				for(int Index = 0; Index < TRACK_MAX_STOPS; Index++)
				{
					SColorOKLCH Stop = TrackColor;
					Stop.m_L = OKLCH_HUE_REFERENCE_LIGHTNESS;
					Stop.m_C = OKLCH_HUE_REFERENCE_CHROMA;
					Stop.m_H = Index / (float)(TRACK_MAX_STOPS - 1);
					AppendTrackStop(Track, ToRgba(Stop));
				}
			}
			else
			{
				AppendAlphaTrack(Track, Rgb);
			}
			break;
		}
		}
		return Track;
	}
} // namespace qm_picker_color

#endif
