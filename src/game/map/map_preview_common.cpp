#include "map_preview_common.h"

#include <base/logger.h>
#include <base/system.h>

#include <engine/map.h>
#include <engine/shared/map.h>

#include <game/layers.h>
#include <game/map/map_renderer.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace MapPreview
{
	void SBounds::Extend(float X, float Y)
	{
		if(!m_Valid)
		{
			m_MinX = m_MaxX = X;
			m_MinY = m_MaxY = Y;
			m_Valid = true;
			return;
		}
		m_MinX = minimum(m_MinX, X);
		m_MinY = minimum(m_MinY, Y);
		m_MaxX = maximum(m_MaxX, X);
		m_MaxY = maximum(m_MaxY, Y);
	}

	void RotatePoint(const CPoint *pCenter, CPoint *pPoint, float Rotation)
	{
		const float X = fx2f(pPoint->x) - fx2f(pCenter->x);
		const float Y = fx2f(pPoint->y) - fx2f(pCenter->y);
		const float RotatedX = X * std::cos(Rotation) - Y * std::sin(Rotation);
		const float RotatedY = X * std::sin(Rotation) + Y * std::cos(Rotation);
		pPoint->x = f2fx(RotatedX + fx2f(pCenter->x));
		pPoint->y = f2fx(RotatedY + fx2f(pCenter->y));
	}

	bool CropClearColorBorder(CImageInfo &Image)
	{
		if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
			return false;

		const size_t PixelSize = Image.PixelSize();
		if(PixelSize < 3)
			return false;

		const uint8_t ClearR = Image.m_pData[0];
		const uint8_t ClearG = Image.m_pData[1];
		const uint8_t ClearB = Image.m_pData[2];
		const uint8_t ClearA = PixelSize >= 4 ? Image.m_pData[3] : 255;

		auto IsClearPixel = [&](size_t X, size_t Y) {
			const size_t Offset = (Y * Image.m_Width + X) * PixelSize;
			return Image.m_pData[Offset + 0] == ClearR &&
			       Image.m_pData[Offset + 1] == ClearG &&
			       Image.m_pData[Offset + 2] == ClearB &&
			       (PixelSize < 4 || Image.m_pData[Offset + 3] == ClearA);
		};

		size_t Top = 0;
		while(Top < Image.m_Height)
		{
			bool AnyContent = false;
			for(size_t X = 0; X < Image.m_Width; ++X)
			{
				if(!IsClearPixel(X, Top))
				{
					AnyContent = true;
					break;
				}
			}
			if(AnyContent)
				break;
			++Top;
		}

		if(Top == Image.m_Height)
			return false;

		size_t Bottom = Image.m_Height - 1;
		while(Bottom > Top)
		{
			bool AnyContent = false;
			for(size_t X = 0; X < Image.m_Width; ++X)
			{
				if(!IsClearPixel(X, Bottom))
				{
					AnyContent = true;
					break;
				}
			}
			if(AnyContent)
				break;
			--Bottom;
		}

		size_t Left = 0;
		while(Left < Image.m_Width)
		{
			bool AnyContent = false;
			for(size_t Y = Top; Y <= Bottom; ++Y)
			{
				if(!IsClearPixel(Left, Y))
				{
					AnyContent = true;
					break;
				}
			}
			if(AnyContent)
				break;
			++Left;
		}

		size_t Right = Image.m_Width - 1;
		while(Right > Left)
		{
			bool AnyContent = false;
			for(size_t Y = Top; Y <= Bottom; ++Y)
			{
				if(!IsClearPixel(Right, Y))
				{
					AnyContent = true;
					break;
				}
			}
			if(AnyContent)
				break;
			--Right;
		}

		const size_t NewWidth = Right - Left + 1;
		const size_t NewHeight = Bottom - Top + 1;
		if(NewWidth == 0 || NewHeight == 0)
			return false;
		if(NewWidth == Image.m_Width && NewHeight == Image.m_Height && Left == 0 && Top == 0)
			return false;

		if(NewWidth > std::numeric_limits<size_t>::max() / NewHeight)
			return false;
		const size_t NewPixelCount = NewWidth * NewHeight;
		if(NewPixelCount > std::numeric_limits<size_t>::max() / PixelSize)
			return false;

		uint8_t *pNewData = static_cast<uint8_t *>(malloc(NewPixelCount * PixelSize));
		if(pNewData == nullptr)
			return false;

		for(size_t Y = 0; Y < NewHeight; ++Y)
		{
			const size_t SrcOffset = ((Top + Y) * Image.m_Width + Left) * PixelSize;
			const size_t DstOffset = Y * NewWidth * PixelSize;
			mem_copy(pNewData + DstOffset, Image.m_pData + SrcOffset, NewWidth * PixelSize);
		}

		free(Image.m_pData);
		Image.m_pData = pNewData;
		Image.m_Width = NewWidth;
		Image.m_Height = NewHeight;
		return true;
	}

	bool IsPreviewLayer(const CLayers &Layers, const CMapItemLayer *pLayer)
	{
		return pLayer != (CMapItemLayer *)Layers.GameLayer() &&
		       pLayer != (CMapItemLayer *)Layers.FrontLayer() &&
		       pLayer != (CMapItemLayer *)Layers.TeleLayer() &&
		       pLayer != (CMapItemLayer *)Layers.SpeedupLayer() &&
		       pLayer != (CMapItemLayer *)Layers.SwitchLayer() &&
		       pLayer != (CMapItemLayer *)Layers.TuneLayer();
	}

	void ExtendTileLayerBounds(SBounds &Bounds, const CMapItemGroup *pGroup, const CMapItemLayerTilemap *pTileLayer, IMap *pMap)
	{
		CTile *pTiles = static_cast<CTile *>(pMap->GetData(pTileLayer->m_Data));
		if(!pTiles)
			return;
		const size_t ExpectedSize = (size_t)pTileLayer->m_Width * (size_t)pTileLayer->m_Height * sizeof(CTile);
		if((size_t)pMap->GetDataSize(pTileLayer->m_Data) < ExpectedSize)
		{
			pMap->UnloadData(pTileLayer->m_Data);
			return;
		}

		for(int y = 0; y < pTileLayer->m_Height; ++y)
		{
			for(int x = 0; x < pTileLayer->m_Width; ++x)
			{
				const CTile &Tile = pTiles[y * pTileLayer->m_Width + x];
				if(Tile.m_Index == TILE_AIR)
					continue;

				const float Left = (float)pGroup->m_OffsetX + x * 32.0f;
				const float Top = (float)pGroup->m_OffsetY + y * 32.0f;
				Bounds.Extend(Left, Top);
				Bounds.Extend(Left + 32.0f, Top + 32.0f);
			}
		}
		pMap->UnloadData(pTileLayer->m_Data);
	}

	void ExtendQuadLayerBounds(SBounds &Bounds, const CMapItemGroup *pGroup, const CMapItemLayerQuads *pQuadLayer, IMap *pMap, IEnvelopeEval *pEnvelopeEval)
	{
		CQuad *pQuads = static_cast<CQuad *>(pMap->GetData(pQuadLayer->m_Data));
		if(!pQuads)
			return;
		const size_t ExpectedSize = (size_t)pQuadLayer->m_NumQuads * sizeof(CQuad);
		if((size_t)pMap->GetDataSize(pQuadLayer->m_Data) < ExpectedSize)
		{
			pMap->UnloadData(pQuadLayer->m_Data);
			return;
		}

		for(int QuadIndex = 0; QuadIndex < pQuadLayer->m_NumQuads; ++QuadIndex)
		{
			const CQuad &Quad = pQuads[QuadIndex];
			ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
			if(Quad.m_ColorEnv >= 0)
				pEnvelopeEval->EnvelopeEval(Quad.m_ColorEnvOffset, Quad.m_ColorEnv, Color, 4);
			if(Color.a <= 0.0f)
				continue;

			ColorRGBA Position(0.0f, 0.0f, 0.0f, 0.0f);
			if(Quad.m_PosEnv >= 0)
				pEnvelopeEval->EnvelopeEval(Quad.m_PosEnvOffset, Quad.m_PosEnv, Position, 3);
			const vec2 Offset(Position.r, Position.g);
			const float Rotation = Position.b / 180.0f * pi;
			CPoint aRotated[4];
			const CPoint *pPoints = Quad.m_aPoints;
			if(Rotation != 0.0f)
			{
				for(int PointIndex = 0; PointIndex < 4; ++PointIndex)
				{
					aRotated[PointIndex] = Quad.m_aPoints[PointIndex];
					RotatePoint(&Quad.m_aPoints[4], &aRotated[PointIndex], Rotation);
				}
				pPoints = aRotated;
			}

			for(int PointIndex = 0; PointIndex < 4; ++PointIndex)
			{
				Bounds.Extend(
					(float)pGroup->m_OffsetX + fx2f(pPoints[PointIndex].x) + Offset.x,
					(float)pGroup->m_OffsetY + fx2f(pPoints[PointIndex].y) + Offset.y);
			}
		}
		pMap->UnloadData(pQuadLayer->m_Data);
	}

	bool IntersectBounds(SBounds &Bounds, float MinX, float MinY, float MaxX, float MaxY)
	{
		if(!Bounds.m_Valid)
			return false;

		Bounds.m_MinX = maximum(Bounds.m_MinX, MinX);
		Bounds.m_MinY = maximum(Bounds.m_MinY, MinY);
		Bounds.m_MaxX = minimum(Bounds.m_MaxX, MaxX);
		Bounds.m_MaxY = minimum(Bounds.m_MaxY, MaxY);
		Bounds.m_Valid = Bounds.m_MinX <= Bounds.m_MaxX && Bounds.m_MinY <= Bounds.m_MaxY;
		return Bounds.m_Valid;
	}

	std::vector<SGroupBounds> CollectGroupBounds(const CLayers &Layers, IEnvelopeEval *pEnvelopeEval)
	{
		std::vector<SGroupBounds> vGroups;
		for(int GroupIndex = 0; GroupIndex < Layers.NumGroups(); ++GroupIndex)
		{
			const CMapItemGroup *pGroup = Layers.GetGroup(GroupIndex);
			if(!pGroup)
				continue;

			SGroupBounds GroupBounds;
			GroupBounds.m_OffsetX = pGroup->m_OffsetX;
			GroupBounds.m_OffsetY = pGroup->m_OffsetY;
			GroupBounds.m_ParallaxX = pGroup->m_ParallaxX;
			GroupBounds.m_ParallaxY = pGroup->m_ParallaxY;
			GroupBounds.m_ParallaxZoom = std::clamp(maximum(pGroup->m_ParallaxX, pGroup->m_ParallaxY), 0, 100);

			for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; ++LayerIndex)
			{
				const CMapItemLayer *pLayer = Layers.GetLayer(pGroup->m_StartLayer + LayerIndex);
				if(!pLayer || !IsPreviewLayer(Layers, pLayer))
					continue;

				if(pLayer->m_Type == LAYERTYPE_TILES)
					ExtendTileLayerBounds(GroupBounds.m_Bounds, pGroup, reinterpret_cast<const CMapItemLayerTilemap *>(pLayer), Layers.Map());
				else if(pLayer->m_Type == LAYERTYPE_QUADS)
					ExtendQuadLayerBounds(GroupBounds.m_Bounds, pGroup, reinterpret_cast<const CMapItemLayerQuads *>(pLayer), Layers.Map(), pEnvelopeEval);
			}

			if(!GroupBounds.m_Bounds.m_Valid)
				continue;

			if(pGroup->m_Version >= 2 && pGroup->m_UseClipping)
			{
				if(!IntersectBounds(
					   GroupBounds.m_Bounds,
					   (float)pGroup->m_ClipX,
					   (float)pGroup->m_ClipY,
					   (float)(pGroup->m_ClipX + pGroup->m_ClipW),
					   (float)(pGroup->m_ClipY + pGroup->m_ClipH)))
					continue;
			}

			vGroups.push_back(GroupBounds);
		}
		return vGroups;
	}

	static bool UpdateCenterRangeForAxis(float BoundMin, float BoundMax, float Offset, float Parallax, float ViewSize, float &RangeMin, float &RangeMax)
	{
		if(Parallax == 0.0f)
			return BoundMin >= Offset - ViewSize * 0.5f && BoundMax <= Offset + ViewSize * 0.5f;

		float AxisMin = (BoundMax - Offset - ViewSize * 0.5f) * 100.0f / Parallax;
		float AxisMax = (BoundMin - Offset + ViewSize * 0.5f) * 100.0f / Parallax;
		if(AxisMin > AxisMax)
			std::swap(AxisMin, AxisMax);

		RangeMin = maximum(RangeMin, AxisMin);
		RangeMax = minimum(RangeMax, AxisMax);
		return RangeMin <= RangeMax;
	}

	bool CameraFitsZoom(const std::vector<SGroupBounds> &vGroups, float BaseWidth, float BaseHeight, float Zoom, vec2 *pCenter)
	{
		float CenterMinX = -std::numeric_limits<float>::infinity();
		float CenterMaxX = std::numeric_limits<float>::infinity();
		float CenterMinY = -std::numeric_limits<float>::infinity();
		float CenterMaxY = std::numeric_limits<float>::infinity();

		for(const auto &Group : vGroups)
		{
			const float Scale = ((float)Group.m_ParallaxZoom * (Zoom - 1.0f) + 100.0f) / 100.0f / Zoom;
			const float ViewWidth = BaseWidth * Scale;
			const float ViewHeight = BaseHeight * Scale;

			if(!UpdateCenterRangeForAxis(Group.m_Bounds.m_MinX, Group.m_Bounds.m_MaxX, (float)Group.m_OffsetX, (float)Group.m_ParallaxX, ViewWidth, CenterMinX, CenterMaxX) ||
				!UpdateCenterRangeForAxis(Group.m_Bounds.m_MinY, Group.m_Bounds.m_MaxY, (float)Group.m_OffsetY, (float)Group.m_ParallaxY, ViewHeight, CenterMinY, CenterMaxY))
				return false;
		}

		if(pCenter != nullptr)
			*pCenter = vec2((CenterMinX + CenterMaxX) * 0.5f, (CenterMinY + CenterMaxY) * 0.5f);
		return true;
	}

	std::pair<vec2, float> CalcCamera(const CLayers &Layers, float Aspect, IEnvelopeEval *pEnvelopeEval)
	{
		const auto vGroups = CollectGroupBounds(Layers, pEnvelopeEval);
		if(vGroups.empty())
			return {vec2(0.0f, 0.0f), 1.0f};

		const float BaseWidth = 1150.0f * Aspect;
		const float BaseHeight = 1150.0f;

		float Low = 0.1f;
		float High = 1.0f;
		while(!CameraFitsZoom(vGroups, BaseWidth, BaseHeight, High, nullptr) && High < 1024.0f)
			High *= 2.0f;

		if(High >= 1024.0f && !CameraFitsZoom(vGroups, BaseWidth, BaseHeight, High, nullptr))
			return {vec2(0.0f, 0.0f), 1.0f};

		for(int i = 0; i < 24; ++i)
		{
			const float Mid = (Low + High) * 0.5f;
			if(CameraFitsZoom(vGroups, BaseWidth, BaseHeight, Mid, nullptr))
				High = Mid;
			else
				Low = Mid;
		}

		vec2 Center(0.0f, 0.0f);
		CameraFitsZoom(vGroups, BaseWidth, BaseHeight, High, &Center);
		return {Center, maximum(High, 0.1f)};
	}

	bool MapUsesExternalResources(IMap *pMap)
	{
		if(pMap == nullptr)
			return false;

		int Start = 0;
		int Num = 0;
		pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
		for(int i = 0; i < Num; ++i)
		{
			const CMapItemImage_v2 *pImg = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(Start + i));
			if(pImg != nullptr && pImg->m_External)
				return true;
		}
		return false;
	}

	// --- CPreviewMapImagesBase ---

	CPreviewMapImagesBase::~CPreviewMapImagesBase()
	{
		Unload();
	}

	void CPreviewMapImagesBase::Init(IGraphics *pGraphics, IMap *pMap, CLayers *pLayers)
	{
		m_pGraphics = pGraphics;
		m_pMap = pMap;
		Unload();
		m_LoadStats = SLoadStats();

		int Start = 0;
		int Num = 0;
		m_pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
		m_vTextures.resize(Num);

		std::vector<unsigned char> vTextureUsageFlags(Num, 0);
		for(int GroupIndex = 0; GroupIndex < pLayers->NumGroups(); ++GroupIndex)
		{
			const CMapItemGroup *pGroup = pLayers->GetGroup(GroupIndex);
			if(!pGroup)
				continue;

			for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; ++LayerIndex)
			{
				const CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + LayerIndex);
				if(!pLayer)
					continue;

				if(pLayer->m_Type == LAYERTYPE_TILES)
				{
					const CMapItemLayerTilemap *pTileLayer = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
					if(pTileLayer->m_Image >= 0 && pTileLayer->m_Image < Num)
						vTextureUsageFlags[pTileLayer->m_Image] |= 1;
				}
				else if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					const CMapItemLayerQuads *pQuadLayer = reinterpret_cast<const CMapItemLayerQuads *>(pLayer);
					if(pQuadLayer->m_Image >= 0 && pQuadLayer->m_Image < Num)
						vTextureUsageFlags[pQuadLayer->m_Image] |= 2;
				}
			}
		}

		const int TextureLoadFlag = m_pGraphics->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
		for(int i = 0; i < Num; ++i)
		{
			if(vTextureUsageFlags[i] == 0)
				continue;

			const CMapItemImage_v2 *pImg = static_cast<const CMapItemImage_v2 *>(m_pMap->GetItem(Start + i));
			if(!pImg)
				continue;

			const char *pName = m_pMap->GetDataString(pImg->m_ImageName);
			if(!pName || !pName[0])
				continue;

			const int LoadFlag = (((vTextureUsageFlags[i] & 1) != 0) ? TextureLoadFlag : 0) |
					     (((vTextureUsageFlags[i] & 2) != 0) ? 0 : (m_pGraphics->HasTextureArraysSupport() ? IGraphics::TEXLOAD_NO_2D_TEXTURE : 0));

			if(pImg->m_External)
			{
				++m_LoadStats.m_ExpectedExternal;
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "mapres/%s.png", pName);
				m_vTextures[i] = m_pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL, LoadFlag);
				if(!m_vTextures[i].IsValid())
				{
					++m_LoadStats.m_MissingExternal;
					log_warn("map_preview", "Missing external map image '%s' at '%s'", pName, aPath);
				}
			}
			else
			{
				CImageInfo ImageInfo;
				ImageInfo.m_Width = pImg->m_Width;
				ImageInfo.m_Height = pImg->m_Height;
				ImageInfo.m_Format = CImageInfo::FORMAT_RGBA;
				ImageInfo.m_pData = static_cast<uint8_t *>(m_pMap->GetData(pImg->m_ImageData));
				size_t ImageDataSize = 0;
				const int FileDataSize = m_pMap->GetDataSize(pImg->m_ImageData);
				if(!ImageInfo.m_pData || FileDataSize < 0 || !ImageInfo.DataSize(ImageDataSize) || (size_t)FileDataSize < ImageDataSize)
				{
					m_pMap->UnloadData(pImg->m_ImageData);
					m_pMap->UnloadData(pImg->m_ImageName);
					continue;
				}

				char aTexName[IO_MAX_PATH_LENGTH];
				str_format(aTexName, sizeof(aTexName), "embedded: %s", pName);
				m_vTextures[i] = m_pGraphics->LoadTextureRaw(ImageInfo, LoadFlag, aTexName);
				m_pMap->UnloadData(pImg->m_ImageData);
			}
			m_pMap->UnloadData(pImg->m_ImageName);
			if(m_vTextures[i].IsValid())
				++m_LoadStats.m_Loaded;
		}
	}

	void CPreviewMapImagesBase::Unload()
	{
		if(m_pGraphics == nullptr)
		{
			m_vTextures.clear();
			return;
		}
		for(auto &Texture : m_vTextures)
			m_pGraphics->UnloadTexture(&Texture);
		m_vTextures.clear();
	}

	IGraphics::CTextureHandle CPreviewMapImagesBase::Get(int Index) const
	{
		if(Index < 0 || Index >= (int)m_vTextures.size())
			return {};
		return m_vTextures[Index];
	}

	int CPreviewMapImagesBase::Num() const
	{
		return (int)m_vTextures.size();
	}

	// --- CPreviewEnvelopeEvalBase ---

	void CPreviewEnvelopeEvalBase::Init(IMap *pMap, std::chrono::nanoseconds TimeNanos)
	{
		m_pMap = pMap;
		m_pEnvelopePoints = std::make_shared<CMapBasedEnvelopePointAccess>(pMap);
		m_TimeNanos = TimeNanos;
	}

	void CPreviewEnvelopeEvalBase::EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const
	{
		if(!m_pMap || !m_pEnvelopePoints)
			return;

		int EnvelopeStart = 0;
		int EnvelopeNum = 0;
		m_pMap->GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum);
		if(EnvelopeIndex < 0 || EnvelopeIndex >= EnvelopeNum)
			return;

		const CMapItemEnvelope *pItem = static_cast<const CMapItemEnvelope *>(m_pMap->GetItem(EnvelopeStart + EnvelopeIndex));
		if(!pItem || pItem->m_Channels <= 0)
			return;

		Channels = minimum<size_t>(Channels, pItem->m_Channels, CEnvPoint::MAX_CHANNELS);
		m_pEnvelopePoints->SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
		if(m_pEnvelopePoints->NumPoints() == 0)
			return;

		CRenderMap::RenderEvalEnvelope(m_pEnvelopePoints.get(), m_TimeNanos + std::chrono::milliseconds(TimeOffsetMillis), Result, Channels);
	}

} // namespace MapPreview
