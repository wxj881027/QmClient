#include "background.h"
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>

CBackground::CBackground(ERenderType MapType, bool OnlineOnly) :
	CMapLayers(MapType, OnlineOnly)
{
	m_pLayers = new CLayers;
	m_pBackgroundLayers = m_pLayers;
	m_pImages = new CMapImages;
	m_pBackgroundImages = m_pImages;
	m_Loaded = false;
	m_ImageBackground = false;
	m_aMapName[0] = '\0';
	m_BackgroundTexture.Invalidate();
}

CBackground::~CBackground()
{
	ClearImageBackground();
	delete m_pBackgroundLayers;
	delete m_pBackgroundImages;
}

CBackgroundEngineMap *CBackground::CreateBGMap()
{
	return new CBackgroundEngineMap;
}

void CBackground::ClearImageBackground()
{
	if(m_BackgroundTexture.IsValid())
		Graphics()->UnloadTexture(&m_BackgroundTexture);
	m_BackgroundTexture.Invalidate();
	m_ImageBackground = false;
}

bool CBackground::LoadImageBackground(const char *pPath)
{
	ClearImageBackground();
	m_BackgroundTexture = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL);
	if(m_BackgroundTexture.IsNullTexture())
	{
		m_BackgroundTexture.Invalidate();
		return false;
	}
	m_ImageBackground = true;
	m_Loaded = true;
	return true;
}

void CBackground::OnInit()
{
	m_pBackgroundMap = CreateBGMap();
	m_pMap = m_pBackgroundMap;

	m_pImages->OnInterfacesInit(GameClient());
	Kernel()->RegisterInterface(m_pBackgroundMap);
	if(g_Config.m_ClBackgroundEntities[0] != '\0' && str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP))
		LoadBackground();
}

void CBackground::LoadBackground()
{
	if(m_Loaded && !m_ImageBackground && m_pMap == m_pBackgroundMap)
		m_pMap->Unload();

	ClearImageBackground();
	m_Loaded = false;
	m_pMap = m_pBackgroundMap;
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;

	str_copy(m_aMapName, g_Config.m_ClBackgroundEntities);
	if(g_Config.m_ClBackgroundEntities[0] != '\0')
	{
		bool NeedImageLoading = false;

		char aBuf[IO_MAX_PATH_LENGTH];
		if(str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0)
		{
			m_pMap = Kernel()->RequestInterface<IEngineMap>();
			if(m_pMap->IsLoaded())
			{
				m_pLayers = GameClient()->Layers();
				m_pImages = &GameClient()->m_MapImages;
				m_Loaded = true;
			}
		}
		else if(str_endswith_nocase(g_Config.m_ClBackgroundEntities, ".png"))
		{
			str_format(aBuf, sizeof(aBuf), "maps/%s", g_Config.m_ClBackgroundEntities);
			LoadImageBackground(aBuf);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "maps/%s%s", g_Config.m_ClBackgroundEntities, str_endswith(g_Config.m_ClBackgroundEntities, ".map") ? "" : ".map");
			if(m_pMap->Load(aBuf))
			{
				m_pLayers->Init(m_pMap, true);
				NeedImageLoading = true;
				m_Loaded = true;
			}
		}

		if(m_Loaded && !m_ImageBackground)
		{
			if(NeedImageLoading)
			{
				m_pImages->LoadBackground(m_pLayers, m_pMap);
			}
			CMapLayers::OnMapLoad();
		}
	}
}

void CBackground::OnMapLoad()
{
	if(str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0 || str_comp(g_Config.m_ClBackgroundEntities, m_aMapName))
	{
		LoadBackground();
	}
}

void CBackground::OnRender()
{
	if(!m_Loaded)
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClOverlayEntities != 100)
		return;

	if(m_ImageBackground)
	{
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float ScreenHeight = 300.0f;
		const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
		Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);
		Graphics()->TextureSet(m_BackgroundTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		const IGraphics::CQuadItem QuadItem(0.0f, 0.0f, ScreenWidth, ScreenHeight);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		return;
	}

	CMapLayers::OnRender();
}
