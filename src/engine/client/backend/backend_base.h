#ifndef ENGINE_CLIENT_BACKEND_BACKEND_BASE_H
#define ENGINE_CLIENT_BACKEND_BACKEND_BASE_H

#include <engine/client/graphics_threaded.h>
#include <engine/graphics.h>

#ifndef BACKEND_NO_SDL
#include <SDL_video.h>
#else
struct SDL_Window;
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SBackendCapabilities;

// 可选的图形后端诊断输出。它只由图形线程写入，在命令完成并等待空闲后读取。
struct SGraphicsBackendDiagnostics
{
	static constexpr size_t STRING_SIZE = 256;
	static constexpr size_t EXTENSIONS_SIZE = 4096;

	char m_aShadingLanguageVersion[STRING_SIZE]{};
	char m_aExtensions[EXTENSIONS_SIZE]{};
	char m_aContextProfile[32]{};
	char m_aDebugCallbackUnavailableReason[128]{};
	char m_aVulkanInstanceApiVersion[STRING_SIZE]{};
	char m_aVulkanDeviceApiVersion[STRING_SIZE]{};
	char m_aVulkanDeviceName[STRING_SIZE]{};
	char m_aVulkanDriverVersion[STRING_SIZE]{};
	char m_aVulkanPresentMode[32]{};
	char m_aVulkanSurfaceFormat[64]{};
	char m_aVulkanTimestampQueryUnavailableReason[128]{};
	int m_ExtensionCount = -1;
	int m_MaxTextureSize = 0;
	int m_VulkanInstanceExtensionCount = -1;
	int m_VulkanLayerCount = -1;
	int m_VulkanQueueFamilyCount = -1;
	int m_VulkanGraphicsQueueFamily = -1;
	int m_VulkanPresentQueueFamily = -1;
	int m_VulkanMemoryHeapCount = -1;
	int m_VulkanMemoryTypeCount = -1;
	int m_VulkanSwapchainImageCount = -1;
	int m_VulkanSwapchainWidth = 0;
	int m_VulkanSwapchainHeight = 0;
	bool m_ShadingLanguageVersionAvailable = false;
	bool m_ExtensionsAvailable = false;
	bool m_ExtensionsTruncated = false;
	bool m_ContextProfileAvailable = false;
	bool m_DebugOutputSupported = false;
	bool m_DebugCallbackEnabled = false;
	bool m_DebugCallbackSynchronous = false;
	bool m_VulkanValidationLayerRequested = false;
	bool m_VulkanValidationLayerEnabled = false;
	bool m_VulkanDebugCallbackEnabled = false;
	bool m_VulkanDeviceFaultAvailable = false;
	bool m_VulkanDeviceFaultEnabled = false;
	bool m_VulkanTimestampQuerySupported = false;
};

enum EDebugGfxModes
{
	DEBUG_GFX_MODE_NONE = 0,
	DEBUG_GFX_MODE_MINIMUM,
	DEBUG_GFX_MODE_AFFECTS_PERFORMANCE,
	DEBUG_GFX_MODE_VERBOSE,
	DEBUG_GFX_MODE_ALL,
};

enum ERunCommandReturnTypes
{
	RUN_COMMAND_COMMAND_HANDLED = 0,
	RUN_COMMAND_COMMAND_UNHANDLED,
	RUN_COMMAND_COMMAND_WARNING,
	RUN_COMMAND_COMMAND_ERROR,
};

enum EGfxErrorType
{
	GFX_ERROR_TYPE_NONE = 0,
	GFX_ERROR_TYPE_INIT,
	GFX_ERROR_TYPE_OUT_OF_MEMORY_IMAGE,
	GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER,
	GFX_ERROR_TYPE_OUT_OF_MEMORY_STAGING,
	GFX_ERROR_TYPE_RENDER_RECORDING,
	GFX_ERROR_TYPE_RENDER_CMD_FAILED,
	GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED,
	GFX_ERROR_TYPE_SWAP_FAILED,
	GFX_ERROR_TYPE_UNKNOWN,
};

enum EGfxWarningType
{
	GFX_WARNING_TYPE_NONE = 0,
	GFX_WARNING_TYPE_INIT_FAILED,
	GFX_WARNING_TYPE_INIT_FAILED_MISSING_INTEGRATED_GPU_DRIVER,
	GFX_WARNING_TYPE_INIT_FAILED_NO_DEVICE_WITH_REQUIRED_VERSION,
	GFX_WARNING_LOW_ON_MEMORY,
	GFX_WARNING_MISSING_EXTENSION,
	GFX_WARNING_TYPE_UNKNOWN,
};

struct SGfxErrorContainer
{
	struct SError
	{
		bool m_RequiresTranslation;
		std::string m_Err;

		bool operator==(const SError &Other) const
		{
			return m_RequiresTranslation == Other.m_RequiresTranslation && m_Err == Other.m_Err;
		}
	};
	EGfxErrorType m_ErrorType = EGfxErrorType::GFX_ERROR_TYPE_NONE;
	std::vector<SError> m_vErrors;
};

struct SGfxWarningContainer
{
	EGfxWarningType m_WarningType = EGfxWarningType::GFX_WARNING_TYPE_NONE;
	std::vector<std::string> m_vWarnings;
};

class CCommandProcessorFragment_GLBase
{
protected:
	SGfxErrorContainer m_Error;
	SGfxWarningContainer m_Warning;

	static void Texture2DTo3D(uint8_t *pImageBuffer, int ImageWidth, int ImageHeight, size_t PixelSize, int SplitCountWidth, int SplitCountHeight, uint8_t *pTarget3DImageData, int &Target3DImageWidth, int &Target3DImageHeight);

	virtual bool GetPresentedImageData(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData) = 0;

public:
	virtual ~CCommandProcessorFragment_GLBase() = default;
	virtual ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *pBaseCommand) = 0;

	virtual void StartCommands(size_t CommandCount, size_t EstimatedRenderCallCount) {}
	virtual void EndCommands() {}

	const SGfxErrorContainer &GetError() { return m_Error; }
	virtual void ErroneousCleanup() {}

	const SGfxWarningContainer &GetWarning() { return m_Warning; }

	enum
	{
		CMD_PRE_INIT = CCommandBuffer::CMDGROUP_PLATFORM_GL,
		CMD_INIT,
		CMD_SHUTDOWN,
		CMD_POST_SHUTDOWN,
	};

	struct SCommand_PreInit : public CCommandBuffer::SCommand
	{
		SCommand_PreInit() :
			SCommand(CMD_PRE_INIT) {}

		SDL_Window *m_pWindow;
		uint32_t m_Width;
		uint32_t m_Height;

		char *m_pVendorString;
		char *m_pVersionString;
		char *m_pRendererString;

		TTwGraphicsGpuList *m_pGpuList;
		SGraphicsBackendDiagnostics *m_pDiagnostics = nullptr;
	};

	struct SCommand_Init : public CCommandBuffer::SCommand
	{
		SCommand_Init() :
			SCommand(CMD_INIT) {}

		SDL_Window *m_pWindow;
		uint32_t m_Width;
		uint32_t m_Height;

		class IStorage *m_pStorage;
		std::atomic<uint64_t> *m_pTextureMemoryUsage;
		std::atomic<uint64_t> *m_pBufferMemoryUsage;
		std::atomic<uint64_t> *m_pStreamMemoryUsage;
		std::atomic<uint64_t> *m_pStagingMemoryUsage;

		TTwGraphicsGpuList *m_pGpuList;

		TGLBackendReadPresentedImageData *m_pReadPresentedImageDataFunc;

		SBackendCapabilities *m_pCapabilities;
		SGraphicsBackendDiagnostics *m_pDiagnostics;
		int *m_pInitError;

		const char **m_pErrStringPtr;

		char *m_pVendorString;
		char *m_pVersionString;
		char *m_pRendererString;

		int m_RequestedMajor;
		int m_RequestedMinor;
		int m_RequestedPatch;

		EBackendType m_RequestedBackend;

		int m_GlewMajor;
		int m_GlewMinor;
		int m_GlewPatch;
	};

	struct SCommand_Shutdown : public CCommandBuffer::SCommand
	{
		SCommand_Shutdown() :
			SCommand(CMD_SHUTDOWN) {}
	};

	struct SCommand_PostShutdown : public CCommandBuffer::SCommand
	{
		SCommand_PostShutdown() :
			SCommand(CMD_POST_SHUTDOWN) {}
	};
};

#endif
