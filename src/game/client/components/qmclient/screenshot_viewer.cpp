#include "screenshot_viewer.h"

#include "screenshot_image_job.h"
#include "screenshot_viewer_viewport.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmImageView.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <memory>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_IOS) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <sys/wait.h>

#include <cerrno>
#include <thread>
#endif

bool OpenQmScreenshotViewer(IClient *pClient, const char *pPath, const char *pErrorTitle, const char *pErrorMessage)
{
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS) || defined(CONF_PLATFORM_EMSCRIPTEN)
	// 移动端和浏览器保留平台原有的文件打开能力。
	return pClient->ViewFile(pPath);
#else
	std::string Path = pPath;
	if(fs_is_relative_path(pPath))
	{
		char aWorkingDirectory[IO_MAX_PATH_LENGTH];
		if(fs_getcwd(aWorkingDirectory, sizeof(aWorkingDirectory)) == nullptr)
			return false;
		Path = std::string(aWorkingDirectory) + "/" + pPath;
	}
	char aExecutable[IO_MAX_PATH_LENGTH];
#if defined(CONF_FAMILY_WINDOWS)
	// 路径辅助函数会核对 GetLastError，不能继承此前 SDL 调用留下的错误。
	SetLastError(ERROR_SUCCESS);
#endif
	if(fs_executable_path(aExecutable, sizeof(aExecutable)) != 0)
		return false;
	const char *apArguments[] = {"--qm-screenshot-viewer", Path.c_str(), pErrorTitle, pErrorMessage};
	const PROCESS Process = shell_execute(aExecutable, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments));
	if(Process == INVALID_PROCESS)
		return false;
#if defined(CONF_FAMILY_WINDOWS)
	CloseHandle(Process);
#else
	// 回收子进程，等待发生在独立线程，不阻塞游戏主循环。
	std::thread([Process]() {
		while(waitpid(Process, nullptr, 0) < 0 && errno == EINTR)
		{
		}
	}).detach();
#endif
	return true;
#endif
}

namespace
{
	void MaskRoundedCorners(SDL_Renderer *pRenderer, const SDL_FRect &Rect, float Radius, int OutputWidth, int OutputHeight)
	{
		if(pRenderer == nullptr || Radius <= 0.5f || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		// 半径不能超过窄边的一半，否则四个角会互相吃掉整块矩形。
		const float R = std::min(Radius, std::min(Rect.w, Rect.h) * 0.5f);
		const int Rows = (int)std::ceil(R);
		SDL_SetRenderDrawColor(pRenderer, 24, 24, 24, 255);
		for(int Edge = 0; Edge < 2; ++Edge)
		{
			for(int Row = 0; Row < Rows; ++Row)
			{
				const float Inset = qm_screenshot_viewer::RoundedCornerInset(R, (float)Row);
				if(Inset <= 0.0f)
					continue;
				const float Y = Edge == 0 ? Rect.y + (float)Row : Rect.y + Rect.h - 1.0f - (float)Row;
				if(Y < 0.0f || Y >= (float)OutputHeight)
					continue;
				const SDL_FRect Left{Rect.x, Y, Inset, 1.0f};
				const SDL_FRect Right{Rect.x + Rect.w - Inset, Y, Inset, 1.0f};
				SDL_RenderFillRect(pRenderer, &Left);
				SDL_RenderFillRect(pRenderer, &Right);
			}
		}
	}
} // namespace

int RunQmScreenshotViewer(const char *pPath, const char *pErrorTitle, const char *pErrorMessage)
{
	SDL_SetMainReady();
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, pErrorTitle, pErrorMessage, nullptr);
		return 1;
	}
	struct SSdlLifetime
	{
		~SSdlLifetime() { SDL_Quit(); }
	} SdlLifetime;
	using CWindow = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
	using CRenderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
	using CTexture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
	struct SImageLifetime
	{
		CImageInfo m_Image;
		~SImageLifetime() { m_Image.Free(); }
	} ImageLifetime;
	CImageInfo &Image = ImageLifetime.m_Image;

	const auto Fail = [&]() {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, pErrorTitle, pErrorMessage, nullptr);
		return 1;
	};

	try
	{
		auto pStorage = CreateLocalStorage();
		// 超采样静帧可以到 4 倍（约 530 MB RGBA，文件也可能超过 64 MiB）：
		// 看图窗口要用更大的预算，否则「查看截图」只会得到「无法打开截图文件」。
		constexpr size_t VIEWER_MAX_DECODED_BYTES = 768 * 1024 * 1024;
		if(!CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), pPath, IStorage::TYPE_ABSOLUTE, Image, VIEWER_MAX_DECODED_BYTES))
			return Fail();
	}
	catch(const std::exception &)
	{
		return Fail();
	}

	SDL_Rect Bounds{0, 0, 1280, 800};
	if(!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &Bounds))
		Bounds = {0, 0, 1280, 800};
	const int Width = std::max(1, std::min((int)Image.m_Width, Bounds.w * 4 / 5));
	const int Height = std::max(1, std::min((int)Image.m_Height, Bounds.h * 4 / 5));
	CWindow pWindow(SDL_CreateWindow("QmClient",
				std::max(320, Width), std::max(240, Height), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
		SDL_DestroyWindow);
	if(!pWindow)
		return Fail();
	SDL_SetWindowPosition(pWindow.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_StopTextInput(pWindow.get());
	// 软件渲染器独立于游戏使用的 OpenGL/Vulkan/Metal 后端与驱动状态。
	CRenderer pRenderer(SDL_CreateRenderer(pWindow.get(), "software"), SDL_DestroyRenderer);
	if(!pRenderer)
		return Fail();
	CTexture pTexture(SDL_CreateTexture(pRenderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, (int)Image.m_Width, (int)Image.m_Height), SDL_DestroyTexture);
	if(!pTexture || !SDL_UpdateTexture(pTexture.get(), nullptr, Image.m_pData, (int)Image.m_Width * 4))
		return Fail();
	SDL_SetTextureScaleMode(pTexture.get(), SDL_SCALEMODE_LINEAR);
	SDL_SetTextureBlendMode(pTexture.get(), SDL_BLENDMODE_BLEND);
	SDL_SetWindowMinimumSize(pWindow.get(), 320, 240);

	const int ImageWidth = (int)Image.m_Width;
	const int ImageHeight = (int)Image.m_Height;
	CQmImageView Viewport;
	Viewport.SetImageSize(ImageWidth, ImageHeight);
	Image.Free();
	bool Dragging = false;
	bool Running = true;
	bool Dirty = true;
	// 默认平滑缩放；按住 Ctrl 切到最近邻，逐像素检查截图内容。
	bool PixelMode = false;
	bool LastPixelMode = false;
	while(Running)
	{
		const bool CurrentPixelMode = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
		if(CurrentPixelMode != LastPixelMode)
		{
			LastPixelMode = CurrentPixelMode;
			Dirty = true;
		}
		PixelMode = CurrentPixelMode;
		Viewport.SetPixelMode(CurrentPixelMode);
		int WindowWidth, WindowHeight;
		SDL_GetWindowSize(pWindow.get(), &WindowWidth, &WindowHeight);
		int OutputWidth, OutputHeight;
		if(!SDL_GetCurrentRenderOutputSize(pRenderer.get(), &OutputWidth, &OutputHeight))
			return Fail();
		Viewport.SetViewportSize(OutputWidth, OutputHeight);
		const float MouseScaleX = WindowWidth > 0 ? (float)OutputWidth / WindowWidth : 1.0f;
		const float MouseScaleY = WindowHeight > 0 ? (float)OutputHeight / WindowHeight : 1.0f;
		if(Dirty && OutputWidth > 0 && OutputHeight > 0)
		{
			SDL_SetRenderDrawColor(pRenderer.get(), 24, 24, 24, 255);
			SDL_RenderClear(pRenderer.get());
			// 采样策略由共享视口决定：1:1 附近用最近邻，大幅缩小时平滑；Ctrl 强制最近邻。
			const bool UseNearest = Viewport.Sampling() == CQmImageView::ESampling::PIXEL;
			SDL_SetTextureScaleMode(pTexture.get(), UseNearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
			const auto Rect = Viewport.ImageRect();
			const float Scale = Viewport.Scale();
			// 只换算窗口里真正看得见的那一块：放大 6 倍时整幅目标矩形有两万多像素宽，
			// 软件渲染器会老老实实算完整块，于是每滚一格要卡好几秒。
			const float VisibleLeft = std::max(Rect.m_X, 0.0f);
			const float VisibleTop = std::max(Rect.m_Y, 0.0f);
			const float VisibleRight = std::min(Rect.m_X + Rect.m_Width, (float)OutputWidth);
			const float VisibleBottom = std::min(Rect.m_Y + Rect.m_Height, (float)OutputHeight);
			if(VisibleRight > VisibleLeft && VisibleBottom > VisibleTop && Scale > 0.0f)
			{
				int SrcX = (int)std::floor((VisibleLeft - Rect.m_X) / Scale);
				int SrcY = (int)std::floor((VisibleTop - Rect.m_Y) / Scale);
				int SrcW = (int)std::ceil((VisibleRight - VisibleLeft) / Scale) + 1;
				int SrcH = (int)std::ceil((VisibleBottom - VisibleTop) / Scale) + 1;
				SrcX = std::clamp(SrcX, 0, ImageWidth - 1);
				SrcY = std::clamp(SrcY, 0, ImageHeight - 1);
				SrcW = std::clamp(SrcW, 1, ImageWidth - SrcX);
				SrcH = std::clamp(SrcH, 1, ImageHeight - SrcY);
				const SDL_FRect Source{(float)SrcX, (float)SrcY, (float)SrcW, (float)SrcH};
				const SDL_FRect Destination{VisibleLeft, VisibleTop, VisibleRight - VisibleLeft, VisibleBottom - VisibleTop};
				if(!SDL_RenderTexture(pRenderer.get(), pTexture.get(), &Source, &Destination))
					return Fail();
			}
			// 图片按原样贴出，圆角由外层容器裁剪：只在角上补背景色，不动像素。
			MaskRoundedCorners(pRenderer.get(), SDL_FRect{Rect.m_X, Rect.m_Y, Rect.m_Width, Rect.m_Height}, 14.0f * (float)OutputWidth / 1600.0f, OutputWidth, OutputHeight);
			SDL_RenderPresent(pRenderer.get());
			char aTitle[IO_MAX_PATH_LENGTH + 64];
			str_format(aTitle, sizeof(aTitle), "%s | %.0f%%%s | QmClient", fs_filename(pPath), Viewport.Scale() * 100.0f, PixelMode ? " | pixel (Ctrl)" : "");
			SDL_SetWindowTitle(pWindow.get(), aTitle);
			Dirty = false;
		}

		SDL_Event Event;
		if(!SDL_WaitEventTimeout(&Event, 100))
			continue;
		switch(Event.type)
		{
		case SDL_EVENT_QUIT:
			Running = false;
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			Running = false;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			Dragging = false;
			SDL_CaptureMouse(false);
			break;
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			Dirty = true;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
		{
			float X, Y;
			SDL_GetMouseState(&X, &Y);
			const int Steps = Event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -Event.wheel.integer_y : Event.wheel.integer_y;
			Viewport.ZoomAt(std::pow(1.2f, (float)std::clamp(Steps, -20, 20)), X * MouseScaleX, Y * MouseScaleY);
			Dirty = true;
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if(Event.button.button == SDL_BUTTON_LEFT)
			{
				if(Event.button.clicks == 2)
					Viewport.ToggleFit();
				else
				{
					Dragging = true;
					SDL_CaptureMouse(true);
				}
				Dirty = true;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if(Event.button.button == SDL_BUTTON_LEFT)
			{
				Dragging = false;
				SDL_CaptureMouse(false);
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if(Dragging)
			{
				Viewport.Pan(Event.motion.xrel * MouseScaleX, Event.motion.yrel * MouseScaleY);
				Dirty = true;
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			switch(Event.key.key)
			{
			case SDLK_ESCAPE: Running = false; break;
			case SDLK_0: Viewport.Fit(); break;
			case SDLK_1: Viewport.ActualSize(); break;
			case SDLK_PLUS:
			case SDLK_EQUALS:
			case SDLK_KP_PLUS: Viewport.ZoomAt(1.2f, OutputWidth * 0.5f, OutputHeight * 0.5f); break;
			case SDLK_MINUS:
			case SDLK_KP_MINUS: Viewport.ZoomAt(1.0f / 1.2f, OutputWidth * 0.5f, OutputHeight * 0.5f); break;
			default: break;
			}
			Dirty = true;
			break;
		}
	}
	SDL_CaptureMouse(false);
	return 0;
}
