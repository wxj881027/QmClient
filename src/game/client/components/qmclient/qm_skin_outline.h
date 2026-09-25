#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SKIN_OUTLINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SKIN_OUTLINE_H

#include <base/vmath.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

inline bool QmShouldDrawSkinOutline(int ClientId, int MainId, int DummyId, bool LocalEnabled, bool OthersEnabled)
{
	return ClientId >= 0 && ((ClientId == MainId || ClientId == DummyId) ? LocalEnabled : OthersEnabled);
}

// 原始皮肤与渲染快照共享遮罩；纹理由皮肤卸载入口释放，调色不重新生成纹理。
class CQmSkinOutline
{
	CImageInfo m_Mask;
	vec2 m_RenderSize;
	IGraphics::CTextureHandle m_Texture;
	vec2 m_QuadScale = vec2(1, 1);
	int m_CachedWidth = 0;

public:
	CQmSkinOutline(const CImageInfo &Source, ivec2 FillPos, ivec2 OutlinePos, ivec2 Size, vec2 RenderSize) :
		m_RenderSize(RenderSize)
	{
		if(Source.m_Format != CImageInfo::FORMAT_RGBA || Source.m_pData == nullptr || Size.x <= 0 || Size.y <= 0 ||
			FillPos.x < 0 || FillPos.y < 0 || OutlinePos.x < 0 || OutlinePos.y < 0 ||
			(size_t)std::max(FillPos.x, OutlinePos.x) + (size_t)Size.x > Source.m_Width ||
			(size_t)std::max(FillPos.y, OutlinePos.y) + (size_t)Size.y > Source.m_Height || RenderSize.x <= 0 || RenderSize.y <= 0)
			return;

		m_Mask.m_Width = Size.x;
		m_Mask.m_Height = Size.y;
		m_Mask.m_Format = CImageInfo::FORMAT_RGBA;
		m_Mask.m_pData = static_cast<uint8_t *>(malloc(m_Mask.DataSize()));
		for(int y = 0; y < Size.y; ++y)
		{
			for(int x = 0; x < Size.x; ++x)
			{
				const size_t Dst = (y * Size.x + x) * 4;
				const size_t Fill = ((FillPos.y + y) * Source.m_Width + FillPos.x + x) * 4;
				const size_t Outline = ((OutlinePos.y + y) * Source.m_Width + OutlinePos.x + x) * 4;
				m_Mask.m_pData[Dst] = m_Mask.m_pData[Dst + 1] = m_Mask.m_pData[Dst + 2] = 255;
				m_Mask.m_pData[Dst + 3] = std::max(Source.m_pData[Fill + 3], Source.m_pData[Outline + 3]);
			}
		}
		// 小素材也要能表达一单位细边；常规 96 像素皮肤保留原采样，大素材限制到 128 像素。
		const float Scale = std::min(1.0f, 128.0f / std::max(Size.x, Size.y));
		const int MaskWidth = std::clamp((int)std::round(Size.x * Scale), std::clamp((int)std::ceil(RenderSize.x), 1, 128), 128);
		const int MaskHeight = std::clamp((int)std::round(Size.y * Scale), std::clamp((int)std::ceil(RenderSize.y), 1, 128), 128);
		if(MaskWidth != Size.x || MaskHeight != Size.y)
			ResizeImage(m_Mask, MaskWidth, MaskHeight);
	}

	~CQmSkinOutline() { m_Mask.Free(); }
	CQmSkinOutline(const CQmSkinOutline &) = delete;
	CQmSkinOutline &operator=(const CQmSkinOutline &) = delete;

	CImageInfo BuildImage(int Width) const
	{
		CImageInfo Image;
		if(Width <= 0 || m_Mask.m_pData == nullptr)
			return Image;
		Width = std::min(Width, 6);
		const float StepX = m_RenderSize.x / m_Mask.m_Width;
		const float StepY = m_RenderSize.y / m_Mask.m_Height;
		const int RadiusX = (int)std::ceil(Width / StepX);
		const int RadiusY = (int)std::ceil(Width / StepY);
		const int PadX = RadiusX + 1;
		const int PadY = RadiusY + 1;
		Image.m_Width = m_Mask.m_Width + 2 * PadX;
		Image.m_Height = m_Mask.m_Height + 2 * PadY;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(Image.DataSize()));
		for(size_t Offset = 0; Offset < Image.DataSize(); Offset += 4)
		{
			Image.m_pData[Offset] = Image.m_pData[Offset + 1] = Image.m_pData[Offset + 2] = 255;
			Image.m_pData[Offset + 3] = 0;
		}
		// 每个圆盘截面用滑动最大值展开，避免高分辨率脚部逐像素扫描整个圆盘。
		std::vector<int> vQueue(m_Mask.m_Width);
		for(int dy = 0; dy <= RadiusY; ++dy)
		{
			const float Remaining = Width * Width - dy * dy * StepY * StepY;
			if(Remaining < 0.0f)
				continue;
			const int SpanX = (int)std::floor(std::sqrt(Remaining) / StepX);
			for(int y = 0; y < (int)m_Mask.m_Height; ++y)
			{
				int Head = 0, Tail = 0, Next = 0;
				const auto RowAlpha = [this, y](int x) { return m_Mask.m_pData[(y * m_Mask.m_Width + x) * 4 + 3]; };
				for(int x = 0; x < (int)Image.m_Width; ++x)
				{
					const int Right = std::min((int)m_Mask.m_Width - 1, x - PadX + SpanX);
					while(Next <= Right)
					{
						while(Tail > Head && RowAlpha(vQueue[Tail - 1]) <= RowAlpha(Next))
							--Tail;
						vQueue[Tail++] = Next++;
					}
					while(Head < Tail && vQueue[Head] < x - PadX - SpanX)
						++Head;
					if(Head == Tail)
						continue;
					const uint8_t Value = RowAlpha(vQueue[Head]);
					for(int TargetY : {y + PadY - dy, y + PadY + dy})
					{
						uint8_t &Alpha = Image.m_pData[(TargetY * Image.m_Width + x) * 4 + 3];
						Alpha = std::max(Alpha, Value);
					}
				}
			}
		}
		const auto AlphaAt = [this](int x, int y) -> uint8_t {
			return x >= 0 && y >= 0 && x < (int)m_Mask.m_Width && y < (int)m_Mask.m_Height ? m_Mask.m_pData[(y * m_Mask.m_Width + x) * 4 + 3] : 0;
		};
		for(int y = 0; y < (int)Image.m_Height; ++y)
		{
			for(int x = 0; x < (int)Image.m_Width; ++x)
			{
				const uint8_t Original = AlphaAt(x - PadX, y - PadY);
				const size_t Offset = (y * Image.m_Width + x) * 4;
				// 只留下外侧环带，半透明角色内部不会叠上一层纯色剪影。
				Image.m_pData[Offset + 3] -= Original;
			}
		}
		return Image;
	}

	void Unload(IGraphics *pGraphics)
	{
		pGraphics->UnloadTexture(&m_Texture);
		m_Mask.Free();
		m_CachedWidth = 0;
	}

	void Render(IGraphics *pGraphics, vec2 Position, vec2 Size, float Angle, ColorRGBA Color, int Width, bool Flip = false)
	{
		if(Width <= 0 || Color.a <= 0.0f || m_Mask.m_pData == nullptr)
			return;
		if(m_CachedWidth != Width || !m_Texture.IsValid() || !pGraphics->IsTextureHandleAllocated(m_Texture))
		{
			CImageInfo Image = BuildImage(Width);
			m_QuadScale = vec2((float)Image.m_Width / m_Mask.m_Width, (float)Image.m_Height / m_Mask.m_Height);
			pGraphics->UnloadTexture(&m_Texture);
			m_Texture = pGraphics->LoadTextureRawMove(Image, 0, "qm_skin_outline");
			m_CachedWidth = Width;
		}
		// 句柄失效（设备重建、槽位复用）时画出来是实心块，当作不可绘制处理。
		if(!m_Texture.IsValid() || m_Texture.IsNullTexture() || !pGraphics->IsTextureHandleAllocated(m_Texture))
			return;
		pGraphics->TextureSet(m_Texture);
		pGraphics->QuadsBegin();
		pGraphics->QuadsSetSubset(Flip ? 1.0f : 0.0f, 0, Flip ? 0.0f : 1.0f, 1);
		pGraphics->QuadsSetRotation(Angle);
		pGraphics->SetColor(Color);
		IGraphics::CQuadItem Quad(Position.x, Position.y, Size.x * m_QuadScale.x, Size.y * m_QuadScale.y);
		pGraphics->QuadsDraw(&Quad, 1);
		pGraphics->QuadsEnd();
		pGraphics->QuadsSetSubset(0, 0, 1, 1);
	}
};

template<typename TSprite>
inline std::shared_ptr<CQmSkinOutline> QmCreateSkinOutline(const CImageInfo &Image, const TSprite &Fill, const TSprite &Outline, vec2 RenderSize)
{
	const int UnitX = Image.m_Width / Fill.m_pSet->m_Gridx;
	const int UnitY = Image.m_Height / Fill.m_pSet->m_Gridy;
	return std::make_shared<CQmSkinOutline>(Image, ivec2(Fill.m_X * UnitX, Fill.m_Y * UnitY), ivec2(Outline.m_X * UnitX, Outline.m_Y * UnitY), ivec2(Fill.m_W * UnitX, Fill.m_H * UnitY), RenderSize);
}

#endif
