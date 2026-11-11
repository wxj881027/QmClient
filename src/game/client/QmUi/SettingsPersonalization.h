#ifndef GAME_CLIENT_QMUI_SETTINGSPERSONALIZATION_H
#define GAME_CLIENT_QMUI_SETTINGSPERSONALIZATION_H

#include "QmIconSwap.h"
#include "SettingsContentLayout.h"
#include "SettingsRowReveal.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_label.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>

// 展开状态按稳定卡片 ID 保存，搜索页与分类页共用同一份；同时负责读写
// qm_expanded_sections（格式 "stableId=1;stableId2=0;"）。稳定 ID 里含冒号
// （例如 qm:lyrics），所以分段只能用 '=' 与 ';'。
class CSettingsPersonalizationState
{
public:
	// 单条 ID 上限：超长分段整条丢弃，脏数据不会把内存撑起来。
	static constexpr size_t MAX_ID_LENGTH = 128;

private:
	std::map<std::string, bool, std::less<>> m_Expanded;
	std::map<std::string, CSettingsRowReveal, std::less<>> m_Reveals;

public:
	bool Expanded(const char *pStableId) const
	{
		const auto It = m_Expanded.find(pStableId);
		return It != m_Expanded.end() && It->second;
	}

	bool &Section(const char *pStableId)
	{
		const auto It = m_Expanded.find(pStableId);
		return It != m_Expanded.end() ? It->second : m_Expanded.emplace(pStableId, false).first->second;
	}

	// 折叠区内容的显隐进度：首次出现时直接就位（读档即展开的区块不再补播入场），
	// 之后展开/收起才走过渡。返回本帧是否仍要声明内容——收起时过渡播完才返回 false，
	// 避免高度先塌、内容再凭空消失。
	bool AdvanceReveal(const char *pStableId, const bool Visible, const float DeltaTime, const int MotionLevel, float *pProgressOut)
	{
		const auto It = m_Reveals.find(pStableId);
		const bool FirstSeen = It == m_Reveals.end();
		CSettingsRowReveal &Reveal = It != m_Reveals.end() ? It->second : m_Reveals.emplace(pStableId, CSettingsRowReveal()).first->second;
		if(FirstSeen && Visible)
			Reveal.SnapVisible(QmInteractiveRevealSeconds(MotionLevel));
		const bool Declared = Reveal.Update(Visible, DeltaTime, MotionLevel);
		if(pProgressOut != nullptr)
			*pProgressOut = Declared ? Reveal.Visual(MotionLevel).m_Fade : 0.0f;
		return Declared;
	}

	// 逐条解析，坏分段跳过而不中断整串；空串等价于「全部收起」。
	void LoadExpandedSections(const char *pSerialized)
	{
		m_Expanded.clear();
		if(pSerialized == nullptr)
			return;
		const std::string Source(pSerialized);
		size_t Offset = 0;
		while(Offset < Source.size())
		{
			const size_t Separator = Source.find(';', Offset);
			const size_t Next = Separator == std::string::npos ? Source.size() : Separator + 1;
			size_t End = Separator == std::string::npos ? Source.size() : Separator;
			// 配置值可能带行尾空白，先裁掉再按格式判断。
			while(Offset < End && (Source[Offset] == ' ' || Source[Offset] == '\t' || Source[Offset] == '\r' || Source[Offset] == '\n'))
				++Offset;
			while(End > Offset && (Source[End - 1] == ' ' || Source[End - 1] == '\t' || Source[End - 1] == '\r' || Source[End - 1] == '\n'))
				--End;
			const size_t Equals = Source.find('=', Offset);
			// 只接受 "stableId=1" / "stableId=0"：等号前必须有 ID，等号后恰好一位状态。
			if(Equals != std::string::npos && Equals < End && Equals > Offset && End == Equals + 2 &&
				(Source[Equals + 1] == '0' || Source[Equals + 1] == '1'))
			{
				const std::string Id = Source.substr(Offset, Equals - Offset);
				if(Id.size() <= MAX_ID_LENGTH)
					m_Expanded[Id] = Source[Equals + 1] == '1';
			}
			Offset = Next;
		}
	}

	// 写回同一格式；条目按 ID 排序（std::map 天然有序），放不下时停在整条边界上。
	void SaveExpandedSections(char *pBuffer, size_t BufferSize) const
	{
		if(pBuffer == nullptr || BufferSize == 0)
			return;
		pBuffer[0] = '\0';
		for(const auto &[Id, Value] : m_Expanded)
		{
			if(Id.empty() || Id.size() > MAX_ID_LENGTH)
				continue;
			// 末尾还要留 '\0'，所以 ID + "=0;" + NUL 必须一次放得下。
			const size_t Needed = static_cast<size_t>(str_length(pBuffer)) + Id.size() + 3 + 1;
			if(Needed > BufferSize)
				break;
			str_append(pBuffer, Id.c_str(), BufferSize);
			str_append(pBuffer, Value ? "=1;" : "=0;", BufferSize);
		}
	}
};

// 折叠行的配色：分割线由卡片边框色压暗一档，文字默认次要色、悬停或展开用强调色。
struct SSettingsFoldRowStyle
{
	ColorRGBA m_LineColor{};
	ColorRGBA m_TextColor{};
	ColorRGBA m_ActiveColor{};
	// 少数卡片上文已经有视觉分隔（例如紧接一组滑条），再加线会切断阅读，
	// 由调用点关掉线但保留折叠行的其它样式。
	bool m_DrawDivider = true;
};

inline SSettingsFoldRowStyle ResolveSettingsFoldRowStyle(const ColorRGBA &CardBorderColor, const ColorRGBA &AccentColor)
{
	SSettingsFoldRowStyle Style;
	Style.m_LineColor = ui_token::color::UiColorSurface(CardBorderColor, 1.0f, ui_token::settings::row::FOLD_LINE_BRIGHTNESS);
	Style.m_TextColor = ui_token::color::TEXT_SECONDARY;
	Style.m_ActiveColor = AccentColor;
	return Style;
}

// 分割线在行上方占「行距 + 1px + 行距」；折叠行本身仍只占一个标准行高。
inline float SettingsFoldRowDividerHeight(float LineSpacing)
{
	return 2.0f * std::max(0.0f, LineSpacing) + ui_token::settings::row::DIVIDER_THICKNESS;
}

// 折叠行：上方一条压暗的分割线，「小字 + chevron」一组右对齐，整行可点。
// 分割线画在行上方的声明行距里，所以行高保持一个标准行高，不额外补间距。
inline bool SettingsFoldRow(CUi *pUi, const void *pId, const CUIRect &Row, bool DrawDivider, const char *pText, float FontSize, float LineSpacing, bool Expanded, const SSettingsFoldRowStyle &Style)
{
	if(pUi == nullptr || pId == nullptr || Row.w <= 0.0f || Row.h <= 0.0f)
		return false;
	ITextRender *pTextRender = pUi->TextRender();
	if(DrawDivider && Style.m_DrawDivider)
	{
		// 线下沿对齐到行顶上方一个行距，线上方的空缺由调用方声明的行距补足。
		const CUIRect Line{Row.x, Row.y - std::max(0.0f, LineSpacing) - ui_token::settings::row::DIVIDER_THICKNESS, Row.w, ui_token::settings::row::DIVIDER_THICKNESS};
		Line.Draw(Style.m_LineColor, IGraphics::CORNER_NONE, 0.0f);
	}
	const bool Hovered = pUi->VisualSurfaceHovered(&Row);
	const bool Active = Hovered || Expanded;
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	pTextRender->TextColor(Active ? Style.m_ActiveColor : Style.m_TextColor);
	// 图标与文字同字号：图标盒的 0.8 倍就是字形字号（QmIconFontSize），所以盒子按
	// 正文字号反推。以前按整行高度画，字形比文字大一圈，箭头成了视觉主体。
	const float LabelSize = std::min(FontSize, Row.h);
	const float IconBox = std::clamp(LabelSize / 0.8f, 0.0f, Row.h);
	CUIRect TextArea = Row;
	CUIRect IconSlot;
	Row.VSplitRight(IconBox, &TextArea, &IconSlot);
	TextArea.w = std::max(0.0f, TextArea.w - ui_token::spacing::XS);
	if(TextArea.w > 0.0f && pText != nullptr && pText[0] != '\0')
	{
		SLabelProperties Props;
		Props.m_MaxWidth = TextArea.w;
		Props.m_MinimumFontSize = LabelSize;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&TextArea, pText, LabelSize, TEXTALIGN_MR, Props);
	}
	// 箭头与设置卡片标题共用同一条形变轨道：展开/收起是 V↔^ 的补间，不是瞬切；
	// 该字重没有烘焙帧条时由 DrawIconSwap 自动回落成交叉淡化。
	if(!pUi->MeasurementOnly())
	{
		IUiContext IconCtx;
		IconCtx.m_pUi = pUi;
		IconCtx.m_ScopeHash = MakeUiScopeHash("settings-fold-row");
		const ui_widget::SIconSwapBinding Chevron = ui_widget::ResolveIconSwapBinding(
			IconCtx, EQmIconSwapPair::CHEVRON,
			EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN,
			EQmIcon::CHEVRON_UP, FontIcons::FONT_ICON_CHEVRON_UP,
			Expanded ? 1.0f : 0.0f, str_quickhash("fold-row-chevron"), pId);
		const CUIRect IconRect = QmIconLabelRect(IconSlot, IconBox, TEXTALIGN_MR);
		// 只有鼠标悬停时箭头才跟文字同色（强调色）；静止与仅展开时保持全局图标色
		// （默认纯白）。全局「图标颜色」本就会覆盖传入色，所以悬停才临时交出控制权。
		const ColorRGBA IconColor = Hovered ? Style.m_ActiveColor : Style.m_TextColor;
		if(Hovered)
		{
			const CQmIconSemanticColorScope IconColorScope;
			ui_widget::DrawIconSwapBinding(IconCtx, IconRect, IconColor, &Chevron);
		}
		else
		{
			ui_widget::DrawIconSwapBinding(IconCtx, IconRect, IconColor, &Chevron);
		}
	}
	pTextRender->TextColor(PreviousColor);
	return pUi->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_LEFT) != 0;
}

// 声明、输入与绘制走同一入口；收起时调用方不声明细调行，因此不会保留空白或隐藏热区。
// 行前、行后各保留一个标准行距：调用方已经留出间距时只取较大者，不会叠成双倍空隙。
// Button(pId, Row, Expanded, DrawDivider) 负责这一行的绘制与命中，返回本帧是否点到整行；
// 布局与展开状态只在这里维护，测试可以传入不依赖 UI 的桩。
template<typename TButton>
bool SettingsPersonalizationSectionEx(CSettingsPersonalizationState &State, CUIRect &Content, const char *pStableId, const SSettingsContentMetrics &Metrics, bool ReadOnly, bool DrawDivider, TButton &&Button)
{
	bool &Expanded = State.Section(pStableId);
	const float LineHeight = std::max(0.0f, Metrics.m_LineHeight);
	const float LineSpacing = std::max(0.0f, Metrics.m_LineSpacing);
	// 行顶之上还有多少内容：第一行不再画分割线，避免线跑到卡片标题区。
	const float ContentTop = Content.y;
	CSettingsContentLayoutScope *pScope = CSettingsContentLayoutScope::Current();
	const bool LayoutDeclaring = pScope != nullptr && pScope->m_Pass == ESettingsContentPass::DECLARE && pScope->m_pDeclarations != nullptr;
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	// 只有真画分割线时才为它预留「行距 + 1px + 行距」；不画的卡只留一个标准行距，
	// 否则会在折叠行上方凭空多出一条空白带。
	const float ReservedSpacing = DrawDivider ? SettingsFoldRowDividerHeight(LineSpacing) : LineSpacing;
	if(LayoutDeclaring && !pScope->m_pDeclarations->empty())
		pScope->m_pDeclarations->back().m_Spacing = std::max(pScope->m_pDeclarations->back().m_Spacing, ReservedSpacing);
	else if(pScope == nullptr)
		Flow.Space(ReservedSpacing); // 非声明式卡片没有布局回放，自己留出线的位置
	if(Flow.Control("personalization", LineHeight, Row))
	{
		const bool Clicked = Button(&Expanded, Row, Expanded, DrawDivider && Row.y > ContentTop);
		if(Clicked && Flow.Input() && !ReadOnly)
			Expanded = !Expanded;
	}
	if(LayoutDeclaring && !pScope->m_pDeclarations->empty())
	{
		// 行后间距先记入声明；展开内容再声明同一处间距时只取较大者。
		auto &Current = pScope->m_pDeclarations->back();
		Current.m_Spacing = std::max(Current.m_Spacing, LineSpacing);
		pScope->m_MinimumSpacingRow = pScope->m_pDeclarations->size() - 1;
	}
	else
	{
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
	}
	return Expanded;
}

namespace qm_card_catalog
{
	// 卡片自己画的可折叠行（例如轮廓类型行）与「个性调整」折叠行共用同一份展开状态
	// 与同一份持久化：不允许卡片另存一份，否则重启后两套状态会不一致。
	// 实现见 cards/QmCardCatalog.cpp。返回引用可直接读写，写入后调用保存入口落盘。
	bool &SettingsPersonalizationExpanded(const char *pStableId);
	void SaveSettingsPersonalizationExpanded();
} // namespace qm_card_catalog

// 兼容入口：默认画分割线，调用点签名不变。
template<typename TButton>
bool SettingsPersonalizationSection(CSettingsPersonalizationState &State, CUIRect &Content, const char *pStableId, const SSettingsContentMetrics &Metrics, bool ReadOnly, TButton &&Button)
{
	return SettingsPersonalizationSectionEx(State, Content, pStableId, Metrics, ReadOnly, true, std::forward<TButton>(Button));
}

#endif
