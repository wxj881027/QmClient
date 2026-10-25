#include "screenshot_viewer.h"

#include "screenshot_image_job.h"
#include "screenshot_viewer_viewport.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/storage.h>

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
#endif

bool OpenQmScreenshotViewer(IClient *pClient, const char *pPath, const char *pErrorTitle, const char *pErrorMessage)
{
	try
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
#if defined(CONF_FAMILY_WINDOWS)
		const PROCESS Process = shell_execute(aExecutable, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments));
		if(Process == INVALID_PROCESS)
			return false;
		CloseHandle(Process);
		return true;
#else
		return QmLaunchScreenshotViewerProcess([&]() { return shell_execute(aExecutable, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)); }, [](PROCESS Process) {
			while(waitpid(Process, nullptr, 0) < 0 && errno == EINTR)
			{
			} });
#endif
#endif
	}
	catch(const std::exception &)
	{
		return false;
	}
}

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
		if(!CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), pPath, IStorage::TYPE_ABSOLUTE, Image))
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

	CQmScreenshotViewerViewport Viewport;
	Viewport.SetImageSize((int)Image.m_Width, (int)Image.m_Height);
	Image.Free();
	bool Dragging = false;
	bool Running = true;
	bool Dirty = true;
	while(Running)
	{
		int WindowWidth, WindowHeight;
		SDL_GetWindowSize(pWindow.get(), &WindowWidth, &WindowHeight);
		int OutputWidth, OutputHeight;
		if(!SDL_GetCurrentRenderOutputSize(pRenderer.get(), &OutputWidth, &OutputHeight))
			return Fail();
		Viewport.Resize(OutputWidth, OutputHeight);
		const float MouseScaleX = WindowWidth > 0 ? (float)OutputWidth / WindowWidth : 1.0f;
		const float MouseScaleY = WindowHeight > 0 ? (float)OutputHeight / WindowHeight : 1.0f;
		if(Dirty && OutputWidth > 0 && OutputHeight > 0)
		{
			SDL_SetRenderDrawColor(pRenderer.get(), 24, 24, 24, 255);
			SDL_RenderClear(pRenderer.get());
			const auto Rect = Viewport.ImageRect();
			const SDL_FRect Destination{Rect.m_X, Rect.m_Y, std::max(1.0f, Rect.m_Width), std::max(1.0f, Rect.m_Height)};
			if(!SDL_RenderTexture(pRenderer.get(), pTexture.get(), nullptr, &Destination))
				return Fail();
			SDL_RenderPresent(pRenderer.get());
			char aTitle[IO_MAX_PATH_LENGTH + 64];
			str_format(aTitle, sizeof(aTitle), "%s | %.0f%% | QmClient", fs_filename(pPath), Viewport.Scale() * 100.0f);
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
