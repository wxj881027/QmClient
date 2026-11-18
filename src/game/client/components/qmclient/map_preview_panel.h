#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PREVIEW_PANEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PREVIEW_PANEL_H

#include <engine/map.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/components/qmclient/browser_map_preview.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

class CGameClient;

// 只管理本面板的逐层准备失败；map、layers 和已上传图片继续由原缓存项持有。
class CQmMapPreviewLayerPreparation
{
	bool m_Failed = false;
	float m_RetryAfterTime = 0.0f;

	template<typename TRenderer>
	static bool ReleaseRenderer(std::unique_ptr<TRenderer> &pRenderer)
	{
		if(pRenderer == nullptr)
			return true;
		try
		{
			// Clear 先销毁失败协程；GPU 删除失败时，图层和 BO 身份仍留在原 owner。
			pRenderer->UnloadMap();
		}
		catch(const std::bad_alloc &) { return false; }
		catch(const std::length_error &) { return false; }
		pRenderer.reset();
		return true;
	}

public:
	static constexpr float RETRY_DELAY = 2.0f;
	bool HasFailed() const { return m_Failed; }

	template<typename TRenderer>
	bool Advance(std::unique_ptr<TRenderer> &pRenderer, float Now)
	{
		if(m_Failed || pRenderer == nullptr)
			return false;
		try
		{
			return pRenderer->AdvanceMapLoad(1);
		}
		catch(const std::bad_alloc &) {}
		catch(const std::length_error &) {}
		m_Failed = true;
		m_RetryAfterTime = Now + RETRY_DELAY;
		ReleaseRenderer(pRenderer);
		return false;
	}

	template<typename TRenderer, typename TCreateRenderer>
	void Retry(std::unique_ptr<TRenderer> &pRenderer, float Now, TCreateRenderer &&CreateRenderer)
	{
		if(!m_Failed || Now < m_RetryAfterTime)
			return;
		m_RetryAfterTime = Now + RETRY_DELAY;
		if(!ReleaseRenderer(pRenderer))
			return;
		try
		{
			// 候选只初始化加载状态，不上传图层；完整缓存项与图片来源不参与替换。
			auto pCandidate = CreateRenderer();
			if(pCandidate == nullptr)
				return;
			pRenderer = std::move(pCandidate);
			m_Failed = false;
		}
		catch(const std::bad_alloc &) {}
		catch(const std::length_error &) {}
	}
};

// 缓存项进入回收后不再绘制；删除命令失败时，所有来源与 GPU 身份仍由原项持有。
// 模板只隔离资源接口，生产缓存和真实 renderer 的回归共用这条清理、腾位入口。
template<typename TEntry>
class CQmMapPreviewCacheEntries
{
	IGraphics *m_pGraphics;
	std::vector<std::unique_ptr<TEntry>> m_vEntries;

public:
	static constexpr size_t CAPACITY = 3;
	static constexpr float MIN_AGE = 0.4f;

	explicit CQmMapPreviewCacheEntries(IGraphics *pGraphics) :
		m_pGraphics(pGraphics)
	{
		m_vEntries.reserve(CAPACITY);
	}
	~CQmMapPreviewCacheEntries()
	{
		// 显式 Clear 负责图形清理；终态析构只取消任务并释放 CPU owner。
		for(auto &pEntry : m_vEntries)
			pEntry->CancelLoad();
	}

	size_t Size() const { return m_vEntries.size(); }
	TEntry *Find(const char *pMapName) const
	{
		for(const auto &pEntry : m_vEntries)
			if(str_comp(pEntry->m_aMapName, pMapName) == 0)
				return pEntry.get();
		return nullptr;
	}
	static bool IsInitializing(const TEntry *pEntry) { return pEntry == nullptr || pEntry->m_Retiring; }

	bool Release(TEntry &Entry)
	{
		Entry.m_Retiring = true;
		Entry.CancelLoad();
		try
		{
			if(m_pGraphics != nullptr && Entry.m_GraphicsResetVersion == m_pGraphics->GraphicsResourcesResetVersion())
			{
				// renderer 借用地图和图片；先收回图层，再删除图片和离屏目标。
				if(Entry.m_pRenderer != nullptr)
					Entry.m_pRenderer->UnloadMap();
				if(Entry.m_pImages != nullptr)
					Entry.m_pImages->UnloadPreview();
				if(Entry.m_RenderTarget.IsValid())
					m_pGraphics->DestroyRenderTarget(&Entry.m_RenderTarget);
			}
			else
			{
				// 旧代 GPU 已整体失效；先销毁协程借用，再关闭地图，不向新代发送删除。
				Entry.m_pRenderer.reset();
				Entry.m_RenderTarget.Invalidate();
			}
			if(Entry.m_pMap != nullptr)
				Entry.m_pMap->Unload();
			return true;
		}
		catch(const std::bad_alloc &) { return false; }
		catch(const std::length_error &) { return false; }
	}

	bool Clear()
	{
		for(auto It = m_vEntries.begin(); It != m_vEntries.end();)
		{
			if(Release(**It))
				It = m_vEntries.erase(It);
			else
				++It;
		}
		return m_vEntries.empty();
	}

	bool MakeRoom(float Now)
	{
		if(m_vEntries.size() < CAPACITY)
			return true;
		// Touch 和 Insert 维持最近使用顺序；失败项留在原位，继续尝试其他可淘汰项。
		for(size_t Index = m_vEntries.size(); Index > 0; --Index)
		{
			auto &pEntry = m_vEntries[Index - 1];
			if(Now - pEntry->m_LastUseTime < MIN_AGE || !Release(*pEntry))
				continue;
			m_vEntries.erase(m_vEntries.begin() + Index - 1);
			return true;
		}
		return false;
	}

	void Touch(TEntry &Entry, float Now)
	{
		Entry.m_LastUseTime = Now;
		const auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &pEntry) { return pEntry.get() == &Entry; });
		dbg_assert(It != m_vEntries.end(), "Preview cache entry must be owned before use");
		std::rotate(m_vEntries.begin(), It, It + 1);
	}
	TEntry *Insert(std::unique_ptr<TEntry> pEntry)
	{
		dbg_assert(m_vEntries.size() < CAPACITY, "Preview cache must make room before insertion");
		auto *pResult = pEntry.get();
		m_vEntries.insert(m_vEntries.begin(), std::move(pEntry));
		return pResult;
	}
	void RetryMissing(const char *pMapName)
	{
		auto *pEntry = Find(pMapName);
		if(pEntry != nullptr && pEntry->m_LoadFailed && !pEntry->m_Retiring)
			pEntry->m_RetryAfterTime = 0.0f;
	}
};

// 预览绘制临时借用投影与裁剪；离屏目标只在 Begin 成功后由本作用域结束。
class CQmMapPreviewDrawScope
{
	IGraphics &m_Graphics;
	std::array<float, 4> m_aScreen;
	CUiScopedClipSuspend m_Clips;
	bool m_RenderTargetActive = false;

public:
	explicit CQmMapPreviewDrawScope(CUi &Ui) :
		m_Graphics(*Ui.Graphics()), m_Clips(&Ui)
	{
		m_Graphics.GetScreen(&m_aScreen[0], &m_aScreen[1], &m_aScreen[2], &m_aScreen[3]);
	}
	~CQmMapPreviewDrawScope()
	{
		EndRenderTarget();
		m_Graphics.MapScreen(m_aScreen[0], m_aScreen[1], m_aScreen[2], m_aScreen[3]);
	}
	CQmMapPreviewDrawScope(const CQmMapPreviewDrawScope &) = delete;
	CQmMapPreviewDrawScope &operator=(const CQmMapPreviewDrawScope &) = delete;

	bool BeginRenderTarget(IGraphics::CRenderTargetHandle Target, ColorRGBA ClearColor)
	{
		dbg_assert(!m_RenderTargetActive, "Preview render target already active");
		m_RenderTargetActive = m_Graphics.BeginRenderTarget(Target, ClearColor);
		return m_RenderTargetActive;
	}
	void EndRenderTarget()
	{
		if(m_RenderTargetActive)
		{
			m_Graphics.EndRenderTarget();
			m_RenderTargetActive = false;
		}
	}
};

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
	~CQmMapPreviewPanel()
	{
		if(m_pfnDestroyRenderState != nullptr)
			m_pfnDestroyRenderState(m_pRenderState);
	}
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

	CUi::EPopupMenuFunctionResult Popup(void *pContext, CUIRect View, bool Active)
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
		// Init 的生产回调只在资源准备失败时存在；外壳测试无需链接客户端实现。
		if(m_pfnRetryInit != nullptr)
			m_pfnRetryInit(this);
		if(m_pInitRetryGameClient != nullptr || m_pfnDrawCanvas == nullptr || m_pGameClient == nullptr)
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
	CGameClient *m_pInitRetryGameClient = nullptr;
	void (*m_pfnRetryInit)(CQmMapPreviewPanel *) = nullptr;
	void *m_pRenderState = nullptr;
	void (*m_pfnDestroyRenderState)(void *) noexcept = nullptr;
	FDrawCanvas m_pfnDrawCanvas = nullptr;
};

#endif