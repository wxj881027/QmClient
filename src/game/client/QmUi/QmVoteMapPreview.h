#ifndef GAME_CLIENT_QMUI_QMVOTEMAPPREVIEW_H
#define GAME_CLIENT_QMUI_QMVOTEMAPPREVIEW_H

#include "SecondaryPanel.h"
#include "UiButtons.h"
#include "UiContext.h"

#include <base/str.h>

#include <engine/textrender.h>

#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <string>

// 投票页「预览地图」二级面板：入口按钮、面板骨架与内容排版只有这一份实现。
// 外壳复用 ui_widget::CSecondaryPanel（与服务器列表、设置页的二级面板同一套骨架），
// 封面缩略图由调用点注入绘制回调画进 Layout.m_Cover——本头文件不申请纹理、不访问
// GameClient，因此可以被真实 UI fixture 直接驱动做开关闭环测试。
namespace qm_vote_map_preview
{
	// 面板内容区逐行列出的事实：地图名进标题，作者/难度/分数在内容区。
	// 顺序即绘制顺序；COUNT 只作为数组长度，不绘制。
	enum class EFact
	{
		MAPPER,
		DIFFICULTY,
		POINTS,
		COUNT,
	};

	// 并排模式下封面宽度占内容宽度的上限：再宽事实行就没有位置。
	inline constexpr float SIDE_COVER_WIDTH_RATIO = 0.5f;
	// 打开面板时拷贝的一帧地图信息；切换目标后旧内容不会残留在面板里。
	struct SInfo
	{
		std::string m_Name;
		std::string m_Mapper;
		std::string m_Category;
		// 官方封面地址；服务器投票选项合并出来的条目可能为空，此时不画封面。
		std::string m_Thumbnail;
		int m_Stars = -1;
		int m_Points = -1;
	};

	// 预览目标：地图库页取用户选中的那张图，其余子页取当前正在玩的地图。
	// 两者都为空时按钮置灰（没有可预览的对象），不打开空面板。
	inline const char *TargetName(const char *pSelectedName, const char *pCurrentMap)
	{
		if(pSelectedName != nullptr && pSelectedName[0] != '\0')
			return pSelectedName;
		return pCurrentMap != nullptr ? pCurrentMap : "";
	}

	// 事实行文字；数据缺失时给破折号占位，不整行消失（行数固定，面板高度不跳）。
	inline void FormatFact(const EFact Fact, const SInfo &Info, char *pBuf, const int BufSize)
	{
		if(pBuf == nullptr || BufSize <= 0)
			return;
		pBuf[0] = '\0';
		switch(Fact)
		{
		case EFact::MAPPER:
			str_format(pBuf, BufSize, "%s: %s", Localize("Mapper"), Info.m_Mapper.empty() ? "—" : Info.m_Mapper.c_str());
			break;
		case EFact::DIFFICULTY:
			if(Info.m_Stars >= 0)
				str_format(pBuf, BufSize, "%s: %d / 5", Localize("Difficulty"), Info.m_Stars);
			else
				str_format(pBuf, BufSize, "%s: —", Localize("Difficulty"));
			break;
		case EFact::POINTS:
			if(Info.m_Points >= 0)
				str_format(pBuf, BufSize, "%s: %d", Localize("Points"), Info.m_Points);
			else
				str_format(pBuf, BufSize, "%s: —", Localize("Points"));
			break;
		case EFact::COUNT:
			break;
		}
	}

	// 内容区排版：封面按给定长宽比贴左，右侧逐行排事实。
	// 面板窄到事实区放不下「标签: 值」一行时改为封面在上、事实在下，两者都不越界；
	// 高度不足时只登记完整放得下的行，不留半行（否则文字会被下一行压住）。
	struct SLayout
	{
		CUIRect m_Cover{};
		CUIRect m_Facts{};
		CUIRect m_aFactRows[static_cast<int>(EFact::COUNT)]{};
		int m_NumFactRows = 0;
		bool m_Stacked = false;
	};

	inline SLayout ResolveLayout(const CUIRect &Content, const float CoverAspect, const float RowHeight)
	{
		SLayout Layout;
		const int NumFacts = static_cast<int>(EFact::COUNT);
		const float Width = std::max(0.0f, Content.w);
		const float Height = std::max(0.0f, Content.h);
		const float Row = std::max(0.0f, RowHeight);
		if(Width <= 0.0f || Height <= 0.0f || Row <= 0.0f)
			return Layout;
		const float Gap = ui_token::secondary_panel::GAP;
		const float Aspect = std::max(CoverAspect, 0.01f);
		const float FactsHeight = Row * static_cast<float>(NumFacts);
		// 并排模式事实行的最小宽度：两档页面内边距 + 一行行高，再窄就只能堆叠。
		const float MinFactsWidth = ui_token::spacing::LG * 2.0f + Row;
		// 封面最多占一半宽度，另一半留给事实行；高度不够时按可用高度反推宽度。
		const float SideCoverWidth = std::min(Width * SIDE_COVER_WIDTH_RATIO, Height * Aspect);
		if(Width - SideCoverWidth - Gap < MinFactsWidth)
		{
			// 堆叠：封面在上、事实在下。封面同时受剩余高度与内容宽度约束，
			// 两个方向都夹一次，长宽比与事实行都不会被挤掉。
			Layout.m_Stacked = true;
			const float CoverHeight = std::clamp(std::min(Height - FactsHeight - Gap, Width / Aspect), 0.0f, Height);
			const float CoverWidth = std::min(Width, CoverHeight * Aspect);
			Layout.m_Cover = {Content.x, Content.y, CoverWidth, CoverHeight};
			const float FactsTop = Content.y + CoverHeight + Gap;
			Layout.m_Facts = {Content.x, FactsTop, Width, std::max(0.0f, std::clamp(Content.y + Height - FactsTop, 0.0f, FactsHeight))};
		}
		else
		{
			Layout.m_Cover = {Content.x, Content.y, SideCoverWidth, std::min(Height, SideCoverWidth / Aspect)};
			Layout.m_Facts = {Content.x + SideCoverWidth + Gap, Content.y, std::max(0.0f, Width - SideCoverWidth - Gap), std::min(FactsHeight, Height)};
		}
		// 事实行按顺序切分；只认完整的一行，留半行会把文字压到下一条事实上。
		// NumFactRows 因此就是实际可绘制行数。
		CUIRect Facts = Layout.m_Facts;
		for(int Index = 0; Index < NumFacts; ++Index)
		{
			const float Remaining = std::max(0.0f, Facts.h);
			if(Remaining + 0.001f < Row)
				break;
			Layout.m_aFactRows[Index] = {Facts.x, Facts.y, Facts.w, Row};
			++Layout.m_NumFactRows;
			Facts.y += Row;
			Facts.h = Remaining - Row;
		}
		return Layout;
	}

	// 内容绘制回调：只画封面缩略图（纹理切换与项目提交），文字排版在面板内部完成。
	using FDrawContent = void (*)(CUi *pUi, const SLayout &Layout, void *pUser);

	struct SContext
	{
		// 面板骨架与文字要用 UI；弹层回调拿不到调用栈，因此随上下文持有。
		CUi *m_pUi = nullptr;
		SInfo m_Info;
		// 封面长宽比（宽/高）：由调用点按缩略图像素尺寸给出，本文件不写死比例。
		float m_CoverAspect = 1.0f;
		FDrawContent m_pfnDrawContent = nullptr;
		void *m_pUser = nullptr;
	};

	// 面板内容：标题（地图名 + 右侧分类小字）、关闭按钮、封面与三条事实。
	inline CUi::EPopupMenuFunctionResult Popup(void *pContext, CUIRect View, bool Active)
	{
		auto *pCtx = static_cast<SContext *>(pContext);
		if(pCtx == nullptr || pCtx->m_pUi == nullptr || pCtx->m_Info.m_Name.empty())
			return CUi::POPUP_CLOSE_CURRENT;
		CUi *pUi = pCtx->m_pUi;
		IUiContext PanelCtx;
		PanelCtx.m_pUi = pUi;
		const ui_widget::SSecondaryPanelMetrics Metrics = ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w);
		ui_widget::CSecondaryPanel Panel(PanelCtx, View, Active, Metrics, {});
		static ui_widget::SSecondaryPanelLabel s_Title;
		static ui_widget::SSecondaryPanelLabel s_Caption;
		static CButtonContainer s_CloseButton;
		// 分类放在标题右侧小字：与服务器列表预览面板同一种「标题 + 右侧小字」读法。
		if(Panel.Header(s_Title, s_CloseButton, pCtx->m_Info.m_Name.c_str(), &s_Caption, pCtx->m_Info.m_Category.empty() ? Localize("Unknown") : pCtx->m_Info.m_Category.c_str()))
			return CUi::POPUP_CLOSE_CURRENT;
		const CUIRect Content = Panel.ContentRect();
		const SLayout Layout = ResolveLayout(Content, pCtx->m_CoverAspect, Metrics.m_RowHeight);
		if(pCtx->m_pfnDrawContent != nullptr)
			pCtx->m_pfnDrawContent(pUi, Layout, pCtx->m_pUser);
		char aFact[192];
		for(int Index = 0; Index < Layout.m_NumFactRows; ++Index)
		{
			FormatFact(static_cast<EFact>(Index), pCtx->m_Info, aFact, sizeof(aFact));
			SLabelProperties Props;
			Props.m_MaxWidth = Layout.m_aFactRows[Index].w;
			Props.m_EllipsisAtEnd = true;
			pUi->DoLabel(&Layout.m_aFactRows[Index], aFact, Metrics.m_FontSize, TEXTALIGN_ML, Props);
		}
		return CUi::POPUP_KEEP_OPEN;
	}

	// 入口按钮：投票页所有子页共用的方形图标按钮（眼睛 = 预览，与别处语义一致）。
	// 返回本帧是否刚打开面板；面板已打开时保持现状，不重复入栈。
	inline bool DoButton(const IUiContext &Ctx, CButtonContainer *pButton, SPopupMenuId *pPopupId, SContext *pContext, const CUIRect &Rect, const bool Enabled)
	{
		if(Ctx.m_pUi == nullptr || pButton == nullptr || pPopupId == nullptr || pContext == nullptr)
			return false;
		if(!ui_widget::IconButton(Ctx, pButton, EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Rect, !Enabled))
			return false;
		CUi *pUi = Ctx.m_pUi;
		pContext->m_pUi = pUi;
		if(pUi->IsPopupOpen(pPopupId))
			return false;
		const SPopupMenuProperties Props = ui_widget::SecondaryPanelProperties();
		const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*pUi->Screen());
		pUi->DoPopupMenu(pPopupId, PanelRect.x, PanelRect.y, PanelRect.w, PanelRect.h, pContext, Popup, Props);
		return true;
	}
} // namespace qm_vote_map_preview

#endif
