/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mapimages.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/perf_timer.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/qmclient/map_image_decode_job.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/map/map_render_items.h>
#include <game/mapitems.h>

static bool ImageDataSizeValid(const CImageInfo &Image, size_t &DataSize)
{
	return Image.DataSize(DataSize) && DataSize != 0;
}

namespace
{
	// 钩子碰撞预览需要的两个 tile（下标即 CMapImages::m_aHookPreviewTileTextures 槽位）。
	constexpr int gs_aHookPreviewTileIndices[] = {TILE_NOHOOK, TILE_SOLID};

	static int HookPreviewTileSlot(int TileIndex)
	{
		for(size_t i = 0; i < std::size(gs_aHookPreviewTileIndices); ++i)
		{
			if(gs_aHookPreviewTileIndices[i] == TileIndex)
				return static_cast<int>(i);
		}
		return -1;
	}

	// 钩子碰撞预览小图解码 job：worker 只做文件探测/读取与 PNG 解码，不触碰 GPU；
	// 自包含（不回指 CMapImages），主线程 poll 到完成后校验 key 仍一致才上传。
	class CHookPreviewDecodeJob : public IJob
	{
	public:
		CHookPreviewDecodeJob(IStorage *pStorage, const char *pEntitiesPath, int ModType, bool Masked) :
			m_pStorage(pStorage),
			m_ModType(ModType),
			m_Masked(Masked)
		{
			str_copy(m_aEntitiesPath, pEntitiesPath, sizeof(m_aEntitiesPath));
		}

		~CHookPreviewDecodeJob() override
		{
			FreeTiles();
		}

		void Run() override
		{
			const char *pModName = gs_apModEntitiesNames[m_ModType];
			char aPath[IO_MAX_PATH_LENGTH];
			// 与 GetEntities 相同的探测顺序；先 FileExists，避免对缺失路径刷解码报错
			str_format(aPath, sizeof(aPath), "%s/%s.png", m_aEntitiesPath, pModName);
			if(!m_pStorage->FileExists(aPath, IStorage::TYPE_ALL))
			{
				bool Found = false;
				if(m_ModType == MAP_IMAGE_MOD_TYPE_DDNET)
				{
					str_format(aPath, sizeof(aPath), "%s.png", m_aEntitiesPath);
					Found = m_pStorage->FileExists(aPath, IStorage::TYPE_ALL);
				}
				if(!Found)
					str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", pModName);
			}

			void *pFileData = nullptr;
			unsigned FileSize = 0;
			if(!m_pStorage->ReadFile(aPath, IStorage::TYPE_ALL, &pFileData, &FileSize))
			{
				log_error("mapimages", "Hook preview tile decode: failed to read '%s'.", aPath);
				return;
			}
			str_copy(m_aSourcePath, aPath, sizeof(m_aSourcePath));

			CImageInfo Info;
			if(!CImageLoader::LoadPng(pFileData, FileSize, aPath, Info))
			{
				free(pFileData);
				log_error("mapimages", "Hook preview tile decode: failed to decode '%s'.", aPath);
				return;
			}
			free(pFileData);

			if(Info.m_Width < 16 || Info.m_Height < 16 || Info.m_Format == CImageInfo::FORMAT_UNDEFINED)
			{
				Info.Free();
				return;
			}

			const size_t TileWidth = Info.m_Width / 16;
			const size_t TileHeight = Info.m_Height / 16;
			for(size_t i = 0; i < std::size(gs_aHookPreviewTileIndices); ++i)
			{
				CImageInfo &Tile = m_aTileImages[i];
				Tile.m_Width = TileWidth;
				Tile.m_Height = TileHeight;
				Tile.m_Format = Info.m_Format;
				size_t TileDataSize = 0;
				if(!Tile.DataSize(TileDataSize) || TileDataSize == 0)
				{
					Info.Free();
					FreeTiles();
					return;
				}
				Tile.m_pData = static_cast<uint8_t *>(malloc(TileDataSize));
				if(Tile.m_pData == nullptr)
				{
					Info.Free();
					FreeTiles();
					return;
				}
				const size_t OffsetX = static_cast<size_t>(gs_aHookPreviewTileIndices[i] % 16) * TileWidth;
				const size_t OffsetY = static_cast<size_t>(gs_aHookPreviewTileIndices[i] / 16) * TileHeight;
				Tile.CopyRectFrom(Info, OffsetX, OffsetY, TileWidth, TileHeight, 0, 0);
			}
			Info.Free();
			m_Success = true;
		}

		void FreeTiles()
		{
			for(auto &Tile : m_aTileImages)
				Tile.Free();
		}

		CImageInfo m_aTileImages[std::size(gs_aHookPreviewTileIndices)];
		char m_aSourcePath[IO_MAX_PATH_LENGTH] = "";
		char m_aEntitiesPath[IO_MAX_PATH_LENGTH] = "";
		int m_ModType = MAP_IMAGE_MOD_TYPE_DDNET;
		bool m_Masked = false;
		bool m_Success = false;

	private:
		IStorage *m_pStorage;
	};
} // namespace

static bool ResolveEntitiesImagePath(IStorage *pStorage, const char *pEntitiesPath, int ModType, char *pOut, size_t OutSize)
{
	if(pStorage == nullptr || pEntitiesPath == nullptr || ModType < 0 || ModType >= MAP_IMAGE_MOD_TYPE_COUNT)
		return false;
	const char *pModName = gs_apModEntitiesNames[ModType];
	str_format(pOut, OutSize, "%s/%s.png", pEntitiesPath, pModName);
	if(pStorage->FileExists(pOut, IStorage::TYPE_ALL))
		return true;
	if(ModType == MAP_IMAGE_MOD_TYPE_DDNET)
	{
		str_format(pOut, OutSize, "%s.png", pEntitiesPath);
		if(pStorage->FileExists(pOut, IStorage::TYPE_ALL))
			return true;
	}
	str_format(pOut, OutSize, "editor/entities_clear/%s.png", pModName);
	return pStorage->FileExists(pOut, IStorage::TYPE_ALL);
}

static void LogEntityTexturePrewarm(
	const IClient *pClient,
	const char *pState,
	int Slot,
	int ModType,
	bool Masked,
	bool Blank,
	int Layer,
	int Step,
	int TotalSteps,
	const CImageInfo *pImage,
	double DecodeMs,
	double StepMs,
	double TotalMs,
	const char *pPath,
	const char *pDetail = nullptr)
{
	if(!QmPerfEnabled())
		return;

	const char *pModName = ModType >= 0 && ModType < MAP_IMAGE_MOD_TYPE_COUNT ? gs_apModEntitiesNames[ModType] : "unknown";
	size_t ImageWidth = 0;
	size_t ImageHeight = 0;
	size_t TextureBytes = 0;
	if(pImage != nullptr)
	{
		ImageWidth = pImage->m_Width;
		ImageHeight = pImage->m_Height;
		(void)pImage->DataSize(TextureBytes);
	}
	char aDetail[128] = "";
	if(pDetail != nullptr && pDetail[0] != '\0')
		str_format(aDetail, sizeof(aDetail), " detail=%s", pDetail);

	char aPayload[1024];
	str_format(aPayload, sizeof(aPayload),
		"event=entity_texture_prewarm state=%s slot=%d mod=%s masked=%d blank=%d layer=%d step=%d steps=%d image_w=%zu image_h=%zu texture_bytes=%zu decode_ms=%.2f step_ms=%.3f total_ms=%.2f path=\"%s\"%s",
		pState, Slot, pModName, Masked ? 1 : 0, Blank ? 1 : 0, Layer, Step, TotalSteps,
		ImageWidth, ImageHeight, TextureBytes, DecodeMs, StepMs, TotalMs, pPath != nullptr ? pPath : "", aDetail);
	QmPerfLogPayload("perf/connect", aPayload, pClient, "assets");
}

CMapImages::CMapImages()
{
	m_Count = 0;
	std::fill(std::begin(m_aEntitiesIsLoaded), std::end(m_aEntitiesIsLoaded), false);
	m_SpeedupArrowIsLoaded = false;
	m_TuneColorsIsLoaded = false;

	str_copy(m_aEntitiesPath, "editor/entities_clear");

	static_assert(std::size(gs_apModEntitiesNames) == MAP_IMAGE_MOD_TYPE_COUNT, "Mod name string count is not equal to mod type count");
}

CMapImages::~CMapImages()
{
	CancelEntitiesPrewarm(false);
}

void CMapImages::OnInit()
{
	m_TextureScale = g_Config.m_ClTextEntitiesSize;
	InitOverlayTextures();

	const auto Selection = ResolveEntitiesAssetSelection(g_Config.m_ClAssetsEntities, g_Config.m_QmBlankAssetFallback != 0);
	str_copy(m_aEntitiesPath, Selection.m_Path.c_str());
	m_EntitiesIsBlank = Selection.m_IsBlank;

	Console()->Chain("cl_text_entities_size", ConchainClTextEntitiesSize, this);

	// 空闲预热：后台解码钩子碰撞预览所需的两块 tile，
	// 把 PNG 解码成本挪离设置页首次点击，菜单会话不再加载完整 entities。
	RequestHookPreviewTileTextures();
}

void CMapImages::CancelImageLoad()
{
	if(m_pMapImageDecode)
		m_pMapImageDecode->Abort();
	m_pMapImageDecode.reset();
	m_ImageLoad = {};
}

void CMapImages::AdvanceImageLoad()
{
	if(m_ImageLoad.Resume())
		m_ImageLoad = {};
}

void CMapImages::Unload()
{
	CancelImageLoad();
	// unload all textures
	for(int i = 0; i < m_Count; i++)
	{
		Graphics()->UnloadTexture(&m_aTextures[i]);
		m_aTextures[i].Invalidate();
	}
	m_Count = 0;
}

CMapLoadTask CMapImages::LoadMapImages(CLayers *pLayers, IMap *pMap, bool Live)
{
	int Start;
	pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &m_Count);
	m_Count = std::clamp<int>(m_Count, 0, MAX_MAPIMAGES);

	int LayersStart, LayersCount;
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersCount);
	bool ShowWarning = false;
	unsigned char aTextureUsedByTileOrQuadLayerFlag[MAX_MAPIMAGES] = {0}; // 0: nothing, 1(as flag): tile layer, 2(as flag): quad layer
	for(int GroupIndex = 0; GroupIndex < pLayers->NumGroups(); GroupIndex++)
	{
		const CMapItemGroup *pGroup = pLayers->GetGroup(GroupIndex);
		if(!pGroup)
		{
			continue;
		}

		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			const CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + LayerIndex);
			if(!pLayer)
			{
				continue;
			}

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap *pLayerTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
				if(pLayerTilemap->m_Image >= 0 && pLayerTilemap->m_Image < m_Count)
				{
					aTextureUsedByTileOrQuadLayerFlag[pLayerTilemap->m_Image] |= 1;
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				const auto *pLayerQuads = MapRenderItems::QuadLayer(*pMap, LayersStart + pGroup->m_StartLayer + LayerIndex);
				if(pLayerQuads == nullptr)
				{
					log_error("mapimages", "Ignoring truncated quad layer %d.", pGroup->m_StartLayer + LayerIndex);
					ShowWarning = true;
					continue;
				}
				if(pLayerQuads->m_Image >= 0 && pLayerQuads->m_Image < m_Count)
				{
					aTextureUsedByTileOrQuadLayerFlag[pLayerQuads->m_Image] |= 2;
				}
			}
		}
	}

	if(Live)
		co_await std::suspend_always{};

	const int TextureLoadFlag = Graphics()->TextureLoadFlags();
	const auto SubmitDecode = [this](const std::shared_ptr<CMapImageDecodeJob> &pJob) {
		return Engine()->TryAddJob(pJob);
	};

	// load new textures
	for(int i = 0; i < m_Count; i++)
	{
		if(aTextureUsedByTileOrQuadLayerFlag[i] == 0)
		{
			// skip loading unused images
			continue;
		}

		const int LoadFlag = (((aTextureUsedByTileOrQuadLayerFlag[i] & 1) != 0) ? TextureLoadFlag : 0) | (((aTextureUsedByTileOrQuadLayerFlag[i] & 2) != 0) ? 0 : (Graphics()->HasTextureArraysSupport() ? IGraphics::TEXLOAD_NO_2D_TEXTURE : 0));
		const auto ImageItem = MapRenderItems::ReadImage(*pMap, Start + i);
		if(!ImageItem)
		{
			log_error("mapimages", "Failed to load truncated map image %d.", i);
			ShowWarning = true;
			continue;
		}

		const char *pName = pMap->GetDataString(ImageItem->m_ImageName);
		if(pName == nullptr || pName[0] == '\0')
		{
			if(ImageItem->m_External)
			{
				log_error("mapimages", "Failed to load map image %d: failed to load name.", i);
				ShowWarning = true;
				continue;
			}
			pName = "(error)";
		}

		if(ImageItem->m_Version > 1 && ImageItem->m_MustBe1 != 1)
		{
			log_error("mapimages", "Failed to load map image %d '%s': invalid map image type.", i, pName);
			ShowWarning = true;
			continue;
		}

		const bool Trace = Live && QmPerfEnabled();
		double ReadMs = 0.0;
		double DecodeMs = 0.0;
		double SubmitMs = 0.0;
		bool CombinedReadDecode = false;
		SMapImageDecodeStart Decode;
		using EDecodeStart = SMapImageDecodeStart::EResult;
		if(ImageItem->m_External)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			bool Translated = false;
			if(Client()->IsSixup())
			{
				Translated =
					!str_comp(pName, "grass_doodads") ||
					!str_comp(pName, "grass_main") ||
					!str_comp(pName, "winter_main") ||
					!str_comp(pName, "generic_shadows") ||
					!str_comp(pName, "generic_unhookable") ||
					!str_comp(pName, "easter");
			}
			str_format(aPath, sizeof(aPath), "mapres/%s%s.png", pName, Translated ? "_0.7" : "");
			if(Live)
			{
				Decode = TryStartMapImageDecode([&] {
					return std::make_shared<CMapImageDecodeJob>(Storage(), aPath, Trace);
				},
					SubmitDecode);
			}
			else
				m_aTextures[i] = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL, LoadFlag);
		}
		else
		{
			if(ImageItem->m_Width <= 0 || ImageItem->m_Height <= 0)
			{
				log_error("mapimages", "Failed to load map image %d '%s': invalid image dimensions.", i, pName);
				ShowWarning = true;
				continue;
			}

			CImageInfo ImageInfo;
			ImageInfo.m_Width = ImageItem->m_Width;
			ImageInfo.m_Height = ImageItem->m_Height;
			ImageInfo.m_Format = CImageInfo::FORMAT_RGBA;
			size_t ImageDataSize = 0;
			const int FileDataSize = pMap->GetDataSize(ImageItem->m_ImageData);
			char aTexName[IO_MAX_PATH_LENGTH];
			str_format(aTexName, sizeof(aTexName), "embedded: %s", pName);
			if(Live && FileDataSize > 0 && ImageInfo.DataSize(ImageDataSize) && (size_t)FileDataSize >= ImageDataSize)
			{
				Decode = TryStartMapImageDecode([&] {
					auto pJob = std::make_shared<CMapImageDecodeJob>(nullptr, aTexName, Trace);
					CPerfTimer ReadTimer(Trace);
					if(pMap->ReadCompressedData(ImageItem->m_ImageData, pJob->m_vCompressed))
					{
						pJob->m_Image = std::move(ImageInfo);
						pJob->m_UncompressedSize = FileDataSize;
					}
					else
						pJob.reset();
					ReadMs = ReadTimer.ElapsedMs();
					return pJob;
				},
					SubmitDecode);
			}
			if(Decode.m_Result == EDecodeStart::NO_JOB)
			{
				CPerfTimer Timer(Trace);
				ImageInfo.m_pData = static_cast<uint8_t *>(pMap->GetData(ImageItem->m_ImageData));
				const int LoadedSize = pMap->GetDataSize(ImageItem->m_ImageData);
				ReadMs += Timer.ElapsedMs();
				CombinedReadDecode = true;
				if(ImageInfo.m_pData && LoadedSize >= 0 && ImageInfo.DataSize(ImageDataSize) && (size_t)LoadedSize >= ImageDataSize)
				{
					Timer.Reset();
					m_aTextures[i] = Graphics()->LoadTextureRaw(ImageInfo, LoadFlag, aTexName);
					SubmitMs = Timer.ElapsedMs();
				}
				else
				{
					log_error("mapimages", "Failed to load embedded map image %d.", i);
					ShowWarning = true;
				}
				pMap->UnloadData(ImageItem->m_ImageData);
			}
		}

		if(const auto &pDecode = Decode.m_pJob)
		{
			m_pMapImageDecode = pDecode;
			do
			{
				co_await std::suspend_always{};
			} while(!pDecode->Done());
			if(pDecode->State() == IJob::STATE_DONE)
			{
				ReadMs += pDecode->m_ReadMs;
				DecodeMs = pDecode->m_DecodeMs;
				CPerfTimer SubmitTimer(Trace);
				if(ImageItem->m_External)
					m_aTextures[i] = Graphics()->FinishExternalTexture(pDecode->m_Image, LoadFlag, pDecode->m_Path.c_str(), pDecode->m_PngliteIncompatible);
				else if(pDecode->m_Image.m_pData)
					m_aTextures[i] = Graphics()->LoadTextureRawMove(pDecode->m_Image, LoadFlag, pDecode->m_Path.c_str());
				else
					log_error("mapimages", "Failed to decompress embedded map image %d.", i);
				SubmitMs = SubmitTimer.ElapsedMs();
			}
			ShowWarning |= !m_aTextures[i].IsValid();
			m_pMapImageDecode.reset();
		}
		else if(Decode.m_Result != EDecodeStart::NO_JOB)
		{
			const char *pReason = Decode.m_Result == EDecodeStart::REJECTED ? "decode queue unavailable" : "insufficient resources to start decoding";
			log_error("mapimages", "Failed to prepare map image %d '%s': %s.", i, pName, pReason);
			ShowWarning = true;
		}
		if(Trace)
		{
			char aExtra[224];
			str_format(aExtra, sizeof(aExtra), "image=%d external=%d read_ms=%.3f decode_ms=%.3f submit_ms=%.3f read_includes_decode=%d",
				i, ImageItem->m_External != 0, ReadMs, DecodeMs, SubmitMs, CombinedReadDecode ? 1 : 0);
			QmPerfLogStage("perf/connect", "map_image_prepare", ReadMs + DecodeMs + SubmitMs, true, Client(), nullptr, nullptr, aExtra);
		}
		pMap->UnloadData(ImageItem->m_ImageName);
		ShowWarning = ShowWarning || m_aTextures[i].IsNullTexture();
		if(Live)
			co_await std::suspend_always{};
	}
	if(ShowWarning)
	{
		// 警告队列分配失败时，保留已经完成的纹理。
		try
		{
			Client()->AddWarning(SWarning(Localize("Some map images could not be loaded. Check the local console for details.")));
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
	}
}

void CMapImages::OnMapLoad()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	CLayers *pLayers = GameClient()->Layers();
	Unload();
	// 实体纹理跨地图保留。OnMapLoad 只卸载地图图片，不能顺带取消启动阶段
	// 已经开始的实体纹理预热；若此前被配置切换取消，这里按当前游戏信息重新启动。
	PrewarmEntities();
	const bool Live = Client()->State() == IClient::STATE_LOADING;
	m_ImageLoad = LoadMapImages(pLayers, pMap, Live);
	if(Live)
		AdvanceImageLoad();
	else
		m_ImageLoad.Finish();
}

void CMapImages::LoadBackground(class CLayers *pLayers, class IMap *pMap)
{
	Unload();
	LoadMapImages(pLayers, pMap, false).Finish();
}

static EMapImageModType GetEntitiesModType(const CGameInfo &GameInfo)
{
	if(GameInfo.m_EntitiesFDDrace)
		return MAP_IMAGE_MOD_TYPE_FDDRACE;
	else if(GameInfo.m_EntitiesDDNet)
		return MAP_IMAGE_MOD_TYPE_DDNET;
	else if(GameInfo.m_EntitiesDDRace)
		return MAP_IMAGE_MOD_TYPE_DDRACE;
	else if(GameInfo.m_EntitiesRace)
		return MAP_IMAGE_MOD_TYPE_RACE;
	else if(GameInfo.m_EntitiesBW)
		return MAP_IMAGE_MOD_TYPE_BLOCKWORLDS;
	else if(GameInfo.m_EntitiesFNG)
		return MAP_IMAGE_MOD_TYPE_FNG;
	else if(GameInfo.m_EntitiesVanilla)
		return MAP_IMAGE_MOD_TYPE_VANILLA;
	else
		return MAP_IMAGE_MOD_TYPE_DDNET;
}

static bool IsValidTile(int LayerType, bool EntitiesAreMasked, EMapImageModType EntitiesModType, int TileIndex)
{
	if(TileIndex == TILE_AIR)
		return false;
	if(!EntitiesAreMasked)
		return true;

	if(EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET || EntitiesModType == MAP_IMAGE_MOD_TYPE_DDRACE)
	{
		if(EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET || TileIndex != TILE_SPEED_BOOST_OLD)
		{
			if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH &&
				!IsValidGameTile(TileIndex) &&
				!IsValidFrontTile(TileIndex) &&
				!IsValidSpeedupTile(TileIndex) &&
				!IsValidTeleTile(TileIndex) &&
				!IsValidTuneTile(TileIndex))
			{
				return false;
			}
			else if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH &&
				!IsValidSwitchTile(TileIndex))
			{
				return false;
			}
		}
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_RACE && IsCreditsTile(TileIndex))
	{
		return false;
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_FNG && IsCreditsTile(TileIndex))
	{
		return false;
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_VANILLA && IsCreditsTile(TileIndex))
	{
		return false;
	}
	return true;
}

static std::shared_ptr<CImageInfo> MakeEntityPrewarmImage(CImageInfo &&Image)
{
	return std::shared_ptr<CImageInfo>(
		new CImageInfo(std::move(Image)),
		[](CImageInfo *pImage) {
			pImage->Free();
			delete pImage;
		});
}

// 实体纹理的 CPU 构建放在 job 线程，主线程只负责轮询、提交纹理上传。
// 每层单独一个 job，因此同一时刻最多只保留一份构建输出，不会把四层
// 4096² 图像同时压在内存里。
class CQmEntityPrewarmBuildJob final : public IJob
{
public:
	enum class EKind
	{
		LAYER,
		TUNE_COLOR,
	};

	CQmEntityPrewarmBuildJob(
		std::shared_ptr<CImageInfo> pSource,
		int ModType,
		bool Masked,
		int LayerType,
		EKind Kind) :
		m_pSource(std::move(pSource)),
		m_ModType(ModType),
		m_Masked(Masked),
		m_LayerType(LayerType),
		m_Kind(Kind)
	{
		Abortable(true);
	}

	~CQmEntityPrewarmBuildJob() override
	{
		m_Build.Free();
	}

	void Run() override
	{
		if(State() == STATE_ABORTED || m_pSource == nullptr)
			return;

		const CImageInfo &Source = *m_pSource;
		if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0 ||
			(Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA))
			return;

		const size_t CopyWidth = Source.m_Width / 16;
		const size_t CopyHeight = Source.m_Height / 16;
		if(CopyWidth == 0 || CopyHeight == 0)
			return;

		m_Build.m_Width = Source.m_Width;
		m_Build.m_Height = Source.m_Height;
		m_Build.m_Format = Source.m_Format;
		size_t BuildDataSize = 0;
		if(!m_Build.DataSize(BuildDataSize) || BuildDataSize == 0)
			return;
		m_Build.m_pData = static_cast<uint8_t *>(malloc(BuildDataSize));
		if(m_Build.m_pData == nullptr)
			return;
		mem_zero(m_Build.m_pData, BuildDataSize);

		if(m_Kind == EKind::TUNE_COLOR)
		{
			const size_t TuneTileX = static_cast<size_t>(TILE_TUNE % 16) * CopyWidth;
			const size_t TuneTileY = static_cast<size_t>(TILE_TUNE / 16) * CopyHeight;
			for(int TileIndex = 1; TileIndex < 256; ++TileIndex)
			{
				if(State() == STATE_ABORTED)
				{
					m_Build.Free();
					return;
				}
				const size_t StartX = CopyWidth * (TileIndex % 16);
				const size_t StartY = CopyHeight * (TileIndex / 16);
				m_Build.CopyRectFrom(Source, TuneTileX, TuneTileY, CopyWidth, CopyHeight, StartX, StartY);
				const float Hue = std::fmod((TileIndex - 1) * normalized_golden_angle, 1.0f);
				ColorizeWithHueRect(m_Build, Hue, 0.75f, StartX, StartY, CopyWidth, CopyHeight);
			}
		}
		else
		{
			for(int TileIndex = 0; TileIndex < 256; ++TileIndex)
			{
				if(State() == STATE_ABORTED)
				{
					m_Build.Free();
					return;
				}
				int SourceTileIndex = TileIndex;
				if(!IsValidTile(m_LayerType, m_Masked, static_cast<EMapImageModType>(m_ModType), SourceTileIndex))
					continue;
				if(m_LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH && SourceTileIndex == TILE_SWITCHTIMEDOPEN)
					SourceTileIndex = 8;
				const size_t OffsetX = static_cast<size_t>(SourceTileIndex % 16) * CopyWidth;
				const size_t OffsetY = static_cast<size_t>(SourceTileIndex / 16) * CopyHeight;
				m_Build.CopyRectFrom(Source, OffsetX, OffsetY, CopyWidth, CopyHeight, OffsetX, OffsetY);
			}
		}
		m_Success = true;
	}

	CImageInfo TakeImage()
	{
		return std::move(m_Build);
	}

	bool Succeeded() const
	{
		return m_Success;
	}

private:
	std::shared_ptr<CImageInfo> m_pSource;
	CImageInfo m_Build;
	int m_ModType;
	bool m_Masked;
	int m_LayerType;
	EKind m_Kind;
	bool m_Success = false;
};

IGraphics::CTextureHandle CMapImages::GetEntities(EMapImageEntityLayerType EntityLayerType)
{
	const bool EntitiesAreMasked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const EMapImageModType EntitiesModType = GetEntitiesModType(GameClient()->m_GameInfo);
	const int EntityTextureSlot = (EntitiesModType * 2) + (int)EntitiesAreMasked;

	// 正常路径下启动预热已经跨帧完成；这里只在进服早于预热完成时收尾。
	// 解码尚未完成则取消预热并回退旧同步路径，保证请求不会等待 job。
	if(!m_aEntitiesIsLoaded[EntityTextureSlot] &&
		m_QmEntityPrewarmState != EQmEntityPrewarmState::NONE &&
		m_QmEntityPrewarmState != EQmEntityPrewarmState::FAILED &&
		m_QmEntityPrewarmModType == EntitiesModType && m_QmEntityPrewarmMasked == EntitiesAreMasked)
	{
		if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING)
			(void)FinishEntitiesPrewarmDecode();
		while(m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING && AdvanceEntitiesPrewarmStep())
		{
		}
		if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING ||
			m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING)
			CancelEntitiesPrewarm();
	}

	if(!m_aEntitiesIsLoaded[EntityTextureSlot])
	{
		const bool Trace = QmPerfEnabled() && Client()->State() == IClient::STATE_LOADING;
		CPerfTimer Timer(Trace);
		m_aEntitiesIsLoaded[EntityTextureSlot] = true;

		int TextureLoadFlag = 0;
		if(Graphics()->HasTextureArraysSupport())
			TextureLoadFlag = (Graphics()->TextureLoadFlags()) | IGraphics::TEXLOAD_NO_2D_TEXTURE;

		CImageInfo ImgInfo;
		char aPath[IO_MAX_PATH_LENGTH] = "";
		// 先探测存在性再解码：缺失路径不进 LoadPng，避免每次会话刷无害报错。
		if(ResolveEntitiesImagePath(Storage(), m_aEntitiesPath, EntitiesModType, aPath, sizeof(aPath)))
			Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);

		// 内置 blank 在关闭回退时按默认实体图尺寸造透明层；开启时保留默认图。
		if(m_EntitiesIsBlank && ImgInfo.m_pData != nullptr)
			ClearImageToTransparent(ImgInfo);

		if(ImgInfo.m_pData != nullptr)
		{
			CImageInfo BuildImageInfo;
			BuildImageInfo.m_Width = ImgInfo.m_Width;
			BuildImageInfo.m_Height = ImgInfo.m_Height;
			BuildImageInfo.m_Format = ImgInfo.m_Format;
			size_t BuildImageDataSize = 0;
			if(!ImageDataSizeValid(BuildImageInfo, BuildImageDataSize))
			{
				ImgInfo.Free();
				log_error("mapimages", "Failed to build entity textures: invalid image size.");
				return m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][EntityLayerType];
			}
			BuildImageInfo.m_pData = static_cast<uint8_t *>(malloc(BuildImageDataSize));
			if(BuildImageInfo.m_pData == nullptr)
			{
				ImgInfo.Free();
				log_error("mapimages", "Failed to build entity textures: allocation failed.");
				return m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][EntityLayerType];
			}
			const size_t CopyWidth = ImgInfo.m_Width / 16;
			const size_t CopyHeight = ImgInfo.m_Height / 16;
			const size_t TuneTileX = static_cast<size_t>(TILE_TUNE % 16) * CopyWidth;
			const size_t TuneTileY = static_cast<size_t>(TILE_TUNE / 16) * CopyHeight;

			// The tune tile is recolored per tune zone below, so start from grayscale.
			ConvertToGrayscaleRect(ImgInfo, TuneTileX, TuneTileY, CopyWidth, CopyHeight);

			// build game layer
			for(int LayerType = 0; LayerType < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT; ++LayerType)
			{
				dbg_assert(!m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][LayerType].IsValid(), "entities texture already loaded when it should not be");

				// set everything transparent
				mem_zero(BuildImageInfo.m_pData, BuildImageDataSize);

				for(int i = 0; i < 256; ++i)
				{
					int TileIndex = i;
					if(IsValidTile(LayerType, EntitiesAreMasked, EntitiesModType, TileIndex))
					{
						if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH && TileIndex == TILE_SWITCHTIMEDOPEN)
						{
							TileIndex = 8;
						}

						const size_t OffsetX = (size_t)(TileIndex % 16) * CopyWidth;
						const size_t OffsetY = (size_t)(TileIndex / 16) * CopyHeight;
						BuildImageInfo.CopyRectFrom(ImgInfo, OffsetX, OffsetY, CopyWidth, CopyHeight, OffsetX, OffsetY);
					}
				}

				m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][LayerType] = Graphics()->LoadTextureRaw(BuildImageInfo, TextureLoadFlag, aPath);
			}

			BuildImageInfo.Free();

			// Build one colored tune tile per texture-array index.
			if(Graphics()->HasTextureArraysSupport())
			{
				CImageInfo TuneMapInfo;
				TuneMapInfo.m_Width = ImgInfo.m_Width;
				TuneMapInfo.m_Height = ImgInfo.m_Height;
				TuneMapInfo.m_Format = ImgInfo.m_Format;
				size_t TuneMapDataSize = 0;
				if(!ImageDataSizeValid(TuneMapInfo, TuneMapDataSize))
				{
					log_error("mapimages", "Failed to build tune color texture: invalid image size.");
				}
				else
				{
					TuneMapInfo.m_pData = static_cast<uint8_t *>(malloc(TuneMapDataSize));
					if(TuneMapInfo.m_pData == nullptr)
					{
						log_error("mapimages", "Failed to build tune color texture: allocation failed.");
					}
					else
					{
						mem_zero(TuneMapInfo.m_pData, TuneMapDataSize);
						for(int TileIndex = 1; TileIndex < 256; ++TileIndex)
						{
							const size_t StartX = CopyWidth * (TileIndex % 16);
							const size_t StartY = CopyHeight * (TileIndex / 16);
							TuneMapInfo.CopyRectFrom(ImgInfo, TuneTileX, TuneTileY, CopyWidth, CopyHeight, StartX, StartY);
							const float Hue = std::fmod((TileIndex - 1) * normalized_golden_angle, 1.0f);
							ColorizeWithHueRect(TuneMapInfo, Hue, 0.75f, StartX, StartY, CopyWidth, CopyHeight);
						}
						m_TuneColorMapTexture = Graphics()->LoadTextureRawMove(TuneMapInfo, TextureLoadFlag);
						m_TuneColorsIsLoaded = true;
					}
				}
			}
			ImgInfo.Free();
		}
		if(Trace)
			QmPerfLogStage("perf/connect", "entity_texture_prepare", Timer.ElapsedMs(), true, Client());
	}

	return m_aaEntitiesTextures[EntityTextureSlot][EntityLayerType];
}

void CMapImages::PrewarmEntities()
{
	if(Engine() == nullptr || Storage() == nullptr)
		return;
	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);
	if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING ||
		m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING ||
		m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE)
	{
		if(m_QmEntityPrewarmModType == ModType && m_QmEntityPrewarmMasked == Masked)
			return;
	}
	// 启动阶段可能先按默认 GameInfo 预热；后续拿到真实服务器信息时必须允许
	// 切换目标，不能因为“已有预热”把新的 entities 槽位挡掉。
	PrewarmEntitiesForGameInfo(GameClient()->m_GameInfo);
}

void CMapImages::PrewarmEntitiesForGameInfo(const CGameInfo &GameInfo)
{
	if(Engine() == nullptr || Storage() == nullptr)
		return;

	const bool Masked = !GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameInfo);
	const int Slot = (ModType * 2) + (int)Masked;
	const int TotalSteps = MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT + (Graphics()->HasTextureArraysSupport() ? 1 : 0);
	const bool SameTarget = m_QmEntityPrewarmModType == ModType && m_QmEntityPrewarmMasked == Masked;
	if(SameTarget && (m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING || m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING))
		return;
	if(SameTarget && m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE)
		return;

	// 连接信息确认后目标 mod 可能变化。已完成的其他槽位纹理保持跨地图可用，
	// 但未完成的半成品必须卸载，避免切换目标后留下无主纹理。
	if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING || m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING)
		CancelEntitiesPrewarm(true);
	else if(m_QmEntityPrewarmState != EQmEntityPrewarmState::NONE)
		CancelEntitiesPrewarm(false);

	if(m_aEntitiesIsLoaded[Slot])
	{
		m_QmEntityPrewarmState = EQmEntityPrewarmState::COMPLETE;
		LogEntityTexturePrewarm(Client(), "reuse", Slot, ModType, Masked, m_EntitiesIsBlank,
			0, 0, TotalSteps, nullptr, 0.0, 0.0, 0.0, m_aEntitiesPath);
		return;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	if(!ResolveEntitiesImagePath(Storage(), m_aEntitiesPath, ModType, aPath, sizeof(aPath)))
	{
		m_QmEntityPrewarmState = EQmEntityPrewarmState::FAILED;
		LogEntityTexturePrewarm(Client(), "missing", Slot, ModType, Masked, m_EntitiesIsBlank,
			0, 0, TotalSteps, nullptr, 0.0, 0.0, 0.0, m_aEntitiesPath, "no_image");
		return;
	}

	try
	{
		const int64_t StartNs = time_get_nanoseconds().count();
		auto pJob = std::make_shared<CMapImageDecodeJob>(Storage(), aPath, false);
		if(!Engine()->TryAddJob(pJob))
		{
			m_QmEntityPrewarmState = EQmEntityPrewarmState::FAILED;
			LogEntityTexturePrewarm(Client(), "queue_failed", Slot, ModType, Masked, m_EntitiesIsBlank,
				0, 0, TotalSteps, nullptr, 0.0, 0.0, 0.0, aPath, "job_rejected");
			return;
		}
		m_pQmEntityPrewarmDecode = std::move(pJob);
		m_pQmEntityPrewarmSource.reset();
		m_pQmEntityPrewarmBuildJob.reset();
		str_copy(m_aQmEntityPrewarmPath, aPath, sizeof(m_aQmEntityPrewarmPath));
		m_QmEntityPrewarmModType = ModType;
		m_QmEntityPrewarmMasked = Masked;
		m_QmEntityPrewarmNextStep = 0;
		m_QmEntityPrewarmTextureLoadFlag = 0;
		m_QmEntityPrewarmCopyWidth = 0;
		m_QmEntityPrewarmCopyHeight = 0;
		m_QmEntityPrewarmStartNs = StartNs;
		m_QmEntityPrewarmDecodeDoneNs = 0;
		m_QmEntityPrewarmNextStepNs = 0;
		m_QmEntityPrewarmState = EQmEntityPrewarmState::DECODING;
		LogEntityTexturePrewarm(Client(), "start", Slot, ModType, Masked, m_EntitiesIsBlank,
			0, 0, TotalSteps, nullptr, 0.0, 0.0, 0.0, aPath);
	}
	catch(const std::exception &Error)
	{
		log_error("mapimages", "Failed to start entity texture prewarm: %s", Error.what());
		m_QmEntityPrewarmState = EQmEntityPrewarmState::FAILED;
		LogEntityTexturePrewarm(Client(), "start_failed", Slot, ModType, Masked, m_EntitiesIsBlank,
			0, 0, TotalSteps, nullptr, 0.0, 0.0, 0.0, aPath, "exception");
	}
}

void CMapImages::CancelEntitiesPrewarm(bool UnloadPartialTextures)
{
	if(m_pQmEntityPrewarmDecode != nullptr)
	{
		m_pQmEntityPrewarmDecode->Abort();
		m_pQmEntityPrewarmDecode.reset();
	}
	if(m_pQmEntityPrewarmBuildJob != nullptr)
	{
		m_pQmEntityPrewarmBuildJob->Abort();
		m_pQmEntityPrewarmBuildJob.reset();
	}
	if(UnloadPartialTextures && m_QmEntityPrewarmState != EQmEntityPrewarmState::COMPLETE &&
		m_QmEntityPrewarmModType >= 0 && m_QmEntityPrewarmModType < MAP_IMAGE_MOD_TYPE_COUNT)
	{
		const int Slot = (m_QmEntityPrewarmModType * 2) + (int)m_QmEntityPrewarmMasked;
		for(int LayerType = 0; LayerType < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT; ++LayerType)
		{
			if(m_aaEntitiesTextures[Slot][LayerType].IsValid())
			{
				Graphics()->UnloadTexture(&m_aaEntitiesTextures[Slot][LayerType]);
				m_aaEntitiesTextures[Slot][LayerType].Invalidate();
			}
		}
	}
	m_pQmEntityPrewarmSource.reset();
	m_pQmEntityPrewarmBuildJob.reset();
	m_aQmEntityPrewarmPath[0] = '\0';
	m_QmEntityPrewarmModType = -1;
	m_QmEntityPrewarmMasked = false;
	m_QmEntityPrewarmNextStep = 0;
	m_QmEntityPrewarmTextureLoadFlag = 0;
	m_QmEntityPrewarmCopyWidth = 0;
	m_QmEntityPrewarmCopyHeight = 0;
	m_QmEntityPrewarmStartNs = 0;
	m_QmEntityPrewarmDecodeDoneNs = 0;
	m_QmEntityPrewarmNextStepNs = 0;
	m_QmEntityPrewarmState = EQmEntityPrewarmState::NONE;
}

bool CMapImages::FinishEntitiesPrewarmDecode()
{
	if(m_QmEntityPrewarmState != EQmEntityPrewarmState::DECODING)
		return m_QmEntityPrewarmState == EQmEntityPrewarmState::BUILDING || m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE;
	if(m_pQmEntityPrewarmDecode == nullptr || !m_pQmEntityPrewarmDecode->Done())
		return false;
	if(m_pQmEntityPrewarmDecode->State() == IJob::STATE_DONE && m_pQmEntityPrewarmDecode->m_Image.m_pData != nullptr)
	{
		m_pQmEntityPrewarmSource = MakeEntityPrewarmImage(std::move(m_pQmEntityPrewarmDecode->m_Image));
		m_pQmEntityPrewarmDecode.reset();
		m_QmEntityPrewarmDecodeDoneNs = time_get_nanoseconds().count();
		if(m_EntitiesIsBlank)
			ClearImageToTransparent(*m_pQmEntityPrewarmSource);
		m_QmEntityPrewarmState = EQmEntityPrewarmState::BUILDING;
		const int Slot = (m_QmEntityPrewarmModType * 2) + (int)m_QmEntityPrewarmMasked;
		const int TotalSteps = MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT + (Graphics()->HasTextureArraysSupport() ? 1 : 0);
		const double DecodeMs = m_QmEntityPrewarmStartNs > 0 ? (double)(m_QmEntityPrewarmDecodeDoneNs - m_QmEntityPrewarmStartNs) / 1000000.0 : 0.0;
		LogEntityTexturePrewarm(Client(), "decoded", Slot, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked, m_EntitiesIsBlank,
			-1, 0, TotalSteps, m_pQmEntityPrewarmSource.get(), DecodeMs, 0.0, DecodeMs, m_aQmEntityPrewarmPath);
		return true;
	}
	m_pQmEntityPrewarmDecode.reset();
	const int Slot = (m_QmEntityPrewarmModType * 2) + (int)m_QmEntityPrewarmMasked;
	const int TotalSteps = MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT + (Graphics()->HasTextureArraysSupport() ? 1 : 0);
	const int64_t NowNs = time_get_nanoseconds().count();
	const double TotalMs = m_QmEntityPrewarmStartNs > 0 ? (double)(NowNs - m_QmEntityPrewarmStartNs) / 1000000.0 : 0.0;
	LogEntityTexturePrewarm(Client(), "decode_failed", Slot, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked, m_EntitiesIsBlank,
		-1, 0, TotalSteps, nullptr, TotalMs, 0.0, TotalMs, m_aQmEntityPrewarmPath);
	CancelEntitiesPrewarm(true);
	m_QmEntityPrewarmState = EQmEntityPrewarmState::FAILED;
	return false;
}

bool CMapImages::AdvanceEntitiesPrewarmStep()
{
	if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING && !FinishEntitiesPrewarmDecode())
		return false;
	if(m_QmEntityPrewarmState != EQmEntityPrewarmState::BUILDING)
		return m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE;

	const int64_t StepStartNs = time_get_nanoseconds().count();
	try
	{
		if(m_pQmEntityPrewarmSource == nullptr)
			throw std::runtime_error("missing decoded entity image");
		const CImageInfo &Source = *m_pQmEntityPrewarmSource;
		const int Slot = (m_QmEntityPrewarmModType * 2) + (int)m_QmEntityPrewarmMasked;
		const int TotalSteps = MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT + (Graphics()->HasTextureArraysSupport() ? 1 : 0);
		const double DecodeMs = m_QmEntityPrewarmStartNs > 0 && m_QmEntityPrewarmDecodeDoneNs > 0 ?
						(double)(m_QmEntityPrewarmDecodeDoneNs - m_QmEntityPrewarmStartNs) / 1000000.0 :
						0.0;

		if(m_QmEntityPrewarmNextStep == 0 && m_QmEntityPrewarmCopyWidth == 0)
		{
			if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0 ||
				(Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA))
				throw std::runtime_error("invalid decoded entity image");
			m_QmEntityPrewarmCopyWidth = Source.m_Width / 16;
			m_QmEntityPrewarmCopyHeight = Source.m_Height / 16;
			if(m_QmEntityPrewarmCopyWidth == 0 || m_QmEntityPrewarmCopyHeight == 0)
				throw std::runtime_error("entity image is smaller than its tile grid");

			const size_t TuneTileX = static_cast<size_t>(TILE_TUNE % 16) * m_QmEntityPrewarmCopyWidth;
			const size_t TuneTileY = static_cast<size_t>(TILE_TUNE / 16) * m_QmEntityPrewarmCopyHeight;
			ConvertToGrayscaleRect(*m_pQmEntityPrewarmSource, TuneTileX, TuneTileY, m_QmEntityPrewarmCopyWidth, m_QmEntityPrewarmCopyHeight);
			m_QmEntityPrewarmTextureLoadFlag = Graphics()->HasTextureArraysSupport() ?
								   (Graphics()->TextureLoadFlags() | IGraphics::TEXLOAD_NO_2D_TEXTURE) :
								   0;
		}

		if(m_pQmEntityPrewarmBuildJob != nullptr)
		{
			if(!m_pQmEntityPrewarmBuildJob->Finished())
			{
				const auto JobState = m_pQmEntityPrewarmBuildJob->State();
				if(JobState == IJob::STATE_ABORTED || JobState == IJob::STATE_FAILED)
				{
					m_pQmEntityPrewarmBuildJob.reset();
					throw std::runtime_error("entity texture build job was canceled or failed");
				}
				return false;
			}

			if(m_pQmEntityPrewarmBuildJob->State() != IJob::STATE_DONE || !m_pQmEntityPrewarmBuildJob->Succeeded())
			{
				m_pQmEntityPrewarmBuildJob.reset();
				throw std::runtime_error("entity texture build job failed");
			}

			CImageInfo BuildImage = m_pQmEntityPrewarmBuildJob->TakeImage();
			m_pQmEntityPrewarmBuildJob.reset();
			if(m_QmEntityPrewarmNextStep < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT)
			{
				const int LayerType = m_QmEntityPrewarmNextStep;
				m_aaEntitiesTextures[Slot][LayerType] = Graphics()->LoadTextureRawMove(
					BuildImage, m_QmEntityPrewarmTextureLoadFlag, m_aQmEntityPrewarmPath);
				++m_QmEntityPrewarmNextStep;
				const int64_t NowNs = time_get_nanoseconds().count();
				LogEntityTexturePrewarm(Client(), "layer", Slot, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked, m_EntitiesIsBlank,
					LayerType, m_QmEntityPrewarmNextStep, TotalSteps, Source.m_pData != nullptr ? m_pQmEntityPrewarmSource.get() : nullptr, DecodeMs,
					(double)(NowNs - StepStartNs) / 1000000.0,
					m_QmEntityPrewarmStartNs > 0 ? (double)(NowNs - m_QmEntityPrewarmStartNs) / 1000000.0 : 0.0,
					m_aQmEntityPrewarmPath);
				return true;
			}

			m_TuneColorMapTexture = Graphics()->LoadTextureRawMove(BuildImage, m_QmEntityPrewarmTextureLoadFlag);
			m_TuneColorsIsLoaded = true;
			m_QmEntityPrewarmNextStep = TotalSteps;
		}

		if(m_QmEntityPrewarmNextStep < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT)
		{
			const int LayerType = m_QmEntityPrewarmNextStep;
			auto pJob = std::make_shared<CQmEntityPrewarmBuildJob>(
				m_pQmEntityPrewarmSource, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked,
				LayerType, CQmEntityPrewarmBuildJob::EKind::LAYER);
			if(!Engine()->TryAddJob(pJob))
				throw std::runtime_error("entity texture build queue unavailable");
			m_pQmEntityPrewarmBuildJob = std::move(pJob);
			return false;
		}

		if(Graphics()->HasTextureArraysSupport() && !m_TuneColorsIsLoaded)
		{
			auto pJob = std::make_shared<CQmEntityPrewarmBuildJob>(
				m_pQmEntityPrewarmSource, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked,
				-1, CQmEntityPrewarmBuildJob::EKind::TUNE_COLOR);
			if(!Engine()->TryAddJob(pJob))
				throw std::runtime_error("tune color build queue unavailable");
			m_pQmEntityPrewarmBuildJob = std::move(pJob);
			return false;
		}
		if(!Graphics()->HasTextureArraysSupport())
		{
			m_TuneColorMapTexture = m_aaEntitiesTextures[Slot][MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH];
			m_TuneColorsIsLoaded = true;
		}

		m_aEntitiesIsLoaded[Slot] = true;
		m_QmEntityPrewarmState = EQmEntityPrewarmState::COMPLETE;
		const int64_t NowNs = time_get_nanoseconds().count();
		LogEntityTexturePrewarm(Client(), "complete", Slot, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked, m_EntitiesIsBlank,
			-1, TotalSteps, TotalSteps, m_pQmEntityPrewarmSource.get(), DecodeMs,
			(double)(NowNs - StepStartNs) / 1000000.0,
			m_QmEntityPrewarmStartNs > 0 ? (double)(NowNs - m_QmEntityPrewarmStartNs) / 1000000.0 : 0.0,
			m_aQmEntityPrewarmPath);
		m_pQmEntityPrewarmSource.reset();
		return true;
	}
	catch(const std::exception &Error)
	{
		log_error("mapimages", "Failed to prewarm entity textures: %s", Error.what());
		const int Slot = (m_QmEntityPrewarmModType * 2) + (int)m_QmEntityPrewarmMasked;
		const int TotalSteps = MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT + (Graphics()->HasTextureArraysSupport() ? 1 : 0);
		const int64_t NowNs = time_get_nanoseconds().count();
		LogEntityTexturePrewarm(Client(), "failed", Slot, m_QmEntityPrewarmModType, m_QmEntityPrewarmMasked, m_EntitiesIsBlank,
			-1, m_QmEntityPrewarmNextStep, TotalSteps, m_pQmEntityPrewarmSource.get(), 0.0,
			(double)(NowNs - StepStartNs) / 1000000.0,
			m_QmEntityPrewarmStartNs > 0 ? (double)(NowNs - m_QmEntityPrewarmStartNs) / 1000000.0 : 0.0,
			m_aQmEntityPrewarmPath, "exception");
		CancelEntitiesPrewarm(true);
		m_QmEntityPrewarmState = EQmEntityPrewarmState::FAILED;
		return false;
	}
}

void CMapImages::AdvanceEntitiesPrewarm()
{
	if(m_QmEntityPrewarmState == EQmEntityPrewarmState::NONE ||
		m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE ||
		m_QmEntityPrewarmState == EQmEntityPrewarmState::FAILED)
		return;
	// 解码完成前只轮询，不占用上传预算；构建阶段按显示器刷新周期推进，
	// 避免进服后的正常帧一帧连做多层纹理上传。
	if(m_QmEntityPrewarmState == EQmEntityPrewarmState::DECODING)
	{
		(void)FinishEntitiesPrewarmDecode();
		return;
	}
	const int64_t NowNs = time_get_nanoseconds().count();
	if(NowNs < m_QmEntityPrewarmNextStepNs)
		return;
	int RefreshRate = Graphics() != nullptr ? Graphics()->ScreenRefreshRate() : 0;
	if(!in_range(RefreshRate, 1, 1000))
		RefreshRate = in_range(g_Config.m_GfxScreenRefreshRate, 1, 1000) ? g_Config.m_GfxScreenRefreshRate : 60;
	(void)AdvanceEntitiesPrewarmStep();
	m_QmEntityPrewarmNextStepNs = NowNs + 1000000000LL / RefreshRate;
}

bool CMapImages::EntitiesPrewarmFinished() const
{
	return m_QmEntityPrewarmState == EQmEntityPrewarmState::NONE ||
	       m_QmEntityPrewarmState == EQmEntityPrewarmState::COMPLETE ||
	       m_QmEntityPrewarmState == EQmEntityPrewarmState::FAILED;
}

IGraphics::CTextureHandle CMapImages::GetSpeedupArrow()
{
	if(!m_SpeedupArrowIsLoaded)
	{
		int TextureLoadFlag = (Graphics()->TextureLoadFlags()) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
		m_SpeedupArrowTexture = Graphics()->LoadTexture("editor/speed_arrow_array.png", IStorage::TYPE_ALL, TextureLoadFlag);
		m_SpeedupArrowIsLoaded = true;
	}
	return m_SpeedupArrowTexture;
}

IGraphics::CTextureHandle CMapImages::GetTuneColors()
{
	if(Graphics()->HasTextureArraysSupport())
	{
		if(!m_TuneColorsIsLoaded)
		{
			// Loading entities also creates the tune color map.
			GetEntities(EMapImageEntityLayerType::MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
			dbg_assert(m_TuneColorsIsLoaded, "Entities did not load the tune color map");
		}
		return m_TuneColorMapTexture;
	}
	return GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
}

IGraphics::CTextureHandle CMapImages::GetOverlayBottom()
{
	return m_OverlayBottomTexture;
}

IGraphics::CTextureHandle CMapImages::GetOverlayTop()
{
	return m_OverlayTopTexture;
}

IGraphics::CTextureHandle CMapImages::GetOverlayCenter()
{
	return m_OverlayCenterTexture;
}

void CMapImages::ChangeEntitiesPath(const char *pPath)
{
	CancelEntitiesPrewarm();
	const auto Selection = ResolveEntitiesAssetSelection(pPath, g_Config.m_QmBlankAssetFallback != 0);
	str_copy(m_aEntitiesPath, Selection.m_Path.c_str());
	m_EntitiesIsBlank = Selection.m_IsBlank;

	ReloadEntitiesTextures();
	// 空白开关变化也会改变预览可见性；清除失败状态和旧纹理后按新选择预热。
	HookPreviewInvalidate();
	// 实体包变化：预览小图缓存随之失效，后台重新解码（key 不变时为幂等空操作）
	RequestHookPreviewTileTextures();
}

void CMapImages::ConchainClTextEntitiesSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CMapImages *pThis = static_cast<CMapImages *>(pUserData);
		pThis->SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}
}

void CMapImages::ReloadEntitiesTextures()
{
	CancelEntitiesPrewarm();
	for(int ModType = 0; ModType < MAP_IMAGE_MOD_TYPE_COUNT * 2; ++ModType)
	{
		if(m_aEntitiesIsLoaded[ModType])
		{
			for(int LayerType = 0; LayerType < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT; ++LayerType)
			{
				Graphics()->UnloadTexture(&m_aaEntitiesTextures[ModType][LayerType]);
			}
			m_aEntitiesIsLoaded[ModType] = false;
		}
	}
}

void CMapImages::OnRender()
{
	HookPreviewPollJob();
}

bool CMapImages::HookPreviewKeyMatches(int ModType, bool Masked) const
{
	return m_HookPreviewModType == ModType && m_HookPreviewMasked == Masked &&
	       str_comp(m_aHookPreviewEntitiesPath, m_aEntitiesPath) == 0;
}

void CMapImages::HookPreviewInvalidate()
{
	for(auto &Texture : m_aHookPreviewTileTextures)
	{
		if(Texture.IsValid())
			Graphics()->UnloadTexture(&Texture);
	}
	m_aHookPreviewEntitiesPath[0] = '\0';
	m_HookPreviewModType = -1;
	m_HookPreviewMasked = false;
	m_HookPreviewFailed = false;
}

void CMapImages::HookPreviewKickJob()
{
	dbg_assert(!m_HookPreviewJobPending, "hook preview decode job already pending");
	HookPreviewInvalidate();

	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);
	m_HookPreviewModType = ModType;
	m_HookPreviewMasked = Masked;
	str_copy(m_aHookPreviewEntitiesPath, m_aEntitiesPath, sizeof(m_aHookPreviewEntitiesPath));
	m_pHookPreviewJob = std::make_shared<CHookPreviewDecodeJob>(Storage(), m_aEntitiesPath, ModType, Masked);
	Engine()->AddJob(m_pHookPreviewJob);
	m_HookPreviewJobPending = true;
}

void CMapImages::HookPreviewPollJob()
{
	if(!m_HookPreviewJobPending || m_pHookPreviewJob == nullptr || !m_pHookPreviewJob->Finished())
		return;

	std::shared_ptr<IJob> pJob = std::move(m_pHookPreviewJob);
	m_HookPreviewJobPending = false;
	auto *pDecode = static_cast<CHookPreviewDecodeJob *>(pJob.get());

	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);
	// 发布前校验 key：解码期间实体包或游戏信息可能已变化，过期结果直接丢弃
	if(m_EntitiesIsBlank || pDecode->m_ModType != ModType || pDecode->m_Masked != Masked ||
		str_comp(pDecode->m_aEntitiesPath, m_aEntitiesPath) != 0)
	{
		pDecode->FreeTiles();
		return;
	}
	if(pJob->State() != IJob::STATE_DONE || !pDecode->m_Success)
	{
		// 包缺失/损坏：保持失败状态，key 变化前不再重试
		m_HookPreviewFailed = true;
		pDecode->FreeTiles();
		return;
	}

	// 相同默认源的任务可跨开关切换复用；失效过的缓存 key 在成功发布时恢复。
	m_HookPreviewModType = ModType;
	m_HookPreviewMasked = Masked;
	str_copy(m_aHookPreviewEntitiesPath, m_aEntitiesPath);
	for(size_t i = 0; i < std::size(gs_aHookPreviewTileIndices); ++i)
	{
		m_aHookPreviewTileTextures[i] = Graphics()->LoadTextureRawMove(pDecode->m_aTileImages[i], 0, pDecode->m_aSourcePath);
	}
	m_HookPreviewFailed = false;
}

void CMapImages::RequestHookPreviewTileTextures()
{
	HookPreviewPollJob();
	if(m_EntitiesIsBlank || m_HookPreviewJobPending || m_HookPreviewFailed)
		return;

	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);
	if(HookPreviewKeyMatches(ModType, Masked) &&
		m_aHookPreviewTileTextures[0].IsValid() && m_aHookPreviewTileTextures[1].IsValid())
		return;

	HookPreviewKickJob();
}

CMapImages::EHookPreviewTileSource CMapImages::GetHookPreviewTileSource(int TileIndex, IGraphics::CTextureHandle &Texture)
{
	Texture = IGraphics::CTextureHandle();
	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);

	// 完整 entities 已在显存：GetEntities 直接命中加载闩，零成本，沿用原渲染路径
	if(m_aEntitiesIsLoaded[(ModType * 2) + (int)Masked])
		return EHookPreviewTileSource::FULL_ENTITIES_LOADED;

	// 空白包：完整路径会造全透明层，预览等价于不绘制
	if(m_EntitiesIsBlank)
		return EHookPreviewTileSource::UNAVAILABLE;

	HookPreviewPollJob();

	if(HookPreviewKeyMatches(ModType, Masked))
	{
		const int Slot = HookPreviewTileSlot(TileIndex);
		if(Slot != -1 && m_aHookPreviewTileTextures[Slot].IsValid())
		{
			Texture = m_aHookPreviewTileTextures[Slot];
			return EHookPreviewTileSource::SMALL_TEXTURE;
		}
	}

	// 兜底：预热未覆盖（如启动后实体包/游戏信息变化）时补一次后台解码
	if(!m_HookPreviewJobPending && !m_HookPreviewFailed)
		HookPreviewKickJob();
	return EHookPreviewTileSource::UNAVAILABLE;
}

void CMapImages::SetTextureScale(int Scale)
{
	if(m_TextureScale == Scale)
		return;

	m_TextureScale = Scale;

	if(Graphics() && m_OverlayCenterTexture.IsValid()) // check if component was initialized
	{
		// reinitialize component
		Graphics()->UnloadTexture(&m_OverlayBottomTexture);
		Graphics()->UnloadTexture(&m_OverlayTopTexture);
		Graphics()->UnloadTexture(&m_OverlayCenterTexture);

		InitOverlayTextures();
	}
}

int CMapImages::GetTextureScale() const
{
	return m_TextureScale;
}

IGraphics::CTextureHandle CMapImages::UploadEntityLayerText(int TextureSize, int MaxWidth, int YOffset)
{
	CImageInfo TextImage;
	TextImage.m_Width = 1024;
	TextImage.m_Height = 1024;
	TextImage.m_Format = CImageInfo::FORMAT_RGBA;
	size_t TextImageDataSize = 0;
	if(!ImageDataSizeValid(TextImage, TextImageDataSize))
		return IGraphics::CTextureHandle();
	TextImage.m_pData = static_cast<uint8_t *>(calloc(TextImageDataSize, sizeof(uint8_t)));
	if(TextImage.m_pData == nullptr)
		return IGraphics::CTextureHandle();

	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 0);
	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 1);
	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 2, 255);

	const int TextureLoadFlag = (Graphics()->TextureLoadFlags()) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
	return Graphics()->LoadTextureRawMove(TextImage, TextureLoadFlag);
}

void CMapImages::UpdateEntityLayerText(CImageInfo &TextImage, int TextureSize, int MaxWidth, int YOffset, int NumbersPower, int MaxNumber)
{
	char aBuf[4];
	int DigitsCount = NumbersPower + 1;

	int CurrentNumber = std::pow(10, NumbersPower);

	if(MaxNumber == -1)
		MaxNumber = CurrentNumber * 10 - 1;

	str_format(aBuf, sizeof(aBuf), "%d", CurrentNumber);

	int CurrentNumberSuitableFontSize = TextRender()->AdjustFontSize(aBuf, DigitsCount, TextureSize, MaxWidth);
	int UniversalSuitableFontSize = CurrentNumberSuitableFontSize * 0.92f; // should be smoothed enough to fit any digits combination

	YOffset += ((TextureSize - UniversalSuitableFontSize) / 2);

	for(; CurrentNumber <= MaxNumber; ++CurrentNumber)
	{
		str_format(aBuf, sizeof(aBuf), "%d", CurrentNumber);

		float x = (CurrentNumber % 16) * 64;
		float y = (CurrentNumber / 16) * 64;

		int ApproximateTextWidth = TextRender()->CalculateTextWidth(aBuf, DigitsCount, 0, UniversalSuitableFontSize);
		int XOffSet = (MaxWidth - std::clamp(ApproximateTextWidth, 0, MaxWidth)) / 2;

		TextRender()->UploadEntityLayerText(TextImage, (TextImage.m_Width / 16) - XOffSet, (TextImage.m_Height / 16) - YOffset, aBuf, DigitsCount, x + XOffSet, y + YOffset, UniversalSuitableFontSize);
	}
}

void CMapImages::InitOverlayTextures()
{
	int TextureSize = 64 * m_TextureScale / 100;
	TextureSize = std::clamp(TextureSize, 2, 64);
	int TextureToVerticalCenterOffset = (64 - TextureSize) / 2 + TextureSize * 0.1f; // should be used to move texture to the center of 64 pixels area

	if(!m_OverlayBottomTexture.IsValid())
	{
		m_OverlayBottomTexture = UploadEntityLayerText(TextureSize / 2, 64, 32 + TextureToVerticalCenterOffset / 2);
	}

	if(!m_OverlayTopTexture.IsValid())
	{
		m_OverlayTopTexture = UploadEntityLayerText(TextureSize / 2, 64, TextureToVerticalCenterOffset / 2);
	}

	if(!m_OverlayCenterTexture.IsValid())
	{
		m_OverlayCenterTexture = UploadEntityLayerText(TextureSize, 64, TextureToVerticalCenterOffset);
	}
}
