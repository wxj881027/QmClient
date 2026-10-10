#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINI_VIEW_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINI_VIEW_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace QmDummyMiniViewLayout
{
	enum class EBackend
	{
		UNKNOWN,
		OPENGL,
		VULKAN,
	};

	// 名称来自 GetDetectedContextVersion 的实际后端，不能使用配置请求值代替。
	inline EBackend ResolveBackend(const char *pDetectedName)
	{
		if(pDetectedName == nullptr)
			return EBackend::UNKNOWN;
		if(std::strcmp(pDetectedName, "OpenGL") == 0 || std::strcmp(pDetectedName, "GLES") == 0)
			return EBackend::OPENGL;
		if(std::strcmp(pDetectedName, "Vulkan") == 0)
			return EBackend::VULKAN;
		return EBackend::UNKNOWN;
	}

	struct SViewport
	{
		int m_X = 0;
		int m_Y = 0;
		int m_W = 0;
		int m_H = 0;

		bool IsVisible() const { return m_W > 0 && m_H > 0; }

		// OpenGL 后端叠加视口原点，需要在 ClipEnable 翻转后得到零偏移的局部裁剪。
		// Vulkan 使用 ClipDisable：后端以完整 presented extent 为默认 scissor，
		// 再映射到 dynamic viewport，避免用前端画布尺寸猜测 presented extent。
		std::optional<SViewport> ResolveClip(EBackend Backend, int CanvasWidth, int CanvasHeight) const
		{
			if(!IsVisible() || CanvasWidth <= 0 || CanvasHeight <= 0 || Backend != EBackend::OPENGL)
				return std::nullopt;
			return SViewport{0, CanvasHeight - m_H, m_W, m_H};
		}
	};

	// UpdateViewport 使用左上原点的屏幕坐标；当前视口的裁剪另外由 ResolveClip 按实际后端转换。
	// 在同一映射中计算内框的像素交集，避免调用端再次翻转 Y 或使用未缩放的 HUD 尺寸。
	inline SViewport ResolveViewport(const CUIRect &ContentRect, const CUIRect &MappedScreen, int DrawableWidth, int DrawableHeight)
	{
		if(ContentRect.w <= 0.0f || ContentRect.h <= 0.0f || MappedScreen.w <= 0.0f || MappedScreen.h <= 0.0f || DrawableWidth <= 0 || DrawableHeight <= 0)
			return {};

		const float XScale = DrawableWidth / MappedScreen.w;
		const float YScale = DrawableHeight / MappedScreen.h;
		const int Left = std::clamp((int)std::round((ContentRect.x - MappedScreen.x) * XScale), 0, DrawableWidth);
		const int Top = std::clamp((int)std::round((ContentRect.y - MappedScreen.y) * YScale), 0, DrawableHeight);
		const int Right = std::clamp((int)std::round((ContentRect.x + ContentRect.w - MappedScreen.x) * XScale), 0, DrawableWidth);
		const int Bottom = std::clamp((int)std::round((ContentRect.y + ContentRect.h - MappedScreen.y) * YScale), 0, DrawableHeight);
		return {Left, Top, Right - Left, Bottom - Top};
	}
} // namespace QmDummyMiniViewLayout

#endif
