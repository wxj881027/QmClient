#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PREVIEW_PANEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PREVIEW_PANEL_H

#include <engine/map.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/components/qmclient/browser_map_preview.h>
#include <game/client/ui.h>
#include <game/localization.h>

class CGameClient;

// 服务器列表与投票页共用的地图预览面板：同一份离屏地图缓存、同一套相机
// 拖拽/滚轮缩放和实体层开关。调用点只决定目标地图、标题右侧说明，以及
// 本地缺图时是否允许自动下载。
//
// 外壳（图标入口、二级面板、标题、关闭、工具行、提示）留在本头文件，
// 便于 qm-ui-popup-tests 直接驱动开关闭环；离屏渲染与缓存由生产侧
// Init() 注入，测试不需要链接整个客户端。
class CQmMapPreviewPanel
{
public:
	struct STarget
	{
		char m_aMapName[MAX_MAP_LENGTH] = {};
		char m_aCaption[192] = {};
		// 投票页缺图时自动下载；服务器列表只读本地已缓存地图，不联网。
		bool m_AllowDownload = false;
	};

	// 每个入口一份：标题、目标地图与相机状态互不串用；缓存仍由面板共享。
	struct SContext : public SPopupMenuId
	{
		CUi *m_pUi = nullptr;
		CQmMapPreviewPanel *m_pPanel = nullptr;
		STarget m_Target;
		CQmBrowserMapPreviewView m_View;
		bool m_InitialViewSet = false;
		int m_RetryButton = 0;
	};

	using FDrawCanvas = void (*)(void *pRenderState, CGameClient *pGameClient, SContext *pContext, CUIRect Preview, CUi *pUi, bool Active);

	CQmMapPreviewPanel() = default;
	~CQmMapPreviewPanel() = default;
	CQmMapPreviewPanel(const CQmMapPreviewPanel &) = delete;
	CQmMapPreviewPanel &operator=(const CQmMapPreviewPanel &) = delete;

	// 生产侧注入离屏渲染与缓存；图形接口存活期间由 CMenus::OnShutdown 释放。
	void Init(CGameClient *pGameClient);
	void Shutdown();

	bool DoIconButton(const IUiContext &Ctx, CButtonContainer *pButton, SContext *pContext, const CUIRect &Rect, const STarget &Target, bool Enabled)
	{
		if(Ctx.m_pUi == nullptr || pButton == nullptr || pContext == nullptr)
			return false;
		if(!ui_widget::IconButton(Ctx, pButton, EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Rect, !Enabled))
			return false;
		if(!Enabled)
			return false;
		Open(Ctx, pContext, Target);
		return true;
	}

	void Open(const IUiContext &Ctx, SContext *pContext, const STarget &Target)
	{
		if(Ctx.m_pUi == nullptr || pContext == nullptr || Target.m_aMapName[0] == '\0')
			return;
		if(Ctx.m_pUi->IsPopupOpen(pContext))
			return;
		if(str_comp(pContext->m_Target.m_aMapName, Target.m_aMapName) != 0)
		{
			pContext->m_View = CQmBrowserMapPreviewView{};
			pContext->m_InitialViewSet = false;
		}
		pContext->m_pUi = Ctx.m_pUi;
		pContext->m_pPanel = this;
		pContext->m_Target = Target;

		const SPopupMenuProperties Props = ui_widget::SecondaryPanelProperties();
		const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*Ctx.m_pUi->Screen());
		Ctx.m_pUi->DoPopupMenu(pContext, PanelRect.x, PanelRect.y, PanelRect.w, PanelRect.h, pContext, &CQmMapPreviewPanel::PopupThunk, Props);
	}

	CUi::EPopupMenuFunctionResult Popup(void *pContext, CUIRect View, bool Active) const
	{
		auto *pCtx = static_cast<SContext *>(pContext);
		if(pCtx == nullptr || pCtx->m_pUi == nullptr || pCtx->m_Target.m_aMapName[0] == '\0')
			return CUi::POPUP_CLOSE_CURRENT;
		CUi *pUi = pCtx->m_pUi;

		IUiContext PanelCtx;
		PanelCtx.m_pUi = pUi;
		static ui_widget::SSecondaryPanelLabel s_Title;
		static ui_widget::SSecondaryPanelLabel s_Caption;
		static ui_widget::SSecondaryPanelLabel s_EntitiesLabel;
		static CButtonContainer s_CloseButton;
		ui_widget::CSecondaryPanel Panel(PanelCtx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w), {});
		if(Panel.Header(s_Title, s_CloseButton, pCtx->m_Target.m_aMapName, &s_Caption, pCtx->m_Target.m_aCaption))
			return CUi::POPUP_CLOSE_CURRENT;
		Panel.ToggleRow(s_EntitiesLabel, Localize("Show entities only (hide textures)"), g_Config.m_QmMapPreviewEntityOnly);
		CUIRect Preview = Panel.ContentRect();
		const auto Metrics = ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w);
		Preview.HSplitTop(Metrics.m_Spacing, nullptr, &Preview);
		CUIRect Hint;
		Preview.HSplitBottom(Metrics.m_RowHeight, &Preview, &Hint);
		Preview.HSplitBottom(Metrics.m_Spacing, &Preview, nullptr);
		SLabelProperties HintProps;
		HintProps.m_MaxWidth = Hint.w;
		HintProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Hint, Localize("Drag to pan, scroll to zoom"), Metrics.m_FontSize, TEXTALIGN_ML, HintProps);
		if(Preview.w <= 2.0f || Preview.h <= 2.0f)
			return CUi::POPUP_KEEP_OPEN;
		if(m_pfnDrawCanvas == nullptr || m_pGameClient == nullptr)
		{
			pUi->DoLabel(&Preview, Localize("Map preview unavailable"), Metrics.m_FontSize, TEXTALIGN_MC);
			return CUi::POPUP_KEEP_OPEN;
		}
		m_pfnDrawCanvas(m_pRenderState, m_pGameClient, pCtx, Preview, pUi, Active);
		return CUi::POPUP_KEEP_OPEN;
	}

private:
	static CUi::EPopupMenuFunctionResult PopupThunk(void *pUser, CUIRect View, bool Active)
	{
		auto *pCtx = static_cast<SContext *>(pUser);
		return pCtx->m_pPanel->Popup(pUser, View, Active);
	}

	CGameClient *m_pGameClient = nullptr;
	void *m_pRenderState = nullptr;
	FDrawCanvas m_pfnDrawCanvas = nullptr;
};

#endif