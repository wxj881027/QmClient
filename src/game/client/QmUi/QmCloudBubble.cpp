#include "QmCloudBubble.h"

#include <engine/image.h>

namespace qm_chat_bubble
{
	void CCloudRenderer::Init(IGraphics *pGraphics)
	{
		if(pGraphics->IsTextureHandleAllocated(m_Texture))
			return;
		std::vector<uint8_t> vPixels(CLOUD_ATLAS_SIZE * CLOUD_ATLAS_SIZE * 4);
		for(int Variant = 0; Variant < 4; ++Variant)
			for(int y = 0; y < CLOUD_TILE_SIZE; ++y)
				for(int x = 0; x < CLOUD_TILE_SIZE; ++x)
				{
					const vec2 Point((x + 0.5f) * 2.4f / CLOUD_TILE_SIZE - 1.2f, (y + 0.5f) * 2.4f / CLOUD_TILE_SIZE - 1.2f);
					const ColorRGBA Color = CloudTexel(Point, Variant);
					const size_t Offset = ((Variant / 2 * CLOUD_TILE_SIZE + y) * CLOUD_ATLAS_SIZE + Variant % 2 * CLOUD_TILE_SIZE + x) * 4;
					vPixels[Offset] = round_to_int(Color.r * 255.0f);
					vPixels[Offset + 1] = round_to_int(Color.g * 255.0f);
					vPixels[Offset + 2] = round_to_int(Color.b * 255.0f);
					vPixels[Offset + 3] = round_to_int(Color.a * 255.0f);
				}
		CImageInfo Image;
		Image.m_Width = Image.m_Height = CLOUD_ATLAS_SIZE;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = vPixels.data();
		// 256 KiB 的程序化云团共享给所有气泡，正文改变和每帧动画都不重新生成。
		m_Texture = pGraphics->LoadTextureRaw(Image, 0, "qm-chat-cloud-procedural");
	}

	void CCloudRenderer::Shutdown(IGraphics *pGraphics)
	{
		if(m_Texture.IsValid())
			pGraphics->UnloadTexture(&m_Texture);
	}

	void CCloudRenderer::Render(IGraphics *pGraphics, const SLayout &Layout, const SPalette &Colors, float Assembly)
	{
		Assembly = std::clamp(Assembly, 0.0f, 1.0f);
		if(Assembly <= 0.0f || Colors.m_Fill.a <= 0.0f)
			return;
		if(!pGraphics->IsTextureHandleAllocated(m_Texture))
		{
			// 资源创建失败时仍保留可读背景，不在逐帧路径反复尝试分配。
			pGraphics->DrawRect(Layout.m_Position.x, Layout.m_Position.y, Layout.m_Size.x, Layout.m_Size.y,
				Colors.m_Fill.WithMultipliedAlpha(Assembly), IGraphics::CORNER_ALL, Layout.m_Radius);
			return;
		}
		const auto aPuffs = CloudPuffs(Layout, Assembly);
		pGraphics->TextureSet(m_Texture);
		pGraphics->QuadsBegin();
		const float Opacity = Assembly * Assembly * (3.0f - 2.0f * Assembly);
		const ColorRGBA Top = Colors.m_Fill.WithAlpha(CloudPuffOpacity(Colors.m_Fill.a * Opacity));
		const ColorRGBA Bottom = Colors.m_FillBottom.WithAlpha(CloudPuffOpacity(Colors.m_FillBottom.a * Opacity));
		pGraphics->SetColor4(Top, Top, Bottom, Bottom);
		for(const SCloudPuff &Puff : aPuffs)
		{
			const float U = (Puff.m_Variant % 2) * 0.5f;
			const float V = (Puff.m_Variant / 2) * 0.5f;
			pGraphics->QuadsSetSubset(U, V, U + 0.5f, V + 0.5f);
			const IGraphics::CQuadItem Quad(Puff.m_Center.x - Puff.m_Size.x * 0.5f, Puff.m_Center.y - Puff.m_Size.y * 0.5f, Puff.m_Size.x, Puff.m_Size.y);
			pGraphics->QuadsDrawTL(&Quad, 1);
		}
		pGraphics->QuadsEnd();
		pGraphics->QuadsSetSubset(0, 0, 1, 1);
		pGraphics->TextureClear();
	}
}
