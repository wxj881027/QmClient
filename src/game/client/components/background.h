#ifndef GAME_CLIENT_COMPONENTS_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_BACKGROUND_H

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/map.h>

#include <game/client/components/maplayers.h>

#include <cstdint>

class CLayers;
class CMapImages;
// Special value to use background of current map
#define CURRENT_MAP "%current%"

inline void NormalizeBackgroundEntitiesValue(const char *pValue, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;

	if(pValue == nullptr)
	{
		pOut[0] = '\0';
		return;
	}

	while(*pValue == '/')
		++pValue;

	if(str_startswith_nocase(pValue, "assets/"))
		pValue += 7;

	if(str_startswith_nocase(pValue, "maps/"))
		pValue += 5;

	str_copy(pOut, pValue, OutSize);
}

inline bool IsDefaultBackgroundEntitiesValue(const char *pValue)
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(pValue, aNormalized, sizeof(aNormalized));
	return aNormalized[0] == '\0';
}

inline bool IsCurrentMapBackgroundEntitiesValue(const char *pValue)
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(pValue, aNormalized, sizeof(aNormalized));
	return str_comp(aNormalized, CURRENT_MAP) == 0;
}

inline bool TryGetBackgroundEntitiesAssetName(const char *pValue, char *pOut, int OutSize)
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(pValue, aNormalized, sizeof(aNormalized));
	if(aNormalized[0] == '\0' || str_comp(aNormalized, CURRENT_MAP) == 0 || str_endswith_nocase(aNormalized, ".png"))
	{
		if(OutSize > 0)
			pOut[0] = '\0';
		return false;
	}

	str_copy(pOut, aNormalized, OutSize);
	if(str_endswith_nocase(pOut, ".map"))
		pOut[str_length(pOut) - 4] = '\0';
	return pOut[0] != '\0';
}

inline void BuildBackgroundEntitiesValueFromAsset(const char *pAssetName, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;

	if(pAssetName == nullptr || pAssetName[0] == '\0' || str_comp(pAssetName, "default") == 0)
	{
		pOut[0] = '\0';
		return;
	}

	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(pAssetName, aNormalized, sizeof(aNormalized));
	if(aNormalized[0] == '\0')
	{
		pOut[0] = '\0';
		return;
	}

	if(str_endswith_nocase(aNormalized, ".map") || str_endswith_nocase(aNormalized, ".png"))
		str_copy(pOut, aNormalized, OutSize);
	else
		str_format(pOut, OutSize, "%s.map", aNormalized);
}

inline void BuildBackgroundEntitiesValueFromInput(const char *pValue, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;

	if(pValue == nullptr)
	{
		pOut[0] = '\0';
		return;
	}

	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(pValue, aNormalized, sizeof(aNormalized));
	if(aNormalized[0] == '\0')
	{
		pOut[0] = '\0';
		return;
	}

	if(str_comp(aNormalized, CURRENT_MAP) == 0)
	{
		str_copy(pOut, CURRENT_MAP, OutSize);
		return;
	}

	str_copy(pOut, aNormalized, OutSize);
}

inline bool BuildBackgroundEntitiesCommitValueFromInput(const char *pInputValue, const char *pCurrentConfigValue, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return false;

	BuildBackgroundEntitiesValueFromInput(pInputValue, pOut, OutSize);

	char aCurrentNormalized[IO_MAX_PATH_LENGTH];
	BuildBackgroundEntitiesValueFromInput(pCurrentConfigValue, aCurrentNormalized, sizeof(aCurrentNormalized));
	return str_comp(aCurrentNormalized, pOut) != 0;
}

inline bool ShouldCommitBackgroundEntitiesInputOnBlur(bool WasActiveBeforeEditBox, bool IsActiveAfterEditBox, const char *pInputValue, const char *pCurrentConfigValue)
{
	if(!WasActiveBeforeEditBox || IsActiveAfterEditBox)
		return false;

	char aPendingValue[IO_MAX_PATH_LENGTH];
	return BuildBackgroundEntitiesCommitValueFromInput(pInputValue, pCurrentConfigValue, aPendingValue, sizeof(aPendingValue));
}

class CBackgroundEngineMap : public CMap
{
	MACRO_INTERFACE("background_enginemap")
};

class CBackground : public CMapLayers
{
protected:
	IEngineMap *m_pMap;
	bool m_Loaded;
	bool m_ImageBackground;
	char m_aMapName[MAX_MAP_LENGTH];
	IGraphics::CTextureHandle m_BackgroundTexture;

	//to avoid memory leak when switching to %current%
	CBackgroundEngineMap *m_pBackgroundMap;
	CLayers *m_pBackgroundLayers;
	CMapImages *m_pBackgroundImages;

	virtual CBackgroundEngineMap *CreateBGMap();
	void ClearImageBackground(bool UnloadTexture = true);
	bool LoadImageBackground(const char *pPath);

public:
	CBackground(ERenderType MapType = ERenderType::RENDERTYPE_BACKGROUND_FORCE, bool OnlineOnly = true);
	~CBackground() override;
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnShutdown() override;
	void OnMapLoad() override;
	void OnRender() override;
	bool RenderCustom(const vec2 &Center, float Zoom);

	void LoadBackground();
	const char *MapName() const { return m_aMapName; }
};

#endif
