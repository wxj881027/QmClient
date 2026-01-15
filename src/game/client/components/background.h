#ifndef GAME_CLIENT_COMPONENTS_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_BACKGROUND_H

#include <engine/graphics.h>
#include <engine/shared/map.h>

#include <game/client/components/maplayers.h>

#include <cstdint>

class CLayers;
class CMapImages;
// Special value to use background of current map
#define CURRENT_MAP "%current%"

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
	void ClearImageBackground();
	bool LoadImageBackground(const char *pPath);

public:
	CBackground(ERenderType MapType = ERenderType::RENDERTYPE_BACKGROUND_FORCE, bool OnlineOnly = true);
	~CBackground() override;
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnMapLoad() override;
	void OnRender() override;
	bool RenderCustom(const vec2 &Center, float Zoom);

	void LoadBackground();
	const char *MapName() const { return m_aMapName; }
};

#endif
