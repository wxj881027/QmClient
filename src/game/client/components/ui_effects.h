#ifndef GAME_CLIENT_COMPONENTS_UI_EFFECTS_H
#define GAME_CLIENT_COMPONENTS_UI_EFFECTS_H

#include <base/color.h>
#include <base/vmath.h>
#include <engine/graphics.h>
#include <game/client/component.h>

// UI Effects component for smooth transitions and animations
class CUiEffects : public CComponent
{
public:
	enum ETransitionType
	{
		TRANSITION_LINEAR = 0,
		TRANSITION_EASE_IN,
		TRANSITION_EASE_OUT,
		TRANSITION_EASE_IN_OUT,
		TRANSITION_BOUNCE,
		NUM_TRANSITION_TYPES
	};

private:
	struct CSmoothValue
	{
		float m_Current;
		float m_Target;
		float m_Speed;
		ETransitionType m_Type;
		
		CSmoothValue() : m_Current(0.0f), m_Target(0.0f), m_Speed(1.0f), m_Type(TRANSITION_EASE_OUT) {}
	};

	std::vector<CSmoothValue> m_vSmoothValues;
	float m_Time;

	struct CScreenshotAnimation
	{
		IGraphics::CTextureHandle m_Texture;
		int m_ImageWidth = 0;
		int m_ImageHeight = 0;
		float m_Time = 0.0f;
		bool m_Active = false;
	};
	CScreenshotAnimation m_ScreenshotAnimation;

	float ApplyTransition(float t, ETransitionType Type);
	void RenderScreenshotAnimation();

public:
	CUiEffects();
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;

	void StartScreenshotAnimation(class CImageInfo &&Image);

	// Smooth value transitions
	int CreateSmoothValue(float Initial = 0.0f, float Speed = 1.0f, ETransitionType Type = TRANSITION_EASE_OUT);
	void SetSmoothValue(int Index, float Target);
	float GetSmoothValue(int Index);
	void UpdateSmoothValue(int Index, float Speed);

	// Color transitions
	static ColorRGBA LerpColor(const ColorRGBA &From, const ColorRGBA &To, float t);
	static ColorRGBA PulseColor(const ColorRGBA &Color, float Time, float Speed = 1.0f);

	// Animation helpers
	float GetPulse(float Speed = 1.0f);
	float GetWave(float Speed = 1.0f, float Offset = 0.0f);
	float GetBounce(float Speed = 1.0f);
};

#endif
