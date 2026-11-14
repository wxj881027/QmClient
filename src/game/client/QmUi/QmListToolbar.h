#ifndef GAME_CLIENT_QMUI_QMLISTTOOLBAR_H
#define GAME_CLIENT_QMUI_QMLISTTOOLBAR_H

#include "UiContext.h"
#include "UiForms.h"

#include <game/client/qm_icon.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <algorithm>
#include <optional>

class CButtonContainer;
class CLineInput;

// 列表页顶部工具栏：搜索框占满剩余宽度，右侧依次是圆形刷新与目录按钮。
// 影子页与 Rank 1 页共用同一份几何与绘制，避免两处各自维护一套间距。
namespace ui_widget
{
	struct SListToolbarLayout
	{
		CUIRect m_SearchRect{};
		CUIRect m_RefreshRect{};
		CUIRect m_FolderRect{};
		float m_Height = 0.0f;
	};

	// 纯几何：任意宽度下都不产生负宽度，窄到放不下时按钮依次让位给搜索框。
	inline SListToolbarLayout LayoutListToolbar(const CUIRect &Row, float FontSize = ui_token::font::BODY)
	{
		SListToolbarLayout Layout;
		Layout.m_Height = std::max(0.0f, FontSize + 2.0f * ui_token::spacing::SM);
		CUIRect Remaining = Row;
		Remaining.h = std::min(std::max(0.0f, Remaining.h), Layout.m_Height);

		const auto CutRight = [](CUIRect &Rest, float Amount) {
			CUIRect Out;
			const float Cut = std::min(std::max(0.0f, Amount), std::max(0.0f, Rest.w));
			Rest.VSplitRight(Cut, &Rest, &Out);
			return Out;
		};

		Layout.m_FolderRect = CutRight(Remaining, Layout.m_Height);
		Remaining.VSplitRight(std::min(ui_token::spacing::XS, std::max(0.0f, Remaining.w)), &Remaining, nullptr);
		Layout.m_RefreshRect = CutRight(Remaining, Layout.m_Height);
		Remaining.VSplitRight(std::min(ui_token::spacing::XS, std::max(0.0f, Remaining.w)), &Remaining, nullptr);
		Layout.m_SearchRect = Remaining;
		return Layout;
	}

	struct SListToolbarOptions
	{
		const char *m_pPlaceholder = nullptr;
		float m_FontSize = ui_token::font::BODY;
		CButtonContainer *m_pRefreshButton = nullptr;
		CButtonContainer *m_pFolderButton = nullptr;
	};

	struct SListToolbarResult
	{
		SListToolbarLayout m_Layout{};
		bool m_SearchChanged = false;
		bool m_RefreshPressed = false;
		bool m_FolderPressed = false;
	};

	// 绘制工具栏并从 View 顶部切走工具栏与下方间距。工具提示由调用方用
	// m_Layout 的按钮矩形和 m_pRefreshButton / m_pFolderButton 自行绘制。
	inline SListToolbarResult ListToolbar(const IUiContext &Ctx, CLineInput *pSearchInput, CUIRect &View, const SListToolbarOptions &Options)
	{
		SListToolbarResult Result;
		CUIRect Toolbar;
		const float Height = std::min(std::max(0.0f, View.h), Options.m_FontSize + 2.0f * ui_token::spacing::SM);
		View.HSplitTop(Height, &Toolbar, &View);
		View.HSplitTop(std::min(ui_token::spacing::SM, std::max(0.0f, View.h)), nullptr, &View);

		Result.m_Layout = LayoutListToolbar(Toolbar, Options.m_FontSize);

		SInputFieldOptions SearchOptions;
		SearchOptions.m_pPlaceholder = Options.m_pPlaceholder;
		SearchOptions.m_FontSize = Options.m_FontSize;
		SearchOptions.m_Mode = EInputFieldMode::SEARCH;
		SearchOptions.m_Clearable = true;
		Result.m_SearchChanged = InputField(Ctx, pSearchInput, Result.m_Layout.m_SearchRect, SearchOptions).m_Changed;

		if(Ctx.m_pUi == nullptr)
			return Result;
		Result.m_RefreshPressed = Ctx.m_pUi->DoButton_QmIcon(Options.m_pRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0,
			&Result.m_Layout.m_RefreshRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true, std::nullopt, false, false, nullptr,
			Result.m_Layout.m_RefreshRect.h * 0.5f) != 0;
		Result.m_FolderPressed = Ctx.m_pUi->DoButton_QmIcon(Options.m_pFolderButton, EQmIcon::FOLDER, FontIcons::FONT_ICON_FOLDER, 0,
			&Result.m_Layout.m_FolderRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true, std::nullopt, false, false, nullptr,
			Result.m_Layout.m_FolderRect.h * 0.5f) != 0;
		return Result;
	}
} // namespace ui_widget

#endif
