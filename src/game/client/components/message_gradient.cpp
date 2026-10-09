// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "message_gradient.h"

#include <base/str.h>

#include <algorithm>

namespace
{
	bool IsGradientDelimiter(char Character)
	{
		return Character == ',' || Character == ';' || Character == ' ' || Character == '\t';
	}

	ColorRGBA LerpColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
	{
		Amount = std::clamp(Amount, 0.0f, 1.0f);
		return ColorRGBA(
			From.r + (To.r - From.r) * Amount,
			From.g + (To.g - From.g) * Amount,
			From.b + (To.b - From.b) * Amount,
			From.a + (To.a - From.a) * Amount);
	}

	ColorRGBA SampleGradient(const ColorRGBA *pColors, int NumColors, float Amount)
	{
		if(NumColors <= 0)
			return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(NumColors == 1)
			return pColors[0];

		Amount = std::clamp(Amount, 0.0f, 1.0f);
		const float ScaledAmount = Amount * (NumColors - 1);
		const int ColorIndex = std::clamp((int)ScaledAmount, 0, NumColors - 2);
		return LerpColor(pColors[ColorIndex], pColors[ColorIndex + 1], ScaledAmount - ColorIndex);
	}

	int CountUtf8Chars(const char *pText)
	{
		int NumChars = 0;
		const char *pCurrent = pText;
		while(str_utf8_decode(&pCurrent) > 0)
			++NumChars;
		return NumChars;
	}
}

int CMessageGradient::Unpack(const char *pGradient, unsigned *pColors, int MaxColors)
{
	if(pGradient == nullptr || pGradient[0] == '\0' || pColors == nullptr || MaxColors <= 0)
		return 0;

	int NumColors = 0;
	const char *pCursor = pGradient;
	char aColor[16];
	while(*pCursor != '\0' && NumColors < MaxColors)
	{
		while(IsGradientDelimiter(*pCursor))
			++pCursor;
		if(*pCursor == '\0')
			break;

		int ColorLength = 0;
		while(pCursor[ColorLength] != '\0' && !IsGradientDelimiter(pCursor[ColorLength]))
			++ColorLength;
		str_copy(aColor, pCursor, std::min<int>(sizeof(aColor), ColorLength + 1));
		pCursor += ColorLength;

		const char *pColor = aColor;
		if(pColor[0] == '#')
			++pColor;
		if(pColor[0] == '$')
			++pColor;

		const auto Color = color_parse<ColorRGBA>(pColor);
		if(!Color.has_value())
			continue;

		pColors[NumColors++] = color_cast<ColorHSLA>(*Color).Pack(true);
	}

	return NumColors;
}

void CMessageGradient::Pack(const unsigned *pColors, int NumColors, char *pGradient, int GradientSize)
{
	if(pGradient == nullptr || GradientSize <= 0)
		return;

	pGradient[0] = '\0';
	if(pColors == nullptr || NumColors <= 0)
		return;

	NumColors = std::clamp(NumColors, MIN_COLORS, MAX_COLORS);
	for(int ColorIndex = 0; ColorIndex < NumColors; ++ColorIndex)
	{
		char aColor[16];
		const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(pColors[ColorIndex], true));
		const bool Alpha = (pColors[ColorIndex] >> 24) != 0xffu;
		str_format(aColor, sizeof(aColor), "%s%0*X", ColorIndex == 0 ? "" : ",", Alpha ? 8 : 6, Color.PackAlphaLast(Alpha));
		str_append(pGradient, aColor, GradientSize);
	}
}

void CMessageGradient::Reset(char *pGradient, int GradientSize)
{
	if(pGradient != nullptr && GradientSize > 0)
		pGradient[0] = '\0';
}

void CMessageGradient::AddTextSplits(CTextCursor &Cursor, const char *pText, const char *pGradient, const ColorRGBA &FallbackColor)
{
	unsigned aPackedColors[MAX_COLORS];
	const int NumColors = Unpack(pGradient, aPackedColors, MAX_COLORS);
	if(NumColors <= 1)
		return;

	const int NumChars = CountUtf8Chars(pText);
	if(NumChars <= 0)
		return;

	ColorRGBA aColors[MAX_COLORS];
	for(int ColorIndex = 0; ColorIndex < NumColors; ++ColorIndex)
		aColors[ColorIndex] = color_cast<ColorRGBA>(ColorHSLA(aPackedColors[ColorIndex], true));

	const int StartChar = Cursor.m_CharCount;
	const float Denominator = NumChars > 1 ? (float)(NumChars - 1) : 1.0f;
	Cursor.m_vColorSplits.reserve(Cursor.m_vColorSplits.size() + NumChars);

	int CharIndex = 0;
	const char *pCurrent = pText;
	while(*pCurrent != '\0' && CharIndex < NumChars)
	{
		const char *pNext = pCurrent;
		if(str_utf8_decode(&pNext) <= 0)
			break;
		ColorRGBA Color = SampleGradient(aColors, NumColors, CharIndex / Denominator);
		Color.a *= FallbackColor.a;
		Cursor.m_vColorSplits.emplace_back(StartChar + (int)(pCurrent - pText), pNext - pCurrent, Color);
		pCurrent = pNext;
		++CharIndex;
	}
}
