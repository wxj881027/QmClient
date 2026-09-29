/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mapimages.h"

#include <base/log.h>
#include <base/math.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>
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
				if(!Tile.DataSize(TileDataSize) || TileDataSize == 0 || (Tile.m_pData = static_cast<uint8_t *>(malloc(TileDataSize))) == nullptr)
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

CMapImages::CMapImages()
{
	m_Count = 0;
	std::fill(std::begin(m_aEntitiesIsLoaded), std::end(m_aEntitiesIsLoaded), false);
	m_SpeedupArrowIsLoaded = false;
	m_TuneColorsIsLoaded = false;

	str_copy(m_aEntitiesPath, "editor/entities_clear");

	static_assert(std::size(gs_apModEntitiesNames) == MAP_IMAGE_MOD_TYPE_COUNT, "Mod name string count is not equal to mod type count");
}

void CMapImages::OnInit()
{
	m_TextureScale = g_Config.m_ClTextEntitiesSize;
	InitOverlayTextures();

	if(str_comp(g_Config.m_ClAssetsEntities, "default") == 0)
	{
		str_copy(m_aEntitiesPath, "editor/entities_clear");
	}
	else
	{
		str_format(m_aEntitiesPath, sizeof(m_aEntitiesPath), "assets/entities/%s", g_Config.m_ClAssetsEntities);
	}
	// blank 状态与 ChangeEntitiesPath 保持一致（原先仅在 ChangeEntitiesPath 设置，启动期恒为 false）
	m_EntitiesIsBlank = IsBlankAssetName(g_Config.m_ClAssetsEntities);

	Console()->Chain("cl_text_entities_size", ConchainClTextEntitiesSize, this);

	// 空闲预热：后台解码钩子碰撞预览所需的两块 tile，
	// 把 PNG 解码成本挪离设置页首次点击，菜单会话不再加载完整 entities。
	RequestHookPreviewTileTextures();
}

void CMapImages::Unload()
{
	// unload all textures
	for(int i = 0; i < m_Count; i++)
	{
		Graphics()->UnloadTexture(&m_aTextures[i]);
	}
}

void CMapImages::OnMapLoadImpl(class CLayers *pLayers, IMap *pMap)
{
	Unload();

	int Start;
	pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &m_Count);
	m_Count = std::clamp<int>(m_Count, 0, MAX_MAPIMAGES);

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
				const CMapItemLayerQuads *pLayerQuads = reinterpret_cast<const CMapItemLayerQuads *>(pLayer);
				if(pLayerQuads->m_Image >= 0 && pLayerQuads->m_Image < m_Count)
				{
					aTextureUsedByTileOrQuadLayerFlag[pLayerQuads->m_Image] |= 2;
				}
			}
		}
	}

	const int TextureLoadFlag = Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;

	// load new textures
	bool ShowWarning = false;
	for(int i = 0; i < m_Count; i++)
	{
		if(aTextureUsedByTileOrQuadLayerFlag[i] == 0)
		{
			// skip loading unused images
			continue;
		}

		const int LoadFlag = (((aTextureUsedByTileOrQuadLayerFlag[i] & 1) != 0) ? TextureLoadFlag : 0) | (((aTextureUsedByTileOrQuadLayerFlag[i] & 2) != 0) ? 0 : (Graphics()->HasTextureArraysSupport() ? IGraphics::TEXLOAD_NO_2D_TEXTURE : 0));
		const CMapItemImage_v2 *pImg = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(Start + i));

		const char *pName = pMap->GetDataString(pImg->m_ImageName);
		if(pName == nullptr || pName[0] == '\0')
		{
			if(pImg->m_External)
			{
				log_error("mapimages", "Failed to load map image %d: failed to load name.", i);
				ShowWarning = true;
				continue;
			}
			pName = "(error)";
		}

		if(pImg->m_Version > 1 && pImg->m_MustBe1 != 1)
		{
			log_error("mapimages", "Failed to load map image %d '%s': invalid map image type.", i, pName);
			ShowWarning = true;
			continue;
		}

		if(pImg->m_External)
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
			m_aTextures[i] = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL, LoadFlag);
		}
		else
		{
			if(pImg->m_Width <= 0 || pImg->m_Height <= 0)
			{
				log_error("mapimages", "Failed to load map image %d '%s': invalid image dimensions.", i, pName);
				ShowWarning = true;
				continue;
			}

			CImageInfo ImageInfo;
			ImageInfo.m_Width = pImg->m_Width;
			ImageInfo.m_Height = pImg->m_Height;
			ImageInfo.m_Format = CImageInfo::FORMAT_RGBA;
			ImageInfo.m_pData = static_cast<uint8_t *>(pMap->GetData(pImg->m_ImageData));
			size_t ImageDataSize = 0;
			const int FileDataSize = pMap->GetDataSize(pImg->m_ImageData);
			if(ImageInfo.m_pData && FileDataSize >= 0 && ImageInfo.DataSize(ImageDataSize) && (size_t)FileDataSize >= ImageDataSize)
			{
				char aTexName[IO_MAX_PATH_LENGTH];
				str_format(aTexName, sizeof(aTexName), "embedded: %s", pName);
				m_aTextures[i] = Graphics()->LoadTextureRaw(ImageInfo, LoadFlag, aTexName);
				pMap->UnloadData(pImg->m_ImageData);
			}
			else
			{
				pMap->UnloadData(pImg->m_ImageData);
				log_error("mapimages", "Failed to load map image %d: failed to load data.", i);
				ShowWarning = true;
				continue;
			}
		}
		pMap->UnloadData(pImg->m_ImageName);
		ShowWarning = ShowWarning || m_aTextures[i].IsNullTexture();
	}
	if(ShowWarning)
	{
		Client()->AddWarning(SWarning(Localize("Some map images could not be loaded. Check the local console for details.")));
	}
}

void CMapImages::OnMapLoad()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	CLayers *pLayers = GameClient()->Layers();
	OnMapLoadImpl(pLayers, pMap);
}

void CMapImages::LoadBackground(class CLayers *pLayers, class IMap *pMap)
{
	OnMapLoadImpl(pLayers, pMap);
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

IGraphics::CTextureHandle CMapImages::GetEntities(EMapImageEntityLayerType EntityLayerType)
{
	const bool EntitiesAreMasked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const EMapImageModType EntitiesModType = GetEntitiesModType(GameClient()->m_GameInfo);

	if(!m_aEntitiesIsLoaded[(EntitiesModType * 2) + (int)EntitiesAreMasked])
	{
		m_aEntitiesIsLoaded[(EntitiesModType * 2) + (int)EntitiesAreMasked] = true;

		int TextureLoadFlag = 0;
		if(Graphics()->HasTextureArraysSupport())
			TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;

		CImageInfo ImgInfo;
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.png", m_aEntitiesPath, gs_apModEntitiesNames[EntitiesModType]);
		// 先探测存在性再解码：缺失路径不进 LoadPng，避免每次会话刷无害报错
		if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);

		// try as single ddnet replacement
		if(ImgInfo.m_pData == nullptr && EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET)
		{
			str_format(aPath, sizeof(aPath), "%s.png", m_aEntitiesPath);
			if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
				Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
		}

		// try default
		if(ImgInfo.m_pData == nullptr)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[EntitiesModType]);
			if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
				Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
		}

		// 选中内置空白材质 "blank"：按默认实体图的尺寸与格式解码后整张清空，
		// 下方按格切块得到的各实体层就是全透明（显式留空，不参与 qm_blank_asset_fallback）。
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
	}

	return m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][EntityLayerType];
}

IGraphics::CTextureHandle CMapImages::GetSpeedupArrow()
{
	if(!m_SpeedupArrowIsLoaded)
	{
		int TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
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
	m_EntitiesIsBlank = IsBlankAssetName(pPath);
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(m_aEntitiesPath, "editor/entities_clear");
	}
	else
	{
		str_format(m_aEntitiesPath, sizeof(m_aEntitiesPath), "assets/entities/%s", pPath);
	}

	ReloadEntitiesTextures();
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
	if(!m_HookPreviewJobPending || m_pHookPreviewJob == nullptr || !m_pHookPreviewJob->Done())
		return;

	std::shared_ptr<IJob> pJob = std::move(m_pHookPreviewJob);
	m_HookPreviewJobPending = false;
	auto *pDecode = static_cast<CHookPreviewDecodeJob *>(pJob.get());

	const bool Masked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const int ModType = GetEntitiesModType(GameClient()->m_GameInfo);
	// 发布前校验 key：解码期间实体包或游戏信息可能已变化，过期结果直接丢弃
	if(pDecode->m_ModType != ModType || pDecode->m_Masked != Masked ||
		str_comp(pDecode->m_aEntitiesPath, m_aEntitiesPath) != 0)
	{
		pDecode->FreeTiles();
		return;
	}
	if(!pDecode->m_Success)
	{
		// 包缺失/损坏：保持失败状态，key 变化前不再重试
		m_HookPreviewFailed = true;
		pDecode->FreeTiles();
		return;
	}

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

	const int TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
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
