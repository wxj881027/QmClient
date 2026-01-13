#include "menu_background.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/client/components/camera.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;

std::array<vec2, CMenuBackground::NUM_POS> GenerateMenuBackgroundPositions()
{
	std::array<vec2, CMenuBackground::NUM_POS> Positions;

	Positions[CMenuBackground::POS_START] = vec2(500.0f, 500.0f);
	Positions[CMenuBackground::POS_BROWSER_INTERNET] = vec2(1000.0f, 1000.0f);
	Positions[CMenuBackground::POS_BROWSER_LAN] = vec2(1100.0f, 1000.0f);
	Positions[CMenuBackground::POS_DEMOS] = vec2(900.0f, 100.0f);
	Positions[CMenuBackground::POS_NEWS] = vec2(500.0f, 750.0f);
	Positions[CMenuBackground::POS_BROWSER_FAVORITES] = vec2(1250.0f, 500.0f);
	Positions[CMenuBackground::POS_SETTINGS_LANGUAGE] = vec2(500.0f, 1200.0f);
	Positions[CMenuBackground::POS_SETTINGS_GENERAL] = vec2(500.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_PLAYER] = vec2(600.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_TEE] = vec2(700.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_APPEARANCE] = vec2(200.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_CONTROLS] = vec2(800.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_GRAPHICS] = vec2(900.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_SOUND] = vec2(1000.0f, 1000.0f);
	Positions[CMenuBackground::POS_SETTINGS_DDNET] = vec2(1200.0f, 200.0f);
	Positions[CMenuBackground::POS_SETTINGS_ASSETS] = vec2(500.0f, 500.0f);
	for(int i = 0; i < CMenuBackground::POS_BROWSER_CUSTOM_NUM; ++i)
		Positions[CMenuBackground::POS_BROWSER_CUSTOM0 + i] = vec2(500.0f + (75.0f * (float)i), 650.0f - (75.0f * (float)i));
	for(int i = 0; i < CMenuBackground::POS_SETTINGS_RESERVED_NUM; ++i)
		Positions[CMenuBackground::POS_SETTINGS_RESERVED0 + i] = vec2(0, 0);
	for(int i = 0; i < CMenuBackground::POS_RESERVED_NUM; ++i)
		Positions[CMenuBackground::POS_RESERVED0 + i] = vec2(0, 0);

	return Positions;
}

CMenuBackground::CMenuBackground() :
	CBackground(ERenderType::RENDERTYPE_FULL_DESIGN, false)
{
	m_RotationCenter = vec2(0.0f, 0.0f);
	m_AnimationStartPos = vec2(0.0f, 0.0f);
	m_Camera.m_Center = vec2(0.0f, 0.0f);
	m_Camera.m_PrevCenter = vec2(0.0f, 0.0f); // unused in this class
	m_ChangedPosition = false;

	ResetPositions();

	m_CurrentPosition = -1;
	m_MoveTime = 0.0f;

	m_IsInit = false;
	m_Loading = false;
}

CBackgroundEngineMap *CMenuBackground::CreateBGMap()
{
	return new CMenuMap;
}

void CMenuBackground::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);
	m_pImages->OnInterfacesInit(pClient);
	m_Camera.OnInterfacesInit(pClient);
}

void CMenuBackground::OnInit()
{
	m_pBackgroundMap = CreateBGMap();
	m_pMap = m_pBackgroundMap;

	m_IsInit = true;

	Kernel()->RegisterInterface<CMenuMap>((CMenuMap *)m_pBackgroundMap);
	if(g_Config.m_ClMenuMap[0] != '\0')
		LoadMenuBackground();

	m_Camera.m_ZoomSet = false;
	m_Camera.m_ZoomSmoothingTarget = 0;
}

void CMenuBackground::ResetPositions()
{
	m_aPositions = GenerateMenuBackgroundPositions();
}

void CMenuBackground::LoadThemeIcon(CTheme &Theme)
{
	char aIconPath[IO_MAX_PATH_LENGTH];
	const char *pName = Theme.m_Name.empty() ? "none" : Theme.m_Name.c_str();
	if(str_endswith_nocase(pName, ".png"))
	{
		str_format(aIconPath, sizeof(aIconPath), "themes/%s", pName);
	}
	else
	{
		char aBaseName[IO_MAX_PATH_LENGTH];
		str_copy(aBaseName, pName, sizeof(aBaseName));
		const char *pExt = str_endswith_nocase(aBaseName, ".map");
		if(pExt)
		{
			char aTmp[IO_MAX_PATH_LENGTH];
			str_truncate(aTmp, sizeof(aTmp), aBaseName, pExt - aBaseName);
			str_copy(aBaseName, aTmp, sizeof(aBaseName));
		}
		const char *pDaySuffix = str_endswith(aBaseName, "_day");
		const char *pNightSuffix = str_endswith(aBaseName, "_night");
		if(pDaySuffix)
		{
			char aTmp[IO_MAX_PATH_LENGTH];
			str_truncate(aTmp, sizeof(aTmp), aBaseName, pDaySuffix - aBaseName);
			str_copy(aBaseName, aTmp, sizeof(aBaseName));
		}
		else if(pNightSuffix)
		{
			char aTmp[IO_MAX_PATH_LENGTH];
			str_truncate(aTmp, sizeof(aTmp), aBaseName, pNightSuffix - aBaseName);
			str_copy(aBaseName, aTmp, sizeof(aBaseName));
		}
		str_format(aIconPath, sizeof(aIconPath), "themes/%s.png", aBaseName);
	}
	Theme.m_IconTexture.Invalidate();
	if(Storage()->FileExists(aIconPath, IStorage::TYPE_ALL))
	{
		Theme.m_IconTexture = Graphics()->LoadTexture(aIconPath, IStorage::TYPE_ALL);
	}

	char aBuf[32 + IO_MAX_PATH_LENGTH];
	const bool IconLoaded = Theme.m_IconTexture.IsValid() && !Theme.m_IconTexture.IsNullTexture();
	if(!IconLoaded)
		str_format(aBuf, sizeof(aBuf), "failed to load theme icon '%s'", aIconPath);
	else
		str_format(aBuf, sizeof(aBuf), "loaded theme icon '%s'", aIconPath);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menuthemes", aBuf);
}

int CMenuBackground::ThemeScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	CMenuBackground *pSelf = (CMenuBackground *)pUser;
	const char *pSuffix = str_endswith(pName, ".map");
	if(!pSuffix)
		pSuffix = str_endswith(pName, ".png");
	if(IsDir || !pSuffix)
		return 0;
	for(const auto &Theme : pSelf->m_vThemes)
	{
		if(str_comp(Theme.m_Name.c_str(), pName) == 0)
			return 0;
	}

	// make new theme
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "added theme '%s' from 'themes/%s'", pName, pName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menuthemes", aBuf);
	pSelf->m_vThemes.emplace_back(pName, true, true);
	pSelf->LoadThemeIcon(pSelf->m_vThemes.back());

	if(time_get_nanoseconds() - pSelf->m_ThemeScanStartTime > 500ms)
	{
		pSelf->GameClient()->m_Menus.RenderLoading(Localize("Loading menu themes"), "", 0);
	}
	return 0;
}

void CMenuBackground::LoadMenuBackground(bool HasDayHint, bool HasNightHint)
{
	if(!m_IsInit)
		return;

	if(m_Loaded && !m_ImageBackground && m_pMap == m_pBackgroundMap)
		m_pMap->Unload();

	m_Loaded = false;
	ClearImageBackground();
	m_pMap = m_pBackgroundMap;
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;

	ResetPositions();

	char aMenuMapClean[IO_MAX_PATH_LENGTH];
	str_copy(aMenuMapClean, g_Config.m_ClMenuMap, sizeof(aMenuMapClean));
	str_utf8_trim_right(aMenuMapClean);
	const char *pMenuMap = str_utf8_skip_whitespaces(aMenuMapClean);
	if(pMenuMap != aMenuMapClean)
		str_copy(aMenuMapClean, pMenuMap, sizeof(aMenuMapClean));
	pMenuMap = aMenuMapClean;
	str_copy(m_aMapName, pMenuMap, sizeof(m_aMapName));

	if(pMenuMap[0] != '\0')
	{
		m_Loading = true;

		if(str_comp(pMenuMap, "auto") == 0)
		{
			switch(time_season())
			{
			case SEASON_SPRING:
			case SEASON_EASTER:
				pMenuMap = "heavens";
				break;
			case SEASON_SUMMER:
				pMenuMap = "jungle";
				break;
			case SEASON_AUTUMN:
			case SEASON_HALLOWEEN:
				pMenuMap = "autumn";
				break;
			case SEASON_WINTER:
			case SEASON_XMAS:
				pMenuMap = "winter";
				break;
			case SEASON_NEWYEAR:
				pMenuMap = "newyear";
				break;
			}
		}
		else if(str_comp(pMenuMap, "rand") == 0)
		{
			// make sure to load themes
			const std::vector<CTheme> &vThemesRef = GetThemes();
			if(vThemesRef.size() > PREDEFINED_THEMES_COUNT)
			{
				int RandomThemeIndex = rand() % (vThemesRef.size() - PREDEFINED_THEMES_COUNT);
				if(RandomThemeIndex + PREDEFINED_THEMES_COUNT < (int)vThemesRef.size())
					pMenuMap = vThemesRef[RandomThemeIndex + PREDEFINED_THEMES_COUNT].m_Name.c_str();
			}
		}

		char aBuf[128];

		const int HourOfTheDay = time_houroftheday();
		const bool IsDaytime = HourOfTheDay >= 6 && HourOfTheDay < 18;
		const bool HasPngExtension = str_endswith_nocase(pMenuMap, ".png") != nullptr;
		const bool HasMapExtension = str_endswith_nocase(pMenuMap, ".map") != nullptr;
		const bool HasExplicitExtension = HasPngExtension || HasMapExtension;
		char aMenuMapBase[IO_MAX_PATH_LENGTH];
		str_copy(aMenuMapBase, pMenuMap, sizeof(aMenuMapBase));
		while(true)
		{
			const char *pExtension = str_endswith_nocase(aMenuMapBase, ".png");
			if(!pExtension)
				pExtension = str_endswith_nocase(aMenuMapBase, ".map");
			if(!pExtension)
				break;
			char aMenuMapTmp[IO_MAX_PATH_LENGTH];
			str_truncate(aMenuMapTmp, sizeof(aMenuMapTmp), aMenuMapBase, pExtension - aMenuMapBase);
			str_copy(aMenuMapBase, aMenuMapTmp, sizeof(aMenuMapBase));
		}
		const char *pMenuMapBase = aMenuMapBase;

		auto FormatThemePath = [&](char *pOut, int OutSize, const char *pName) {
			if(str_startswith(pName, "themes/") || str_startswith(pName, "themes\\"))
				str_copy(pOut, pName, OutSize);
			else
				str_format(pOut, OutSize, "themes/%s", pName);
		};

		auto FindThemeFile = [&](const char *pBasePath, const char *pSuffix, char *pOut, int OutSize) -> bool {
			const char *pThemesPrefix = str_startswith(pBasePath, "themes/");
			if(!pThemesPrefix)
				pThemesPrefix = str_startswith(pBasePath, "themes\\");
			const char *pThemeName = pThemesPrefix ? pThemesPrefix : pBasePath;
			char aFilename[IO_MAX_PATH_LENGTH];
			str_format(aFilename, sizeof(aFilename), "%s%s", pThemeName, pSuffix);
			return Storage()->FindFile(aFilename, "themes", IStorage::TYPE_ALL, pOut, OutSize);
		};

		auto TryLoadTheme = [&](const char *pBasePath) -> bool {
			str_format(aBuf, sizeof(aBuf), "%s.map", pBasePath);
			if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && m_pMap->Load(aBuf))
			{
				m_Loaded = true;
				return true;
			}
			char aFound[IO_MAX_PATH_LENGTH];
			if(FindThemeFile(pBasePath, ".map", aFound, sizeof(aFound)) && m_pMap->Load(aFound))
			{
				m_Loaded = true;
				return true;
			}
			str_format(aBuf, sizeof(aBuf), "%s.png", pBasePath);
			if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && LoadImageBackground(aBuf))
			{
				return true;
			}
			if(FindThemeFile(pBasePath, ".png", aFound, sizeof(aFound)) && LoadImageBackground(aFound))
			{
				return true;
			}
			return false;
		};

		if(HasExplicitExtension)
		{
			FormatThemePath(aBuf, sizeof(aBuf), pMenuMap);
			if(HasMapExtension)
			{
				if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && m_pMap->Load(aBuf))
				{
					m_Loaded = true;
				}
				else
				{
					char aFound[IO_MAX_PATH_LENGTH];
					if(FindThemeFile(aBuf, "", aFound, sizeof(aFound)) && m_pMap->Load(aFound))
					{
						m_Loaded = true;
					}
				}
			}
			else if(HasPngExtension)
			{
				if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && LoadImageBackground(aBuf))
				{
					// LoadImageBackground updates m_Loaded/m_ImageBackground.
				}
				else
				{
					char aFound[IO_MAX_PATH_LENGTH];
					if(FindThemeFile(aBuf, "", aFound, sizeof(aFound)) && LoadImageBackground(aFound))
					{
						// LoadImageBackground updates m_Loaded/m_ImageBackground.
					}
				}
			}
			if(!m_Loaded)
			{
				str_format(aBuf, sizeof(aBuf), "failed to load menu theme '%s'", pMenuMap);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "menuthemes", aBuf);
				m_Loading = false;
				return;
			}
		}
		else
		{
			pMenuMap = pMenuMapBase;

			if(!m_Loaded && ((HasDayHint && IsDaytime) || (HasNightHint && !IsDaytime)))
			{
				FormatThemePath(aBuf, sizeof(aBuf), pMenuMap);
				str_append(aBuf, IsDaytime ? "_day" : "_night", sizeof(aBuf));
				TryLoadTheme(aBuf);
			}

			if(!m_Loaded)
			{
				FormatThemePath(aBuf, sizeof(aBuf), pMenuMap);
				TryLoadTheme(aBuf);
			}

			if(!m_Loaded && ((HasDayHint && !IsDaytime) || (HasNightHint && IsDaytime)))
			{
				FormatThemePath(aBuf, sizeof(aBuf), pMenuMap);
				str_append(aBuf, IsDaytime ? "_night" : "_day", sizeof(aBuf));
				TryLoadTheme(aBuf);
			}
		}

		if(m_Loaded && !m_ImageBackground)
		{
			m_pLayers->Init(m_pMap, true);

			m_pImages->LoadBackground(m_pLayers, m_pMap);
			CMapLayers::OnMapLoad();

			// look for custom positions
			CMapItemLayerTilemap *pTLayer = m_pLayers->GameLayer();
			if(pTLayer)
			{
				int DataIndex = pTLayer->m_Data;
				unsigned int Size = m_pLayers->Map()->GetDataSize(DataIndex);
				void *pTiles = m_pLayers->Map()->GetData(DataIndex);
				unsigned int TileSize = sizeof(CTile);

				if(Size >= pTLayer->m_Width * pTLayer->m_Height * TileSize)
				{
					for(int y = 0; y < pTLayer->m_Height; ++y)
					{
						for(int x = 0; x < pTLayer->m_Width; ++x)
						{
							unsigned char Index = ((CTile *)pTiles)[y * pTLayer->m_Width + x].m_Index;
							if(Index >= TILE_TIME_CHECKPOINT_FIRST && Index <= TILE_TIME_CHECKPOINT_LAST)
							{
								int ArrayIndex = std::clamp<int>((Index - TILE_TIME_CHECKPOINT_FIRST), 0, NUM_POS);
								m_aPositions[ArrayIndex] = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
							}

							x += ((CTile *)pTiles)[y * pTLayer->m_Width + x].m_Skip;
						}
					}
				}
			}
		}
		if(!m_Loaded)
		{
			str_format(aBuf, sizeof(aBuf), "failed to load menu theme '%s'", pMenuMap);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "menuthemes", aBuf);
		}
		m_Loading = false;
	}
}

void CMenuBackground::OnMapLoad()
{
}

void CMenuBackground::OnRender()
{
}

bool CMenuBackground::Render()
{
	if(!m_Loaded)
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

	m_Camera.m_Zoom = 0.7f;

	float DistToCenter = distance(m_Camera.m_Center, m_RotationCenter);
	if(!m_ChangedPosition && absolute(DistToCenter - (float)g_Config.m_ClRotationRadius) <= 0.5f)
	{
		// do little rotation
		float RotPerTick = 360.0f / (float)g_Config.m_ClRotationSpeed * std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
		m_CurrentDirection = rotate(m_CurrentDirection, RotPerTick);
		m_Camera.m_Center = m_RotationCenter + m_CurrentDirection * (float)g_Config.m_ClRotationRadius;
	}
	else
	{
		// positions for the animation
		vec2 DirToCenter;
		if(DistToCenter > 0.5f)
			DirToCenter = normalize(m_AnimationStartPos - m_RotationCenter);
		else
			DirToCenter = vec2(1, 0);
		vec2 TargetPos = m_RotationCenter + DirToCenter * (float)g_Config.m_ClRotationRadius;
		float Distance = distance(m_AnimationStartPos, TargetPos);
		if(Distance > 0.001f)
			m_CurrentDirection = normalize(m_AnimationStartPos - TargetPos);
		else
			m_CurrentDirection = vec2(1.0f, 0.0f);

		// move time
		m_MoveTime += std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f) * g_Config.m_ClCameraSpeed / 10.0f;
		float XVal = 1 - m_MoveTime;
		XVal = std::pow(XVal, 7.0f);

		m_Camera.m_Center = TargetPos + m_CurrentDirection * (XVal * Distance);
		if(m_CurrentPosition < 0)
		{
			m_AnimationStartPos = m_Camera.m_Center;
			m_MoveTime = 0.0f;
		}

		m_ChangedPosition = false;
	}

	CMapLayers::OnRender();

	m_CurrentPosition = -1;

	return true;
}

CCamera *CMenuBackground::GetCurCamera()
{
	return &m_Camera;
}

void CMenuBackground::ChangePosition(int PositionNumber)
{
	if(PositionNumber != m_CurrentPosition)
	{
		if(PositionNumber >= POS_START && PositionNumber < NUM_POS)
		{
			m_CurrentPosition = PositionNumber;
		}
		else
		{
			m_CurrentPosition = POS_START;
		}

		m_ChangedPosition = true;
	}
	m_AnimationStartPos = m_Camera.m_Center;
	m_RotationCenter = m_aPositions[m_CurrentPosition];
	m_MoveTime = 0.0f;
}

void CMenuBackground::RefreshThemes()
{
	m_vThemes.clear();
}

std::vector<CTheme> &CMenuBackground::GetThemes()
{
	if(m_vThemes.empty()) // not loaded yet
	{
		// when adding more here, make sure to change the value of PREDEFINED_THEMES_COUNT too
		m_vThemes.emplace_back("", true, true); // no theme
		LoadThemeIcon(m_vThemes.back());

		m_vThemes.emplace_back("auto", true, true); // auto theme
		LoadThemeIcon(m_vThemes.back());

		m_vThemes.emplace_back("rand", true, true); // random theme
		LoadThemeIcon(m_vThemes.back());

		m_ThemeScanStartTime = time_get_nanoseconds();
		Storage()->ListDirectory(IStorage::TYPE_ALL, "themes", ThemeScan, this);

		std::sort(m_vThemes.begin() + PREDEFINED_THEMES_COUNT, m_vThemes.end());
	}
	return m_vThemes;
}
