// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "ui_effects.h"

#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/qmclient/media_paths.h>
#include <game/client/components/qmclient/screenshot_viewer.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

constexpr float PI = 3.14159265358979323846f;
constexpr float SMOOTH_VALUE_EPSILON = 0.01f; // Threshold for smooth value convergence
constexpr float SMOOTH_VALUE_STEP_MULTIPLIER = 10.0f; // Controls interpolation speed
// 拍立得式的残影：闪一下 → 缩到屏幕中间 → 停一会儿 → 淡出。
// 不是缩到角落当缩略图，而是像刚拍完的照片留在画面上。
constexpr float SCREENSHOT_FLASH_DURATION = 0.16f;
constexpr float SCREENSHOT_SHRINK_START = 0.14f;
constexpr float SCREENSHOT_SHRINK_END = 0.78f;
// 拍完就停在那里，像二级菜单一样等人点掉；只有主动关闭时才快速淡出。
constexpr float SCREENSHOT_DISMISS_DURATION = 0.22f;
constexpr float SCREENSHOT_CARD_SCREEN_FRACTION = 0.62f;
constexpr float SCREENSHOT_CARD_MAX_HEIGHT_FRACTION = 0.66f;
// 卡片内的滚轮缩放范围：1x 贴合卡片，最多放大 8 倍。
constexpr float SCREENSHOT_MAX_ZOOM = 8.0f;
// 圆角按短边比例算：贴图 alpha 和边框共用同一个比例，缩放时才对得上。
constexpr float SCREENSHOT_CARD_CORNER_FRACTION = 0.022f;

// 圆角直接做进 alpha：截图是 RGBA，把四角切成透明后，画面就从圆角里透出来。
// 贴图四边形本身没有圆角参数，这样比在角上补遮挡色块干净，也不怕背景在动。
static void ApplyRoundedAlpha(CImageInfo &Image)
{
	if(Image.m_Format != CImageInfo::FORMAT_RGBA || !Image.m_pData)
		return;
	const int Width = (int)Image.m_Width;
	const int Height = (int)Image.m_Height;
	// 与边框同一比例：卡片显示时圆角才能完全重合。
	const int Radius = std::clamp((int)std::round((float)std::min(Width, Height) * SCREENSHOT_CARD_CORNER_FRACTION), 6, 512);
	if(Width <= Radius * 2 || Height <= Radius * 2)
		return;
	for(int y = 0; y < Radius; ++y)
	{
		for(int x = 0; x < Radius; ++x)
		{
			const float DistanceX = (float)(Radius - x) - 0.5f;
			const float DistanceY = (float)(Radius - y) - 0.5f;
			const float Distance = std::sqrt(DistanceX * DistanceX + DistanceY * DistanceY);
			const float Coverage = std::clamp(Radius + 0.5f - Distance, 0.0f, 1.0f);
			if(Coverage >= 1.0f)
				continue;
			const int CornerX[4] = {x, Width - 1 - x, x, Width - 1 - x};
			const int CornerY[4] = {y, y, Height - 1 - y, Height - 1 - y};
			for(int Corner = 0; Corner < 4; ++Corner)
			{
				unsigned char *pPixel = Image.m_pData + ((size_t)CornerY[Corner] * Width + CornerX[Corner]) * 4;
				pPixel[3] = (unsigned char)(pPixel[3] * Coverage);
			}
		}
	}
}

CUiEffects::CUiEffects()
{
	m_Time = 0.0f;
}

void CUiEffects::OnReset()
{
	// 截图预览是界面效果，不随地图/连接状态重置：拍完的照片要能留在屏幕上等人点掉。
	// 断开、换图都会触发 OnReset，这里清掉的话照片会在回到列表的瞬间消失。
	m_vSmoothValues.clear();
	m_Time = 0.0f;
}

void CUiEffects::OnShutdown()
{
	if(m_ScreenshotAnimation.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);
	m_ScreenshotAnimation = CScreenshotAnimation();
}

void CUiEffects::OnRender()
{
	const float TimePassed = Client()->RenderFrameTime();
	m_Time += TimePassed;

	if(m_ScreenshotAnimation.m_Active)
	{
		m_ScreenshotAnimation.m_Time += TimePassed;
		RenderScreenshotAnimation();
	}

	if(!g_Config.m_ClHudAnimations)
		return;

	// Update all smooth values
	const float SpeedMultiplier = g_Config.m_ClHudAnimationSpeed / 100.0f;
	for(auto &Value : m_vSmoothValues)
	{
		if(std::abs(Value.m_Current - Value.m_Target) < SMOOTH_VALUE_EPSILON)
		{
			Value.m_Current = Value.m_Target;
			continue;
		}

		const float Distance = Value.m_Target - Value.m_Current;
		const float Step = Distance * Value.m_Speed * SpeedMultiplier * TimePassed * SMOOTH_VALUE_STEP_MULTIPLIER;
		Value.m_Current += Step;
	}
}

void CUiEffects::StartScreenshotAnimation(CImageInfo &&Image, const char *pPath)
{
	if(!Image.m_pData || Image.m_Width == 0 || Image.m_Height == 0)
		return;

	if(m_ScreenshotAnimation.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);

	const int ImageWidth = (int)Image.m_Width;
	const int ImageHeight = (int)Image.m_Height;
	// 截图是交换链里读回的 RGBA：部分后端/驱动给出的 alpha 通道是 0，直接上传就会
	// 得到一张全透明贴图——照片卡只剩下外框和按钮，看起来像"什么都没画出来"。
	// 截图永远不透明，这里统一归一化成 255，再去烘焙圆角。
	if(Image.m_Format == CImageInfo::FORMAT_RGBA && Image.m_pData != nullptr)
	{
		const size_t Count = (size_t)ImageWidth * (size_t)ImageHeight;
		unsigned char *pData = Image.m_pData;
		// 先抽样：绝大多数后端给的都是全不透明，抽样过关就不再整图走一遍
		// （4K 图整图扫描约 30ms，能省则省）。
		bool OpaqueSampled = true;
		for(size_t i = 0; i < Count; i += 32)
		{
			if(pData[i * 4 + 3] != 255)
			{
				OpaqueSampled = false;
				break;
			}
		}
		if(!OpaqueSampled)
		{
			for(size_t i = 0; i < Count; ++i)
				pData[i * 4 + 3] = 255;
		}
	}
	ApplyRoundedAlpha(Image);
	// 带 mipmap：照片缩到卡片大小时是 4K→1K 的降采样，没有 mipmap 会又糊又闪。
	IGraphics::CTextureHandle Texture = Graphics()->LoadTextureRawMove(Image, 0, "screenshot-preview");
	if(!Texture.IsValid())
		return;

	m_ScreenshotAnimation.m_Texture = Texture;
	m_ScreenshotAnimation.m_ImageWidth = ImageWidth;
	m_ScreenshotAnimation.m_ImageHeight = ImageHeight;
	m_ScreenshotAnimation.m_Time = 0.0f;
	m_ScreenshotAnimation.m_DismissTime = -1.0f;
	// 照片卡以「铺满卡片」为 1x，不按图片原始像素限制放大；换图会重置缩放和平移。
	m_ScreenshotAnimation.m_View = CQmImageView();
	m_ScreenshotAnimation.m_View.SetImageSize(ImageWidth, ImageHeight);
	m_ScreenshotAnimation.m_View.SetFitAllowsUpscale(true);
	// 落盘路径可能是相对存档目录的写法：界面要用它去开看图窗口，必须换成完整路径，
	// 否则子进程按自己的 cwd 找文件，会直接报「无法打开截图文件」。
	if(pPath != nullptr && pPath[0] != '\0')
	{
		const std::string CompletePath = qmclient::media_paths::CompletePath(Storage(), pPath);
		str_copy(m_ScreenshotAnimation.m_aPath, CompletePath.c_str());
		// CompletePath 对不存在的文件也会拼出一个看起来合法的路径：这里核对一次。
		// 1 倍抓帧的回调早于落盘，文件这时通常还没写完——只记录不报警；
		// 「查看截图」每帧用 HasPath 重新判定，等文件真的落盘再点亮。
		if(!fs_is_file(m_ScreenshotAnimation.m_aPath))
			log_info("screenshot", "saved screenshot not on disk yet: '%s'", m_ScreenshotAnimation.m_aPath);
	}
	else
	{
		m_ScreenshotAnimation.m_aPath[0] = '\0';
	}
	m_ScreenshotAnimation.m_Active = true;
}

void CUiEffects::RenderScreenshotAnimation()
{
	if(!m_ScreenshotAnimation.m_Texture.IsValid() || m_ScreenshotAnimation.m_ImageWidth <= 0 || m_ScreenshotAnimation.m_ImageHeight <= 0)
	{
		m_ScreenshotAnimation.m_Active = false;
		return;
	}

	// 只有玩家主动关闭时才卸载；缩到位之后画面一直留着。
	if(m_ScreenshotAnimation.m_DismissTime >= 0.0f &&
		m_ScreenshotAnimation.m_Time - m_ScreenshotAnimation.m_DismissTime >= SCREENSHOT_DISMISS_DURATION)
	{
		Graphics()->UnloadTexture(&m_ScreenshotAnimation.m_Texture);
		m_ScreenshotAnimation = CScreenshotAnimation();
		return;
	}

	float PreviousTopLeftX = 0.0f;
	float PreviousTopLeftY = 0.0f;
	float PreviousBottomRightX = 0.0f;
	float PreviousBottomRightY = 0.0f;
	Graphics()->GetScreen(&PreviousTopLeftX, &PreviousTopLeftY, &PreviousBottomRightX, &PreviousBottomRightY);

	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const float ScreenWidth = pScreen->w;
	const float ScreenHeight = pScreen->h;

	const float ShrinkT = std::clamp((m_ScreenshotAnimation.m_Time - SCREENSHOT_SHRINK_START) / (SCREENSHOT_SHRINK_END - SCREENSHOT_SHRINK_START), 0.0f, 1.0f);
	const float Ease = ApplyTransition(ShrinkT, TRANSITION_EASE_OUT);
	const float ImageAspect = (float)m_ScreenshotAnimation.m_ImageWidth / (float)m_ScreenshotAnimation.m_ImageHeight;
	// 目标：屏幕正中、按画面比例缩到 62%，高度再卡一次上限，避免竖图顶到边。
	float TargetWidth = ScreenWidth * SCREENSHOT_CARD_SCREEN_FRACTION;
	float TargetHeight = TargetWidth / ImageAspect;
	const float MaxTargetHeight = ScreenHeight * SCREENSHOT_CARD_MAX_HEIGHT_FRACTION;
	if(TargetHeight > MaxTargetHeight)
	{
		TargetHeight = MaxTargetHeight;
		TargetWidth = TargetHeight * ImageAspect;
	}
	const float TargetX = (ScreenWidth - TargetWidth) * 0.5f;
	const float TargetY = (ScreenHeight - TargetHeight) * 0.5f;
	// 视口固定取目标卡片大小：卡片展开动画每帧改变显示尺寸，缩放/平移状态不能跟着抖。
	const int ViewWidth = std::max(1, (int)std::round(TargetWidth));
	const int ViewHeight = std::max(1, (int)std::round(TargetHeight));
	CQmImageView &View = m_ScreenshotAnimation.m_View;
	// 卡片以贴合为 1x，滚轮范围 1-8 倍；窗口缩放导致视口变化时保持相对缩放。
	const float RelativeZoom = View.RelativeZoom();
	View.SetViewportSize(ViewWidth, ViewHeight);
	View.SetScaleLimits(View.FitScale(), View.FitScale() * SCREENSHOT_MAX_ZOOM);
	if(!View.IsFit())
		View.SetScale(View.FitScale() * RelativeZoom);

	const float CardX = TargetX * Ease;
	const float CardY = TargetY * Ease;
	const float CardW = ScreenWidth + (TargetWidth - ScreenWidth) * Ease;
	const float CardH = ScreenHeight + (TargetHeight - ScreenHeight) * Ease;

	// 照片本身是看图窗口：滚轮缩放、按住拖动、右上角关闭、右下角跳转看图程序。
	// 点击用原始输入判定——卡片画在菜单之后，Ui 的按钮归属在别的组件里，拿不到这次点击。
	const bool Interactive = GameClient()->m_Menus.IsActive();
	const bool Settled = ShrinkT >= 1.0f;
	const CUIRect CardRect{CardX, CardY, CardW, CardH};
	// 圆角按卡片短边取同一比例：贴图 alpha 和边框用同一个半径，才不会「图角比边框大」。
	const float CardRadius = std::round(std::min(CardW, CardH) * SCREENSHOT_CARD_CORNER_FRACTION);
	const float CloseSize = 24.0f;
	const CUIRect CloseRect{CardX + CardW - CloseSize - 8.0f, CardY + 8.0f, CloseSize, CloseSize};
	// 「查看截图」只在文件确实存在时可用：路径存在但文件缺失（例如保存失败或已被删除）
	// 时按钮置灰，避免打开一个必然报错的看图窗口。
	const bool HasPath = m_ScreenshotAnimation.m_aPath[0] != '\0' && fs_is_file(m_ScreenshotAnimation.m_aPath);
	const CUIRect ViewRect{CardX + 10.0f, CardY + CardH - 32.0f, 120.0f, 24.0f};
	const vec2 MousePos = Ui()->MousePos();
	const auto Inside = [&](const CUIRect &Rect) {
		return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
	};

	static bool s_WasMouseDown = false;
	static vec2 s_LastMousePos{0.0f, 0.0f};
	static int s_PressedControl = 0; // 0=无 1=关闭 2=查看截图 3=拖动
	const bool MouseDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	if(Settled && m_ScreenshotAnimation.m_DismissTime < 0.0f && Interactive)
	{
		if(Input()->KeyPress(KEY_MOUSE_1))
		{
			if(Inside(CloseRect))
				s_PressedControl = 1;
			else if(HasPath && Inside(ViewRect))
				s_PressedControl = 2;
			else if(Inside(CardRect))
				s_PressedControl = 3;
			else
				m_ScreenshotAnimation.m_DismissTime = m_ScreenshotAnimation.m_Time;
		}
		if(s_PressedControl == 3 && MouseDown)
			View.Pan(MousePos.x - s_LastMousePos.x, MousePos.y - s_LastMousePos.y);
		if(!MouseDown && s_WasMouseDown)
		{
			if(s_PressedControl == 1 && Inside(CloseRect))
				m_ScreenshotAnimation.m_DismissTime = m_ScreenshotAnimation.m_Time;
			else if(s_PressedControl == 2 && HasPath && Inside(ViewRect))
			{
				OpenQmScreenshotViewer(Client(), m_ScreenshotAnimation.m_aPath, Localize("Error opening screenshot"), Localize("Unable to open the screenshot file"));
				m_ScreenshotAnimation.m_DismissTime = m_ScreenshotAnimation.m_Time;
			}
			s_PressedControl = 0;
		}
		// 滚轮只在这张照片上生效，避免和背后的列表滚动抢。
		int Steps = 0;
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			Steps = 1;
		else if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			Steps = -1;
		if(Steps != 0 && Inside(CardRect))
		{
			// 以鼠标位置为锚点缩放：指哪放大哪，和看图程序一致。
			View.ZoomAt(std::pow(1.2f, (float)Steps), MousePos.x - CardX, MousePos.y - CardY);
		}
	}
	s_WasMouseDown = MouseDown;
	s_LastMousePos = MousePos;
	if(Settled && Interactive && Input()->KeyPress(KEY_ESCAPE) && m_ScreenshotAnimation.m_DismissTime < 0.0f)
		m_ScreenshotAnimation.m_DismissTime = m_ScreenshotAnimation.m_Time;
	const float DismissT = m_ScreenshotAnimation.m_DismissTime < 0.0f ? 0.0f :
									    std::clamp((m_ScreenshotAnimation.m_Time - m_ScreenshotAnimation.m_DismissTime) / SCREENSHOT_DISMISS_DURATION, 0.0f, 1.0f);
	const float CardAlpha = 1.0f - DismissT;
	const float FlashAlpha = m_ScreenshotAnimation.m_Time < SCREENSHOT_FLASH_DURATION ? 0.65f * (1.0f - m_ScreenshotAnimation.m_Time / SCREENSHOT_FLASH_DURATION) : 0.0f;

	// 不再画投影：卡片边缘只有一条 1px 描边。之前那张 0.28 黑的圆角"影子"
	// 是硬边、还偏移 (2+8*Ease)，在照片旁边会露出一条明显的深色带，看起来像
	// 多了一层底板；照片本身已经铺满圆角，不需要靠投影找体积。

	// 视口按整数像素取整，再换算回卡片当前大小：1x 正好铺满卡片，展开动画按比例缩。
	const CQmImageView::SRect ImageRect = View.ImageRect();
	const float RectScaleX = View.ViewportWidth() > 0 ? CardW / (float)View.ViewportWidth() : 1.0f;
	const float RectScaleY = View.ViewportHeight() > 0 ? CardH / (float)View.ViewportHeight() : 1.0f;
	// 图片在卡片内的位置与大小（缩放/平移后的可见区）。
	const float ImageLeft = ImageRect.m_X * RectScaleX;
	const float ImageTop = ImageRect.m_Y * RectScaleY;
	const float ImageW = ImageRect.m_Width * RectScaleX;
	const float ImageH = ImageRect.m_Height * RectScaleY;
	// 细边框把圆角轮廓勾出来，缩到中间后才有「一张照片」的体积感。
	// 走 SDF 纹理管线时边框和照片同一次绘制提交（同一套圆角覆盖度），
	// 只有回退到四边形时才需要单独描边。
	// 边框 alpha 先不含 CardAlpha：DrawRoundedImage 内部会再乘一次 Image.m_Alpha，
	// 回退路径才在这里补上，避免 SDF 路径把淡出算两遍（0.16*Ease*CardAlpha²）。
	const ColorRGBA BorderTint = Ease > 0.04f ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f * Ease) : ColorRGBA();
	// 照片作为圆角容器的填充直接采样：四角由圆角覆盖度真实裁掉，放大/平移后
	// 图片铺满卡片也不会露出方角。UV 是「卡片像素 → 图片 UV」的仿射，可见区
	// （ImageLeft/ImageW 等）来自看图窗口的缩放/平移状态。
	SRoundedImageParams Image;
	Image.m_Texture = m_ScreenshotAnimation.m_Texture;
	Image.m_UVScale = vec2(1.0f / maximum(ImageW, 0.0001f), 1.0f / maximum(ImageH, 0.0001f));
	Image.m_UVOffset = vec2(-ImageLeft / maximum(ImageW, 0.0001f), -ImageTop / maximum(ImageH, 0.0001f));
	Image.m_Alpha = CardAlpha;
	const bool RoundedPhoto = DrawRoundedImage(Ui(), CardRect, Image, BorderTint, CardRadius, 1.0f);
	if(!RoundedPhoto)
	{
		// 能力回退：旧后端（Metal 等）没有纹理圆角管线，只能画方角四边形，
		// 四角靠贴图里预烘焙的圆角 alpha。1x 贴合时与边框完全重合，放大后露方角。
		Graphics()->BlendNormal();
		Graphics()->TextureSet(m_ScreenshotAnimation.m_Texture);
		Ui()->ClipEnable(&CardRect);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, CardAlpha);
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem PreviewQuad(CardX + ImageLeft, CardY + ImageTop, ImageW, ImageH);
		Graphics()->QuadsDrawTL(&PreviewQuad, 1);
		Graphics()->QuadsEnd();
		Ui()->ClipDisable();
		Graphics()->TextureClear();
		// 细边框把圆角轮廓勾出来，缩到中间后才有「一张照片」的体积感。
		if(BorderTint.a > 0.0f)
		{
			const CUIRect BorderRect{CardX, CardY, CardW, CardH};
			DrawRoundedSurface(Ui(), BorderRect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ColorRGBA(BorderTint.r, BorderTint.g, BorderTint.b, BorderTint.a * CardAlpha), CardRadius, 1.0f);
		}
	}

	// 看图窗口的两个操作：右上角关闭、右下角跳到独立看图程序。
	if(Settled && Interactive)
	{
		const ColorRGBA ButtonFill = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * CardAlpha);
		const ColorRGBA ButtonBorder = ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f * CardAlpha);
		DrawRoundedSurface(Ui(), CloseRect, ButtonFill, ButtonBorder, CloseSize * 0.5f, 1.0f);
		Ui()->DoLabel(&CloseRect, "×", 17.0f, TEXTALIGN_MC);

		if(HasPath)
		{
			DrawRoundedSurface(Ui(), ViewRect, ButtonFill, ButtonBorder, 6.0f, 1.0f);
			SLabelProperties ViewProps;
			ViewProps.m_MaxWidth = (int)ViewRect.w;
			ViewProps.m_DisallowNewline = true;
			ViewProps.m_EllipsisAtEnd = true;
			ViewProps.m_EnableWidthCheck = false;
			Ui()->DoLabel(&ViewRect, Localize("View screenshot"), 12.0f, TEXTALIGN_MC, ViewProps);
		}
	}

	if(FlashAlpha > 0.0f)
	{
		Graphics()->BlendNormal();
		Graphics()->DrawRect(0.0f, 0.0f, ScreenWidth, ScreenHeight, ColorRGBA(1.0f, 1.0f, 1.0f, FlashAlpha), IGraphics::CORNER_NONE, 0.0f);
	}

	// 照片画在菜单之后，而软件光标是菜单里画的：这里补画一次，鼠标才会显示在照片上面。
	if(Interactive)
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

	Graphics()->MapScreen(PreviousTopLeftX, PreviousTopLeftY, PreviousBottomRightX, PreviousBottomRightY);
}

float CUiEffects::ApplyTransition(float t, ETransitionType Type)
{
	t = std::clamp(t, 0.0f, 1.0f);

	switch(Type)
	{
	case TRANSITION_LINEAR:
		return t;
	case TRANSITION_EASE_IN:
		return t * t;
	case TRANSITION_EASE_OUT:
		return t * (2.0f - t);
	case TRANSITION_EASE_IN_OUT:
		return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
	case TRANSITION_BOUNCE:
	{
		const float N1 = 7.5625f;
		const float D1 = 2.75f;
		if(t < 1.0f / D1)
		{
			return N1 * t * t;
		}
		else if(t < 2.0f / D1)
		{
			t -= 1.5f / D1;
			return N1 * t * t + 0.75f;
		}
		else if(t < 2.5f / D1)
		{
			t -= 2.25f / D1;
			return N1 * t * t + 0.9375f;
		}
		else
		{
			t -= 2.625f / D1;
			return N1 * t * t + 0.984375f;
		}
	}
	default:
		return t;
	}
}

int CUiEffects::CreateSmoothValue(float Initial, float Speed, ETransitionType Type)
{
	CSmoothValue Value;
	Value.m_Current = Initial;
	Value.m_Target = Initial;
	Value.m_Speed = Speed;
	Value.m_Type = Type;

	m_vSmoothValues.push_back(Value);
	return m_vSmoothValues.size() - 1;
}

void CUiEffects::SetSmoothValue(int Index, float Target)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;

	m_vSmoothValues[Index].m_Target = Target;
}

float CUiEffects::GetSmoothValue(int Index)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return 0.0f;

	return m_vSmoothValues[Index].m_Current;
}

void CUiEffects::UpdateSmoothValue(int Index, float Speed)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;

	m_vSmoothValues[Index].m_Speed = Speed;
}

ColorRGBA CUiEffects::LerpColor(const ColorRGBA &From, const ColorRGBA &To, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return ColorRGBA(
		From.r + (To.r - From.r) * t,
		From.g + (To.g - From.g) * t,
		From.b + (To.b - From.b) * t,
		From.a + (To.a - From.a) * t);
}

ColorRGBA CUiEffects::PulseColor(const ColorRGBA &Color, float Time, float Speed)
{
	const float Pulse = (std::sin(Time * Speed * PI) + 1.0f) * 0.5f;
	return ColorRGBA(
		Color.r * (0.5f + Pulse * 0.5f),
		Color.g * (0.5f + Pulse * 0.5f),
		Color.b * (0.5f + Pulse * 0.5f),
		Color.a);
}

float CUiEffects::GetPulse(float Speed)
{
	return (std::sin(m_Time * Speed * PI) + 1.0f) * 0.5f;
}

float CUiEffects::GetWave(float Speed, float Offset)
{
	return std::sin((m_Time * Speed + Offset) * PI);
}

float CUiEffects::GetBounce(float Speed)
{
	const float t = std::fmod(m_Time * Speed, 1.0f);
	return ApplyTransition(t, TRANSITION_BOUNCE);
}
