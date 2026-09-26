#ifndef GAME_MAP_MAP_PREVIEW_COMMON_H
#define GAME_MAP_MAP_PREVIEW_COMMON_H

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/map.h>

#include <game/map/render_interfaces.h>
#include <game/map/render_map.h>
#include <game/mapitems.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class IStorage;
class CLayers;
class CMapBasedEnvelopePointAccess;

namespace MapPreview
{
	struct SBounds
	{
		float m_MinX = 0.0f;
		float m_MinY = 0.0f;
		float m_MaxX = 0.0f;
		float m_MaxY = 0.0f;
		bool m_Valid = false;

		void Extend(float X, float Y);
	};

	struct SGroupBounds
	{
		SBounds m_Bounds;
		int m_OffsetX = 0;
		int m_OffsetY = 0;
		int m_ParallaxX = 100;
		int m_ParallaxY = 100;
		int m_ParallaxZoom = 100;
	};

	void RotatePoint(const CPoint *pCenter, CPoint *pPoint, float Rotation);
	bool CropClearColorBorder(CImageInfo &Image);
	bool IsPreviewLayer(const CLayers &Layers, const CMapItemLayer *pLayer);
	void ExtendTileLayerBounds(SBounds &Bounds, const CMapItemGroup *pGroup, const CMapItemLayerTilemap *pTileLayer, IMap *pMap);
	void ExtendQuadLayerBounds(SBounds &Bounds, const CMapItemGroup *pGroup, const CMapItemLayerQuads *pQuadLayer, IMap *pMap, IEnvelopeEval *pEnvelopeEval);
	bool IntersectBounds(SBounds &Bounds, float MinX, float MinY, float MaxX, float MaxY);
	std::vector<SGroupBounds> CollectGroupBounds(const CLayers &Layers, IEnvelopeEval *pEnvelopeEval);
	bool CameraFitsZoom(const std::vector<SGroupBounds> &vGroups, float BaseWidth, float BaseHeight, float Zoom, vec2 *pCenter);
	std::pair<vec2, float> CalcCamera(const CLayers &Layers, float Aspect, IEnvelopeEval *pEnvelopeEval);
	bool MapUsesExternalResources(IMap *pMap);

	class CPreviewMapImagesBase : public IMapImages
	{
	public:
		struct SLoadStats
		{
			int m_Loaded = 0;
			int m_ExpectedExternal = 0;
			int m_MissingExternal = 0;
		};

		CPreviewMapImagesBase() = default;
		~CPreviewMapImagesBase() override;
		CPreviewMapImagesBase(const CPreviewMapImagesBase &) = delete;
		CPreviewMapImagesBase &operator=(const CPreviewMapImagesBase &) = delete;

		void Init(IGraphics *pGraphics, IMap *pMap, CLayers *pLayers);
		void Unload();
		IGraphics::CTextureHandle Get(int Index) const override;
		int Num() const override;
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType) override { return {}; }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return {}; }
		IGraphics::CTextureHandle GetTuneColors() override { return {}; }
		IGraphics::CTextureHandle GetOverlayBottom() override { return {}; }
		IGraphics::CTextureHandle GetOverlayTop() override { return {}; }
		IGraphics::CTextureHandle GetOverlayCenter() override { return {}; }

		const SLoadStats &LoadStats() const { return m_LoadStats; }

	private:
		IGraphics *m_pGraphics = nullptr;
		IMap *m_pMap = nullptr;
		std::vector<IGraphics::CTextureHandle> m_vTextures;
		SLoadStats m_LoadStats;
	};

	class CPreviewEnvelopeEvalBase : public IEnvelopeEval
	{
	public:
		void Init(IMap *pMap, std::chrono::nanoseconds TimeNanos = std::chrono::nanoseconds(0));
		void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const override;

	private:
		std::shared_ptr<CMapBasedEnvelopePointAccess> m_pEnvelopePoints;
		IMap *m_pMap = nullptr;
		std::chrono::nanoseconds m_TimeNanos;
	};

} // namespace MapPreview

#endif
