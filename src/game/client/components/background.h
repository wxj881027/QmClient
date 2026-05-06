#ifndef GAME_CLIENT_COMPONENTS_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_BACKGROUND_H

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/map.h>

#include <game/client/components/maplayers.h>

#include <array>
#include <cstdint>
#include <vector>

#if defined(CONF_VIDEORECORDER)
struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;
#endif

class CLayers;
class CMapImages;
// Special value to use background of current map
#define CURRENT_MAP "%current%"

inline constexpr std::array<const char *, 2> BACKGROUND_IMAGE_EXTENSIONS = {
	".png",
	".webp",
};

inline constexpr std::array<const char *, 4> BACKGROUND_VIDEO_EXTENSIONS = {
	".mp4",
	".webm",
	".mov",
	".mkv",
};

inline bool IsBackgroundImageExtension(const char *pName)
{
	if(pName == nullptr)
		return false;
	for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
	{
		if(str_endswith_nocase(pName, pExtension))
			return true;
	}
	return false;
}

inline const char *FindBackgroundFileExtension(const char *pName)
{
	if(pName == nullptr)
		return nullptr;
	if(str_endswith_nocase(pName, ".map"))
		return ".map";
	for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
	{
		if(str_endswith_nocase(pName, pExtension))
			return pExtension;
	}
	for(const char *pExtension : BACKGROUND_VIDEO_EXTENSIONS)
	{
		if(str_endswith_nocase(pName, pExtension))
			return pExtension;
	}
	return nullptr;
}

inline bool IsBackgroundVideoExtension(const char *pName)
{
	if(pName == nullptr)
		return false;
	for(const char *pExtension : BACKGROUND_VIDEO_EXTENSIONS)
	{
		if(str_endswith_nocase(pName, pExtension))
			return true;
	}
	return false;
}

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
	if(aNormalized[0] == '\0' || str_comp(aNormalized, CURRENT_MAP) == 0)
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

	if(str_endswith_nocase(aNormalized, ".map") || IsBackgroundImageExtension(aNormalized) || IsBackgroundVideoExtension(aNormalized))
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
	bool m_VideoBackground = false;
	char m_aMapName[MAX_MAP_LENGTH];
	IGraphics::CTextureHandle m_BackgroundTexture;
#if defined(CONF_VIDEORECORDER)
	AVFormatContext *m_pVideoFormatContext = nullptr;
	AVCodecContext *m_pVideoCodecContext = nullptr;
	SwsContext *m_pVideoSwsContext = nullptr;
	AVFrame *m_pVideoFrame = nullptr;
	AVFrame *m_pVideoRgbaFrame = nullptr;
	AVPacket *m_pVideoPacket = nullptr;
	int m_VideoStreamIndex = -1;
	int64_t m_VideoStartTime = 0;
	double m_VideoDuration = 0.0;
	double m_VideoLastFrameTime = -1.0;
	double m_VideoFrameInterval = 1.0 / 30.0;
	int m_VideoWidth = 0;
	int m_VideoHeight = 0;
	std::vector<uint8_t> m_vVideoFrameBuffer;
#endif

	//to avoid memory leak when switching to %current%
	CBackgroundEngineMap *m_pBackgroundMap;
	CLayers *m_pBackgroundLayers;
	CMapImages *m_pBackgroundImages;

	virtual CBackgroundEngineMap *CreateBGMap();
	void ClearImageBackground(bool UnloadTexture = true);
	bool LoadImageBackground(const char *pPath);
	void ClearVideoBackground(bool UnloadTexture = true);
	bool LoadVideoBackground(const char *pPath);
	bool UpdateVideoBackground();
	bool RenderBackgroundTexture();
	IGraphics::CTextureHandle ActiveBackgroundTexture() const { return m_BackgroundTexture; }
#if defined(CONF_VIDEORECORDER)
	bool DecodeNextVideoFrame();
	bool RestartVideoBackground();
	bool UploadVideoFrame();
#endif

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
