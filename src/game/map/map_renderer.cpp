#include "map_renderer.h"

#include "render_screen.h"

#include <base/log.h>

#include <game/map/envelope_manager.h>
#include <game/map/map_render_items.h>

#include <algorithm>

const int LAYER_DEFAULT_TILESET = -1;

void CMapRenderer::Clear()
{
	m_LayerTask = {};
	if(m_pPreparingLayer)
	{
		m_pPreparingLayer->Unload();
		m_pPreparingLayer.reset();
	}
	for(auto &pLayer : m_vpRenderLayers)
		pLayer->Unload();
	m_vpRenderLayers.clear();
	m_pFirstWorldTile = nullptr;
	m_pLoadGroup.reset();
	m_pLoadEnvelopeManager.reset();
	m_pLoadLayers = nullptr;
	m_pLoadImages = nullptr;
	m_LoadCallback.reset();
	m_GameGroupId = -1;
	m_GameLayerId = -1;
	m_LoadFinished = true;
}

void CMapRenderer::BeginLoad(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional)
{
	Clear();
	m_LoadType = Type;
	m_pLoadLayers = pLayers;
	m_pLoadImages = pMapImages;
	m_pLoadEnvelopeManager = std::make_shared<CEnvelopeManager>(pEnvelopeEval, pLayers->Map());
	m_LoadCallback = std::move(CallbackMapRendererInitOptional);
	m_LoadGroupId = 0;
	m_LoadLayerId = 0;
	m_LoadCompletedWork = 0;
	m_LoadPassedGameLayer = false;
	m_LoadFinished = false;

	m_LoadTotalWork = 0;
	bool CountPassedGameLayer = false;
	for(int GroupId = 0; GroupId < pLayers->NumGroups(); ++GroupId)
	{
		CMapItemGroup *pGroup = pLayers->GetGroup(GroupId);
		for(int LayerId = 0; LayerId < pGroup->m_NumLayers; ++LayerId)
		{
			const int LayerType = GetLayerType(pLayers->GetLayer(pGroup->m_StartLayer + LayerId));
			if(LayerType == LAYER_GAME && m_GameGroupId < 0)
			{
				m_GameGroupId = GroupId;
				m_GameLayerId = LayerId;
			}
			CountPassedGameLayer |= LayerType == LAYER_GAME;
			if((Type == ERenderType::RENDERTYPE_BACKGROUND || Type == ERenderType::RENDERTYPE_BACKGROUND_FORCE) && CountPassedGameLayer)
				break;
			if(Type == ERenderType::RENDERTYPE_FOREGROUND && !CountPassedGameLayer)
				continue;
			++m_LoadTotalWork;
		}
		if((Type == ERenderType::RENDERTYPE_BACKGROUND || Type == ERenderType::RENDERTYPE_BACKGROUND_FORCE) && CountPassedGameLayer)
			break;
	}
	if(m_LoadTotalWork == 0 && m_LoadCallback.has_value())
		(*m_LoadCallback)(1, 1);
}

void CMapRenderer::LoadStep(int MaxLayers, CMapLoadTask::TClock::time_point Deadline, SLoadProfile *pProfile)
{
	if(m_LoadFinished || m_pLoadLayers == nullptr)
		return;
	if(m_pLoadLayers->Map() == nullptr)
	{
		m_LoadFinished = true;
		return;
	}

	MaxLayers = maximum(MaxLayers, 1);
	int LayersStart, LayersCount;
	m_pLoadLayers->Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersCount);
	int ProcessedLayers = 0;
	// 首轮至少推进一个位置，后续扫描都检查截止时间。
	for(bool WithinDeadline = true;
		WithinDeadline && ProcessedLayers < MaxLayers && m_LoadGroupId < m_pLoadLayers->NumGroups();
		WithinDeadline = Deadline == CMapLoadTask::TClock::time_point::max() || CMapLoadTask::TClock::now() < Deadline)
	{
		CMapItemGroup *pGroup = m_pLoadLayers->GetGroup(m_LoadGroupId);
		if(pGroup == nullptr || m_LoadLayerId >= pGroup->m_NumLayers)
		{
			m_pLoadGroup.reset();
			++m_LoadGroupId;
			m_LoadLayerId = 0;
			continue;
		}

		if(!m_pPreparingLayer)
		{
			if(!m_pLoadGroup)
			{
				m_pLoadGroup = std::make_unique<CRenderLayerGroup>(m_LoadGroupId, pGroup);
				m_pLoadGroup->OnInit(Graphics(), TextRender(), RenderMap(), m_pLoadEnvelopeManager, m_pLoadLayers->Map(), m_pLoadImages, m_TileAndQuadBuffering);
				if(!m_pLoadGroup->IsValid())
				{
					log_error("map_renderer", "error group was null, group number = %d, total groups = %d", m_LoadGroupId, m_pLoadLayers->NumGroups());
					m_pLoadGroup.reset();
					++m_LoadGroupId;
					m_LoadLayerId = 0;
					continue;
				}
			}

			CMapItemLayer *pLayer = m_pLoadLayers->GetLayer(pGroup->m_StartLayer + m_LoadLayerId);
			const int LayerType = GetLayerType(pLayer);
			m_LoadPassedGameLayer |= LayerType == LAYER_GAME;
			if((m_LoadType == ERenderType::RENDERTYPE_BACKGROUND_FORCE || m_LoadType == ERenderType::RENDERTYPE_BACKGROUND) && m_LoadPassedGameLayer)
			{
				m_pLoadGroup.reset();
				m_LoadFinished = true;
				return;
			}
			if(m_LoadType == ERenderType::RENDERTYPE_FOREGROUND && !m_LoadPassedGameLayer)
			{
				++m_LoadLayerId;
				++ProcessedLayers;
				continue;
			}

			if(m_pLoadGroup)
			{
				m_vpRenderLayers.push_back(std::move(m_pLoadGroup));
			}

			std::unique_ptr<CRenderLayer> pRenderLayer;
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTileLayer = (CMapItemLayerTilemap *)pLayer;
				switch(LayerType)
				{
				case LAYER_DEFAULT_TILESET: pRenderLayer = std::make_unique<CRenderLayerTile>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_GAME: pRenderLayer = std::make_unique<CRenderLayerEntityGame>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_FRONT: pRenderLayer = std::make_unique<CRenderLayerEntityFront>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_TELE: pRenderLayer = std::make_unique<CRenderLayerEntityTele>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_SPEEDUP: pRenderLayer = std::make_unique<CRenderLayerEntitySpeedup>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_SWITCH: pRenderLayer = std::make_unique<CRenderLayerEntitySwitch>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				case LAYER_TUNE: pRenderLayer = std::make_unique<CRenderLayerEntityTune>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pTileLayer); break;
				default: dbg_assert_failed("Unknown LayerType %d", LayerType);
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				auto *pQuadLayer = MapRenderItems::QuadLayer(*m_pLoadLayers->Map(), LayersStart + pGroup->m_StartLayer + m_LoadLayerId);
				if(pQuadLayer != nullptr)
					pRenderLayer = std::make_unique<CRenderLayerQuads>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, pQuadLayer);
			}
			if(pRenderLayer)
			{
				pRenderLayer->OnInit(Graphics(), TextRender(), RenderMap(), m_pLoadEnvelopeManager, m_pLoadLayers->Map(), m_pLoadImages, m_TileAndQuadBuffering);
				if(pRenderLayer->IsValid())
				{
					if(!m_pFirstWorldTile && pLayer->m_Type == LAYERTYPE_TILES && pGroup->m_ParallaxX == 100 && pGroup->m_ParallaxY == 100)
						m_pFirstWorldTile = pRenderLayer.get();
					m_LayerTask = pRenderLayer->Prepare();
					m_pPreparingLayer = std::move(pRenderLayer);
				}
			}
		}
		if(m_pPreparingLayer)
		{
			if(pProfile)
			{
				const CMapItemLayer *pLayer = m_pLoadLayers->GetLayer(pGroup->m_StartLayer + m_LoadLayerId);
				pProfile->m_Group = m_LoadGroupId;
				pProfile->m_Layer = m_LoadLayerId;
				pProfile->m_Type = pLayer->m_Type;
				if(pLayer->m_Type == LAYERTYPE_TILES)
				{
					const auto *pTiles = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
					pProfile->m_Width = pTiles->m_Width;
					pProfile->m_Height = pTiles->m_Height;
				}
				else if(pLayer->m_Type == LAYERTYPE_QUADS)
					pProfile->m_Quads = reinterpret_cast<const CMapItemLayerQuads *>(pLayer)->m_NumQuads;
			}
			if(Deadline == CMapLoadTask::TClock::time_point::max())
				m_LayerTask.Finish();
			else if(!m_LayerTask.RunUntil(Deadline))
				return;

			m_LayerTask = {};
			m_vpRenderLayers.push_back(std::move(m_pPreparingLayer));
			if(pProfile)
				pProfile->m_Complete = true;
		}

		++m_LoadLayerId;
		++m_LoadCompletedWork;
		if(m_LoadCallback.has_value())
			(*m_LoadCallback)(m_LoadCompletedWork, m_LoadTotalWork);
		++ProcessedLayers;
	}

	if(m_LoadGroupId >= m_pLoadLayers->NumGroups())
	{
		m_pLoadGroup.reset();
		m_LoadFinished = true;
	}
}

void CMapRenderer::Load(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional, bool TileAndQuadBuffering)
{
	m_TileAndQuadBuffering = TileAndQuadBuffering;
	BeginLoad(Type, pLayers, pMapImages, pEnvelopeEval, std::move(CallbackMapRendererInitOptional));
	while(!IsLoadFinished())
		LoadStep(64);
}

void CMapRenderer::Render(const CRenderLayerParams &Params, const std::function<void()> &BeforeWorldTiles)
{
	std::array<float, 4> aRestoreScreen;
	Graphics()->GetScreen(&aRestoreScreen[0], &aRestoreScreen[1], &aRestoreScreen[2], &aRestoreScreen[3]);

	bool DoRenderGroup = true;
	CRenderLayer *pGroup = nullptr;
	bool DrewBeforeTiles = false;
	for(auto &pRenderLayer : m_vpRenderLayers)
	{
		if(!MapRenderLayerInPass(Params.m_RenderType, pRenderLayer->GetGroup(), pRenderLayer->IsGroup() ? -1 : pRenderLayer->GetLayer(), m_GameGroupId, m_GameLayerId))
			continue;
		if(pRenderLayer->IsGroup())
		{
			pGroup = pRenderLayer.get();
			DoRenderGroup = pRenderLayer->DoRender(Params);
		}

		if(BeforeWorldTiles && pRenderLayer.get() == m_pFirstWorldTile)
		{
			Graphics()->ClipDisable();
			Graphics()->MapScreenToGameInterface(Params.m_Center.x, Params.m_Center.y, Params.m_Zoom);
			BeforeWorldTiles();
			DrewBeforeTiles = true;
			if(pGroup && (DoRenderGroup = pGroup->DoRender(Params)))
				pGroup->Render(Params);
		}

		if(!DoRenderGroup)
			continue;

		if(pRenderLayer->DoRender(Params))
			pRenderLayer->Render(Params);
	}

	// Reset clip from last group
	Graphics()->ClipDisable();

	if(BeforeWorldTiles && !DrewBeforeTiles)
	{
		Graphics()->MapScreenToGameInterface(Params.m_Center.x, Params.m_Center.y, Params.m_Zoom);
		BeforeWorldTiles();
	}

	if(Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND || Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND_FORCE)
	{
		std::array<float, 4> aInterface;
		Graphics()->MapScreenToWorld(Params.m_Center.x, Params.m_Center.y, 100.0f, 100.0f, 100.0f,
			0, 0, Graphics()->GameScreenAspect(), Params.m_Zoom, aInterface.data());
		if(MapRenderScreen::IsValidScreen(aInterface))
			aRestoreScreen = aInterface;
	}
	// 背景目标无效时回到进入渲染器时的有效视口，避免收尾再次发布坏投影。
	if(MapRenderScreen::IsValidScreen(aRestoreScreen))
		Graphics()->MapScreen(aRestoreScreen[0], aRestoreScreen[1], aRestoreScreen[2], aRestoreScreen[3]);
}

int CMapRenderer::GetLayerType(const CMapItemLayer *pLayer) const
{
	if(pLayer->m_Type != LAYERTYPE_TILES)
		return LAYER_DEFAULT_TILESET;

	const int Flags = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer)->m_Flags;
	if(Flags & TILESLAYERFLAG_GAME)
		return LAYER_GAME;
	else if(Flags & TILESLAYERFLAG_FRONT)
		return LAYER_FRONT;
	else if(Flags & TILESLAYERFLAG_SWITCH)
		return LAYER_SWITCH;
	else if(Flags & TILESLAYERFLAG_TELE)
		return LAYER_TELE;
	else if(Flags & TILESLAYERFLAG_SPEEDUP)
		return LAYER_SPEEDUP;
	else if(Flags & TILESLAYERFLAG_TUNE)
		return LAYER_TUNE;
	return LAYER_DEFAULT_TILESET;
}
