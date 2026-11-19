#include "UiFormLogic.h"

#include <base/system.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ui_widget
{
	std::string FormatNumericFieldValue(int StoredValue, const SNumericValueFormat &Format)
	{
		if(Format.m_AllowInfinite && StoredValue == Format.m_InfiniteStoredValue)
			return "∞";
		const int Divisor = std::max(1, Format.m_DisplayDivisor);
		const int Precision = std::clamp(Format.m_Precision, 0, 6);
		if(Precision <= 0)
			return std::to_string(StoredValue / Divisor);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%.*f", Precision, (double)StoredValue / (double)Divisor);
		return aBuf;
	}

	bool ParseNumericFieldValue(const char *pText, const SNumericValueFormat &Format, int StoredMin, int StoredMax, int *pStoredValue)
	{
		if(pText == nullptr || pStoredValue == nullptr || StoredMin > StoredMax)
			return false;
		if(Format.m_AllowInfinite && (str_comp(pText, "∞") == 0 || str_comp_nocase(pText, "inf") == 0))
		{
			*pStoredValue = Format.m_InfiniteStoredValue;
			return true;
		}

		char *pEnd = nullptr;
		const double Parsed = std::strtod(pText, &pEnd);
		if(pEnd == pText || !std::isfinite(Parsed))
			return false;
		while(pEnd != nullptr && *pEnd == ' ')
			++pEnd;
		if(pEnd != nullptr && *pEnd != '\0')
			return false;

		const double Divisor = std::max(1, Format.m_DisplayDivisor);
		const double ClampedDisplayValue = std::clamp(Parsed, StoredMin / Divisor, StoredMax / Divisor);
		*pStoredValue = static_cast<int>(std::round(ClampedDisplayValue * Divisor));
		return true;
	}

	bool NumericFieldShouldCommit(EInputCommitPolicy Policy, bool SliderReleased, const SInputFieldResult &InputResult)
	{
		if(Policy == EInputCommitPolicy::LIVE)
			return InputResult.m_Changed || SliderReleased;
		return SliderReleased || InputResult.m_Submitted || InputResult.m_Deactivated;
	}

	bool UpdateNumericFieldSliderCommit(SNumericFieldCommitState &State, EInputCommitPolicy Policy, bool SliderActive, bool SliderReleased, int CandidateStoredValue, int *pStoredValue)
	{
		if(pStoredValue == nullptr)
			return false;

		State.m_SliderWasActive = SliderActive;
		if(Policy == EInputCommitPolicy::LIVE)
		{
			State.m_HasPendingValue = false;
			if(*pStoredValue == CandidateStoredValue)
				return false;
			*pStoredValue = CandidateStoredValue;
			return true;
		}

		if(SliderActive)
		{
			State.m_PendingStoredValue = CandidateStoredValue;
			State.m_HasPendingValue = true;
			return false;
		}

		if(!SliderReleased || !State.m_HasPendingValue)
			return false;

		State.m_HasPendingValue = false;
		if(*pStoredValue == State.m_PendingStoredValue)
			return false;
		*pStoredValue = State.m_PendingStoredValue;
		return true;
	}

	SNumericFieldLayout ResolveNumericFieldLayout(const CUIRect &Rect, bool HasLabel, bool HasUnit, float UiScale)
	{
		SNumericFieldLayout Layout{};
		const float Scale = std::max(UiScale, 0.1f);
		const float Gap = 8.0f * Scale;
		const float MinSliderWidth = 96.0f * Scale;
		const float InputWidth = 64.0f * Scale;
		const float UnitWidth = HasUnit ? 36.0f * Scale : 0.0f;
		const float LabelWidth = HasLabel ? std::clamp(Rect.w * 0.25f, 108.0f * Scale, 180.0f * Scale) : 0.0f;
		const float RequiredWidth = LabelWidth + (HasLabel ? Gap : 0.0f) + MinSliderWidth + Gap + InputWidth + UnitWidth;
		Layout.m_TwoRows = HasLabel && Rect.w < RequiredWidth;
		Layout.m_Feedback = Layout.m_TwoRows ? ENumericLayoutFeedback::TWO_ROWS : ENumericLayoutFeedback::NONE;

		if(Layout.m_TwoRows)
		{
			Layout.m_LabelRect = {Rect.x, Rect.y, Rect.w, Rect.h};
			Layout.m_InputRect = {Rect.x + Rect.w - InputWidth - UnitWidth, Rect.y, InputWidth, Rect.h};
			Layout.m_UnitRect = {Rect.x + Rect.w - UnitWidth, Rect.y, UnitWidth, Rect.h};
			Layout.m_SliderRect = {Rect.x, Rect.y + Rect.h + Gap * 0.5f, Rect.w, Rect.h};
			return Layout;
		}

		float Cursor = Rect.x;
		if(HasLabel)
		{
			Layout.m_LabelRect = {Cursor, Rect.y, LabelWidth, Rect.h};
			Cursor += LabelWidth + Gap;
		}
		Layout.m_InputRect = {Rect.x + Rect.w - InputWidth - UnitWidth, Rect.y, InputWidth, Rect.h};
		Layout.m_UnitRect = {Rect.x + Rect.w - UnitWidth, Rect.y, UnitWidth, Rect.h};
		Layout.m_SliderRect = {Cursor, Rect.y, std::max(MinSliderWidth, Layout.m_InputRect.x - Gap - Cursor), Rect.h};
		if(Layout.m_SliderRect.w <= MinSliderWidth)
			Layout.m_Feedback = ENumericLayoutFeedback::TRACK_AT_MINIMUM;
		return Layout;
	}
} // namespace ui_widget
