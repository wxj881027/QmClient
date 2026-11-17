#ifndef GAME_CLIENT_QMUI_QMPIEOPTIONPOOL_H
#define GAME_CLIENT_QMUI_QMPIEOPTIONPOOL_H

#include "UiSurface.h"
#include "UiTokens.h"

#include <engine/textrender.h>

#include <game/client/ui.h>

#include <algorithm>

// 饼菜单选项池：一条圆角容器里排一批选项胶囊，宽度不够时自动换行。
// 池只表达「这些选项现在在不在饼菜单里」；颜色在预览扇区上改，池里不放色块控件。
// 所有尺寸与颜色都取 token：池本身不持有任何像素或颜色常量。

struct SQmPieOptionChip
{
	CUIRect m_Rect{};
	// 选项在全局选项表里的下标，绘制与拖动都靠它回到配置项。
	int m_Option = -1;
};

struct SQmPieOptionPoolLayout
{
	static constexpr int MAX_CHIPS = 16;
	CUIRect m_Container{};
	SQmPieOptionChip m_aChips[MAX_CHIPS]{};
	int m_Count = 0;
	// 池的总高（含上下内边距）：调用点用它声明布局高度。
	float m_Height = 0.0f;
};

// 单个胶囊的宽度：左右内边距 + 色点 + 间距 + 图标盒 + 图标间距 + 文本宽。
// 文本宽由调用点按正文字号量好后传入，池内不再量字。
inline float QmPieOptionChipWidth(float TextWidth, float BodySize)
{
	const float PadX = ui_token::control::PadX(BodySize);
	const float Dot = ui_token::control::IconBox(BodySize) * 0.5f;
	const float Icon = ui_token::control::IconBox(BodySize);
	return PadX * 2.0f + Dot + ui_token::spacing::XS + Icon + ui_token::control::IconTextGap(BodySize) + std::max(0.0f, TextWidth);
}

// 解一个池的几何：Area 是池容器的矩形（宽由调用点给定，高会被覆盖成算出来的高度）。
// 池为空时仍保留一行胶囊的高度，避免最后一个选项被拖走后整卡高度跳一下。
inline SQmPieOptionPoolLayout QmResolvePieOptionPool(const CUIRect &Area, int Count, const float *pChipWidths, float ChipHeight, float BodySize)
{
	SQmPieOptionPoolLayout Result;
	Result.m_Container = Area;
	const float Pad = ui_token::list::ContainerPadding(ChipHeight);
	const float Gap = ui_token::spacing::XS;
	const float UsableWidth = std::max(0.0f, Area.w - Pad * 2.0f);
	Result.m_Count = std::clamp(Count, 0, SQmPieOptionPoolLayout::MAX_CHIPS);
	float CursorX = Pad;
	float CursorY = Pad;
	int Rows = 1;
	for(int Index = 0; Index < Result.m_Count; ++Index)
	{
		// 胶囊宽度夹在「一个行高」与可用宽度之间：超长名字最多占满一行。
		const float ChipWidth = std::clamp(pChipWidths[Index], ChipHeight, std::max(ChipHeight, UsableWidth));
		const bool Wrap = CursorX > Pad && CursorX + ChipWidth > Pad + UsableWidth;
		if(Wrap)
		{
			CursorX = Pad;
			CursorY += ChipHeight + Gap;
			Rows++;
		}
		Result.m_aChips[Index].m_Rect = {Area.x + CursorX, Area.y + CursorY, ChipWidth, ChipHeight};
		CursorX += ChipWidth + Gap;
	}
	Result.m_Height = Pad * 2.0f + Rows * ChipHeight + (Rows - 1) * Gap;
	// 池自己拥有高度：容器矩形跟着算出来的高度走，调用点只给左上角与宽度。
	// 调用点声明布局高度时取 m_Height，绘制容器时直接用 m_Container，两者不会脱节。
	Result.m_Container.h = Result.m_Height;
	return Result;
}

// 单池内排序（池内拖动改顺序，例如地图标签）：把指针位置翻译成「被拖项移除之后
// 应该插入的槽位」，范围 0..Count-1，直接对应容器里的最终索引。
// 判定按每颗胶囊的横向中心：指针落在某颗左半 → 插到它前面；全部超过 → 插到末尾。
// SkipIndex 是正在拖的那颗：它的矩形仍在布局里（原位保持不动），但不参与比较，
// 否则指针一离开自己就会「插到自己前面」而抖动。
inline int QmResolveChipDropSlot(const CUIRect *pRects, int Count, int SkipIndex, const vec2 &MousePos)
{
	int Slot = 0;
	for(int Index = 0; Index < Count; ++Index)
	{
		if(Index == SkipIndex)
			continue;
		const CUIRect &Rect = pRects[Index];
		if(MousePos.x < Rect.x + Rect.w * 0.5f)
			return Slot;
		++Slot;
	}
	return Slot;
}

// 拖动状态：跨趟跨帧保留。图形/测量趟只读它，只有交互趟推进它。
struct SQmPieOptionPoolDrag
{
	// 起手所在的池与项：-1 表示当前没有拖动。
	int m_Pool = -1;
	int m_Index = -1;
	int m_Option = -1;
	vec2 m_Start{0.0f, 0.0f};
	vec2 m_GrabOffset{0.0f, 0.0f};
	// 指针离开起手点超过一档阈值后才算「拖」，否则仍是一次点击。
	bool m_Dragging = false;
	CUIRect m_GhostRect{};

	void Reset()
	{
		m_Pool = -1;
		m_Index = -1;
		m_Option = -1;
		m_Dragging = false;
	}
	bool Active() const { return m_Pool >= 0; }
};

// 一个选项胶囊：色点 + 图标 + 名字。悬浮与拖动都用同一档白色描边，不跟随选项颜色，
// 否则一堆红/紫胶囊会把「可拖动」读成「错误」或「已选中」。
inline void QmDrawPieOptionChip(const IUiContext &Ctx, const CUIRect &Rect, const char *pLabel, const char *pIcon,
	ColorRGBA OptionColor, float BodySize, bool Hovered, bool Dragging)
{
	if(Ctx.m_pUi == nullptr)
		return;
	const bool Active = Hovered || Dragging;
	const ColorRGBA Fill = Active ? ui_token::color::TINT_ROW_ACTIVE : ui_token::color::TINT_PART;
	const ColorRGBA Border = Active ? ui_token::feedback::ACTIVE_BORDER : ui_token::color::BORDER_SUBTLE;
	DrawRoundedSurface(Ctx, Rect, Fill, Border, ui_token::radius::PILL, ui_token::feedback::ICON_BORDER_WIDTH);

	const float PadX = ui_token::control::PadX(BodySize);
	const float Dot = ui_token::control::IconBox(BodySize) * 0.5f;
	const float Icon = ui_token::control::IconBox(BodySize);
	CUIRect Cursor = Rect;
	Cursor.x += PadX;
	Cursor.w = std::max(0.0f, Cursor.w - PadX);

	CUIRect DotRect{Cursor.x, Rect.y + (Rect.h - Dot) * 0.5f, Dot, Dot};
	DrawRoundedSurface(Ctx, DotRect, OptionColor, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::PILL);
	Cursor.x += Dot + ui_token::spacing::XS;
	Cursor.w = std::max(0.0f, Cursor.w - Dot - ui_token::spacing::XS);

	// 图标沿用预览里的画法：临时切到图标字体，量出真实宽高后居中，再切回正文。
	ITextRender *pText = Ctx.m_pUi->TextRender();
	if(pIcon != nullptr && pIcon[0] != '\0')
	{
		const EFontPreset PreviousPreset = pText->GetFontPreset();
		const ColorRGBA PreviousColor = pText->GetTextColor();
		pText->SetFontPreset(EFontPreset::ICON_FONT);
		pText->TextColor(ui_token::color::TEXT_PRIMARY);
		const float IconWidth = pText->TextWidth(Icon, pIcon);
		CUIRect IconRect{Cursor.x + (Icon - IconWidth) * 0.5f, Rect.y + (Rect.h - Icon) * 0.5f, IconWidth, Icon};
		Ctx.m_pUi->DoLabel(&IconRect, pIcon, Icon, TEXTALIGN_MC);
		pText->SetFontPreset(PreviousPreset);
		pText->TextColor(PreviousColor);
	}
	Cursor.x += Icon + ui_token::control::IconTextGap(BodySize);
	Cursor.w = std::max(0.0f, Cursor.w - Icon - ui_token::control::IconTextGap(BodySize));

	SLabelProperties Props;
	Props.m_MaxWidth = Cursor.w;
	Props.m_EllipsisAtEnd = true;
	Props.SetColor(ui_token::color::TEXT_PRIMARY);
	Ctx.m_pUi->DoLabel(&Cursor, pLabel, BodySize, TEXTALIGN_ML, Props);
}

#endif
