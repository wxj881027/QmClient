#include "map_preview_panel.h"

#include <base/perf_timer.h>

#include <engine/client/client.h>
#include <engine/client/map_download_resolver.h>
#include <engine/gfx/image_loader.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/background.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/qmclient/browser_map_preview_loader.h>
#include <game/client/components/qmclient/dummy_mini_view_layout.h>
#include <game/client/components/qmclient/map_download_index.h>
#include <game/client/components/qmclient/map_preview_panel.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/layers.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	// 失败条目的重查间隔：投票页下载完成后，占位应自行换成预览。
	constexpr float MAP_PREVIEW_FAILED_RETRY_DELAY = CQmMapPreviewLayerPreparation::RETRY_DELAY;

	// 地图贴图属于缓存项；实体和数字图集复用客户端资源，避免预览重复加载、
	// 丢失玩家实体包配置，或逐出地图后遗留实体纹理。
	class CMapPreviewImages : public CMapImages
	{
		std::vector<IGraphics::CTextureHandle> m_vTextures;
		CMapImages &m_Shared;

	public:
		explicit CMapPreviewImages(CMapImages &Shared) :
			m_Shared(Shared) {}
		IGraphics::CTextureHandle Get(int Index) const override { return m_vTextures[Index]; }
		int Num() const override { return m_vTextures.size(); }
		void Resize(int Count) { m_vTextures.resize(Count); }
		bool Upload(int Index, const CImageInfo &Image, int Usage)
		{
			// 未使用或解码失败的图片沿原规则跳过；有像素的上传拒绝必须重试。
			if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
				return true;
			const int ArrayFlag = Graphics()->TextureLoadFlags();
			const int Flags = ((Usage & 1) ? ArrayFlag : 0) | ((Usage & 2) ? 0 : (Graphics()->HasTextureArraysSupport() ? IGraphics::TEXLOAD_NO_2D_TEXTURE : 0));
			const auto Texture = Graphics()->LoadTextureRaw(Image, Flags, "map-preview");
			if(!Texture.IsValid())
				return false;
			m_vTextures[Index] = Texture;
			return true;
		}
		void UnloadPreview()
		{
			for(auto &Texture : m_vTextures)
				Graphics()->UnloadTexture(&Texture);
			m_vTextures.clear();
		}
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType Type) override { return m_Shared.GetEntities(Type); }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return m_Shared.GetSpeedupArrow(); }
		IGraphics::CTextureHandle GetTuneColors() override { return m_Shared.GetTuneColors(); }
		IGraphics::CTextureHandle GetOverlayBottom() override { return m_Shared.GetOverlayBottom(); }
		IGraphics::CTextureHandle GetOverlayTop() override { return m_Shared.GetOverlayTop(); }
		IGraphics::CTextureHandle GetOverlayCenter() override { return m_Shared.GetOverlayCenter(); }
	};

	// 离线渲染器（OnlineOnly=false）：菜单内直接 RenderCustomWithCamera。
	// SetEntityOnly：100 = 只渲染实体层（关贴图），0 = 正常完整渲染。
	class CMapPreviewLayers : public CMapLayers
	{
	public:
		CMapPreviewLayers() :
			CMapLayers(ERenderType::RENDERTYPE_FULL_DESIGN, false) {}
		void SetSources(CLayers *pLayers, CMapImages *pImages)
		{
			m_pLayers = pLayers;
			m_pImages = pImages;
		}
		void SetEntityOnly(bool EntityOnly)
		{
			m_EntityOverlayValOverride = EntityOnly ? 100 : 0;
		}
	};

	struct SMapPreviewEntry
	{
		char m_aMapName[MAX_MAP_LENGTH] = {0};
		std::unique_ptr<CMap> m_pMap;
		std::unique_ptr<CLayers> m_pLayers;
		std::unique_ptr<CMapPreviewImages> m_pImages;
		std::shared_ptr<CQmBrowserMapPreviewLoadJob> m_pLoadJob;
		std::weak_ptr<IHttpRequest> m_ValidationDownload;
		int m_UploadIndex = 0;
		std::unique_ptr<CMapPreviewLayers> m_pRenderer;
		CQmMapPreviewLayerPreparation m_LayerPreparation;
		IGraphics::CRenderTargetHandle m_RenderTarget;
		bool m_NeedsRender = true;
		vec2 m_RenderedCenter{};
		float m_RenderedZoom = 0.0f;
		bool m_RenderedEntities = false;
		unsigned m_RenderedBackgroundColor = 0;
		char m_aRenderedBackground[IO_MAX_PATH_LENGTH] = {};
		int m_TargetHeight = 0;
		int m_TargetWidth = 0;
		std::optional<vec2> m_Start;
		uint32_t m_GraphicsResetVersion = 0;
		vec2 m_WorldSize = vec2(32.0f * 64, 32.0f * 64);
		bool m_LoadFailed = false;
		bool m_Retiring = false;
		float m_LastUseTime = 0.0f;
		float m_RetryAfterTime = 0.0f;

		void CancelLoad() noexcept
		{
			if(m_pLoadJob != nullptr)
				m_pLoadJob->m_Cancelled.store(true);
		}
	};

	// 预览框里的下载进度：轨道 + 已完成比例 + 百分比文字。
	void DrawDownloadProgress(CGameClient *pGameClient, const CUIRect &Rect, int Progress)
	{
		CUi *pUi = pGameClient->Ui();
		if(pUi == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		const int Percent = std::clamp(Progress, 0, 100);
		CUIRect Track, Label;
		Rect.HSplitBottom(std::min(Rect.h, ui_token::font::BODY * 3.0f), &Label, nullptr);
		Label.HSplitTop(std::min(Label.h, ui_token::settings::ROW_HEIGHT * 0.5f), &Track, nullptr);
		Track.w = std::min(Track.w * 0.6f, 240.0f);
		Track.x = Rect.x + (Rect.w - Track.w) * 0.5f;
		DrawRoundedSurface(pUi, Track, ui_token::color::LIST_ITEM_HOVER, ColorRGBA(), Track.h * 0.5f);
		CUIRect Fill = Track;
		Fill.w *= Percent / 100.0f;
		if(Fill.w > 1.0f)
			DrawRoundedSurface(pUi, Fill, pUi->QmControlTheme().m_Accent, ColorRGBA(), Track.h * 0.5f);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Downloading map %d%%"), Percent);
		pUi->DoLabel(&Label, aBuf, ui_token::font::SMALL, TEXTALIGN_MC);
	}
} // namespace

class CQmMapPreviewCache
{
public:
	explicit CQmMapPreviewCache(IGraphics *pGraphics) :
		m_pGraphics(pGraphics), m_Entries(pGraphics) {}

	// 请求和取消状态独立于客户端，只在这里借用传输接口与本地路径。
	bool EnsureDownloaded(CGameClient *pGameClient, const char *pMapName)
	{
		auto *pClient = static_cast<CClient *>(pGameClient->Client());
		if(pClient == nullptr || pClient->Http() == nullptr)
			return false;
		return m_Download.Ensure(pMapName, g_Config.m_ClMapDownloadUrl, *pClient->Http(), pGameClient->Storage());
	}
	int DownloadProgress(const char *pMapName) const { return m_Download.Progress(pMapName); }
	bool DownloadFailed(const char *pMapName) const { return m_Download.HasFailed(pMapName); }
	void CancelDownload() { m_Download.Retry(); }
	void RecheckDownloaded(const char *pMapName)
	{
		if(m_Download.TakeCompletedDownload(pMapName))
			m_Entries.RetryMissing(pMapName);
	}
	bool Clear()
	{
		// Init 替换缓存也必须等旧文件请求结束，不能让两个 cache 写同一临时路径。
		const bool DownloadStopped = m_Download.Stop();
		const bool EntriesReleased = m_Entries.Clear();
		return DownloadStopped && EntriesReleased;
	}

	// 返回命中的缓存项；没有可回收槽位时返回空，由画布显示初始化占位。
	SMapPreviewEntry *Acquire(const char *pMapName, float NowTime, IStorage *pStorage, CGameClient *pGameClient)
	{
		if(auto *pEntry = m_Entries.Find(pMapName))
		{
			if(pEntry->m_Retiring || pEntry->m_GraphicsResetVersion != m_pGraphics->GraphicsResourcesResetVersion())
			{
				if(!m_Entries.Release(*pEntry))
					return pEntry;
				*pEntry = SMapPreviewEntry{};
				str_copy(pEntry->m_aMapName, pMapName);
				pEntry->m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
				LoadEntry(*pEntry, pStorage, pGameClient, NowTime);
			}
			pEntry->m_LayerPreparation.Retry(pEntry->m_pRenderer, NowTime, [&] {
				return CreateRenderer(pGameClient, pEntry->m_pLayers.get(), pEntry->m_pImages.get());
			});
			if(pEntry->m_LoadFailed && NowTime >= pEntry->m_RetryAfterTime)
			{
				pEntry->m_RetryAfterTime = NowTime + MAP_PREVIEW_FAILED_RETRY_DELAY;
				pEntry->m_LoadFailed = false;
				LoadEntry(*pEntry, pStorage, pGameClient, NowTime);
				pEntry->m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
			}
			m_Entries.Touch(*pEntry, NowTime);
			return pEntry;
		}

		if(!m_Entries.MakeRoom(NowTime))
			return nullptr;
		return m_Entries.Create([&](SMapPreviewEntry &Entry) {
			str_copy(Entry.m_aMapName, pMapName);
			Entry.m_LastUseTime = NowTime;
			LoadEntry(Entry, pStorage, pGameClient, NowTime);
			Entry.m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
		});
	}

	bool AdvanceEntry(SMapPreviewEntry &Entry, CGameClient *pGameClient, float Now)
	try
	{
		if(!Entry.m_pLoadJob)
			return Entry.m_pRenderer != nullptr;
		auto &Job = *Entry.m_pLoadJob;
		if(!Job.Finished())
			return false;
		const bool Loaded = Job.State() == IJob::STATE_DONE && Job.m_Success;
		m_Download.LocalLoadResult(Entry.m_ValidationDownload, Loaded);
		Entry.m_ValidationDownload.reset();
		if(!Loaded)
		{
			Entry.m_LoadFailed = true;
			Entry.m_RetryAfterTime = Now + MAP_PREVIEW_FAILED_RETRY_DELAY;
			Entry.m_pLoadJob.reset();
			return false;
		}
		if(!Entry.m_pImages)
		{
			auto pImages = std::make_unique<CMapPreviewImages>(pGameClient->m_MapImages);
			pImages->OnInterfacesInit(pGameClient);
			pImages->Resize(Job.m_vImages.size());
			Entry.m_pImages = std::move(pImages);
		}
		// 一帧最多上传一张贴图，不让高分辨率纹理集中复制并挤满图形命令缓冲。
		if(Entry.m_UploadIndex < static_cast<int>(Job.m_vImages.size()))
		{
			const CPerfTimer Timer(QmPerfEnabled());
			const int Index = Entry.m_UploadIndex;
			if(!Entry.m_pImages->Upload(Index, Job.m_vImages[Index], Job.m_vUsage[Index]))
				return false;
			Job.m_vImages[Index].Free();
			++Entry.m_UploadIndex;
			QmPerfLogStage("perf/map_preview", "upload", Timer.ElapsedMs(), false, pGameClient->Client());
			return false;
		}
		// OnMapLoad 在离线模式只 BeginLoad，尚未上传图层；失败候选可直接析构。
		// 先备好求值器和加载状态，再移走 job 的来源，避免下一帧读取半发布状态。
		auto pRenderer = CreateRenderer(pGameClient, Job.m_pLayers.get(), Entry.m_pImages.get());
		Entry.m_pMap = std::move(Job.m_pMap);
		Entry.m_Start = Job.m_Start;
		Entry.m_pLayers = std::move(Job.m_pLayers);
		Entry.m_pRenderer = std::move(pRenderer);

		// 整图范围以游戏层瓦片数为准；缺游戏层的地图退回 64x64 默认值。
		if(Entry.m_pLayers->GameLayer() != nullptr)
			Entry.m_WorldSize = vec2(Entry.m_pLayers->GameLayer()->m_Width * 32.0f, Entry.m_pLayers->GameLayer()->m_Height * 32.0f);
		Entry.m_NeedsRender = true;
		Entry.m_pLoadJob.reset();
		return false;
	}
	catch(const std::bad_alloc &)
	{
		// 保留完成的 job、当前图片和已成功上传的纹理，下帧继续当前准备阶段。
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}

private:
	static std::unique_ptr<CMapPreviewLayers> CreateRenderer(CGameClient *pGameClient, CLayers *pLayers, CMapPreviewImages *pImages)
	{
		auto pRenderer = std::make_unique<CMapPreviewLayers>();
		pRenderer->OnInterfacesInit(pGameClient);
		pRenderer->SetSources(pLayers, pImages);
		pRenderer->OnMapLoad();
		return pRenderer;
	}

	void LoadEntry(SMapPreviewEntry &Entry, IStorage *pStorage, CGameClient *pGameClient, float Now) noexcept
	{
		try
		{
			// 先记录下载终态，再提交 worker；不能给早已开始的旧任务补挂新下载身份。
			const auto ValidationDownload = m_Download.ValidationRequest(Entry.m_aMapName);
			auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(pStorage, Entry.m_aMapName, str_comp(pGameClient->Client()->GetCurrentMap(), Entry.m_aMapName) == 0 ? pGameClient->Client()->GetCurrentMapPath() : "", pGameClient->Client()->IsSixup());
			if(pGameClient->Engine()->TryAddJob(pJob))
			{
				Entry.m_pLoadJob = std::move(pJob);
				Entry.m_ValidationDownload = ValidationDownload;
				Entry.m_UploadIndex = 0;
				return;
			}
		}
		catch(...)
		{
			// 创建或提交失败沿预览现有退避重试。
		}
		Entry.m_LoadFailed = true;
		Entry.m_RetryAfterTime = Now + MAP_PREVIEW_FAILED_RETRY_DELAY;
	}

	IGraphics *m_pGraphics;
	CQmMapPreviewCacheEntries<SMapPreviewEntry> m_Entries;
	CQmMapPreviewDownloadState m_Download;
};

namespace
{
	struct SRenderer
	{
		std::unique_ptr<CQmMapPreviewCache> m_pCache;
	};

	void DrawCanvas(void *pRenderState, CGameClient *pGameClient, CQmMapPreviewPanel::SContext *pCtx, CUIRect Preview, CUi *pUi, bool Active)
	{
		auto *pRenderer = static_cast<SRenderer *>(pRenderState);
		if(pRenderer == nullptr || pRenderer->m_pCache == nullptr || pGameClient == nullptr || pCtx == nullptr || pUi == nullptr)
			return;
		CQmMapPreviewCache *pCache = pRenderer->m_pCache.get();

		// 预览框背板。
		CUiScopedGaussianBlurSuppression PreviewBlurSuppression(pUi);
		Preview.Draw(ColorRGBA(0.055f, 0.065f, 0.085f, 1.0f), IGraphics::CORNER_ALL, 6.0f);
		Preview.Margin(1.0f, &Preview);

		const float Now = pGameClient->Client()->GlobalTime();
		SMapPreviewEntry *pEntry = pCache->Acquire(pCtx->m_Target.m_aMapName, Now, pGameClient->Storage(), pGameClient);
		if(CQmMapPreviewCacheEntries<SMapPreviewEntry>::IsInitializing(pEntry) || pEntry->m_LayerPreparation.HasFailed())
		{
			pUi->DoLabel(&Preview, Localize("Initializing map layers"), 10.0f, TEXTALIGN_MC);
			return;
		}
		if(pEntry->m_LoadFailed)
		{
			if(pCtx->m_Target.m_AllowDownload)
			{
				if(pCache->DownloadFailed(pCtx->m_Target.m_aMapName))
				{
					if(pUi->DoButtonLogic(&pCtx->m_RetryButton, 0, &Preview, BUTTONFLAG_LEFT) != 0)
					{
						pCache->CancelDownload();
						pCache->EnsureDownloaded(pGameClient, pCtx->m_Target.m_aMapName);
					}
					pUi->DoLabel(&Preview, Localize("Map download failed, click to retry"), ui_token::font::SMALL, TEXTALIGN_MC);
					return;
				}
				if(pCache->EnsureDownloaded(pGameClient, pCtx->m_Target.m_aMapName))
				{
					DrawDownloadProgress(pGameClient, Preview, pCache->DownloadProgress(pCtx->m_Target.m_aMapName));
					pCache->RecheckDownloaded(pCtx->m_Target.m_aMapName);
				}
				else
					pUi->DoLabel(&Preview, Localize("Map not downloaded yet — join the server to preview it"), ui_token::font::SMALL, TEXTALIGN_MC);
				return;
			}
			// 骨架占位：未缓存不联网，进入服务器下载后才能预览。
			pUi->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			pUi->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			pUi->TextRender()->TextColor(ColorRGBA(0.55f, 0.58f, 0.62f, 1.0f));
			pUi->DoLabel_QmIcon(&Preview, EQmIcon::MAP, FontIcons::FONT_ICON_MAP, 26.0f, TEXTALIGN_MC);
			pUi->TextRender()->SetRenderFlags(0);
			pUi->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pUi->TextRender()->TextColor(pUi->TextRender()->DefaultTextColor());
			CUIRect HintRect;
			Preview.HSplitTop(Preview.h * 0.5f + 8.0f, nullptr, &HintRect);
			pUi->DoLabel(&HintRect, Localize("Map not downloaded yet — join the server to preview it"), 9.5f, TEXTALIGN_MC);
			return;
		}

		const CPerfTimer PrepareTimer(QmPerfEnabled());
		const bool Ready = pCache->AdvanceEntry(*pEntry, pGameClient, Now) && pEntry->m_LayerPreparation.Advance(pEntry->m_pRenderer, Now);
		QmPerfLogStage("perf/map_preview", "prepare", PrepareTimer.ElapsedMs(), false, pGameClient->Client());
		if(!Ready)
		{
			pUi->DoLabel(&Preview, Localize("Initializing map layers"), 10.0f, TEXTALIGN_MC);
			return;
		}

		// 使用引擎同一投影宽高比，画布内居中留边，避免拉伸。
		const float Aspect = pGameClient->Graphics()->GameScreenAspect();
		// 使用画布对应的物理像素，避免高分屏把固定纹理再次放大。
		const float CanvasWidth = std::min(Preview.w, Preview.h * Aspect);
		const int TargetWidth = std::clamp((int)std::ceil(CanvasWidth * pGameClient->Graphics()->ScreenWidth() / pUi->Screen()->w), 128, 4096);
		const int TargetHeight = maximum(32, (int)std::round(TargetWidth / Aspect));
		const uint32_t ResetVersion = pGameClient->Graphics()->GraphicsResourcesResetVersion();
		const bool Reset = pEntry->m_GraphicsResetVersion != ResetVersion;
		if(Reset)
			pEntry->m_RenderTarget.Invalidate();
		if(!pEntry->m_RenderTarget.IsValid() || pEntry->m_TargetWidth != TargetWidth || pEntry->m_TargetHeight != TargetHeight)
		{
			if(pEntry->m_RenderTarget.IsValid())
				pGameClient->Graphics()->DestroyRenderTarget(&pEntry->m_RenderTarget);
			pEntry->m_RenderTarget = pGameClient->Graphics()->CreateRenderTarget(TargetWidth, TargetHeight);
			pEntry->m_TargetHeight = TargetHeight;
			pEntry->m_TargetWidth = TargetWidth;
			pEntry->m_NeedsRender = true;
			pEntry->m_GraphicsResetVersion = ResetVersion;
		}
		if(!pEntry->m_RenderTarget.IsValid())
		{
			pUi->DoLabel(&Preview, Localize("Map preview rendering unavailable"), 10.0f, TEXTALIGN_MC);
			return;
		}

		float DrawW = Preview.w;
		float DrawH = DrawW / Aspect;
		if(DrawH > Preview.h)
		{
			DrawH = Preview.h;
			DrawW = DrawH * Aspect;
		}
		const CUIRect DrawRect(Preview.x + (Preview.w - DrawW) * 0.5f, Preview.y + (Preview.h - DrawH) * 0.5f, DrawW, DrawH);
		float BaseW, BaseH;
		pGameClient->Graphics()->CalcScreenParams(Aspect, 1.0f, &BaseW, &BaseH);
		auto &Camera = pCtx->m_View;
		Camera.Configure(pEntry->m_WorldSize, vec2(BaseW, BaseH));
		if(!pCtx->m_InitialViewSet)
		{
			if(pEntry->m_Start)
				Camera.FocusStart(*pEntry->m_Start);
			pCtx->m_InitialViewSet = true;
		}
		QmBrowserMapPreviewInput(*pUi, Camera, DrawRect, Active);
		const bool EntityOnly = g_Config.m_QmMapPreviewEntityOnly != 0;
		const bool DynamicBackground = EntityOnly && !IsDefaultBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities) && !IsBackgroundImageExtension(g_Config.m_ClBackgroundEntities);
		const bool ViewChanged = Camera.Center() != pEntry->m_RenderedCenter || Camera.Zoom() != pEntry->m_RenderedZoom || EntityOnly != pEntry->m_RenderedEntities ||
					 (EntityOnly && (pEntry->m_RenderedBackgroundColor != g_Config.m_ClBackgroundEntitiesColor || str_comp(pEntry->m_aRenderedBackground, g_Config.m_ClBackgroundEntities) != 0)) || DynamicBackground;
		if(pEntry->m_NeedsRender || ViewChanged)
		{
			try
			{
				CQmMapPreviewDrawScope DrawScope(*pUi);
				pEntry->m_pRenderer->SetEntityOnly(EntityOnly);
				if(DrawScope.BeginRenderTarget(pEntry->m_RenderTarget, ColorRGBA(0.055f, 0.065f, 0.085f, 1.0f)))
				{
					// Begin 已清除旧像素；只有整次绘制与 End 成功才重新标记完整。
					pEntry->m_NeedsRender = true;
					if(EntityOnly)
						pGameClient->m_Background.RenderPreview(Camera.Center(), Camera.Zoom(), *pEntry->m_pRenderer);
					// FULL_DESIGN 明确跳过实体；实体模式使用包含完整实体层的前景策略。
					pEntry->m_pRenderer->RenderCustomWithCamera(Camera.Center(), Camera.Zoom(), EntityOnly ? ERenderType::RENDERTYPE_FOREGROUND : ERenderType::RENDERTYPE_FULL_DESIGN);
					DrawScope.EndRenderTarget();
					pEntry->m_RenderedCenter = Camera.Center();
					pEntry->m_RenderedZoom = Camera.Zoom();
					pEntry->m_RenderedEntities = EntityOnly;
					pEntry->m_RenderedBackgroundColor = g_Config.m_ClBackgroundEntitiesColor;
					str_copy(pEntry->m_aRenderedBackground, g_Config.m_ClBackgroundEntities);
					pEntry->m_NeedsRender = false;
				}
			}
			catch(const std::bad_alloc &)
			{
			}
			catch(const std::length_error &)
			{
			}
		}
		if(pEntry->m_NeedsRender)
		{
			pUi->DoLabel(&Preview, Localize("Initializing map layers"), 10.0f, TEXTALIGN_MC);
			return;
		}

		int BackendMajor = 0, BackendMinor = 0, BackendPatch = 0;
		const char *pBackendName = "";
		const auto Backend = pGameClient->Graphics()->GetDetectedContextVersion(BackendMajor, BackendMinor, BackendPatch, pBackendName) ? QmDummyMiniViewLayout::ResolveBackend(pBackendName) : QmDummyMiniViewLayout::EBackend::UNKNOWN;
		IGraphics::SRenderTargetDrawParams DrawParams;
		DrawParams.m_X = DrawRect.x;
		DrawParams.m_Y = DrawRect.y;
		DrawParams.m_W = DrawRect.w;
		DrawParams.m_H = DrawRect.h;
		DrawParams.m_Alpha = pUi->ControlAlphaScale();
		DrawParams.m_Corners = IGraphics::CORNER_ALL;
		DrawParams.m_Rounding = ui_token::radius::BASE;
		DrawParams.m_CornerSmoothing = QmEffectiveCornerSmoothing();
		DrawParams.m_CornerCurve = (float)g_Config.m_QmUiCornerCurve;
		DrawParams.m_CornerSeam = QmEffectiveCapsuleSeam();
		if(Backend == QmDummyMiniViewLayout::EBackend::VULKAN)
			DrawParams.SetTextureCoordinates(0.0f, 0.0f, 1.0f, 1.0f);
		const CUIRect Clip = pUi->IsClipped() ? Preview.Intersection(*pUi->ClipArea()) : Preview;
		try
		{
			CQmMapPreviewDrawScope DrawScope(*pUi);
			pUi->ClipEnable(&Clip);
			pGameClient->Graphics()->DrawRenderTarget(pEntry->m_RenderTarget, DrawParams);
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
	}
} // namespace

void CQmMapPreviewPanel::Init(CGameClient *pGameClient)
{
	if(pGameClient == nullptr)
		return;
	if(m_pRenderState != nullptr && m_pGameClient == pGameClient && m_pfnDrawCanvas != nullptr && m_pInitRetryGameClient == nullptr)
	{
		m_pInitRetryGameClient = nullptr;
		m_pfnRetryInit = nullptr;
		return;
	}
	m_pInitRetryGameClient = pGameClient;
	m_pfnRetryInit = [](CQmMapPreviewPanel *pPanel) {
		pPanel->Init(pPanel->m_pInitRetryGameClient);
	};
	try
	{
		auto pRenderer = std::make_unique<SRenderer>();
		pRenderer->m_pCache = std::make_unique<CQmMapPreviewCache>(pGameClient->Graphics());
		// 候选全部备齐后才回收原包；删除失败的包仍占着 owner，不能被候选覆盖。
		auto *pPrevious = static_cast<SRenderer *>(m_pRenderState);
		if(pPrevious != nullptr && !pPrevious->m_pCache->Clear())
		{
			m_pfnDrawCanvas = nullptr;
			return;
		}
		if(m_pfnDestroyRenderState != nullptr)
			m_pfnDestroyRenderState(m_pRenderState);
		m_pGameClient = pGameClient;
		m_pRenderState = pRenderer.release();
		m_pfnDestroyRenderState = [](void *pState) noexcept { delete static_cast<SRenderer *>(pState); };
		m_pfnDrawCanvas = &DrawCanvas;
		m_pInitRetryGameClient = nullptr;
		m_pfnRetryInit = nullptr;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}
}

void CQmMapPreviewPanel::Shutdown()
{
	m_pfnDrawCanvas = nullptr;
	m_pGameClient = nullptr;
	m_pInitRetryGameClient = nullptr;
	m_pfnRetryInit = nullptr;
	auto *pRenderer = static_cast<SRenderer *>(m_pRenderState);
	if(pRenderer != nullptr && !pRenderer->m_pCache->Clear())
		return;
	if(m_pfnDestroyRenderState != nullptr)
		m_pfnDestroyRenderState(m_pRenderState);
	m_pRenderState = nullptr;
	m_pfnDestroyRenderState = nullptr;
}
