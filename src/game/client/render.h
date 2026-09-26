/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_RENDER_H
#define GAME_CLIENT_RENDER_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/qm_title_effect.h>
#include <game/client/skin.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

class CAnimState;
class CSpeedupTile;
class CSwitchTile;
class CTeleTile;
class CTile;
class CTuneTile;
class CEnvPoint;
class CEnvPointBezier;
class CEnvPointBezier_upstream;
class CMapItemGroup;
class CQuad;

class CSkinDescriptor
{
public:
	enum
	{
		FLAG_SIX = 1,
		FLAG_SEVEN = 2,
	};
	unsigned m_Flags;

	char m_aSkinName[MAX_SKIN_LENGTH];

	class CSixup
	{
	public:
		char m_aaSkinPartNames[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH];
		bool m_BotDecoration;
		bool m_XmasHat;

		void Reset();
		bool operator==(const CSixup &Other) const;
		bool operator!=(const CSixup &Other) const { return !(*this == Other); }
	};
	CSixup m_aSixup[NUM_DUMMIES];

	CSkinDescriptor();
	void Reset();
	bool IsValid() const;
	bool operator==(const CSkinDescriptor &Other) const;
	bool operator!=(const CSkinDescriptor &Other) const { return !(*this == Other); }
};

class CTeeRenderInfo
{
public:
	static bool IsDrawableTextureState(const bool IsValid, const bool IsNullTexture)
	{
		return IsValid && !IsNullTexture;
	}

	static bool IsDrawableTexture(const IGraphics::CTextureHandle &Texture)
	{
		return IsDrawableTextureState(Texture.IsValid(), Texture.IsNullTexture());
	}

	/**
	 * 句柄不仅「看起来有效」，而且在当前图形纪元里还真的分配着纹理。
	 * 设备重建、槽位释放、皮肤贴图被卸载之后，旧句柄依旧 IsValid()，
	 * 交给绘制就会变成没有贴图的实心块（纯白 Tee 就是这么来的）。
	 */
	static bool IsLiveDrawableTextureState(const bool IsValid, const bool IsNullTexture, const bool IsAllocated)
	{
		return IsDrawableTextureState(IsValid, IsNullTexture) && IsAllocated;
	}

	static bool IsLiveDrawableTexture(const IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture)
	{
		return IsLiveDrawableTextureState(Texture.IsValid(), Texture.IsNullTexture(), pGraphics == nullptr || pGraphics->IsTextureHandleAllocated(Texture));
	}

	static bool AreTextureVariantsDrawableState(const bool OriginalValid, const bool OriginalNullTexture, const bool ColorableValid, const bool ColorableNullTexture)
	{
		return IsDrawableTextureState(OriginalValid, OriginalNullTexture) && IsDrawableTextureState(ColorableValid, ColorableNullTexture);
	}

	static bool AreTextureVariantsDrawable(const IGraphics::CTextureHandle &Original, const IGraphics::CTextureHandle &Colorable)
	{
		return AreTextureVariantsDrawableState(Original.IsValid(), Original.IsNullTexture(), Colorable.IsValid(), Colorable.IsNullTexture());
	}

	CTeeRenderInfo()
	{
		Reset();
	}

	void Reset()
	{
		m_OriginalRenderSkin.Reset();
		m_ColorableRenderSkin.Reset();
		m_SkinMetrics.Reset();
		m_CustomColoredSkin = false;
		m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		m_OutlineColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		m_QmSkinOutlineColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		m_QmSkinOutlineWidth = 0;
		m_Size = 1.0f;
		m_GotAirJump = true;
		m_TeeRenderFlags = 0;
		m_FeetFlipped = false;

		for(auto &Sixup : m_aSixup)
			Sixup.Reset();
	}

	void Apply(const CSkin *pSkin)
	{
		m_OriginalRenderSkin = pSkin->m_OriginalSkin;
		m_ColorableRenderSkin = pSkin->m_ColorableSkin;
		m_BloodColor = pSkin->m_BloodColor;
		m_SkinMetrics = pSkin->m_Metrics;
	}

	void ApplySkin(const CTeeRenderInfo &TeeRenderInfo)
	{
		m_OriginalRenderSkin = TeeRenderInfo.m_OriginalRenderSkin;
		m_ColorableRenderSkin = TeeRenderInfo.m_ColorableRenderSkin;
		m_BloodColor = TeeRenderInfo.m_BloodColor;
		m_SkinMetrics = TeeRenderInfo.m_SkinMetrics;
	}

	void ResetMissingDescriptorBranches(const unsigned Flags)
	{
		if(!(Flags & CSkinDescriptor::FLAG_SIX))
		{
			m_OriginalRenderSkin.Reset();
			m_ColorableRenderSkin.Reset();
			m_SkinMetrics.Reset();
		}
		if(!(Flags & CSkinDescriptor::FLAG_SEVEN))
		{
			for(auto &Sixup : m_aSixup)
				Sixup.Reset();
		}
	}

	void ApplyColors(bool CustomColoredSkin, int ColorBody, int ColorFeet)
	{
		m_CustomColoredSkin = CustomColoredSkin;
		if(CustomColoredSkin)
		{
			m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(ColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT));
			m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(ColorFeet).UnclampLighting(ColorHSLA::DARKEST_LGT));
		}
		else
		{
			m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	bool Valid() const
	{
		if(IsDrawableTexture(m_CustomColoredSkin ? m_ColorableRenderSkin.m_Body : m_OriginalRenderSkin.m_Body))
		{
			return true;
		}
		for(const auto &Sixup : m_aSixup)
		{
			if(IsDrawableTexture(Sixup.PartTexture(protocol7::SKINPART_BODY)))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 这份渲染信息里是否有已经失效的纹理（设备重建、槽位释放、皮肤贴图被卸载）。
	 * 失效句柄在 IsValid() 上依旧为真，继续复用就会把 Tee 画成没有贴图的实心白块。
	 */
	bool HasStaleTexture(const IGraphics *pGraphics) const;

	CSkin::CSkinTextures m_OriginalRenderSkin;
	CSkin::CSkinTextures m_ColorableRenderSkin;

	CSkin::CSkinMetrics m_SkinMetrics;

	bool m_CustomColoredSkin;
	ColorRGBA m_BloodColor;

	ColorRGBA m_ColorBody;
	ColorRGBA m_ColorFeet;
	ColorRGBA m_OutlineColor;
	ColorRGBA m_QmSkinOutlineColor;
	int m_QmSkinOutlineWidth;
	float m_Size;
	bool m_GotAirJump;
	int m_TeeRenderFlags;
	bool m_FeetFlipped;

	class CSixup
	{
	public:
		void Reset()
		{
			for(auto &Texture : m_aOriginalTextures)
			{
				Texture.Invalidate();
			}
			for(auto &Texture : m_aColorableTextures)
			{
				Texture.Invalidate();
			}
			std::fill(std::begin(m_aUseCustomColors), std::end(m_aUseCustomColors), false);
			std::fill(std::begin(m_aColors), std::end(m_aColors), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			m_HatTexture.Invalidate();
			m_BotTexture.Invalidate();
			m_HatSpriteIndex = 0;
			m_BotColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
			for(auto &pSource : m_apChatAvatarOriginal)
				pSource.reset();
			for(auto &pSource : m_apChatAvatarColorable)
				pSource.reset();
			for(auto &pOutline : m_apQmSkinOutlines)
				pOutline.reset();
		}

		IGraphics::CTextureHandle m_aOriginalTextures[protocol7::NUM_SKINPARTS];
		IGraphics::CTextureHandle m_aColorableTextures[protocol7::NUM_SKINPARTS];
		bool m_aUseCustomColors[protocol7::NUM_SKINPARTS];
		ColorRGBA m_aColors[protocol7::NUM_SKINPARTS];
		ColorRGBA m_BloodColor;
		IGraphics::CTextureHandle m_HatTexture;
		IGraphics::CTextureHandle m_BotTexture;
		int m_HatSpriteIndex;
		ColorRGBA m_BotColor;
		// 聊天导出的六人皮肤头像素材，按部件与原始/可着色两套分别持有；皮肤描边同样按部件持有。
		std::shared_ptr<CQmSkinOutline> m_apQmSkinOutlines[protocol7::NUM_SKINPARTS];
		std::shared_ptr<const QmChatAvatar::SSource> m_apChatAvatarOriginal[protocol7::NUM_SKINPARTS];
		std::shared_ptr<const QmChatAvatar::SSource> m_apChatAvatarColorable[protocol7::NUM_SKINPARTS];

		const IGraphics::CTextureHandle &PartTexture(int Part) const
		{
			return (m_aUseCustomColors[Part] ? m_aColorableTextures : m_aOriginalTextures)[Part];
		}

		bool RequiredPartTextureVariantsDrawable() const
		{
			constexpr int s_aRequiredParts[] = {
				protocol7::SKINPART_BODY,
				protocol7::SKINPART_HANDS,
				protocol7::SKINPART_FEET,
				protocol7::SKINPART_EYES,
			};
			return std::all_of(std::begin(s_aRequiredParts), std::end(s_aRequiredParts), [&](int Part) {
				return CTeeRenderInfo::AreTextureVariantsDrawable(m_aOriginalTextures[Part], m_aColorableTextures[Part]);
			});
		}
	};

	CSixup m_aSixup[NUM_DUMMIES];

	bool SixDescriptorReady() const
	{
		return AreTextureVariantsDrawable(m_OriginalRenderSkin.m_Body, m_ColorableRenderSkin.m_Body);
	}

	bool SevenDescriptorReady() const
	{
		return std::all_of(std::begin(m_aSixup), std::end(m_aSixup), [](const CSixup &Sixup) { return Sixup.RequiredPartTextureVariantsDrawable(); });
	}
};

constexpr int DEFAULT_SKIN_CHANGE_TRANSITION_DURATION_MS = 500;

enum
{
	SKIN_CHANGE_TRANSITION_GHOST_POP = 0,
	SKIN_CHANGE_TRANSITION_FADE_SCALE,
	SKIN_CHANGE_TRANSITION_SLIDE_LEFT,
	SKIN_CHANGE_TRANSITION_SPIN_POP,
	SKIN_CHANGE_TRANSITION_THEME_SWITCH,
	SKIN_CHANGE_TRANSITION_GLITCH,
	SKIN_CHANGE_TRANSITION_ELASTIC,
	SKIN_CHANGE_TRANSITION_TYPE_COUNT,
};

enum
{
	SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC = 0,
	SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK,
	SKIN_CHANGE_TRANSITION_EASING_LINEAR,
	SKIN_CHANGE_TRANSITION_EASING_EASE_IN_OUT_QUAD,
	SKIN_CHANGE_TRANSITION_EASING_COUNT,
};

struct SSkinChangeTransitionBlend
{
	float m_PreviousAlpha = 0.0f;
	float m_CurrentAlpha = 1.0f;
	vec2 m_PreviousBodyScale = vec2(1.0f, 1.0f);
	vec2 m_PreviousFeetScale = vec2(1.0f, 1.0f);
	vec2 m_CurrentBodyScale = vec2(1.0f, 1.0f);
	vec2 m_CurrentFeetScale = vec2(1.0f, 1.0f);
	vec2 m_PreviousPosOffset = vec2(0.0f, 0.0f);
	vec2 m_CurrentPosOffset = vec2(0.0f, 0.0f);
	float m_PreviousAngleOffset = 0.0f;
	float m_CurrentAngleOffset = 0.0f;
};

inline float ClampSkinChangeTransitionProgress(float Progress)
{
	return std::clamp(Progress, 0.0f, 1.0f);
}

inline int ClampSkinChangeTransitionType(int TransitionType)
{
	return std::clamp(TransitionType, 0, SKIN_CHANGE_TRANSITION_TYPE_COUNT - 1);
}

inline int ClampSkinChangeTransitionEasing(int Easing)
{
	return std::clamp(Easing, 0, SKIN_CHANGE_TRANSITION_EASING_COUNT - 1);
}

inline float SkinChangeTransitionIntensityScale(int Intensity)
{
	return std::clamp(Intensity, 0, 300) / 100.0f;
}

inline float QmEvaluateVisualEasing(float Progress, int Easing)
{
	Progress = ClampSkinChangeTransitionProgress(Progress);
	switch(ClampSkinChangeTransitionEasing(Easing))
	{
	case SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK:
	{
		constexpr float Overshoot = 1.70158f;
		const float T = Progress - 1.0f;
		return 1.0f + (Overshoot + 1.0f) * T * T * T + Overshoot * T * T;
	}
	case SKIN_CHANGE_TRANSITION_EASING_LINEAR:
		return Progress;
	case SKIN_CHANGE_TRANSITION_EASING_EASE_IN_OUT_QUAD:
		return Progress < 0.5f ? 2.0f * Progress * Progress : 1.0f - std::pow(-2.0f * Progress + 2.0f, 2.0f) * 0.5f;
	case SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC:
	default:
		return 1.0f - std::pow(1.0f - Progress, 3.0f);
	}
}

inline float SkinChangeTransitionDurationSeconds(int DurationMs)
{
	return std::max(DurationMs, 0) / 1000.0f;
}

inline float ResolveSkinChangeTransitionProgress(float ElapsedSeconds, int DurationMs)
{
	if(DurationMs <= 0)
	{
		return 1.0f;
	}

	const float DurationSeconds = SkinChangeTransitionDurationSeconds(DurationMs);
	if(DurationSeconds <= 0.0f)
	{
		return 1.0f;
	}

	return ClampSkinChangeTransitionProgress(ElapsedSeconds / DurationSeconds);
}

enum class ESkinChangeTransitionAction
{
	KEEP,
	START,
	CANCEL,
};

inline ESkinChangeTransitionAction ResolveSkinChangeTransitionAction(bool HasPreviousAppearance, bool SkinChanged, bool AppearanceChanged)
{
	if(!HasPreviousAppearance || !AppearanceChanged)
		return ESkinChangeTransitionAction::KEEP;
	return SkinChanged ? ESkinChangeTransitionAction::START : ESkinChangeTransitionAction::CANCEL;
}

inline bool ShouldRunLiveSkinChangeTransition(bool DemoPlayback)
{
	return !DemoPlayback;
}

inline SSkinChangeTransitionBlend ComputeSkinChangeTransitionBlend(float Progress, vec2 BodyScale, vec2 FeetScale, int TransitionType, int Easing, int Intensity)
{
	Progress = ClampSkinChangeTransitionProgress(Progress);
	TransitionType = ClampSkinChangeTransitionType(TransitionType);

	const float EaseOut = QmEvaluateVisualEasing(Progress, Easing);
	const float AlphaProgress = std::clamp(EaseOut, 0.0f, 1.0f);
	const float Enter = 1.0f - EaseOut;
	const float IntensityScale = SkinChangeTransitionIntensityScale(Intensity);
	const float Pop = std::sin(Progress * pi) * IntensityScale;
	SSkinChangeTransitionBlend Blend;

	switch(TransitionType)
	{
	case SKIN_CHANGE_TRANSITION_FADE_SCALE:
	{
		const float PreviousScaleFactor = 1.0f - 0.06f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.12f * IntensityScale + 0.12f * IntensityScale * EaseOut;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		break;
	}
	case SKIN_CHANGE_TRANSITION_SLIDE_LEFT:
	{
		const float PreviousScaleFactor = 1.0f - 0.03f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.03f * IntensityScale + 0.03f * IntensityScale * EaseOut;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		Blend.m_PreviousPosOffset = vec2(-14.0f * IntensityScale * EaseOut, 0.0f);
		Blend.m_CurrentPosOffset = vec2(18.0f * IntensityScale * Enter, 0.0f);
		break;
	}
	case SKIN_CHANGE_TRANSITION_SPIN_POP:
	{
		const float PreviousScaleFactor = 1.0f - 0.04f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.08f * IntensityScale + 0.08f * IntensityScale * EaseOut + 0.03f * Pop;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		Blend.m_PreviousAngleOffset = -0.18f * IntensityScale * (1.0f - Progress);
		Blend.m_CurrentAngleOffset = 0.20f * IntensityScale * Enter;
		break;
	}
	case SKIN_CHANGE_TRANSITION_THEME_SWITCH:
	{
		const float PreviousScaleFactor = 1.0f - 0.02f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.04f * IntensityScale + 0.04f * IntensityScale * EaseOut;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		Blend.m_PreviousPosOffset = vec2(0.0f, -8.0f * IntensityScale * EaseOut);
		Blend.m_CurrentPosOffset = vec2(0.0f, 8.0f * IntensityScale * Enter);
		break;
	}
	case SKIN_CHANGE_TRANSITION_GLITCH:
	{
		// Faulty/jitter transition: high-frequency horizontal jitter on both skins.
		// Previous skin jitters leftwards while fading out; current jitters in from the right.
		const float JitterFreq = 7.0f;
		const float JitterAmp = 6.0f * IntensityScale;
		const float PreviousJitter = std::sin(Progress * JitterFreq * pi) * JitterAmp;
		const float CurrentJitter = std::sin((1.0f - Progress) * JitterFreq * pi) * JitterAmp;
		const float PreviousScaleFactor = 1.0f - 0.04f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.04f * IntensityScale + 0.04f * IntensityScale * EaseOut;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		Blend.m_PreviousPosOffset = vec2(-10.0f * IntensityScale * EaseOut + PreviousJitter, 0.0f);
		Blend.m_CurrentPosOffset = vec2(10.0f * IntensityScale * Enter + CurrentJitter, 0.0f);
		break;
	}
	case SKIN_CHANGE_TRANSITION_ELASTIC:
	{
		// Elastic squash-and-stretch: previous skin squashes vertically and fades;
		// current skin bounces in with an overshoot pop. No rotation.
		const float PreviousScaleFactor = 1.0f - 0.10f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.08f * IntensityScale + 0.08f * IntensityScale * EaseOut + 0.06f * Pop;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = AlphaProgress;
		Blend.m_PreviousBodyScale = vec2(BodyScale.x * (1.0f + 0.06f * IntensityScale * EaseOut), BodyScale.y * PreviousScaleFactor);
		Blend.m_PreviousFeetScale = vec2(FeetScale.x * (1.0f + 0.06f * IntensityScale * EaseOut), FeetScale.y * PreviousScaleFactor);
		Blend.m_CurrentBodyScale = vec2(BodyScale.x * CurrentScaleFactor, BodyScale.y * CurrentScaleFactor);
		Blend.m_CurrentFeetScale = vec2(FeetScale.x * CurrentScaleFactor, FeetScale.y * CurrentScaleFactor);
		Blend.m_PreviousPosOffset = vec2(0.0f, 6.0f * IntensityScale * EaseOut);
		Blend.m_CurrentPosOffset = vec2(0.0f, -6.0f * IntensityScale * Enter);
		break;
	}
	case SKIN_CHANGE_TRANSITION_GHOST_POP:
	default:
	{
		const float PreviousScaleFactor = 1.0f - 0.06f * IntensityScale * EaseOut;
		const float CurrentScaleFactor = 1.0f - 0.06f * IntensityScale + 0.06f * IntensityScale * EaseOut + 0.05f * Pop;
		Blend.m_PreviousAlpha = 1.0f - AlphaProgress;
		Blend.m_CurrentAlpha = 0.18f + 0.82f * AlphaProgress;
		Blend.m_PreviousBodyScale = BodyScale * PreviousScaleFactor;
		Blend.m_PreviousFeetScale = FeetScale * PreviousScaleFactor;
		Blend.m_CurrentBodyScale = BodyScale * CurrentScaleFactor;
		Blend.m_CurrentFeetScale = FeetScale * CurrentScaleFactor;
		break;
	}
	}

	return Blend;
}

inline SSkinChangeTransitionBlend ComputeSkinChangeTransitionBlend(float Progress, vec2 BodyScale, vec2 FeetScale, int TransitionType)
{
	return ComputeSkinChangeTransitionBlend(Progress, BodyScale, FeetScale, TransitionType, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 100);
}

inline SSkinChangeTransitionBlend ComputeSkinChangeTransitionBlend(float Progress, int TransitionType, int Easing, int Intensity)
{
	return ComputeSkinChangeTransitionBlend(Progress, vec2(1.0f, 1.0f), vec2(1.0f, 1.0f), TransitionType, Easing, Intensity);
}

inline SSkinChangeTransitionBlend ComputeSkinChangeTransitionBlend(float Progress, int TransitionType)
{
	return ComputeSkinChangeTransitionBlend(Progress, vec2(1.0f, 1.0f), vec2(1.0f, 1.0f), TransitionType);
}

inline SSkinChangeTransitionBlend ComputeSkinChangeTransitionBlend(float Progress)
{
	return ComputeSkinChangeTransitionBlend(Progress, SKIN_CHANGE_TRANSITION_GHOST_POP);
}

class CManagedTeeRenderInfo
{
	friend class CGameClient;
	CTeeRenderInfo m_TeeRenderInfo;
	CSkinDescriptor m_SkinDescriptor;
	bool m_DescriptorRenderInfoReady = false;
	// 失效句柄的修复次数，只用于限制日志刷屏（修复本身每次都会做）。
	int m_StaleRepairAttempts = 0;
	std::function<void()> m_RefreshCallback = nullptr;

public:
	CManagedTeeRenderInfo(const CTeeRenderInfo &TeeRenderInfo, const CSkinDescriptor &SkinDescriptor) :
		m_TeeRenderInfo(TeeRenderInfo),
		m_SkinDescriptor(SkinDescriptor)
	{
	}

	CTeeRenderInfo &TeeRenderInfo() { return m_TeeRenderInfo; }
	const CTeeRenderInfo &TeeRenderInfo() const { return m_TeeRenderInfo; }
	const CSkinDescriptor &SkinDescriptor() const { return m_SkinDescriptor; }
	bool DescriptorRenderInfoReady() const { return m_DescriptorRenderInfoReady; }
	void SetDescriptorRenderInfoReady(bool Ready) { m_DescriptorRenderInfoReady = Ready; }
	void SetRefreshCallback(const std::function<void()> &RefreshCallback) { m_RefreshCallback = RefreshCallback; }
};

// Tee Render Flags
enum
{
	TEE_EFFECT_FROZEN = 1,
	TEE_NO_WEAPON = 2,
	TEE_EFFECT_SPARKLE = 4,
	TEE_CUSTOM_OUTLINE_COLOR = 8,
	TEE_PREVIEW_LAYER_BODY_OUTLINE = 1 << 8,
	TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE = 1 << 9,
	TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE = 1 << 10,
	TEE_PREVIEW_LAYER_OUTLINE = TEE_PREVIEW_LAYER_BODY_OUTLINE | TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE | TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE,
	TEE_PREVIEW_LAYER_BODY = 1 << 11,
	TEE_PREVIEW_LAYER_BACK_FEET = 1 << 12,
	TEE_PREVIEW_LAYER_FRONT_FEET = 1 << 13,
	TEE_PREVIEW_LAYER_FEET = TEE_PREVIEW_LAYER_BACK_FEET | TEE_PREVIEW_LAYER_FRONT_FEET,
	TEE_PREVIEW_LAYER_EYES = 1 << 14,
	TEE_PREVIEW_LAYER_ALL = TEE_PREVIEW_LAYER_OUTLINE | TEE_PREVIEW_LAYER_BODY | TEE_PREVIEW_LAYER_FEET | TEE_PREVIEW_LAYER_EYES,
};

inline int ResolveTeePreviewLayers(int TeeRenderFlags)
{
	const int PreviewLayers = TeeRenderFlags & TEE_PREVIEW_LAYER_ALL;
	return PreviewLayers != 0 ? PreviewLayers : TEE_PREVIEW_LAYER_ALL;
}

inline bool HasTeePreviewLayer(int TeeRenderFlags, int PreviewLayer)
{
	return (ResolveTeePreviewLayers(TeeRenderFlags) & PreviewLayer) != 0;
}

enum
{
	QM_TEXT_EFFECT_BORDER = 1 << 0,
	QM_TEXT_EFFECT_GRADIENT = 1 << 1,
	QM_TEXT_EFFECT_RAINBOW = 1 << 2,
	QM_TEXT_EFFECT_GLOW = 1 << 3,
	// QmClient：名牌文字特效每帧允许的绘制次数；-1 表示不限制（满档，等同历史行为）。
	QM_TEXT_EFFECT_DRAWS_UNLIMITED = -1,
};

struct SQmTextEffectRenderStyle
{
	int m_Effects = 0;
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA m_OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f);
	ColorRGBA m_BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	ColorRGBA m_GradientColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA m_GlowColor = ColorRGBA(0.35f, 0.75f, 1.0f, 0.35f);
	float m_BorderRange = 1.0f;
	float m_GlowRange = 0.0f;
	float m_Time = 0.0f;
	// QmClient：本帧允许的特效绘制次数（不含本体），由名牌的自适应档位写入；
	// QM_TEXT_EFFECT_DRAWS_UNLIMITED 表示满档。设置页预览不写这个字段，因此永远是满档。
	int m_MaxEffectDraws = QM_TEXT_EFFECT_DRAWS_UNLIMITED;
};

// QmClient：旧版 Calamity 语义的头衔绘制参数已移到 qm_title_effect.h（与「抛光」档同处一处，
// 便于对比两套效果的差异）。描边是固定半径的 8 方向偏移（Calamity 的 ExoticRainbow / BurnishedAuric 用 2 像素）；
// 辉光是沿圆周均布的加法混合副本，半径带 sin^5 呼吸、整圈随时间旋转，
// 对应 Calamity 的 16 份 / 半径 4 + 16*sine。

class CRenderTools
{
	class IGraphics *m_pGraphics;
	class ITextRender *m_pTextRender;
	class CGameClient *m_pGameClient;

	int m_TeeQuadContainerIndex;

	static void GetRenderTeeBodyScale(float BaseSize, float &BodyScale);
	static void GetRenderTeeFeetScale(float BaseSize, float &FeetScaleWidth, float &FeetScaleHeight);
	static void GetRenderTeeBodyBounds(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, float AssumedScale, float AnimScale, float &MinX, float &MinY, float &MaxX, float &MaxY);
	static void ExpandRenderTeeFeetBounds(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, float AssumedScale, float AnimScale, float &MinX, float &MaxX, float &MaxY);

	void RenderTee6(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha = 1.0f, vec2 BodyScale = vec2(1.0f, 1.0f), vec2 FeetScale = vec2(1.0f, 1.0f), float BodyAngle = 0.0f, float FeetAngle = 0.0f) const;
	void RenderTee7(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha = 1.0f, vec2 BodyScale = vec2(1.0f, 1.0f), vec2 FeetScale = vec2(1.0f, 1.0f), float BodyAngle = 0.0f, float FeetAngle = 0.0f) const;

public:
	class IGraphics *Graphics() const { return m_pGraphics; }
	class ITextRender *TextRender() const { return m_pTextRender; }
	class CGameClient *GameClient() const { return m_pGameClient; }

	bool m_LocalTeeRender = false; // TClient

	void Init(class IGraphics *pGraphics, class ITextRender *pTextRender, class CGameClient *pGameClient);

	void RenderCursor(vec2 Center, float Size, float Alpha = 1.0f) const;
	void RenderIcon(int ImageId, int SpriteId, const CUIRect *pRect, const ColorRGBA *pColor = nullptr) const;
	void RenderTextContainerWithEffects(STextContainerIndex TextContainerIndex, const SQmTextEffectRenderStyle &Style, float X, float Y) const;
	void RenderTitleContainerWithPolishedEffects(STextContainerIndex TextContainerIndex, const SQmTitlePolishStyle &Style, float X, float Y) const;
	void RenderTitleContainerWithCalamityEffects(STextContainerIndex TextContainerIndex, const SQmTitleEffectStyle &Style, float X, float Y) const;

	// larger rendering methods
	static void GetRenderTeeBodySize(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &BodyOffset, float &Width, float &Height);
	static void GetRenderTeeFeetSize(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &FeetOffset, float &Width, float &Height);
	static void GetRenderTeeAnimScaleAndBaseSize(const CTeeRenderInfo *pInfo, float &AnimScale, float &BaseSize);

	// returns the offset to use, to render the tee with @see RenderTee exactly in the mid
	static void GetRenderTeeOffsetToRenderedTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &TeeOffsetToMid);
	// object render methods
	void RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha = 1.0f) const;
	void RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, int TeeRenderFlags, float Alpha = 1.0f) const;
	void RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale, vec2 FeetScale, float BodyAngle, float FeetAngle) const;
	void RenderTeeWithSkinChangeTransition(const CAnimState *pAnim, const CTeeRenderInfo *pPreviousInfo, const CTeeRenderInfo *pCurrentInfo, int Emote, vec2 Dir, vec2 Pos, float Progress, float Alpha = 1.0f, vec2 BodyScale = vec2(1.0f, 1.0f), vec2 FeetScale = vec2(1.0f, 1.0f), float BodyAngle = 0.0f, float FeetAngle = 0.0f) const;
};

#endif
