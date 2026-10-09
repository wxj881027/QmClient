#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_GRADIENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_GRADIENT_H

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/QmGradientGeometry.h>
#include <game/client/ui_rect.h>

enum class EQmChatGradientRole
{
	SYSTEM,
	CLIENT,
	HIGHLIGHT,
	TEAM,
	FRIEND,
	NORMAL,
};

inline SQmGradientGeometryBinding QmChatGradientBinding(CConfig &Config, EQmChatGradientRole Role)
{
	switch(Role)
	{
	case EQmChatGradientRole::SYSTEM:
		return {&Config.m_QmChatSystemGradientType, &Config.m_QmChatSystemGradientAngle, &Config.m_QmChatSystemGradientCenterX, &Config.m_QmChatSystemGradientCenterY, &Config.m_QmChatSystemGradientRange, &Config.m_QmChatSystemGradientReverse};
	case EQmChatGradientRole::CLIENT:
		return {&Config.m_QmChatClientGradientType, &Config.m_QmChatClientGradientAngle, &Config.m_QmChatClientGradientCenterX, &Config.m_QmChatClientGradientCenterY, &Config.m_QmChatClientGradientRange, &Config.m_QmChatClientGradientReverse};
	case EQmChatGradientRole::HIGHLIGHT:
		return {&Config.m_QmChatHighlightGradientType, &Config.m_QmChatHighlightGradientAngle, &Config.m_QmChatHighlightGradientCenterX, &Config.m_QmChatHighlightGradientCenterY, &Config.m_QmChatHighlightGradientRange, &Config.m_QmChatHighlightGradientReverse};
	case EQmChatGradientRole::TEAM:
		return {&Config.m_QmChatTeamGradientType, &Config.m_QmChatTeamGradientAngle, &Config.m_QmChatTeamGradientCenterX, &Config.m_QmChatTeamGradientCenterY, &Config.m_QmChatTeamGradientRange, &Config.m_QmChatTeamGradientReverse};
	case EQmChatGradientRole::FRIEND:
		return {&Config.m_QmChatFriendGradientType, &Config.m_QmChatFriendGradientAngle, &Config.m_QmChatFriendGradientCenterX, &Config.m_QmChatFriendGradientCenterY, &Config.m_QmChatFriendGradientRange, &Config.m_QmChatFriendGradientReverse};
	case EQmChatGradientRole::NORMAL:
		return {&Config.m_QmChatNormalGradientType, &Config.m_QmChatNormalGradientAngle, &Config.m_QmChatNormalGradientCenterX, &Config.m_QmChatNormalGradientCenterY, &Config.m_QmChatNormalGradientRange, &Config.m_QmChatNormalGradientReverse};
	}
	return QmChatGradientBinding(Config, EQmChatGradientRole::NORMAL);
}

inline SQmColorGradient QmChatGradientStyle(const CConfig &Config, EQmChatGradientRole Role, ColorRGBA Fallback)
{
	switch(Role)
	{
	case EQmChatGradientRole::SYSTEM:
		return SQmColorGradient::FromConfig(Config.m_ClMessageSystemGradient, Fallback, Config.m_QmChatSystemGradientType, Config.m_QmChatSystemGradientAngle,
			Config.m_QmChatSystemGradientCenterX, Config.m_QmChatSystemGradientCenterY, Config.m_QmChatSystemGradientRange, Config.m_QmChatSystemGradientReverse != 0);
	case EQmChatGradientRole::CLIENT:
		return SQmColorGradient::FromConfig(Config.m_ClMessageClientGradient, Fallback, Config.m_QmChatClientGradientType, Config.m_QmChatClientGradientAngle,
			Config.m_QmChatClientGradientCenterX, Config.m_QmChatClientGradientCenterY, Config.m_QmChatClientGradientRange, Config.m_QmChatClientGradientReverse != 0);
	case EQmChatGradientRole::HIGHLIGHT:
		return SQmColorGradient::FromConfig(Config.m_ClMessageHighlightGradient, Fallback, Config.m_QmChatHighlightGradientType, Config.m_QmChatHighlightGradientAngle,
			Config.m_QmChatHighlightGradientCenterX, Config.m_QmChatHighlightGradientCenterY, Config.m_QmChatHighlightGradientRange, Config.m_QmChatHighlightGradientReverse != 0);
	case EQmChatGradientRole::TEAM:
		return SQmColorGradient::FromConfig(Config.m_ClMessageTeamGradient, Fallback, Config.m_QmChatTeamGradientType, Config.m_QmChatTeamGradientAngle,
			Config.m_QmChatTeamGradientCenterX, Config.m_QmChatTeamGradientCenterY, Config.m_QmChatTeamGradientRange, Config.m_QmChatTeamGradientReverse != 0);
	case EQmChatGradientRole::FRIEND:
		return SQmColorGradient::FromConfig(Config.m_ClMessageFriendGradient, Fallback, Config.m_QmChatFriendGradientType, Config.m_QmChatFriendGradientAngle,
			Config.m_QmChatFriendGradientCenterX, Config.m_QmChatFriendGradientCenterY, Config.m_QmChatFriendGradientRange, Config.m_QmChatFriendGradientReverse != 0);
	case EQmChatGradientRole::NORMAL:
		return SQmColorGradient::FromConfig(Config.m_ClMessageGradient, Fallback, Config.m_QmChatNormalGradientType, Config.m_QmChatNormalGradientAngle,
			Config.m_QmChatNormalGradientCenterX, Config.m_QmChatNormalGradientCenterY, Config.m_QmChatNormalGradientRange, Config.m_QmChatNormalGradientReverse != 0);
	}
	return QmChatGradientStyle(Config, EQmChatGradientRole::NORMAL, Fallback);
}

inline CUIRect QmChatGradientBounds(const CTextCursor &Start, const CTextCursor &Measured)
{
	const bool Wrapped = Measured.m_LineCount > Start.m_LineCount;
	const float X = Wrapped ? Start.m_StartX : Start.m_X;
	const float Width = Wrapped ? Measured.m_LongestLineWidth : Measured.m_X - Start.m_X;
	const float Y = Measured.m_HasVisualBoundingBox ? Measured.m_VisualTop : Start.m_Y;
	const float Height = Measured.m_HasVisualBoundingBox ? Measured.m_VisualBottom - Measured.m_VisualTop :
							       Start.m_FontSize * (Measured.m_LineCount - Start.m_LineCount + 1);
	return {X, Y, std::max(0.0001f, Width), std::max(0.0001f, Height)};
}

// 上下文只借用到同步生成顶点结束；退出后恢复游标，译文标记和后续片段不会继承正文渐变。
class CQmChatGradientPaint
{
	CTextCursor &m_Cursor;
	CTextCursor::FColorSampler m_pfnPreviousSampler;
	const void *m_pPreviousContext;
	int m_PreviousColumns;
	int m_PreviousRows;
	SQmColorGradient m_Gradient;
	SQmGradientTextPaint m_Paint{};

public:
	CQmChatGradientPaint(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, const SQmColorGradient *pGradient) :
		m_Cursor(Cursor),
		m_pfnPreviousSampler(Cursor.m_pfnColorSampler),
		m_pPreviousContext(Cursor.m_pColorSamplerContext),
		m_PreviousColumns(Cursor.m_ColorSamplerColumns),
		m_PreviousRows(Cursor.m_ColorSamplerRows)
	{
		if(pTextRender == nullptr || pGradient == nullptr || pGradient->m_NumColors <= 1 ||
			pText == nullptr || pText[0] == '\0' || (Cursor.m_Flags & TEXTFLAG_RENDER) == 0)
			return;
		m_Gradient = *pGradient;
		CTextCursor Measure = Cursor;
		Measure.m_Flags &= ~TEXTFLAG_RENDER;
		Measure.m_pfnColorSampler = nullptr;
		Measure.m_pColorSamplerContext = nullptr;
		Measure.m_CalculateVisualBoundingBox = true;
		Measure.m_HasVisualBoundingBox = false;
		Measure.m_LongestLineWidth = 0.0f;
		pTextRender->TextEx(&Measure, pText);
		const CUIRect Bounds = QmChatGradientBounds(Cursor, Measure);
		m_Paint = {&m_Gradient, Bounds.TopLeft(), Bounds.Size(), 1.0f};
		const auto Grid = QmGradientTextGrid(m_Gradient, Bounds.Size(), Cursor.m_FontSize);
		Cursor.m_pfnColorSampler = SQmGradientTextPaint::Sample;
		Cursor.m_pColorSamplerContext = &m_Paint;
		Cursor.m_ColorSamplerColumns = Grid[0];
		Cursor.m_ColorSamplerRows = Grid[1];
	}

	~CQmChatGradientPaint()
	{
		m_Cursor.m_pfnColorSampler = m_pfnPreviousSampler;
		m_Cursor.m_pColorSamplerContext = m_pPreviousContext;
		m_Cursor.m_ColorSamplerColumns = m_PreviousColumns;
		m_Cursor.m_ColorSamplerRows = m_PreviousRows;
	}

	CQmChatGradientPaint(const CQmChatGradientPaint &) = delete;
	CQmChatGradientPaint &operator=(const CQmChatGradientPaint &) = delete;
};

#endif
