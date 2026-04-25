#include "background.h"
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <engine/shared/config.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>

namespace
{
bool TryMigrateLegacyEntityBgMapPath(IStorage *pStorage, const char *pManagedPath)
{
	if(pStorage == nullptr || pManagedPath == nullptr || !str_endswith_nocase(pManagedPath, ".map"))
		return false;
	if(pStorage->FileExists(pManagedPath, IStorage::TYPE_ALL))
		return true;

	char aLegacyPath[IO_MAX_PATH_LENGTH];
	str_copy(aLegacyPath, pManagedPath, sizeof(aLegacyPath));
	aLegacyPath[str_length(aLegacyPath) - 4] = '\0';

	static constexpr const char *s_apLegacyExtensions[] = {
		".png",
		".webp",
		".jpg",
		".jpeg",
	};

	for(const char *pExtension : s_apLegacyExtensions)
	{
		char aCandidatePath[IO_MAX_PATH_LENGTH];
		str_format(aCandidatePath, sizeof(aCandidatePath), "%s%s", aLegacyPath, pExtension);
		if(pStorage->FileExists(aCandidatePath, IStorage::TYPE_SAVE))
		{
			if(pStorage->RenameFile(aCandidatePath, pManagedPath, IStorage::TYPE_SAVE))
				return true;
		}
	}

	return pStorage->FileExists(pManagedPath, IStorage::TYPE_ALL);
}

void ResolveBackgroundEntitiesStoragePath(IStorage *pStorage, const char *pBackgroundEntities, bool IsImageFile, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(pBackgroundEntities == nullptr || pBackgroundEntities[0] == '\0')
		return;

	const char *pExtension = IsImageFile ? ".png" : ".map";
	if(str_startswith_nocase(pBackgroundEntities, "entity_bg/"))
	{
		char aManagedPath[IO_MAX_PATH_LENGTH];
		char aMapPath[IO_MAX_PATH_LENGTH];
		str_format(aManagedPath, sizeof(aManagedPath), "assets/%s%s", pBackgroundEntities, str_endswith(pBackgroundEntities, pExtension) ? "" : pExtension);
		str_format(aMapPath, sizeof(aMapPath), "maps/%s%s", pBackgroundEntities, str_endswith(pBackgroundEntities, pExtension) ? "" : pExtension);

		const bool ManagedExists = pStorage != nullptr && (IsImageFile ? pStorage->FileExists(aManagedPath, IStorage::TYPE_ALL) : TryMigrateLegacyEntityBgMapPath(pStorage, aManagedPath));
		const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, IStorage::TYPE_ALL);
		if(ManagedExists || !MapExists)
			str_copy(pOut, aManagedPath, OutSize);
		else
			str_copy(pOut, aMapPath, OutSize);
	}
	else
	{
		str_format(pOut, OutSize, "maps/%s%s", pBackgroundEntities, str_endswith(pBackgroundEntities, pExtension) ? "" : pExtension);
	}
}
}

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
	ClearImageBackground(false);
	delete m_pBackgroundLayers;
	delete m_pBackgroundImages;
}

CBackgroundEngineMap *CBackground::CreateBGMap()
{
	return new CBackgroundEngineMap;
}

void CBackground::ClearImageBackground(bool UnloadTexture)
{
	if(UnloadTexture && m_BackgroundTexture.IsValid())
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
	if(!IsDefaultBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities) && !IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities))
		LoadBackground();
}

void CBackground::OnShutdown()
{
	ClearImageBackground();
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

	char aBackgroundEntities[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities, aBackgroundEntities, sizeof(aBackgroundEntities));

	str_copy(m_aMapName, aBackgroundEntities);
	const char *pBackgroundEntities = aBackgroundEntities;
	if(pBackgroundEntities[0] != '\0')
	{
		bool NeedImageLoading = false;

		char aBuf[IO_MAX_PATH_LENGTH];
		if(str_comp(pBackgroundEntities, CURRENT_MAP) == 0)
		{
			m_pMap = Kernel()->RequestInterface<IEngineMap>();
			if(m_pMap->IsLoaded())
			{
				m_pLayers = GameClient()->Layers();
				m_pImages = &GameClient()->m_MapImages;
				m_Loaded = true;
			}
		}
		else if(str_endswith_nocase(pBackgroundEntities, ".png"))
		{
			ResolveBackgroundEntitiesStoragePath(Storage(), pBackgroundEntities, true, aBuf, sizeof(aBuf));
			LoadImageBackground(aBuf);
		}
		else
		{
			ResolveBackgroundEntitiesStoragePath(Storage(), pBackgroundEntities, false, aBuf, sizeof(aBuf));
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
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities, aNormalized, sizeof(aNormalized));
	if(str_comp(aNormalized, CURRENT_MAP) == 0 || str_comp(aNormalized, m_aMapName))
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

bool CBackground::RenderCustom(const vec2 &Center, float Zoom)
{
	if(!m_Loaded)
		return false;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	if(g_Config.m_ClOverlayEntities != 100)
		return false;

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
		return true;
	}

	CMapLayers::RenderCustom(Center, Zoom);
	return true;
}
