/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "maplayers.h"

#include <engine/map.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <utility>

CMapLayers::CMapLayers(ERenderType Type, bool OnlineOnly)
{
	m_Type = Type;
	m_OnlineOnly = OnlineOnly;

	// static parameters for ingame rendering
	m_Params.m_RenderType = m_Type;
	m_Params.m_RenderInvalidTiles = false;
	m_Params.m_TileAndQuadBuffering = true;
	m_Params.m_RenderTileBorder = true;
}

void CMapLayers::OnInit()
{
	m_pLayers = Layers();
	m_pImages = &GameClient()->m_MapImages;
	m_MapRenderer.Clear();
	m_MapLoaded = false;
	m_MapLoadInProgress = false;
}

CCamera *CMapLayers::GetCurCamera()
{
	return &GameClient()->m_Camera;
}

void CMapLayers::OnMapLoad()
{
	m_MapLoaded = false;
	m_MapLoadInProgress = false;
	char aCaption[64 + MAX_MAP_LENGTH];
	str_format(aCaption, sizeof(aCaption), "%s: %s", Localize("Loading map"), Client()->GetCurrentMap());
	const bool RenderProgress = m_OnlineOnly;

	FCallbackMapRendererInit ProgressBarCallback = [&](int Completed, int Total) {
		if(RenderProgress)
			GameClient()->m_Menus.RenderLoadingDirect(aCaption, Localize("Initializing layers"), Total > 0 ? std::make_optional(Completed / (float)Total) : std::nullopt);
	};

	if(RenderProgress)
		GameClient()->m_Menus.RenderLoadingDirect(aCaption, Localize("Initializing layers"), 0.0f);

	// can't do that in CMapLayers::OnInit, because some of this interfaces are not available yet
	m_MapRenderer.OnInit(Graphics(), TextRender(), RenderMap());

	m_EnvEvaluator = CEnvelopeState(m_pLayers->Map(), m_OnlineOnly);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
	std::optional<FCallbackMapRendererInit> ProgressCallback;
	if(RenderProgress)
		ProgressCallback = ProgressBarCallback;
	if(m_OnlineOnly)
	{
		m_MapRenderer.Load(m_Type, m_pLayers, m_pImages, &m_EnvEvaluator, ProgressCallback);
		m_MapLoaded = true;
	}
	else
	{
		// 菜单背景的图层和 GPU 资源初始化分帧推进，避免一次性初始化所有图层卡住渲染循环。
		m_MapRenderer.BeginLoad(m_Type, m_pLayers, m_pImages, &m_EnvEvaluator);
		m_MapLoadInProgress = true;
	}
}

void CMapLayers::OnRender()
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!AdvanceMapLoad())
		return;

	// dynamic parameters for ingame rendering
	m_Params.m_EntityOverlayVal = m_Type == RENDERTYPE_FULL_DESIGN ? 0 : g_Config.m_ClOverlayEntities;
	m_Params.m_Center = GetCurCamera()->m_Center;
	m_Params.m_Zoom = GetCurCamera()->m_Zoom;
	m_Params.m_RenderText = g_Config.m_ClTextEntities;
	m_Params.m_DebugRenderGroupClips = g_Config.m_DbgRenderGroupClips;
	m_Params.m_DebugRenderQuadClips = g_Config.m_DbgRenderQuadClips;
	m_Params.m_DebugRenderClusterClips = g_Config.m_DbgRenderClusterClips;
	m_Params.m_DebugRenderTileClips = g_Config.m_DbgRenderTileClips;

	m_MapRenderer.Render(m_Params);
}

bool CMapLayers::AdvanceMapLoad()
{
	if(!m_MapLoaded && m_MapLoadInProgress)
	{
		m_MapRenderer.LoadStep(2);
		if(m_MapRenderer.IsLoadFinished())
		{
			m_MapLoadInProgress = false;
			m_MapLoaded = true;
		}
	}
	return m_MapLoaded;
}

void CMapLayers::RenderCustom(const vec2 &Center, float Zoom)
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!m_MapLoaded)
		return;
	RenderCustomWithCamera(Center, Zoom);
}

void CMapLayers::RenderCustomWithCamera(const vec2 &Center, float Zoom)
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!m_MapLoaded)
		return;

	m_Params.m_EntityOverlayVal = m_Type == RENDERTYPE_FULL_DESIGN ? 0 : g_Config.m_ClOverlayEntities;
	m_Params.m_Center = Center;
	m_Params.m_Zoom = Zoom;
	m_Params.m_RenderText = g_Config.m_ClTextEntities;
	m_Params.m_DebugRenderGroupClips = g_Config.m_DbgRenderGroupClips;
	m_Params.m_DebugRenderQuadClips = g_Config.m_DbgRenderQuadClips;
	m_Params.m_DebugRenderClusterClips = g_Config.m_DbgRenderClusterClips;
	m_Params.m_DebugRenderTileClips = g_Config.m_DbgRenderTileClips;

	m_MapRenderer.Render(m_Params);
}

void CMapLayers::SwapState(CMapLayers &Other)
{
	using std::swap;
	swap(m_pLayers, Other.m_pLayers);
	swap(m_pImages, Other.m_pImages);
	swap(m_Type, Other.m_Type);
	swap(m_OnlineOnly, Other.m_OnlineOnly);
	swap(m_Params, Other.m_Params);
	swap(m_MapRenderer, Other.m_MapRenderer);
	swap(m_EnvEvaluator, Other.m_EnvEvaluator);
	swap(m_MapLoaded, Other.m_MapLoaded);
	swap(m_MapLoadInProgress, Other.m_MapLoadInProgress);
}
