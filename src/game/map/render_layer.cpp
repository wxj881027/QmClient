#include "render_layer.h"

#include "qm_entity_overlay_build_plan.h"
#include "render_screen.h"

#include <base/log.h>

#include <engine/client/graphics_buffer_pair.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/localization.h>
#include <game/map/map_render_items.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>

namespace
{
	template<typename T>
	T &QmCheckedOptional(std::optional<T> &Opt)
	{
		dbg_assert(Opt.has_value(), "optional must contain a value");
		return *Opt; // NOLINT(bugprone-unchecked-optional-access)
	}

	template<typename T>
	const T &QmCheckedOptional(const std::optional<T> &Opt)
	{
		dbg_assert(Opt.has_value(), "optional must contain a value");
		return *Opt; // NOLINT(bugprone-unchecked-optional-access)
	}

	/************************
	 * Render Buffer Helper *
	 ************************/
	class CTexCoords
	{
	public:
		std::array<uint8_t, 4> m_aTexX;
		std::array<uint8_t, 4> m_aTexY;
	};

	constexpr static CTexCoords CalculateTexCoords(unsigned int Flags)
	{
		CTexCoords TexCoord;
		TexCoord.m_aTexX = {0, 1, 1, 0};
		TexCoord.m_aTexY = {0, 0, 1, 1};

		if(Flags & TILEFLAG_XFLIP)
			std::rotate(std::begin(TexCoord.m_aTexX), std::begin(TexCoord.m_aTexX) + 2, std::end(TexCoord.m_aTexX));

		if(Flags & TILEFLAG_YFLIP)
			std::rotate(std::begin(TexCoord.m_aTexY), std::begin(TexCoord.m_aTexY) + 2, std::end(TexCoord.m_aTexY));

		if(Flags & (TILEFLAG_ROTATE >> 1))
		{
			std::rotate(std::begin(TexCoord.m_aTexX), std::begin(TexCoord.m_aTexX) + 3, std::end(TexCoord.m_aTexX));
			std::rotate(std::begin(TexCoord.m_aTexY), std::begin(TexCoord.m_aTexY) + 3, std::end(TexCoord.m_aTexY));
		}
		return TexCoord;
	}

	template<std::size_t N>
	constexpr static std::array<CTexCoords, N> MakeTexCoordsTable()
	{
		std::array<CTexCoords, N> aTexCoords = {};
		for(std::size_t i = 0; i < N; ++i)
			aTexCoords[i] = CalculateTexCoords(i);
		return aTexCoords;
	}

	constexpr std::array<CTexCoords, 8> TEX_COORDS_TABLE = MakeTexCoordsTable<8>();

	static void FillTmpTile(CGraphicTile *pTmpTile, CGraphicTileTextureCoords *pTmpTex, unsigned char Flags, unsigned char Index, int x, int y, const ivec2 &Offset, int Scale)
	{
		if(pTmpTex)
		{
			uint8_t TableFlag = (Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP)) + ((Flags & TILEFLAG_ROTATE) >> 1);
			const auto &aTexX = TEX_COORDS_TABLE[TableFlag].m_aTexX;
			const auto &aTexY = TEX_COORDS_TABLE[TableFlag].m_aTexY;

			pTmpTex->m_TexCoordTopLeft.x = aTexX[0];
			pTmpTex->m_TexCoordTopLeft.y = aTexY[0];
			pTmpTex->m_TexCoordBottomLeft.x = aTexX[3];
			pTmpTex->m_TexCoordBottomLeft.y = aTexY[3];
			pTmpTex->m_TexCoordTopRight.x = aTexX[1];
			pTmpTex->m_TexCoordTopRight.y = aTexY[1];
			pTmpTex->m_TexCoordBottomRight.x = aTexX[2];
			pTmpTex->m_TexCoordBottomRight.y = aTexY[2];

			pTmpTex->m_TexCoordTopLeft.z = Index;
			pTmpTex->m_TexCoordBottomLeft.z = Index;
			pTmpTex->m_TexCoordTopRight.z = Index;
			pTmpTex->m_TexCoordBottomRight.z = Index;

			bool HasRotation = (Flags & TILEFLAG_ROTATE) != 0;
			pTmpTex->m_TexCoordTopLeft.w = HasRotation;
			pTmpTex->m_TexCoordBottomLeft.w = HasRotation;
			pTmpTex->m_TexCoordTopRight.w = HasRotation;
			pTmpTex->m_TexCoordBottomRight.w = HasRotation;
		}

		const int64_t Left = static_cast<int64_t>(x) * Scale + Offset.x;
		const int64_t Top = static_cast<int64_t>(y) * Scale + Offset.y;
		vec2 TopLeft(static_cast<float>(Left), static_cast<float>(Top));
		vec2 BottomRight(static_cast<float>(Left + Scale), static_cast<float>(Top + Scale));
		pTmpTile->m_TopLeft = TopLeft;
		pTmpTile->m_BottomLeft.x = TopLeft.x;
		pTmpTile->m_BottomLeft.y = BottomRight.y;
		pTmpTile->m_TopRight.x = BottomRight.x;
		pTmpTile->m_TopRight.y = TopLeft.y;
		pTmpTile->m_BottomRight = BottomRight;
	}

	static void FillTmpTileSpeedup(CGraphicTile *pTmpTile, CGraphicTileTextureCoords *pTmpTex, unsigned char Flags, int x, int y, const ivec2 &Offset, int Scale, short AngleRotate)
	{
		int Angle = AngleRotate % 360;
		FillTmpTile(pTmpTile, pTmpTex, Angle >= 270 ? ROTATION_270 : (Angle >= 180 ? ROTATION_180 : (Angle >= 90 ? ROTATION_90 : 0)), AngleRotate % 90, x, y, Offset, Scale);
	}

	static bool AddTile(std::vector<CGraphicTile> &vTmpTiles, std::vector<CGraphicTileTextureCoords> &vTmpTileTexCoords, unsigned char Index, unsigned char Flags, int x, int y, bool DoTextureCoords, bool FillSpeedup = false, int AngleRotate = -1, const ivec2 &Offset = ivec2{0, 0}, int Scale = 32)
	{
		if(Index <= 0)
			return false;

		vTmpTiles.emplace_back();
		CGraphicTile &Tile = vTmpTiles.back();
		CGraphicTileTextureCoords *pTileTex = nullptr;
		if(DoTextureCoords)
		{
			vTmpTileTexCoords.emplace_back();
			CGraphicTileTextureCoords &TileTex = vTmpTileTexCoords.back();
			pTileTex = &TileTex;
		}
		if(FillSpeedup)
			FillTmpTileSpeedup(&Tile, pTileTex, Flags, x, y, Offset, Scale, AngleRotate);
		else
			FillTmpTile(&Tile, pTileTex, Flags, Index, x, y, Offset, Scale);

		return true;
	}

	class CTmpQuadVertexTextured
	{
	public:
		float m_X, m_Y, m_CenterX, m_CenterY;
		unsigned char m_R, m_G, m_B, m_A;
		float m_U, m_V;
	};

	class CTmpQuadVertex
	{
	public:
		float m_X, m_Y, m_CenterX, m_CenterY;
		unsigned char m_R, m_G, m_B, m_A;
	};

	class CTmpQuad
	{
	public:
		CTmpQuadVertex m_aVertices[4];
	};

	class CTmpQuadTextured
	{
	public:
		CTmpQuadVertexTextured m_aVertices[4];
	};

	int ImmediateTileRenderFlags(const CRenderLayerParams &Params, bool TileAndQuadBuffering)
	{
		int Flags = Params.m_RenderTileBorder ? TILERENDERFLAG_EXTEND : 0;
		// 整图导出按完整设计绘制并显式关闭图层缓冲，保留完整导出范围。
		if(TileAndQuadBuffering || Params.m_RenderType != ERenderType::RENDERTYPE_FULL_DESIGN)
			Flags |= TILERENDERFLAG_LIMIT_IMMEDIATE_WORK;
		return Flags;
	}

	bool BindTileNumberTexture(IGraphics &Graphics, IGraphics::CTextureHandle Texture)
	{
		if(!Graphics.IsTextureHandleAllocated(Texture))
			return false;
		Graphics.TextureSet(Texture);
		return true;
	}

}

static bool CheckedDataSize(size_t Count, size_t ItemSize, size_t &DataSize)
{
	if(ItemSize != 0 && Count > std::numeric_limits<size_t>::max() / ItemSize)
	{
		DataSize = 0;
		return false;
	}
	DataSize = Count * ItemSize;
	return true;
}

static bool CheckedAddSize(size_t Lhs, size_t Rhs, size_t &DataSize)
{
	if(Lhs > std::numeric_limits<size_t>::max() - Rhs)
	{
		DataSize = 0;
		return false;
	}
	DataSize = Lhs + Rhs;
	return true;
}

bool CRenderLayerTile::CTileLayerVisuals::Init(unsigned int Width, unsigned int Height)
{
	m_Width = Width;
	m_Height = Height;
	if(Width == 0 || Height == 0)
		return false;
	if constexpr(sizeof(unsigned int) >= sizeof(ptrdiff_t))
		if(Width >= std::numeric_limits<std::ptrdiff_t>::max() || Height >= std::numeric_limits<std::ptrdiff_t>::max())
			return false;

	m_vTilesOfLayer.reserve((size_t)Height * (size_t)Width);

	m_vBorderTop.resize(Width);
	m_vBorderBottom.resize(Width);

	m_vBorderLeft.resize(Height);
	m_vBorderRight.resize(Height);
	return true;
}

/**************
 * Base Layer *
 **************/

CRenderLayer::CRenderLayer(int GroupId, int LayerId, int Flags) :
	m_GroupId(GroupId), m_LayerId(LayerId), m_Flags(Flags) {}

void CRenderLayer::OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering)
{
	CRenderComponent::OnInit(pGraphics, pTextRender, pRenderMap);
	m_pMap = pMap;
	m_pMapImages = pMapImages;
	m_pEnvelopeManager = pEnvelopeManager;
	m_TileAndQuadBuffering = TileAndQuadBuffering;
}

void CRenderLayer::UseTexture(IGraphics::CTextureHandle TextureHandle)
{
	if(TextureHandle.IsValid())
		Graphics()->TextureSet(TextureHandle);
	else
		Graphics()->TextureClear();
}

bool CRenderLayer::IsVisibleInClipRegion(const std::optional<CClipRegion> &ClipRegion) const
{
	// always show unclipped regions
	if(!ClipRegion.has_value())
		return true;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	float Left = ClipRegion->m_X;
	float Top = ClipRegion->m_Y;
	float Right = ClipRegion->m_X + ClipRegion->m_Width;
	float Bottom = ClipRegion->m_Y + ClipRegion->m_Height;

	return Right >= ScreenX0 && Left <= ScreenX1 && Bottom >= ScreenY0 && Top <= ScreenY1;
}

/**************
 * Group *
 **************/

CRenderLayerGroup::CRenderLayerGroup(int GroupId, CMapItemGroup *pGroup) :
	CRenderLayer(GroupId, 0, 0), m_pGroup(pGroup) {}

CMapLoadTask CRenderLayerGroup::Init()
{
	co_return;
}

std::optional<std::array<float, 4>> CRenderLayerGroup::ScreenForGroup(const CRenderLayerParams &Params) const
{
	const int ParallaxZoom = std::clamp(maximum(m_pGroup->m_ParallaxX, m_pGroup->m_ParallaxY), 0, 100);
	std::array<float, 4> aPoints;
	Graphics()->MapScreenToWorld(Params.m_Center.x, Params.m_Center.y, m_pGroup->m_ParallaxX, m_pGroup->m_ParallaxY, static_cast<float>(ParallaxZoom),
		m_pGroup->m_OffsetX, m_pGroup->m_OffsetY, Graphics()->GameScreenAspect(), Params.m_Zoom, aPoints.data());
	return MapRenderScreen::IsValidScreen(aPoints) ? std::make_optional(aPoints) : std::nullopt;
}

bool CRenderLayerGroup::DoRender(const CRenderLayerParams &Params)
{
	// 组没有 clip 时也不能发布坏投影；失败关闭旧裁剪并跳过组成员。
	if(!ScreenForGroup(Params))
	{
		Graphics()->ClipDisable();
		return false;
	}

	if(!g_Config.m_GfxNoclip || Params.m_RenderType == ERenderType::RENDERTYPE_FULL_DESIGN)
	{
		Graphics()->ClipDisable();
		if(m_pGroup->m_Version >= 2 && m_pGroup->m_UseClipping)
		{
			std::array<float, 4> aPoints;
			Graphics()->MapScreenToWorld(Params.m_Center.x, Params.m_Center.y, 100.0f, 100.0f, 100.0f,
				0, 0, Graphics()->GameScreenAspect(), Params.m_Zoom, aPoints.data());
			const auto Clip = MapRenderScreen::GroupClip(aPoints, Graphics()->ScreenWidth(), Graphics()->ScreenHeight(),
				{m_pGroup->m_ClipX, m_pGroup->m_ClipY, m_pGroup->m_ClipW, m_pGroup->m_ClipH});
			if(!Clip)
				return false;
			Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

			// Render debug before enabling the clip
			if(Params.m_DebugRenderGroupClips)
			{
				char aDebugText[32];
				str_format(aDebugText, sizeof(aDebugText), "Group %d", m_GroupId);
				RenderMap()->RenderDebugClip(m_pGroup->m_ClipX, m_pGroup->m_ClipY, m_pGroup->m_ClipW, m_pGroup->m_ClipH, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), Params.m_Zoom, aDebugText);
			}

			Graphics()->ClipEnable(Clip->m_X, Clip->m_Y, Clip->m_Width, Clip->m_Height);
		}
	}
	return true;
}

void CRenderLayerGroup::Render(const CRenderLayerParams &Params)
{
	const auto Screen = ScreenForGroup(Params);
	if(!Screen)
	{
		Graphics()->ClipDisable();
		return;
	}
	Graphics()->MapScreen((*Screen)[0], (*Screen)[1], (*Screen)[2], (*Screen)[3]);
}

/**************
 * Tile Layer *
 **************/

CRenderLayerTile::CRenderLayerTile(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayer(GroupId, LayerId, Flags)
{
	m_pLayerTilemap = pLayerTilemap;
	m_Color = ColorRGBA(m_pLayerTilemap->m_Color.r / 255.0f, m_pLayerTilemap->m_Color.g / 255.0f, m_pLayerTilemap->m_Color.b / 255.0f, pLayerTilemap->m_Color.a / 255.0f);
	m_pTiles = nullptr;
}

void CRenderLayerTile::RenderTileLayer(const ColorRGBA &Color, const CRenderLayerParams &Params, CTileLayerVisuals *pTileLayerVisuals)
{
	CTileLayerVisuals &Visuals = pTileLayerVisuals ? *pTileLayerVisuals : QmCheckedOptional(m_VisualTiles);
	if(Visuals.m_BufferContainerIndex == -1)
		return; // no visuals were created

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const auto ScreenRectRange = MapRenderScreen::TileRange(ScreenX0, ScreenY0, ScreenX1, ScreenY1, 32.0f, MapRenderScreen::ETileRangeMode::BUFFERED);
	if(!ScreenRectRange)
		return;
	int ScreenRectY0 = ScreenRectRange->m_Y0;
	int ScreenRectX0 = ScreenRectRange->m_X0;
	int ScreenRectY1 = ScreenRectRange->m_Y1;
	int ScreenRectX1 = ScreenRectRange->m_X1;

	if(IsVisibleInClipRegion(m_LayerClip))
	{
		float VisibleX0 = ScreenX0;
		float VisibleY0 = ScreenY0;
		float VisibleX1 = ScreenX1;
		float VisibleY1 = ScreenY1;
		if(m_LayerClip.has_value())
		{
			const CClipRegion &Clip = QmCheckedOptional(m_LayerClip);
			VisibleX0 = std::max(VisibleX0, Clip.m_X);
			VisibleY0 = std::max(VisibleY0, Clip.m_Y);
			VisibleX1 = std::min(VisibleX1, Clip.m_X + Clip.m_Width);
			VisibleY1 = std::min(VisibleY1, Clip.m_Y + Clip.m_Height);
		}

		// create the indice buffers we want to draw -- reuse them
		m_vpRenderTileLayerIndexOffsets.clear();
		m_vRenderTileLayerDrawCounts.clear();

		const auto VisibleRectRange = MapRenderScreen::TileRange(VisibleX0, VisibleY0, VisibleX1, VisibleY1, 32.0f, MapRenderScreen::ETileRangeMode::BUFFERED);
		if(VisibleRectRange)
		{
			const int X0 = std::max(VisibleRectRange->m_X0, 0);
			const int X1 = std::min(VisibleRectRange->m_X1, (int)Visuals.m_Width);
			const int Y0 = std::max(VisibleRectRange->m_Y0, 0);
			const int Y1 = std::min(VisibleRectRange->m_Y1, (int)Visuals.m_Height);
			if(X0 < X1 && Y0 < Y1)
			{
				QmBuildTileLayerDrawRanges((int)Visuals.m_Width, X0, X1, Y0, Y1,
					[&](size_t Index) { return Visuals.m_vTilesOfLayer[Index].IndexBufferByteOffset(); },
					[&](size_t Index) { return Visuals.m_vTilesOfLayer[Index].DoDraw(); },
					m_vRenderTileLayerRanges);
				if(!m_vRenderTileLayerRanges.empty())
				{
					m_vpRenderTileLayerIndexOffsets.resize(m_vRenderTileLayerRanges.size());
					m_vRenderTileLayerDrawCounts.resize(m_vRenderTileLayerRanges.size());
					for(size_t i = 0; i < m_vRenderTileLayerRanges.size(); ++i)
					{
						m_vpRenderTileLayerIndexOffsets[i] = (offset_ptr_size)m_vRenderTileLayerRanges[i].m_Offset;
						m_vRenderTileLayerDrawCounts[i] = m_vRenderTileLayerRanges[i].m_Count;
					}
					Graphics()->RenderTileLayer(Visuals.m_BufferContainerIndex, Color, m_vpRenderTileLayerIndexOffsets.data(), m_vRenderTileLayerDrawCounts.data(), m_vpRenderTileLayerIndexOffsets.size());
				}
			}
		}
	}

	if(Params.m_RenderTileBorder && (ScreenRectX1 > (int)Visuals.m_Width || ScreenRectY1 > (int)Visuals.m_Height || ScreenRectX0 < 0 || ScreenRectY0 < 0))
	{
		RenderTileBorder(Color, ScreenRectX0, ScreenRectY0, ScreenRectX1, ScreenRectY1, &Visuals);
	}
}

void CRenderLayerTile::RenderTileBorder(const ColorRGBA &Color, int BorderX0, int BorderY0, int BorderX1, int BorderY1, CTileLayerVisuals *pTileLayerVisuals)
{
	CTileLayerVisuals &Visuals = *pTileLayerVisuals;

	int Y0 = std::max(0, BorderY0);
	int X0 = std::max(0, BorderX0);
	int Y1 = std::min((int)Visuals.m_Height, BorderY1);
	int X1 = std::min((int)Visuals.m_Width, BorderX1);

	// corners
	auto DrawCorner = [&](vec2 Offset, vec2 Scale, CTileLayerVisuals::CTileVisual &Visual) {
		Offset *= 32.0f;
		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, Color, (offset_ptr_size)Visual.IndexBufferByteOffset(), Offset, Scale, 1);
	};

	if(BorderX0 < 0)
	{
		// Draw corners on left side
		if(BorderY0 < 0 && Visuals.m_BorderTopLeft.DoDraw())
		{
			DrawCorner(
				vec2(0, 0),
				vec2(std::abs(BorderX0), std::abs(BorderY0)),
				Visuals.m_BorderTopLeft);
		}
		if(BorderY1 > (int)Visuals.m_Height && Visuals.m_BorderBottomLeft.DoDraw())
		{
			DrawCorner(
				vec2(0, Visuals.m_Height),
				vec2(std::abs(BorderX0), BorderY1 - Visuals.m_Height),
				Visuals.m_BorderBottomLeft);
		}
	}
	if(BorderX1 > (int)Visuals.m_Width)
	{
		// Draw corners on right side
		if(BorderY0 < 0 && Visuals.m_BorderTopRight.DoDraw())
		{
			DrawCorner(
				vec2(Visuals.m_Width, 0),
				vec2(BorderX1 - Visuals.m_Width, std::abs(BorderY0)),
				Visuals.m_BorderTopRight);
		}
		if(BorderY1 > (int)Visuals.m_Height && Visuals.m_BorderBottomRight.DoDraw())
		{
			DrawCorner(
				vec2(Visuals.m_Width, Visuals.m_Height),
				vec2(BorderX1 - Visuals.m_Width, BorderY1 - Visuals.m_Height),
				Visuals.m_BorderBottomRight);
		}
	}

	// borders
	auto DrawBorder = [&](vec2 Offset, vec2 Scale, CTileLayerVisuals::CTileVisual &StartVisual, CTileLayerVisuals::CTileVisual &EndVisual) {
		unsigned int DrawNum = ((EndVisual.IndexBufferByteOffset() - StartVisual.IndexBufferByteOffset()) / (sizeof(unsigned int) * 6)) + (EndVisual.DoDraw() ? 1lu : 0lu);
		offset_ptr_size pOffset = (offset_ptr_size)StartVisual.IndexBufferByteOffset();
		Offset *= 32.0f;
		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, Color, pOffset, Offset, Scale, DrawNum);
	};

	if(Y0 < (int)Visuals.m_Height && Y1 > 0)
	{
		if(BorderX1 > (int)Visuals.m_Width)
		{
			// Draw right border
			DrawBorder(
				vec2(Visuals.m_Width, 0),
				vec2(BorderX1 - Visuals.m_Width, 1.f),
				Visuals.m_vBorderRight[Y0], Visuals.m_vBorderRight[Y1 - 1]);
		}
		if(BorderX0 < 0)
		{
			// Draw left border
			DrawBorder(
				vec2(0, 0),
				vec2(std::abs(BorderX0), 1),
				Visuals.m_vBorderLeft[Y0], Visuals.m_vBorderLeft[Y1 - 1]);
		}
	}

	if(X0 < (int)Visuals.m_Width && X1 > 0)
	{
		if(BorderY0 < 0)
		{
			// Draw top border
			DrawBorder(
				vec2(0, 0),
				vec2(1, std::abs(BorderY0)),
				Visuals.m_vBorderTop[X0], Visuals.m_vBorderTop[X1 - 1]);
		}
		if(BorderY1 > (int)Visuals.m_Height)
		{
			// Draw bottom border
			DrawBorder(
				vec2(0, Visuals.m_Height),
				vec2(1, BorderY1 - Visuals.m_Height),
				Visuals.m_vBorderBottom[X0], Visuals.m_vBorderBottom[X1 - 1]);
		}
	}
}

void CRenderLayerTile::RenderKillTileBorder(const ColorRGBA &Color)
{
	CTileLayerVisuals &Visuals = QmCheckedOptional(m_VisualTiles);
	if(Visuals.m_BufferContainerIndex == -1)
		return; // no visuals were created

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const auto BorderRange = MapRenderScreen::TileRange(ScreenX0, ScreenY0, ScreenX1, ScreenY1, 32.0f, MapRenderScreen::ETileRangeMode::BUFFERED);
	if(!BorderRange)
		return;
	int BorderY0 = BorderRange->m_Y0;
	int BorderX0 = BorderRange->m_X0;
	int BorderY1 = BorderRange->m_Y1;
	int BorderX1 = BorderRange->m_X1;

	if(BorderX0 >= -BorderRenderDistance && BorderY0 >= -BorderRenderDistance && BorderX1 <= (int)Visuals.m_Width + BorderRenderDistance && BorderY1 <= (int)Visuals.m_Height + BorderRenderDistance)
		return;
	if(!Visuals.m_BorderKillTile.DoDraw())
		return;

	BorderX0 = std::clamp(BorderX0, -300, (int)Visuals.m_Width + 299);
	BorderY0 = std::clamp(BorderY0, -300, (int)Visuals.m_Height + 299);
	BorderX1 = std::clamp(BorderX1, -300, (int)Visuals.m_Width + 299);
	BorderY1 = std::clamp(BorderY1, -300, (int)Visuals.m_Height + 299);

	auto DrawKillBorder = [&](vec2 Offset, vec2 Scale) {
		offset_ptr_size pOffset = (offset_ptr_size)Visuals.m_BorderKillTile.IndexBufferByteOffset();
		Offset *= 32.0f;
		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, Color, pOffset, Offset, Scale, 1);
	};

	// Draw left kill tile border
	if(BorderX0 < -BorderRenderDistance)
	{
		DrawKillBorder(
			vec2(BorderX0, BorderY0),
			vec2(-BorderRenderDistance - BorderX0, BorderY1 - BorderY0));
	}
	// Draw top kill tile border
	if(BorderY0 < -BorderRenderDistance)
	{
		DrawKillBorder(
			vec2(std::max(BorderX0, -BorderRenderDistance), BorderY0),
			vec2(std::min(BorderX1, (int)Visuals.m_Width + BorderRenderDistance) - std::max(BorderX0, -BorderRenderDistance), -BorderRenderDistance - BorderY0));
	}
	// Draw right kill tile border
	if(BorderX1 > (int)Visuals.m_Width + BorderRenderDistance)
	{
		DrawKillBorder(
			vec2(Visuals.m_Width + BorderRenderDistance, BorderY0),
			vec2(BorderX1 - (Visuals.m_Width + BorderRenderDistance), BorderY1 - BorderY0));
	}
	// Draw bottom kill tile border
	if(BorderY1 > (int)Visuals.m_Height + BorderRenderDistance)
	{
		DrawKillBorder(
			vec2(std::max(BorderX0, -BorderRenderDistance), Visuals.m_Height + BorderRenderDistance),
			vec2(std::min(BorderX1, (int)Visuals.m_Width + BorderRenderDistance) - std::max(BorderX0, -BorderRenderDistance), BorderY1 - (Visuals.m_Height + BorderRenderDistance)));
	}
}

ColorRGBA CRenderLayerTile::GetRenderColor(const CRenderLayerParams &Params) const
{
	ColorRGBA Color = m_Color;
	if(Params.m_EntityOverlayVal && Params.m_RenderType != ERenderType::RENDERTYPE_BACKGROUND_FORCE)
		Color.a *= (100 - Params.m_EntityOverlayVal) / 100.0f;

	ColorRGBA ColorEnv = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	m_pEnvelopeManager->EnvelopeEval()->EnvelopeEval(m_pLayerTilemap->m_ColorEnvOffset, m_pLayerTilemap->m_ColorEnv, ColorEnv, 4);
	Color = Color.Multiply(ColorEnv);
	return Color;
}

void CRenderLayerTile::Render(const CRenderLayerParams &Params)
{
	UseTexture(GetTexture());
	ColorRGBA Color = GetRenderColor(Params);
	if(Graphics()->IsTileBufferingEnabled() && m_TileAndQuadBuffering)
	{
		RenderTileLayerWithTileBuffer(Color, Params);
	}
	else
	{
		RenderTileLayerNoTileBuffer(Color, Params);
	}

	if(Params.m_DebugRenderTileClips && m_LayerClip.has_value())
	{
		const CClipRegion &Clip = QmCheckedOptional(m_LayerClip);
		char aDebugText[32];
		str_format(aDebugText, sizeof(aDebugText), "Group %d LayerId %d", m_GroupId, m_LayerId);
		RenderMap()->RenderDebugClip(Clip.m_X, Clip.m_Y, Clip.m_Width, Clip.m_Height, ColorRGBA(1.0f, 0.5f, 0.0f, 1.0f), Params.m_Zoom, aDebugText);
	}
}

bool CRenderLayerTile::DoRender(const CRenderLayerParams &Params)
{
	// skip rendering if we render background force, but deactivated tile layer and want to render a tilelayer
	if(!g_Config.m_ClBackgroundShowTilesLayers && Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND_FORCE)
		return false;

	// skip rendering anything but entities if we only want to render entities
	if(Params.m_EntityOverlayVal == 100 && Params.m_RenderType != ERenderType::RENDERTYPE_BACKGROUND_FORCE)
		return false;

	// skip rendering if detail layers if not wanted
	if(m_Flags & LAYERFLAG_DETAIL && !g_Config.m_GfxHighDetail && Params.m_RenderType != ERenderType::RENDERTYPE_FULL_DESIGN) // detail but no details
		return false;
	return true;
}

void CRenderLayerTile::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	RenderTileLayer(Color, Params);
}

void CRenderLayerTile::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);
	Graphics()->BlendNone();
	RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, Color, TileRenderFlags | LAYERRENDERFLAG_OPAQUE);
	Graphics()->BlendNormal();
	RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, Color, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT);
}

CMapLoadTask CRenderLayerTile::Init()
{
	if(m_pLayerTilemap->m_Image >= 0 && m_pLayerTilemap->m_Image < m_pMapImages->Num())
		m_TextureHandle = m_pMapImages->Get(m_pLayerTilemap->m_Image);
	else
		m_TextureHandle.Invalidate();
	co_await UploadTileData(m_VisualTiles, 0, false);

	// 设计层上传 GPU 后不再需要原始 tile 数据，释放可显著降低大地图内存占用。
	if(IsDesignLayer() && Graphics()->IsTileBufferingEnabled() && m_TileAndQuadBuffering)
	{
		m_pMap->UnloadData(GetDataIndex());
		m_pTiles = nullptr;
		m_TileDataLoaded = false;
	}
}

CMapLoadTask CRenderLayerTile::UploadTileData(std::optional<CTileLayerVisuals> &VisualsOptional, int CurOverlay, bool AddAsSpeedup, bool IsGameLayer, FTileFilter Filter)
{
	if(!Graphics()->IsTileBufferingEnabled() || !m_TileAndQuadBuffering)
		co_return;

	// prepare all visuals for all tile layers
	std::vector<CGraphicTile> vTmpTiles;
	std::vector<CGraphicTileTextureCoords> vTmpTileTexCoords;
	std::vector<CGraphicTile> vTmpBorderTopTiles;
	std::vector<CGraphicTileTextureCoords> vTmpBorderTopTilesTexCoords;
	std::vector<CGraphicTile> vTmpBorderLeftTiles;
	std::vector<CGraphicTileTextureCoords> vTmpBorderLeftTilesTexCoords;
	std::vector<CGraphicTile> vTmpBorderRightTiles;
	std::vector<CGraphicTileTextureCoords> vTmpBorderRightTilesTexCoords;
	std::vector<CGraphicTile> vTmpBorderBottomTiles;
	std::vector<CGraphicTileTextureCoords> vTmpBorderBottomTilesTexCoords;
	std::vector<CGraphicTile> vTmpBorderCorners;
	std::vector<CGraphicTileTextureCoords> vTmpBorderCornersTexCoords;

	// 数字覆盖层绘制时绑定独立图集，底层贴图失败也需要保留纹理坐标。
	const bool DoTextureCoords = CurOverlay != 0 || GetTexture().IsValid();

	// create the visual and set it in the optional, afterwards get it
	CTileLayerVisuals v;
	v.OnInit(this);
	VisualsOptional = v;
	CTileLayerVisuals &Visuals = QmCheckedOptional(VisualsOptional);

	if(!Visuals.Init(m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height))
		co_return;

	Visuals.m_IsTextured = DoTextureCoords;

	if(!DoTextureCoords)
	{
		vTmpTiles.reserve((size_t)m_pLayerTilemap->m_Width * m_pLayerTilemap->m_Height);
		vTmpBorderTopTiles.reserve((size_t)m_pLayerTilemap->m_Width);
		vTmpBorderBottomTiles.reserve((size_t)m_pLayerTilemap->m_Width);
		vTmpBorderLeftTiles.reserve((size_t)m_pLayerTilemap->m_Height);
		vTmpBorderRightTiles.reserve((size_t)m_pLayerTilemap->m_Height);
		vTmpBorderCorners.reserve((size_t)4);
	}
	else
	{
		vTmpTileTexCoords.reserve((size_t)m_pLayerTilemap->m_Width * m_pLayerTilemap->m_Height);
		vTmpBorderTopTilesTexCoords.reserve((size_t)m_pLayerTilemap->m_Width);
		vTmpBorderBottomTilesTexCoords.reserve((size_t)m_pLayerTilemap->m_Width);
		vTmpBorderLeftTilesTexCoords.reserve((size_t)m_pLayerTilemap->m_Height);
		vTmpBorderRightTilesTexCoords.reserve((size_t)m_pLayerTilemap->m_Height);
		vTmpBorderCornersTexCoords.reserve((size_t)4);
	}

	int DrawLeft = m_pLayerTilemap->m_Width;
	int DrawRight = 0;
	int DrawTop = m_pLayerTilemap->m_Height;
	int DrawBottom = 0;

	unsigned TilesSinceYield = 0;
	int x = 0;
	int y = 0;
	for(y = 0; y < m_pLayerTilemap->m_Height; ++y)
	{
		for(x = 0; x < m_pLayerTilemap->m_Width; ++x)
		{
			if(++TilesSinceYield == 4096)
			{
				TilesSinceYield = 0;
				co_await std::suspend_always{};
			}
			unsigned char Index = 0;
			unsigned char Flags = 0;
			int AngleRotate = -1;
			GetTileData(&Index, &Flags, &AngleRotate, x, y, CurOverlay);
			if(Filter && !Filter(Index, Flags, AngleRotate, x, y, CurOverlay))
				Index = 0;

			// the amount of tiles handled before this tile
			int TilesHandledCount = vTmpTiles.size();
			auto &TileVisual = Visuals.m_vTilesOfLayer.emplace_back();
			TileVisual.SetIndexBufferByteOffset((offset_ptr32)(TilesHandledCount));

			if(AddTile(vTmpTiles, vTmpTileTexCoords, Index, Flags, x, y, DoTextureCoords, AddAsSpeedup, AngleRotate))
			{
				TileVisual.Draw(true);

				// calculate clip region boundaries based on draws
				DrawLeft = std::min(DrawLeft, x);
				DrawRight = std::max(DrawRight, x);
				DrawTop = std::min(DrawTop, y);
				DrawBottom = std::max(DrawBottom, y);
			}

			// do the border tiles
			if(x == 0)
			{
				if(y == 0)
				{
					Visuals.m_BorderTopLeft.SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderCorners.size()));
					if(AddTile(vTmpBorderCorners, vTmpBorderCornersTexCoords, Index, Flags, 0, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{-32, -32}))
						Visuals.m_BorderTopLeft.Draw(true);
				}
				else if(y == m_pLayerTilemap->m_Height - 1)
				{
					Visuals.m_BorderBottomLeft.SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderCorners.size()));
					if(AddTile(vTmpBorderCorners, vTmpBorderCornersTexCoords, Index, Flags, 0, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{-32, 0}))
						Visuals.m_BorderBottomLeft.Draw(true);
				}
				Visuals.m_vBorderLeft[y].SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderLeftTiles.size()));
				if(AddTile(vTmpBorderLeftTiles, vTmpBorderLeftTilesTexCoords, Index, Flags, 0, y, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{-32, 0}))
					Visuals.m_vBorderLeft[y].Draw(true);
			}
			else if(x == m_pLayerTilemap->m_Width - 1)
			{
				if(y == 0)
				{
					Visuals.m_BorderTopRight.SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderCorners.size()));
					if(AddTile(vTmpBorderCorners, vTmpBorderCornersTexCoords, Index, Flags, 0, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{0, -32}))
						Visuals.m_BorderTopRight.Draw(true);
				}
				else if(y == m_pLayerTilemap->m_Height - 1)
				{
					Visuals.m_BorderBottomRight.SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderCorners.size()));
					if(AddTile(vTmpBorderCorners, vTmpBorderCornersTexCoords, Index, Flags, 0, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{0, 0}))
						Visuals.m_BorderBottomRight.Draw(true);
				}
				Visuals.m_vBorderRight[y].SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderRightTiles.size()));
				if(AddTile(vTmpBorderRightTiles, vTmpBorderRightTilesTexCoords, Index, Flags, 0, y, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{0, 0}))
					Visuals.m_vBorderRight[y].Draw(true);
			}
			if(y == 0)
			{
				Visuals.m_vBorderTop[x].SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderTopTiles.size()));
				if(AddTile(vTmpBorderTopTiles, vTmpBorderTopTilesTexCoords, Index, Flags, x, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{0, -32}))
					Visuals.m_vBorderTop[x].Draw(true);
			}
			else if(y == m_pLayerTilemap->m_Height - 1)
			{
				Visuals.m_vBorderBottom[x].SetIndexBufferByteOffset((offset_ptr32)(vTmpBorderBottomTiles.size()));
				if(AddTile(vTmpBorderBottomTiles, vTmpBorderBottomTilesTexCoords, Index, Flags, x, 0, DoTextureCoords, AddAsSpeedup, AngleRotate, ivec2{0, 0}))
					Visuals.m_vBorderBottom[x].Draw(true);
			}
		}
	}

	// shrink clip region
	// we only apply the clip once for the first overlay type (tile visuals). Physic layers can have multiple layers for text, e.g. speedup force
	// the first overlay is always the largest and you will never find an overlay, where the text is written over AIR
	if(CurOverlay == 0)
	{
		if(DrawLeft > DrawRight || DrawTop > DrawBottom)
		{
			// we are drawing nothing, layer is empty
			QmCheckedOptional(m_LayerClip).m_Height = 0.0f;
			QmCheckedOptional(m_LayerClip).m_Width = 0.0f;
		}
		else
		{
			QmCheckedOptional(m_LayerClip).m_X = DrawLeft * 32.0f;
			QmCheckedOptional(m_LayerClip).m_Y = DrawTop * 32.0f;
			QmCheckedOptional(m_LayerClip).m_Width = (DrawRight - DrawLeft + 1) * 32.0f;
			QmCheckedOptional(m_LayerClip).m_Height = (DrawBottom - DrawTop + 1) * 32.0f;
		}
	}

	// append one kill tile to the gamelayer
	if(IsGameLayer)
	{
		Visuals.m_BorderKillTile.SetIndexBufferByteOffset((offset_ptr32)(vTmpTiles.size()));
		if(AddTile(vTmpTiles, vTmpTileTexCoords, TILE_DEATH, 0, 0, 0, DoTextureCoords))
			Visuals.m_BorderKillTile.Draw(true);
	}

	// CTileVisual 的第 28 位存储绘制标记，索引的字节偏移也必须能用 offset_ptr 表示。
	constexpr uint64_t MaxTileOffset = std::min<uint64_t>(0x0fffffff, std::numeric_limits<offset_ptr>::max() / (6 * sizeof(uint32_t)));
	const uint64_t TotalTiles = static_cast<uint64_t>(vTmpTiles.size()) + vTmpBorderCorners.size() +
				    vTmpBorderTopTiles.size() + vTmpBorderBottomTiles.size() + vTmpBorderLeftTiles.size() + vTmpBorderRightTiles.size();
	if(TotalTiles > MaxTileOffset)
	{
		log_error("map/render", "Tile layer exceeds the buffered index range.");
		co_return;
	}

	// inserts and clears tiles and tile texture coords
	auto InsertTiles = [&](std::vector<CGraphicTile> &vTiles, std::vector<CGraphicTileTextureCoords> &vTexCoords) {
		vTmpTiles.insert(vTmpTiles.end(), vTiles.begin(), vTiles.end());
		vTmpTileTexCoords.insert(vTmpTileTexCoords.end(), vTexCoords.begin(), vTexCoords.end());
		vTiles.clear();
		vTexCoords.clear();
	};

	// add the border corners, then the borders and fix their byte offsets
	int TilesHandledCount = vTmpTiles.size();
	Visuals.m_BorderTopLeft.AddIndexBufferByteOffset(TilesHandledCount);
	Visuals.m_BorderTopRight.AddIndexBufferByteOffset(TilesHandledCount);
	Visuals.m_BorderBottomLeft.AddIndexBufferByteOffset(TilesHandledCount);
	Visuals.m_BorderBottomRight.AddIndexBufferByteOffset(TilesHandledCount);

	// add the Corners to the tiles
	InsertTiles(vTmpBorderCorners, vTmpBorderCornersTexCoords);

	// now the borders
	int TilesHandledCountTop = vTmpTiles.size();
	int TilesHandledCountBottom = TilesHandledCountTop + vTmpBorderTopTiles.size();
	int TilesHandledCountLeft = TilesHandledCountBottom + vTmpBorderBottomTiles.size();
	int TilesHandledCountRight = TilesHandledCountLeft + vTmpBorderLeftTiles.size();

	if(m_pLayerTilemap->m_Width > 0 && m_pLayerTilemap->m_Height > 0)
	{
		for(int i = 0; i < std::max(m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height); ++i)
		{
			if(i < m_pLayerTilemap->m_Width)
			{
				Visuals.m_vBorderTop[i].AddIndexBufferByteOffset(TilesHandledCountTop);
				Visuals.m_vBorderBottom[i].AddIndexBufferByteOffset(TilesHandledCountBottom);
			}
			if(i < m_pLayerTilemap->m_Height)
			{
				Visuals.m_vBorderLeft[i].AddIndexBufferByteOffset(TilesHandledCountLeft);
				Visuals.m_vBorderRight[i].AddIndexBufferByteOffset(TilesHandledCountRight);
			}
		}
	}

	InsertTiles(vTmpBorderTopTiles, vTmpBorderTopTilesTexCoords);
	InsertTiles(vTmpBorderBottomTiles, vTmpBorderBottomTilesTexCoords);
	InsertTiles(vTmpBorderLeftTiles, vTmpBorderLeftTilesTexCoords);
	InsertTiles(vTmpBorderRightTiles, vTmpBorderRightTilesTexCoords);

	Visuals.m_BufferContainerIndex = -1;

	size_t TileTexCoordsDataSize = 0;
	size_t TileDataSize = 0;
	size_t UploadDataSize = 0;
	if(!CheckedDataSize(vTmpTileTexCoords.size(), sizeof(CGraphicTileTextureCoords), TileTexCoordsDataSize) ||
		!CheckedDataSize(vTmpTiles.size(), sizeof(CGraphicTile), TileDataSize) ||
		!CheckedAddSize(TileTexCoordsDataSize, TileDataSize, UploadDataSize))
	{
		log_error("map/render", "Tile layer upload data size overflow.");
		co_return;
	}
	if(UploadDataSize == 0)
	{
		co_return;
	}

	std::unique_ptr<void, decltype(&free)> pUploadData(malloc(UploadDataSize), free);
	if(pUploadData == nullptr)
	{
		log_error("map/render", "Failed to allocate tile layer upload data.");
		co_return;
	}

	if(DoTextureCoords)
	{
		class CVertex
		{
		public:
			vec2 m_Pos;
			ubvec4 m_Tex;
		};

		static_assert(sizeof(CVertex) == sizeof(vec2) + sizeof(ubvec4)); // no padding

		CVertex *pDst = static_cast<CVertex *>(pUploadData.get());
		dbg_assert(UploadDataSize == vTmpTiles.size() * sizeof(*pDst) * 4, "invalid upload size");

		for(size_t TileIndex = 0; TileIndex < vTmpTiles.size(); ++TileIndex)
		{
			if(TileIndex != 0 && TileIndex % 4096 == 0)
				co_await std::suspend_always{};
			const auto &GraphicTile = vTmpTiles[TileIndex];
			const auto &GraphicCoords = vTmpTileTexCoords[TileIndex];

			*pDst++ = {GraphicTile.m_TopLeft, GraphicCoords.m_TexCoordTopLeft};
			*pDst++ = {GraphicTile.m_TopRight, GraphicCoords.m_TexCoordTopRight};
			*pDst++ = {GraphicTile.m_BottomRight, GraphicCoords.m_TexCoordBottomRight};
			*pDst++ = {GraphicTile.m_BottomLeft, GraphicCoords.m_TexCoordBottomLeft};
		}
	}
	else
	{
		// we don't have texture coords, so we can optimize
		dbg_assert(UploadDataSize == vTmpTiles.size() * sizeof(CGraphicTile), "invalid upload size");
		auto *pDst = static_cast<CGraphicTile *>(pUploadData.get());
		for(size_t Start = 0; Start < vTmpTiles.size(); Start += 4096)
		{
			if(Start != 0)
				co_await std::suspend_always{};
			const size_t Count = std::min<size_t>(4096, vTmpTiles.size() - Start);
			mem_copy(pDst + Start, vTmpTiles.data() + Start, Count * sizeof(CGraphicTile));
		}
	}

	// first create the buffer object
	Visuals.m_BufferObjectIndex = Graphics()->CreateBufferObject(UploadDataSize, pUploadData.release(), 0, true);

	// then create the buffer container
	SBufferContainerInfo ContainerInfo;
	ContainerInfo.m_Stride = (DoTextureCoords ? (sizeof(float) * 2 + sizeof(ubvec4)) : 0);
	ContainerInfo.m_VertBufferBindingIndex = Visuals.m_BufferObjectIndex;
	ContainerInfo.m_vAttributes.emplace_back();
	SBufferContainerInfo::SAttribute *pAttr = &ContainerInfo.m_vAttributes.back();
	pAttr->m_DataTypeCount = 2;
	pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
	pAttr->m_Normalized = false;
	pAttr->m_pOffset = nullptr;
	pAttr->m_FuncType = 0;
	if(DoTextureCoords)
	{
		ContainerInfo.m_vAttributes.emplace_back();
		pAttr = &ContainerInfo.m_vAttributes.back();
		pAttr->m_DataTypeCount = 4;
		pAttr->m_Type = GRAPHICS_TYPE_UNSIGNED_BYTE;
		pAttr->m_Normalized = false;
		pAttr->m_pOffset = (void *)(sizeof(vec2));
		pAttr->m_FuncType = 1;
	}

	Visuals.m_BufferContainerIndex = Graphics()->CreateBufferContainer(&ContainerInfo);
	// and finally inform the backend how many indices are required
	Graphics()->IndicesNumRequiredNotify(vTmpTiles.size() * 6);
}

void CRenderLayerTile::Unload()
{
	if(m_VisualTiles.has_value())
	{
		m_VisualTiles->Unload();
		m_VisualTiles = std::nullopt;
	}
}

void CRenderLayerTile::CTileLayerVisuals::Unload()
{
	DeleteGraphicsBufferPair(Graphics(), m_BufferContainerIndex, m_BufferObjectIndex);
}

int CRenderLayerTile::GetDataIndex() const
{
	return m_pLayerTilemap->m_Data;
}

void *CRenderLayerTile::GetRawData() const
{
	return m_pMap->GetData(GetDataIndex());
}

void CRenderLayerTile::OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering)
{
	CRenderLayer::OnInit(pGraphics, pTextRender, pRenderMap, pEnvelopeManager, pMap, pMapImages, TileAndQuadBuffering);
	// 文件声明的长度仍可能有效，必须以图层实际取得的缓冲为准。
	m_TileDataLoaded = InitTileData();
}

CMapLoadTask CRenderLayerTile::Prepare()
{
	// set clip region
	if(!Graphics()->IsTileBufferingEnabled() || !m_TileAndQuadBuffering)
	{
		// shrink clip region, this is done in `UploadTileData` for buffered backends
		int MinX = m_pLayerTilemap->m_Width;
		int MaxX = 0;
		int MinY = m_pLayerTilemap->m_Height;
		int MaxY = 0;
		unsigned TilesSinceYield = 0;
		for(int TileY = 0; TileY < m_pLayerTilemap->m_Height; ++TileY)
		{
			for(int TileX = 0; TileX < m_pLayerTilemap->m_Width; ++TileX)
			{
				if(++TilesSinceYield == 4096)
				{
					TilesSinceYield = 0;
					co_await std::suspend_always{};
				}
				unsigned char Index = 0;
				unsigned char Flags = 0;
				int Angle = 0;
				GetTileData(&Index, &Flags, &Angle, static_cast<unsigned int>(TileX), static_cast<unsigned int>(TileY), 0);

				if(Index > 0)
				{
					MinX = std::min(TileX, MinX);
					MaxX = std::max(TileX, MaxX);
					MinY = std::min(TileY, MinY);
					MaxY = std::max(TileY, MaxY);
				}
			}
		}

		if(MinX > MaxX || MinY > MaxY)
		{
			// layer is empty
			m_LayerClip = CClipRegion(0.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			m_LayerClip = CClipRegion(MinX * 32.0f, MinY * 32.0f, (MaxX - MinX + 1) * 32.0f, (MaxY - MinY + 1) * 32.0f);
		}
	}
	else
	{
		m_LayerClip = CClipRegion(0.0f, 0.0f, m_pLayerTilemap->m_Width * 32.0f, m_pLayerTilemap->m_Height * 32.0f);
	}
	co_await Init();
}

bool CRenderLayerTile::InitTileData()
{
	m_pTiles = GetData<CTile>();
	return m_pTiles != nullptr;
}

template<class T>
T *CRenderLayerTile::GetData() const
{
	return (T *)GetRawData();
}

void CRenderLayerTile::GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const
{
	*pIndex = m_pTiles[y * m_pLayerTilemap->m_Width + x].m_Index;
	*pFlags = m_pTiles[y * m_pLayerTilemap->m_Width + x].m_Flags;
}

/**************
 * Quad Layer *
 **************/

CRenderLayerQuads::CRenderLayerQuads(int GroupId, int LayerId, int Flags, CMapItemLayerQuads *pLayerQuads) :
	CRenderLayer(GroupId, LayerId, Flags)
{
	m_pLayerQuads = pLayerQuads;
	m_pQuads = nullptr;
}

void CRenderLayerQuads::RenderQuadLayer(float Alpha, const CRenderLayerParams &Params)
{
	CQuadLayerVisuals &Visuals = QmCheckedOptional(m_VisualQuad);
	if(Visuals.m_BufferContainerIndex == -1)
		return; // no visuals were created

	std::optional<std::span<const CQuad>> Quads;
	for(auto &QuadCluster : m_vQuadClusters)
	{
		if(!IsVisibleInClipRegion(QuadCluster.m_ClipRegion))
			continue;

		if(!QuadCluster.m_Grouped)
		{
			// 图层上传后共享的数据块可能已经释放，绘制时按需重新借用。
			if(!Quads)
				Quads.emplace(MapRenderItems::QuadData(*m_pMap, *m_pLayerQuads));
			if(Quads->empty())
				continue;

			bool AnyVisible = false;
			for(int QuadClusterId = 0; QuadClusterId < QuadCluster.m_NumQuads; ++QuadClusterId)
			{
				const CQuad *pQuad = &(*Quads)[QuadCluster.m_StartIndex + QuadClusterId];

				ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				if(pQuad->m_ColorEnv >= 0)
				{
					m_pEnvelopeManager->EnvelopeEval()->EnvelopeEval(pQuad->m_ColorEnvOffset, pQuad->m_ColorEnv, Color, 4);
				}
				Color.a *= Alpha;

				SQuadRenderInfo &QInfo = QuadCluster.m_vQuadRenderInfo[QuadClusterId];
				if(Color.a < 0.0f)
					Color.a = 0.0f;
				QInfo.m_Color = Color;
				if(Color.a > 0.0f)
				{
					AnyVisible = true;
					ColorRGBA Position = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
					m_pEnvelopeManager->EnvelopeEval()->EnvelopeEval(pQuad->m_PosEnvOffset, pQuad->m_PosEnv, Position, 3);
					QInfo.m_Offsets.x = Position.r;
					QInfo.m_Offsets.y = Position.g;
					QInfo.m_Rotation = Position.b / 180.0f * pi;
				}
			}
			if(AnyVisible)
				Graphics()->RenderQuadLayer(Visuals.m_BufferContainerIndex, QuadCluster.m_vQuadRenderInfo.data(), QuadCluster.m_NumQuads, QuadCluster.m_StartIndex);
		}
		else
		{
			SQuadRenderInfo &QInfo = QuadCluster.m_vQuadRenderInfo[0];

			ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			if(QuadCluster.m_ColorEnv >= 0)
			{
				m_pEnvelopeManager->EnvelopeEval()->EnvelopeEval(QuadCluster.m_ColorEnvOffset, QuadCluster.m_ColorEnv, Color, 4);
			}

			Color.a *= Alpha;
			if(Color.a <= 0.0f)
				continue;
			QInfo.m_Color = Color;

			if(QuadCluster.m_PosEnv >= 0)
			{
				ColorRGBA Position = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
				m_pEnvelopeManager->EnvelopeEval()->EnvelopeEval(QuadCluster.m_PosEnvOffset, QuadCluster.m_PosEnv, Position, 3);

				QInfo.m_Offsets.x = Position.r;
				QInfo.m_Offsets.y = Position.g;
				QInfo.m_Rotation = Position.b / 180.0f * pi;
			}
			Graphics()->RenderQuadLayer(Visuals.m_BufferContainerIndex, &QInfo, (size_t)QuadCluster.m_NumQuads, QuadCluster.m_StartIndex, true);
		}
	}

	if(Params.m_DebugRenderClusterClips)
	{
		for(auto &QuadCluster : m_vQuadClusters)
		{
			if(!IsVisibleInClipRegion(QuadCluster.m_ClipRegion) || !QuadCluster.m_ClipRegion.has_value())
				continue;

			char aDebugText[64];
			str_format(aDebugText, sizeof(aDebugText), "Group %d, quad layer %d, quad start %d, grouped %d", m_GroupId, m_LayerId, QuadCluster.m_StartIndex, QuadCluster.m_Grouped);
			RenderMap()->RenderDebugClip(QuadCluster.m_ClipRegion->m_X, QuadCluster.m_ClipRegion->m_Y, QuadCluster.m_ClipRegion->m_Width, QuadCluster.m_ClipRegion->m_Height, ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f), Params.m_Zoom, aDebugText);
		}
	}
}

void CRenderLayerQuads::OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering)
{
	CRenderLayer::OnInit(pGraphics, pTextRender, pRenderMap, pEnvelopeManager, pMap, pMapImages, TileAndQuadBuffering);
	m_pQuads = MapRenderItems::QuadData(*m_pMap, *m_pLayerQuads).data();
}

CMapLoadTask CRenderLayerQuads::Init()
{
	m_LayerClip.reset();
	if(m_pLayerQuads->m_Image >= 0 && m_pLayerQuads->m_Image < m_pMapImages->Num())
		m_TextureHandle = m_pMapImages->Get(m_pLayerQuads->m_Image);
	else
		m_TextureHandle.Invalidate();

	if(!Graphics()->IsQuadBufferingEnabled() || !m_TileAndQuadBuffering)
	{
		// create clip region for unbuffered backends
		CQuadCluster QuadCluster;
		QuadCluster.m_Grouped = false;
		QuadCluster.m_StartIndex = 0;
		QuadCluster.m_NumQuads = m_pLayerQuads->m_NumQuads;

		// unused, because cluster is not grouped
		QuadCluster.m_PosEnv = -1;
		QuadCluster.m_PosEnvOffset = 0;
		QuadCluster.m_ColorEnv = -1;
		QuadCluster.m_ColorEnvOffset = 0;

		co_await CalculateClipping(QuadCluster);
		co_return;
	}

	CQuadLayerVisuals v;
	v.OnInit(this);
	m_VisualQuad = v;
	CQuadLayerVisuals *pQLayerVisuals = &QmCheckedOptional(m_VisualQuad);

	const bool Textured = GetTexture().IsValid();

	size_t UploadDataSize = 0;
	if(!CheckedDataSize(m_pLayerQuads->m_NumQuads, Textured ? sizeof(CTmpQuadTextured) : sizeof(CTmpQuad), UploadDataSize))
	{
		log_error("map/render", "Quad layer upload data size overflow.");
		co_return;
	}
	if(UploadDataSize == 0)
		co_return;
	// 分段直接填充提交缓冲，完成后由渲染后端接管，取消时由当前任务释放。
	std::unique_ptr<void, decltype(&free)> pUploadData(malloc(UploadDataSize), free);
	if(!pUploadData)
	{
		log_error("map/render", "Failed to allocate quad layer upload data.");
		co_return;
	}
	auto *pQuads = static_cast<CTmpQuad *>(pUploadData.get());
	auto *pQuadsTextured = static_cast<CTmpQuadTextured *>(pUploadData.get());

	auto SetQuadRenderInfo = [&](SQuadRenderInfo &QInfo, int QuadId, bool InitInfo) {
		CQuad *pQuad = &m_pQuads[QuadId];

		// init for envelopeless quad layers
		if(InitInfo)
		{
			QInfo.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			QInfo.m_Offsets.x = 0;
			QInfo.m_Offsets.y = 0;
			QInfo.m_Rotation = 0;
		}

		for(int j = 0; j < 4; ++j)
		{
			int QuadIdX = j;
			if(j == 2)
				QuadIdX = 3;
			else if(j == 3)
				QuadIdX = 2;
			if(!Textured)
			{
				// ignore the conversion for the position coordinates
				pQuads[QuadId].m_aVertices[j].m_X = fx2f(pQuad->m_aPoints[QuadIdX].x);
				pQuads[QuadId].m_aVertices[j].m_Y = fx2f(pQuad->m_aPoints[QuadIdX].y);
				pQuads[QuadId].m_aVertices[j].m_CenterX = fx2f(pQuad->m_aPoints[4].x);
				pQuads[QuadId].m_aVertices[j].m_CenterY = fx2f(pQuad->m_aPoints[4].y);
				pQuads[QuadId].m_aVertices[j].m_R = (unsigned char)pQuad->m_aColors[QuadIdX].r;
				pQuads[QuadId].m_aVertices[j].m_G = (unsigned char)pQuad->m_aColors[QuadIdX].g;
				pQuads[QuadId].m_aVertices[j].m_B = (unsigned char)pQuad->m_aColors[QuadIdX].b;
				pQuads[QuadId].m_aVertices[j].m_A = (unsigned char)pQuad->m_aColors[QuadIdX].a;
			}
			else
			{
				// ignore the conversion for the position coordinates
				pQuadsTextured[QuadId].m_aVertices[j].m_X = fx2f(pQuad->m_aPoints[QuadIdX].x);
				pQuadsTextured[QuadId].m_aVertices[j].m_Y = fx2f(pQuad->m_aPoints[QuadIdX].y);
				pQuadsTextured[QuadId].m_aVertices[j].m_CenterX = fx2f(pQuad->m_aPoints[4].x);
				pQuadsTextured[QuadId].m_aVertices[j].m_CenterY = fx2f(pQuad->m_aPoints[4].y);
				pQuadsTextured[QuadId].m_aVertices[j].m_U = fx2f(pQuad->m_aTexcoords[QuadIdX].x);
				pQuadsTextured[QuadId].m_aVertices[j].m_V = fx2f(pQuad->m_aTexcoords[QuadIdX].y);
				pQuadsTextured[QuadId].m_aVertices[j].m_R = (unsigned char)pQuad->m_aColors[QuadIdX].r;
				pQuadsTextured[QuadId].m_aVertices[j].m_G = (unsigned char)pQuad->m_aColors[QuadIdX].g;
				pQuadsTextured[QuadId].m_aVertices[j].m_B = (unsigned char)pQuad->m_aColors[QuadIdX].b;
				pQuadsTextured[QuadId].m_aVertices[j].m_A = (unsigned char)pQuad->m_aColors[QuadIdX].a;
			}
		}
	};

	m_vQuadClusters.clear();

	// create quad clusters
	bool AllClustersBounded = true;
	int QuadStart = 0;
	while(QuadStart < m_pLayerQuads->m_NumQuads)
	{
		CQuadCluster QuadCluster;
		QuadCluster.m_StartIndex = QuadStart;
		QuadCluster.m_Grouped = true;
		QuadCluster.m_ColorEnv = m_pQuads[QuadStart].m_ColorEnv;
		QuadCluster.m_ColorEnvOffset = m_pQuads[QuadStart].m_ColorEnvOffset;
		QuadCluster.m_PosEnv = m_pQuads[QuadStart].m_PosEnv;
		QuadCluster.m_PosEnvOffset = m_pQuads[QuadStart].m_PosEnvOffset;

		int QuadOffset = 0;
		for(int QuadClusterId = 0; QuadClusterId < m_pLayerQuads->m_NumQuads - QuadStart; ++QuadClusterId)
		{
			if(QuadClusterId != 0 && QuadClusterId % 1024 == 0)
				co_await std::suspend_always{};
			const CQuad *pQuad = &m_pQuads[QuadStart + QuadClusterId];
			bool IsGrouped = QuadCluster.m_Grouped && pQuad->m_ColorEnv == QuadCluster.m_ColorEnv && pQuad->m_ColorEnvOffset == QuadCluster.m_ColorEnvOffset && pQuad->m_PosEnv == QuadCluster.m_PosEnv && pQuad->m_PosEnvOffset == QuadCluster.m_PosEnvOffset;

			// we are reaching gpu batch limit, here we break and close the QuadCluster if it's ungrouped
			if(QuadClusterId >= (int)gs_GraphicsMaxQuadsRenderCount)
			{
				// expand a cluster, if it's grouped
				if(!IsGrouped)
					break;
			}
			QuadOffset++;
			QuadCluster.m_Grouped = IsGrouped;
		}
		QuadCluster.m_NumQuads = QuadOffset;

		// fill cluster info
		if(QuadCluster.m_Grouped)
		{
			// grouped quads only need one render info, because all their envs and env offsets are equal
			QuadCluster.m_vQuadRenderInfo.resize(1);
			for(int QuadClusterId = 0; QuadClusterId < QuadCluster.m_NumQuads; ++QuadClusterId)
			{
				if(QuadClusterId != 0 && QuadClusterId % 1024 == 0)
					co_await std::suspend_always{};
				SetQuadRenderInfo(QuadCluster.m_vQuadRenderInfo[0], QuadCluster.m_StartIndex + QuadClusterId, QuadClusterId == 0);
			}
		}
		else
		{
			QuadCluster.m_vQuadRenderInfo.resize(QuadCluster.m_NumQuads);
			for(int QuadClusterId = 0; QuadClusterId < QuadCluster.m_NumQuads; ++QuadClusterId)
			{
				if(QuadClusterId != 0 && QuadClusterId % 1024 == 0)
					co_await std::suspend_always{};
				SetQuadRenderInfo(QuadCluster.m_vQuadRenderInfo[QuadClusterId], QuadCluster.m_StartIndex + QuadClusterId, true);
			}
		}

		co_await CalculateClipping(QuadCluster);

		if(!QuadCluster.m_ClipRegion.has_value())
			AllClustersBounded = false;
		m_vQuadClusters.push_back(std::move(QuadCluster));
		QuadStart += QuadOffset;
	}
	if(!AllClustersBounded)
		m_LayerClip.reset();

	// gpu upload
	pQLayerVisuals->m_BufferObjectIndex = Graphics()->CreateBufferObject(UploadDataSize, pUploadData.release(), 0, true);
	// then create the buffer container
	SBufferContainerInfo ContainerInfo;
	ContainerInfo.m_Stride = (Textured ? (sizeof(CTmpQuadTextured) / 4) : (sizeof(CTmpQuad) / 4));
	ContainerInfo.m_VertBufferBindingIndex = pQLayerVisuals->m_BufferObjectIndex;
	ContainerInfo.m_vAttributes.emplace_back();
	SBufferContainerInfo::SAttribute *pAttr = &ContainerInfo.m_vAttributes.back();
	pAttr->m_DataTypeCount = 4;
	pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
	pAttr->m_Normalized = false;
	pAttr->m_pOffset = nullptr;
	pAttr->m_FuncType = 0;
	ContainerInfo.m_vAttributes.emplace_back();
	pAttr = &ContainerInfo.m_vAttributes.back();
	pAttr->m_DataTypeCount = 4;
	pAttr->m_Type = GRAPHICS_TYPE_UNSIGNED_BYTE;
	pAttr->m_Normalized = true;
	pAttr->m_pOffset = (void *)(sizeof(float) * 4);
	pAttr->m_FuncType = 0;
	if(Textured)
	{
		ContainerInfo.m_vAttributes.emplace_back();
		pAttr = &ContainerInfo.m_vAttributes.back();
		pAttr->m_DataTypeCount = 2;
		pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
		pAttr->m_Normalized = false;
		pAttr->m_pOffset = (void *)(sizeof(float) * 4 + sizeof(unsigned char) * 4);
		pAttr->m_FuncType = 0;
	}

	pQLayerVisuals->m_BufferContainerIndex = Graphics()->CreateBufferContainer(&ContainerInfo);
	// and finally inform the backend how many indices are required
	Graphics()->IndicesNumRequiredNotify(m_pLayerQuads->m_NumQuads * 6);
}

void CRenderLayerQuads::Unload()
{
	if(m_VisualQuad.has_value())
	{
		m_VisualQuad->Unload();
		m_VisualQuad = std::nullopt;
	}
}

void CRenderLayerQuads::CQuadLayerVisuals::Unload()
{
	DeleteGraphicsBufferPair(Graphics(), m_BufferContainerIndex, m_BufferObjectIndex);
}

CMapLoadTask CRenderLayerQuads::CalculateQuadClipping(const CQuadCluster &QuadCluster, float aQuadOffsetMin[2], float aQuadOffsetMax[2], bool &Available) const
{
	// check if the grouped clipping is available for early exit
	if(QuadCluster.m_Grouped)
	{
		const CEnvelopeExtrema::CEnvelopeExtremaItem &Extrema = m_pEnvelopeManager->EnvelopeExtrema()->GetExtrema(QuadCluster.m_PosEnv);
		if(!Extrema.m_Available)
			co_return;
	}

	// calculate quad position offsets
	for(int Channel = 0; Channel < 2; ++Channel)
	{
		aQuadOffsetMin[Channel] = std::numeric_limits<float>::max(); // minimum of channel
		aQuadOffsetMax[Channel] = std::numeric_limits<float>::lowest(); // maximum of channel
	}

	for(int QuadId = QuadCluster.m_StartIndex; QuadId < QuadCluster.m_StartIndex + QuadCluster.m_NumQuads; ++QuadId)
	{
		if(QuadId != QuadCluster.m_StartIndex && QuadId % 1024 == 0)
			co_await std::suspend_always{};
		const CQuad *pQuad = &m_pQuads[QuadId];

		const CEnvelopeExtrema::CEnvelopeExtremaItem &Extrema = m_pEnvelopeManager->EnvelopeExtrema()->GetExtrema(pQuad->m_PosEnv);
		if(!Extrema.m_Available)
			co_return;

		// calculate clip region
		if(!Extrema.m_Rotating)
		{
			for(int QuadIdPoint = 0; QuadIdPoint < 4; ++QuadIdPoint)
			{
				for(int Channel = 0; Channel < 2; ++Channel)
				{
					float OffsetMinimum = fx2f(pQuad->m_aPoints[QuadIdPoint][Channel]);
					float OffsetMaximum = fx2f(pQuad->m_aPoints[QuadIdPoint][Channel]);

					// calculate env offsets for every ungrouped quad
					if(!QuadCluster.m_Grouped && pQuad->m_PosEnv >= 0)
					{
						OffsetMinimum += fx2f(Extrema.m_Minima[Channel]);
						OffsetMaximum += fx2f(Extrema.m_Maxima[Channel]);
					}
					aQuadOffsetMin[Channel] = std::min(aQuadOffsetMin[Channel], OffsetMinimum);
					aQuadOffsetMax[Channel] = std::max(aQuadOffsetMax[Channel], OffsetMaximum);
				}
			}
		}
		else
		{
			const CPoint &CenterFX = pQuad->m_aPoints[4];
			vec2 Center(fx2f(CenterFX.x), fx2f(CenterFX.y));
			float MaxDistance = 0;
			for(int QuadIdPoint = 0; QuadIdPoint < 4; ++QuadIdPoint)
			{
				const CPoint &QuadPointFX = pQuad->m_aPoints[QuadIdPoint];
				vec2 QuadPoint(fx2f(QuadPointFX.x), fx2f(QuadPointFX.y));
				float Distance = length(Center - QuadPoint);
				MaxDistance = std::max(Distance, MaxDistance);
			}

			for(int Channel = 0; Channel < 2; ++Channel)
			{
				float OffsetMinimum = Center[Channel] - MaxDistance;
				float OffsetMaximum = Center[Channel] + MaxDistance;
				if(!QuadCluster.m_Grouped && pQuad->m_PosEnv >= 0)
				{
					OffsetMinimum += fx2f(Extrema.m_Minima[Channel]);
					OffsetMaximum += fx2f(Extrema.m_Maxima[Channel]);
				}
				aQuadOffsetMin[Channel] = std::min(aQuadOffsetMin[Channel], OffsetMinimum);
				aQuadOffsetMax[Channel] = std::max(aQuadOffsetMax[Channel], OffsetMaximum);
			}
		}
	}

	// add env offsets for the quad group
	if(QuadCluster.m_Grouped && QuadCluster.m_PosEnv >= 0)
	{
		const CEnvelopeExtrema::CEnvelopeExtremaItem &Extrema = m_pEnvelopeManager->EnvelopeExtrema()->GetExtrema(QuadCluster.m_PosEnv);

		for(int Channel = 0; Channel < 2; ++Channel)
		{
			aQuadOffsetMin[Channel] += fx2f(Extrema.m_Minima[Channel]);
			aQuadOffsetMax[Channel] += fx2f(Extrema.m_Maxima[Channel]);
		}
	}
	Available = true;
}

CMapLoadTask CRenderLayerQuads::CalculateClipping(CQuadCluster &QuadCluster)
{
	float aQuadOffsetMin[2];
	float aQuadOffsetMax[2];

	bool CreateClip = false;
	co_await CalculateQuadClipping(QuadCluster, aQuadOffsetMin, aQuadOffsetMax, CreateClip);

	if(!CreateClip)
		co_return;

	QuadCluster.m_ClipRegion = std::make_optional<CClipRegion>();
	std::optional<CClipRegion> &ClipRegion = QuadCluster.m_ClipRegion;

	// X channel
	ClipRegion->m_X = aQuadOffsetMin[0];
	ClipRegion->m_Width = aQuadOffsetMax[0] - aQuadOffsetMin[0];

	// Y channel
	ClipRegion->m_Y = aQuadOffsetMin[1];
	ClipRegion->m_Height = aQuadOffsetMax[1] - aQuadOffsetMin[1];

	// update layer clip
	if(!m_LayerClip.has_value())
	{
		m_LayerClip = ClipRegion;
	}
	else
	{
		auto &LayerClip = QmCheckedOptional(m_LayerClip);
		float ClipRight = std::max(ClipRegion->m_X + ClipRegion->m_Width, LayerClip.m_X + LayerClip.m_Width);
		float ClipBottom = std::max(ClipRegion->m_Y + ClipRegion->m_Height, LayerClip.m_Y + LayerClip.m_Height);
		LayerClip.m_X = std::min(ClipRegion->m_X, LayerClip.m_X);
		LayerClip.m_Y = std::min(ClipRegion->m_Y, LayerClip.m_Y);
		LayerClip.m_Width = ClipRight - LayerClip.m_X;
		LayerClip.m_Height = ClipBottom - LayerClip.m_Y;
	}
}

void CRenderLayerQuads::Render(const CRenderLayerParams &Params)
{
	UseTexture(GetTexture());

	bool Force = Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND_FORCE || Params.m_RenderType == ERenderType::RENDERTYPE_FULL_DESIGN;
	float Alpha = Force ? 1.f : (100 - Params.m_EntityOverlayVal) / 100.0f;
	if(!Graphics()->IsQuadBufferingEnabled() || !m_TileAndQuadBuffering)
	{
		const auto Quads = MapRenderItems::QuadData(*m_pMap, *m_pLayerQuads);
		if(!Quads.empty())
			RenderMap()->ForceRenderQuads(Quads.data(), m_pLayerQuads->m_NumQuads, m_pEnvelopeManager->EnvelopeEval(), Alpha);
	}
	else
	{
		RenderQuadLayer(Alpha, Params);
	}

	if(Params.m_DebugRenderQuadClips && m_LayerClip.has_value())
	{
		char aDebugText[64];
		str_format(aDebugText, sizeof(aDebugText), "Group %d, quad layer %d", m_GroupId, m_LayerId);
		const auto &LayerClip = QmCheckedOptional(m_LayerClip);
		RenderMap()->RenderDebugClip(LayerClip.m_X, LayerClip.m_Y, LayerClip.m_Width, LayerClip.m_Height, ColorRGBA(1.0f, 0.0f, 0.5f, 1.0f), Params.m_Zoom, aDebugText);
	}
}

bool CRenderLayerQuads::DoRender(const CRenderLayerParams &Params)
{
	// skip rendering anything but entities if we only want to render entities
	if(Params.m_EntityOverlayVal == 100 && Params.m_RenderType != ERenderType::RENDERTYPE_BACKGROUND_FORCE)
		return false;

	// skip rendering if detail layers if not wanted
	if(m_Flags & LAYERFLAG_DETAIL && !g_Config.m_GfxHighDetail && Params.m_RenderType != ERenderType::RENDERTYPE_FULL_DESIGN) // detail but no details
		return false;

	// this option only deactivates quads in the background
	if(Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND || Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND_FORCE)
	{
		if(!g_Config.m_ClShowQuads)
			return false;
	}

	return IsVisibleInClipRegion(m_LayerClip);
}

/****************
 * Entity Layer *
 ****************/
static bool IsFreezeAlphaTile(int Index)
{
	return CQmEntityOverlayBuildPlan::Classify(Index) == CQmEntityOverlayBuildPlan::FREEZE;
}

static bool IsUnfreezeAlphaTile(int Index)
{
	return CQmEntityOverlayBuildPlan::Classify(Index) == CQmEntityOverlayBuildPlan::UNFREEZE;
}

static bool IsDeepFreezeAlphaTile(int Index)
{
	return CQmEntityOverlayBuildPlan::Classify(Index) == CQmEntityOverlayBuildPlan::DEEP_FREEZE;
}

static bool IsDeepUnfreezeAlphaTile(int Index)
{
	return CQmEntityOverlayBuildPlan::Classify(Index) == CQmEntityOverlayBuildPlan::DEEP_UNFREEZE;
}

static bool IsSpecialEntityAlphaTile(int Index)
{
	return CQmEntityOverlayBuildPlan::Classify(Index) != CQmEntityOverlayBuildPlan::NUM_CATEGORIES;
}

static bool IsSpecialSwitchAlphaTile(int Index)
{
	const auto Category = CQmEntityOverlayBuildPlan::Classify(Index);
	return Category != CQmEntityOverlayBuildPlan::NUM_CATEGORIES && Category != CQmEntityOverlayBuildPlan::DEATH;
}

static bool FilterBaseEntityAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return !IsSpecialEntityAlphaTile(Index);
}

static bool FilterDeathAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return Index == TILE_DEATH;
}

static bool FilterFreezeAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return IsFreezeAlphaTile(Index);
}

static bool FilterUnfreezeAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return IsUnfreezeAlphaTile(Index);
}

static bool FilterDeepFreezeAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return IsDeepFreezeAlphaTile(Index);
}

static bool FilterDeepUnfreezeAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return IsDeepUnfreezeAlphaTile(Index);
}

static bool IsTeleCheckpointAlphaTile(int Index)
{
	return IsTeleTileCheckpoint(Index);
}

static bool FilterBaseTeleAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return !IsTeleCheckpointAlphaTile(Index);
}

static bool FilterCheckpointTeleAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return IsTeleCheckpointAlphaTile(Index);
}

static bool FilterBaseSwitchAlphaTile(unsigned char Index, void *pUser)
{
	(void)pUser;
	return !IsSpecialSwitchAlphaTile(Index);
}

// BASE
CRenderLayerEntityBase::CRenderLayerEntityBase(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerTile(GroupId, LayerId, Flags, pLayerTilemap) {}

bool CRenderLayerEntityBase::DoRender(const CRenderLayerParams &Params)
{
	// skip rendering if we render background force or full design
	if(Params.m_RenderType == ERenderType::RENDERTYPE_BACKGROUND_FORCE || Params.m_RenderType == ERenderType::RENDERTYPE_FULL_DESIGN)
		return false;

	// skip rendering of entities if don't want them
	if(!Params.m_EntityOverlayVal)
		return false;

	return true;
}

IGraphics::CTextureHandle CRenderLayerEntityBase::GetTexture() const
{
	return m_pMapImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
}

// GAME
CRenderLayerEntityGame::CRenderLayerEntityGame(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

CMapLoadTask CRenderLayerEntityGame::Init()
{
	const std::optional<CClipRegion> LayerClip = m_LayerClip;
	CQmEntityOverlayBuildPlan Plan;
	co_await UploadTileData(m_VisualTiles, 0, false, true, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Flags;
		(void)AngleRotate;
		(void)x;
		(void)y;
		(void)CurOverlay;
		Plan.Record(Index);
		return !IsSpecialEntityAlphaTile(Index);
	});
	m_LayerClip = LayerClip;
	for(auto Category = Plan.NextPresent(); Category != CQmEntityOverlayBuildPlan::NUM_CATEGORIES; Category = Plan.NextPresent(Category + 1))
	{
		std::optional<CTileLayerVisuals> *pVisuals = nullptr;
		switch(Category)
		{
		case CQmEntityOverlayBuildPlan::DEATH: pVisuals = &m_VisualTilesDeath; break;
		case CQmEntityOverlayBuildPlan::FREEZE: pVisuals = &m_VisualTilesFreeze; break;
		case CQmEntityOverlayBuildPlan::UNFREEZE: pVisuals = &m_VisualTilesUnfreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_FREEZE: pVisuals = &m_VisualTilesDeepFreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_UNFREEZE: pVisuals = &m_VisualTilesDeepUnfreeze; break;
		case CQmEntityOverlayBuildPlan::NUM_CATEGORIES: continue;
		}
		co_await UploadTileData(*pVisuals, 0, false, false, [Category](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
			(void)Flags;
			(void)AngleRotate;
			(void)x;
			(void)y;
			(void)CurOverlay;
			return CQmEntityOverlayBuildPlan::Classify(Index) == Category;
		});
		m_LayerClip = LayerClip;
	}
}

void CRenderLayerEntityGame::Unload()
{
	CRenderLayerTile::Unload();
	if(m_VisualTilesDeath.has_value())
	{
		m_VisualTilesDeath->Unload();
		m_VisualTilesDeath = std::nullopt;
	}
	if(m_VisualTilesFreeze.has_value())
	{
		m_VisualTilesFreeze->Unload();
		m_VisualTilesFreeze = std::nullopt;
	}
	if(m_VisualTilesUnfreeze.has_value())
	{
		m_VisualTilesUnfreeze->Unload();
		m_VisualTilesUnfreeze = std::nullopt;
	}
	if(m_VisualTilesDeepFreeze.has_value())
	{
		m_VisualTilesDeepFreeze->Unload();
		m_VisualTilesDeepFreeze = std::nullopt;
	}
	if(m_VisualTilesDeepUnfreeze.has_value())
	{
		m_VisualTilesDeepUnfreeze->Unload();
		m_VisualTilesDeepUnfreeze = std::nullopt;
	}
}

void CRenderLayerEntityGame::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	if(Params.m_RenderTileBorder)
		RenderKillTileBorder(Color.Multiply(GetDeathBorderColor()));
	RenderTileLayer(Color, Params);

	auto RenderFiltered = [&](std::optional<CTileLayerVisuals> &Visuals, float Alpha) {
		if(!Visuals.has_value() || Alpha <= 0.0f)
			return;
		RenderTileLayer(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), Params, &QmCheckedOptional(Visuals));
	};

	RenderFiltered(m_VisualTilesDeath, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeathAlpha));
	RenderFiltered(m_VisualTilesFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha));
	RenderFiltered(m_VisualTilesUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha));
	RenderFiltered(m_VisualTilesDeepFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha));
	RenderFiltered(m_VisualTilesDeepUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha));
}

void CRenderLayerEntityGame::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);

	auto RenderFiltered = [&](ColorRGBA GroupColor, CRenderMap::FTileRenderFilter Filter) {
		if(GroupColor.a <= 0.0f)
			return;
		Graphics()->BlendNone();
		RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_OPAQUE, Filter);
		Graphics()->BlendNormal();
		RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT, Filter);
	};

	RenderFiltered(Color, FilterBaseEntityAlphaTile);

	if(Params.m_RenderTileBorder)
	{
		RenderMap()->RenderTileRectangle(-BorderRenderDistance, -BorderRenderDistance, m_pLayerTilemap->m_Width + 2 * BorderRenderDistance, m_pLayerTilemap->m_Height + 2 * BorderRenderDistance,
			TILE_AIR, TILE_DEATH, // display air inside, death outside
			32.0f, Color.Multiply(GetDeathBorderColor()), TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT);
	}

	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeathAlpha)), FilterDeathAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha)), FilterFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha)), FilterUnfreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha)), FilterDeepFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha)), FilterDeepUnfreezeAlphaTile);
}

ColorRGBA CRenderLayerEntityGame::GetDeathBorderColor() const
{
	// draw kill tiles outside the entity clipping rectangle
	// slow blinking to hint that it's not a part of the map
	float Seconds = time_get() / (float)time_freq();
	float Alpha = 0.3f + 0.35f * (1.f + std::sin(2.f * pi * Seconds / 3.f));
	return ColorRGBA(1.f, 1.f, 1.f, Alpha);
}

// FRONT
CRenderLayerEntityFront::CRenderLayerEntityFront(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

int CRenderLayerEntityFront::GetDataIndex() const
{
	return m_pLayerTilemap->m_Front;
}

CMapLoadTask CRenderLayerEntityFront::Init()
{
	const std::optional<CClipRegion> LayerClip = m_LayerClip;
	CQmEntityOverlayBuildPlan Plan;
	co_await UploadTileData(m_VisualTiles, 0, false, false, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Flags;
		(void)AngleRotate;
		(void)x;
		(void)y;
		(void)CurOverlay;
		Plan.Record(Index);
		return !IsSpecialEntityAlphaTile(Index);
	});
	m_LayerClip = LayerClip;
	for(auto Category = Plan.NextPresent(); Category != CQmEntityOverlayBuildPlan::NUM_CATEGORIES; Category = Plan.NextPresent(Category + 1))
	{
		std::optional<CTileLayerVisuals> *pVisuals = nullptr;
		switch(Category)
		{
		case CQmEntityOverlayBuildPlan::DEATH: pVisuals = &m_VisualTilesDeath; break;
		case CQmEntityOverlayBuildPlan::FREEZE: pVisuals = &m_VisualTilesFreeze; break;
		case CQmEntityOverlayBuildPlan::UNFREEZE: pVisuals = &m_VisualTilesUnfreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_FREEZE: pVisuals = &m_VisualTilesDeepFreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_UNFREEZE: pVisuals = &m_VisualTilesDeepUnfreeze; break;
		case CQmEntityOverlayBuildPlan::NUM_CATEGORIES: continue;
		}
		co_await UploadTileData(*pVisuals, 0, false, false, [Category](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
			(void)Flags;
			(void)AngleRotate;
			(void)x;
			(void)y;
			(void)CurOverlay;
			return CQmEntityOverlayBuildPlan::Classify(Index) == Category;
		});
		m_LayerClip = LayerClip;
	}
}

void CRenderLayerEntityFront::Unload()
{
	CRenderLayerTile::Unload();
	if(m_VisualTilesDeath.has_value())
	{
		m_VisualTilesDeath->Unload();
		m_VisualTilesDeath = std::nullopt;
	}
	if(m_VisualTilesFreeze.has_value())
	{
		m_VisualTilesFreeze->Unload();
		m_VisualTilesFreeze = std::nullopt;
	}
	if(m_VisualTilesUnfreeze.has_value())
	{
		m_VisualTilesUnfreeze->Unload();
		m_VisualTilesUnfreeze = std::nullopt;
	}
	if(m_VisualTilesDeepFreeze.has_value())
	{
		m_VisualTilesDeepFreeze->Unload();
		m_VisualTilesDeepFreeze = std::nullopt;
	}
	if(m_VisualTilesDeepUnfreeze.has_value())
	{
		m_VisualTilesDeepUnfreeze->Unload();
		m_VisualTilesDeepUnfreeze = std::nullopt;
	}
}

void CRenderLayerEntityFront::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	RenderTileLayer(Color, Params);

	auto RenderFiltered = [&](std::optional<CTileLayerVisuals> &Visuals, float Alpha) {
		if(!Visuals.has_value() || Alpha <= 0.0f)
			return;
		RenderTileLayer(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), Params, &QmCheckedOptional(Visuals));
	};

	RenderFiltered(m_VisualTilesDeath, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeathAlpha));
	RenderFiltered(m_VisualTilesFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha));
	RenderFiltered(m_VisualTilesUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha));
	RenderFiltered(m_VisualTilesDeepFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha));
	RenderFiltered(m_VisualTilesDeepUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha));
}

void CRenderLayerEntityFront::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);

	auto RenderFiltered = [&](ColorRGBA GroupColor, CRenderMap::FTileRenderFilter Filter) {
		if(GroupColor.a <= 0.0f)
			return;
		Graphics()->BlendNone();
		RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_OPAQUE, Filter);
		Graphics()->BlendNormal();
		RenderMap()->RenderTilemap(m_pTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT, Filter);
	};

	RenderFiltered(Color, FilterBaseEntityAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeathAlpha)), FilterDeathAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha)), FilterFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha)), FilterUnfreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha)), FilterDeepFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha)), FilterDeepUnfreezeAlphaTile);
}

// TELE
CRenderLayerEntityTele::CRenderLayerEntityTele(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

int CRenderLayerEntityTele::GetDataIndex() const
{
	return m_pLayerTilemap->m_Tele;
}

CMapLoadTask CRenderLayerEntityTele::Init()
{
	if(!m_pTeleTiles)
		co_return;

	const std::optional<CClipRegion> LayerClip = m_LayerClip;
	auto IsCheckpointTile = [&](unsigned int x, unsigned int y) {
		return IsTeleCheckpointAlphaTile(m_pTeleTiles[y * m_pLayerTilemap->m_Width + x].m_Type);
	};

	co_await UploadTileData(m_VisualTiles, 0, false, false, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		return !IsCheckpointTile(x, y);
	});
	m_LayerClip = LayerClip;
	co_await UploadTileData(m_VisualCheckpointTiles, 0, false, false, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		return IsCheckpointTile(x, y);
	});
	m_LayerClip = LayerClip;
	co_await UploadTileData(m_VisualTeleNumbers, 1, false, false, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		return !IsCheckpointTile(x, y);
	});
	m_LayerClip = LayerClip;
	co_await UploadTileData(m_VisualCheckpointNumbers, 1, false, false, [&](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		return IsCheckpointTile(x, y);
	});
	m_LayerClip = LayerClip;
}

bool CRenderLayerEntityTele::InitTileData()
{
	m_pTeleTiles = GetData<CTeleTile>();
	return m_pTeleTiles != nullptr;
}

void CRenderLayerEntityTele::Unload()
{
	CRenderLayerTile::Unload();
	if(m_VisualTeleNumbers.has_value())
	{
		m_VisualTeleNumbers->Unload();
		m_VisualTeleNumbers = std::nullopt;
	}
	if(m_VisualCheckpointTiles.has_value())
	{
		m_VisualCheckpointTiles->Unload();
		m_VisualCheckpointTiles = std::nullopt;
	}
	if(m_VisualCheckpointNumbers.has_value())
	{
		m_VisualCheckpointNumbers->Unload();
		m_VisualCheckpointNumbers = std::nullopt;
	}
}

void CRenderLayerEntityTele::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	(void)Color;
	const float TeleAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayTeleAlpha);
	const ColorRGBA TeleColor(1.0f, 1.0f, 1.0f, TeleAlpha);
	const float CheckpointAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayTeleCheckpointAlpha);
	const ColorRGBA CheckpointColor(1.0f, 1.0f, 1.0f, CheckpointAlpha);

	if(TeleAlpha > 0.0f)
		RenderTileLayer(TeleColor, Params);
	if(CheckpointAlpha > 0.0f && m_VisualCheckpointTiles.has_value())
		RenderTileLayer(CheckpointColor, Params, &QmCheckedOptional(m_VisualCheckpointTiles));
	if(Params.m_RenderText && BindTileNumberTexture(*Graphics(), m_pMapImages->GetOverlayCenter()))
	{
		if(TeleAlpha > 0.0f && m_VisualTeleNumbers.has_value())
			RenderTileLayer(TeleColor, Params, &QmCheckedOptional(m_VisualTeleNumbers));
		if(CheckpointAlpha > 0.0f && m_VisualCheckpointNumbers.has_value())
			RenderTileLayer(CheckpointColor, Params, &QmCheckedOptional(m_VisualCheckpointNumbers));
	}
}

void CRenderLayerEntityTele::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	(void)Color;
	const float TeleAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayTeleAlpha);
	const ColorRGBA TeleColor(1.0f, 1.0f, 1.0f, TeleAlpha);
	const float CheckpointAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayTeleCheckpointAlpha);
	const ColorRGBA CheckpointColor(1.0f, 1.0f, 1.0f, CheckpointAlpha);
	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);

	auto RenderFiltered = [&](ColorRGBA GroupColor, CRenderMap::FTileRenderFilter Filter) {
		if(GroupColor.a <= 0.0f)
			return;
		RenderMap()->RenderTelemap(m_pTeleTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT, Filter);
	};

	RenderFiltered(TeleColor, FilterBaseTeleAlphaTile);
	RenderFiltered(CheckpointColor, FilterCheckpointTeleAlphaTile);

	int OverlayRenderFlags = (Params.m_RenderText ? OVERLAYRENDERFLAG_TEXT : 0) | (Params.m_RenderInvalidTiles ? OVERLAYRENDERFLAG_EDITOR : 0);
	if(TeleAlpha > 0.0f)
		RenderMap()->RenderTeleOverlay(m_pTeleTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, OverlayRenderFlags, TeleColor.a, FilterBaseTeleAlphaTile);
	if(CheckpointAlpha > 0.0f)
		RenderMap()->RenderTeleOverlay(m_pTeleTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, OverlayRenderFlags, CheckpointColor.a, FilterCheckpointTeleAlphaTile);
}

void CRenderLayerEntityTele::GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const
{
	*pIndex = m_pTeleTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
	*pFlags = 0;
	if(CurOverlay == 1)
	{
		if(IsTeleTileNumberUsedAny(*pIndex))
			*pIndex = m_pTeleTiles[y * m_pLayerTilemap->m_Width + x].m_Number;
		else
			*pIndex = 0;
	}
}

// SPEEDUP
CRenderLayerEntitySpeedup::CRenderLayerEntitySpeedup(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

IGraphics::CTextureHandle CRenderLayerEntitySpeedup::GetTexture() const
{
	return m_pMapImages->GetSpeedupArrow();
}

int CRenderLayerEntitySpeedup::GetDataIndex() const
{
	return m_pLayerTilemap->m_Speedup;
}

CMapLoadTask CRenderLayerEntitySpeedup::Init()
{
	co_await UploadTileData(m_VisualTiles, 0, true);
	co_await UploadTileData(m_VisualForce, 1, false);
	co_await UploadTileData(m_VisualMaxSpeed, 2, false);
}

bool CRenderLayerEntitySpeedup::InitTileData()
{
	m_pSpeedupTiles = GetData<CSpeedupTile>();
	return m_pSpeedupTiles != nullptr;
}

void CRenderLayerEntitySpeedup::Unload()
{
	CRenderLayerTile::Unload();
	if(m_VisualForce.has_value())
	{
		m_VisualForce->Unload();
		m_VisualForce = std::nullopt;
	}
	if(m_VisualMaxSpeed.has_value())
	{
		m_VisualMaxSpeed->Unload();
		m_VisualMaxSpeed = std::nullopt;
	}
}

void CRenderLayerEntitySpeedup::GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const
{
	*pIndex = m_pSpeedupTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
	unsigned char Force = m_pSpeedupTiles[y * m_pLayerTilemap->m_Width + x].m_Force;
	unsigned char MaxSpeed = m_pSpeedupTiles[y * m_pLayerTilemap->m_Width + x].m_MaxSpeed;
	*pFlags = 0;
	*pAngleRotate = m_pSpeedupTiles[y * m_pLayerTilemap->m_Width + x].m_Angle;
	if((Force == 0 && *pIndex == TILE_SPEED_BOOST_OLD) || (Force == 0 && MaxSpeed == 0 && *pIndex == TILE_SPEED_BOOST) || !IsValidSpeedupTile(*pIndex))
		*pIndex = 0;
	else if(CurOverlay == 1)
		*pIndex = Force;
	else if(CurOverlay == 2)
		*pIndex = MaxSpeed;
}

void CRenderLayerEntitySpeedup::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	// draw arrow -- clamp to the edge of the arrow image
	Graphics()->WrapClamp();
	UseTexture(GetTexture());
	RenderTileLayer(Color, Params);
	Graphics()->WrapNormal();

	if(Params.m_RenderText)
	{
		if(BindTileNumberTexture(*Graphics(), m_pMapImages->GetOverlayBottom()))
			RenderTileLayer(Color, Params, &QmCheckedOptional(m_VisualForce));
		if(BindTileNumberTexture(*Graphics(), m_pMapImages->GetOverlayTop()))
			RenderTileLayer(Color, Params, &QmCheckedOptional(m_VisualMaxSpeed));
	}
}

void CRenderLayerEntitySpeedup::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	int OverlayRenderFlags = (Params.m_RenderText ? OVERLAYRENDERFLAG_TEXT : 0) | (Params.m_RenderInvalidTiles ? OVERLAYRENDERFLAG_EDITOR : 0);
	RenderMap()->RenderSpeedupOverlay(m_pSpeedupTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, OverlayRenderFlags, Color.a);
}

// SWITCH
CRenderLayerEntitySwitch::CRenderLayerEntitySwitch(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

IGraphics::CTextureHandle CRenderLayerEntitySwitch::GetTexture() const
{
	return m_pMapImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH);
}

int CRenderLayerEntitySwitch::GetDataIndex() const
{
	return m_pLayerTilemap->m_Switch;
}

CMapLoadTask CRenderLayerEntitySwitch::Init()
{
	const std::optional<CClipRegion> LayerClip = m_LayerClip;
	CQmEntityOverlayBuildPlan Plan;
	auto BaseFilter = [this, &Plan](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		const unsigned char Type = m_pSwitchTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
		Plan.Record(Type, false);
		return !IsSpecialSwitchAlphaTile(Type);
	};

	auto NumberFilter = [this](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		const unsigned char Type = m_pSwitchTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
		return IsSwitchTileNumberUsed(Type);
	};

	auto DelayFilter = [this](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
		(void)Index;
		(void)Flags;
		(void)AngleRotate;
		(void)CurOverlay;
		const unsigned char Type = m_pSwitchTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
		return IsSwitchTileDelayUsed(Type);
	};

	co_await UploadTileData(m_VisualTiles, 0, false, false, BaseFilter);
	m_LayerClip = LayerClip;
	co_await UploadTileData(m_VisualSwitchNumberTop, 1, false, false, NumberFilter);
	m_LayerClip = LayerClip;
	co_await UploadTileData(m_VisualSwitchNumberBottom, 2, false, false, DelayFilter);
	m_LayerClip = LayerClip;
	for(auto Category = Plan.NextPresent(); Category != CQmEntityOverlayBuildPlan::NUM_CATEGORIES; Category = Plan.NextPresent(Category + 1))
	{
		std::optional<CTileLayerVisuals> *pVisuals = nullptr;
		switch(Category)
		{
		case CQmEntityOverlayBuildPlan::FREEZE: pVisuals = &m_VisualTilesFreeze; break;
		case CQmEntityOverlayBuildPlan::UNFREEZE: pVisuals = &m_VisualTilesUnfreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_FREEZE: pVisuals = &m_VisualTilesDeepFreeze; break;
		case CQmEntityOverlayBuildPlan::DEEP_UNFREEZE: pVisuals = &m_VisualTilesDeepUnfreeze; break;
		case CQmEntityOverlayBuildPlan::DEATH:
		case CQmEntityOverlayBuildPlan::NUM_CATEGORIES: continue;
		}
		co_await UploadTileData(*pVisuals, 0, false, false, [this, Category](unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay) {
			(void)Index;
			(void)Flags;
			(void)AngleRotate;
			(void)CurOverlay;
			const unsigned char Type = m_pSwitchTiles[y * m_pLayerTilemap->m_Width + x].m_Type;
			return CQmEntityOverlayBuildPlan::Classify(Type) == Category;
		});
		m_LayerClip = LayerClip;
	}
}

bool CRenderLayerEntitySwitch::InitTileData()
{
	m_pSwitchTiles = GetData<CSwitchTile>();
	return m_pSwitchTiles != nullptr;
}

void CRenderLayerEntitySwitch::Unload()
{
	CRenderLayerTile::Unload();
	if(m_VisualSwitchNumberTop.has_value())
	{
		m_VisualSwitchNumberTop->Unload();
		m_VisualSwitchNumberTop = std::nullopt;
	}
	if(m_VisualSwitchNumberBottom.has_value())
	{
		m_VisualSwitchNumberBottom->Unload();
		m_VisualSwitchNumberBottom = std::nullopt;
	}
	if(m_VisualTilesFreeze.has_value())
	{
		m_VisualTilesFreeze->Unload();
		m_VisualTilesFreeze = std::nullopt;
	}
	if(m_VisualTilesUnfreeze.has_value())
	{
		m_VisualTilesUnfreeze->Unload();
		m_VisualTilesUnfreeze = std::nullopt;
	}
	if(m_VisualTilesDeepFreeze.has_value())
	{
		m_VisualTilesDeepFreeze->Unload();
		m_VisualTilesDeepFreeze = std::nullopt;
	}
	if(m_VisualTilesDeepUnfreeze.has_value())
	{
		m_VisualTilesDeepUnfreeze->Unload();
		m_VisualTilesDeepUnfreeze = std::nullopt;
	}
}

void CRenderLayerEntitySwitch::GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const
{
	if(!m_pSwitchTiles)
	{
		*pIndex = 0;
		*pFlags = 0;
		return;
	}

	*pFlags = 0;
	const CSwitchTile &Tile = m_pSwitchTiles[y * m_pLayerTilemap->m_Width + x];
	*pIndex = Tile.m_Type;
	if(CurOverlay == 0)
	{
		*pFlags = Tile.m_Flags;
		if(*pIndex == TILE_SWITCHTIMEDOPEN)
			*pIndex = 8;
	}
	else if(CurOverlay == 1)
	{
		*pIndex = Tile.m_Number;
	}
	else if(CurOverlay == 2)
	{
		*pIndex = Tile.m_Delay;
	}
}

void CRenderLayerEntitySwitch::RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	(void)Color;
	if(!m_pSwitchTiles)
		return;

	const float SwitchAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlaySwitchAlpha);
	const ColorRGBA SwitchColor(1.0f, 1.0f, 1.0f, SwitchAlpha);
	if(SwitchAlpha > 0.0f)
		RenderTileLayer(SwitchColor, Params);

	auto RenderFiltered = [&](std::optional<CTileLayerVisuals> &Visuals, float Alpha) {
		if(!Visuals.has_value() || Alpha <= 0.0f)
			return;
		RenderTileLayer(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), Params, &QmCheckedOptional(Visuals));
	};

	RenderFiltered(m_VisualTilesFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha));
	RenderFiltered(m_VisualTilesUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha));
	RenderFiltered(m_VisualTilesDeepFreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha));
	RenderFiltered(m_VisualTilesDeepUnfreeze, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha));

	if(SwitchAlpha > 0.0f && Params.m_RenderText && m_pMapImages && m_VisualSwitchNumberTop.has_value() && m_VisualSwitchNumberBottom.has_value())
	{
		if(BindTileNumberTexture(*Graphics(), m_pMapImages->GetOverlayTop()))
			RenderTileLayer(SwitchColor, Params, &QmCheckedOptional(m_VisualSwitchNumberTop));
		if(BindTileNumberTexture(*Graphics(), m_pMapImages->GetOverlayBottom()))
			RenderTileLayer(SwitchColor, Params, &QmCheckedOptional(m_VisualSwitchNumberBottom));
	}
}

void CRenderLayerEntitySwitch::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	(void)Color;
	if(!m_pSwitchTiles)
		return;

	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);

	auto RenderFiltered = [&](ColorRGBA GroupColor, CRenderMap::FTileRenderFilter Filter) {
		if(GroupColor.a <= 0.0f)
			return;
		RenderMap()->RenderSwitchmap(m_pSwitchTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, GroupColor, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT, Filter);
	};

	const float SwitchAlpha = QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlaySwitchAlpha);
	const ColorRGBA SwitchColor(1.0f, 1.0f, 1.0f, SwitchAlpha);
	if(SwitchAlpha > 0.0f)
		RenderFiltered(SwitchColor, FilterBaseSwitchAlphaTile);

	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayFreezeAlpha)), FilterFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayUnfreezeAlpha)), FilterUnfreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepFreezeAlpha)), FilterDeepFreezeAlphaTile);
	RenderFiltered(ColorRGBA(1.0f, 1.0f, 1.0f, QmComposeEntityOverlayAlpha(Params.m_EntityOverlayVal, g_Config.m_QmEntityOverlayDeepUnfreezeAlpha)), FilterDeepUnfreezeAlphaTile);

	if(SwitchAlpha > 0.0f && Params.m_RenderText)
	{
		int OverlayRenderFlags = (Params.m_RenderText ? OVERLAYRENDERFLAG_TEXT : 0) | (Params.m_RenderInvalidTiles ? OVERLAYRENDERFLAG_EDITOR : 0);
		RenderMap()->RenderSwitchOverlay(m_pSwitchTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, OverlayRenderFlags, SwitchColor.a);
	}
}

// TUNE
CRenderLayerEntityTune::CRenderLayerEntityTune(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap) :
	CRenderLayerEntityBase(GroupId, LayerId, Flags, pLayerTilemap) {}

IGraphics::CTextureHandle CRenderLayerEntityTune::GetTexture() const
{
	// 关闭着色时改用普通实体贴图：编号颜色图集的每个切片都带色相，
	// 用原始 TILE_TUNE 序号采样会得到固定杂色而不是灰色 tune tile。
	if(!g_Config.m_QmShowTuneZoneColors)
		return m_pMapImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
	return m_pMapImages->GetTuneColors();
}

void CRenderLayerEntityTune::GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const
{
	const CTuneTile &Tile = m_pTuneTiles[y * m_pLayerTilemap->m_Width + x];
	// Assign color indices in encounter order for higher adjacent color distance.
	// 开关关闭时保留原始 tune tile，不按编号着色。
	*pIndex = g_Config.m_QmShowTuneZoneColors ? m_TuneColorMapper.TileTextureIndex(Tile.m_Type, Tile.m_Number, Graphics()->HasTextureArraysSupport()) : Tile.m_Type;
	*pFlags = 0;
}

CMapLoadTask CRenderLayerEntityTune::Init()
{
	m_TuneColorMapper.Reset();
	co_await CRenderLayerTile::Init();
}

int CRenderLayerEntityTune::GetDataIndex() const
{
	return m_pLayerTilemap->m_Tune;
}

bool CRenderLayerEntityTune::InitTileData()
{
	m_pTuneTiles = GetData<CTuneTile>();
	return m_pTuneTiles != nullptr;
}

void CRenderLayerEntityTune::RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params)
{
	// 开关关闭时传空 mapper，RenderTunemap 会退回原始单色 tile。
	CTuneColorMapper *pTuneColorMapper = g_Config.m_QmShowTuneZoneColors ? &m_TuneColorMapper : nullptr;
	const int TileRenderFlags = ImmediateTileRenderFlags(Params, m_TileAndQuadBuffering);
	RenderMap()->RenderTunemap(m_pTuneTiles, m_pLayerTilemap->m_Width, m_pLayerTilemap->m_Height, 32.0f, Color, TileRenderFlags | LAYERRENDERFLAG_TRANSPARENT, pTuneColorMapper);
}
