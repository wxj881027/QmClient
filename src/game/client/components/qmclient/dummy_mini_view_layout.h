#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINI_VIEW_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINI_VIEW_LAYOUT_H

#include <engine/graphics.h>

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

	// 分身小窗边框设计尺寸，单位为 HUD 单位（300 单位等于屏幕高度）。
	// 旧边框从外缘到画面共 3.35 单位（1080p 下约 12 像素）显得很厚；
	// 这里整体收到约一半：深色包边 + 强调色细线 + 画布色内框。
	struct SFrameStyle
	{
		float m_BezelWidth = 1.75f;
		float m_RimWidth = 0.5f;
		float m_AccentWidth = 0.35f;
	};

	// 各层收窄后的矩形与圆角。画布（离屏画面）回贴遮罩与边框环带内缘
	// 必须共用 m_Content / m_ContentRadius，否则画面与边框之间会露缝或压边。
	struct SFrameGeometry
	{
		CUIRect m_Outer{};
		float m_Radius = 0.0f;
		CUIRect m_Content{};
		float m_ContentRadius = 0.0f;
		CUIRect m_Accent{};
		float m_AccentRadius = 0.0f;
		CUIRect m_Body{};
		float m_BodyRadius = 0.0f;
		float m_BezelWidth = 0.0f;
		float m_RimWidth = 0.0f;
		float m_AccentWidth = 0.0f;
		bool m_HasContent = false;
	};

	// 圆角 SDF 的角半径顺序：左上、右上、右下、左下（与 rounded_rect_sdf 片元一致）。
	inline vec4 ResolveFrameCornerRadii(float Radius, int Corners)
	{
		return vec4(
			(Corners & IGraphics::CORNER_TL) ? Radius : 0.0f,
			(Corners & IGraphics::CORNER_TR) ? Radius : 0.0f,
			(Corners & IGraphics::CORNER_BR) ? Radius : 0.0f,
			(Corners & IGraphics::CORNER_BL) ? Radius : 0.0f);
	}

	// 边框不能吃掉整个小窗：各层宽度都夹在外矩形内，窗口过小时先退化强调线再退化内容。
	inline SFrameGeometry ResolveFrame(const CUIRect &Outer, float RequestedRadius, const SFrameStyle &Style)
	{
		SFrameGeometry Frame;
		Frame.m_Outer = Outer;
		if(Outer.w <= 0.0f || Outer.h <= 0.0f)
			return Frame;

		const float HalfMinExtent = std::min(Outer.w, Outer.h) * 0.5f;
		Frame.m_Radius = std::clamp(RequestedRadius, 0.0f, HalfMinExtent);
		Frame.m_BezelWidth = std::clamp(Style.m_BezelWidth, 0.0f, HalfMinExtent);
		Frame.m_RimWidth = std::clamp(Style.m_RimWidth, 0.0f, Frame.m_BezelWidth);
		Frame.m_AccentWidth = std::clamp(Style.m_AccentWidth, 0.0f, std::max(0.0f, Frame.m_BezelWidth - Frame.m_RimWidth));

		Frame.m_Content = CUIRect{Outer.x + Frame.m_BezelWidth, Outer.y + Frame.m_BezelWidth,
			Outer.w - Frame.m_BezelWidth * 2.0f, Outer.h - Frame.m_BezelWidth * 2.0f};
		Frame.m_ContentRadius = std::max(0.0f, Frame.m_Radius - Frame.m_BezelWidth);
		Frame.m_HasContent = Frame.m_Content.w > 0.0f && Frame.m_Content.h > 0.0f;

		Frame.m_Accent = CUIRect{Outer.x + Frame.m_RimWidth, Outer.y + Frame.m_RimWidth,
			Outer.w - Frame.m_RimWidth * 2.0f, Outer.h - Frame.m_RimWidth * 2.0f};
		Frame.m_AccentRadius = std::max(0.0f, Frame.m_Radius - Frame.m_RimWidth);

		const float BodyInset = Frame.m_RimWidth + Frame.m_AccentWidth;
		Frame.m_Body = CUIRect{Outer.x + BodyInset, Outer.y + BodyInset,
			Outer.w - BodyInset * 2.0f, Outer.h - BodyInset * 2.0f};
		Frame.m_BodyRadius = std::max(0.0f, Frame.m_Radius - BodyInset);
		return Frame;
	}

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
