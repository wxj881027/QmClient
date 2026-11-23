#ifndef ENGINE_CLIENT_QM_GLYPH_RASTERIZER_H
#define ENGINE_CLIENT_QM_GLYPH_RASTERIZER_H

#include "glyph_outline.h"
#include "glyph_render_retry.h"
#include "qm_glyph_geometry.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

inline constexpr int QM_MAX_GLYPH_ATLAS_DIMENSION = 16 * 1024;

struct SQmGlyphBitmap
{
	int m_Width = 0;
	int m_Height = 0;
	int m_CharWidth = 0;
	int m_CharHeight = 0;
	int m_OffsetX = 0;
	int m_OffsetY = 0;
	float m_AdvanceX = 0.0f;
	float m_LayoutWidth = 0.0f;
	float m_LayoutBearingX = 0.0f;
	std::vector<uint8_t> m_vFill;
	std::vector<uint8_t> m_vOutline;
};

struct SQmGlyphWorkerTask
{
	std::tuple<FT_Face, int, int, bool> m_Key;
	FT_UInt m_GlyphIndex = 0;
	const FT_Byte *m_pFontData = nullptr;
	FT_Long m_FontDataSize = 0;
	FT_Long m_FaceIndex = 0;
	int m_FontSize = 0;
	std::vector<FT_Fixed> m_vVarCoords;
	uint64_t m_Generation = 0;
	bool m_GpuGeometry = false;
};

// 每个 worker 独占 FT_Library/FT_Face。字体文件字节由主线程 collection
// 持有，生命周期长于 job pool；图集写入和 GPU 上传仍回主线程。
class CQmGlyphRasterizer
{
	FT_Library m_Library = nullptr;
	FT_Face m_pFace = nullptr;
	const FT_Byte *m_pFaceData = nullptr;
	FT_Long m_FaceDataSize = 0;
	FT_Long m_FaceIndex = -1;
	std::vector<FT_Fixed> m_vVarCoords;
	int m_PixelSize = 0;

public:
	~CQmGlyphRasterizer()
	{
		if(m_pFace != nullptr)
			FT_Done_Face(m_pFace);
		if(m_Library != nullptr)
			FT_Done_FreeType(m_Library);
	}

	bool PrepareFace(const SQmGlyphWorkerTask &Task)
	{
		if(m_Library == nullptr && FT_Init_FreeType(&m_Library) != 0)
			return false;
		if(m_pFace == nullptr || m_pFaceData != Task.m_pFontData || m_FaceDataSize != Task.m_FontDataSize || m_FaceIndex != Task.m_FaceIndex)
		{
			if(m_pFace != nullptr)
			{
				FT_Done_Face(m_pFace);
				m_pFace = nullptr;
			}
			if(FT_New_Memory_Face(m_Library, Task.m_pFontData, Task.m_FontDataSize, Task.m_FaceIndex, &m_pFace) != 0)
				return false;
			m_pFaceData = Task.m_pFontData;
			m_FaceDataSize = Task.m_FontDataSize;
			m_FaceIndex = Task.m_FaceIndex;
			m_vVarCoords.clear();
			m_PixelSize = 0;
		}
		if(m_vVarCoords != Task.m_vVarCoords)
		{
			// 空坐标表示回到字体默认轴值；不能只跳过调用，否则从自定义字重
			// 切回默认字重时会沿用上一次的 wght 坐标。
			if(FT_Set_Var_Design_Coordinates(m_pFace, static_cast<FT_UInt>(Task.m_vVarCoords.size()),
				   Task.m_vVarCoords.empty() ? nullptr : const_cast<FT_Fixed *>(Task.m_vVarCoords.data())) != 0)
				return false;
			m_vVarCoords = Task.m_vVarCoords;
			m_PixelSize = 0;
		}
		if(m_PixelSize != Task.m_FontSize)
		{
			if(FT_Set_Pixel_Sizes(m_pFace, 0, Task.m_FontSize) != 0)
				return false;
			m_PixelSize = Task.m_FontSize;
		}
		return true;
	}

	EQmGlyphRenderResult RasterizeGeometry(const SQmGlyphWorkerTask &Task, SQmGlyphGeometry &Geometry)
	{
		if(std::get<3>(Task.m_Key) || !PrepareFace(Task))
			return EQmGlyphRenderResult::RETRY;
		if(FT_Load_Glyph(m_pFace, Task.m_GlyphIndex, FT_LOAD_NO_BITMAP) != 0)
			return EQmGlyphRenderResult::RETRY;
		if(!QmBuildGlyphGeometry(m_pFace->glyph->outline, m_pFace->glyph->metrics, Task.m_FontSize, false,
			   (m_pFace->glyph->outline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0, Geometry))
			return EQmGlyphRenderResult::UNSUPPORTED;
		Geometry.m_OffsetX = static_cast<int>(m_pFace->glyph->metrics.horiBearingX >> 6);
		Geometry.m_OffsetY = -static_cast<int>((m_pFace->glyph->metrics.height >> 6) - (m_pFace->glyph->metrics.horiBearingY >> 6));
		Geometry.m_AdvanceX = static_cast<float>(m_pFace->glyph->advance.x >> 6);
		return EQmGlyphRenderResult::SUCCESS;
	}

	EQmGlyphRenderResult Rasterize(const SQmGlyphWorkerTask &Task, SQmGlyphBitmap &Bitmap)
	{
		if(!PrepareFace(Task))
			return EQmGlyphRenderResult::RETRY;
		if(FT_Load_Glyph(m_pFace, Task.m_GlyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | (std::get<3>(Task.m_Key) ? FT_LOAD_NO_HINTING : 0)) != 0)
			return EQmGlyphRenderResult::RETRY;

		const FT_Bitmap *pBitmap = &m_pFace->glyph->bitmap;
		if(pBitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
			return EQmGlyphRenderResult::UNSUPPORTED;

		const unsigned RealWidth = pBitmap->width;
		const unsigned RealHeight = pBitmap->rows;
		float OutlineRadius = 0.0f;
		int OutlineThickness = 0;
		int X = 0;
		int Y = 0;
		if(RealWidth > 0)
		{
			OutlineRadius = std::get<3>(Task.m_Key) ? QmNameplateGlyphOutlineRadius(Task.m_FontSize) : static_cast<float>(QmAdjustOutlineThicknessToFontSize(1, Task.m_FontSize));
			OutlineThickness = static_cast<int>(std::ceil(OutlineRadius));
			X += (OutlineThickness + 1);
			Y += (OutlineThickness + 1);
		}
		if(RealWidth > QM_MAX_GLYPH_ATLAS_DIMENSION - X * 2 || RealHeight > QM_MAX_GLYPH_ATLAS_DIMENSION - Y * 2)
			return EQmGlyphRenderResult::UNSUPPORTED;

		const unsigned Width = RealWidth + X * 2;
		const unsigned Height = RealHeight + Y * 2;
		Bitmap.m_Width = static_cast<int>(Width);
		Bitmap.m_Height = static_cast<int>(Height);
		Bitmap.m_CharWidth = static_cast<int>(RealWidth);
		Bitmap.m_CharHeight = static_cast<int>(RealHeight);
		Bitmap.m_OffsetX = std::get<3>(Task.m_Key) ? m_pFace->glyph->bitmap_left - X : static_cast<int>(m_pFace->glyph->metrics.horiBearingX >> 6);
		Bitmap.m_OffsetY = std::get<3>(Task.m_Key) ? m_pFace->glyph->bitmap_top - static_cast<int>(RealHeight) - Y : -static_cast<int>((m_pFace->glyph->metrics.height >> 6) - (m_pFace->glyph->metrics.horiBearingY >> 6));
		Bitmap.m_AdvanceX = std::get<3>(Task.m_Key) ? m_pFace->glyph->advance.x / 64.0f : static_cast<float>(m_pFace->glyph->advance.x >> 6);
		Bitmap.m_LayoutWidth = m_pFace->glyph->metrics.width / 64.0f;
		Bitmap.m_LayoutBearingX = m_pFace->glyph->metrics.horiBearingX / 64.0f;
		if(Width > 0 && Height > 0)
		{
			const size_t GlyphDataSize = static_cast<size_t>(Width) * Height;
			Bitmap.m_vFill.assign(GlyphDataSize, 0);
			Bitmap.m_vOutline.assign(GlyphDataSize, 0);
			for(unsigned py = 0; py < pBitmap->rows; ++py)
				mem_copy(Bitmap.m_vFill.data() + (py + Y) * Width + X, &pBitmap->buffer[py * pBitmap->width], pBitmap->width);
			if(std::get<3>(Task.m_Key))
				QmGrowGlyphOutlineContinuous(Bitmap.m_vFill.data(), Bitmap.m_vOutline.data(), static_cast<int>(Width), static_cast<int>(Height), OutlineRadius);
			else
				QmGrowGlyphOutline(Bitmap.m_vFill.data(), Bitmap.m_vOutline.data(), static_cast<int>(Width), static_cast<int>(Height), OutlineThickness);
		}
		return EQmGlyphRenderResult::SUCCESS;
	}
};

#endif
