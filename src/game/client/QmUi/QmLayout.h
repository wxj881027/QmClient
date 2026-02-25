/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_UI_QM_LAYOUT_H
#define GAME_CLIENT_QM_UI_QM_LAYOUT_H

#include <vector>

enum class EUiAxis
{
	ROW,
	COLUMN,
};

enum class EUiAlign
{
	START,
	CENTER,
	END,
	STRETCH,
};

enum class EUiLengthType
{
	AUTO,
	PX,
	PERCENT,
	FLEX,
};

struct SUiLength
{
	EUiLengthType m_Type = EUiLengthType::AUTO;
	float m_Value = 0.0f;

	static SUiLength Auto()
	{
		return {EUiLengthType::AUTO, 0.0f};
	}

	static SUiLength Px(float Value)
	{
		return {EUiLengthType::PX, Value};
	}

	static SUiLength Percent(float Value)
	{
		return {EUiLengthType::PERCENT, Value};
	}

	static SUiLength Flex(float Value)
	{
		return {EUiLengthType::FLEX, Value};
	}
};

struct SUiEdges
{
	float m_Left = 0.0f;
	float m_Top = 0.0f;
	float m_Right = 0.0f;
	float m_Bottom = 0.0f;
};

struct SUiLayoutBox
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
};

struct SUiStyle
{
	EUiAxis m_Axis = EUiAxis::COLUMN;
	SUiLength m_Width = SUiLength::Auto();
	SUiLength m_Height = SUiLength::Auto();
	SUiLength m_MinWidth = SUiLength::Auto();
	SUiLength m_MinHeight = SUiLength::Auto();
	SUiLength m_MaxWidth = SUiLength::Auto();
	SUiLength m_MaxHeight = SUiLength::Auto();
	SUiEdges m_Padding;
	float m_Gap = 0.0f;
	EUiAlign m_AlignItems = EUiAlign::START;
	EUiAlign m_JustifyContent = EUiAlign::START;
	bool m_Clip = false;
};

struct SUiLayoutChild
{
	SUiStyle m_Style;
	SUiLayoutBox m_Box;
};

class CUiV2LayoutEngine
{
public:
	SUiLayoutBox ResolveRoot(const SUiLayoutBox &RootBox) const;
	SUiLayoutBox ContentBox(const SUiStyle &ContainerStyle, const SUiLayoutBox &ContainerBox) const;
	SUiLayoutBox ApplyConstraints(const SUiStyle &Style, const SUiLayoutBox &ParentBox) const;
	void ComputeChildren(const SUiStyle &ContainerStyle, const SUiLayoutBox &ContainerBox, std::vector<SUiLayoutChild> &vChildren) const;
};

#endif
