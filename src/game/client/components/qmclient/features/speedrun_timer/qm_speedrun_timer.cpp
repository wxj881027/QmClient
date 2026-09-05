/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_speedrun_timer.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/textrender.h>

#include <game/localization.h>

void CQmSpeedrunTimer::Render(const float HudWidth, ITextRender *pTextRender) const
{
	const SQmSpeedrunTimerState &TimerState = State();
	if(!TimerState.m_Visible || !pTextRender)
		return;

	char aBuffer[64];
	const float Half = HudWidth / 2.0f;
	if(TimerState.m_Expired)
	{
		str_copy(aBuffer, Localize("TIME EXPIRED!"), sizeof(aBuffer));
		constexpr float FontSize = 12.0f;
		const float Width = pTextRender->TextWidth(FontSize, aBuffer, -1, -1.0f);
		pTextRender->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
		pTextRender->Text(Half - Width / 2.0f, 25.0f, FontSize, aBuffer, -1.0f);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		return;
	}

	QmSpeedrunTimerFormat(TimerState.m_RemainingMilliseconds, aBuffer, sizeof(aBuffer));
	constexpr float FontSize = 8.0f;
	const float Width = pTextRender->TextWidth(FontSize, aBuffer, -1, -1.0f);
	if(TimerState.m_RemainingMilliseconds <= 60 * 1000)
		pTextRender->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
	pTextRender->Text(Half - Width / 2.0f, 20.0f, FontSize, aBuffer, -1.0f);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}
