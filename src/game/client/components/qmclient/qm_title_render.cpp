// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_title_render.h"

#include "qmclient_utils.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

SQmTitleRenderStyle QmTitleResolveRenderStyle(const char *pServerStyleId)
{
	// 本地兜底 id 只在 qm_title_style_enabled 打开时才参与，语义与设置页下拉框一致。
	return QmTitleResolveRenderStyle(pServerStyleId, g_Config.m_QmTitleStyleEnabled != 0, g_Config.m_QmTitleStyle);
}

SQmTitleRenderStyle QmTitleResolveRenderStyle(const char *pServerStyleId, const bool LocalStyleEnabled, const char *pLocalStyleId)
{
	SQmTitleRenderStyle Style;
	// 服务端分配的风格优先；未知 id 会被 QmTitleStyleById 拒绝（返回 nullptr）后回退到本地配置。
	if(pServerStyleId != nullptr && pServerStyleId[0] != '\0')
		Style.m_pStyle = QmTitleStyleById(pServerStyleId);
	if(Style.m_pStyle == nullptr && LocalStyleEnabled)
		Style.m_pStyle = QmTitleStyleById(pLocalStyleId);
	if(Style.m_pStyle == nullptr)
		return Style;

	// 配色优先级：本地配色档（单色/彩虹）压过风格自带颜色，风格只保留浮动/掠光/插值。
	// 「跟随服务器」档不覆盖，保持既有表现：服务端下发什么颜色就是什么颜色。
	Style.m_ColorOverride = g_Config.m_QmTitleColorMode == (int)EQmTitleColorMode::SINGLE ||
				g_Config.m_QmTitleColorMode == (int)EQmTitleColorMode::RAINBOW;

	// 服务端只下发风格 id，浮动与相位强度仍由本地配置决定。
	Style.m_Interpolation = EQmTitleInterpolation::Smooth;
	Style.m_Bob.m_Mode = (EQmTitleMotionMode)g_Config.m_QmTitleMotionMode;
	Style.m_Bob.m_Amplitude = (float)g_Config.m_QmTitleBobAmplitude;
	Style.m_Bob.m_WaveLength = (float)g_Config.m_QmTitleBobWavelength;
	Style.m_Bob.m_Speed = (float)g_Config.m_QmTitleBobSpeed / 100.0f;
	Style.m_Bob.m_QuantizeStep = g_Config.m_QmTitleBobPixelSnap != 0 ? 1.0f : 0.0f;
	Style.m_PhasePerPxOverride = (float)g_Config.m_QmTitlePhase / 1000.0f;
	return Style;
}

namespace
{
	float QmTitlePrefixWidth(ITextRender *pTextRender, const char *pText, int PrefixBytes, float FontSize, float LeftX, const CQmTitleTextMetrics *pMetrics)
	{
		if(pMetrics != nullptr)
			return pMetrics->PrefixWidth(PrefixBytes, LeftX);
		char aPrefix[64];
		if(PrefixBytes >= (int)sizeof(aPrefix))
			return LeftX;
		mem_copy(aPrefix, pText, PrefixBytes);
		aPrefix[PrefixBytes] = '\0';
		return pTextRender->TextWidth(FontSize, aPrefix);
	}

	// 向白色插值：只动 RGB，透明度由调用方保持（名牌淡入淡出依赖它）。
	ColorRGBA QmTitleLighten(const ColorRGBA &Color, const float Amount)
	{
		return ColorRGBA(
			Color.r + (1.0f - Color.r) * Amount,
			Color.g + (1.0f - Color.g) * Amount,
			Color.b + (1.0f - Color.b) * Amount,
			Color.a);
	}
}

bool QmTitleRenderFillCursor(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, const float FontSize, const SQmTitleRenderStyle &Style, const float TimeSec, const float Alpha, const SQmTitleShimmer &Shimmer, const CQmTitleTextMetrics *pMetrics, const ColorRGBA &Color, const ColorRGBA &ColorEnd)
{
	if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || Style.m_pStyle == nullptr)
		return false;

	const SQmTitleStyle &SourceStyle = *Style.m_pStyle;
	// 相位系数可被调用方覆盖：多数风格自带值为 0（整行同步变色），
	// 只有提高该值才能让短头衔也看出从左向右扫过的光带。
	SQmTitleStyle TitleStyle = SourceStyle;
	TitleStyle.m_PhasePerPx = QmTitleStyleEffectivePhasePerPx(SourceStyle, Style.m_PhasePerPxOverride);
	const bool UseBob = Style.m_Bob.m_Amplitude != 0.0f && Style.m_Bob.m_WaveLength > 0.0f;

	// 掠光按字符总数分配相位，因此这里按 UTF-8 字符计数（ByteIndex 仍按字节，与引擎语义一致）。
	const int CharCount = QmTitleShimmerUtf8CharCount(pText);

	// 色段与偏移都使用字节偏移作为字符序号，与引擎的 m_CharCount 语义（按字节累加）一致。
	const int BaseIndex = Cursor.m_CharCount;
	const char *pCurrent = pText;
	float PixelX = 0.0f;
	int CharIndex = 0;

	while(*pCurrent != '\0')
	{
		const char *pNext = pCurrent;
		if(str_utf8_decode(&pNext) <= 0)
			break;

		// 当前字符的左边缘等于它的前缀宽度，右边缘等于含入当前字符后的宽度。
		// 这与 Calamity 的 pos.X = MeasureString(已绘制前缀).X 语义一致，也保留了字形间距。
		const float LeftX = PixelX;
		const float RightX = QmTitlePrefixWidth(pTextRender, pText, (int)(pNext - pText), FontSize, LeftX, pMetrics);

		ColorRGBA LeftColor;
		ColorRGBA RightColor;
		if(Style.m_ColorOverride)
		{
			// 本地配色档优先：不采样风格颜色，直接用调用方给的颜色（单色为同一个色，彩虹为逐字符色）。
			LeftColor = Color.WithAlpha(Alpha);
			RightColor = ColorEnd.WithAlpha(Alpha);
		}
		else
		{
			LeftColor = QmTitleStyleSampleWithInterpolation(TitleStyle, TimeSec, LeftX, Style.m_Interpolation).WithAlpha(Alpha);
			RightColor = QmTitleStyleSampleWithInterpolation(TitleStyle, TimeSec, RightX, Style.m_Interpolation).WithAlpha(Alpha);
		}

		if(Shimmer.m_Enabled)
		{
			const float Factor = QmTitleShimmerFactor(Shimmer, CharIndex, CharCount, TimeSec);
			if(Factor > 0.0f)
			{
				// 只提亮 RGB，逐字符透明度（名牌淡入淡出）保持不变。
				LeftColor = QmTitleLighten(LeftColor, Factor);
				RightColor = QmTitleLighten(RightColor, Factor);
			}
		}

		const int ByteIndex = (int)(pCurrent - pText);
		Cursor.m_vColorSplits.emplace_back(BaseIndex + ByteIndex, (int)(pNext - pCurrent), LeftColor, RightColor);

		if(UseBob)
			Cursor.m_vCharOffsets.emplace_back(BaseIndex + ByteIndex, 0.0f, QmTitleStyleBobOffset(Style.m_Bob, TimeSec, LeftX), QmTitleStyleBobScale(Style.m_Bob, TimeSec, LeftX));

		PixelX = RightX;
		pCurrent = pNext;
		++CharIndex;
	}

	// 静态单色且不浮动时内容不随时间变化，调用方无需逐帧重建；
	// 掠光逐帧改变顶点色，必须逐帧重建。
	// 颜色被本地配色档接管时也不能按静态处理：彩虹档逐字符颜色依赖本函数生成的色段。
	return UseBob || Shimmer.m_Enabled || Style.m_ColorOverride || TitleStyle.m_Mode != EQmTitleStyleMode::Static;
}

void QmTitleRenderFillMotionOffsets(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, const float FontSize, const SQmTitleRenderStyle &Style, const float TimeSec, const SQmTitleShimmer &Shimmer, const CQmTitleTextMetrics *pMetrics)
{
	if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || Style.m_pStyle == nullptr)
		return;
	if(Style.m_Bob.m_Amplitude == 0.0f || Style.m_Bob.m_WaveLength <= 0.0f)
		return;

	// 相位与 QmTitleRenderFillCursor 一致（按字符左边缘的累计像素宽度），
	// 否则同一风格在「颜色被本地配色档接管」与「用风格颜色」两种情况下波纹位置会不一致。
	const int BaseIndex = Cursor.m_CharCount;
	const char *pCurrent = pText;
	float PixelX = 0.0f;
	while(*pCurrent != '\0')
	{
		const char *pNext = pCurrent;
		if(str_utf8_decode(&pNext) <= 0)
			break;

		const float LeftX = PixelX;
		const float RightX = QmTitlePrefixWidth(pTextRender, pText, (int)(pNext - pText), FontSize, LeftX, pMetrics);

		Cursor.m_vCharOffsets.emplace_back(BaseIndex + (int)(pCurrent - pText), 0.0f, QmTitleStyleBobOffset(Style.m_Bob, TimeSec, LeftX), QmTitleStyleBobScale(Style.m_Bob, TimeSec, LeftX));
		PixelX = RightX;
		pCurrent = pNext;
	}
}
