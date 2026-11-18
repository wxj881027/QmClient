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
	constexpr size_t MAP_PREVIEW_CACHE_CAPACITY = 3;
	// LRU 逐出前的最小驻留时长：刚用过的条目即使超出容量也先保留一拍。
	constexpr float MAP_PREVIEW_LRU_MIN_AGE = 0.4f;
	// 失败条目的重查间隔：投票页下载完成后，占位应自行换成预览。
	constexpr float MAP_PREVIEW_FAILED_RETRY_DELAY = 2.0f;

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
		int m_UploadIndex = 0;
		std::unique_ptr<CMapPreviewLayers> m_pRenderer;
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
		float m_LastUseTime = 0.0f;
		float m_RetryAfterTime = 0.0f;
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
	std::shared_ptr<IHttpRequest> m_pDownloadTask;
	char m_aDownloadName[MAX_MAP_LENGTH] = {};
	std::shared_ptr<IHttpRequest> m_pIndexTask;
	bool m_IndexLoaded = false;
	bool m_IndexFailed = false;
	std::string m_NotFoundName;
	std::vector<std::string> m_vIndexHrefs;

	explicit CQmMapPreviewCache(IGraphics *pGraphics) :
		m_pGraphics(pGraphics) {}
	~CQmMapPreviewCache() { Clear(); }

	// 本地缺图时按地图名拉一次，进度供预览控件显示。
	bool EnsureDownloaded(CGameClient *pGameClient, const char *pMapName)
	{
		if(pMapName == nullptr || pMapName[0] == '\0')
			return false;
		if(m_pDownloadTask && str_comp(m_aDownloadName, pMapName) == 0)
			return true;
		auto *pClient = static_cast<CClient *>(pGameClient->Client());
		if(pClient == nullptr || pClient->Http() == nullptr)
			return false;
		if(m_NotFoundName != pMapName)
			m_NotFoundName.clear();
		if(!EnsureIndex(pGameClient))
			return m_pIndexTask != nullptr;
		char aEscaped[MAX_MAP_LENGTH * 3];
		EscapeUrl(aEscaped, sizeof(aEscaped), pMapName);
		const std::string Prefix = std::string(aEscaped) + "_";
		const std::string Href = QmSelectMapDownloadHref(m_vIndexHrefs, Prefix);
		if(Href.empty())
		{
			m_NotFoundName = pMapName;
			return false;
		}
		SQmMapDownloadPlan Plan;
		if(!QmBuildIndexedMapDownloadPlan(pMapName, Href, g_Config.m_ClMapDownloadUrl, &Plan))
		{
			m_NotFoundName = pMapName;
			return false;
		}
		std::shared_ptr<IHttpRequest> pTask(HttpGetFile(Plan.m_Url.c_str(), pGameClient->Storage(), Plan.m_FileName.c_str(), IStorage::TYPE_SAVE).release());
		if(!pTask)
			return false;
		m_pDownloadTask = pTask;
		str_copy(m_aDownloadName, pMapName);
		pClient->Http()->Run(m_pDownloadTask);
		return true;
	}

	int DownloadProgress(const char *pMapName) const
	{
		if(m_pIndexTask)
			return m_pIndexTask->Progress();
		if(!m_pDownloadTask || str_comp(m_aDownloadName, pMapName) != 0)
			return -1;
		return m_pDownloadTask->Progress();
	}

	bool DownloadFailed(const char *pMapName) const
	{
		if(m_IndexFailed || m_NotFoundName == pMapName)
			return true;
		if(!m_pDownloadTask || str_comp(m_aDownloadName, pMapName) != 0)
			return false;
		const EHttpState State = m_pDownloadTask->State();
		return State == EHttpState::ERROR || State == EHttpState::ABORTED;
	}

	void CancelDownload()
	{
		m_pDownloadTask.reset();
		m_aDownloadName[0] = '\0';
		if(m_pIndexTask)
			m_pIndexTask->Abort();
		m_pIndexTask.reset();
		m_IndexFailed = false;
		m_NotFoundName.clear();
	}

	bool DownloadFinished(const char *pMapName) const
	{
		return m_pDownloadTask && str_comp(m_aDownloadName, pMapName) == 0 &&
		       m_pDownloadTask->State() == EHttpState::DONE && m_pDownloadTask->Progress() >= 100;
	}

	void Clear()
	{
		if(m_pGraphics == nullptr)
		{
			m_vEntries.clear();
			return;
		}
		const uint32_t CurrentVersion = m_pGraphics->GraphicsResourcesResetVersion();
		for(auto &pEntry : m_vEntries)
			ReleaseEntry(*pEntry, CurrentVersion);
		m_vEntries.clear();
	}

	// 返回命中的缓存项；本地没有对应 .map 时返回带 m_LoadFailed 的空项。
	SMapPreviewEntry *Acquire(const char *pMapName, float NowTime, IStorage *pStorage, CGameClient *pGameClient)
	{
		for(auto It = m_vEntries.begin(); It != m_vEntries.end(); ++It)
		{
			if(str_comp((*It)->m_aMapName, pMapName) == 0)
			{
				(*It)->m_LastUseTime = NowTime;
				if((*It)->m_GraphicsResetVersion != m_pGraphics->GraphicsResourcesResetVersion())
				{
					ReleaseEntry(**It, m_pGraphics->GraphicsResourcesResetVersion());
					**It = SMapPreviewEntry{};
					str_copy((*It)->m_aMapName, pMapName);
					(*It)->m_LastUseTime = NowTime;
					(*It)->m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
					LoadEntry(**It, pStorage, pGameClient, NowTime);
				}
				// 失败条目间隔重查：下载完成前打开过弹窗的图，下载完成后应直接出预览。
				if((*It)->m_LoadFailed && NowTime >= (*It)->m_RetryAfterTime)
				{
					(*It)->m_RetryAfterTime = NowTime + MAP_PREVIEW_FAILED_RETRY_DELAY;
					(*It)->m_LoadFailed = false;
					LoadEntry(**It, pStorage, pGameClient, NowTime);
					(*It)->m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
				}
				auto pEntry = std::move(*It);
				m_vEntries.erase(It);
				m_vEntries.insert(m_vEntries.begin(), std::move(pEntry));
				return m_vEntries.front().get();
			}
		}

		if(m_vEntries.size() >= MAP_PREVIEW_CACHE_CAPACITY)
		{
			auto ItLru = std::min_element(m_vEntries.begin(), m_vEntries.end(), [](const auto &A, const auto &B) {
				return A->m_LastUseTime < B->m_LastUseTime;
			});
			if(ItLru == m_vEntries.end() || NowTime - (*ItLru)->m_LastUseTime < MAP_PREVIEW_LRU_MIN_AGE)
				return nullptr;
			ReleaseEntry(**ItLru, m_pGraphics->GraphicsResourcesResetVersion());
			m_vEntries.erase(ItLru);
		}

		auto pEntry = std::make_unique<SMapPreviewEntry>();
		str_copy(pEntry->m_aMapName, pMapName, sizeof(pEntry->m_aMapName));
		pEntry->m_LastUseTime = NowTime;
		LoadEntry(*pEntry, pStorage, pGameClient, NowTime);
		pEntry->m_GraphicsResetVersion = m_pGraphics->GraphicsResourcesResetVersion();
		m_vEntries.insert(m_vEntries.begin(), std::move(pEntry));
		return m_vEntries.front().get();
	}

	static bool AdvanceEntry(SMapPreviewEntry &Entry, CGameClient *pGameClient, float Now)
	try
	{
		if(!Entry.m_pLoadJob)
			return Entry.m_pRenderer != nullptr;
		auto &Job = *Entry.m_pLoadJob;
		if(!Job.Finished())
			return false;
		if(Job.State() != IJob::STATE_DONE || !Job.m_Success)
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
		auto pRenderer = std::make_unique<CMapPreviewLayers>();
		pRenderer->OnInterfacesInit(pGameClient);
		pRenderer->SetSources(Job.m_pLayers.get(), Entry.m_pImages.get());
		pRenderer->OnMapLoad();
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
	bool EnsureIndex(CGameClient *pGameClient)
	{
		if(m_IndexLoaded)
			return true;
		if(m_pIndexTask)
		{
			if(!m_pIndexTask->Done())
				return false;
			if(m_pIndexTask->State() == EHttpState::DONE)
			{
				unsigned char *pData = nullptr;
				size_t Length = 0;
				m_pIndexTask->Result(&pData, &Length);
				ParseIndex(pData, Length);
				m_IndexLoaded = true;
			}
			else
				m_IndexFailed = true;
			m_pIndexTask.reset();
			return m_IndexLoaded;
		}
		if(m_IndexFailed)
			return false;
		auto *pClient = static_cast<CClient *>(pGameClient->Client());
		if(pClient == nullptr || pClient->Http() == nullptr)
			return false;
		char aIndexUrl[512];
		str_format(aIndexUrl, sizeof(aIndexUrl), "%s/", g_Config.m_ClMapDownloadUrl);
		auto pTask = HttpGet(aIndexUrl);
		pTask->WriteToMemory();
		pTask->MaxResponseSize(8 * 1024 * 1024);
		pTask->Timeout(CTimeout{10000, 60000, 1024, 10});
		pTask->LogProgress(HTTPLOG::FAILURE);
		m_pIndexTask = std::shared_ptr<IHttpRequest>(pTask.release());
		pClient->Http()->Run(m_pIndexTask);
		return false;
	}

	void ParseIndex(const unsigned char *pData, size_t Length)
	{
		if(pData == nullptr || Length == 0)
			return;
		const std::string_view Html(reinterpret_cast<const char *>(pData), Length);
		size_t Pos = 0;
		while((Pos = Html.find("href=\"", Pos)) != std::string_view::npos)
		{
			Pos += 6;
			const size_t End = Html.find('"', Pos);
			if(End == std::string_view::npos)
				break;
			std::string Href(Html.substr(Pos, End - Pos));
			if(Href.size() > 4 && Href.compare(Href.size() - 4, 4, ".map") == 0 && Href.find('/') == std::string::npos)
				m_vIndexHrefs.push_back(std::move(Href));
			Pos = End + 1;
		}
	}

	void ReleaseEntry(SMapPreviewEntry &Entry, uint32_t CurrentResetVersion)
	{
		const bool HandleAlive = Entry.m_GraphicsResetVersion == CurrentResetVersion;
		if(Entry.m_RenderTarget.IsValid())
		{
			if(HandleAlive)
				m_pGraphics->DestroyRenderTarget(&Entry.m_RenderTarget);
			else
				Entry.m_RenderTarget.Invalidate();
		}
		if(Entry.m_pLoadJob)
			Entry.m_pLoadJob->m_Cancelled.store(true);
		if(HandleAlive)
		{
			if(Entry.m_pRenderer != nullptr)
				Entry.m_pRenderer->UnloadMap();
			if(Entry.m_pImages != nullptr)
				Entry.m_pImages->UnloadPreview();
			if(Entry.m_pMap != nullptr)
				Entry.m_pMap->Unload();
		}
	}

	static void LoadEntry(SMapPreviewEntry &Entry, IStorage *pStorage, CGameClient *pGameClient, float Now) noexcept
	{
		try
		{
			auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(pStorage, Entry.m_aMapName, str_comp(pGameClient->Client()->GetCurrentMap(), Entry.m_aMapName) == 0 ? pGameClient->Client()->GetCurrentMapPath() : "", pGameClient->Client()->IsSixup());
			if(pGameClient->Engine()->TryAddJob(pJob))
			{
				Entry.m_pLoadJob = std::move(pJob);
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
	std::vector<std::unique_ptr<SMapPreviewEntry>> m_vEntries;
};

namespace
{
	struct SRenderer
	{
		CQmMapPreviewCache *m_pCache = nullptr;
	};

	void DrawCanvas(void *pRenderState, CGameClient *pGameClient, CQmMapPreviewPanel::SContext *pCtx, CUIRect Preview, CUi *pUi, bool Active)
	{
		auto *pRenderer = static_cast<SRenderer *>(pRenderState);
		if(pRenderer == nullptr || pRenderer->m_pCache == nullptr || pGameClient == nullptr || pCtx == nullptr || pUi == nullptr)
			return;
		CQmMapPreviewCache *pCache = pRenderer->m_pCache;

		// 预览框背板。
		CUiScopedGaussianBlurSuppression PreviewBlurSuppression(pUi);
		Preview.Draw(ColorRGBA(0.055f, 0.065f, 0.085f, 1.0f), IGraphics::CORNER_ALL, 6.0f);
		Preview.Margin(1.0f, &Preview);

		const float Now = pGameClient->Client()->GlobalTime();
		SMapPreviewEntry *pEntry = pCache->Acquire(pCtx->m_Target.m_aMapName, Now, pGameClient->Storage(), pGameClient);
		if(pEntry == nullptr || pEntry->m_LoadFailed)
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
					if(pCache->DownloadFinished(pCtx->m_Target.m_aMapName))
						pCache->Clear();
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
		const bool Ready = CQmMapPreviewCache::AdvanceEntry(*pEntry, pGameClient, Now) && pEntry->m_pRenderer->AdvanceMapLoad(1);
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
			// UI 批次必须先落到原目标；地图渲染的投影和裁剪不得泄漏到菜单。
			pUi->FlushQuadBatch();
			pUi->ClipEnable(pUi->Screen());
			pEntry->m_pRenderer->SetEntityOnly(EntityOnly);
			if(pGameClient->Graphics()->BeginRenderTarget(pEntry->m_RenderTarget, ColorRGBA(0.055f, 0.065f, 0.085f, 1.0f)))
			{
				if(EntityOnly)
					pGameClient->m_Background.RenderPreview(Camera.Center(), Camera.Zoom(), *pEntry->m_pRenderer);
				// FULL_DESIGN 明确跳过实体；实体模式使用包含完整实体层的前景策略。
				pEntry->m_pRenderer->RenderCustomWithCamera(Camera.Center(), Camera.Zoom(), EntityOnly ? ERenderType::RENDERTYPE_FOREGROUND : ERenderType::RENDERTYPE_FULL_DESIGN);
				pGameClient->Graphics()->EndRenderTarget();
				pEntry->m_RenderedCenter = Camera.Center();
				pEntry->m_RenderedZoom = Camera.Zoom();
				pEntry->m_RenderedEntities = EntityOnly;
				pEntry->m_RenderedBackgroundColor = g_Config.m_ClBackgroundEntitiesColor;
				str_copy(pEntry->m_aRenderedBackground, g_Config.m_ClBackgroundEntities);
				pEntry->m_NeedsRender = false;
			}
			pUi->MapScreen();
			pUi->ClipDisable();
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
		pUi->FlushQuadBatch();
		pUi->ClipEnable(&Preview);
		pGameClient->Graphics()->DrawRenderTarget(pEntry->m_RenderTarget, DrawParams);
		pUi->ClipDisable();
	}
} // namespace

void CQmMapPreviewPanel::Init(CGameClient *pGameClient)
{
	if(pGameClient == nullptr)
		return;
	if(m_pRenderState != nullptr)
	{
		if(m_pGameClient == pGameClient)
			return;
		Shutdown();
	}
	auto *pRenderer = new SRenderer();
	pRenderer->m_pCache = new CQmMapPreviewCache(pGameClient->Graphics());
	m_pGameClient = pGameClient;
	m_pRenderState = pRenderer;
	m_pfnDrawCanvas = &DrawCanvas;
}

void CQmMapPreviewPanel::Shutdown()
{
	auto *pRenderer = static_cast<SRenderer *>(m_pRenderState);
	if(pRenderer != nullptr)
	{
		delete pRenderer->m_pCache;
		delete pRenderer;
	}
	m_pRenderState = nullptr;
	m_pfnDrawCanvas = nullptr;
	m_pGameClient = nullptr;
}
