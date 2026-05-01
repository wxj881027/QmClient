#include "ui_effects.h"

#include <engine/shared/config.h>
#include <engine/image.h>
#include <game/client/gameclient.h>

#include <cmath>
#include <algorithm>

constexpr float PI = 3.14159265358979323846f;
constexpr float SMOOTH_VALUE_EPSILON = 0.01f; // Threshold for smooth value convergence
constexpr float SMOOTH_VALUE_STEP_MULTIPLIER = 10.0f; // Controls interpolation speed
constexpr float SCREENSHOT_ANIMATION_DURATION = 0.75f;
constexpr float SCREENSHOT_FLASH_DURATION = 0.16f;
constexpr float SCREENSHOT_SHRINK_START = 0.12f;
constexpr float SCREENSHOT_THUMBNAIL_SCREEN_FRACTION = 0.23f;
constexpr float SCREENSHOT_THUMBNAIL_MARGIN = 14.0f;

CUiEffects::CUiEffects()
{
	m_Time = 0.0f;
}

void CUiEffects::OnReset()
{
	if(m_ScreenshotAnimation.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);
	m_ScreenshotAnimation = CScreenshotAnimation();
	m_vSmoothValues.clear();
	m_Time = 0.0f;
}

void CUiEffects::OnShutdown()
{
	if(m_ScreenshotAnimation.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);
	m_ScreenshotAnimation = CScreenshotAnimation();
}

void CUiEffects::OnRender()
{
	const float TimePassed = Client()->RenderFrameTime();
	m_Time += TimePassed;

	if(m_ScreenshotAnimation.m_Active)
	{
		m_ScreenshotAnimation.m_Time += TimePassed;
		RenderScreenshotAnimation();
	}

	if(!g_Config.m_ClHudAnimations)
		return;

	// Update all smooth values
	const float SpeedMultiplier = g_Config.m_ClHudAnimationSpeed / 100.0f;
	for(auto &Value : m_vSmoothValues)
	{
		if(std::abs(Value.m_Current - Value.m_Target) < SMOOTH_VALUE_EPSILON)
		{
			Value.m_Current = Value.m_Target;
			continue;
		}

		const float Distance = Value.m_Target - Value.m_Current;
		const float Step = Distance * Value.m_Speed * SpeedMultiplier * TimePassed * SMOOTH_VALUE_STEP_MULTIPLIER;
		Value.m_Current += Step;
	}
}

void CUiEffects::StartScreenshotAnimation(CImageInfo &&Image)
{
	if(!Image.m_pData || Image.m_Width == 0 || Image.m_Height == 0)
		return;

	if(m_ScreenshotAnimation.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);

	const int ImageWidth = (int)Image.m_Width;
	const int ImageHeight = (int)Image.m_Height;
	IGraphics::CTextureHandle Texture = Graphics()->LoadTextureRawMove(Image, IGraphics::TEXLOAD_NO_MIPMAPS, "screenshot-preview");
	if(!Texture.IsValid())
		return;

	m_ScreenshotAnimation.m_Texture = Texture;
	m_ScreenshotAnimation.m_ImageWidth = ImageWidth;
	m_ScreenshotAnimation.m_ImageHeight = ImageHeight;
	m_ScreenshotAnimation.m_Time = 0.0f;
	m_ScreenshotAnimation.m_Active = true;
}

void CUiEffects::RenderScreenshotAnimation()
{
	if(!m_ScreenshotAnimation.m_Texture.IsValid() || m_ScreenshotAnimation.m_ImageWidth <= 0 || m_ScreenshotAnimation.m_ImageHeight <= 0)
	{
		m_ScreenshotAnimation.m_Active = false;
		return;
	}

	if(m_ScreenshotAnimation.m_Time >= SCREENSHOT_ANIMATION_DURATION)
	{
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);
		m_ScreenshotAnimation = CScreenshotAnimation();
		return;
	}

	float PreviousTopLeftX = 0.0f;
	float PreviousTopLeftY = 0.0f;
	float PreviousBottomRightX = 0.0f;
	float PreviousBottomRightY = 0.0f;
	Graphics()->GetScreen(&PreviousTopLeftX, &PreviousTopLeftY, &PreviousBottomRightX, &PreviousBottomRightY);

	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const float ScreenWidth = pScreen->w;
	const float ScreenHeight = pScreen->h;

	const float t = std::clamp(m_ScreenshotAnimation.m_Time / SCREENSHOT_ANIMATION_DURATION, 0.0f, 1.0f);
	const float ShrinkT = std::clamp((m_ScreenshotAnimation.m_Time - SCREENSHOT_SHRINK_START) / (SCREENSHOT_ANIMATION_DURATION - SCREENSHOT_SHRINK_START), 0.0f, 1.0f);
	const float Ease = ApplyTransition(ShrinkT, TRANSITION_EASE_IN_OUT);
	const float ImageAspect = (float)m_ScreenshotAnimation.m_ImageWidth / (float)m_ScreenshotAnimation.m_ImageHeight;
	const float TargetWidth = ScreenWidth * SCREENSHOT_THUMBNAIL_SCREEN_FRACTION;
	const float TargetHeight = TargetWidth / ImageAspect;
	const float StartX = 0.0f;
	const float StartY = 0.0f;
	const float StartW = ScreenWidth;
	const float StartH = ScreenHeight;
	const float TargetX = ScreenWidth - TargetWidth - SCREENSHOT_THUMBNAIL_MARGIN;
	const float TargetY = SCREENSHOT_THUMBNAIL_MARGIN;

	const float PreviewX = StartX + (TargetX - StartX) * Ease;
	const float PreviewY = StartY + (TargetY - StartY) * Ease;
	const float PreviewW = StartW + (TargetWidth - StartW) * Ease;
	const float PreviewH = StartH + (TargetHeight - StartH) * Ease;
	const float PreviewAlpha = t > 0.82f ? 1.0f - (t - 0.82f) / 0.18f : 1.0f;
	const float FlashAlpha = m_ScreenshotAnimation.m_Time < SCREENSHOT_FLASH_DURATION ? 0.65f * (1.0f - m_ScreenshotAnimation.m_Time / SCREENSHOT_FLASH_DURATION) : 0.0f;

	if(Ease > 0.08f)
	{
		const float ShadowOffset = 3.0f + 4.0f * Ease;
		Graphics()->DrawRect(PreviewX + ShadowOffset, PreviewY + ShadowOffset, PreviewW, PreviewH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f * Ease * PreviewAlpha), IGraphics::CORNER_ALL, 6.0f);
	}

	Graphics()->TextureSet(m_ScreenshotAnimation.m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, PreviewAlpha);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem PreviewQuad(PreviewX, PreviewY, PreviewW, PreviewH);
	Graphics()->QuadsDrawTL(&PreviewQuad, 1);
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();

	if(FlashAlpha > 0.0f)
		Graphics()->DrawRect(0.0f, 0.0f, ScreenWidth, ScreenHeight, ColorRGBA(1.0f, 1.0f, 1.0f, FlashAlpha), IGraphics::CORNER_NONE, 0.0f);

	Graphics()->MapScreen(PreviousTopLeftX, PreviousTopLeftY, PreviousBottomRightX, PreviousBottomRightY);
}

float CUiEffects::ApplyTransition(float t, ETransitionType Type)
{
	t = std::clamp(t, 0.0f, 1.0f);
	
	switch(Type)
	{
	case TRANSITION_LINEAR:
		return t;
	case TRANSITION_EASE_IN:
		return t * t;
	case TRANSITION_EASE_OUT:
		return t * (2.0f - t);
	case TRANSITION_EASE_IN_OUT:
		return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
	case TRANSITION_BOUNCE:
		{
			const float n1 = 7.5625f;
			const float d1 = 2.75f;
			if(t < 1.0f / d1)
				return n1 * t * t;
			else if(t < 2.0f / d1)
			{
				t -= 1.5f / d1;
				return n1 * t * t + 0.75f;
			}
			else if(t < 2.5f / d1)
			{
				t -= 2.25f / d1;
				return n1 * t * t + 0.9375f;
			}
			else
			{
				t -= 2.625f / d1;
				return n1 * t * t + 0.984375f;
			}
		}
	default:
		return t;
	}
}

int CUiEffects::CreateSmoothValue(float Initial, float Speed, ETransitionType Type)
{
	CSmoothValue Value;
	Value.m_Current = Initial;
	Value.m_Target = Initial;
	Value.m_Speed = Speed;
	Value.m_Type = Type;
	
	m_vSmoothValues.push_back(Value);
	return m_vSmoothValues.size() - 1;
}

void CUiEffects::SetSmoothValue(int Index, float Target)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;
	
	m_vSmoothValues[Index].m_Target = Target;
}

float CUiEffects::GetSmoothValue(int Index)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return 0.0f;
	
	return m_vSmoothValues[Index].m_Current;
}

void CUiEffects::UpdateSmoothValue(int Index, float Speed)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;
	
	m_vSmoothValues[Index].m_Speed = Speed;
}

ColorRGBA CUiEffects::LerpColor(const ColorRGBA &From, const ColorRGBA &To, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return ColorRGBA(
		From.r + (To.r - From.r) * t,
		From.g + (To.g - From.g) * t,
		From.b + (To.b - From.b) * t,
		From.a + (To.a - From.a) * t
	);
}

ColorRGBA CUiEffects::PulseColor(const ColorRGBA &Color, float Time, float Speed)
{
	const float Pulse = (std::sin(Time * Speed * PI) + 1.0f) * 0.5f;
	return ColorRGBA(
		Color.r * (0.5f + Pulse * 0.5f),
		Color.g * (0.5f + Pulse * 0.5f),
		Color.b * (0.5f + Pulse * 0.5f),
		Color.a
	);
}

float CUiEffects::GetPulse(float Speed)
{
	return (std::sin(m_Time * Speed * PI) + 1.0f) * 0.5f;
}

float CUiEffects::GetWave(float Speed, float Offset)
{
	return std::sin((m_Time * Speed + Offset) * PI);
}

float CUiEffects::GetBounce(float Speed)
{
	const float t = std::fmod(m_Time * Speed, 1.0f);
	return ApplyTransition(t, TRANSITION_BOUNCE);
}
