/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmLayout.h"

#include <algorithm>
#include <vector>

namespace
{
float ResolveLengthNoFlex(const SUiLength &Length, float ParentAxis, float AutoValue)
{
	switch(Length.m_Type)
	{
	case EUiLengthType::AUTO:
		return AutoValue;
	case EUiLengthType::PX:
		return Length.m_Value;
	case EUiLengthType::PERCENT:
		return ParentAxis * Length.m_Value;
	case EUiLengthType::FLEX:
		return AutoValue;
	}
	return AutoValue;
}

bool ResolveConstraint(const SUiLength &Length, float ParentAxis, float &OutValue)
{
	switch(Length.m_Type)
	{
	case EUiLengthType::PX:
		OutValue = Length.m_Value;
		return true;
	case EUiLengthType::PERCENT:
		OutValue = ParentAxis * Length.m_Value;
		return true;
	case EUiLengthType::AUTO:
	case EUiLengthType::FLEX:
		return false;
	}
	return false;
}

float ApplyMinMax(float Value, const SUiLength &MinValue, const SUiLength &MaxValue, float ParentAxis)
{
	float Resolved = Value;
	float Bound = 0.0f;
	if(ResolveConstraint(MinValue, ParentAxis, Bound))
		Resolved = std::max(Resolved, Bound);
	if(ResolveConstraint(MaxValue, ParentAxis, Bound))
		Resolved = std::min(Resolved, Bound);
	return std::max(0.0f, Resolved);
}
}

SUiLayoutBox CUiV2LayoutEngine::ResolveRoot(const SUiLayoutBox &RootBox) const
{
	return RootBox;
}

SUiLayoutBox CUiV2LayoutEngine::ContentBox(const SUiStyle &ContainerStyle, const SUiLayoutBox &ContainerBox) const
{
	SUiLayoutBox Result = ContainerBox;
	Result.m_X += ContainerStyle.m_Padding.m_Left;
	Result.m_Y += ContainerStyle.m_Padding.m_Top;
	Result.m_W -= ContainerStyle.m_Padding.m_Left + ContainerStyle.m_Padding.m_Right;
	Result.m_H -= ContainerStyle.m_Padding.m_Top + ContainerStyle.m_Padding.m_Bottom;
	Result.m_W = std::max(0.0f, Result.m_W);
	Result.m_H = std::max(0.0f, Result.m_H);
	return Result;
}

SUiLayoutBox CUiV2LayoutEngine::ApplyConstraints(const SUiStyle &Style, const SUiLayoutBox &ParentBox) const
{
	SUiLayoutBox Result = ParentBox;

	Result.m_W = ResolveLengthNoFlex(Style.m_Width, ParentBox.m_W, Result.m_W);
	Result.m_H = ResolveLengthNoFlex(Style.m_Height, ParentBox.m_H, Result.m_H);
	Result.m_W = ApplyMinMax(Result.m_W, Style.m_MinWidth, Style.m_MaxWidth, ParentBox.m_W);
	Result.m_H = ApplyMinMax(Result.m_H, Style.m_MinHeight, Style.m_MaxHeight, ParentBox.m_H);

	return Result;
}

void CUiV2LayoutEngine::ComputeChildren(const SUiStyle &ContainerStyle, const SUiLayoutBox &ContainerBox, std::vector<SUiLayoutChild> &vChildren) const
{
	if(vChildren.empty())
		return;

	const bool IsRow = ContainerStyle.m_Axis == EUiAxis::ROW;
	const SUiLayoutBox Content = ContentBox(ContainerStyle, ContainerBox);
	const float ParentMain = IsRow ? Content.m_W : Content.m_H;
	const float ParentCross = IsRow ? Content.m_H : Content.m_W;
	const float BaseGap = std::max(0.0f, ContainerStyle.m_Gap);

	std::vector<float> vMainSizes(vChildren.size(), 0.0f);
	std::vector<float> vCrossSizes(vChildren.size(), 0.0f);
	float FixedMain = 0.0f;
	float FlexSum = 0.0f;

	for(size_t i = 0; i < vChildren.size(); ++i)
	{
		const SUiStyle &Style = vChildren[i].m_Style;
		const SUiLength MainLength = IsRow ? Style.m_Width : Style.m_Height;
		const SUiLength MinMain = IsRow ? Style.m_MinWidth : Style.m_MinHeight;
		const SUiLength MaxMain = IsRow ? Style.m_MaxWidth : Style.m_MaxHeight;
		const float PreferredMain = IsRow ? vChildren[i].m_Box.m_W : vChildren[i].m_Box.m_H;

		if(MainLength.m_Type == EUiLengthType::FLEX)
		{
			FlexSum += std::max(0.0f, MainLength.m_Value);
		}
		else
		{
			float Main = ResolveLengthNoFlex(MainLength, ParentMain, PreferredMain);
			Main = ApplyMinMax(Main, MinMain, MaxMain, ParentMain);
			vMainSizes[i] = Main;
			FixedMain += Main;
		}
	}

	const float GapsTotal = BaseGap * (vChildren.size() > 1 ? static_cast<float>(vChildren.size() - 1) : 0.0f);
	const float AvailableForFlex = std::max(0.0f, ParentMain - FixedMain - GapsTotal);
	if(FlexSum > 0.0f)
	{
		for(size_t i = 0; i < vChildren.size(); ++i)
		{
			const SUiStyle &Style = vChildren[i].m_Style;
			const SUiLength MainLength = IsRow ? Style.m_Width : Style.m_Height;
			if(MainLength.m_Type != EUiLengthType::FLEX)
				continue;

			const SUiLength MinMain = IsRow ? Style.m_MinWidth : Style.m_MinHeight;
			const SUiLength MaxMain = IsRow ? Style.m_MaxWidth : Style.m_MaxHeight;
			const float Weight = std::max(0.0f, MainLength.m_Value);
			float Main = (FlexSum > 0.0f) ? AvailableForFlex * (Weight / FlexSum) : 0.0f;
			Main = ApplyMinMax(Main, MinMain, MaxMain, ParentMain);
			vMainSizes[i] = Main;
		}
	}

	for(size_t i = 0; i < vChildren.size(); ++i)
	{
		const SUiStyle &Style = vChildren[i].m_Style;
		const SUiLength CrossLength = IsRow ? Style.m_Height : Style.m_Width;
		const SUiLength MinCross = IsRow ? Style.m_MinHeight : Style.m_MinWidth;
		const SUiLength MaxCross = IsRow ? Style.m_MaxHeight : Style.m_MaxWidth;
		const float PreferredCross = IsRow ? vChildren[i].m_Box.m_H : vChildren[i].m_Box.m_W;

		const bool StretchAuto = ContainerStyle.m_AlignItems == EUiAlign::STRETCH &&
			(CrossLength.m_Type == EUiLengthType::AUTO || CrossLength.m_Type == EUiLengthType::FLEX);
		float Cross = StretchAuto ? ParentCross : ResolveLengthNoFlex(CrossLength, ParentCross, PreferredCross);
		Cross = ApplyMinMax(Cross, MinCross, MaxCross, ParentCross);
		vCrossSizes[i] = Cross;
	}

	float TotalMain = GapsTotal;
	for(float Size : vMainSizes)
		TotalMain += Size;

	const float FreeMain = std::max(0.0f, ParentMain - TotalMain);
	float LeadingOffset = 0.0f;
	float Gap = BaseGap;
	if(ContainerStyle.m_JustifyContent == EUiAlign::CENTER)
	{
		LeadingOffset = FreeMain / 2.0f;
	}
	else if(ContainerStyle.m_JustifyContent == EUiAlign::END)
	{
		LeadingOffset = FreeMain;
	}
	else if(ContainerStyle.m_JustifyContent == EUiAlign::STRETCH && vChildren.size() > 1)
	{
		Gap += FreeMain / static_cast<float>(vChildren.size() - 1);
	}

	float MainPos = (IsRow ? Content.m_X : Content.m_Y) + LeadingOffset;
	const float CrossStart = IsRow ? Content.m_Y : Content.m_X;
	for(size_t i = 0; i < vChildren.size(); ++i)
	{
		const float MainSize = vMainSizes[i];
		const float CrossSize = vCrossSizes[i];
		const float CrossFree = std::max(0.0f, ParentCross - CrossSize);

		float CrossOffset = 0.0f;
		if(ContainerStyle.m_AlignItems == EUiAlign::CENTER)
			CrossOffset = CrossFree / 2.0f;
		else if(ContainerStyle.m_AlignItems == EUiAlign::END)
			CrossOffset = CrossFree;

		if(IsRow)
		{
			vChildren[i].m_Box.m_X = MainPos;
			vChildren[i].m_Box.m_Y = CrossStart + CrossOffset;
			vChildren[i].m_Box.m_W = MainSize;
			vChildren[i].m_Box.m_H = CrossSize;
		}
		else
		{
			vChildren[i].m_Box.m_X = CrossStart + CrossOffset;
			vChildren[i].m_Box.m_Y = MainPos;
			vChildren[i].m_Box.m_W = CrossSize;
			vChildren[i].m_Box.m_H = MainSize;
		}

		MainPos += MainSize + Gap;
	}
}
