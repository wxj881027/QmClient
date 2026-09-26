#include "menu_background.h"

#include "theme_scan.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

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

namespace
{
	constexpr int MAX_THEME_ICON_DIMENSION = 512;

	void ResizeThemeIconIfNeeded(CImageInfo &Image)
	{
		if(Image.m_Width <= MAX_THEME_ICON_DIMENSION && Image.m_Height <= MAX_THEME_ICON_DIMENSION)
			return;

		const double Scale = minimum(
			(double)MAX_THEME_ICON_DIMENSION / (double)Image.m_Width,
			(double)MAX_THEME_ICON_DIMENSION / (double)Image.m_Height);
		const int NewWidth = maximum(1, (int)std::floor((double)Image.m_Width * Scale));
		const int NewHeight = maximum(1, (int)std::floor((double)Image.m_Height * Scale));
		ResizeImage(Image, NewWidth, NewHeight);
	}
}

class CMenuBackground::CThemeListLoadJob : public IJob
{
public:
	struct SThemeEntry
	{
		std::string m_Name;
		bool m_HasDay = true;
		bool m_HasNight = true;
		std::string m_IconPath;
	};

private:
	IStorage *m_pStorage;
	mutable CLock m_Lock;
	std::vector<SThemeEntry> m_vEntries;
	bool m_Completed = false;

protected:
	static int ScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		if(IsDir || !IsThemeFileCandidate(pName))
			return 0;

		auto *pEntries = static_cast<std::vector<SThemeEntry> *>(pUser);
		for(const auto &Entry : *pEntries)
		{
			if(str_comp(Entry.m_Name.c_str(), pName) == 0)
				return 0;
		}

		SThemeEntry Entry;
		Entry.m_Name = pName;
		Entry.m_IconPath = ThemeIconPathFromName(pName);
		pEntries->push_back(std::move(Entry));
		return 0;
	}

	void Run() override REQUIRES(!m_Lock)
	{
		std::vector<SThemeEntry> vEntries;
		m_pStorage->ListDirectory(IStorage::TYPE_ALL, "themes", ScanCallback, &vEntries);
		std::sort(vEntries.begin(), vEntries.end(), [](const SThemeEntry &Left, const SThemeEntry &Right) {
			return Left.m_Name < Right.m_Name;
		});

		const CLockScope Lock(m_Lock);
		m_vEntries = std::move(vEntries);
		m_Completed = true;
	}

public:
	explicit CThemeListLoadJob(IStorage *pStorage) :
		m_pStorage(pStorage)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	std::vector<SThemeEntry> Entries() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_vEntries;
	}
};

class CMenuBackground::CThemeIconLoadJob : public IJob
{
public:
	struct SResult
	{
		std::string m_ThemeName;
		CImageInfo m_Image;
		bool m_Success = false;
	};

private:
	IStorage *m_pStorage;
	std::string m_ThemeName;
	std::string m_IconPath;
	mutable CLock m_Lock;
	SResult m_Result;
	bool m_Completed = false;

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		void *pFileData = nullptr;
		unsigned FileSize = 0;
		if(!m_pStorage->ReadFile(m_IconPath.c_str(), IStorage::TYPE_ALL, &pFileData, &FileSize))
		{
			const CLockScope Lock(m_Lock);
			m_Completed = true;
			return;
		}

		CImageInfo Image;
		bool Loaded = LoadBackgroundImageData(pFileData, FileSize, m_IconPath.c_str(), Image);
		free(pFileData);
		if(Loaded)
			Loaded = ConvertToRgba(Image) || (Image.m_pData != nullptr && Image.m_Format == CImageInfo::FORMAT_RGBA);

		const CLockScope Lock(m_Lock);
		if(Loaded)
		{
			ResizeThemeIconIfNeeded(Image);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = true;
		}
		m_Completed = true;
	}

public:
	CThemeIconLoadJob(const char *pThemeName, const char *pIconPath, IStorage *pStorage) :
		m_pStorage(pStorage),
		m_ThemeName(pThemeName),
		m_IconPath(pIconPath)
	{
		m_Result.m_ThemeName = pThemeName;
		Abortable(true);
	}

	~CThemeIconLoadJob()
	{
		m_Result.m_Image.Free();
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	SResult TakeResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

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

void CMenuBackground::InitializeLoadedMap()
{
	m_pLayers->Init(m_pMap, true);
	m_pImages->LoadBackground(m_pLayers, m_pMap);
	CMapLayers::OnMapLoad();

	CMapItemLayerTilemap *pTLayer = m_pLayers->GameLayer();
	if(!pTLayer)
		return;

	const int DataIndex = pTLayer->m_Data;
	const unsigned int Size = m_pLayers->Map()->GetDataSize(DataIndex);
	void *pTiles = m_pLayers->Map()->GetData(DataIndex);
	const unsigned int TileSize = sizeof(CTile);
	if(Size < pTLayer->m_Width * pTLayer->m_Height * TileSize)
		return;

	for(int y = 0; y < pTLayer->m_Height; ++y)
	{
		for(int x = 0; x < pTLayer->m_Width; ++x)
		{
			const unsigned char Index = ((CTile *)pTiles)[y * pTLayer->m_Width + x].m_Index;
			if(Index >= TILE_TIME_CHECKPOINT_FIRST && Index <= TILE_TIME_CHECKPOINT_LAST)
			{
				const int ArrayIndex = std::clamp<int>((Index - TILE_TIME_CHECKPOINT_FIRST), 0, NUM_POS);
				m_aPositions[ArrayIndex] = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
			}
			x += ((CTile *)pTiles)[y * pTLayer->m_Width + x].m_Skip;
		}
	}
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
	if(m_IsInit)
		return;

	m_pBackgroundMap = CreateBGMap();
	m_pMap = m_pBackgroundMap;

	m_IsInit = true;

	Kernel()->RegisterInterface<CMenuMap>((CMenuMap *)m_pBackgroundMap);
	EnsureThemeEntries();
	EnsureThemeListJob();
	if(g_Config.m_ClMenuMap[0] != '\0')
		LoadMenuBackground();

	// 启动路径：首个加载帧呈现之前必须完成菜单背景的图层初始化。
	// 菜单背景图层是分帧初始化的（CMapRenderer::LoadStep），若拖到加载循环里逐帧推进，
	// 前几帧 Render() 只能返回 false，调用方就会回退到程序化背景（(none) 棋盘格），
	// 即启动时看到的灰屏 / None 背景。这里一次性走完，让首个加载帧就有主题背景；
	// 运行时切换主题仍走分帧（那时有 m_pPreviousBackground 顶着）。
	if(m_Loading)
	{
		// 上限仅作保险，正常主题的图层数远小于此。
		for(int Guard = 0; m_Loading && Guard < 4096; Guard++)
			AdvanceLoading();
		if(m_Loading)
		{
			log_warn("menuthemes", "menu background layer initialization did not finish during init");
			m_Loading = false;
		}
	}

	m_Camera.m_ZoomSet = false;
	m_Camera.m_ZoomSmoothingTarget = 0;
}

void CMenuBackground::AdvanceLoading()
{
	if(!m_Loading)
		return;
	// 非地图背景（图片/视频/加载失败）没有分帧工作，直接结束等待。
	if(!m_Loaded || m_ImageBackground || m_VideoBackground || AdvanceMapLoad())
		m_Loading = false;
}

void CMenuBackground::PreserveCurrentBackground()
{
	if(!m_Loaded || m_ImageBackground || m_VideoBackground || m_pBackgroundMap == nullptr || m_pBackgroundLayers == nullptr || m_pBackgroundImages == nullptr)
		return;

	m_pPreviousBackground = std::make_unique<SPreviousBackground>();
	m_pPreviousBackground->m_pMap.reset(static_cast<CMenuMap *>(m_pBackgroundMap));
	m_pPreviousBackground->m_pLayers.reset(m_pBackgroundLayers);
	m_pPreviousBackground->m_pImages.reset(m_pBackgroundImages);
	m_pPreviousBackground->m_pRenderer = std::make_unique<CMapLayers>(ERenderType::RENDERTYPE_FULL_DESIGN, false);
	m_pPreviousBackground->m_pRenderer->OnInterfacesInit(GameClient());
	SwapState(*m_pPreviousBackground->m_pRenderer);

	m_pBackgroundMap = CreateBGMap();
	m_pBackgroundLayers = new CLayers;
	m_pBackgroundImages = new CMapImages;
	m_pMap = m_pBackgroundMap;
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;
	m_pImages->OnInterfacesInit(GameClient());
}

void CMenuBackground::ReleasePreviousBackground()
{
	if(!m_pPreviousBackground)
		return;
	if(m_pPreviousBackground->m_pImages)
		m_pPreviousBackground->m_pImages->Unload();
	m_pPreviousBackground.reset();
}

void CMenuBackground::ResetPositions()
{
	m_aPositions = GenerateMenuBackgroundPositions();
}

void CMenuBackground::EnsureThemeEntries()
{
	if(!m_vThemes.empty())
		return;

	m_vThemes.emplace_back("", true, true); // no theme
	m_vThemes.back().m_IconPath = ThemeIconPathFromName("");

	m_vThemes.emplace_back("auto", true, true); // auto theme
	m_vThemes.back().m_IconPath = ThemeIconPathFromName("auto");

	m_vThemes.emplace_back("rand", true, true); // random theme
	m_vThemes.back().m_IconPath = ThemeIconPathFromName("rand");
}

void CMenuBackground::EnsureThemeListJob()
{
	if(m_pThemeListLoadJob)
		return;
	m_ThemeScanStartTime = time_get_nanoseconds();
	m_pThemeListLoadJob = std::make_shared<CThemeListLoadJob>(Storage());
	Engine()->AddJob(m_pThemeListLoadJob);
}

void CMenuBackground::ProcessThemeListJob()
{
	if(!m_pThemeListLoadJob || !m_pThemeListLoadJob->IsCompleted())
		return;

	for(const auto &Entry : m_pThemeListLoadJob->Entries())
	{
		auto It = std::find_if(m_vThemes.begin(), m_vThemes.end(), [&](const CTheme &Theme) {
			return str_comp(Theme.m_Name.c_str(), Entry.m_Name.c_str()) == 0;
		});
		if(It != m_vThemes.end())
			continue;

		CTheme Theme(Entry.m_Name.c_str(), Entry.m_HasDay, Entry.m_HasNight);
		Theme.m_IconPath = Entry.m_IconPath;
		m_vThemes.push_back(std::move(Theme));
	}

	if(m_vThemes.size() > PREDEFINED_THEMES_COUNT)
		std::sort(m_vThemes.begin() + PREDEFINED_THEMES_COUNT, m_vThemes.end());

	m_ThemeListLoaded = true;
	m_pThemeListLoadJob.reset();
}

void CMenuBackground::QueueNextThemeIconLoad()
{
	for(auto &Theme : m_vThemes)
	{
		if(Theme.m_IconTexture.IsValid() || Theme.m_IconLoadRequested || Theme.m_IconLoadFailed || Theme.m_IconPath.empty())
			continue;

		QueueThemeIconLoad(Theme);
		break;
	}
}

void CMenuBackground::QueueThemeIconLoad(CTheme &Theme)
{
	if(Theme.m_IconLoadRequested || Theme.m_IconLoadFailed || Theme.m_IconPath.empty())
		return;
	if(!Storage()->FileExists(Theme.m_IconPath.c_str(), IStorage::TYPE_ALL))
	{
		Theme.m_IconLoadFailed = true;
		return;
	}

	Theme.m_IconLoadRequested = true;
	auto pJob = std::make_shared<CThemeIconLoadJob>(Theme.m_Name.c_str(), Theme.m_IconPath.c_str(), Storage());
	Engine()->AddJob(pJob);
	m_vThemeIconLoadJobs.push_back(pJob);
}

void CMenuBackground::ProcessThemeIconJobs()
{
	auto It = m_vThemeIconLoadJobs.begin();
	while(It != m_vThemeIconLoadJobs.end())
	{
		auto &pJob = *It;
		if(!pJob->IsCompleted())
		{
			++It;
			continue;
		}

		CThemeIconLoadJob::SResult Result = pJob->TakeResult();
		auto ThemeIt = std::find_if(m_vThemes.begin(), m_vThemes.end(), [&](const CTheme &Theme) {
			return str_comp(Theme.m_Name.c_str(), Result.m_ThemeName.c_str()) == 0;
		});
		if(ThemeIt != m_vThemes.end())
		{
			ThemeIt->m_IconLoadRequested = false;
			ThemeIt->m_IconLoadFailed = !Result.m_Success;
			if(Result.m_Success)
				ThemeIt->m_IconTexture = Graphics()->LoadTextureRawMove(const_cast<CImageInfo &>(Result.m_Image), 0, Result.m_ThemeName.c_str());
		}

		It = m_vThemeIconLoadJobs.erase(It);
		break;
	}
}

void CMenuBackground::LoadMenuBackground(bool HasDayHint, bool HasNightHint)
{
	if(!m_IsInit)
		return;

	ReleasePreviousBackground();
	PreserveCurrentBackground();
	if(m_pMap != nullptr && m_pMap == m_pBackgroundMap && m_pPreviousBackground == nullptr)
		m_pMap->Unload();

	m_Loaded = false;
	ClearImageBackground();
	ClearVideoBackground();
	m_pMap = m_pBackgroundMap;
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;

	ResetPositions();
	InvalidateCurrentPosition();

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
			const ETimeSeason Season = time_season();
			switch(Season)
			{
			case ETimeSeason::SPRING:
			case ETimeSeason::EASTER:
				pMenuMap = "heavens";
				break;
			case ETimeSeason::SUMMER:
				pMenuMap = "jungle";
				break;
			case ETimeSeason::AUTUMN:
			case ETimeSeason::HALLOWEEN:
				pMenuMap = "autumn";
				break;
			case ETimeSeason::WINTER:
			case ETimeSeason::XMAS:
				pMenuMap = "winter";
				break;
			case ETimeSeason::NEWYEAR:
				pMenuMap = "newyear";
				break;
			default:
				dbg_assert_failed("Invalid season: %d", (int)Season);
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

		char aBuf[IO_MAX_PATH_LENGTH];

		const int HourOfTheDay = time_houroftheday();
		const bool IsDaytime = HourOfTheDay >= 6 && HourOfTheDay < 18;
		const bool HasImageExtension = IsBackgroundImageExtension(pMenuMap);
		const bool HasVideoExtension = IsBackgroundVideoExtension(pMenuMap);
		const bool HasMapExtension = str_endswith_nocase(pMenuMap, ".map") != nullptr;
		const bool HasExplicitExtension = HasImageExtension || HasVideoExtension || HasMapExtension;
		char aMenuMapBase[IO_MAX_PATH_LENGTH];
		str_copy(aMenuMapBase, pMenuMap, sizeof(aMenuMapBase));
		while(true)
		{
			const char *pExtension = FindBackgroundFileExtension(aMenuMapBase);
			if(pExtension == nullptr)
				break;
			char aMenuMapTmp[IO_MAX_PATH_LENGTH];
			str_truncate(aMenuMapTmp, sizeof(aMenuMapTmp), aMenuMapBase, str_length(aMenuMapBase) - str_length(pExtension));
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
			std::string CandidatePath = BuildThemeCandidatePath(pBasePath, ".map");
			if(Storage()->FileExists(CandidatePath.c_str(), IStorage::TYPE_ALL) && m_pMap->Load(CandidatePath.c_str(), IStorage::TYPE_ALL))
			{
				m_Loaded = true;
				return true;
			}
			char aFound[IO_MAX_PATH_LENGTH];
			if(FindThemeFile(pBasePath, ".map", aFound, sizeof(aFound)) && m_pMap->Load(aFound, IStorage::TYPE_ALL))
			{
				m_Loaded = true;
				return true;
			}
			for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
			{
				CandidatePath = BuildThemeCandidatePath(pBasePath, pExtension);
				if(Storage()->FileExists(CandidatePath.c_str(), IStorage::TYPE_ALL) && LoadImageBackground(CandidatePath.c_str()))
				{
					return true;
				}
				if(FindThemeFile(pBasePath, pExtension, aFound, sizeof(aFound)) && LoadImageBackground(aFound))
				{
					return true;
				}
			}
			for(const char *pExtension : BACKGROUND_VIDEO_EXTENSIONS)
			{
				CandidatePath = BuildThemeCandidatePath(pBasePath, pExtension);
				if(Storage()->FileExists(CandidatePath.c_str(), IStorage::TYPE_ALL) && LoadVideoBackground(CandidatePath.c_str()))
				{
					return true;
				}
				if(FindThemeFile(pBasePath, pExtension, aFound, sizeof(aFound)) && LoadVideoBackground(aFound))
				{
					return true;
				}
			}
			return false;
		};

		if(HasExplicitExtension)
		{
			FormatThemePath(aBuf, sizeof(aBuf), pMenuMap);
			if(HasMapExtension)
			{
				if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && m_pMap->Load(aBuf, IStorage::TYPE_ALL))
				{
					m_Loaded = true;
				}
				else
				{
					char aFound[IO_MAX_PATH_LENGTH];
					if(FindThemeFile(aBuf, "", aFound, sizeof(aFound)) && m_pMap->Load(aFound, IStorage::TYPE_ALL))
					{
						m_Loaded = true;
					}
				}
			}
			else if(HasImageExtension)
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
			else if(HasVideoExtension)
			{
				if(Storage()->FileExists(aBuf, IStorage::TYPE_ALL) && LoadVideoBackground(aBuf))
				{
					// LoadVideoBackground updates m_Loaded/m_VideoBackground.
				}
				else
				{
					char aFound[IO_MAX_PATH_LENGTH];
					if(FindThemeFile(aBuf, "", aFound, sizeof(aFound)) && LoadVideoBackground(aFound))
					{
						// LoadVideoBackground updates m_Loaded/m_VideoBackground.
					}
				}
			}
			if(!m_Loaded)
			{
				str_format(aBuf, sizeof(aBuf), "failed to load menu theme '%s'", pMenuMap);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "menuthemes", aBuf);
				ReleasePreviousBackground();
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

		if(m_Loaded && !m_ImageBackground && !m_VideoBackground)
			InitializeLoadedMap();
		if(!m_Loaded)
		{
			str_format(aBuf, sizeof(aBuf), "failed to load menu theme '%s'", pMenuMap);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "menuthemes", aBuf);
			ReleasePreviousBackground();
		}
		else if(pMenuMap[0] == '\0')
		{
			ReleasePreviousBackground();
		}
		m_Loading = m_Loaded && !m_ImageBackground && !m_VideoBackground && !IsMapLoaded();
	}
	else
	{
		ReleasePreviousBackground();
		m_Loading = false;
	}
}

void CMenuBackground::OnMapLoad()
{
}

void CMenuBackground::OnRender()
{
	UpdateThemeLoading();
}

bool CMenuBackground::Render()
{
	if(!InterfacesInitialized())
		return false;

	if(!m_Loaded && !m_pPreviousBackground)
		return false;

	if(m_Loaded && RenderBackgroundTexture())
		return true;

	const bool MapReady = AdvanceMapLoad();

	// 位置未选定且相机处于退化态时，下面的位置插值会变成不动点：
	// 启动首帧 m_RotationCenter 与 m_Camera.m_Center 都是构造函数里的 (0,0)，
	// 而 LoadMenuBackground() 又通过 InvalidateCurrentPosition() 把 m_CurrentPosition 设回 -1。
	// 于是 DistToCenter = 0（≠ cl_rotation_radius）走插值分支，DirToCenter 与 Distance 都是零向量，
	// 且 m_CurrentPosition < 0 会每帧把 m_MoveTime 清零 → 相机永久停在 (0,0)。
	// 唯一出口本来是菜单页渲染里的 ChangePosition()，而启动加载界面（声音/菜单图片加载）
	// 期间没有任何菜单页渲染，所以加载界面背后的主题背景完全静止。
	// 这里把相机种子到起始位置的轨道半径上，交给上面的旋转分支（cl_rotation_speed 默认 40 秒一圈）。
	// 额外的距离判断保证只在退化态生效：切换主题时相机已在轨道上，不会被拽回 POS_START。
	//
	// 同时把 m_CurrentPosition 记为 POS_START：相机确实已经停在起始位置的轨道上。
	// 否则等菜单页真正渲染时 RenderStartMenuImpl() 里的 ChangePosition(POS_START) 会通过
	// NewPosition != m_CurrentPosition 的检查，置 m_ChangedPosition=true 走插值分支；
	// 而那时 m_AnimationStartPos 与 TargetPos 重合（Distance==0），插值分支会把
	// m_CurrentDirection 重置成 (1,0)，让已经转了一会儿的背景在一帧内跳回起始角度。
	// 记为 POS_START 后 ChangePosition() 直接早退，旋转无跳变地继续。
	if(m_CurrentPosition < 0 && distance(m_Camera.m_Center, m_RotationCenter) <= 0.5f)
	{
		m_RotationCenter = m_aPositions[POS_START];
		m_CurrentDirection = vec2(1.0f, 0.0f);
		m_Camera.m_Center = m_RotationCenter + m_CurrentDirection * (float)g_Config.m_ClRotationRadius;
		m_AnimationStartPos = m_Camera.m_Center;
		m_CurrentPosition = POS_START;
		m_ChangedPosition = false;
		m_MoveTime = 0.0f;
	}

	m_Camera.m_Zoom = 0.7f;

	// 相机动画的时间步长在这里自己取墙钟差值，不用 Client()->RenderFrameTime()。
	// 原因：启动阶段的加载界面（GameClient()->OnInit() 加载声音/菜单图片时，由 CSounds
	// 与菜单图片回调调用的 RenderLoading）整个跑在 CClient::Run() 主循环之前，而
	// m_RenderFrameTime 只在主循环的 render 分支里被赋值，在那之前恒为 client.h 的初值
	// 0.0001f。用它算旋转量只有 360/40*0.0001 ≈ 0.0009°/帧，加载界面背后的主题背景
	// 看上去仍然是静止的。主循环里两者本就相等（m_RenderFrameTime 就是两次 render 的
	// 墙钟差），所以换成墙钟差不会改变正常菜单阶段的观感。
	const std::chrono::nanoseconds CameraNow = time_get_nanoseconds();
	const float CameraFrameTime = std::clamp(
		(CameraNow - m_LastCameraFrameTime).count() / 1000000000.0f, 0.0f, 0.1f);
	m_LastCameraFrameTime = CameraNow;

	float DistToCenter = distance(m_Camera.m_Center, m_RotationCenter);
	if(!m_ChangedPosition && absolute(DistToCenter - (float)g_Config.m_ClRotationRadius) <= 0.5f)
	{
		// do little rotation
		float RotPerTick = 360.0f / (float)g_Config.m_ClRotationSpeed * CameraFrameTime;
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
		m_MoveTime += CameraFrameTime * g_Config.m_ClCameraSpeed / 10.0f;
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

	if(!MapReady)
	{
		if(m_pPreviousBackground && m_pPreviousBackground->m_pRenderer)
		{
			m_pPreviousBackground->m_pRenderer->RenderCustomWithCamera(m_Camera.m_Center, m_Camera.m_Zoom);
			return true;
		}
		return false;
	}

	m_Loading = false;
	ReleasePreviousBackground();
	CMapLayers::OnRender();

	return true;
}

CCamera *CMenuBackground::GetCurCamera()
{
	return &m_Camera;
}

void CMenuBackground::ChangePosition(int PositionNumber)
{
	const int NewPosition = PositionNumber >= POS_START && PositionNumber < NUM_POS ? PositionNumber : POS_START;
	if(NewPosition == m_CurrentPosition)
		return;

	m_CurrentPosition = NewPosition;
	m_ChangedPosition = true;
	m_AnimationStartPos = m_Camera.m_Center;
	m_RotationCenter = m_aPositions[m_CurrentPosition];
	m_MoveTime = 0.0f;
}

void CMenuBackground::InvalidateCurrentPosition()
{
	m_CurrentPosition = -1;
	m_ChangedPosition = false;
	m_MoveTime = 0.0f;
}

void CMenuBackground::RefreshThemes()
{
	m_vThemes.clear();
	m_pThemeListLoadJob.reset();
	m_vThemeIconLoadJobs.clear();
	m_ThemeListLoaded = false;
}

std::vector<CTheme> &CMenuBackground::GetThemes()
{
	EnsureThemeEntries();
	return m_vThemes;
}

void CMenuBackground::UpdateThemeLoading()
{
	EnsureThemeEntries();
	ProcessThemeListJob();
	ProcessThemeIconJobs();
	if(!m_ThemeListLoaded)
		EnsureThemeListJob();
	QueueNextThemeIconLoad();
}
