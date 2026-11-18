#ifndef GAME_CLIENT_QMUI_QMLISTCARD_H
#define GAME_CLIENT_QMUI_QMLISTCARD_H

#include "UiButtons.h"
#include "UiSurface.h"
#include "UiTokens.h"

#include <game/client/ui_rect.h>

// 列表卡片（list card）：网格或列表里的「一张卡」的几何与外壳，两种形态共用同一条路径。
//
// 形态（EAxis）：
// - STACK：上下堆叠——头部（标题 / 副标题 / 尾部徽标·按钮）+ 内容区 + 可选底栏。
//   资源贴图卡、指令工坊预设卡用这一种。
// - ROW：左右分栏——前置区（缩略图/ttee 预览）+ 主体 + 可选尾部槽。
//   皮肤列表的格子用这一种（前置 60 宽预览 + 名称/图标）。
//
// 组件只负责「一张卡占多大、各部分从哪切、外壳怎么画」：
// 内容绘制、数据加载、缩略图任务调度、入场动画全部留在调用点——资源页与皮肤页的
// 异步加载/预览缓存/预算调度都是各自独一份的，不进组件。
//
// 与设置卡片（SettingsCard）是两个组件，不要混用：设置卡片是按页纵向堆叠的功能模块，
// 有自己的标题栏、折叠、展开高度测量与卡间阴影。
namespace qm_list_card
{
	enum class EAxis
	{
		// 上下堆叠：头部 → 内容 → 底栏。
		STACK,
		// 左右分栏：前置 → 主体 → 尾部槽。
		ROW,
	};

	struct SMetrics
	{
		float m_UiScale = 1.0f;
		// 内容到卡片四边：与设置卡片同一档 token；行式卡片传 0（内容直接落在格子矩形里）。
		float m_Padding = ui_token::settings::CARD_PADDING;
		// 卡片圆角：固定卡片档，不再按行高或格子宽度推导。
		float m_Radius = ui_token::radius::CARD;
		// 标题行高与副标题行高（未乘 UiScale 的字体行高档）。
		float m_TitleHeight = ui_token::font::HEADLINE * 1.25f;
		float m_SubtitleHeight = ui_token::font::SMALL * 1.4f;
		// 头部与内容区之间的一档缝。
		float m_HeaderGap = ui_token::spacing::SM;
		// 内容区与底栏之间的一档缝。
		float m_FooterGap = ui_token::spacing::SM;

		SMetrics Scaled() const
		{
			SMetrics Result = *this;
			Result.m_Padding *= m_UiScale;
			Result.m_Radius *= m_UiScale;
			Result.m_TitleHeight *= m_UiScale;
			Result.m_SubtitleHeight *= m_UiScale;
			Result.m_HeaderGap *= m_UiScale;
			Result.m_FooterGap *= m_UiScale;
			return Result;
		}
	};

	struct SOptions
	{
		EAxis m_Axis = EAxis::STACK;

		// STACK 形态
		// 标题下面是否还有一行副标题（作者、当前状态等）。
		bool m_HasSubtitle = false;
		// 头部右侧尾部区宽度（徽标 + 操作按钮）；0 表示没有尾部区。
		float m_TrailingWidth = 0.0f;
		// 尾部区只占标题行，还是吃掉整段头部（含副标题行）。
		bool m_TrailingOnTitleRow = true;
		// 底部动作区高度；0 表示没有底栏。
		float m_FooterHeight = 0.0f;

		// ROW 形态
		// 前置区宽度（预览/头像）；0 表示没有前置区。
		float m_LeadingWidth = 0.0f;
		// 尾部槽宽度（右侧动作或数值）；0 表示没有。
		float m_TrailingSlotWidth = 0.0f;
		// 前置/尾部槽与主体之间的缝。
		float m_RowGap = ui_token::spacing::XS;
	};

	struct SLayout
	{
		// 卡片外壳矩形（含内边距）。
		CUIRect m_Card;
		// 生效圆角（已乘 UiScale），外壳绘制直接取它，调用点不再自己乘。
		float m_Radius = ui_token::radius::CARD;
		// 内边距之后的可用区。
		CUIRect m_Content;

		// STACK：头部整段（标题行 + 可选副标题行）与它的两行。
		CUIRect m_Header;
		CUIRect m_Title;
		CUIRect m_Subtitle;
		// STACK：头部右侧尾部区（徽标、图标按钮）。没有时为零矩形。
		CUIRect m_Trailing;
		// STACK：底栏（模式行、应用按钮等）。没有时为零矩形。
		CUIRect m_Footer;

		// ROW：前置区与尾部槽。不在该形态时为零矩形。
		CUIRect m_Leading;
		CUIRect m_TrailingSlot;

		// 两种形态共用的主体：STACK 是头部之下、底栏之上；ROW 是前置与尾部槽之间。
		CUIRect m_Body;
	};

	namespace detail
	{
		inline SLayout ResolveStackLayout(const CUIRect &Card, const SMetrics &Metrics, const SOptions &Options, SLayout Layout)
		{
			const float HeaderHeight = Options.m_HasSubtitle ? Metrics.m_TitleHeight + Metrics.m_SubtitleHeight : Metrics.m_TitleHeight;
			Layout.m_Header = CUIRect{Layout.m_Content.x, Layout.m_Content.y, Layout.m_Content.w, std::min(HeaderHeight, Layout.m_Content.h)};

			// 尾部先切右侧：标题列拿到的是「头部宽 - 尾部宽 - 一档缝」，标题再长也只省略不挤压。
			CUIRect TitleRow = Layout.m_Header;
			if(Options.m_TrailingWidth > 0.0f)
			{
				const float TrailingWidth = std::min(Options.m_TrailingWidth, std::max(0.0f, Layout.m_Header.w));
				Layout.m_Trailing = CUIRect{
					Layout.m_Header.x + Layout.m_Header.w - TrailingWidth,
					Layout.m_Header.y,
					TrailingWidth,
					Options.m_TrailingOnTitleRow ? std::min(Metrics.m_TitleHeight, Layout.m_Header.h) : Layout.m_Header.h};
				TitleRow.w = std::max(0.0f, TitleRow.w - TrailingWidth - ui_token::spacing::XS);
			}

			Layout.m_Title = CUIRect{TitleRow.x, TitleRow.y, TitleRow.w, std::min(Metrics.m_TitleHeight, TitleRow.h)};
			if(Options.m_HasSubtitle)
				Layout.m_Subtitle = CUIRect{TitleRow.x, TitleRow.y + Metrics.m_TitleHeight, TitleRow.w, std::max(0.0f, TitleRow.h - Metrics.m_TitleHeight)};

			// 剩余高度按「头部 → 缝 → 内容 → 缝 → 底栏」切；高度不够时优先保住内容区。
			float BodyTop = Layout.m_Header.y + Layout.m_Header.h + Metrics.m_HeaderGap;
			float BodyBottom = Layout.m_Content.y + Layout.m_Content.h;
			if(Options.m_FooterHeight > 0.0f)
			{
				const float FooterHeight = std::min(Options.m_FooterHeight, std::max(0.0f, BodyBottom - BodyTop));
				Layout.m_Footer = CUIRect{Layout.m_Content.x, BodyBottom - FooterHeight, Layout.m_Content.w, FooterHeight};
				BodyBottom = Layout.m_Footer.y - Metrics.m_FooterGap;
			}
			Layout.m_Body = CUIRect{Layout.m_Content.x, BodyTop, Layout.m_Content.w, std::max(0.0f, BodyBottom - BodyTop)};
			return Layout;
		}

		inline SLayout ResolveRowLayout(const CUIRect &Card, const SMetrics &Metrics, const SOptions &Options, SLayout Layout)
		{
			CUIRect Row = Layout.m_Content;
			if(Options.m_LeadingWidth > 0.0f)
			{
				const float LeadingWidth = std::min(Options.m_LeadingWidth, std::max(0.0f, Row.w));
				Layout.m_Leading = CUIRect{Row.x, Row.y, LeadingWidth, Row.h};
				// 缝紧贴在前置区右侧：主体从「前置右缘 + 缝」开始，而不是把缝吞进主体宽度。
				const float Gap = Row.w > LeadingWidth ? std::min(Options.m_RowGap, Row.w - LeadingWidth) : 0.0f;
				Row.x += LeadingWidth + Gap;
				Row.w = std::max(0.0f, Row.w - LeadingWidth - Gap);
			}
			if(Options.m_TrailingSlotWidth > 0.0f)
			{
				const float SlotWidth = std::min(Options.m_TrailingSlotWidth, std::max(0.0f, Row.w));
				Layout.m_TrailingSlot = CUIRect{Row.x + Row.w - SlotWidth, Row.y, SlotWidth, Row.h};
				const float Gap = Row.w > SlotWidth ? std::min(Options.m_RowGap, Row.w - SlotWidth) : 0.0f;
				Row.w = std::max(0.0f, Row.w - SlotWidth - Gap);
			}
			// ROW 形态没有独立的头部/底栏：主体占满剩余高度，竖向切分交给调用点
			// （皮肤格在上面切一行图标条，资源行可能切标题/副标题）。
			Layout.m_Body = CUIRect{Row.x, Row.y, Row.w, Row.h};
			return Layout;
		}
	} // namespace detail

	// 纯几何：同一份入参在测量趟与绘制趟必须得到同一结果，不再由调用点各自切。
	// Metrics 必须先经 Scaled()，这里的乘除关系只认缩放后的值。
	inline SLayout ResolveLayout(const CUIRect &Card, const SMetrics &Metrics, const SOptions &Options)
	{
		SLayout Layout{};
		Layout.m_Card = Card;
		Layout.m_Radius = Metrics.m_Radius;
		Layout.m_Content = CUIRect{
			Card.x + Metrics.m_Padding,
			Card.y + Metrics.m_Padding,
			std::max(0.0f, Card.w - Metrics.m_Padding * 2.0f),
			std::max(0.0f, Card.h - Metrics.m_Padding * 2.0f)};
		return Options.m_Axis == EAxis::ROW ? detail::ResolveRowLayout(Card, Metrics, Options, Layout) :
						      detail::ResolveStackLayout(Card, Metrics, Options, Layout);
	}

	// 卡片外壳：底色、常驻淡边、悬浮/选中白边全部走按钮表面管线，与工坊预设卡、
	// 资源页卡片的现有观感一致。返回卡片实际填充色，供调用点做文字对比。
	// 只在「本组件同时接管命中」的场合用；命中有外层列表时用 DrawStaticShell。
	inline ColorRGBA DrawShell(const IUiContext &Ctx, const void *pId, const SLayout &Layout, bool Selected = false, bool Enabled = true, const CUIRect *pHitRect = nullptr, const ColorRGBA *pFill = nullptr)
	{
		ui_widget::SButtonSurfaceOptions Options;
		// 卡片是独立视觉件：用 SECONDARY 保住常驻淡边，悬浮/选中再加统一白边。
		Options.m_Role = EUiButtonRole::SECONDARY;
		Options.m_Radius = Layout.m_Radius;
		Options.m_Selected = Selected;
		Options.m_Enabled = Enabled;
		Options.m_pHitRect = pHitRect != nullptr ? pHitRect : &Layout.m_Card;
		if(pFill != nullptr)
			Options.m_Color = *pFill;
		return ui_widget::DrawButtonSurface(Ctx, pId, Layout.m_Card, Options);
	}

	// 静态外壳：命中与悬浮反馈由外层列表（CListBox）负责的卡片用它，只画几何与填充，
	// 不登记交互、不抢命中。圆角与内边距仍来自同一份 Layout。
	inline void DrawStaticShell(CUi *pUi, const SLayout &Layout, const ColorRGBA &Fill, const ColorRGBA &Border = ColorRGBA())
	{
		if(pUi == nullptr)
			return;
		DrawRoundedSurface(pUi, Layout.m_Card, Fill, Border, Layout.m_Radius);
	}
} // namespace qm_list_card

#endif
