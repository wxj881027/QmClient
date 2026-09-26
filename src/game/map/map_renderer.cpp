#include "map_renderer.h"

#include <base/log.h>

#include <game/map/envelope_manager.h>

#include <algorithm>

const int LAYER_DEFAULT_TILESET = -1;

void CMapRenderer::Clear()
{
	for(auto &pLayer : m_vpRenderLayers)
		pLayer->Unload();
	m_vpRenderLayers.clear();
	m_pLoadGroup.reset();
	m_pLoadEnvelopeManager.reset();
	m_pLoadLayers = nullptr;
	m_pLoadImages = nullptr;
	m_LoadCallback.reset();
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

void CMapRenderer::LoadStep(int MaxLayers)
{
	if(m_LoadFinished || m_pLoadLayers == nullptr)
		return;

	MaxLayers = maximum(MaxLayers, 1);
	int ProcessedLayers = 0;
	while(ProcessedLayers < MaxLayers && m_LoadGroupId < m_pLoadLayers->NumGroups())
	{
		CMapItemGroup *pGroup = m_pLoadLayers->GetGroup(m_LoadGroupId);
		if(pGroup == nullptr)
		{
			++m_LoadGroupId;
			m_LoadLayerId = 0;
			continue;
		}

		if(!m_pLoadGroup)
		{
			m_pLoadGroup = std::make_unique<CRenderLayerGroup>(m_LoadGroupId, pGroup);
			m_pLoadGroup->OnInit(Graphics(), TextRender(), RenderMap(), m_pLoadEnvelopeManager, m_pLoadLayers->Map(), m_pLoadImages);
			if(!m_pLoadGroup->IsValid())
			{
				log_error("map_renderer", "error group was null, group number = %d, total groups = %d", m_LoadGroupId, m_pLoadLayers->NumGroups());
				m_pLoadGroup.reset();
				++m_LoadGroupId;
				m_LoadLayerId = 0;
				continue;
			}
		}

		if(m_LoadLayerId >= pGroup->m_NumLayers)
		{
			m_pLoadGroup.reset();
			++m_LoadGroupId;
			m_LoadLayerId = 0;
			continue;
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
			++m_LoadCompletedWork;
			if(m_LoadCallback.has_value())
				(*m_LoadCallback)(m_LoadCompletedWork, m_LoadTotalWork);
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
			pRenderLayer = std::make_unique<CRenderLayerQuads>(m_LoadGroupId, m_LoadLayerId, pLayer->m_Flags, (CMapItemLayerQuads *)pLayer);
		}
		if(pRenderLayer)
		{
			pRenderLayer->OnInit(Graphics(), TextRender(), RenderMap(), m_pLoadEnvelopeManager, m_pLoadLayers->Map(), m_pLoadImages);
			if(pRenderLayer->IsValid())
			{
				pRenderLayer->Init();
				m_vpRenderLayers.push_back(std::move(pRenderLayer));
			}
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

void CMapRenderer::Load(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional)
{
	BeginLoad(Type, pLayers, pMapImages, pEnvelopeEval, std::move(CallbackMapRendererInitOptional));
	while(!IsLoadFinished())
		LoadStep(64);
}

void CMapRenderer::Render(const CRenderLayerParams &Params)
{
	float ScreenXLeft, ScreenYTop, ScreenXRight, ScreenYBottom;
	Graphics()->GetScreen(&ScreenXLeft, &ScreenYTop, &ScreenXRight, &ScreenYBottom);

	bool DoRenderGroup = true;
	for(auto &pRenderLayer : m_vpRenderLayers)
	{
		if(pRenderLayer->IsGroup())
			DoRenderGroup = pRenderLayer->DoRender(Params);

		if(!DoRenderGroup)
			continue;

		if(pRenderLayer->DoRender(Params))
			pRenderLayer->Render(Params);
	}

	// Reset clip from last group
	Graphics()->ClipDisable();

	// don't reset screen on background
	if(Params.m_RenderType != ERenderType::RENDERTYPE_BACKGROUND && Params.m_RenderType != ERenderType::RENDERTYPE_BACKGROUND_FORCE)
	{
		// reset the screen like it was before
		Graphics()->MapScreen(ScreenXLeft, ScreenYTop, ScreenXRight, ScreenYBottom);
	}
	else
	{
		// reset the screen to the default interface
		Graphics()->MapScreenToGameInterface(Params.m_Center.x, Params.m_Center.y, Params.m_Zoom);
	}
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
