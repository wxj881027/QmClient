/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "maplayers.h"

#include <base/perf_timer.h>

#include <engine/map.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <cinttypes>
#include <utility>

CMapLayers::CMapLayers(ERenderType Type, bool OnlineOnly)
{
	m_Type = Type;
	m_OnlineOnly = OnlineOnly;

	// static parameters for ingame rendering
	m_Params.m_RenderType = m_Type;
	m_Params.m_RenderInvalidTiles = false;
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
	m_LoadLayerCpuMs = 0.0;
	m_LoadLayerPeakMs = 0.0;
	m_LoadLayerPeakFrame = 0;
	m_LoadLayerSlices = 0;
	m_MapLoaded = false;
	m_MapLoadInProgress = false;
	char aCaption[64 + MAX_MAP_LENGTH];
	str_format(aCaption, sizeof(aCaption), "%s: %s", Localize("Loading map"), Client()->GetCurrentMap());
	const bool LiveConnection = m_OnlineOnly && Client()->State() == IClient::STATE_LOADING;
	const bool RenderProgress = m_OnlineOnly && !LiveConnection;

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
	if(m_OnlineOnly && !LiveConnection)
	{
		m_MapRenderer.Load(m_Type, m_pLayers, m_pImages, &m_EnvEvaluator, ProgressCallback, true);
		m_MapLoaded = true;
	}
	else
	{
		// 连接与菜单背景均分帧准备；不保存引用局部加载文案的回调，避免下一帧访问悬空引用。
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

bool CMapLayers::AdvanceMapLoad(int MaxLayers, CMapLoadTask::TClock::time_point Deadline)
{
	if(!m_MapLoaded && m_MapLoadInProgress)
	{
		const bool Trace = Deadline != CMapLoadTask::TClock::time_point::max() && QmPerfEnabled();
		CPerfTimer Timer(Trace);
		CMapRenderer::SLoadProfile Profile;
		m_MapRenderer.LoadStep(MaxLayers, Deadline, Trace ? &Profile : nullptr);
		if(Trace && Profile.m_Group >= 0)
		{
			const double SliceMs = Timer.ElapsedMs();
			m_LoadLayerCpuMs += SliceMs;
			if(SliceMs >= m_LoadLayerPeakMs)
			{
				m_LoadLayerPeakMs = SliceMs;
				m_LoadLayerPeakFrame = Client()->PerfFrame();
			}
			++m_LoadLayerSlices;
			if(Profile.m_Complete || QmPerfShouldLogDuration(SliceMs))
			{
				char aExtra[256];
				str_format(aExtra, sizeof(aExtra), "pass=%s group=%d layer=%d type=%d width=%d height=%d quads=%d slices=%u peak_ms=%.3f peak_frame=%" PRIu64,
					m_Type == ERenderType::RENDERTYPE_FOREGROUND ? "foreground" : "background",
					Profile.m_Group, Profile.m_Layer, Profile.m_Type, Profile.m_Width, Profile.m_Height, Profile.m_Quads, m_LoadLayerSlices, m_LoadLayerPeakMs, m_LoadLayerPeakFrame);
				QmPerfLogStage("perf/connect", Profile.m_Complete ? "map_layer_prepare" : "map_layer_prepare_slice",
					Profile.m_Complete ? m_LoadLayerCpuMs : SliceMs, true, Client(), nullptr, nullptr, aExtra);
			}
		}
		if(Profile.m_Complete || !Trace)
		{
			m_LoadLayerCpuMs = 0.0;
			m_LoadLayerPeakMs = 0.0;
			m_LoadLayerPeakFrame = 0;
			m_LoadLayerSlices = 0;
		}
		if(m_MapRenderer.IsLoadFinished())
		{
			m_MapLoadInProgress = false;
			m_MapLoaded = true;
		}
	}
	return m_MapLoaded;
}

void CMapLayers::UnloadMap()
{
	m_MapRenderer.Clear();
	m_MapLoaded = false;
	m_MapLoadInProgress = false;
}

void CMapLayers::RenderCustom(const vec2 &Center, float Zoom)
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!m_MapLoaded)
		return;
	RenderCustomWithCamera(Center, Zoom);
}

void CMapLayers::RenderCustomWithCamera(const vec2 &Center, float Zoom, std::optional<ERenderType> RenderType)
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	RenderWithCamera(Center, Zoom, RenderType);
}

void CMapLayers::RenderWithCamera(const vec2 &Center, float Zoom, std::optional<ERenderType> RenderType)
{
	if(!m_MapLoaded)
		return;

	const int PreviousType = m_Params.m_RenderType;
	m_Params.m_RenderType = RenderType.value_or(m_Type);
	m_Params.m_EntityOverlayVal = m_EntityOverlayValOverride >= 0 ?
					      m_EntityOverlayValOverride :
					      (m_Type == RENDERTYPE_FULL_DESIGN ? 0 : g_Config.m_ClOverlayEntities);
	m_Params.m_Center = Center;
	m_Params.m_Zoom = Zoom;
	m_Params.m_RenderText = g_Config.m_ClTextEntities;
	m_Params.m_DebugRenderGroupClips = g_Config.m_DbgRenderGroupClips;
	m_Params.m_DebugRenderQuadClips = g_Config.m_DbgRenderQuadClips;
	m_Params.m_DebugRenderClusterClips = g_Config.m_DbgRenderClusterClips;
	m_Params.m_DebugRenderTileClips = g_Config.m_DbgRenderTileClips;

	m_MapRenderer.Render(m_Params);
	m_Params.m_RenderType = PreviousType;
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
	swap(m_LoadLayerCpuMs, Other.m_LoadLayerCpuMs);
	swap(m_LoadLayerPeakMs, Other.m_LoadLayerPeakMs);
	swap(m_LoadLayerPeakFrame, Other.m_LoadLayerPeakFrame);
	swap(m_LoadLayerSlices, Other.m_LoadLayerSlices);
}
