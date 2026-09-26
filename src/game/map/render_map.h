#ifndef GAME_MAP_RENDER_MAP_H
#define GAME_MAP_RENDER_MAP_H

#include <base/color.h>

#include <game/map/render_interfaces.h>
#include <game/mapitems.h>

#include <array>
#include <chrono>
#include <cmath>

enum
{
	LAYERRENDERFLAG_OPAQUE = 1,
	LAYERRENDERFLAG_TRANSPARENT = 2,

	TILERENDERFLAG_EXTEND = 4,

	OVERLAYRENDERFLAG_TEXT = 1,
	OVERLAYRENDERFLAG_EDITOR = 2,
};

class IEnvelopePointAccess
{
public:
	virtual ~IEnvelopePointAccess() = default;
	virtual int NumPoints() const = 0;
	virtual const CEnvPoint *GetPoint(int Index) const = 0;
	virtual const CEnvPointBezier *GetBezier(int Index) const = 0;
	int FindPointIndex(CFixedTime Time) const;
};

class CMapBasedEnvelopePointAccess : public IEnvelopePointAccess
{
	int m_StartPoint;
	int m_NumPoints;
	int m_NumPointsMax;
	CEnvPoint *m_pPoints;
	CEnvPointBezier *m_pPointsBezier;
	CEnvPointBezier_upstream *m_pPointsBezierUpstream;

public:
	CMapBasedEnvelopePointAccess(class CDataFileReader *pReader);
	CMapBasedEnvelopePointAccess(class IMap *pMap);
	void SetPointsRange(int StartPoint, int NumPoints);
	int StartPoint() const;
	int NumPoints() const override;
	int NumPointsMax() const;
	const CEnvPoint *GetPoint(int Index) const override;
	const CEnvPointBezier *GetBezier(int Index) const override;
};

class IGraphics;
class ITextRender;

class CTuneColorMapper
{
public:
	CTuneColorMapper() { Reset(); }
	uint8_t TuneNumberToColorIndex(uint8_t TuneNumber)
	{
		if(TuneNumber == 0)
			return 0;

		uint8_t &TuneColorIndex = m_aTuneNumberToColorIndex[TuneNumber - 1];
		if(TuneColorIndex == 0)
		{
			TuneColorIndex = m_NextTuneNumberIndex + 1;
			++m_NextTuneNumberIndex;
		}
		return TuneColorIndex;
	}
	uint8_t TileTextureIndex(uint8_t TileType, uint8_t TuneNumber, bool HasTextureArrays)
	{
		// 非 array 后端采样普通实体贴图，编号颜色图集仅供 texture array 后端使用。
		if(!HasTextureArrays || TuneNumber == 0)
			return TileType;
		return TuneNumberToColorIndex(TuneNumber);
	}
	ColorRGBA TuneColorIndexToColor(uint8_t TuneColorIndex) const
	{
		if(TuneColorIndex == 0)
			return ColorRGBA(1.0f, 1.0f, 1.0f);

		const float Hue = std::fmod((TuneColorIndex - 1) * normalized_golden_angle, 1.0f);
		return color_cast<ColorRGBA>(ColorHSLA(Hue, 0.75f, 0.5f, 1.0f));
	}
	void Reset()
	{
		m_aTuneNumberToColorIndex.fill(0);
		m_NextTuneNumberIndex = 0;
	}

private:
	std::array<uint8_t, 255> m_aTuneNumberToColorIndex;
	uint8_t m_NextTuneNumberIndex = 0;
};

class CRenderMap
{
	IGraphics *m_pGraphics;
	ITextRender *m_pTextRender;

public:
	using FTileRenderFilter = bool (*)(unsigned char Index, void *pUser);
	void Init(IGraphics *pGraphics, ITextRender *pTextRender);
	IGraphics *Graphics() { return m_pGraphics; }
	ITextRender *TextRender() { return m_pTextRender; }

	// map render methods (render_map.cpp)
	static void RenderEvalEnvelope(const IEnvelopePointAccess *pPoints, std::chrono::nanoseconds TimeNanos, ColorRGBA &Result, size_t Channels);
	void ForceRenderQuads(CQuad *pQuads, int NumQuads, int Flags, const IEnvelopeEval *pEnvEval, float Alpha = 1.0f);
	void RenderTile(int x, int y, unsigned char Index, float Scale, ColorRGBA Color);
	void RenderTilemap(CTile *pTiles, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter = nullptr, void *pFilterUser = nullptr);

	// render a rectangle made of IndexIn tiles, over a background made of IndexOut tiles
	// the rectangle include all tiles in [RectX, RectX+RectW-1] x [RectY, RectY+RectH-1]
	void RenderTileRectangle(int RectX, int RectY, int RectW, int RectH, unsigned char IndexIn, unsigned char IndexOut, float Scale, ColorRGBA Color, int RenderFlags);

	// DDRace
	void RenderTeleOverlay(CTeleTile *pTele, int w, int h, float Scale, int OverlayRenderFlags, float Alpha = 1.0f, FTileRenderFilter pFilter = nullptr, void *pFilterUser = nullptr);
	void RenderSpeedupOverlay(CSpeedupTile *pSpeedup, int w, int h, float Scale, int OverlayRenderFlags, float Alpha = 1.0f);
	void RenderSwitchOverlay(CSwitchTile *pSwitch, int w, int h, float Scale, int OverlayRenderFlags, float Alpha = 1.0f);
	void RenderTuneOverlay(CTuneTile *pTune, int w, int h, float Scale, int OverlayRenderFlags, float Alpha = 1.0f);
	void RenderTelemap(CTeleTile *pTele, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter = nullptr, void *pFilterUser = nullptr);
	void RenderSwitchmap(CSwitchTile *pSwitch, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter = nullptr, void *pFilterUser = nullptr);
	void RenderTunemap(CTuneTile *pTune, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, CTuneColorMapper *pTuneColorMapper);

	void RenderDebugClip(float ClipX, float ClipY, float ClipW, float ClipH, ColorRGBA Color, float Zoom, const char *pLabel);
};

#endif
