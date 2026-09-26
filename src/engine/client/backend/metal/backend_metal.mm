#include "backend_metal.h"
#include "metal_frame_state.h"
#include "metal_render_target_state.h"
#include "metal_types.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/client/backend_sdl.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#if defined(CONF_VIDEORECORDER)
#include <engine/shared/video.h>
#endif
#include <engine/storage.h>

#include <SDL_metal.h>

#define pi qmclient_carbon_pi
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreGraphics/CoreGraphics.h>
#undef pi
#include <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr size_t gs_MetalGpuInfoStringSize = 256;
enum class EMetalBackendState
{
	UNINITIALIZED,
	PRE_INITIALIZED,
	INITIALIZED,
	SHUTDOWN,
};

const char *MetalBackendStateName(EMetalBackendState State)
{
	switch(State)
	{
	case EMetalBackendState::UNINITIALIZED: return "uninitialized";
	case EMetalBackendState::PRE_INITIALIZED: return "pre_initialized";
	case EMetalBackendState::INITIALIZED: return "initialized";
	case EMetalBackendState::SHUTDOWN: return "shutdown";
	}
	return "unknown";
}

class CCommandProcessorFragment_Metal final : public CCommandProcessorFragment_GLBase
{
	static constexpr size_t gs_FrameSlotCount = 3;
	static constexpr size_t gs_StreamBufferSize = 4 * 1024 * 1024;
	static constexpr size_t gs_TransientBufferPoolByteLimit = 8 * 1024 * 1024;

	struct STextureSlot
	{
		id<MTLTexture> m_Texture = nil;
		id<MTLTexture> m_TextureArray = nil;
		id<MTLBuffer> m_Staging = nil;
		SMetalTextureLayout m_Layout;
		SMetalTextureLayout m_ArrayLayout;
		size_t m_Width = 0;
		size_t m_Height = 0;
		size_t m_ArraySourceWidth = 0;
		size_t m_ArraySourceHeight = 0;
		size_t m_ArrayWidth = 0;
		size_t m_ArrayHeight = 0;
		EMetalTextureFormat m_Format = EMetalTextureFormat::RGBA8;
		bool m_Is2DArray = false;
		bool m_Allocated = false;
	};

	struct SBufferSlot
	{
		id<MTLBuffer> m_Buffer = nil;
		size_t m_DataBytes = 0;
		std::array<uint64_t, gs_FrameSlotCount> m_aLastUsedFrameIds{};
		bool m_Allocated = false;
		bool m_OneTimeUse = false;
	};

	struct STransientBuffer
	{
		id<MTLBuffer> m_Buffer = nil;
		size_t m_DataBytes = 0;
	};

	struct SBufferContainerSlot
	{
		int m_Stride = 0;
		int m_VertBufferBindingIndex = -1;
		std::vector<SBufferContainerInfo::SAttribute> m_vAttributes;
		bool m_Allocated = false;
	};

	struct SRenderTarget
	{
		id<MTLTexture> m_Texture = nil;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		size_t m_DataBytes = 0;
		bool m_Allocated = false;
	};

	struct SFrameSlot
	{
		id<MTLBuffer> m_VertexBuffer = nil;
		id<MTLCommandBuffer> m_CommandBuffer = nil;
		id<MTLTexture> m_BackbufferTexture = nil;
		id<MTLTexture> m_MultiSampleTexture = nil;
		uint32_t m_BackbufferWidth = 0;
		uint32_t m_BackbufferHeight = 0;
		size_t m_BackbufferDataBytes = 0;
		uint32_t m_MultiSampleTextureWidth = 0;
		uint32_t m_MultiSampleTextureHeight = 0;
		uint32_t m_MultiSampleTextureSampleCount = 0;
		size_t m_MultiSampleTextureDataBytes = 0;
		bool m_BackbufferHasContents = false;
		size_t m_VertexOffset = 0;
		uint64_t m_FrameId = 0;
	};

	using TPipelineStates = std::array<id<MTLRenderPipelineState>, 60>;
	using TSdfPipelineStates = std::array<id<MTLRenderPipelineState>, static_cast<size_t>(EMetalBlendMode::ADDITIVE) + 1>;

	EMetalBackendState m_State = EMetalBackendState::UNINITIALIZED;
	uint64_t m_FrameId = 0;
	SDL_MetalView m_MetalView = nullptr;
	void *m_pLayer = nullptr;
	SDL_Window *m_pWindow = nullptr;
	bool m_VSync = true;
	id<MTLDevice> m_Device = nil;
	id<MTLCommandQueue> m_CommandQueue = nil;
	id<MTLBuffer> m_QuadIndexBuffer = nil;
	size_t m_QuadIndexCount = 0;
	id<MTLLibrary> m_ShaderLibrary = nil;
	TPipelineStates m_aPipelineStates{};
	TPipelineStates m_aMultiSamplePipelineStates{};
	TSdfPipelineStates m_aMediaIslandSdfPipelines{};
	TSdfPipelineStates m_aRoundedRectSdfPipelines{};
	TSdfPipelineStates m_aMultiSampleMediaIslandSdfPipelines{};
	TSdfPipelineStates m_aMultiSampleRoundedRectSdfPipelines{};
	TSdfPipelineStates m_aTexturedMsdfPipelines{};
	TSdfPipelineStates m_aMultiSampleTexturedMsdfPipelines{};
	id<MTLRenderPipelineState> m_GaussianBlurPipeline = nil;
	id<MTLSamplerState> m_RepeatSampler = nil;
	id<MTLSamplerState> m_ClampSampler = nil;
	// 图块数组不依赖自动生成的 mip 级。部分 macOS/Metal 设备在数组纹理
	// 上传与异步 mip 生成交错时会短暂读到未初始化的高 mip 级，低 Zoom 下
	// 表现为整片黑屏，因此使用独立的非 mip sampler。
	id<MTLSamplerState> m_TileRepeatSampler = nil;
	id<MTLSamplerState> m_TileClampSampler = nil;
	std::array<SFrameSlot, gs_FrameSlotCount> m_aFrameSlots{};
	std::array<std::vector<STransientBuffer>, gs_FrameSlotCount> m_aTransientBuffers{};
	std::array<std::vector<STransientBuffer>, gs_FrameSlotCount> m_aRetiredTransientBuffers{};
	std::array<std::vector<STransientBuffer>, gs_FrameSlotCount> m_aRetiredBuffers{};
	std::array<size_t, gs_FrameSlotCount> m_aTransientBufferPoolBytes{};
	CMetalFrameState m_FrameState{gs_FrameSlotCount};
	size_t m_CurrentFrameSlot = 0;
	id<MTLCommandBuffer> m_CurrentCommandBuffer = nil;
	id<MTLRenderCommandEncoder> m_CurrentRenderEncoder = nil;
	id<MTLBlitCommandEncoder> m_CurrentBlitEncoder = nil;
	id<CAMetalDrawable> m_CurrentDrawable = nil;
	MTLScissorRect m_BoundScissorRect{};
	bool m_HasBoundScissorRect = false;
	id<MTLBuffer> m_LastPresentedReadback = nil;
	id<MTLCommandBuffer> m_LastPresentedCommandBuffer = nil;
	uint32_t m_LastPresentedReadbackWidth = 0;
	uint32_t m_LastPresentedReadbackHeight = 0;
	size_t m_LastPresentedReadbackRowBytes = 0;
	std::atomic_bool m_GpuFailure = false;
	std::atomic<uint64_t> m_GpuFailureFrameId = 0;
	std::atomic<int> m_GpuFailureCommandId = -1;
	std::atomic<int> m_GpuFailureStatus = 0;
	std::atomic<int> m_LastCommandId = -1;
	std::mutex m_GpuFailureMutex;
	std::string m_GpuFailureDescription;
	SBackendCapabilities *m_pCapabilities = nullptr;
	IStorage *m_pStorage = nullptr;
	bool m_CommandBufferCommitted = false;
	// 仅命令缓冲区容量切分（非 present）时，下一段才能继续加载当前 backbuffer。
	// present 尝试失败也会保留旧内容用于诊断，但不能把它当作下一逻辑帧的起点。
	bool m_BackbufferContinuationPending = false;
	bool m_RenderEncoderStarted = false;
	bool m_SkipCurrentFrame = false;
	uint32_t m_MultiSamplingCount = 0;
	CMetalRenderTargetState m_RenderTargetState;
	uint32_t m_DrawableWidth = 0;
	uint32_t m_DrawableHeight = 0;
	size_t m_StreamMemoryBytes = 0;
	size_t m_BufferMemoryBytes = 0;
	std::vector<STextureSlot> m_vTextureSlots;
	std::vector<SBufferSlot> m_vBufferSlots;
	std::vector<SBufferContainerSlot> m_vBufferContainers;
	std::vector<SRenderTarget> m_vRenderTargets;
	// 无 VSync 时最后一次成功 present 的时间，用于按刷新周期节流呈现。
	std::chrono::nanoseconds m_LastPresentTimeNs = std::chrono::nanoseconds::zero();
	// 无 VSync 时的呈现节奏锚点：每次 present 的目标呈现时刻（vsync 序列估计）。
	std::chrono::nanoseconds m_NextPresentTimeNs = std::chrono::nanoseconds::zero();
	// 主显示器实际刷新率（Hz），用于 present 节流；0 表示未知。
	int m_PresentRefreshRateHz = 0;
	bool m_MetalPerfEnabled = false;
	uint32_t m_MetalPerfFrameCount = 0;
	uint64_t m_MetalPerfCommandBufferCount = 0;
	uint64_t m_MetalPerfEstimatedRenderCalls = 0;
	uint64_t m_MetalPerfEncoderCount = 0;
	uint64_t m_MetalPerfRenderEncoderCount = 0;
	uint64_t m_MetalPerfBlitEncoderCount = 0;
	uint64_t m_MetalPerfUploadInterruptCount = 0;
	uint64_t m_MetalPerfClearCount = 0;
	uint64_t m_MetalPerfRenderTargetSwitchCount = 0;
	uint64_t m_MetalPerfFrameSlotWaitCount = 0;
	uint64_t m_MetalPerfDrawableAcquireCount = 0;
	uint64_t m_MetalPerfDrawableUnavailableCount = 0;
	uint64_t m_MetalPerfDrawableAcquireOver16MsCount = 0;
	uint64_t m_MetalPerfDrawableAcquireOver33MsCount = 0;
	uint64_t m_MetalPerfDrawableAcquireOver100MsCount = 0;
	uint64_t m_MetalPerfGpuCompletionWaitCount = 0;
	double m_MetalPerfFrameSlotWaitMs = 0.0;
	double m_MetalPerfDrawableAcquireMs = 0.0;
	double m_MetalPerfDrawableAcquireMaxMs = 0.0;
	double m_MetalPerfGpuCompletionWaitMs = 0.0;
	uint64_t m_MetalPerfUploadBytes = 0;
	uint64_t m_MetalPerfReadbackBytes = 0;
	uint64_t m_MetalPerfTextureCreateCount = 0;
	uint64_t m_MetalPerfTextureCreateBytes = 0;
	uint64_t m_MetalPerfBufferCreateCount = 0;
	uint64_t m_MetalPerfBufferCreateBytes = 0;
	uint64_t m_MetalPerfBufferReuseCount = 0;
	uint64_t m_MetalPerfBufferReuseBytes = 0;
	uint64_t m_MetalPerfTransientPoolReuseCount = 0;
	uint64_t m_MetalPerfTransientPoolReuseBytes = 0;
	double m_MetalPerfCommandProcessingMs = 0.0;
	double m_MetalPerfCommandProcessingMaxMs = 0.0;
	std::chrono::nanoseconds m_MetalPerfFrameStart = std::chrono::nanoseconds::zero();
	// Metal completion handler 不在图形线程执行，GPU timing 单独同步，避免与采样日志竞争。
	std::atomic<uint64_t> m_MetalPerfGeneration{0};
	std::mutex m_MetalPerfGpuTimingMutex;
	uint64_t m_MetalPerfGpuTimingGeneration = 0;
	uint64_t m_MetalPerfGpuExecutionCount = 0;
	uint64_t m_MetalPerfGpuExecutionUnavailableCount = 0;
	double m_MetalPerfGpuExecutionMs = 0.0;
	double m_MetalPerfGpuExecutionMaxMs = 0.0;
	int m_MetalPerfRequestedRenderThreads = 1;
	unsigned int m_RequiredIndicesNum = 0;
	std::atomic<uint64_t> *m_pTextureMemoryUsage = nullptr;
	std::atomic<uint64_t> *m_pBufferMemoryUsage = nullptr;
	std::atomic<uint64_t> *m_pStreamMemoryUsage = nullptr;
	std::atomic<uint64_t> *m_pStagingMemoryUsage = nullptr;
	char *m_pVendorString = nullptr;
	char *m_pVersionString = nullptr;
	char *m_pRendererString = nullptr;
	TTwGraphicsGpuList *m_pGpuList = nullptr;

	void ResetMetalPerfStats()
	{
		m_MetalPerfFrameCount = 0;
		m_MetalPerfCommandBufferCount = 0;
		m_MetalPerfEstimatedRenderCalls = 0;
		m_MetalPerfEncoderCount = 0;
		m_MetalPerfRenderEncoderCount = 0;
		m_MetalPerfBlitEncoderCount = 0;
		m_MetalPerfUploadInterruptCount = 0;
		m_MetalPerfClearCount = 0;
		m_MetalPerfRenderTargetSwitchCount = 0;
		m_MetalPerfFrameSlotWaitCount = 0;
		m_MetalPerfDrawableAcquireCount = 0;
		m_MetalPerfDrawableUnavailableCount = 0;
		m_MetalPerfDrawableAcquireOver16MsCount = 0;
		m_MetalPerfDrawableAcquireOver33MsCount = 0;
		m_MetalPerfDrawableAcquireOver100MsCount = 0;
		m_MetalPerfGpuCompletionWaitCount = 0;
		m_MetalPerfFrameSlotWaitMs = 0.0;
		m_MetalPerfDrawableAcquireMs = 0.0;
		m_MetalPerfDrawableAcquireMaxMs = 0.0;
		m_MetalPerfGpuCompletionWaitMs = 0.0;
		m_MetalPerfUploadBytes = 0;
		m_MetalPerfReadbackBytes = 0;
		m_MetalPerfTextureCreateCount = 0;
		m_MetalPerfTextureCreateBytes = 0;
		m_MetalPerfBufferCreateCount = 0;
		m_MetalPerfBufferCreateBytes = 0;
		m_MetalPerfBufferReuseCount = 0;
		m_MetalPerfBufferReuseBytes = 0;
		m_MetalPerfTransientPoolReuseCount = 0;
		m_MetalPerfTransientPoolReuseBytes = 0;
		m_MetalPerfCommandProcessingMs = 0.0;
		m_MetalPerfCommandProcessingMaxMs = 0.0;
		m_MetalPerfFrameStart = std::chrono::nanoseconds::zero();
	}

	void ClearMetalGpuTimingStats(uint64_t Generation)
	{
		std::lock_guard<std::mutex> Lock(m_MetalPerfGpuTimingMutex);
		m_MetalPerfGpuTimingGeneration = Generation;
		m_MetalPerfGpuExecutionCount = 0;
		m_MetalPerfGpuExecutionUnavailableCount = 0;
		m_MetalPerfGpuExecutionMs = 0.0;
		m_MetalPerfGpuExecutionMaxMs = 0.0;
	}

	void RecordMetalGpuExecutionTiming(id<MTLCommandBuffer> Buffer, uint64_t Generation)
	{
		if(m_MetalPerfGeneration.load(std::memory_order_acquire) != Generation)
			return;
		const CFTimeInterval GpuStartTime = Buffer.GPUStartTime;
		const CFTimeInterval GpuEndTime = Buffer.GPUEndTime;
		std::lock_guard<std::mutex> Lock(m_MetalPerfGpuTimingMutex);
		if(m_MetalPerfGpuTimingGeneration != Generation)
			return;
		if(GpuStartTime <= 0.0 || GpuEndTime < GpuStartTime)
		{
			++m_MetalPerfGpuExecutionUnavailableCount;
			return;
		}
		const double GpuExecutionMs = (GpuEndTime - GpuStartTime) * 1000.0;
		++m_MetalPerfGpuExecutionCount;
		m_MetalPerfGpuExecutionMs += GpuExecutionMs;
		m_MetalPerfGpuExecutionMaxMs = std::max(m_MetalPerfGpuExecutionMaxMs, GpuExecutionMs);
	}

	void TakeMetalGpuTimingStats(uint64_t &ExecutionCount, double &ExecutionMs, double &ExecutionMaxMs, uint64_t &UnavailableCount)
	{
		std::lock_guard<std::mutex> Lock(m_MetalPerfGpuTimingMutex);
		ExecutionCount = m_MetalPerfGpuExecutionCount;
		ExecutionMs = m_MetalPerfGpuExecutionMs;
		ExecutionMaxMs = m_MetalPerfGpuExecutionMaxMs;
		UnavailableCount = m_MetalPerfGpuExecutionUnavailableCount;
		m_MetalPerfGpuExecutionCount = 0;
		m_MetalPerfGpuExecutionUnavailableCount = 0;
		m_MetalPerfGpuExecutionMs = 0.0;
		m_MetalPerfGpuExecutionMaxMs = 0.0;
	}

	void MaybeLogMetalPerfStats()
	{
		if(!m_MetalPerfEnabled || m_MetalPerfFrameCount != 120)
			return;
		uint64_t GpuExecutionCount = 0;
		uint64_t GpuExecutionUnavailableCount = 0;
		double GpuExecutionMs = 0.0;
		double GpuExecutionMaxMs = 0.0;
		TakeMetalGpuTimingStats(GpuExecutionCount, GpuExecutionMs, GpuExecutionMaxMs, GpuExecutionUnavailableCount);
		CAMetalLayer *pLayer = m_pLayer != nullptr ? (__bridge CAMetalLayer *)m_pLayer : nil;
		int DisplaySync = 0;
#if defined(CONF_PLATFORM_MACOS)
		DisplaySync = pLayer != nil && pLayer.displaySyncEnabled ? 1 : 0;
#endif
		const int AllowsNextDrawableTimeout = pLayer != nil && pLayer.allowsNextDrawableTimeout ? 1 : 0;
		const unsigned long MaxDrawables = pLayer != nil ? static_cast<unsigned long>(pLayer.maximumDrawableCount) : 0;
		const int PresentsWithTransaction = pLayer != nil && pLayer.presentsWithTransaction ? 1 : 0;
		dbg_msg("perf/macos_metal", "event=frame_sample sample_frames=%u command_buffers=%llu render_calls=%llu encoders=%llu render_encoders=%llu blit_encoders=%llu upload_interrupts=%llu clears=%llu rt_switches=%llu command_processing_ms_sum=%.3f command_processing_ms_max=%.3f requested_render_threads=%d actual_render_threads=1 frame_slot_wait_count=%llu frame_slot_wait_ms_sum=%.3f drawable_acquire_count=%llu drawable_acquire_ms_sum=%.3f drawable_acquire_ms_max=%.3f drawable_acquire_over_16ms_count=%llu drawable_acquire_over_33ms_count=%llu drawable_acquire_over_100ms_count=%llu drawable_unavailable_count=%llu gpu_completion_wait_count=%llu gpu_completion_wait_ms_sum=%.3f gpu_execution_count=%llu gpu_execution_ms_sum=%.3f gpu_execution_ms_max=%.3f gpu_execution_unavailable_count=%llu upload_bytes=%llu readback_bytes=%llu texture_create_count=%llu texture_create_bytes=%llu buffer_create_count=%llu buffer_create_bytes=%llu buffer_reuse_count=%llu buffer_reuse_bytes=%llu transient_pool_reuse_count=%llu transient_pool_reuse_bytes=%llu display_sync=%d allows_next_drawable_timeout=%d max_drawables=%lu presents_with_transaction=%d",
			m_MetalPerfFrameCount, (unsigned long long)m_MetalPerfCommandBufferCount, (unsigned long long)m_MetalPerfEstimatedRenderCalls, (unsigned long long)m_MetalPerfEncoderCount, (unsigned long long)m_MetalPerfRenderEncoderCount, (unsigned long long)m_MetalPerfBlitEncoderCount, (unsigned long long)m_MetalPerfUploadInterruptCount, (unsigned long long)m_MetalPerfClearCount, (unsigned long long)m_MetalPerfRenderTargetSwitchCount, m_MetalPerfCommandProcessingMs, m_MetalPerfCommandProcessingMaxMs, m_MetalPerfRequestedRenderThreads, (unsigned long long)m_MetalPerfFrameSlotWaitCount, m_MetalPerfFrameSlotWaitMs, (unsigned long long)m_MetalPerfDrawableAcquireCount, m_MetalPerfDrawableAcquireMs, m_MetalPerfDrawableAcquireMaxMs, (unsigned long long)m_MetalPerfDrawableAcquireOver16MsCount, (unsigned long long)m_MetalPerfDrawableAcquireOver33MsCount, (unsigned long long)m_MetalPerfDrawableAcquireOver100MsCount, (unsigned long long)m_MetalPerfDrawableUnavailableCount, (unsigned long long)m_MetalPerfGpuCompletionWaitCount, m_MetalPerfGpuCompletionWaitMs, (unsigned long long)GpuExecutionCount, GpuExecutionMs, GpuExecutionMaxMs, (unsigned long long)GpuExecutionUnavailableCount, (unsigned long long)m_MetalPerfUploadBytes, (unsigned long long)m_MetalPerfReadbackBytes, (unsigned long long)m_MetalPerfTextureCreateCount, (unsigned long long)m_MetalPerfTextureCreateBytes, (unsigned long long)m_MetalPerfBufferCreateCount, (unsigned long long)m_MetalPerfBufferCreateBytes, (unsigned long long)m_MetalPerfBufferReuseCount, (unsigned long long)m_MetalPerfBufferReuseBytes, (unsigned long long)m_MetalPerfTransientPoolReuseCount, (unsigned long long)m_MetalPerfTransientPoolReuseBytes, DisplaySync, AllowsNextDrawableTimeout, MaxDrawables, PresentsWithTransaction);
		ResetMetalPerfStats();
	}

	void SetDevice(id<MTLDevice> Device)
	{
#if !__has_feature(objc_arc)
		[m_Device release];
		m_Device = [Device retain];
#else
		m_Device = Device;
#endif
	}

	void ReleaseDevice()
	{
#if !__has_feature(objc_arc)
		[m_Device release];
#endif
		m_Device = nil;
	}

	static void ReleaseMetalObject(id Object)
	{
#if !__has_feature(objc_arc)
		[Object release];
#else
		(void)Object;
#endif
	}

	static id RetainMetalObject(id Object)
	{
#if !__has_feature(objc_arc)
		return [Object retain];
#else
		return Object;
#endif
	}

	static bool ScissorRectsEqual(const MTLScissorRect &Left, const MTLScissorRect &Right)
	{
		return Left.x == Right.x && Left.y == Right.y && Left.width == Right.width && Left.height == Right.height;
	}

	void ReleaseCommandQueue()
	{
		ReleaseMetalObject(m_CommandQueue);
		m_CommandQueue = nil;
	}

	static size_t PipelineIndex(bool Textured, bool Text, EMetalBlendMode BlendMode)
	{
		return (Text ? 9 : (Textured ? 3 : 0)) + static_cast<size_t>(BlendMode);
	}

	static size_t TilePipelineIndex(bool Textured, bool Border, EMetalBlendMode BlendMode)
	{
		return 18 + (static_cast<size_t>(Border) * 2 + static_cast<size_t>(Textured)) * 3 + static_cast<size_t>(BlendMode);
	}

	static size_t QuadPipelineIndex(bool Textured, bool Grouped, EMetalBlendMode BlendMode)
	{
		return 30 + (static_cast<size_t>(Grouped) * 2 + static_cast<size_t>(Textured)) * 3 + static_cast<size_t>(BlendMode);
	}

	static size_t QuadContainerExPipelineIndex(bool Textured, EMetalBlendMode BlendMode)
	{
		return 42 + static_cast<size_t>(Textured) * 3 + static_cast<size_t>(BlendMode);
	}

	static size_t SpriteMultiplePipelineIndex(bool Textured, EMetalBlendMode BlendMode)
	{
		return 48 + static_cast<size_t>(Textured) * 3 + static_cast<size_t>(BlendMode);
	}

	static size_t TextureArrayPipelineIndex(EMetalBlendMode BlendMode)
	{
		return 54 + static_cast<size_t>(BlendMode);
	}

	static bool MetalArraySourceDimensions(size_t SourceWidth, size_t SourceHeight, size_t &ArraySourceWidth, size_t &ArraySourceHeight)
	{
		if(SourceWidth == 0 || SourceHeight == 0 || SourceWidth > static_cast<size_t>(std::numeric_limits<int>::max()) || SourceHeight > static_cast<size_t>(std::numeric_limits<int>::max()))
			return false;
		ArraySourceWidth = SourceWidth;
		ArraySourceHeight = SourceHeight;
		if(ArraySourceWidth % 16 == 0 && ArraySourceHeight % 16 == 0)
			return true;
		// 图集必须按 16x16 tile 切片。向上补齐而不是向下取幂，避免
		// 非整除尺寸的尾部 tile 被裁掉后在低 Zoom 时显示为黑块。
		if(SourceWidth > std::numeric_limits<size_t>::max() - 15 || SourceHeight > std::numeric_limits<size_t>::max() - 15)
			return false;
		ArraySourceWidth = ((SourceWidth + 15) / 16) * 16;
		ArraySourceHeight = ((SourceHeight + 15) / 16) * 16;
		if(ArraySourceWidth > static_cast<size_t>(std::numeric_limits<int>::max()) || ArraySourceHeight > static_cast<size_t>(std::numeric_limits<int>::max()))
			return false;
		return ArraySourceWidth % 16 == 0 && ArraySourceHeight % 16 == 0;
	}

	id<MTLRenderPipelineState> CurrentPipeline(size_t Index) const
	{
		const TPipelineStates &Pipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aPipelineStates : m_aMultiSamplePipelineStates;
		return Index < Pipelines.size() ? Pipelines[Index] : nil;
	}

	uint32_t SupportedMultiSamplingCount(uint32_t RequestedCount) const
	{
		if(m_Device == nil)
			return 0;
		return MetalSelectSampleCount(RequestedCount, [m_Device supportsTextureSampleCount:2], [m_Device supportsTextureSampleCount:4], [m_Device supportsTextureSampleCount:8]);
	}

	static MTLBlendFactor MetalBlendFactor(EMetalBlendFactor Factor)
	{
		switch(Factor)
		{
		case EMetalBlendFactor::ZERO: return MTLBlendFactorZero;
		case EMetalBlendFactor::ONE: return MTLBlendFactorOne;
		case EMetalBlendFactor::SOURCE_ALPHA: return MTLBlendFactorSourceAlpha;
		case EMetalBlendFactor::ONE_MINUS_SOURCE_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
		}
		return MTLBlendFactorOne;
	}

	void ReleaseFrameSlotResources(size_t Slot)
	{
		if(Slot >= m_aFrameSlots.size())
			return;
		SFrameSlot &Frame = m_aFrameSlots[Slot];
		const CMetalFrameState::ESlotState State = m_FrameState.SlotState(Slot);
		if(State == CMetalFrameState::ESlotState::IN_FLIGHT)
			return;
		ReleaseMetalObject(Frame.m_CommandBuffer);
		Frame.m_CommandBuffer = nil;
		Frame.m_VertexOffset = 0;
		Frame.m_FrameId = 0;
		for(STransientBuffer &Buffer : m_aRetiredTransientBuffers[Slot])
			RecycleTransientBuffer(Slot, Buffer);
		m_aRetiredTransientBuffers[Slot].clear();
		for(STransientBuffer &Buffer : m_aRetiredBuffers[Slot])
		{
			SubBufferMemory(Buffer.m_DataBytes);
			ReleaseMetalObject(Buffer.m_Buffer);
		}
		m_aRetiredBuffers[Slot].clear();
	}

	void RecycleTransientBuffer(size_t Slot, STransientBuffer Buffer)
	{
		if(Slot >= gs_FrameSlotCount || Buffer.m_Buffer == nil)
			return;
		if(Buffer.m_DataBytes <= gs_TransientBufferPoolByteLimit - m_aTransientBufferPoolBytes[Slot])
		{
			m_aTransientBuffers[Slot].push_back(Buffer);
			m_aTransientBufferPoolBytes[Slot] += Buffer.m_DataBytes;
			return;
		}
		SubBufferMemory(Buffer.m_DataBytes);
		ReleaseMetalObject(Buffer.m_Buffer);
	}

	void ReleaseTransientBufferPools()
	{
		for(size_t Slot = 0; Slot < gs_FrameSlotCount; ++Slot)
		{
			for(STransientBuffer &Buffer : m_aTransientBuffers[Slot])
			{
				SubBufferMemory(Buffer.m_DataBytes);
				ReleaseMetalObject(Buffer.m_Buffer);
			}
			for(STransientBuffer &Buffer : m_aRetiredTransientBuffers[Slot])
			{
				SubBufferMemory(Buffer.m_DataBytes);
				ReleaseMetalObject(Buffer.m_Buffer);
			}
			for(STransientBuffer &Buffer : m_aRetiredBuffers[Slot])
			{
				SubBufferMemory(Buffer.m_DataBytes);
				ReleaseMetalObject(Buffer.m_Buffer);
			}
			m_aTransientBuffers[Slot].clear();
			m_aRetiredTransientBuffers[Slot].clear();
			m_aRetiredBuffers[Slot].clear();
			m_aTransientBufferPoolBytes[Slot] = 0;
		}
	}

	template<size_t Size>
	static void ReleasePipelineStates(std::array<id<MTLRenderPipelineState>, Size> &Pipelines)
	{
		for(id<MTLRenderPipelineState> Pipeline : Pipelines)
			ReleaseMetalObject(Pipeline);
		Pipelines.fill(nil);
	}

	void DestroyMultiSampleTexture(size_t Slot)
	{
		if(Slot >= m_aFrameSlots.size())
			return;
		SFrameSlot &Frame = m_aFrameSlots[Slot];
		if(Frame.m_MultiSampleTexture != nil)
		{
			SubTextureMemory(Frame.m_MultiSampleTextureDataBytes);
			ReleaseMetalObject(Frame.m_MultiSampleTexture);
		}
		Frame.m_MultiSampleTexture = nil;
		Frame.m_MultiSampleTextureWidth = 0;
		Frame.m_MultiSampleTextureHeight = 0;
		Frame.m_MultiSampleTextureSampleCount = 0;
		Frame.m_MultiSampleTextureDataBytes = 0;
	}

	void DestroyAllMultiSampleTextures()
	{
		for(size_t Slot = 0; Slot < m_aFrameSlots.size(); ++Slot)
			DestroyMultiSampleTexture(Slot);
	}

	void DestroyBackbufferTexture(size_t Slot)
	{
		if(Slot >= m_aFrameSlots.size())
			return;
		SFrameSlot &Frame = m_aFrameSlots[Slot];
		if(Frame.m_BackbufferTexture != nil)
		{
			SubTextureMemory(Frame.m_BackbufferDataBytes);
			ReleaseMetalObject(Frame.m_BackbufferTexture);
		}
		Frame.m_BackbufferTexture = nil;
		Frame.m_BackbufferWidth = 0;
		Frame.m_BackbufferHeight = 0;
		Frame.m_BackbufferDataBytes = 0;
		Frame.m_BackbufferHasContents = false;
	}

	bool EnsureBackbufferTexture(uint32_t Width, uint32_t Height)
	{
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		if(Frame.m_BackbufferTexture != nil && Frame.m_BackbufferWidth == Width && Frame.m_BackbufferHeight == Height)
			return true;
		DestroyBackbufferTexture(m_CurrentFrameSlot);
		if(m_Device == nil || Width == 0 || Height == 0)
			return false;
		size_t PixelCount = 0;
		size_t DataBytes = 0;
		if(!MetalCheckedMul(Width, Height, PixelCount) || !MetalCheckedMul(PixelCount, 4, DataBytes))
			return false;
		MTLTextureDescriptor *pDescriptor = [[MTLTextureDescriptor alloc] init];
		pDescriptor.textureType = MTLTextureType2D;
		pDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
		pDescriptor.width = Width;
		pDescriptor.height = Height;
		pDescriptor.mipmapLevelCount = 1;
		pDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		pDescriptor.storageMode = MTLStorageModePrivate;
		Frame.m_BackbufferTexture = [m_Device newTextureWithDescriptor:pDescriptor];
#if !__has_feature(objc_arc)
		[pDescriptor release];
#endif
		if(Frame.m_BackbufferTexture == nil)
			return false;
		Frame.m_BackbufferWidth = Width;
		Frame.m_BackbufferHeight = Height;
		Frame.m_BackbufferDataBytes = DataBytes;
		AddTextureMemory(DataBytes);
		return true;
	}

	bool HasValidBackbufferContents() const
	{
		const SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		return Frame.m_BackbufferHasContents && Frame.m_BackbufferTexture != nil && Frame.m_BackbufferWidth == m_DrawableWidth && Frame.m_BackbufferHeight == m_DrawableHeight && m_DrawableWidth > 0 && m_DrawableHeight > 0;
	}

	id<MTLTexture> CurrentBackbufferTexture() const
	{
		return m_aFrameSlots[m_CurrentFrameSlot].m_BackbufferTexture;
	}

	bool CurrentBackbufferHasContents() const
	{
		return m_aFrameSlots[m_CurrentFrameSlot].m_BackbufferHasContents;
	}

	void SetCurrentBackbufferHasContents(bool HasContents)
	{
		m_aFrameSlots[m_CurrentFrameSlot].m_BackbufferHasContents = HasContents;
	}

	bool EnsureMultiSampleTexture(uint32_t Width, uint32_t Height)
	{
		if(m_MultiSamplingCount == 0)
			return false;
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		if(Frame.m_MultiSampleTexture != nil && Frame.m_MultiSampleTextureWidth == Width && Frame.m_MultiSampleTextureHeight == Height && Frame.m_MultiSampleTextureSampleCount == m_MultiSamplingCount)
			return true;
		// MSAA attachment 重建后内容未定义，下一次 render pass 必须清屏。
		SetCurrentBackbufferHasContents(false);
		DestroyMultiSampleTexture(m_CurrentFrameSlot);
		if(m_Device == nil || Width == 0 || Height == 0)
			return false;
		size_t PixelCount = 0;
		size_t DataBytes = 0;
		if(!MetalCheckedMul(Width, Height, PixelCount) || !MetalCheckedMul(PixelCount, 4, DataBytes) || !MetalCheckedMul(DataBytes, m_MultiSamplingCount, DataBytes))
			return false;
		MTLTextureDescriptor *pDescriptor = [[MTLTextureDescriptor alloc] init];
		pDescriptor.textureType = MTLTextureType2DMultisample;
		pDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
		pDescriptor.width = Width;
		pDescriptor.height = Height;
		pDescriptor.sampleCount = m_MultiSamplingCount;
		pDescriptor.mipmapLevelCount = 1;
		pDescriptor.usage = MTLTextureUsageRenderTarget;
		pDescriptor.storageMode = MTLStorageModePrivate;
		Frame.m_MultiSampleTexture = [m_Device newTextureWithDescriptor:pDescriptor];
#if !__has_feature(objc_arc)
		[pDescriptor release];
#endif
		if(Frame.m_MultiSampleTexture == nil)
			return false;
		Frame.m_MultiSampleTextureWidth = Width;
		Frame.m_MultiSampleTextureHeight = Height;
		Frame.m_MultiSampleTextureSampleCount = m_MultiSamplingCount;
		Frame.m_MultiSampleTextureDataBytes = DataBytes;
		AddTextureMemory(DataBytes);
		return true;
	}

	void ReleaseGpuObjects()
	{
		WaitForGpuIdle();
		EndActiveEncoders();
		DestroyAllRenderTargets();
		DestroyAllMultiSampleTextures();
		for(size_t Slot = 0; Slot < m_aFrameSlots.size(); ++Slot)
			DestroyBackbufferTexture(Slot);
		ReleaseMetalObject(m_CurrentDrawable);
		m_CurrentDrawable = nil;
		m_FrameState.ClearReadbackPresented();
		for(SFrameSlot &Frame : m_aFrameSlots)
			Frame.m_BackbufferHasContents = false;
		m_RenderTargetState.Reset();
		ReleaseMetalObject(m_LastPresentedReadback);
		m_LastPresentedReadback = nil;
		ReleaseMetalObject(m_LastPresentedCommandBuffer);
		m_LastPresentedCommandBuffer = nil;
		m_LastPresentedReadbackWidth = 0;
		m_LastPresentedReadbackHeight = 0;
		m_LastPresentedReadbackRowBytes = 0;
		m_CurrentCommandBuffer = nil;
		m_BackbufferContinuationPending = false;
		ReleasePipelineStates(m_aPipelineStates);
		ReleasePipelineStates(m_aMultiSamplePipelineStates);
		ReleasePipelineStates(m_aMediaIslandSdfPipelines);
		ReleasePipelineStates(m_aRoundedRectSdfPipelines);
		ReleasePipelineStates(m_aMultiSampleMediaIslandSdfPipelines);
		ReleasePipelineStates(m_aMultiSampleRoundedRectSdfPipelines);
		ReleasePipelineStates(m_aTexturedMsdfPipelines);
		ReleasePipelineStates(m_aMultiSampleTexturedMsdfPipelines);
		ReleaseMetalObject(m_GaussianBlurPipeline);
		ReleaseMetalObject(m_RepeatSampler);
		ReleaseMetalObject(m_ClampSampler);
		ReleaseMetalObject(m_TileRepeatSampler);
		ReleaseMetalObject(m_TileClampSampler);
		ReleaseMetalObject(m_ShaderLibrary);
		ReleaseMetalObject(m_QuadIndexBuffer);
		m_QuadIndexBuffer = nil;
		m_QuadIndexCount = 0;
		m_RepeatSampler = nil;
		m_ClampSampler = nil;
		m_TileRepeatSampler = nil;
		m_TileClampSampler = nil;
		m_ShaderLibrary = nil;
		m_GaussianBlurPipeline = nil;
		for(size_t Slot = 0; Slot < m_aFrameSlots.size(); ++Slot)
		{
			ReleaseMetalObject(m_aFrameSlots[Slot].m_CommandBuffer);
			ReleaseMetalObject(m_aFrameSlots[Slot].m_VertexBuffer);
			m_aFrameSlots[Slot] = {};
		}
		if(m_StreamMemoryBytes != 0)
		{
			SubStreamMemory(m_StreamMemoryBytes);
			m_StreamMemoryBytes = 0;
		}
		if(m_BufferMemoryBytes != 0)
		{
			SubBufferMemory(m_BufferMemoryBytes);
			m_BufferMemoryBytes = 0;
		}
		ReleaseCommandQueue();
	}

	void WaitForGpuIdle()
	{
		if(m_CurrentCommandBuffer != nil && !m_CommandBufferCommitted)
			CommitCurrentFrame(false, true);
		for(SFrameSlot &Frame : m_aFrameSlots)
		{
			if(Frame.m_CommandBuffer != nil)
				[Frame.m_CommandBuffer waitUntilCompleted];
		}
		// 已提交但未 present 的分片可能仍持有旧 layer drawable。
		// resize 或重初始化边界不能继续复用该 drawable。
		ReleaseMetalObject(m_CurrentDrawable);
		m_CurrentDrawable = nil;
		for(SFrameSlot &Frame : m_aFrameSlots)
			Frame.m_BackbufferHasContents = false;
		m_FrameState.DrainFrames();
	}

	void RecordGpuFailure(id<MTLCommandBuffer> Buffer, uint64_t FrameId)
	{
		std::string Description = "Metal command buffer failed";
		NSError *pError = Buffer.error;
		if(pError != nil)
		{
			const char *pDomain = pError.domain.UTF8String;
			const char *pLocalizedDescription = pError.localizedDescription.UTF8String;
			Description += " (domain=";
			Description += pDomain != nullptr ? pDomain : "unknown";
			Description += ", code=" + std::to_string(static_cast<long long>(pError.code));
			if(pLocalizedDescription != nullptr)
			{
				Description += ", description=";
				Description += pLocalizedDescription;
			}
			Description += ")";
		}
		{
			std::lock_guard<std::mutex> Lock(m_GpuFailureMutex);
			m_GpuFailureDescription = std::move(Description);
		}
		m_GpuFailureFrameId.store(FrameId, std::memory_order_relaxed);
		m_GpuFailureCommandId.store(m_LastCommandId.load(std::memory_order_relaxed), std::memory_order_relaxed);
		m_GpuFailureStatus.store(static_cast<int>(Buffer.status), std::memory_order_relaxed);
		m_GpuFailure.store(true, std::memory_order_release);
	}

	bool SetGpuFailureError()
	{
		if(!m_GpuFailure.load(std::memory_order_acquire))
			return false;
		const uint64_t FrameId = m_GpuFailureFrameId.load(std::memory_order_relaxed);
		const int CommandId = m_GpuFailureCommandId.load(std::memory_order_relaxed);
		const int Status = m_GpuFailureStatus.load(std::memory_order_relaxed);
		std::string Description;
		{
			std::lock_guard<std::mutex> Lock(m_GpuFailureMutex);
			Description = m_GpuFailureDescription;
		}
		m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
		m_Error.m_vErrors.emplace_back(SGfxErrorContainer::SError{
			false,
			"Metal GPU command buffer failure (stage=completion, frame_id=" + std::to_string(FrameId) + ", command_id=" + std::to_string(CommandId) + ", status=" + std::to_string(Status) + "): " + Description});
		return true;
	}

	void AddTextureMemory(size_t Bytes)
	{
		if(m_pTextureMemoryUsage != nullptr)
			m_pTextureMemoryUsage->fetch_add(Bytes, std::memory_order_relaxed);
	}

	void SubTextureMemory(size_t Bytes)
	{
		if(m_pTextureMemoryUsage != nullptr)
			m_pTextureMemoryUsage->fetch_sub(Bytes, std::memory_order_relaxed);
	}

	void AddBufferMemory(size_t Bytes)
	{
		if(m_pBufferMemoryUsage != nullptr)
			m_pBufferMemoryUsage->fetch_add(Bytes, std::memory_order_relaxed);
	}

	void SubBufferMemory(size_t Bytes)
	{
		if(m_pBufferMemoryUsage != nullptr)
			m_pBufferMemoryUsage->fetch_sub(Bytes, std::memory_order_relaxed);
	}

	bool EnsureBufferSlot(int Slot)
	{
		if(Slot < 0)
			return false;
		if(static_cast<size_t>(Slot) >= m_vBufferSlots.size())
			m_vBufferSlots.resize(static_cast<size_t>(Slot) + 1);
		return true;
	}

	uint8_t BufferInFlightMask(const SBufferSlot &Buffer, bool IncludeCurrentRecording) const
	{
		if(m_State != EMetalBackendState::INITIALIZED || m_CurrentCommandBuffer == nil)
			return 0;
		uint8_t Mask = 0;
		for(size_t Slot = 0; Slot < gs_FrameSlotCount; ++Slot)
		{
			const uint64_t FrameId = m_aFrameSlots[Slot].m_FrameId;
			if(FrameId == 0 || Buffer.m_aLastUsedFrameIds[Slot] != FrameId || m_FrameState.SlotState(Slot) != CMetalFrameState::ESlotState::IN_FLIGHT)
				continue;
			if(!IncludeCurrentRecording && Slot == m_CurrentFrameSlot && !m_CommandBufferCommitted)
				continue;
			Mask |= static_cast<uint8_t>(1u << Slot);
		}
		return Mask;
	}

	bool IsBufferInFlight(const SBufferSlot &Buffer) const
	{
		return BufferInFlightMask(Buffer, false) != 0;
	}

	size_t PreferredBufferSlot(const SBufferSlot &Buffer) const
	{
		size_t PreferredSlot = m_CurrentFrameSlot;
		uint64_t PreferredFrameId = 0;
		for(size_t Slot = 0; Slot < gs_FrameSlotCount; ++Slot)
		{
			const uint64_t FrameId = Buffer.m_aLastUsedFrameIds[Slot];
			if(FrameId != 0 && FrameId == m_aFrameSlots[Slot].m_FrameId && FrameId > PreferredFrameId)
			{
				PreferredFrameId = FrameId;
				PreferredSlot = Slot;
			}
		}
		return PreferredSlot;
	}

	bool WaitForBufferIdle(SBufferSlot &Buffer)
	{
		const uint8_t Mask = BufferInFlightMask(Buffer, false);
		bool Success = true;
		for(size_t Slot = 0; Slot < gs_FrameSlotCount; ++Slot)
		{
			if((Mask & (1u << Slot)) == 0)
				continue;
			SFrameSlot &Frame = m_aFrameSlots[Slot];
			if(Frame.m_CommandBuffer == nil)
				return false;
			[Frame.m_CommandBuffer waitUntilCompleted];
			const bool FrameSuccess = Frame.m_CommandBuffer.status == MTLCommandBufferStatusCompleted;
			Success = Success && FrameSuccess;
			m_FrameState.CompleteFrame({Frame.m_FrameId, Slot}, FrameSuccess);
			ReleaseFrameSlotResources(Slot);
		}
		return Success;
	}

	void DestroyBuffer(int Slot)
	{
		if(Slot < 0 || static_cast<size_t>(Slot) >= m_vBufferSlots.size())
			return;
		SBufferSlot &Buffer = m_vBufferSlots[Slot];
		if(!Buffer.m_Allocated)
			return;
		const uint8_t CommittedMask = BufferInFlightMask(Buffer, false);
		const bool CurrentRecordingUse = Buffer.m_Buffer != nil &&
			(BufferInFlightMask(Buffer, true) & (1u << m_CurrentFrameSlot)) != 0 && !m_CommandBufferCommitted;
		if(Buffer.m_Buffer != nil && CommittedMask != 0)
		{
			// 同一 buffer 可能被多个已提交帧槽引用，先等待所有 committed
			// 用户；当前尚未提交的录制引用仍需延迟到当前槽完成。
			if(!WaitForBufferIdle(Buffer))
				WaitForGpuIdle();
		}
		bool DeferredRelease = false;
		if(Buffer.m_Buffer != nil && CurrentRecordingUse)
		{
			if(Buffer.m_OneTimeUse)
				m_aRetiredTransientBuffers[m_CurrentFrameSlot].push_back({Buffer.m_Buffer, Buffer.m_DataBytes});
			else
				m_aRetiredBuffers[m_CurrentFrameSlot].push_back({Buffer.m_Buffer, Buffer.m_DataBytes});
			DeferredRelease = true;
		}
		if(DeferredRelease)
		{
			// 所有权已经转移到对应 frame slot 的 deferred 队列；此处不能
			// 再 release 或扣减内存，否则队列回收时会使用悬空对象。
		}
		else if(Buffer.m_Buffer != nil && Buffer.m_OneTimeUse)
		{
			// 即使尚未绑定到 encoder，也必须按当前 command buffer 所属 slot 延迟回收；
			// 这样从池中取出后立刻删除不会重复扣减已计入的 buffer 内存。
			const size_t LastUsedSlot = PreferredBufferSlot(Buffer);
			// one-time buffer 必须等最后一次绑定它的 command buffer 完成后才能复用。
			RecycleTransientBuffer(LastUsedSlot, {Buffer.m_Buffer, Buffer.m_DataBytes});
		}
		else
		{
			SubBufferMemory(Buffer.m_DataBytes);
			ReleaseMetalObject(Buffer.m_Buffer);
		}
		Buffer = {};
	}

	void DestroyAllBuffers()
	{
		for(size_t Slot = 0; Slot < m_vBufferSlots.size(); ++Slot)
			DestroyBuffer(static_cast<int>(Slot));
		m_vBufferSlots.clear();
		ReleaseTransientBufferPools();
	}

	bool EnsureBufferContainerSlot(int Slot)
	{
		if(Slot < 0)
			return false;
		if(static_cast<size_t>(Slot) >= m_vBufferContainers.size())
			m_vBufferContainers.resize(static_cast<size_t>(Slot) + 1);
		return true;
	}

	void DestroyBufferContainer(int Slot, bool DestroyAllBO)
	{
		if(Slot < 0 || static_cast<size_t>(Slot) >= m_vBufferContainers.size())
			return;
		SBufferContainerSlot &Container = m_vBufferContainers[Slot];
		if(!Container.m_Allocated)
			return;
		if(DestroyAllBO && Container.m_VertBufferBindingIndex >= 0)
			DestroyBuffer(Container.m_VertBufferBindingIndex);
		Container = {};
	}

	void DestroyAllBufferContainers()
	{
		for(size_t Slot = 0; Slot < m_vBufferContainers.size(); ++Slot)
			DestroyBufferContainer(static_cast<int>(Slot), false);
		m_vBufferContainers.clear();
		m_RequiredIndicesNum = 0;
	}

	bool UpdateBufferContainer(int Slot, int Stride, int VertBufferBindingIndex, size_t AttributeCount, const SBufferContainerInfo::SAttribute *pAttributes, bool ReplaceExisting)
	{
		if(!EnsureBufferContainerSlot(Slot) || (AttributeCount > 0 && pAttributes == nullptr))
			return false;
		if(Stride < 0)
			return false;
		if(Stride == 0)
		{
			size_t TightStride = 0;
			for(size_t Index = 0; Index < AttributeCount; ++Index)
			{
				const SBufferContainerInfo::SAttribute &Attribute = pAttributes[Index];
				const size_t TypeBytes = MetalVertexDataTypeBytes(Attribute.m_Type);
				const size_t DataTypeCount = Attribute.m_DataTypeCount > 0 ? static_cast<size_t>(Attribute.m_DataTypeCount) : 0;
				if(DataTypeCount == 0 || TypeBytes == 0 || DataTypeCount > std::numeric_limits<size_t>::max() / TypeBytes)
					return false;
				const size_t AttributeBytes = DataTypeCount * TypeBytes;
				const size_t AttributeOffset = reinterpret_cast<uintptr_t>(Attribute.m_pOffset);
				if(AttributeOffset > std::numeric_limits<size_t>::max() - AttributeBytes)
					return false;
				TightStride = std::max(TightStride, AttributeOffset + AttributeBytes);
			}
			if(TightStride == 0 || TightStride > static_cast<size_t>(std::numeric_limits<int>::max()))
				return false;
			Stride = static_cast<int>(TightStride);
		}
		SBufferContainerSlot &Container = m_vBufferContainers[Slot];
		for(size_t Index = 0; Index < AttributeCount; ++Index)
		{
			const SBufferContainerInfo::SAttribute &Attribute = pAttributes[Index];
			if(Attribute.m_DataTypeCount <= 0)
				return false;
			const SMetalVertexAttribute MetalAttribute{static_cast<uint32_t>(Attribute.m_DataTypeCount), Attribute.m_Type, Attribute.m_Normalized, reinterpret_cast<uintptr_t>(Attribute.m_pOffset), Attribute.m_FuncType};
			if(!MetalValidateVertexAttribute(Stride, MetalAttribute))
				return false;
		}
		if(ReplaceExisting && Container.m_Allocated)
			DestroyBufferContainer(Slot, true);
		Container.m_Stride = Stride;
		Container.m_VertBufferBindingIndex = VertBufferBindingIndex;
		Container.m_vAttributes.clear();
		if(AttributeCount > 0)
			Container.m_vAttributes.assign(pAttributes, pAttributes + AttributeCount);
		Container.m_Allocated = true;
		return true;
	}

	void EndRenderEncoderForBlit()
	{
		if(m_CurrentRenderEncoder == nil)
			return;
		[m_CurrentRenderEncoder endEncoding];
		ReleaseMetalObject(m_CurrentRenderEncoder);
		m_CurrentRenderEncoder = nil;
		m_RenderEncoderStarted = false;
	}

	void DestroyRenderTarget(int TargetId)
	{
		if(TargetId < 0 || static_cast<size_t>(TargetId) >= m_vRenderTargets.size())
			return;
		if(!m_RenderTargetState.CanDestroy(TargetId))
			return;
		SRenderTarget &Target = m_vRenderTargets[TargetId];
		if(Target.m_Allocated)
		{
			SubTextureMemory(Target.m_DataBytes);
			ReleaseMetalObject(Target.m_Texture);
		}
		Target = {};
	}

	bool CreateRenderTarget(int TargetId, int Width, int Height)
	{
		if(TargetId < 0 || Width <= 0 || Height <= 0 || m_Device == nil || m_State != EMetalBackendState::INITIALIZED)
			return false;
		if(static_cast<size_t>(TargetId) >= m_vRenderTargets.size())
			m_vRenderTargets.resize(static_cast<size_t>(TargetId) + 1);
		if(!m_RenderTargetState.CanDestroy(TargetId))
			return false;
		DestroyRenderTarget(TargetId);
		if(static_cast<size_t>(Width) > std::numeric_limits<size_t>::max() / static_cast<size_t>(Height))
			return false;
		const size_t PixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);
		if(PixelCount > std::numeric_limits<size_t>::max() / 4)
			return false;
		MTLTextureDescriptor *pDescriptor = [[MTLTextureDescriptor alloc] init];
		pDescriptor.textureType = MTLTextureType2D;
		pDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
		pDescriptor.width = static_cast<NSUInteger>(Width);
		pDescriptor.height = static_cast<NSUInteger>(Height);
		pDescriptor.mipmapLevelCount = 1;
		pDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		pDescriptor.storageMode = MTLStorageModePrivate;
		id<MTLTexture> Texture = [m_Device newTextureWithDescriptor:pDescriptor];
#if !__has_feature(objc_arc)
		[pDescriptor release];
#endif
		if(Texture == nil)
			return false;
		SRenderTarget &Target = m_vRenderTargets[TargetId];
		Target.m_Texture = Texture;
		Target.m_Width = static_cast<uint32_t>(Width);
		Target.m_Height = static_cast<uint32_t>(Height);
		Target.m_DataBytes = PixelCount * 4;
		Target.m_Allocated = true;
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfTextureCreateCount;
			m_MetalPerfTextureCreateBytes += Target.m_DataBytes;
		}
		AddTextureMemory(Target.m_DataBytes);
		return true;
	}

	void DestroyAllRenderTargets()
	{
		if(m_RenderTargetState.IsActive())
		{
			EndActiveEncoders();
			m_RenderTargetState.Reset();
		}
		for(size_t TargetId = 0; TargetId < m_vRenderTargets.size(); ++TargetId)
			DestroyRenderTarget(static_cast<int>(TargetId));
		m_vRenderTargets.clear();
	}

	// 小于该尺寸的持久 buffer 使用 Shared 存储：CPU 直写 + didModifyRange 更新，
	// 避免每次更新都结束 render encoder 并插入 blit encoder，消除服务器列表等
	// UI 场景每帧数百次 encoder 打断。Apple Silicon 统一内存下 Shared 读性能接近 Private。
	static constexpr size_t QM_METAL_MAX_SHARED_BUFFER_BYTES = 256 * 1024;

	bool CopyIntoBuffer(id<MTLBuffer> Destination, size_t DestinationOffset, const void *pData, size_t DataBytes)
	{
		if(Destination == nil || m_CurrentCommandBuffer == nil || DataBytes == 0)
			return false;
		if(Destination.storageMode == MTLStorageModeShared)
		{
			// Shared buffer 直接写内存并通知 GPU，不需要 blit，不打断 render encoder。
			if(pData != nullptr)
				mem_copy(static_cast<uint8_t *>(Destination.contents) + DestinationOffset, pData, DataBytes);
			else
				std::memset(static_cast<uint8_t *>(Destination.contents) + DestinationOffset, 0, DataBytes);
	#if defined(CONF_PLATFORM_MACOS)
			[Destination didModifyRange:NSMakeRange(DestinationOffset, DataBytes)];
	#endif
			if(m_MetalPerfEnabled)
				m_MetalPerfUploadBytes += DataBytes;
			return true;
		}
		id<MTLBuffer> Staging = [m_Device newBufferWithLength:DataBytes options:MTLResourceStorageModeShared];
		if(Staging == nil)
			return false;
		if(m_MetalPerfEnabled)
			m_MetalPerfUploadBytes += DataBytes;
		if(pData != nullptr)
			mem_copy(Staging.contents, pData, DataBytes);
		else
			std::memset(Staging.contents, 0, DataBytes);
		EndRenderEncoderForBlit();
		if(m_CurrentBlitEncoder == nil)
		{
			m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
			if(m_MetalPerfEnabled && m_CurrentBlitEncoder != nil)
			{
				++m_MetalPerfEncoderCount;
				++m_MetalPerfBlitEncoderCount;
			}
		}
		const bool Success = m_CurrentBlitEncoder != nil;
		if(Success)
			[m_CurrentBlitEncoder copyFromBuffer:Staging sourceOffset:0 toBuffer:Destination destinationOffset:DestinationOffset size:DataBytes];
		if(m_pStagingMemoryUsage != nullptr)
			m_pStagingMemoryUsage->fetch_add(DataBytes, std::memory_order_relaxed);
		if(m_pStagingMemoryUsage != nullptr)
			m_pStagingMemoryUsage->fetch_sub(DataBytes, std::memory_order_relaxed);
		ReleaseMetalObject(Staging);
		return Success;
	}

	bool CreateBuffer(int Slot, size_t DataBytes, const void *pData, int Flags)
	{
		if(!EnsureBufferSlot(Slot))
			return false;
		if(m_vBufferSlots[Slot].m_Allocated)
			DestroyBuffer(Slot);
		const bool OneTimeUse = (Flags & IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT) != 0;
		SMetalBufferLayout Layout;
		if(!MetalBufferLayout(DataBytes, OneTimeUse, Layout) || m_Device == nil || m_CurrentCommandBuffer == nil)
			return false;
		id<MTLBuffer> Buffer = nil;
		bool ReusedTransientBuffer = false;
		if(OneTimeUse)
		{
			auto &Available = m_aTransientBuffers[m_CurrentFrameSlot];
			for(auto It = Available.begin(); It != Available.end(); ++It)
			{
				if(It->m_DataBytes == DataBytes)
				{
					Buffer = It->m_Buffer;
					m_aTransientBufferPoolBytes[m_CurrentFrameSlot] -= It->m_DataBytes;
					Available.erase(It);
					ReusedTransientBuffer = true;
					break;
				}
			}
		}
		if(Buffer == nil)
		{
			// 小尺寸持久 buffer 也使用 Shared：更新时直写内存，避免 blit 打断。
			const bool UseSharedStorage = OneTimeUse || DataBytes <= QM_METAL_MAX_SHARED_BUFFER_BYTES;
			const MTLResourceOptions Options = UseSharedStorage ? MTLResourceStorageModeShared : MTLResourceStorageModePrivate;
			Buffer = [m_Device newBufferWithLength:DataBytes options:Options];
		}
		if(Buffer == nil)
			return false;
		if(m_MetalPerfEnabled && ReusedTransientBuffer)
		{
			++m_MetalPerfTransientPoolReuseCount;
			m_MetalPerfTransientPoolReuseBytes += DataBytes;
		}
		if(m_MetalPerfEnabled && !ReusedTransientBuffer)
		{
			++m_MetalPerfBufferCreateCount;
			m_MetalPerfBufferCreateBytes += DataBytes;
		}
		bool Success = true;
		if(OneTimeUse)
		{
			if(pData != nullptr)
				mem_copy(Buffer.contents, pData, DataBytes);
			else
				std::memset(Buffer.contents, 0, DataBytes);
		}
		else
		{
			Success = CopyIntoBuffer(Buffer, 0, pData, DataBytes);
		}
		if(!Success)
		{
			ReleaseMetalObject(Buffer);
			return false;
		}
		SBufferSlot &SlotInfo = m_vBufferSlots[Slot];
		SlotInfo.m_Buffer = Buffer;
		SlotInfo.m_DataBytes = DataBytes;
		SlotInfo.m_OneTimeUse = OneTimeUse;
		SlotInfo.m_Allocated = true;
		if(!ReusedTransientBuffer)
			AddBufferMemory(DataBytes);
		return true;
	}

	bool UpdateBuffer(int Slot, size_t Offset, size_t DataBytes, const void *pData)
	{
		if(Slot < 0 || static_cast<size_t>(Slot) >= m_vBufferSlots.size() || !m_vBufferSlots[Slot].m_Allocated)
			return false;
		SBufferSlot &Buffer = m_vBufferSlots[Slot];
		if(!MetalValidateBufferRange(Buffer.m_DataBytes, Offset, DataBytes))
			return false;
		if(!WaitForBufferIdle(Buffer))
			return false;
		if(Buffer.m_OneTimeUse)
		{
			if(pData != nullptr)
				mem_copy(static_cast<uint8_t *>(Buffer.m_Buffer.contents) + Offset, pData, DataBytes);
			else
				std::memset(static_cast<uint8_t *>(Buffer.m_Buffer.contents) + Offset, 0, DataBytes);
			return true;
		}
		return CopyIntoBuffer(Buffer.m_Buffer, Offset, pData, DataBytes);
	}

	bool RecreateBuffer(int Slot, size_t DataBytes, const void *pData, int Flags)
	{
		const bool OneTimeUse = (Flags & IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT) != 0;
		if(Slot >= 0 && static_cast<size_t>(Slot) < m_vBufferSlots.size())
		{
			SBufferSlot &Existing = m_vBufferSlots[Slot];
			if(Existing.m_Allocated && !OneTimeUse && !Existing.m_OneTimeUse && Existing.m_DataBytes == DataBytes && !IsBufferInFlight(Existing))
			{
				const bool Success = UpdateBuffer(Slot, 0, DataBytes, pData);
				if(Success && m_MetalPerfEnabled)
				{
					++m_MetalPerfBufferReuseCount;
					m_MetalPerfBufferReuseBytes += DataBytes;
				}
				return Success;
			}
		}
		DestroyBuffer(Slot);
		return CreateBuffer(Slot, DataBytes, pData, Flags);
	}

	bool CopyBuffer(int WriteSlot, int ReadSlot, size_t WriteOffset, size_t ReadOffset, size_t CopyBytes)
	{
		if(WriteSlot < 0 || ReadSlot < 0 || static_cast<size_t>(WriteSlot) >= m_vBufferSlots.size() || static_cast<size_t>(ReadSlot) >= m_vBufferSlots.size() || !m_vBufferSlots[WriteSlot].m_Allocated || !m_vBufferSlots[ReadSlot].m_Allocated)
			return false;
		SBufferSlot &WriteBuffer = m_vBufferSlots[WriteSlot];
		SBufferSlot &ReadBuffer = m_vBufferSlots[ReadSlot];
		if(!MetalValidateBufferRange(WriteBuffer.m_DataBytes, WriteOffset, CopyBytes) || !MetalValidateBufferRange(ReadBuffer.m_DataBytes, ReadOffset, CopyBytes))
			return false;
		if(WriteBuffer.m_OneTimeUse && ReadBuffer.m_OneTimeUse)
		{
			if(WriteSlot == ReadSlot && WriteOffset < ReadOffset + CopyBytes && ReadOffset < WriteOffset + CopyBytes)
				std::memmove(static_cast<uint8_t *>(WriteBuffer.m_Buffer.contents) + WriteOffset, static_cast<uint8_t *>(ReadBuffer.m_Buffer.contents) + ReadOffset, CopyBytes);
			else
				mem_copy(static_cast<uint8_t *>(WriteBuffer.m_Buffer.contents) + WriteOffset, static_cast<uint8_t *>(ReadBuffer.m_Buffer.contents) + ReadOffset, CopyBytes);
			return true;
		}
		if(m_CurrentCommandBuffer == nil)
			return false;
		EndRenderEncoderForBlit();
		if(m_CurrentBlitEncoder == nil)
		{
			m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
			if(m_MetalPerfEnabled && m_CurrentBlitEncoder != nil)
			{
				++m_MetalPerfEncoderCount;
				++m_MetalPerfBlitEncoderCount;
			}
		}
		if(m_CurrentBlitEncoder == nil)
			return false;
		[m_CurrentBlitEncoder copyFromBuffer:ReadBuffer.m_Buffer sourceOffset:ReadOffset toBuffer:WriteBuffer.m_Buffer destinationOffset:WriteOffset size:CopyBytes];
		return true;
	}

	void AddStreamMemory(size_t Bytes)
	{
		if(m_pStreamMemoryUsage != nullptr)
			m_pStreamMemoryUsage->fetch_add(Bytes, std::memory_order_relaxed);
	}

	void SubStreamMemory(size_t Bytes)
	{
		if(m_pStreamMemoryUsage != nullptr)
			m_pStreamMemoryUsage->fetch_sub(Bytes, std::memory_order_relaxed);
	}

	bool LoadShaderLibrary(const SCommand_Init *pCommand)
	{
		if(pCommand->m_pStorage == nullptr)
			return false;
		constexpr const char *pShaderLibraryPath = "shader/metal/qmclient.metallib";
		void *pShaderData = nullptr;
		unsigned ShaderSize = 0;
		const bool ShaderLibraryRead = pCommand->m_pStorage->ReadFile(pShaderLibraryPath, IStorage::TYPE_ALL, &pShaderData, &ShaderSize);
		if(!ShaderLibraryRead || pShaderData == nullptr || ShaderSize == 0)
		{
			dbg_msg("gfx/metal", "failed to read shader library '%s' from storage type TYPE_ALL (read=%d, bytes=%u)", pShaderLibraryPath, ShaderLibraryRead, ShaderSize);
			free(pShaderData);
			return false;
		}

		dispatch_data_t Data = dispatch_data_create(pShaderData, ShaderSize, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
			free(pShaderData);
		});
		NSError *pError = nil;
		m_ShaderLibrary = [m_Device newLibraryWithData:Data error:&pError];
#if !OS_OBJECT_USE_OBJC
		dispatch_release(Data);
#endif
		if(m_ShaderLibrary == nil && pError != nil)
			dbg_msg("gfx/metal", "failed to load shader library '%s': %s", pShaderLibraryPath, [[pError localizedDescription] UTF8String]);
		return m_ShaderLibrary != nil;
	}

	bool CreateSamplerStates()
	{
		MTLSamplerDescriptor *pDescriptor = [[MTLSamplerDescriptor alloc] init];
		pDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
		pDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
		pDescriptor.mipFilter = MTLSamplerMipFilterLinear;
		pDescriptor.sAddressMode = MTLSamplerAddressModeRepeat;
		pDescriptor.tAddressMode = MTLSamplerAddressModeRepeat;
		m_RepeatSampler = [m_Device newSamplerStateWithDescriptor:pDescriptor];
		pDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
		pDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
		m_ClampSampler = [m_Device newSamplerStateWithDescriptor:pDescriptor];
		// Tile array textures currently upload level 0 only. Do not sample
		// uninitialized array mip levels while zooming out.
		pDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
		pDescriptor.sAddressMode = MTLSamplerAddressModeRepeat;
		pDescriptor.tAddressMode = MTLSamplerAddressModeRepeat;
		m_TileRepeatSampler = [m_Device newSamplerStateWithDescriptor:pDescriptor];
		pDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
		pDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
		m_TileClampSampler = [m_Device newSamplerStateWithDescriptor:pDescriptor];
#if !__has_feature(objc_arc)
		[pDescriptor release];
#endif
		return m_RepeatSampler != nil && m_ClampSampler != nil && m_TileRepeatSampler != nil && m_TileClampSampler != nil;
	}

	bool CreatePipelineStates(uint32_t SampleCount, TPipelineStates &PipelineStates)
	{
		if(SampleCount == 0 || m_Device == nil || m_ShaderLibrary == nil)
			return false;
		TPipelineStates NewPipelineStates{};
		id<MTLFunction> VertexFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_vertex"];
		id<MTLFunction> ColorFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_fragment"];
		id<MTLFunction> TexturedFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_textured_fragment"];
		id<MTLFunction> TextFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_text_fragment"];
		id<MTLFunction> TexArrayVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_tex_array_vertex"];
		id<MTLFunction> TexArrayFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_tex_array_fragment"];
		if(VertexFunction == nil || ColorFunction == nil || TexturedFunction == nil || TextFunction == nil || TexArrayVertex == nil || TexArrayFragment == nil)
		{
			ReleaseMetalObject(VertexFunction);
			ReleaseMetalObject(ColorFunction);
			ReleaseMetalObject(TexturedFunction);
			ReleaseMetalObject(TextFunction);
			ReleaseMetalObject(TexArrayVertex);
			ReleaseMetalObject(TexArrayFragment);
			return false;
		}

		bool Success = true;
		auto CreatePipeline = [this, &Success, SampleCount, &NewPipelineStates](size_t Index, id<MTLFunction> Vertex, id<MTLFunction> Fragment, MTLVertexDescriptor *pDescriptor) {
			for(int Blend = 0; Blend <= static_cast<int>(EMetalBlendMode::ADDITIVE); ++Blend)
			{
				MTLRenderPipelineDescriptor *pPipeline = [[MTLRenderPipelineDescriptor alloc] init];
				pPipeline.vertexFunction = Vertex;
				pPipeline.fragmentFunction = Fragment;
				pPipeline.vertexDescriptor = pDescriptor;
				pPipeline.rasterSampleCount = SampleCount;
				pPipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
				const SMetalBlendState BlendState = MetalBlendState(static_cast<EMetalBlendMode>(Blend));
				pPipeline.colorAttachments[0].blendingEnabled = BlendState.m_Enabled;
				pPipeline.colorAttachments[0].sourceRGBBlendFactor = MetalBlendFactor(BlendState.m_Source);
				pPipeline.colorAttachments[0].destinationRGBBlendFactor = MetalBlendFactor(BlendState.m_Destination);
				pPipeline.colorAttachments[0].sourceAlphaBlendFactor = MetalBlendFactor(BlendState.m_Source);
				pPipeline.colorAttachments[0].destinationAlphaBlendFactor = MetalBlendFactor(BlendState.m_Destination);
				NSError *pError = nil;
				NewPipelineStates[Index + static_cast<size_t>(Blend)] = [m_Device newRenderPipelineStateWithDescriptor:pPipeline error:&pError];
				if(NewPipelineStates[Index + static_cast<size_t>(Blend)] == nil && pError != nil)
					dbg_msg("gfx/metal", "pipeline %zu blend %d failed: %s", Index, Blend, [[pError localizedDescription] UTF8String]);
				Success = Success && NewPipelineStates[Index + static_cast<size_t>(Blend)] != nil;
#if !__has_feature(objc_arc)
				[pPipeline release];
#endif
			}
		};

		MTLVertexDescriptor *pVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		pVertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[0].offset = offsetof(GL_SVertex, m_Pos);
		pVertexDescriptor.attributes[0].bufferIndex = 0;
		pVertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[1].offset = offsetof(GL_SVertex, m_Tex);
		pVertexDescriptor.attributes[1].bufferIndex = 0;
		pVertexDescriptor.attributes[2].format = MTLVertexFormatUChar4Normalized;
		pVertexDescriptor.attributes[2].offset = offsetof(GL_SVertex, m_Color);
		pVertexDescriptor.attributes[2].bufferIndex = 0;
		pVertexDescriptor.layouts[0].stride = sizeof(GL_SVertex);
		pVertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		for(int Textured = 0; Textured <= 1; ++Textured)
		{
			for(int Text = 0; Text <= 1; ++Text)
			{
				if(Textured == 0 && Text != 0)
					continue;
				CreatePipeline(PipelineIndex(Textured != 0, Text != 0, EMetalBlendMode::NONE), VertexFunction, Text ? TextFunction : (Textured ? TexturedFunction : ColorFunction), pVertexDescriptor);
			}
		}

		MTLVertexDescriptor *pTexArrayDescriptor = [[MTLVertexDescriptor alloc] init];
		pTexArrayDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pTexArrayDescriptor.attributes[0].offset = offsetof(GL_SVertexTex3DStream, m_Pos);
		pTexArrayDescriptor.attributes[0].bufferIndex = 0;
		pTexArrayDescriptor.attributes[1].format = MTLVertexFormatUChar4Normalized;
		pTexArrayDescriptor.attributes[1].offset = offsetof(GL_SVertexTex3DStream, m_Color);
		pTexArrayDescriptor.attributes[1].bufferIndex = 0;
		pTexArrayDescriptor.attributes[2].format = MTLVertexFormatFloat3;
		pTexArrayDescriptor.attributes[2].offset = offsetof(GL_SVertexTex3DStream, m_Tex);
		pTexArrayDescriptor.attributes[2].bufferIndex = 0;
		pTexArrayDescriptor.layouts[0].stride = sizeof(GL_SVertexTex3DStream);
		pTexArrayDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		CreatePipeline(TextureArrayPipelineIndex(EMetalBlendMode::NONE), TexArrayVertex, TexArrayFragment, pTexArrayDescriptor);

		id<MTLFunction> TileVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_vertex"];
		id<MTLFunction> TileBorderVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_border_vertex"];
		id<MTLFunction> TilePlainVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_plain_vertex"];
		id<MTLFunction> TileFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_fragment"];
		id<MTLFunction> TileTexturedFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_textured_fragment"];
		id<MTLFunction> TileBorderTexturedFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_tile_border_textured_fragment"];
		id<MTLFunction> QuadVertexGrouped = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_vertex_grouped"];
		id<MTLFunction> QuadVertexUngrouped = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_vertex_ungrouped"];
		id<MTLFunction> QuadPlainVertexGrouped = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_plain_vertex_grouped"];
		id<MTLFunction> QuadPlainVertexUngrouped = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_plain_vertex_ungrouped"];
		id<MTLFunction> QuadFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_fragment"];
		id<MTLFunction> QuadTexturedFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_textured_fragment"];
		id<MTLFunction> QuadContainerExVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_container_ex_vertex"];
		id<MTLFunction> QuadContainerExFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_container_ex_fragment"];
		id<MTLFunction> QuadContainerExTexturedFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_quad_container_ex_textured_fragment"];
		id<MTLFunction> SpriteMultipleVertex = [m_ShaderLibrary newFunctionWithName:@"qmclient_sprite_multiple_vertex"];
		id<MTLFunction> SpriteMultipleFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_sprite_multiple_fragment"];
		id<MTLFunction> SpriteMultipleTexturedFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_sprite_multiple_textured_fragment"];
		if(TileVertex == nil || TileBorderVertex == nil || TilePlainVertex == nil || TileFragment == nil || TileTexturedFragment == nil || TileBorderTexturedFragment == nil || QuadVertexGrouped == nil || QuadVertexUngrouped == nil || QuadPlainVertexGrouped == nil || QuadPlainVertexUngrouped == nil || QuadFragment == nil || QuadTexturedFragment == nil || QuadContainerExVertex == nil || QuadContainerExFragment == nil || QuadContainerExTexturedFragment == nil || SpriteMultipleVertex == nil || SpriteMultipleFragment == nil || SpriteMultipleTexturedFragment == nil)
			Success = false;

		MTLVertexDescriptor *pTileDescriptor = [[MTLVertexDescriptor alloc] init];
		pTileDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pTileDescriptor.attributes[0].offset = 0;
		pTileDescriptor.attributes[0].bufferIndex = 0;
		pTileDescriptor.attributes[1].format = MTLVertexFormatUChar4;
		pTileDescriptor.attributes[1].offset = sizeof(float) * 2;
		pTileDescriptor.attributes[1].bufferIndex = 0;
		pTileDescriptor.layouts[0].stride = sizeof(float) * 2 + sizeof(uint8_t) * 4;
		pTileDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		MTLVertexDescriptor *pTilePlainDescriptor = [[MTLVertexDescriptor alloc] init];
		pTilePlainDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pTilePlainDescriptor.attributes[0].offset = 0;
		pTilePlainDescriptor.attributes[0].bufferIndex = 0;
		pTilePlainDescriptor.layouts[0].stride = sizeof(float) * 2;
		pTilePlainDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		if(TileVertex != nil && TileTexturedFragment != nil)
			CreatePipeline(TilePipelineIndex(true, false, EMetalBlendMode::NONE), TileVertex, TileTexturedFragment, pTileDescriptor);
		if(TilePlainVertex != nil && TileFragment != nil)
			CreatePipeline(TilePipelineIndex(false, false, EMetalBlendMode::NONE), TilePlainVertex, TileFragment, pTilePlainDescriptor);
		if(TileBorderVertex != nil && TileBorderTexturedFragment != nil)
			CreatePipeline(TilePipelineIndex(true, true, EMetalBlendMode::NONE), TileBorderVertex, TileBorderTexturedFragment, pTileDescriptor);
		if(TilePlainVertex != nil && TileFragment != nil)
			CreatePipeline(TilePipelineIndex(false, true, EMetalBlendMode::NONE), TilePlainVertex, TileFragment, pTilePlainDescriptor);

		MTLVertexDescriptor *pQuadDescriptor = [[MTLVertexDescriptor alloc] init];
		pQuadDescriptor.attributes[0].format = MTLVertexFormatFloat4;
		pQuadDescriptor.attributes[0].offset = 0;
		pQuadDescriptor.attributes[0].bufferIndex = 0;
		pQuadDescriptor.attributes[1].format = MTLVertexFormatUChar4Normalized;
		pQuadDescriptor.attributes[1].offset = sizeof(float) * 4;
		pQuadDescriptor.attributes[1].bufferIndex = 0;
		pQuadDescriptor.attributes[2].format = MTLVertexFormatFloat2;
		pQuadDescriptor.attributes[2].offset = sizeof(float) * 4 + sizeof(uint8_t) * 4;
		pQuadDescriptor.attributes[2].bufferIndex = 0;
		pQuadDescriptor.layouts[0].stride = sizeof(float) * 4 + sizeof(uint8_t) * 4 + sizeof(float) * 2;
		pQuadDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		MTLVertexDescriptor *pQuadPlainDescriptor = [[MTLVertexDescriptor alloc] init];
		pQuadPlainDescriptor.attributes[0].format = MTLVertexFormatFloat4;
		pQuadPlainDescriptor.attributes[0].offset = 0;
		pQuadPlainDescriptor.attributes[0].bufferIndex = 0;
		pQuadPlainDescriptor.attributes[1].format = MTLVertexFormatUChar4Normalized;
		pQuadPlainDescriptor.attributes[1].offset = sizeof(float) * 4;
		pQuadPlainDescriptor.attributes[1].bufferIndex = 0;
		pQuadPlainDescriptor.layouts[0].stride = sizeof(float) * 4 + sizeof(uint8_t) * 4;
		pQuadPlainDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		if(QuadPlainVertexGrouped != nil && QuadFragment != nil)
			CreatePipeline(QuadPipelineIndex(false, true, EMetalBlendMode::NONE), QuadPlainVertexGrouped, QuadFragment, pQuadPlainDescriptor);
		if(QuadPlainVertexUngrouped != nil && QuadFragment != nil)
			CreatePipeline(QuadPipelineIndex(false, false, EMetalBlendMode::NONE), QuadPlainVertexUngrouped, QuadFragment, pQuadPlainDescriptor);
		if(QuadVertexGrouped != nil && QuadTexturedFragment != nil)
			CreatePipeline(QuadPipelineIndex(true, true, EMetalBlendMode::NONE), QuadVertexGrouped, QuadTexturedFragment, pQuadDescriptor);
		if(QuadVertexUngrouped != nil && QuadTexturedFragment != nil)
			CreatePipeline(QuadPipelineIndex(true, false, EMetalBlendMode::NONE), QuadVertexUngrouped, QuadTexturedFragment, pQuadDescriptor);
		if(QuadContainerExVertex != nil && QuadContainerExFragment != nil)
			CreatePipeline(QuadContainerExPipelineIndex(false, EMetalBlendMode::NONE), QuadContainerExVertex, QuadContainerExFragment, pVertexDescriptor);
		if(QuadContainerExVertex != nil && QuadContainerExTexturedFragment != nil)
			CreatePipeline(QuadContainerExPipelineIndex(true, EMetalBlendMode::NONE), QuadContainerExVertex, QuadContainerExTexturedFragment, pVertexDescriptor);
		if(SpriteMultipleVertex != nil && SpriteMultipleFragment != nil)
			CreatePipeline(SpriteMultiplePipelineIndex(false, EMetalBlendMode::NONE), SpriteMultipleVertex, SpriteMultipleFragment, pVertexDescriptor);
		if(SpriteMultipleVertex != nil && SpriteMultipleTexturedFragment != nil)
			CreatePipeline(SpriteMultiplePipelineIndex(true, EMetalBlendMode::NONE), SpriteMultipleVertex, SpriteMultipleTexturedFragment, pVertexDescriptor);
#if !__has_feature(objc_arc)
		[pVertexDescriptor release];
		[pTexArrayDescriptor release];
		[pTileDescriptor release];
		[pTilePlainDescriptor release];
		[pQuadDescriptor release];
		[pQuadPlainDescriptor release];
		[VertexFunction release];
		[ColorFunction release];
		[TexturedFunction release];
		[TextFunction release];
		[TexArrayVertex release];
		[TexArrayFragment release];
		[TileVertex release];
		[TileBorderVertex release];
		[TilePlainVertex release];
		[TileFragment release];
		[TileTexturedFragment release];
		[TileBorderTexturedFragment release];
		[QuadVertexGrouped release];
		[QuadVertexUngrouped release];
		[QuadPlainVertexGrouped release];
		[QuadPlainVertexUngrouped release];
		[QuadFragment release];
		[QuadTexturedFragment release];
		[QuadContainerExVertex release];
		[QuadContainerExFragment release];
		[QuadContainerExTexturedFragment release];
		[SpriteMultipleVertex release];
		[SpriteMultipleFragment release];
		[SpriteMultipleTexturedFragment release];
#endif
		if(Success)
		{
			ReleasePipelineStates(PipelineStates);
			PipelineStates.swap(NewPipelineStates);
		}
		ReleasePipelineStates(NewPipelineStates);
		return Success;
	}

	bool CreateGaussianBlurPipeline()
	{
		if(m_Device == nil || m_ShaderLibrary == nil)
			return false;
		id<MTLFunction> VertexFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_vertex"];
		id<MTLFunction> FragmentFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_gaussian_blur_fragment"];
		if(VertexFunction == nil || FragmentFunction == nil)
		{
			ReleaseMetalObject(VertexFunction);
			ReleaseMetalObject(FragmentFunction);
			return false;
		}
		MTLVertexDescriptor *pVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		pVertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[0].offset = offsetof(GL_SVertex, m_Pos);
		pVertexDescriptor.attributes[0].bufferIndex = 0;
		pVertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[1].offset = offsetof(GL_SVertex, m_Tex);
		pVertexDescriptor.attributes[1].bufferIndex = 0;
		pVertexDescriptor.attributes[2].format = MTLVertexFormatUChar4Normalized;
		pVertexDescriptor.attributes[2].offset = offsetof(GL_SVertex, m_Color);
		pVertexDescriptor.attributes[2].bufferIndex = 0;
		pVertexDescriptor.layouts[0].stride = sizeof(GL_SVertex);
		pVertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		MTLRenderPipelineDescriptor *pPipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
		pPipelineDescriptor.vertexFunction = VertexFunction;
		pPipelineDescriptor.fragmentFunction = FragmentFunction;
		pPipelineDescriptor.vertexDescriptor = pVertexDescriptor;
		pPipelineDescriptor.rasterSampleCount = 1;
		pPipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		pPipelineDescriptor.colorAttachments[0].blendingEnabled = NO;
		NSError *pError = nil;
		id<MTLRenderPipelineState> Pipeline = [m_Device newRenderPipelineStateWithDescriptor:pPipelineDescriptor error:&pError];
#if !__has_feature(objc_arc)
		[pPipelineDescriptor release];
		[pVertexDescriptor release];
		[VertexFunction release];
		[FragmentFunction release];
#endif
		if(Pipeline == nil)
			return false;
		ReleaseMetalObject(m_GaussianBlurPipeline);
		m_GaussianBlurPipeline = Pipeline;
		return true;
	}

	bool CreateSdfPipelineStates(uint32_t SampleCount, TSdfPipelineStates &MediaIslandPipelines, TSdfPipelineStates &RoundedRectPipelines)
	{
		if(SampleCount == 0 || m_Device == nil || m_ShaderLibrary == nil)
			return false;
		TSdfPipelineStates NewMediaIslandPipelines{};
		TSdfPipelineStates NewRoundedRectPipelines{};
		id<MTLFunction> VertexFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_vertex"];
		id<MTLFunction> MediaIslandFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_media_island_sdf_fragment"];
		id<MTLFunction> RoundedRectFragment = [m_ShaderLibrary newFunctionWithName:@"qmclient_rounded_rect_sdf_fragment"];
		if(VertexFunction == nil || MediaIslandFragment == nil || RoundedRectFragment == nil)
		{
			ReleaseMetalObject(VertexFunction);
			ReleaseMetalObject(MediaIslandFragment);
			ReleaseMetalObject(RoundedRectFragment);
			return false;
		}

		MTLVertexDescriptor *pVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		pVertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[0].offset = offsetof(GL_SVertex, m_Pos);
		pVertexDescriptor.attributes[0].bufferIndex = 0;
		pVertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[1].offset = offsetof(GL_SVertex, m_Tex);
		pVertexDescriptor.attributes[1].bufferIndex = 0;
		pVertexDescriptor.attributes[2].format = MTLVertexFormatUChar4Normalized;
		pVertexDescriptor.attributes[2].offset = offsetof(GL_SVertex, m_Color);
		pVertexDescriptor.attributes[2].bufferIndex = 0;
		pVertexDescriptor.layouts[0].stride = sizeof(GL_SVertex);
		pVertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

		auto CreatePipelines = [this, SampleCount, VertexFunction, pVertexDescriptor](id<MTLFunction> FragmentFunction, TSdfPipelineStates &Pipelines, const char *pName) {
			bool Success = true;
			for(int Blend = 0; Blend <= static_cast<int>(EMetalBlendMode::ADDITIVE); ++Blend)
			{
				MTLRenderPipelineDescriptor *pPipeline = [[MTLRenderPipelineDescriptor alloc] init];
				pPipeline.vertexFunction = VertexFunction;
				pPipeline.fragmentFunction = FragmentFunction;
				pPipeline.vertexDescriptor = pVertexDescriptor;
				pPipeline.rasterSampleCount = SampleCount;
				pPipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
				const SMetalBlendState BlendState = MetalBlendState(static_cast<EMetalBlendMode>(Blend));
				pPipeline.colorAttachments[0].blendingEnabled = BlendState.m_Enabled;
				pPipeline.colorAttachments[0].sourceRGBBlendFactor = MetalBlendFactor(BlendState.m_Source);
				pPipeline.colorAttachments[0].destinationRGBBlendFactor = MetalBlendFactor(BlendState.m_Destination);
				pPipeline.colorAttachments[0].sourceAlphaBlendFactor = MetalBlendFactor(BlendState.m_Source);
				pPipeline.colorAttachments[0].destinationAlphaBlendFactor = MetalBlendFactor(BlendState.m_Destination);
				NSError *pError = nil;
				Pipelines[static_cast<size_t>(Blend)] = [m_Device newRenderPipelineStateWithDescriptor:pPipeline error:&pError];
				if(Pipelines[static_cast<size_t>(Blend)] == nil && pError != nil)
					dbg_msg("gfx/metal", "%s pipeline blend %d failed: %s", pName, Blend, [[pError localizedDescription] UTF8String]);
				Success = Success && Pipelines[static_cast<size_t>(Blend)] != nil;
#if !__has_feature(objc_arc)
				[pPipeline release];
#endif
			}
			return Success;
		};

		const bool Success = CreatePipelines(MediaIslandFragment, NewMediaIslandPipelines, "Media Island SDF") && CreatePipelines(RoundedRectFragment, NewRoundedRectPipelines, "rounded-rect SDF");
#if !__has_feature(objc_arc)
		[pVertexDescriptor release];
		[VertexFunction release];
		[MediaIslandFragment release];
		[RoundedRectFragment release];
#endif
		if(Success)
		{
			ReleasePipelineStates(MediaIslandPipelines);
			ReleasePipelineStates(RoundedRectPipelines);
			MediaIslandPipelines.swap(NewMediaIslandPipelines);
			RoundedRectPipelines.swap(NewRoundedRectPipelines);
		}
		ReleasePipelineStates(NewMediaIslandPipelines);
		ReleasePipelineStates(NewRoundedRectPipelines);
		return Success;
	}

	bool CreateTexturedMsdfPipelineStates(uint32_t SampleCount, TSdfPipelineStates &Pipelines)
	{
		if(SampleCount == 0 || m_Device == nil || m_ShaderLibrary == nil)
			return false;
		TSdfPipelineStates NewPipelines{};
		id<MTLFunction> VertexFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_vertex"];
		id<MTLFunction> FragmentFunction = [m_ShaderLibrary newFunctionWithName:@"qmclient_textured_msdf_fragment"];
		if(VertexFunction == nil || FragmentFunction == nil)
		{
			ReleaseMetalObject(VertexFunction);
			ReleaseMetalObject(FragmentFunction);
			return false;
		}

		MTLVertexDescriptor *pVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		pVertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[0].offset = offsetof(GL_SVertex, m_Pos);
		pVertexDescriptor.attributes[0].bufferIndex = 0;
		pVertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
		pVertexDescriptor.attributes[1].offset = offsetof(GL_SVertex, m_Tex);
		pVertexDescriptor.attributes[1].bufferIndex = 0;
		pVertexDescriptor.attributes[2].format = MTLVertexFormatUChar4Normalized;
		pVertexDescriptor.attributes[2].offset = offsetof(GL_SVertex, m_Color);
		pVertexDescriptor.attributes[2].bufferIndex = 0;
		pVertexDescriptor.layouts[0].stride = sizeof(GL_SVertex);
		pVertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

		bool Success = true;
		for(int Blend = 0; Blend <= static_cast<int>(EMetalBlendMode::ADDITIVE); ++Blend)
		{
			MTLRenderPipelineDescriptor *pPipeline = [[MTLRenderPipelineDescriptor alloc] init];
			pPipeline.vertexFunction = VertexFunction;
			pPipeline.fragmentFunction = FragmentFunction;
			pPipeline.vertexDescriptor = pVertexDescriptor;
			pPipeline.rasterSampleCount = SampleCount;
			pPipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
			const SMetalBlendState BlendState = MetalBlendState(static_cast<EMetalBlendMode>(Blend));
			pPipeline.colorAttachments[0].blendingEnabled = BlendState.m_Enabled;
			pPipeline.colorAttachments[0].sourceRGBBlendFactor = MetalBlendFactor(BlendState.m_Source);
			pPipeline.colorAttachments[0].destinationRGBBlendFactor = MetalBlendFactor(BlendState.m_Destination);
			pPipeline.colorAttachments[0].sourceAlphaBlendFactor = MetalBlendFactor(BlendState.m_Source);
			pPipeline.colorAttachments[0].destinationAlphaBlendFactor = MetalBlendFactor(BlendState.m_Destination);
			NSError *pError = nil;
			NewPipelines[static_cast<size_t>(Blend)] = [m_Device newRenderPipelineStateWithDescriptor:pPipeline error:&pError];
			if(NewPipelines[static_cast<size_t>(Blend)] == nil && pError != nil)
				dbg_msg("gfx/metal", "Textured MSDF pipeline blend %d failed: %s", Blend, [[pError localizedDescription] UTF8String]);
			Success = Success && NewPipelines[static_cast<size_t>(Blend)] != nil;
#if !__has_feature(objc_arc)
			[pPipeline release];
#endif
		}
#if !__has_feature(objc_arc)
		[pVertexDescriptor release];
		[VertexFunction release];
		[FragmentFunction release];
#endif
		if(Success)
		{
			ReleasePipelineStates(Pipelines);
			Pipelines.swap(NewPipelines);
		}
		ReleasePipelineStates(NewPipelines);
		return Success;
	}

	static bool HasSdfPipelines(const TSdfPipelineStates &Pipelines)
	{
		return std::all_of(Pipelines.begin(), Pipelines.end(), [](id<MTLRenderPipelineState> Pipeline) { return Pipeline != nil; });
	}

	bool HasSdfSupportForCurrentSampleCount() const
	{
		const TSdfPipelineStates &MediaIslandPipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aMediaIslandSdfPipelines : m_aMultiSampleMediaIslandSdfPipelines;
		const TSdfPipelineStates &RoundedRectPipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aRoundedRectSdfPipelines : m_aMultiSampleRoundedRectSdfPipelines;
		return HasSdfPipelines(MediaIslandPipelines) && HasSdfPipelines(RoundedRectPipelines);
	}

	bool HasTexturedMsdfSupportForCurrentSampleCount() const
	{
		const TSdfPipelineStates &Pipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aTexturedMsdfPipelines : m_aMultiSampleTexturedMsdfPipelines;
		return HasSdfPipelines(Pipelines);
	}

	bool CreateQuadIndexBuffer(size_t IndexCount)
	{
		if(IndexCount == 0 || IndexCount % 6 != 0 || IndexCount / 6 > (std::numeric_limits<uint32_t>::max() / 4))
			return false;
		size_t IndexBytes = 0;
		if(!MetalCheckedMul(IndexCount, sizeof(uint32_t), IndexBytes))
			return false;
		std::vector<uint32_t> vIndices(IndexCount);
		for(size_t Quad = 0; Quad < IndexCount / 6; ++Quad)
		{
			const uint32_t Base = static_cast<uint32_t>(Quad * 4);
			const size_t Offset = Quad * 6;
			vIndices[Offset + 0] = Base + 0;
			vIndices[Offset + 1] = Base + 1;
			vIndices[Offset + 2] = Base + 2;
			vIndices[Offset + 3] = Base + 0;
			vIndices[Offset + 4] = Base + 2;
			vIndices[Offset + 5] = Base + 3;
		}
		id<MTLBuffer> NewIndexBuffer = [m_Device newBufferWithLength:IndexBytes options:MTLResourceStorageModeShared];
		if(NewIndexBuffer == nil || NewIndexBuffer.contents == nullptr)
		{
			ReleaseMetalObject(NewIndexBuffer);
			return false;
		}
		mem_copy(NewIndexBuffer.contents, vIndices.data(), IndexBytes);
		ReleaseMetalObject(m_QuadIndexBuffer);
		m_QuadIndexBuffer = NewIndexBuffer;
		m_QuadIndexCount = IndexCount;
		if(m_BufferMemoryBytes != 0)
			SubBufferMemory(m_BufferMemoryBytes);
		m_BufferMemoryBytes = IndexBytes;
		AddBufferMemory(m_BufferMemoryBytes);
		return true;
	}

	bool EnsureQuadIndexCapacity(size_t RequiredIndexCount)
	{
		if(RequiredIndexCount == 0 || RequiredIndexCount % 6 != 0)
			return false;
		if(RequiredIndexCount <= m_QuadIndexCount)
			return true;
		size_t NewIndexCount = m_QuadIndexCount == 0 ? 6 : m_QuadIndexCount;
		while(NewIndexCount < RequiredIndexCount)
		{
			if(NewIndexCount > std::numeric_limits<size_t>::max() / 2)
			{
				NewIndexCount = RequiredIndexCount;
				break;
			}
			NewIndexCount *= 2;
		}
		NewIndexCount = std::max(NewIndexCount - NewIndexCount % 6, RequiredIndexCount);
		return CreateQuadIndexBuffer(NewIndexCount);
	}

	bool CreateFrameResources()
	{
		for(SFrameSlot &Frame : m_aFrameSlots)
		{
			Frame.m_VertexBuffer = [m_Device newBufferWithLength:gs_StreamBufferSize options:MTLResourceStorageModeShared];
			if(Frame.m_VertexBuffer == nil)
				return false;
		}

		if(!CreateQuadIndexBuffer(static_cast<size_t>(CCommandBuffer::MAX_VERTICES) / 4 * 6))
			return false;
		m_StreamMemoryBytes = gs_FrameSlotCount * gs_StreamBufferSize;
		AddStreamMemory(m_StreamMemoryBytes);
		return true;
	}

	bool EnsureTextureSlot(int Slot)
	{
		if(Slot < 0 || static_cast<size_t>(Slot) >= CCommandBuffer::MAX_TEXTURES)
			return false;
		if(static_cast<size_t>(Slot) >= m_vTextureSlots.size())
			m_vTextureSlots.resize(static_cast<size_t>(Slot) + 1);
		return true;
	}

	void DestroyTexture(int Slot)
	{
		if(!EnsureTextureSlot(Slot))
			return;
		STextureSlot &Texture = m_vTextureSlots[Slot];
		if(!Texture.m_Allocated)
			return;
		SubTextureMemory(Texture.m_Layout.m_DataBytes);
		SubTextureMemory(Texture.m_ArrayLayout.m_DataBytes);
		if(Texture.m_Staging != nil)
		{
			if(m_pStagingMemoryUsage != nullptr)
				m_pStagingMemoryUsage->fetch_sub([Texture.m_Staging length], std::memory_order_relaxed);
			ReleaseMetalObject(Texture.m_Staging);
		}
		ReleaseMetalObject(Texture.m_Texture);
		ReleaseMetalObject(Texture.m_TextureArray);
		Texture = {};
	}

	bool CreateTexture(int Slot, size_t Width, size_t Height, EMetalTextureFormat Format, int Flags, uint8_t *pData)
	{
		if(!EnsureTextureSlot(Slot))
			return false;
		STextureSlot &Texture = m_vTextureSlots[Slot];
		if(Texture.m_Allocated)
			DestroyTexture(Slot);

		const bool Wants2D = (Flags & TextureFlag::NO_2D_TEXTURE) == 0;
		const bool Wants2DArray = (Flags & TextureFlag::TO_2D_ARRAY_TEXTURE) != 0;
		if(!Wants2D && !Wants2DArray)
			return false;
		SMetalTextureLayout Layout{};
		if(Wants2D && !MetalTextureLayout(Width, Height, Format, (Flags & TextureFlag::NO_MIPMAPS) != 0, Layout))
			return false;
		size_t ArraySourceWidth = Width;
		size_t ArraySourceHeight = Height;
		size_t ArrayWidth = 0;
		size_t ArrayHeight = 0;
		SMetalTextureLayout ArrayLayout{};
		// Tile arrays are uploaded as a single level and sampled with the
		// non-mipmapped tile sampler. Keep the descriptor aligned with that
		// contract instead of allocating unused mip levels.
		if(Wants2DArray && (!MetalArraySourceDimensions(Width, Height, ArraySourceWidth, ArraySourceHeight) || !MetalTextureArrayLayout(ArraySourceWidth, ArraySourceHeight, Format, true, ArrayWidth, ArrayHeight, ArrayLayout)))
			return false;
		const size_t LayoutBytes = Wants2D ? Layout.m_DataBytes : 0;
		const size_t ArrayLayoutBytes = Wants2DArray ? ArrayLayout.m_DataBytes : 0;
		if(LayoutBytes > std::numeric_limits<size_t>::max() - ArrayLayoutBytes)
			return false;

		if(m_Device == nil || m_State != EMetalBackendState::INITIALIZED || m_CommandQueue == nil)
			return false;

		const MTLPixelFormat PixelFormat = Format == EMetalTextureFormat::R8 ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA8Unorm;
		// 创建即上传且无 mipmap 的纹理（UI 图标、旗帜、头像等）在统一内存设备上
		// 使用 Shared 存储，首次上传走 CPU 直写（replaceRegion）：不打断活跃的
		// render encoder、无需 staging buffer、内容即时可见。有 mipmap 或动态
		// 更新的纹理保持 Private + blit（成本低且不改变既有语义）。
		const bool UseSharedStorage = pData != nullptr && [m_Device hasUnifiedMemory] &&
			(!Wants2D || Layout.m_MipLevels <= 1) && (!Wants2DArray || ArrayLayout.m_MipLevels <= 1);
		id<MTLTexture> pTexture = nil;
		if(Wants2D)
		{
			MTLTextureDescriptor *pDescriptor = [[MTLTextureDescriptor alloc] init];
			pDescriptor.textureType = MTLTextureType2D;
			pDescriptor.pixelFormat = PixelFormat;
			pDescriptor.width = Width;
			pDescriptor.height = Height;
			pDescriptor.mipmapLevelCount = Layout.m_MipLevels;
			pDescriptor.usage = MTLTextureUsageShaderRead;
			pDescriptor.storageMode = UseSharedStorage ? MTLStorageModeShared : MTLStorageModePrivate;
			pTexture = [m_Device newTextureWithDescriptor:pDescriptor];
	#if !__has_feature(objc_arc)
			[pDescriptor release];
	#endif
			if(pTexture == nil)
				return false;
		}
		id<MTLTexture> pTextureArray = nil;
		if(Wants2DArray)
		{
			MTLTextureDescriptor *pArrayDescriptor = [[MTLTextureDescriptor alloc] init];
			pArrayDescriptor.textureType = MTLTextureType2DArray;
			pArrayDescriptor.pixelFormat = PixelFormat;
			pArrayDescriptor.width = ArrayWidth;
			pArrayDescriptor.height = ArrayHeight;
			pArrayDescriptor.arrayLength = METAL_TEXTURE_ARRAY_LAYERS;
			pArrayDescriptor.mipmapLevelCount = 1;
			pArrayDescriptor.usage = MTLTextureUsageShaderRead;
			pArrayDescriptor.storageMode = UseSharedStorage ? MTLStorageModeShared : MTLStorageModePrivate;
			pTextureArray = [m_Device newTextureWithDescriptor:pArrayDescriptor];
	#if !__has_feature(objc_arc)
			[pArrayDescriptor release];
	#endif
			if(pTextureArray == nil)
			{
				ReleaseMetalObject(pTexture);
				return false;
			}
		}

		Texture.m_Texture = pTexture;
		Texture.m_TextureArray = pTextureArray;
		Texture.m_Layout = Layout;
		Texture.m_ArrayLayout = ArrayLayout;
		Texture.m_Width = Width;
		Texture.m_Height = Height;
		Texture.m_ArraySourceWidth = ArraySourceWidth;
		Texture.m_ArraySourceHeight = ArraySourceHeight;
		Texture.m_ArrayWidth = ArrayWidth;
		Texture.m_ArrayHeight = ArrayHeight;
		Texture.m_Format = Format;
		Texture.m_Is2DArray = Wants2DArray;
		Texture.m_Allocated = true;
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfTextureCreateCount;
			m_MetalPerfTextureCreateBytes += LayoutBytes + ArrayLayoutBytes;
		}
		AddTextureMemory(LayoutBytes + ArrayLayoutBytes);

		bool Uploaded = pData == nullptr || pTexture == nil;
		if(pData != nullptr && pTexture != nil)
			Uploaded = UploadTextureRegion(Texture, 0, 0, Width, Height, pData);
		if(Uploaded && pTextureArray != nil && pData != nullptr)
		{
			const uint8_t *pArraySourceData = pData;
			uint8_t *pResizedArrayData = nullptr;
			if(ArraySourceWidth != Width || ArraySourceHeight != Height)
			{
				pResizedArrayData = ResizeImage(pData, static_cast<int>(Width), static_cast<int>(Height), static_cast<int>(ArraySourceWidth), static_cast<int>(ArraySourceHeight), static_cast<int>(MetalTextureBytesPerPixel(Format)));
				pArraySourceData = pResizedArrayData;
			}
			size_t SourceBytes = 0;
			if(pArraySourceData == nullptr || !MetalCheckedMul(ArraySourceWidth, ArraySourceHeight, SourceBytes) || !MetalCheckedMul(SourceBytes, MetalTextureBytesPerPixel(Format), SourceBytes))
				Uploaded = false;
			else
			{
				std::vector<uint8_t> vArrayData(SourceBytes);
				int ConvertedWidth = 0;
				int ConvertedHeight = 0;
				Texture2DTo3D(const_cast<uint8_t *>(pArraySourceData), static_cast<int>(ArraySourceWidth), static_cast<int>(ArraySourceHeight), MetalTextureBytesPerPixel(Format), 16, 16, vArrayData.data(), ConvertedWidth, ConvertedHeight);
				Uploaded = ConvertedWidth == static_cast<int>(ArrayWidth) && ConvertedHeight == static_cast<int>(ArrayHeight) && UploadTextureArray(Texture, vArrayData.data(), ArrayWidth, ArrayHeight, 0, METAL_TEXTURE_ARRAY_LAYERS);
			}
			free(pResizedArrayData);
		}
		if(!Uploaded)
		{
			DestroyTexture(Slot);
			return false;
		}
		if(m_CurrentBlitEncoder != nil)
		{
			if(Layout.m_MipLevels > 1 && Texture.m_Texture != nil)
				[m_CurrentBlitEncoder generateMipmapsForTexture:Texture.m_Texture];
			if(ArrayLayout.m_MipLevels > 1 && Texture.m_TextureArray != nil)
				[m_CurrentBlitEncoder generateMipmapsForTexture:Texture.m_TextureArray];
		}
		return true;
	}

	bool UpdateTexture(int Slot, size_t X, size_t Y, size_t Width, size_t Height, EMetalTextureFormat Format, uint8_t *pData)
	{
		if(!EnsureTextureSlot(Slot) || !m_vTextureSlots[Slot].m_Allocated || m_CommandQueue == nil)
			return false;
		STextureSlot &Texture = m_vTextureSlots[Slot];
		if(Texture.m_Format != Format || !MetalValidateSubregion(Texture.m_Width, Texture.m_Height, X, Y, Width, Height))
			return false;
		if(Texture.m_Is2DArray && (X != 0 || Y != 0 || Width != Texture.m_Width || Height != Texture.m_Height))
			return false;
		if(pData == nullptr)
			return true;
		bool Uploaded = Texture.m_Texture == nil || UploadTextureRegion(Texture, X, Y, Width, Height, pData);
		if(Uploaded && Texture.m_Is2DArray)
		{
			const uint8_t *pArraySourceData = pData;
			uint8_t *pResizedArrayData = nullptr;
			if(Texture.m_ArraySourceWidth != Texture.m_Width || Texture.m_ArraySourceHeight != Texture.m_Height)
			{
				pResizedArrayData = ResizeImage(pData, static_cast<int>(Texture.m_Width), static_cast<int>(Texture.m_Height), static_cast<int>(Texture.m_ArraySourceWidth), static_cast<int>(Texture.m_ArraySourceHeight), static_cast<int>(MetalTextureBytesPerPixel(Format)));
				pArraySourceData = pResizedArrayData;
			}
			size_t SourceBytes = 0;
			if(pArraySourceData == nullptr || !MetalCheckedMul(Texture.m_ArraySourceWidth, Texture.m_ArraySourceHeight, SourceBytes) || !MetalCheckedMul(SourceBytes, MetalTextureBytesPerPixel(Format), SourceBytes) || Texture.m_ArraySourceWidth > static_cast<size_t>(std::numeric_limits<int>::max()) || Texture.m_ArraySourceHeight > static_cast<size_t>(std::numeric_limits<int>::max()))
			{
				free(pResizedArrayData);
				return false;
			}
			std::vector<uint8_t> vArrayData(SourceBytes);
			int ConvertedWidth = 0;
			int ConvertedHeight = 0;
			Texture2DTo3D(const_cast<uint8_t *>(pArraySourceData), static_cast<int>(Texture.m_ArraySourceWidth), static_cast<int>(Texture.m_ArraySourceHeight), MetalTextureBytesPerPixel(Format), 16, 16, vArrayData.data(), ConvertedWidth, ConvertedHeight);
			Uploaded = ConvertedWidth == static_cast<int>(Texture.m_ArrayWidth) && ConvertedHeight == static_cast<int>(Texture.m_ArrayHeight) && UploadTextureArray(Texture, vArrayData.data(), Texture.m_ArrayWidth, Texture.m_ArrayHeight, 0, METAL_TEXTURE_ARRAY_LAYERS);
			free(pResizedArrayData);
		}
		if(Uploaded && m_CurrentBlitEncoder != nil)
		{
			if(Texture.m_Layout.m_MipLevels > 1 && Texture.m_Texture != nil)
				[m_CurrentBlitEncoder generateMipmapsForTexture:Texture.m_Texture];
			if(Texture.m_ArrayLayout.m_MipLevels > 1 && Texture.m_TextureArray != nil)
				[m_CurrentBlitEncoder generateMipmapsForTexture:Texture.m_TextureArray];
		}
		return Uploaded;
	}

	static STWGraphicGpu::ETWGraphicsGpuType DeviceType(id<MTLDevice> Device)
		{
			if(str_startswith(Device.name.UTF8String, "Apple"))
				return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INTEGRATED;
	#if defined(CONF_PLATFORM_IOS)
			return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INTEGRATED;
	#else
			return Device.lowPower ? STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INTEGRATED : STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_DISCRETE;
	#endif
		}

	static const char *VendorName(id<MTLDevice> Device)
	{
		const char *pName = Device.name.UTF8String;
		if(str_startswith(pName, "Apple"))
			return "Apple";
		if(str_startswith_nocase(pName, "AMD"))
			return "AMD";
		if(str_startswith_nocase(pName, "Intel"))
			return "Intel";
		if(str_startswith_nocase(pName, "NVIDIA"))
			return "NVIDIA";
		return "Metal";
	}

	void UpdateDrawableSize()
	{
		if(m_pWindow == nullptr || m_pLayer == nullptr)
			return;
		int Width = 0;
		int Height = 0;
		SDL_Metal_GetDrawableSize(m_pWindow, &Width, &Height);
		CAMetalLayer *pLayer = (__bridge CAMetalLayer *)m_pLayer;
		pLayer.drawableSize = CGSizeMake(std::max(Width, 0), std::max(Height, 0));
		m_DrawableWidth = static_cast<uint32_t>(std::max(Width, 0));
		m_DrawableHeight = static_cast<uint32_t>(std::max(Height, 0));
	}

	void ConfigureLayer()
	{
		CAMetalLayer *pLayer = (__bridge CAMetalLayer *)m_pLayer;
		pLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		// macOS 限制 maximumDrawableCount 合法范围为 [2, 3]，不能通过增大
		// drawable 池缓解无 VSync 高帧率下的呈现竞争；改用别的策略。
		pLayer.maximumDrawableCount = 3;
		// The private backbuffer is copied into the drawable with a blit encoder,
		// therefore the drawable must remain usable outside a render pass.
		pLayer.framebufferOnly = NO;
		pLayer.presentsWithTransaction = NO;
		// 仅在关闭 VSync 时允许 drawable 获取超时。开启 VSync 时必须保留
		// 系统背压，否则 nextDrawable 返回 nil 会直接丢掉本帧呈现。
		pLayer.allowsNextDrawableTimeout = !m_VSync;
		// displaySyncEnabled 仅在 macOS 上可用；iOS 的 CAMetalLayer 始终由系统
		// 管理显示同步，不能访问该属性，否则 iOS deployment target 会产生不可用 API。
#if defined(CONF_PLATFORM_MACOS)
		pLayer.displaySyncEnabled = m_VSync;
#endif
		// 缓存主显示器刷新率，供无 VSync 的 present 节流使用。
		// 部分模式（如 ProMotion 可变刷新率）可能返回 0，视为未知。
		m_PresentRefreshRateHz = 0;
#if defined(CONF_PLATFORM_MACOS)
		CGDirectDisplayID DisplayId = CGMainDisplayID();
		CGDisplayModeRef Mode = CGDisplayCopyDisplayMode(DisplayId);
		if(Mode != nullptr)
		{
			const int Rate = (int)std::lround(CGDisplayModeGetRefreshRate(Mode));
			CGDisplayModeRelease(Mode);
			if(Rate > 0)
				m_PresentRefreshRateHz = Rate;
		}
#endif
		int DisplaySync = 0;
#if defined(CONF_PLATFORM_MACOS)
		DisplaySync = pLayer.displaySyncEnabled ? 1 : 0;
#endif
		dbg_msg("gfx/metal", "layer configured: vsync=%d display_sync=%d allows_next_drawable_timeout=%d max_drawables=%lu framebuffer_only=%d presents_with_transaction=%d refresh_hz=%d", m_VSync ? 1 : 0, DisplaySync, pLayer.allowsNextDrawableTimeout ? 1 : 0, static_cast<unsigned long>(pLayer.maximumDrawableCount), pLayer.framebufferOnly ? 1 : 0, pLayer.presentsWithTransaction ? 1 : 0, m_PresentRefreshRateHz);
		UpdateDrawableSize();
	}

	bool VideoCaptureActive() const
	{
#if defined(CONF_VIDEORECORDER)
		const IVideo *pVideo = IVideo::Current();
		return pVideo != nullptr && pVideo->IsRecording();
#else
		return false;
#endif
	}

	void SelectDevice(const char *pConfiguredGpuName)
	{
		id<MTLDevice> pDefaultDevice = MTLCreateSystemDefaultDevice();
		NSArray<id<MTLDevice>> *pDevices = nil;
#if defined(CONF_PLATFORM_MACOS)
		pDevices = MTLCopyAllDevices();
#else
		// MTLCopyAllDevices 在 iOS 18 才可用；QmClient 支持更低 deployment target，
		// iOS 直接使用系统默认设备，避免触发不可用 API。
		pDevices = pDefaultDevice != nil ? @[pDefaultDevice] : @[];
#endif
		id<MTLDevice> pSelectedDevice = pDefaultDevice;
		const bool IsAuto = pConfiguredGpuName == nullptr || str_comp(pConfiguredGpuName, "auto") == 0;
		bool ExplicitMatch = false;

		if(m_pGpuList != nullptr)
		{
			m_pGpuList->m_vGpus.clear();
			m_pGpuList->m_AutoGpu = {};
			for(id<MTLDevice> pDevice in pDevices)
			{
				STWGraphicGpu::STWGraphicGpuItem Gpu = {};
				str_copy(Gpu.m_aName, pDevice.name.UTF8String);
				Gpu.m_GpuType = DeviceType(pDevice);
				m_pGpuList->m_vGpus.push_back(Gpu);
				if(pDefaultDevice == pDevice)
					m_pGpuList->m_AutoGpu = Gpu;
				if(!IsAuto && str_comp(pDevice.name.UTF8String, pConfiguredGpuName) == 0)
				{
					pSelectedDevice = pDevice;
					ExplicitMatch = true;
				}
			}
		}

		if(!IsAuto && !ExplicitMatch)
			log_warn("gfx/metal", "Configured GPU '%s' was not found; using the system default Metal device.", pConfiguredGpuName);

		SetDevice(pSelectedDevice);
		if(m_Device == nil)
		{
			if(m_pVendorString != nullptr)
				str_copy(m_pVendorString, "Metal", gs_MetalGpuInfoStringSize);
			if(m_pVersionString != nullptr)
				str_copy(m_pVersionString, "Metal native", gs_MetalGpuInfoStringSize);
			if(m_pRendererString != nullptr)
				str_copy(m_pRendererString, "No Metal device", gs_MetalGpuInfoStringSize);
		}
		else
		{
			if(m_pVendorString != nullptr)
				str_copy(m_pVendorString, VendorName(m_Device), gs_MetalGpuInfoStringSize);
			if(m_pVersionString != nullptr)
				str_copy(m_pVersionString, "Metal native", gs_MetalGpuInfoStringSize);
			if(m_pRendererString != nullptr)
				str_copy(m_pRendererString, m_Device.name.UTF8String, gs_MetalGpuInfoStringSize);
			CAMetalLayer *pLayer = (__bridge CAMetalLayer *)m_pLayer;
			pLayer.device = m_Device;
			ConfigureLayer();
		}

	#if defined(CONF_PLATFORM_MACOS) && !__has_feature(objc_arc)
		[pDevices release];
	#endif
	#if !__has_feature(objc_arc)
		[pDefaultDevice release];
	#endif
	}

	bool WaitForPresentedReadback()
	{
		if(m_LastPresentedReadback == nil || m_LastPresentedCommandBuffer == nil)
			return false;
		const auto PresentedReadbackWaitStart = m_MetalPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
		[m_LastPresentedCommandBuffer waitUntilCompleted];
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfGpuCompletionWaitCount;
			m_MetalPerfGpuCompletionWaitMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - PresentedReadbackWaitStart).count();
		}
		if(m_LastPresentedCommandBuffer.status == MTLCommandBufferStatusCompleted)
			return true;
		dbg_msg("gfx/metal", "presented readback command buffer incomplete: status=%ld", static_cast<long>(m_LastPresentedCommandBuffer.status));
		return false;
	}

	bool GetPresentedImageData(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData) override
	{
		if(m_LastPresentedReadback == nil || m_LastPresentedCommandBuffer == nil || !WaitForPresentedReadback())
			return false;
		size_t PixelCount = 0;
		size_t DataBytes = 0;
		if(!MetalCheckedMul(static_cast<size_t>(m_LastPresentedReadbackWidth), static_cast<size_t>(m_LastPresentedReadbackHeight), PixelCount) || !MetalCheckedMul(PixelCount, 4, DataBytes))
			return false;
		vDstData.resize(DataBytes);
		const uint8_t *pReadback = static_cast<const uint8_t *>(m_LastPresentedReadback.contents);
		for(uint32_t Y = 0; Y < m_LastPresentedReadbackHeight; ++Y)
			CopyBgraToRgba(vDstData.data() + static_cast<size_t>(Y) * m_LastPresentedReadbackWidth * 4, pReadback + static_cast<size_t>(Y) * m_LastPresentedReadbackRowBytes, m_LastPresentedReadbackWidth);
		Width = m_LastPresentedReadbackWidth;
		Height = m_LastPresentedReadbackHeight;
		Format = CImageInfo::FORMAT_RGBA;
		return true;
	}

	void SetUnsupportedCommandError(const CCommandBuffer::SCommand *pCommand, EGfxErrorType ErrorType = GFX_ERROR_TYPE_RENDER_CMD_FAILED)
	{
		m_Error = {};
		m_Error.m_ErrorType = ErrorType;
		m_Error.m_vErrors.emplace_back(SGfxErrorContainer::SError{
			false,
			"Metal command processor does not implement command " + std::to_string(pCommand->m_Cmd) +
				" (backend_state=" + MetalBackendStateName(m_State) + ", frame_id=" + std::to_string(m_FrameId) + ")"});
	}

	void SetResourceCommandError(const CCommandBuffer::SCommand *pCommand)
	{
		m_Error = {};
		m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_CMD_FAILED;
		m_Error.m_vErrors.emplace_back(SGfxErrorContainer::SError{
			false,
			"Metal buffer resource command failed " + std::to_string(pCommand->m_Cmd) +
				" (backend_state=" + MetalBackendStateName(m_State) + ", frame_id=" + std::to_string(m_FrameId) + ")"});
	}

	void SetSwapError()
	{
		m_Error = {};
		m_Error.m_ErrorType = GFX_ERROR_TYPE_SWAP_FAILED;
		m_Error.m_vErrors.emplace_back(SGfxErrorContainer::SError{
			false,
			std::string("Metal drawable presentation failed (backend_state=") + MetalBackendStateName(m_State) + ", frame_id=" + std::to_string(m_FrameId) + ")"});
	}

	void Cmd_PreInit(const SCommand_PreInit *pCommand)
	{
		m_State = EMetalBackendState::UNINITIALIZED;
		m_MetalPerfRequestedRenderThreads = g_Config.m_GfxRenderThreadCount;
		dbg_msg("gfx/metal", "render command implementation: requested_threads=%d processor_threads=1 (current Metal backend has no parallel encoder partition; this is not a driver workaround)", m_MetalPerfRequestedRenderThreads);
		m_pVendorString = pCommand->m_pVendorString;
		m_pVersionString = pCommand->m_pVersionString;
		m_pRendererString = pCommand->m_pRendererString;
		m_pGpuList = pCommand->m_pGpuList;
		m_pWindow = pCommand->m_pWindow;
		m_VSync = pCommand->m_VSync;
		if(m_MetalView != nullptr)
		{
			SDL_Metal_DestroyView(m_MetalView);
			m_MetalView = nullptr;
			m_pLayer = nullptr;
		}
		if(pCommand->m_pWindow == nullptr)
		{
			if(pCommand->m_pInitError != nullptr)
				*pCommand->m_pInitError = -1;
			if(pCommand->m_pErrStringPtr != nullptr)
				*pCommand->m_pErrStringPtr = "Metal pre-init requires an SDL window";
			return;
		}

		m_MetalView = SDL_Metal_CreateView(pCommand->m_pWindow);
		m_pLayer = m_MetalView != nullptr ? SDL_Metal_GetLayer(m_MetalView) : nullptr;
		if(m_MetalView == nullptr || m_pLayer == nullptr)
		{
			if(m_MetalView != nullptr)
				SDL_Metal_DestroyView(m_MetalView);
			m_MetalView = nullptr;
			m_pLayer = nullptr;
			if(pCommand->m_pInitError != nullptr)
				*pCommand->m_pInitError = -1;
			if(pCommand->m_pErrStringPtr != nullptr)
				*pCommand->m_pErrStringPtr = "SDL could not create the Metal view or layer";
			return;
		}
		SelectDevice(g_Config.m_GfxGpuName);
		if(m_Device == nil)
		{
			if(pCommand->m_pInitError != nullptr)
				*pCommand->m_pInitError = -1;
			if(pCommand->m_pErrStringPtr != nullptr)
				*pCommand->m_pErrStringPtr = "No Metal device is available";
			return;
		}
		m_State = EMetalBackendState::PRE_INITIALIZED;
	}

	void Cmd_Init(const SCommand_Init *pCommand)
	{
		m_pCapabilities = pCommand->m_pCapabilities;
		m_pStorage = pCommand->m_pStorage;
		if(pCommand->m_pReadPresentedImageDataFunc != nullptr)
			*pCommand->m_pReadPresentedImageDataFunc = [this](uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData) {
				return GetPresentedImageData(Width, Height, Format, vDstData);
			};
		m_pBufferMemoryUsage = pCommand->m_pBufferMemoryUsage;
		m_pStreamMemoryUsage = pCommand->m_pStreamMemoryUsage;
		m_pTextureMemoryUsage = pCommand->m_pTextureMemoryUsage;
		m_pStagingMemoryUsage = pCommand->m_pStagingMemoryUsage;
		if(m_pCapabilities != nullptr)
			m_pCapabilities->Reset();
		if(m_pBufferMemoryUsage != nullptr)
			m_pBufferMemoryUsage->store(0, std::memory_order_relaxed);
		if(m_pStreamMemoryUsage != nullptr)
			m_pStreamMemoryUsage->store(0, std::memory_order_relaxed);
		if(m_pTextureMemoryUsage != nullptr)
			m_pTextureMemoryUsage->store(0, std::memory_order_relaxed);
		if(m_pStagingMemoryUsage != nullptr)
			m_pStagingMemoryUsage->store(0, std::memory_order_relaxed);
		if(pCommand->m_pInitError != nullptr)
			*pCommand->m_pInitError = -1;
		if(pCommand->m_pErrStringPtr != nullptr)
			*pCommand->m_pErrStringPtr = "native Metal initialization failed";

		if(m_State != EMetalBackendState::PRE_INITIALIZED || m_Device == nil || m_pLayer == nullptr)
		{
			if(pCommand->m_pErrStringPtr != nullptr)
				*pCommand->m_pErrStringPtr = "Metal init requires a pre-initialized device and layer";
			return;
		}

		m_CommandQueue = [m_Device newCommandQueue];
		m_MultiSamplingCount = SupportedMultiSamplingCount(static_cast<uint32_t>(std::max(g_Config.m_GfxFsaaSamples, 0)));
		const char *pFailedStage = nullptr;
		if(m_CommandQueue == nil)
			pFailedStage = "command queue";
		else if(!LoadShaderLibrary(pCommand))
			pFailedStage = "shader library";
		else if(!CreateSamplerStates())
			pFailedStage = "sampler states";
		else if(!CreatePipelineStates(1, m_aPipelineStates))
			pFailedStage = "single-sample pipelines";
		else if(!CreateGaussianBlurPipeline())
			pFailedStage = "Gaussian blur pipeline";
		else if(m_MultiSamplingCount > 0 && !CreatePipelineStates(m_MultiSamplingCount, m_aMultiSamplePipelineStates))
			pFailedStage = "MSAA pipelines";
		else if(!CreateFrameResources())
			pFailedStage = "frame resources";
		if(pFailedStage != nullptr)
		{
			dbg_msg("gfx/metal", "native Metal initialization failed at %s", pFailedStage);
			ReleaseGpuObjects();
			if(m_pCapabilities != nullptr)
				m_pCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);
			if(pCommand->m_pErrStringPtr != nullptr)
				*pCommand->m_pErrStringPtr = pFailedStage;
			return;
		}
		const bool SingleSampleSdfPipelines = CreateSdfPipelineStates(1, m_aMediaIslandSdfPipelines, m_aRoundedRectSdfPipelines);
		bool SdfPipelinesAvailable = SingleSampleSdfPipelines;
		if(SdfPipelinesAvailable && m_MultiSamplingCount > 0)
			SdfPipelinesAvailable = CreateSdfPipelineStates(m_MultiSamplingCount, m_aMultiSampleMediaIslandSdfPipelines, m_aMultiSampleRoundedRectSdfPipelines);
		if(!SdfPipelinesAvailable)
		{
			dbg_msg("gfx/metal", "Qm SDF pipelines unavailable; using the existing geometry fallback");
			ReleasePipelineStates(m_aMediaIslandSdfPipelines);
			ReleasePipelineStates(m_aRoundedRectSdfPipelines);
			ReleasePipelineStates(m_aMultiSampleMediaIslandSdfPipelines);
			ReleasePipelineStates(m_aMultiSampleRoundedRectSdfPipelines);
		}
		const bool SingleSampleTexturedMsdfPipelines = CreateTexturedMsdfPipelineStates(1, m_aTexturedMsdfPipelines);
		bool TexturedMsdfPipelinesAvailable = SingleSampleTexturedMsdfPipelines;
		if(TexturedMsdfPipelinesAvailable && m_MultiSamplingCount > 0)
			TexturedMsdfPipelinesAvailable = CreateTexturedMsdfPipelineStates(m_MultiSamplingCount, m_aMultiSampleTexturedMsdfPipelines);
		if(!TexturedMsdfPipelinesAvailable)
		{
			dbg_msg("gfx/metal", "Textured MSDF pipelines unavailable; using the existing geometry fallback");
			ReleasePipelineStates(m_aTexturedMsdfPipelines);
			ReleasePipelineStates(m_aMultiSampleTexturedMsdfPipelines);
		}

		m_FrameState.DrainFrames();
		m_CurrentFrameSlot = 0;
		m_FrameId = 0;
		m_GpuFailure.store(false, std::memory_order_relaxed);
		m_GpuFailureFrameId.store(0, std::memory_order_relaxed);
		m_GpuFailureCommandId.store(-1, std::memory_order_relaxed);
		m_GpuFailureStatus.store(0, std::memory_order_relaxed);
		m_LastCommandId.store(-1, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> Lock(m_GpuFailureMutex);
			m_GpuFailureDescription.clear();
		}
		m_State = EMetalBackendState::INITIALIZED;
		if(m_pCapabilities != nullptr)
		{
			m_pCapabilities->m_MipMapping = true;
			m_pCapabilities->m_NPOTTextures = true;
			m_pCapabilities->m_2DArrayTextures = true;
			// Metal implements the buffered tile/quad command paths with the same
			// indexed uint32 stream used by the backend handlers below. Publishing
			// these capabilities avoids the immediate fallback during zoom-heavy
			// scenes and keeps CPU command encoding bounded by the buffer container.
			m_pCapabilities->m_TileBuffering = true;
			m_pCapabilities->m_QuadBuffering = true;
			m_pCapabilities->m_ShaderSupport = true;
			m_pCapabilities->m_QuadContainerBuffering = true;
			m_pCapabilities->m_TextBuffering = true;
			m_pCapabilities->m_TrianglesAsQuads = true;
			m_pCapabilities->m_MediaIslandSdf = SdfPipelinesAvailable;
			m_pCapabilities->m_RoundedRectSdf = SdfPipelinesAvailable;
			m_pCapabilities->m_TexturedMsdf.store(TexturedMsdfPipelinesAvailable, std::memory_order_release);
			m_pCapabilities->m_RenderTargets = true;
			m_pCapabilities->m_RenderTargetGaussianBlur = true;
			m_pCapabilities->m_BackbufferCapture = true;
			m_pCapabilities->m_pRenderTargetSupportReason = "supported";
		}
		if(pCommand->m_pInitError != nullptr)
			*pCommand->m_pInitError = 0;
		if(pCommand->m_pErrStringPtr != nullptr)
			*pCommand->m_pErrStringPtr = nullptr;
	}

	void Cmd_Shutdown(const SCommand_Shutdown *pCommand)
	{
		(void)pCommand;
		if(m_pCapabilities != nullptr)
			m_pCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);
		m_State = EMetalBackendState::SHUTDOWN;
	}

	void Cmd_PostShutdown(const SCommand_PostShutdown *pCommand)
	{
		(void)pCommand;
		WaitForGpuIdle();
		DestroyAllBufferContainers();
		DestroyAllBuffers();
		ReleaseGpuObjects();
		m_FrameState.DrainFrames();
		m_GpuFailure.store(false, std::memory_order_relaxed);
		m_GpuFailureFrameId.store(0, std::memory_order_relaxed);
		m_GpuFailureCommandId.store(-1, std::memory_order_relaxed);
		m_GpuFailureStatus.store(0, std::memory_order_relaxed);
		m_LastCommandId.store(-1, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> Lock(m_GpuFailureMutex);
			m_GpuFailureDescription.clear();
		}
		if(m_MetalView != nullptr)
			SDL_Metal_DestroyView(m_MetalView);
		m_MetalView = nullptr;
		m_pLayer = nullptr;
		m_pWindow = nullptr;
		for(size_t Slot = 0; Slot < m_vTextureSlots.size(); ++Slot)
			DestroyTexture(static_cast<int>(Slot));
		m_vTextureSlots.clear();
		ReleaseCommandQueue();
		ReleaseDevice();
		if(m_pCapabilities != nullptr)
			m_pCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);
		m_State = EMetalBackendState::UNINITIALIZED;
	}

	void ErroneousCleanup() override
	{
		EndActiveEncoders();
		WaitForGpuIdle();
		DestroyAllBufferContainers();
		DestroyAllBuffers();
		ReleaseGpuObjects();
		if(m_pCapabilities != nullptr)
			m_pCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);
		if(m_MetalView != nullptr)
			SDL_Metal_DestroyView(m_MetalView);
		m_MetalView = nullptr;
		m_pLayer = nullptr;
		m_pWindow = nullptr;
		m_State = EMetalBackendState::SHUTDOWN;
	}

	bool BeginRenderEncoder(const MTLClearColor &ClearColor)
	{
		if(m_RenderEncoderStarted)
			return m_CurrentRenderEncoder != nil;
		const int ActiveTargetId = m_RenderTargetState.ActiveTargetId();
		if(ActiveTargetId >= 0 && static_cast<size_t>(ActiveTargetId) < m_vRenderTargets.size())
		{
			const SRenderTarget &Target = m_vRenderTargets[ActiveTargetId];
			return Target.m_Allocated && BeginRenderEncoderForTexture(Target.m_Texture, Target.m_Width, Target.m_Height, ClearColor, MTLLoadActionLoad, false);
		}
		if(m_CurrentCommandBuffer == nil || m_pLayer == nullptr)
			return false;
		// 屏幕绘制先进入私有 backbuffer，避免每个逻辑帧在首个绘制命令
		// 上获取 CAMetalLayer drawable，从而被合成器的刷新节奏背压。
		if(m_DrawableWidth == 0 || m_DrawableHeight == 0)
		{
			m_SkipCurrentFrame = true;
			return false;
		}
		if(!EnsureBackbufferTexture(m_DrawableWidth, m_DrawableHeight))
		{
			m_SkipCurrentFrame = true;
			return false;
		}
		if(m_MultiSamplingCount > 0)
		{
			if(!EnsureMultiSampleTexture(m_DrawableWidth, m_DrawableHeight))
				return false;
			return BeginRenderEncoderForTexture(m_aFrameSlots[m_CurrentFrameSlot].m_MultiSampleTexture, m_DrawableWidth, m_DrawableHeight, ClearColor, CurrentBackbufferHasContents() ? MTLLoadActionLoad : MTLLoadActionClear, true, CurrentBackbufferTexture());
		}
		return BeginRenderEncoderForTexture(CurrentBackbufferTexture(), m_DrawableWidth, m_DrawableHeight, ClearColor, CurrentBackbufferHasContents() ? MTLLoadActionLoad : MTLLoadActionClear, true);
	}

	bool BeginRenderEncoderForTexture(id<MTLTexture> Texture, uint32_t Width, uint32_t Height, const MTLClearColor &ClearColor, MTLLoadAction LoadAction, bool Backbuffer, id<MTLTexture> ResolveTexture = nil)
	{
		if(Texture == nil || Width == 0 || Height == 0 || m_CurrentCommandBuffer == nil || (ResolveTexture != nil && Texture.sampleCount <= 1))
			return false;
		if(m_CurrentBlitEncoder != nil)
		{
			[m_CurrentBlitEncoder endEncoding];
			ReleaseMetalObject(m_CurrentBlitEncoder);
			m_CurrentBlitEncoder = nil;
		}
		MTLRenderPassDescriptor *pPass = [MTLRenderPassDescriptor renderPassDescriptor];
		pPass.colorAttachments[0].texture = Texture;
		pPass.colorAttachments[0].loadAction = LoadAction;
		pPass.colorAttachments[0].storeAction = ResolveTexture != nil ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionStore;
		pPass.colorAttachments[0].resolveTexture = ResolveTexture;
		pPass.colorAttachments[0].clearColor = ClearColor;
		m_CurrentRenderEncoder = RetainMetalObject([m_CurrentCommandBuffer renderCommandEncoderWithDescriptor:pPass]);
		if(m_CurrentRenderEncoder == nil)
			return false;
		m_CurrentRenderEncoder.label = @"QmClient Metal frame";
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfEncoderCount;
			++m_MetalPerfRenderEncoderCount;
		}
		[m_CurrentRenderEncoder setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(Width), static_cast<double>(Height), 0.0, 1.0}];
		m_HasBoundScissorRect = false;
		m_RenderEncoderStarted = true;
		if(Backbuffer)
		{
			SetCurrentBackbufferHasContents(true);
			m_FrameState.ClearReadbackPresented();
		}
		return true;
	}

	bool UploadTextureRegion(STextureSlot &Texture, size_t X, size_t Y, size_t Width, size_t Height, const uint8_t *pData)
	{
		if(pData == nullptr || m_CurrentCommandBuffer == nil || Texture.m_Texture == nil)
			return false;
		const size_t BytesPerPixel = MetalTextureBytesPerPixel(Texture.m_Format);
		if(Width == 0 || Height == 0 || Width > std::numeric_limits<size_t>::max() / BytesPerPixel)
			return false;
		const size_t RowBytes = Width * BytesPerPixel;
		if(Height > std::numeric_limits<size_t>::max() / RowBytes)
			return false;
		const size_t TightBytes = RowBytes * Height;
		// Shared 纹理由 CPU 直接写入，源数据本身是紧凑行布局，不需要为
		// replaceRegion 额外构造 256 字节对齐的 staging 副本。
		if(Texture.m_Texture.storageMode == MTLStorageModeShared)
		{
			[Texture.m_Texture replaceRegion:MTLRegionMake2D((NSUInteger)X, (NSUInteger)Y, (NSUInteger)Width, (NSUInteger)Height) mipmapLevel:0 withBytes:pData bytesPerRow:RowBytes];
			if(m_MetalPerfEnabled)
				m_MetalPerfUploadBytes += TightBytes;
			return true;
		}

		const size_t AlignedRowBytes = (RowBytes + 255) & ~size_t(255);
		if(AlignedRowBytes < RowBytes || Height > std::numeric_limits<size_t>::max() / AlignedRowBytes)
			return false;
		std::vector<uint8_t> vUpload(AlignedRowBytes * Height, 0);
		for(size_t Row = 0; Row < Height; ++Row)
			mem_copy(vUpload.data() + Row * AlignedRowBytes, pData + Row * RowBytes, RowBytes);
		id<MTLBuffer> Staging = [m_Device newBufferWithBytes:vUpload.data() length:vUpload.size() options:MTLResourceStorageModeShared];
		if(Staging == nil)
			return false;
		if(m_MetalPerfEnabled)
			m_MetalPerfUploadBytes += vUpload.size();
		if(m_CurrentRenderEncoder != nil)
		{
			if(m_MetalPerfEnabled)
				++m_MetalPerfUploadInterruptCount;
			[m_CurrentRenderEncoder endEncoding];
			ReleaseMetalObject(m_CurrentRenderEncoder);
			m_CurrentRenderEncoder = nil;
			m_RenderEncoderStarted = false;
		}
		m_HasBoundScissorRect = false;
		if(m_CurrentBlitEncoder == nil)
		{
			m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
			if(m_MetalPerfEnabled && m_CurrentBlitEncoder != nil)
			{
				++m_MetalPerfEncoderCount;
				++m_MetalPerfBlitEncoderCount;
			}
		}
		if(m_CurrentBlitEncoder == nil)
		{
			ReleaseMetalObject(Staging);
			return false;
		}
		[m_CurrentBlitEncoder copyFromBuffer:Staging sourceOffset:0 sourceBytesPerRow:AlignedRowBytes sourceBytesPerImage:AlignedRowBytes * Height sourceSize:MTLSizeMake(Width, Height, 1) toTexture:Texture.m_Texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(X, Y, 0)];
		if(Texture.m_Staging != nil)
		{
			if(m_pStagingMemoryUsage != nullptr)
				m_pStagingMemoryUsage->fetch_sub([Texture.m_Staging length], std::memory_order_relaxed);
			ReleaseMetalObject(Texture.m_Staging);
		}
		Texture.m_Staging = RetainMetalObject(Staging);
		if(m_pStagingMemoryUsage != nullptr)
			m_pStagingMemoryUsage->fetch_add([Staging length], std::memory_order_relaxed);
		ReleaseMetalObject(Staging);
		return true;
	}

	bool UploadTextureArray(STextureSlot &Texture, const uint8_t *pData, size_t Width, size_t Height, size_t LayerStart, size_t LayerCount)
	{
		if(pData == nullptr || m_CurrentCommandBuffer == nil || Texture.m_TextureArray == nil || LayerCount == 0 || LayerStart > METAL_TEXTURE_ARRAY_LAYERS - 1 || LayerCount > METAL_TEXTURE_ARRAY_LAYERS - LayerStart || Width == 0 || Height == 0)
			return false;
		const size_t BytesPerPixel = MetalTextureBytesPerPixel(Texture.m_Format);
		if(Width > std::numeric_limits<size_t>::max() / BytesPerPixel)
			return false;
		const size_t RowBytes = Width * BytesPerPixel;
		if(Height > std::numeric_limits<size_t>::max() / RowBytes)
			return false;
		const size_t TightSliceBytes = RowBytes * Height;
		if(LayerCount > std::numeric_limits<size_t>::max() / TightSliceBytes)
			return false;
		if(Texture.m_TextureArray.storageMode == MTLStorageModeShared)
		{
			for(size_t Layer = 0; Layer < LayerCount; ++Layer)
				[Texture.m_TextureArray replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)Width, (NSUInteger)Height) mipmapLevel:0 slice:(NSUInteger)(LayerStart + Layer) withBytes:pData + Layer * TightSliceBytes bytesPerRow:RowBytes bytesPerImage:TightSliceBytes];
			if(m_MetalPerfEnabled)
				m_MetalPerfUploadBytes += TightSliceBytes * LayerCount;
			return true;
		}

		const size_t AlignedRowBytes = (RowBytes + 255) & ~size_t(255);
		if(AlignedRowBytes < RowBytes || Height > std::numeric_limits<size_t>::max() / AlignedRowBytes)
			return false;
		const size_t SliceBytes = AlignedRowBytes * Height;
		if(LayerCount > std::numeric_limits<size_t>::max() / SliceBytes)
			return false;
		std::vector<uint8_t> vUpload(SliceBytes * LayerCount, 0);
		const size_t SourceSliceBytes = RowBytes * Height;
		for(size_t Layer = 0; Layer < LayerCount; ++Layer)
			for(size_t Row = 0; Row < Height; ++Row)
				mem_copy(vUpload.data() + Layer * SliceBytes + Row * AlignedRowBytes, pData + Layer * SourceSliceBytes + Row * RowBytes, RowBytes);
		id<MTLBuffer> Staging = [m_Device newBufferWithBytes:vUpload.data() length:vUpload.size() options:MTLResourceStorageModeShared];
		if(Staging == nil)
			return false;
		if(m_MetalPerfEnabled)
			m_MetalPerfUploadBytes += vUpload.size();
		if(m_CurrentRenderEncoder != nil)
		{
			if(m_MetalPerfEnabled)
				++m_MetalPerfUploadInterruptCount;
			[m_CurrentRenderEncoder endEncoding];
			ReleaseMetalObject(m_CurrentRenderEncoder);
			m_CurrentRenderEncoder = nil;
			m_RenderEncoderStarted = false;
		}
		if(m_CurrentBlitEncoder == nil)
		{
			m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
			if(m_MetalPerfEnabled && m_CurrentBlitEncoder != nil)
			{
				++m_MetalPerfEncoderCount;
				++m_MetalPerfBlitEncoderCount;
			}
		}
		if(m_CurrentBlitEncoder == nil)
		{
			ReleaseMetalObject(Staging);
			return false;
		}
		for(size_t Layer = 0; Layer < LayerCount; ++Layer)
			[m_CurrentBlitEncoder copyFromBuffer:Staging sourceOffset:Layer * SliceBytes sourceBytesPerRow:AlignedRowBytes sourceBytesPerImage:SliceBytes sourceSize:MTLSizeMake(Width, Height, 1) toTexture:Texture.m_TextureArray destinationSlice:LayerStart + Layer destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
		if(Texture.m_Staging != nil)
		{
			if(m_pStagingMemoryUsage != nullptr)
				m_pStagingMemoryUsage->fetch_sub([Texture.m_Staging length], std::memory_order_relaxed);
			ReleaseMetalObject(Texture.m_Staging);
		}
		Texture.m_Staging = RetainMetalObject(Staging);
		if(m_pStagingMemoryUsage != nullptr)
			m_pStagingMemoryUsage->fetch_add([Staging length], std::memory_order_relaxed);
		ReleaseMetalObject(Staging);
		return true;
	}

	bool DrawPrimitive(const CCommandBuffer::SState &State, EPrimitiveType PrimitiveType, unsigned PrimitiveCount, const GL_SVertex *pVertices)
	{
		if(pVertices == nullptr || PrimitiveCount == 0 || m_CurrentCommandBuffer == nil)
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		size_t VertexCount = 0;
		EMetalPrimitiveType MetalType = EMetalPrimitiveType::TRIANGLES;
		switch(PrimitiveType)
		{
		case EPrimitiveType::LINES: MetalType = EMetalPrimitiveType::LINES; break;
		case EPrimitiveType::QUADS: MetalType = EMetalPrimitiveType::QUADS; break;
		case EPrimitiveType::TRIANGLES: MetalType = EMetalPrimitiveType::TRIANGLES; break;
		}
		if(!MetalPrimitiveVertexCount(MetalType, PrimitiveCount, VertexCount))
			return false;
		const size_t Bytes = VertexCount * sizeof(GL_SVertex);
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || Bytes > gs_StreamBufferSize - VertexOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexOffset, pVertices, Bytes);
		SMetalUniforms Uniforms;
		Uniforms.m_MVP = {};
		const float Width = State.m_ScreenBR.x - State.m_ScreenTL.x;
		const float Height = State.m_ScreenTL.y - State.m_ScreenBR.y;
		if(Width == 0.0f || Height == 0.0f)
			return false;
		Uniforms.m_MVP.m_v[0] = 2.0f / Width;
		Uniforms.m_MVP.m_v[5] = 2.0f / Height;
		Uniforms.m_MVP.m_v[10] = 1.0f;
		Uniforms.m_MVP.m_v[12] = -(State.m_ScreenBR.x + State.m_ScreenTL.x) / Width;
		Uniforms.m_MVP.m_v[13] = -(State.m_ScreenTL.y + State.m_ScreenBR.y) / Height;
		Uniforms.m_MVP.m_v[15] = 1.0f;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		const size_t UniformOffset = (VertexOffset + Bytes + 255) & ~size_t(255);
		if(UniformOffset > gs_StreamBufferSize || sizeof(Uniforms) > gs_StreamBufferSize - UniformOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + UniformOffset, &Uniforms, sizeof(Uniforms));
		const bool Textured = State.m_Texture >= 0 && static_cast<size_t>(State.m_Texture) < m_vTextureSlots.size() && m_vTextureSlots[State.m_Texture].m_Allocated;
		const EMetalBlendMode BlendMode = static_cast<EMetalBlendMode>(State.m_BlendMode);
		id<MTLRenderPipelineState> Pipeline = CurrentPipeline(PipelineIndex(Textured, false, BlendMode));
		if(Pipeline == nil)
			return false;
		[m_CurrentRenderEncoder setRenderPipelineState:Pipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		if(Textured)
		{
			[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[State.m_Texture].m_Texture atIndex:0];
			[m_CurrentRenderEncoder setFragmentSamplerState:State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
		}
		SetScissor(State);
		if(PrimitiveType == EPrimitiveType::QUADS)
				[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:PrimitiveCount * 6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		else
			[m_CurrentRenderEncoder drawPrimitives:PrimitiveType == EPrimitiveType::LINES ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:VertexCount];
		Frame.m_VertexOffset = UniformOffset + sizeof(Uniforms);
		return true;
	}

	bool DrawSdf(const CCommandBuffer::SState &State, EPrimitiveType PrimitiveType, unsigned PrimitiveCount, const CCommandBuffer::SVertex *pVertices, const void *pParams, size_t ParamsSize, const TSdfPipelineStates &Pipelines, id<MTLTexture> BackdropTexture)
	{
		if(pVertices == nullptr || pParams == nullptr || ParamsSize == 0 || PrimitiveCount == 0 || m_CurrentCommandBuffer == nil)
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		size_t VertexCount = 0;
		EMetalPrimitiveType MetalType = EMetalPrimitiveType::TRIANGLES;
		switch(PrimitiveType)
		{
		case EPrimitiveType::LINES: MetalType = EMetalPrimitiveType::LINES; break;
		case EPrimitiveType::QUADS: MetalType = EMetalPrimitiveType::QUADS; break;
		case EPrimitiveType::TRIANGLES: MetalType = EMetalPrimitiveType::TRIANGLES; break;
		}
		if(!MetalPrimitiveVertexCount(MetalType, PrimitiveCount, VertexCount) || VertexCount > std::numeric_limits<size_t>::max() / sizeof(CCommandBuffer::SVertex))
			return false;
		const size_t VertexBytes = VertexCount * sizeof(CCommandBuffer::SVertex);
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		const size_t UniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255);
		const size_t ParamsOffset = (UniformOffset + sizeof(SMetalUniforms) + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || VertexBytes > gs_StreamBufferSize - VertexOffset || UniformOffset > gs_StreamBufferSize || sizeof(SMetalUniforms) > gs_StreamBufferSize - UniformOffset || ParamsOffset > gs_StreamBufferSize || ParamsSize > gs_StreamBufferSize - ParamsOffset)
			return false;
		const EMetalBlendMode BlendMode = static_cast<EMetalBlendMode>(State.m_BlendMode);
		if(static_cast<size_t>(BlendMode) > static_cast<size_t>(EMetalBlendMode::ADDITIVE))
			return false;
		id<MTLRenderPipelineState> Pipeline = Pipelines[static_cast<size_t>(BlendMode)];
		if(Pipeline == nil)
			return false;
		SMetalUniforms Uniforms;
		if(!BuildMvp(State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		uint8_t *pStreamData = static_cast<uint8_t *>(Frame.m_VertexBuffer.contents);
		mem_copy(pStreamData + VertexOffset, pVertices, VertexBytes);
		mem_copy(pStreamData + UniformOffset, &Uniforms, sizeof(Uniforms));
		mem_copy(pStreamData + ParamsOffset, pParams, ParamsSize);
		[m_CurrentRenderEncoder setRenderPipelineState:Pipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentBuffer:Frame.m_VertexBuffer offset:ParamsOffset atIndex:1];
		if(BackdropTexture != nil)
		{
			[m_CurrentRenderEncoder setFragmentTexture:BackdropTexture atIndex:0];
			[m_CurrentRenderEncoder setFragmentSamplerState:m_ClampSampler atIndex:0];
		}
		SetScissor(State);
		if(PrimitiveType == EPrimitiveType::QUADS)
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(PrimitiveCount) * 6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		else
			[m_CurrentRenderEncoder drawPrimitives:PrimitiveType == EPrimitiveType::LINES ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(VertexCount)];
		Frame.m_VertexOffset = ParamsOffset + ParamsSize;
		return true;
	}

	bool DrawMediaIslandSdf(const CCommandBuffer::SCommand_RenderMediaIslandSdf &Command)
	{
		const TSdfPipelineStates &Pipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aMediaIslandSdfPipelines : m_aMultiSampleMediaIslandSdfPipelines;
		IGraphics::SMediaIslandSdfParams Params = Command.m_Params;
		id<MTLTexture> BackdropTexture = nil;
		if(!m_RenderTargetState.IsActive() && Command.m_BackdropTargetId >= 0 && static_cast<size_t>(Command.m_BackdropTargetId) < m_vRenderTargets.size())
		{
			const SRenderTarget &Target = m_vRenderTargets[Command.m_BackdropTargetId];
			if(Target.m_Allocated && Target.m_Texture != nil)
				BackdropTexture = Target.m_Texture;
		}
		if(BackdropTexture == nil)
			Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV] = vec4();
		return DrawSdf(Command.m_State, Command.m_PrimType, Command.m_PrimCount, Command.m_pVertices, &Params, sizeof(Params), Pipelines, BackdropTexture);
	}

	bool DrawTexturedMsdf(const CCommandBuffer::SCommand_RenderTexturedMsdf &Command)
	{
		if(Command.m_pVertices == nullptr || Command.m_PrimCount == 0 || Command.m_MsdfParams.x <= 0.0f || Command.m_MsdfParams.y <= 0.0f || Command.m_MsdfParams.z <= 0.0f || Command.m_State.m_Texture < 0 || static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size())
			return false;
		const STextureSlot &Texture = m_vTextureSlots[Command.m_State.m_Texture];
		if(!Texture.m_Allocated || Texture.m_Texture == nil)
			return false;
		const TSdfPipelineStates &Pipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aTexturedMsdfPipelines : m_aMultiSampleTexturedMsdfPipelines;
		if(!HasSdfPipelines(Pipelines))
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;

		size_t VertexCount = 0;
		EMetalPrimitiveType MetalType = EMetalPrimitiveType::TRIANGLES;
		switch(Command.m_PrimType)
		{
		case EPrimitiveType::LINES: MetalType = EMetalPrimitiveType::LINES; break;
		case EPrimitiveType::QUADS: MetalType = EMetalPrimitiveType::QUADS; break;
		case EPrimitiveType::TRIANGLES: MetalType = EMetalPrimitiveType::TRIANGLES; break;
		}
		if(!MetalPrimitiveVertexCount(MetalType, Command.m_PrimCount, VertexCount) || VertexCount > std::numeric_limits<size_t>::max() / sizeof(CCommandBuffer::SVertex))
			return false;
		const size_t VertexBytes = VertexCount * sizeof(CCommandBuffer::SVertex);
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		const size_t UniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255);
		const size_t ParamsOffset = (UniformOffset + sizeof(SMetalUniforms) + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || VertexBytes > gs_StreamBufferSize - VertexOffset || UniformOffset > gs_StreamBufferSize || sizeof(SMetalUniforms) > gs_StreamBufferSize - UniformOffset || ParamsOffset > gs_StreamBufferSize || sizeof(Command.m_MsdfParams) > gs_StreamBufferSize - ParamsOffset)
			return false;

		SMetalUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		uint8_t *pStreamData = static_cast<uint8_t *>(Frame.m_VertexBuffer.contents);
		mem_copy(pStreamData + VertexOffset, Command.m_pVertices, VertexBytes);
		mem_copy(pStreamData + UniformOffset, &Uniforms, sizeof(Uniforms));
		mem_copy(pStreamData + ParamsOffset, &Command.m_MsdfParams, sizeof(Command.m_MsdfParams));

		const EMetalBlendMode BlendMode = static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode);
		if(static_cast<size_t>(BlendMode) > static_cast<size_t>(EMetalBlendMode::ADDITIVE))
			return false;
		id<MTLRenderPipelineState> Pipeline = Pipelines[static_cast<size_t>(BlendMode)];
		if(Pipeline == nil)
			return false;
		[m_CurrentRenderEncoder setRenderPipelineState:Pipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:Texture.m_Texture atIndex:0];
		[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
		[m_CurrentRenderEncoder setFragmentBuffer:Frame.m_VertexBuffer offset:ParamsOffset atIndex:1];
		SetScissor(Command.m_State);
		if(Command.m_PrimType == EPrimitiveType::QUADS)
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(Command.m_PrimCount) * 6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		else
			[m_CurrentRenderEncoder drawPrimitives:Command.m_PrimType == EPrimitiveType::LINES ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(VertexCount)];
		Frame.m_VertexOffset = ParamsOffset + sizeof(Command.m_MsdfParams);
		return true;
	}

	bool DrawRoundedRectSdf(const CCommandBuffer::SCommand_RenderRoundedRectSdf &Command)
	{
		const TSdfPipelineStates &Pipelines = m_RenderTargetState.IsActive() || m_MultiSamplingCount == 0 ? m_aRoundedRectSdfPipelines : m_aMultiSampleRoundedRectSdfPipelines;
		return DrawSdf(Command.m_State, Command.m_PrimType, Command.m_PrimCount, Command.m_pVertices, &Command.m_Params, sizeof(Command.m_Params), Pipelines, nil);
	}

	bool DrawTex3D(const CCommandBuffer::SCommand_RenderTex3D &Command)
	{
		if(Command.m_pVertices == nullptr || Command.m_PrimCount == 0 || Command.m_State.m_Texture < 0 || static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated || m_vTextureSlots[Command.m_State.m_Texture].m_TextureArray == nil)
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		size_t VertexCount = 0;
		EMetalPrimitiveType MetalType = EMetalPrimitiveType::TRIANGLES;
		switch(Command.m_PrimType)
		{
		case EPrimitiveType::LINES: MetalType = EMetalPrimitiveType::LINES; break;
		case EPrimitiveType::QUADS: MetalType = EMetalPrimitiveType::QUADS; break;
		case EPrimitiveType::TRIANGLES: MetalType = EMetalPrimitiveType::TRIANGLES; break;
		}
		if(!MetalPrimitiveVertexCount(MetalType, Command.m_PrimCount, VertexCount) || VertexCount > std::numeric_limits<size_t>::max() / sizeof(GL_SVertexTex3DStream))
			return false;
		const size_t Bytes = VertexCount * sizeof(GL_SVertexTex3DStream);
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || Bytes > gs_StreamBufferSize - VertexOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexOffset, Command.m_pVertices, Bytes);
		SMetalUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		const size_t UniformOffset = (VertexOffset + Bytes + 255) & ~size_t(255);
		const EMetalBlendMode BlendMode = static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode);
		if(UniformOffset > gs_StreamBufferSize || sizeof(Uniforms) > gs_StreamBufferSize - UniformOffset || static_cast<size_t>(BlendMode) > static_cast<size_t>(EMetalBlendMode::ADDITIVE))
			return false;
		const size_t Pipeline = TextureArrayPipelineIndex(BlendMode);
		id<MTLRenderPipelineState> pPipeline = CurrentPipeline(Pipeline);
		if(pPipeline == nil)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + UniformOffset, &Uniforms, sizeof(Uniforms));
		[m_CurrentRenderEncoder setRenderPipelineState:pPipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_TextureArray atIndex:0];
		[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
		SetScissor(Command.m_State);
		if(Command.m_PrimType == EPrimitiveType::QUADS)
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(Command.m_PrimCount) * 6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		else
			[m_CurrentRenderEncoder drawPrimitives:Command.m_PrimType == EPrimitiveType::LINES ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(VertexCount)];
		Frame.m_VertexOffset = UniformOffset + sizeof(Uniforms);
		return true;
	}

	bool BuildMvp(const CCommandBuffer::SState &State, SMetalFloat4x4 &Mvp) const
	{
		Mvp = {};
		const float Width = State.m_ScreenBR.x - State.m_ScreenTL.x;
		const float Height = State.m_ScreenTL.y - State.m_ScreenBR.y;
		if(Width == 0.0f || Height == 0.0f)
			return false;
		Mvp.m_v[0] = 2.0f / Width;
		Mvp.m_v[5] = 2.0f / Height;
		Mvp.m_v[10] = 1.0f;
		Mvp.m_v[12] = -(State.m_ScreenBR.x + State.m_ScreenTL.x) / Width;
		Mvp.m_v[13] = -(State.m_ScreenTL.y + State.m_ScreenBR.y) / Height;
		Mvp.m_v[15] = 1.0f;
		return true;
	}

	bool AllocateUniformData(const void *pData, size_t DataBytes, size_t &Offset)
	{
		if(pData == nullptr || DataBytes == 0)
			return false;
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		Offset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		if(Offset > gs_StreamBufferSize || DataBytes > gs_StreamBufferSize - Offset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + Offset, pData, DataBytes);
		Frame.m_VertexOffset = Offset + DataBytes;
		return true;
	}

	bool GetContainerDrawResources(int ContainerIndex, size_t RequiredVertexCount, const SBufferContainerSlot *&pContainer, SBufferSlot *&pBuffer)
	{
		if(ContainerIndex < 0 || static_cast<size_t>(ContainerIndex) >= m_vBufferContainers.size())
			return false;
		const SBufferContainerSlot &Container = m_vBufferContainers[ContainerIndex];
		if(!Container.m_Allocated || Container.m_Stride <= 0 || Container.m_VertBufferBindingIndex < 0 || static_cast<size_t>(Container.m_VertBufferBindingIndex) >= m_vBufferSlots.size())
			return false;
		SBufferSlot &Buffer = m_vBufferSlots[Container.m_VertBufferBindingIndex];
		if(!Buffer.m_Allocated || Buffer.m_Buffer == nil || RequiredVertexCount > Buffer.m_DataBytes / static_cast<size_t>(Container.m_Stride))
			return false;
		pContainer = &Container;
		pBuffer = &Buffer;
		return true;
	}

	static bool MatchesContainerAttribute(const SBufferContainerInfo::SAttribute &Attribute, uint32_t DataTypeCount, uint32_t Type, bool Normalized, size_t Offset, uint32_t FuncType)
	{
		return MetalVertexAttributeEquals(SMetalVertexAttribute{static_cast<uint32_t>(Attribute.m_DataTypeCount), Attribute.m_Type, Attribute.m_Normalized, reinterpret_cast<uintptr_t>(Attribute.m_pOffset), Attribute.m_FuncType}, DataTypeCount, Type, Normalized, Offset, FuncType);
	}

	static bool MatchesContainerLayout(const SBufferContainerSlot &Container, bool Textured, bool Quad)
	{
		if(Quad)
		{
			if(Container.m_vAttributes.size() != (Textured ? 3 : 2) || Container.m_Stride != static_cast<int>(Textured ? sizeof(float) * 4 + sizeof(uint8_t) * 4 + sizeof(float) * 2 : sizeof(float) * 4 + sizeof(uint8_t) * 4))
				return false;
			if(!MatchesContainerAttribute(Container.m_vAttributes[0], 4, METAL_GRAPHICS_TYPE_FLOAT, false, 0, 0) || !MatchesContainerAttribute(Container.m_vAttributes[1], 4, METAL_GRAPHICS_TYPE_UNSIGNED_BYTE, true, sizeof(float) * 4, 0))
				return false;
			return !Textured || MatchesContainerAttribute(Container.m_vAttributes[2], 2, METAL_GRAPHICS_TYPE_FLOAT, false, sizeof(float) * 4 + sizeof(uint8_t) * 4, 0);
		}
		if(Container.m_vAttributes.size() != (Textured ? 2 : 1) || Container.m_Stride != static_cast<int>(Textured ? sizeof(float) * 2 + sizeof(uint8_t) * 4 : sizeof(float) * 2))
			return false;
		if(!MatchesContainerAttribute(Container.m_vAttributes[0], 2, METAL_GRAPHICS_TYPE_FLOAT, false, 0, 0))
			return false;
		return !Textured || MatchesContainerAttribute(Container.m_vAttributes[1], 4, METAL_GRAPHICS_TYPE_UNSIGNED_BYTE, false, sizeof(float) * 2, 1);
	}

	static bool MatchesStandardVertexLayout(const SBufferContainerSlot &Container)
	{
		return Container.m_vAttributes.size() == 3 && Container.m_Stride == static_cast<int>(sizeof(GL_SVertex)) &&
			MatchesContainerAttribute(Container.m_vAttributes[0], 2, METAL_GRAPHICS_TYPE_FLOAT, false, offsetof(GL_SVertex, m_Pos), 0) &&
			MatchesContainerAttribute(Container.m_vAttributes[1], 2, METAL_GRAPHICS_TYPE_FLOAT, false, offsetof(GL_SVertex, m_Tex), 0) &&
			MatchesContainerAttribute(Container.m_vAttributes[2], 4, METAL_GRAPHICS_TYPE_UNSIGNED_BYTE, true, offsetof(GL_SVertex, m_Color), 0);
	}

	void SetScissor(const CCommandBuffer::SState &State)
	{
		if(m_CurrentRenderEncoder == nil)
			return;
		uint32_t Width = m_DrawableWidth;
		uint32_t Height = m_DrawableHeight;
		const int ActiveTargetId = m_RenderTargetState.ActiveTargetId();
		if(ActiveTargetId >= 0 && static_cast<size_t>(ActiveTargetId) < m_vRenderTargets.size())
		{
			const SRenderTarget &Target = m_vRenderTargets[ActiveTargetId];
			Width = Target.m_Width;
			Height = Target.m_Height;
		}
		if(!State.m_ClipEnable)
		{
			const MTLScissorRect Rect{0, 0, Width, Height};
			if(!m_HasBoundScissorRect || !ScissorRectsEqual(m_BoundScissorRect, Rect))
			{
				[m_CurrentRenderEncoder setScissorRect:Rect];
				m_BoundScissorRect = Rect;
				m_HasBoundScissorRect = true;
			}
			return;
		}
		const auto ClampCoordinate = [](long long Value, uint32_t Dimension) {
			return std::clamp(Value, 0LL, static_cast<long long>(Dimension));
		};
		const long long ClipLeft = ClampCoordinate(State.m_ClipX, Width);
		const long long ClipRight = ClampCoordinate(static_cast<long long>(State.m_ClipX) + std::max(State.m_ClipW, 0), Width);
		// CGraphics_Threaded::ClipEnable stores the OpenGL-style bottom-left Y
		// coordinate. Metal uses a top-left scissor origin, so convert back to
		// the presented texture coordinate system exactly once here.
		const long long ClipBottom = ClampCoordinate(State.m_ClipY, Height);
		const long long ClipTop = ClampCoordinate(static_cast<long long>(State.m_ClipY) + std::max(State.m_ClipH, 0), Height);
		const MTLScissorRect Rect{
			static_cast<NSUInteger>(ClipLeft),
			static_cast<NSUInteger>(std::max(static_cast<long long>(Height) - ClipTop, 0LL)),
			static_cast<NSUInteger>(std::max(ClipRight - ClipLeft, 0LL)),
			static_cast<NSUInteger>(std::max(ClipTop - ClipBottom, 0LL))};
		if(!m_HasBoundScissorRect || !ScissorRectsEqual(m_BoundScissorRect, Rect))
		{
			[m_CurrentRenderEncoder setScissorRect:Rect];
			m_BoundScissorRect = Rect;
			m_HasBoundScissorRect = true;
		}
	}

	bool PrepareContainerPipeline(const CCommandBuffer::SState &State, size_t Pipeline, SBufferSlot &Buffer, size_t UniformOffset)
	{
		id<MTLRenderPipelineState> pPipeline = CurrentPipeline(Pipeline);
		if(static_cast<size_t>(State.m_BlendMode) > static_cast<size_t>(EMetalBlendMode::ADDITIVE) || pPipeline == nil)
			return false;
		[m_CurrentRenderEncoder setRenderPipelineState:pPipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Buffer.m_Buffer offset:0 atIndex:0];
		Buffer.m_aLastUsedFrameIds[m_CurrentFrameSlot] = m_aFrameSlots[m_CurrentFrameSlot].m_FrameId;
		[m_CurrentRenderEncoder setVertexBuffer:m_aFrameSlots[m_CurrentFrameSlot].m_VertexBuffer offset:UniformOffset atIndex:1];
		SetScissor(State);
		return true;
	}

	static bool DecodeQuadIndexRange(const char *pIndicesOffset, unsigned int DrawCount, size_t &QuadOffset, size_t &QuadCount)
	{
		const size_t OffsetBytes = static_cast<size_t>(reinterpret_cast<uintptr_t>(pIndicesOffset));
		if(OffsetBytes % (6 * sizeof(uint32_t)) != 0 || DrawCount % 6 != 0)
			return false;
		QuadOffset = OffsetBytes / (6 * sizeof(uint32_t));
		QuadCount = DrawCount / 6;
		return true;
	}

	bool DrawTileLayer(const CCommandBuffer::SCommand_RenderTileLayer &Command, bool Border, const vec2 &Offset, const vec2 &Scale)
	{
		if(Command.m_IndicesDrawNum <= 0 || Command.m_pIndicesOffsets == nullptr || Command.m_pDrawCount == nullptr)
			return true;
		const bool Textured = Command.m_State.m_Texture >= 0;
		if(Textured && (static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated || m_vTextureSlots[Command.m_State.m_Texture].m_TextureArray == nil))
			return false;
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(!GetContainerDrawResources(Command.m_BufferContainerIndex, 1, pContainer, pBuffer) || !MatchesContainerLayout(*pContainer, Textured, false))
			return false;
		// 先完成整个 tile 批次的索引范围校验和扩容，再开始编码。
		// 否则批次中途发现容量不足会留下半帧绘制，随后上层看到黑屏/闪烁。
		size_t MaxQuadEnd = 0;
		for(int Index = 0; Index < Command.m_IndicesDrawNum; ++Index)
		{
			size_t QuadOffset = 0;
			size_t QuadCount = 0;
			if(!DecodeQuadIndexRange(Command.m_pIndicesOffsets[Index], Command.m_pDrawCount[Index], QuadOffset, QuadCount) || QuadCount == 0 || QuadOffset > std::numeric_limits<size_t>::max() - QuadCount || QuadOffset > std::numeric_limits<size_t>::max() / 4 || QuadCount > std::numeric_limits<size_t>::max() / 4 || QuadOffset * 4 > pBuffer->m_DataBytes / static_cast<size_t>(pContainer->m_Stride) || QuadCount * 4 > pBuffer->m_DataBytes / static_cast<size_t>(pContainer->m_Stride) - QuadOffset * 4 || QuadOffset + QuadCount > std::numeric_limits<size_t>::max() / 6)
				return false;
			MaxQuadEnd = std::max(MaxQuadEnd, QuadOffset + QuadCount);
		}
		if(MaxQuadEnd == 0 || MaxQuadEnd > std::numeric_limits<size_t>::max() / 6 || !EnsureQuadIndexCapacity(MaxQuadEnd * 6))
			return false;
		SMetalTileUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{Command.m_Color.r, Command.m_Color.g, Command.m_Color.b, Command.m_Color.a}};
		Uniforms.m_Transform = Border ? SMetalFloat4{{Offset.x, Offset.y, Scale.x, Scale.y}} : SMetalFloat4{{0.0f, 0.0f, 1.0f, 1.0f}};
		size_t UniformOffset = 0;
		if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset))
			return false;
		// 完成所有可能失败的 CPU 校验和流缓冲分配后再开启 encoder，
		// 避免缩放场景中半帧编码失败而被上层提交成黑帧。
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		if(!PrepareContainerPipeline(Command.m_State, TilePipelineIndex(Textured, Border, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
			return false;
		[m_CurrentRenderEncoder setFragmentBuffer:m_aFrameSlots[m_CurrentFrameSlot].m_VertexBuffer offset:UniformOffset atIndex:1];
		if(Textured)
		{
			[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_TextureArray atIndex:0];
			[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_TileClampSampler : m_TileRepeatSampler atIndex:0];
		}
		for(int Index = 0; Index < Command.m_IndicesDrawNum; ++Index)
		{
			size_t QuadOffset = 0;
			size_t QuadCount = 0;
			if(!DecodeQuadIndexRange(Command.m_pIndicesOffsets[Index], Command.m_pDrawCount[Index], QuadOffset, QuadCount) || QuadCount == 0 || QuadOffset > std::numeric_limits<size_t>::max() / 4 || QuadCount > std::numeric_limits<size_t>::max() / 4 || QuadOffset * 4 > pBuffer->m_DataBytes / static_cast<size_t>(pContainer->m_Stride) || QuadCount * 4 > pBuffer->m_DataBytes / static_cast<size_t>(pContainer->m_Stride) - QuadOffset * 4 || QuadOffset > std::numeric_limits<size_t>::max() - QuadCount || QuadOffset + QuadCount > std::numeric_limits<size_t>::max() / 6 || (QuadOffset + QuadCount) * 6 > m_QuadIndexCount)
				return false;
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(QuadCount * 6) indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:QuadOffset * 6 * sizeof(uint32_t)];
		}
		return true;
	}

	bool DrawBorderTile(const CCommandBuffer::SCommand_RenderBorderTile &Command)
	{
		if(Command.m_DrawNum > std::numeric_limits<unsigned int>::max() / 6)
			return false;
		CCommandBuffer::SCommand_RenderTileLayer Tile;
		unsigned int DrawCount = Command.m_DrawNum * 6;
		Tile.m_State = Command.m_State;
		Tile.m_Color = Command.m_Color;
		Tile.m_pIndicesOffsets = const_cast<char **>(&Command.m_pIndicesOffset);
		Tile.m_pDrawCount = &DrawCount;
		Tile.m_IndicesDrawNum = 1;
		Tile.m_BufferContainerIndex = Command.m_BufferContainerIndex;
		return DrawTileLayer(Tile, true, Command.m_Offset, Command.m_Scale);
	}

	bool DrawQuadLayer(const CCommandBuffer::SCommand_RenderQuadLayer &Command, bool Grouped)
	{
		if(Command.m_QuadNum == 0 || Command.m_pQuadInfo == nullptr || Command.m_QuadNum > std::numeric_limits<size_t>::max() / 4)
			return Command.m_QuadNum == 0;
		const bool Textured = Command.m_State.m_Texture >= 0;
		if(Textured && (static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated))
			return false;
		const size_t QuadOffset = Command.m_QuadOffset < 0 ? std::numeric_limits<size_t>::max() : static_cast<size_t>(Command.m_QuadOffset);
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(QuadOffset == std::numeric_limits<size_t>::max() || Command.m_QuadNum > std::numeric_limits<size_t>::max() - QuadOffset || QuadOffset + Command.m_QuadNum > std::numeric_limits<size_t>::max() / 4 || !GetContainerDrawResources(Command.m_BufferContainerIndex, (QuadOffset + Command.m_QuadNum) * 4, pContainer, pBuffer) || !MatchesContainerLayout(*pContainer, Textured, true))
			return false;
		if(QuadOffset + Command.m_QuadNum > std::numeric_limits<size_t>::max() / 6 || !EnsureQuadIndexCapacity((QuadOffset + Command.m_QuadNum) * 6))
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		const size_t MaxQuads = Grouped ? Command.m_QuadNum : std::min(Command.m_QuadNum, static_cast<size_t>(METAL_MAX_QUADS));
		for(size_t RenderOffset = 0; RenderOffset < Command.m_QuadNum; RenderOffset += Grouped ? Command.m_QuadNum : MaxQuads)
		{
			const size_t RenderCount = Grouped ? Command.m_QuadNum : std::min(MaxQuads, Command.m_QuadNum - RenderOffset);
			SMetalQuadUniforms Uniforms;
			if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
				return false;
			const size_t InfoIndex = Grouped ? 0 : RenderOffset;
			for(size_t Index = 0; Index < (Grouped ? 1 : RenderCount); ++Index)
			{
				const SQuadRenderInfo &Info = Command.m_pQuadInfo[InfoIndex + Index];
				Uniforms.m_aColors[Index] = {{Info.m_Color.r, Info.m_Color.g, Info.m_Color.b, Info.m_Color.a}};
				Uniforms.m_aOffsetsRotations[Index] = {{Info.m_Offsets.x, Info.m_Offsets.y, Info.m_Rotation, 0.0f}};
			}
			Uniforms.m_QuadOffset = static_cast<int>(QuadOffset + RenderOffset);
			size_t UniformOffset = 0;
			if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset) || !PrepareContainerPipeline(Command.m_State, QuadPipelineIndex(Textured, Grouped, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
				return false;
			if(Textured)
			{
				[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_Texture atIndex:0];
				[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
			}
			if((QuadOffset + RenderOffset + RenderCount) * 6 > m_QuadIndexCount)
				return false;
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(RenderCount * 6) indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:(QuadOffset + RenderOffset) * 6 * sizeof(uint32_t)];
			if(Grouped)
				break;
		}
		return true;
	}

	bool GetStandardQuadDrawResources(int ContainerIndex, const void *pOffset, unsigned int DrawNum, size_t &QuadOffset, size_t &QuadCount, const SBufferContainerSlot *&pContainer, SBufferSlot *&pBuffer)
	{
		if(!DecodeQuadIndexRange(static_cast<const char *>(pOffset), DrawNum, QuadOffset, QuadCount) || QuadCount == 0 || QuadOffset > std::numeric_limits<size_t>::max() - QuadCount || QuadOffset + QuadCount > std::numeric_limits<size_t>::max() / 4)
			return false;
		const size_t QuadEnd = QuadOffset + QuadCount;
		if(QuadEnd > std::numeric_limits<size_t>::max() / 6 || !EnsureQuadIndexCapacity(QuadEnd * 6))
			return false;
		if(!GetContainerDrawResources(ContainerIndex, QuadEnd * 4, pContainer, pBuffer) || !MatchesStandardVertexLayout(*pContainer))
			return false;
		return true;
	}

	bool DrawQuadContainer(const CCommandBuffer::SCommand_RenderQuadContainer &Command)
	{
		if(Command.m_DrawNum == 0)
			return true;
		const bool Textured = Command.m_State.m_Texture >= 0;
		if(Textured && (static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated))
			return false;
		size_t QuadOffset = 0;
		size_t QuadCount = 0;
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(!GetStandardQuadDrawResources(Command.m_BufferContainerIndex, Command.m_pOffset, Command.m_DrawNum, QuadOffset, QuadCount, pContainer, pBuffer))
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		SMetalUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		size_t UniformOffset = 0;
		if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset) || !PrepareContainerPipeline(Command.m_State, PipelineIndex(Textured, false, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
			return false;
		if(Textured)
		{
			[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_Texture atIndex:0];
			[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
		}
		[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:Command.m_DrawNum indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:QuadOffset * 6 * sizeof(uint32_t)];
		(void)pContainer;
		(void)QuadCount;
		return true;
	}

	bool DrawText(const CCommandBuffer::SCommand_RenderText &Command)
	{
		if(Command.m_DrawNum == 0)
			return true;
		if(Command.m_DrawNum < 0 || Command.m_TextureSize <= 0 || Command.m_BufferContainerIndex < 0)
			return false;
		if(Command.m_TextTextureIndex < 0 || Command.m_TextOutlineTextureIndex < 0 || static_cast<size_t>(Command.m_TextTextureIndex) >= m_vTextureSlots.size() || static_cast<size_t>(Command.m_TextOutlineTextureIndex) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_TextTextureIndex].m_Allocated || !m_vTextureSlots[Command.m_TextOutlineTextureIndex].m_Allocated)
			return false;
		size_t QuadOffset = 0;
		size_t QuadCount = 0;
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(!GetStandardQuadDrawResources(Command.m_BufferContainerIndex, nullptr, static_cast<unsigned int>(Command.m_DrawNum), QuadOffset, QuadCount, pContainer, pBuffer))
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		SMetalTextUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{Command.m_TextColor.r, Command.m_TextColor.g, Command.m_TextColor.b, Command.m_TextColor.a}};
		Uniforms.m_OutlineColor = {{Command.m_TextOutlineColor.r, Command.m_TextOutlineColor.g, Command.m_TextOutlineColor.b, Command.m_TextOutlineColor.a}};
		Uniforms.m_Params = {{static_cast<float>(Command.m_TextureSize), 0.0f, 0.0f, 0.0f}};
		size_t UniformOffset = 0;
		if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset) || !PrepareContainerPipeline(Command.m_State, PipelineIndex(true, true, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
			return false;
		[m_CurrentRenderEncoder setFragmentBuffer:m_aFrameSlots[m_CurrentFrameSlot].m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_TextTextureIndex].m_Texture atIndex:0];
		[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_TextOutlineTextureIndex].m_Texture atIndex:1];
		[m_CurrentRenderEncoder setFragmentSamplerState:m_ClampSampler atIndex:0];
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(Command.m_DrawNum) indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:QuadOffset * 6 * sizeof(uint32_t)];
		(void)pContainer;
		(void)QuadCount;
		return true;
	}

	bool DrawQuadContainerEx(const CCommandBuffer::SCommand_RenderQuadContainerEx &Command)
	{
		if(Command.m_DrawNum == 0)
			return true;
		const bool Textured = Command.m_State.m_Texture >= 0;
		if(Textured && (static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated))
			return false;
		size_t QuadOffset = 0;
		size_t QuadCount = 0;
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(!GetStandardQuadDrawResources(Command.m_BufferContainerIndex, Command.m_pOffset, Command.m_DrawNum, QuadOffset, QuadCount, pContainer, pBuffer))
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		SMetalQuadContainerUniforms Uniforms;
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_CenterRotation = {{Command.m_Center.x, Command.m_Center.y, Command.m_Rotation, 0.0f}};
		Uniforms.m_VertexColor = {{Command.m_VertexColor.r, Command.m_VertexColor.g, Command.m_VertexColor.b, Command.m_VertexColor.a}};
		size_t UniformOffset = 0;
		if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset) || !PrepareContainerPipeline(Command.m_State, QuadContainerExPipelineIndex(Textured, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
			return false;
		[m_CurrentRenderEncoder setFragmentBuffer:m_aFrameSlots[m_CurrentFrameSlot].m_VertexBuffer offset:UniformOffset atIndex:1];
		if(Textured)
		{
			[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_Texture atIndex:0];
			[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
		}
		[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:Command.m_DrawNum indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:QuadOffset * 6 * sizeof(uint32_t)];
		(void)pContainer;
		(void)QuadCount;
		return true;
	}

	bool DrawQuadContainerAsSpriteMultiple(const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple &Command)
	{
		if(Command.m_DrawNum == 0 || Command.m_DrawCount == 0)
			return true;
		if(Command.m_pRenderInfo == nullptr)
			return false;
		const bool Textured = Command.m_State.m_Texture >= 0;
		if(Textured && (static_cast<size_t>(Command.m_State.m_Texture) >= m_vTextureSlots.size() || !m_vTextureSlots[Command.m_State.m_Texture].m_Allocated))
			return false;
		size_t QuadOffset = 0;
		size_t QuadCount = 0;
		const SBufferContainerSlot *pContainer = nullptr;
		SBufferSlot *pBuffer = nullptr;
		if(!GetStandardQuadDrawResources(Command.m_BufferContainerIndex, Command.m_pOffset, Command.m_DrawNum, QuadOffset, QuadCount, pContainer, pBuffer) || QuadCount != 1)
			return false;
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		SMetalSpriteMultipleUniforms Uniforms{};
		if(!BuildMvp(Command.m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Center = {{Command.m_Center.x, Command.m_Center.y, 0.0f, 0.0f}};
		Uniforms.m_VertexColor = {{Command.m_VertexColor.r, Command.m_VertexColor.g, Command.m_VertexColor.b, Command.m_VertexColor.a}};
		for(unsigned int RenderOffset = 0; RenderOffset < Command.m_DrawCount;)
		{
			const unsigned int RenderCount = std::min(Command.m_DrawCount - RenderOffset, static_cast<unsigned int>(METAL_MAX_SPRITES));
			for(unsigned int Index = 0; Index < RenderCount; ++Index)
			{
				const IGraphics::SRenderSpriteInfo &Info = Command.m_pRenderInfo[RenderOffset + Index];
				Uniforms.m_aRenderInfo[Index] = {{Info.m_Pos.x, Info.m_Pos.y, Info.m_Scale, Info.m_Rotation}};
			}
			size_t UniformOffset = 0;
			if(!AllocateUniformData(&Uniforms, sizeof(Uniforms), UniformOffset) || !PrepareContainerPipeline(Command.m_State, SpriteMultiplePipelineIndex(Textured, static_cast<EMetalBlendMode>(Command.m_State.m_BlendMode)), *pBuffer, UniformOffset))
				return false;
			if(Textured)
			{
				[m_CurrentRenderEncoder setFragmentTexture:m_vTextureSlots[Command.m_State.m_Texture].m_Texture atIndex:0];
				[m_CurrentRenderEncoder setFragmentSamplerState:Command.m_State.m_WrapMode == EWrapMode::CLAMP ? m_ClampSampler : m_RepeatSampler atIndex:0];
			}
			[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:Command.m_DrawNum indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:QuadOffset * 6 * sizeof(uint32_t) instanceCount:RenderCount baseVertex:0 baseInstance:0];
			RenderOffset += RenderCount;
		}
		(void)pContainer;
		return true;
	}

	void Cmd_Clear(const CCommandBuffer::SCommand_Clear *pCommand)
	{
		if(m_MetalPerfEnabled)
			++m_MetalPerfClearCount;
		EndActiveEncoders();
		const MTLClearColor ClearColor = MTLClearColorMake(pCommand->m_Color.r, pCommand->m_Color.g, pCommand->m_Color.b, 0.0);
		const int ActiveTargetId = m_RenderTargetState.ActiveTargetId();
		if(ActiveTargetId >= 0 && static_cast<size_t>(ActiveTargetId) < m_vRenderTargets.size())
		{
			const SRenderTarget &Target = m_vRenderTargets[ActiveTargetId];
			if(Target.m_Allocated && BeginRenderEncoderForTexture(Target.m_Texture, Target.m_Width, Target.m_Height, ClearColor, MTLLoadActionClear, false))
				return;
		}
		else
		{
			SetCurrentBackbufferHasContents(false);
			if(BeginRenderEncoder(ClearColor))
				return;
		}
		if(m_CurrentRenderEncoder == nil && !m_SkipCurrentFrame)
			SetUnsupportedCommandError(pCommand);
	}

	void Cmd_Render(const CCommandBuffer::SCommand_Render *pCommand)
	{
		if(!DrawPrimitive(pCommand->m_State, pCommand->m_PrimType, pCommand->m_PrimCount, pCommand->m_pVertices) && !m_SkipCurrentFrame)
			SetUnsupportedCommandError(pCommand);
	}

	bool Cmd_RenderTarget_Create(const CCommandBuffer::SCommand_RenderTarget_Create *pCommand)
	{
		if(pCommand->m_TargetId < 0 || pCommand->m_Width <= 0 || pCommand->m_Height <= 0)
			return true;
		return CreateRenderTarget(pCommand->m_TargetId, pCommand->m_Width, pCommand->m_Height);
	}

	bool Cmd_RenderTarget_Destroy(const CCommandBuffer::SCommand_RenderTarget_Destroy *pCommand)
	{
		if(pCommand->m_TargetId < 0 || static_cast<size_t>(pCommand->m_TargetId) >= m_vRenderTargets.size() || !m_RenderTargetState.CanDestroy(pCommand->m_TargetId))
			return true;
		DestroyRenderTarget(pCommand->m_TargetId);
		return true;
	}

	bool Cmd_RenderTarget_Begin(const CCommandBuffer::SCommand_RenderTarget_Begin *pCommand)
	{
		if(m_MetalPerfEnabled)
			++m_MetalPerfRenderTargetSwitchCount;
		if(pCommand->m_TargetId < 0 || static_cast<size_t>(pCommand->m_TargetId) >= m_vRenderTargets.size() || m_RenderTargetState.IsActive())
			return true;
		const SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(!Target.m_Allocated || Target.m_Texture == nil)
			return true;
		EndActiveEncoders();
		if(!m_RenderTargetState.Begin(pCommand->m_TargetId))
			return true;
		if(BeginRenderEncoderForTexture(Target.m_Texture, Target.m_Width, Target.m_Height, MTLClearColorMake(pCommand->m_ClearColor.r, pCommand->m_ClearColor.g, pCommand->m_ClearColor.b, pCommand->m_ClearColor.a), MTLLoadActionClear, false))
			return true;
		m_RenderTargetState.Reset();
		return false;
	}

	bool Cmd_RenderTarget_End(const CCommandBuffer::SCommand_RenderTarget_End *pCommand)
	{
		(void)pCommand;
		if(m_MetalPerfEnabled)
			++m_MetalPerfRenderTargetSwitchCount;
		const int ActiveTargetId = m_RenderTargetState.ActiveTargetId();
		if(ActiveTargetId < 0 || static_cast<size_t>(ActiveTargetId) >= m_vRenderTargets.size())
			return true;
		EndActiveEncoders();
		m_RenderTargetState.End();
		return true;
	}

	bool Cmd_RenderTarget_Draw(const CCommandBuffer::SCommand_RenderTarget_Draw *pCommand)
	{
		if(pCommand->m_TargetId < 0 || static_cast<size_t>(pCommand->m_TargetId) >= m_vRenderTargets.size() || pCommand->m_W <= 0.0f || pCommand->m_H <= 0.0f || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0 || !m_RenderTargetState.CanDraw(pCommand->m_TargetId))
			return true;
		const size_t PrimCount = static_cast<size_t>(pCommand->m_PrimCount);
		if(PrimCount > std::numeric_limits<size_t>::max() / (4 * sizeof(CCommandBuffer::SVertex)))
			return false;
		const SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(!Target.m_Allocated || Target.m_Texture == nil || m_CurrentCommandBuffer == nil)
			return false;
		const size_t VertexBytes = PrimCount * 4 * sizeof(CCommandBuffer::SVertex);
		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || VertexBytes > gs_StreamBufferSize - VertexOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexOffset, pCommand->m_pVertices, VertexBytes);
		SMetalUniforms Uniforms;
		if(!BuildMvp(pCommand->m_State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		const size_t UniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255);
		const EMetalBlendMode BlendMode = static_cast<EMetalBlendMode>(pCommand->m_State.m_BlendMode);
		if(UniformOffset > gs_StreamBufferSize || sizeof(Uniforms) > gs_StreamBufferSize - UniformOffset || static_cast<size_t>(BlendMode) > static_cast<size_t>(EMetalBlendMode::ADDITIVE))
			return false;
		id<MTLRenderPipelineState> Pipeline = CurrentPipeline(PipelineIndex(true, false, BlendMode));
		if(Pipeline == nil || !BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + UniformOffset, &Uniforms, sizeof(Uniforms));
		[m_CurrentRenderEncoder setRenderPipelineState:Pipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:Target.m_Texture atIndex:0];
		[m_CurrentRenderEncoder setFragmentSamplerState:m_ClampSampler atIndex:0];
		SetScissor(pCommand->m_State);
		[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(pCommand->m_PrimCount) * 6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		Frame.m_VertexOffset = UniformOffset + sizeof(Uniforms);
		return true;
	}

	bool Cmd_RenderTarget_GaussianBlurPass(const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *pCommand)
	{
		if(!m_RenderTargetState.IsActive() || m_GaussianBlurPipeline == nil || pCommand->m_SourceTargetId < 0 || pCommand->m_Radius < 1 || pCommand->m_Radius > IGraphics::GAUSSIAN_BLUR_MAX_RADIUS)
			return false;
		const int DestinationTargetId = m_RenderTargetState.ActiveTargetId();
		if(DestinationTargetId < 0 || pCommand->m_SourceTargetId == DestinationTargetId || static_cast<size_t>(pCommand->m_SourceTargetId) >= m_vRenderTargets.size() || static_cast<size_t>(DestinationTargetId) >= m_vRenderTargets.size())
			return false;
		const SRenderTarget &Source = m_vRenderTargets[pCommand->m_SourceTargetId];
		const SRenderTarget &Destination = m_vRenderTargets[DestinationTargetId];
		const bool DualKawase = pCommand->m_Mode == IGraphics::EBlurMode::DUAL;
		if(!Source.m_Allocated || !Destination.m_Allocated || Source.m_Texture == nil || Destination.m_Texture == nil || Source.m_Width == 0 || Source.m_Height == 0 || Destination.m_Width == 0 || Destination.m_Height == 0 ||
			(!DualKawase && (Source.m_Width != Destination.m_Width || Source.m_Height != Destination.m_Height)) ||
			(DualKawase && (pCommand->m_Upsample ? (Source.m_Width >= Destination.m_Width || Source.m_Height >= Destination.m_Height) : (Source.m_Width <= Destination.m_Width || Source.m_Height <= Destination.m_Height))) || m_CurrentCommandBuffer == nil)
			return false;

		std::array<CCommandBuffer::SVertex, 4> aVertices{};
		aVertices[0].m_Pos = vec2(-1.0f, -1.0f);
		aVertices[0].m_Tex = vec2(0.0f, 0.0f);
		aVertices[1].m_Pos = vec2(1.0f, -1.0f);
		aVertices[1].m_Tex = vec2(1.0f, 0.0f);
		aVertices[2].m_Pos = vec2(1.0f, 1.0f);
		aVertices[2].m_Tex = vec2(1.0f, 1.0f);
		aVertices[3].m_Pos = vec2(-1.0f, 1.0f);
		aVertices[3].m_Tex = vec2(0.0f, 1.0f);
		for(CCommandBuffer::SVertex &Vertex : aVertices)
			Vertex.m_Color = CCommandBuffer::SColor{255, 255, 255, 255};

		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		const size_t VertexBytes = sizeof(aVertices);
		const size_t VertexUniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255);
		const size_t FragmentUniformOffset = (VertexUniformOffset + sizeof(SMetalUniforms) + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || VertexBytes > gs_StreamBufferSize - VertexOffset || VertexUniformOffset > gs_StreamBufferSize || sizeof(SMetalUniforms) > gs_StreamBufferSize - VertexUniformOffset || FragmentUniformOffset > gs_StreamBufferSize || sizeof(SMetalGaussianBlurUniforms) > gs_StreamBufferSize - FragmentUniformOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexOffset, aVertices.data(), VertexBytes);
		SMetalUniforms VertexUniforms{};
		VertexUniforms.m_MVP.m_v[0] = 1.0f;
		VertexUniforms.m_MVP.m_v[5] = 1.0f;
		VertexUniforms.m_MVP.m_v[10] = 1.0f;
		VertexUniforms.m_MVP.m_v[15] = 1.0f;
		VertexUniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		SMetalGaussianBlurUniforms FragmentUniforms{};
		const bool Gaussian = pCommand->m_Mode == IGraphics::EBlurMode::GAUSSIAN;
		FragmentUniforms.m_TexelOffsetRadius = {{Gaussian && !pCommand->m_Horizontal ? 0.0f : 1.0f / static_cast<float>(Source.m_Width), Gaussian && pCommand->m_Horizontal ? 0.0f : 1.0f / static_cast<float>(Source.m_Height), static_cast<float>(pCommand->m_Radius), static_cast<float>(pCommand->m_Mode)}};
		mem_copy(FragmentUniforms.m_Weights0.m_v, pCommand->m_aWeights.data(), sizeof(pCommand->m_aWeights));
		FragmentUniforms.m_Weights2.m_v[3] = static_cast<float>(pCommand->m_Pass);
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexUniformOffset, &VertexUniforms, sizeof(VertexUniforms));
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + FragmentUniformOffset, &FragmentUniforms, sizeof(FragmentUniforms));

		EndActiveEncoders();
		if(!BeginRenderEncoder({0.0, 0.0, 0.0, 0.0}))
			return false;
		[m_CurrentRenderEncoder setRenderPipelineState:m_GaussianBlurPipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexUniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentBuffer:Frame.m_VertexBuffer offset:FragmentUniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:Source.m_Texture atIndex:0];
		[m_CurrentRenderEncoder setFragmentSamplerState:m_ClampSampler atIndex:0];
		[m_CurrentRenderEncoder setScissorRect:MTLScissorRect{0, 0, Destination.m_Width, Destination.m_Height}];
		m_HasBoundScissorRect = false;
		[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		Frame.m_VertexOffset = FragmentUniformOffset + sizeof(FragmentUniforms);
		return true;
	}

	bool Cmd_RenderTarget_Readback(const CCommandBuffer::SCommand_RenderTarget_Readback *pCommand)
	{
		if(pCommand->m_TargetId < 0 || pCommand->m_pImage == nullptr || static_cast<size_t>(pCommand->m_TargetId) >= m_vRenderTargets.size() || m_RenderTargetState.IsActive())
			return true;
		const SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(!Target.m_Allocated || Target.m_Texture == nil || Target.m_Width == 0 || Target.m_Height == 0)
			return true;
		size_t RowBytes = 0;
		id<MTLBuffer> Readback = EncodeTextureReadback(Target.m_Texture, Target.m_Width, Target.m_Height, RowBytes);
		if(Readback == nil)
			return false;
		id<MTLBuffer> PresentedReadback = nil;
		size_t PresentedRowBytes = 0;
		const bool HasDrawable = HasValidBackbufferContents();
		if(HasDrawable)
		{
			PresentedReadback = EncodeDrawableReadback(m_DrawableWidth, m_DrawableHeight, PresentedRowBytes);
			if(PresentedReadback == nil)
			{
				ReleaseMetalObject(Readback);
				return false;
			}
		}
		const bool FrameCompleted = HasDrawable ? CommitCurrentFrame(true, true) : CommitCurrentFrameForReadback();
		if(!FrameCompleted)
		{
			ReleaseMetalObject(Readback);
			ReleaseMetalObject(PresentedReadback);
			return false;
		}
		if(HasDrawable)
		{
			StoreLastPresentedReadback(PresentedReadback, m_DrawableWidth, m_DrawableHeight, PresentedRowBytes);
			ReleaseMetalObject(m_LastPresentedCommandBuffer);
			m_LastPresentedCommandBuffer = RetainMetalObject(m_CurrentCommandBuffer);
			m_FrameState.MarkReadbackPresented();
			ReleaseMetalObject(PresentedReadback);
		}
		size_t PixelCount = 0;
		size_t DataBytes = 0;
		if(!MetalCheckedMul(static_cast<size_t>(Target.m_Width), static_cast<size_t>(Target.m_Height), PixelCount) || !MetalCheckedMul(PixelCount, 4, DataBytes))
		{
			ReleaseMetalObject(Readback);
			return false;
		}
		uint8_t *pImageData = static_cast<uint8_t *>(malloc(DataBytes));
		if(pImageData == nullptr)
		{
			ReleaseMetalObject(Readback);
			return false;
		}
		const uint8_t *pReadback = static_cast<const uint8_t *>(Readback.contents);
		for(uint32_t Y = 0; Y < Target.m_Height; ++Y)
			CopyBgraToRgba(pImageData + static_cast<size_t>(Y) * Target.m_Width * 4, pReadback + static_cast<size_t>(Y) * RowBytes, Target.m_Width);
		pCommand->m_pImage->m_Width = Target.m_Width;
		pCommand->m_pImage->m_Height = Target.m_Height;
		pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
		pCommand->m_pImage->m_pData = pImageData;
		ReleaseMetalObject(Readback);
		return true;
	}

	bool Cmd_RenderTarget_CaptureBackbuffer(const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *pCommand)
	{
		if(m_RenderTargetState.IsActive() || !HasValidBackbufferContents() || pCommand->m_TargetId < 0 || static_cast<size_t>(pCommand->m_TargetId) >= m_vRenderTargets.size())
			return true;
		const SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(!Target.m_Allocated || Target.m_Texture == nil || Target.m_Width == 0 || Target.m_Height == 0)
			return true;

		std::array<CCommandBuffer::SVertex, 4> aVertices{};
		aVertices[0].m_Pos = vec2(0.0f, 0.0f);
		aVertices[0].m_Tex = vec2(0.0f, 0.0f);
		aVertices[1].m_Pos = vec2(static_cast<float>(Target.m_Width), 0.0f);
		aVertices[1].m_Tex = vec2(1.0f, 0.0f);
		aVertices[2].m_Pos = vec2(static_cast<float>(Target.m_Width), static_cast<float>(Target.m_Height));
		aVertices[2].m_Tex = vec2(1.0f, 1.0f);
		aVertices[3].m_Pos = vec2(0.0f, static_cast<float>(Target.m_Height));
		aVertices[3].m_Tex = vec2(0.0f, 1.0f);
		for(CCommandBuffer::SVertex &Vertex : aVertices)
			Vertex.m_Color = CCommandBuffer::SColor{255, 255, 255, 255};

		SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		const size_t VertexOffset = (Frame.m_VertexOffset + 255) & ~size_t(255);
		const size_t VertexBytes = sizeof(aVertices);
		const size_t UniformOffset = (VertexOffset + VertexBytes + 255) & ~size_t(255);
		if(VertexOffset > gs_StreamBufferSize || VertexBytes > gs_StreamBufferSize - VertexOffset || UniformOffset > gs_StreamBufferSize || sizeof(SMetalUniforms) > gs_StreamBufferSize - UniformOffset)
			return false;
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + VertexOffset, aVertices.data(), VertexBytes);
		SMetalUniforms Uniforms;
		CCommandBuffer::SState State{};
		State.m_ScreenTL = vec2(0.0f, 0.0f);
		State.m_ScreenBR = vec2(static_cast<float>(Target.m_Width), static_cast<float>(Target.m_Height));
		if(!BuildMvp(State, Uniforms.m_MVP))
			return false;
		Uniforms.m_Color = {{1.0f, 1.0f, 1.0f, 1.0f}};
		mem_copy(static_cast<uint8_t *>(Frame.m_VertexBuffer.contents) + UniformOffset, &Uniforms, sizeof(Uniforms));
		id<MTLRenderPipelineState> Pipeline = m_aPipelineStates[PipelineIndex(true, false, EMetalBlendMode::NONE)];
		if(Pipeline == nil || m_CurrentCommandBuffer == nil)
			return false;
		if(CurrentBackbufferTexture() == nil && !BeginRenderEncoder({0.0, 0.0, 0.0, 1.0}))
			return false;
		EndActiveEncoders();
		if(CurrentBackbufferTexture() == nil || !BeginRenderEncoderForTexture(Target.m_Texture, Target.m_Width, Target.m_Height, MTLClearColorMake(0.0, 0.0, 0.0, 1.0), MTLLoadActionClear, false))
			return false;
		[m_CurrentRenderEncoder setRenderPipelineState:Pipeline];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:VertexOffset atIndex:0];
		[m_CurrentRenderEncoder setVertexBuffer:Frame.m_VertexBuffer offset:UniformOffset atIndex:1];
		[m_CurrentRenderEncoder setFragmentTexture:CurrentBackbufferTexture() atIndex:0];
		[m_CurrentRenderEncoder setFragmentSamplerState:m_ClampSampler atIndex:0];
		[m_CurrentRenderEncoder setScissorRect:MTLScissorRect{0, 0, Target.m_Width, Target.m_Height}];
		m_HasBoundScissorRect = false;
		[m_CurrentRenderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:6 indexType:MTLIndexTypeUInt32 indexBuffer:m_QuadIndexBuffer indexBufferOffset:0];
		Frame.m_VertexOffset = UniformOffset + sizeof(Uniforms);
		EndActiveEncoders();
		return true;
	}

	void EndActiveEncoders()
	{
		if(m_CurrentRenderEncoder != nil)
		{
			[m_CurrentRenderEncoder endEncoding];
			ReleaseMetalObject(m_CurrentRenderEncoder);
			m_CurrentRenderEncoder = nil;
			m_RenderEncoderStarted = false;
		}
		if(m_CurrentBlitEncoder != nil)
		{
			[m_CurrentBlitEncoder endEncoding];
			ReleaseMetalObject(m_CurrentBlitEncoder);
			m_CurrentBlitEncoder = nil;
		}
	}

	bool CommitCurrentFrame(bool Present, bool WaitForCompletion)
	{
		if(m_CurrentCommandBuffer == nil || m_CommandBufferCommitted)
			return false;
		EndActiveEncoders();
		const bool BackbufferReady = HasValidBackbufferContents();
		// drawable 池最多 3 个，WindowServer 按显示器刷新率回收。逻辑帧率远高于
		// 刷新率时每帧 present 会耗尽池，nextDrawable 阻塞到下一个刷新周期，形成
		// 周期性锁帧卡顿。无 VSync 时按刷新周期主动节流 present：逻辑帧照常渲染
		// 提交，呈现均匀且不阻塞（录像/截图路径独立，不受节流影响）。
		bool EffectivePresent = Present;
		bool ThrottledPresent = false;
		// 刷新率优先级：显式配置 > 显示器实际刷新率 > 不节流。
		// 配置为 0（未设置）时用缓存的实际刷新率，仍无法获取则不节流，
		// 避免 120/144Hz 显示器被错误地限制到 60Hz 呈现。
		const int RefreshRate = g_Config.m_GfxScreenRefreshRate > 0 ? g_Config.m_GfxScreenRefreshRate : m_PresentRefreshRateHz;
		if(EffectivePresent && RefreshRate > 0 && !m_VSync && !VideoCaptureActive())
		{
			const std::chrono::nanoseconds Now = time_get_nanoseconds();
			const std::chrono::nanoseconds PresentInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / RefreshRate));
			// 距上次成功 present 不足刷新周期时跳过本帧 present。逻辑帧照常
			// 渲染提交，不被 nextDrawable 阻塞，呈现由 pacing 锚点精确化。
			if(m_LastPresentTimeNs.count() != 0 && Now - m_LastPresentTimeNs < PresentInterval)
			{
				EffectivePresent = false;
				ThrottledPresent = true;
			}
		}
		// The drawable pool is finite. Keep presentation late and do not add an
		// application-side in-flight limit here: that would turn an uncapped,
		// no-VSync render loop into an artificial display-refresh cap.
		const bool PresentationRequested = EffectivePresent && BackbufferReady && m_pLayer != nullptr;
		bool CanPresent = false;
		if(PresentationRequested)
		{
			CAMetalLayer *pLayer = (__bridge CAMetalLayer *)m_pLayer;
			const auto DrawableAcquireStart = m_MetalPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
			m_CurrentDrawable = RetainMetalObject([pLayer nextDrawable]);
			if(m_MetalPerfEnabled)
			{
				const double DrawableAcquireMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - DrawableAcquireStart).count();
				++m_MetalPerfDrawableAcquireCount;
				m_MetalPerfDrawableAcquireMs += DrawableAcquireMs;
				m_MetalPerfDrawableAcquireMaxMs = std::max(m_MetalPerfDrawableAcquireMaxMs, DrawableAcquireMs);
				m_MetalPerfDrawableAcquireOver16MsCount += DrawableAcquireMs > 16.667;
				m_MetalPerfDrawableAcquireOver33MsCount += DrawableAcquireMs > 33.333;
				m_MetalPerfDrawableAcquireOver100MsCount += DrawableAcquireMs > 100.0;
			}
			if(m_CurrentDrawable != nil)
			{
				m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
				if(m_MetalPerfEnabled && m_CurrentBlitEncoder != nil)
					++m_MetalPerfBlitEncoderCount;
				if(m_CurrentBlitEncoder != nil)
				{
					[m_CurrentBlitEncoder copyFromTexture:CurrentBackbufferTexture() sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_DrawableWidth, m_DrawableHeight, 1) toTexture:m_CurrentDrawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
					[m_CurrentBlitEncoder endEncoding];
					ReleaseMetalObject(m_CurrentBlitEncoder);
					m_CurrentBlitEncoder = nil;
					CanPresent = true;
				}
			}
			if(m_CurrentDrawable == nil && m_MetalPerfEnabled)
				++m_MetalPerfDrawableUnavailableCount;
		}
		const bool FrameFinalized = CanPresent ?
			m_FrameState.FinalizeFrameForPresent(true) == CMetalFrameState::EFinalizeResult::PRESENTED :
			m_FrameState.FinalizeFrameWithoutPresent();
		const bool BackbufferPresented = CanPresent && FrameFinalized;
		const bool PresentationSucceeded = !PresentationRequested || BackbufferPresented;
		if(BackbufferPresented)
		{
			m_LastPresentTimeNs = time_get_nanoseconds();
			if(!m_VSync && RefreshRate > 0)
			{
				// present pacing：把呈现时刻锚定到固定节奏序列，使每帧显示间隔
				// 精确等于刷新周期、帧内容年龄恒定，消除快速移动时的速度感波动。
				const std::chrono::nanoseconds PresentInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / RefreshRate));
				if(m_NextPresentTimeNs.count() == 0 || m_NextPresentTimeNs < m_LastPresentTimeNs)
					m_NextPresentTimeNs = m_LastPresentTimeNs;
				const double Duration = std::max(0.0, std::chrono::duration<double>(m_NextPresentTimeNs - m_LastPresentTimeNs).count());
				[m_CurrentCommandBuffer presentDrawable:m_CurrentDrawable afterMinimumDuration:Duration];
				m_NextPresentTimeNs += PresentInterval;
			}
			else
			{
				[m_CurrentCommandBuffer presentDrawable:m_CurrentDrawable];
			}
		}
		[m_CurrentCommandBuffer commit];
		m_CommandBufferCommitted = true;
		// 只有命令缓冲区容量切片才需要沿用当前私有 backbuffer。present
		// 失败时当前内容没有进入屏幕，丢弃这次未呈现的 slot，避免下一帧
		// 复用同一 slot 并等待仍在 GPU 队列中的 command buffer。节流跳过的
		// present 同样视为已消费，下一帧正常轮转清屏。
		m_BackbufferContinuationPending = CurrentBackbufferHasContents() && !Present;
		// 非 present 提交可能只是同一逻辑帧的 buffer slice；保留 drawable 和
		// backbuffer 内容供下一段继续 load。真正 present 或无 backbuffer 时再释放。
		if(Present || !CurrentBackbufferHasContents())
		{
			ReleaseMetalObject(m_CurrentDrawable);
			m_CurrentDrawable = nil;
		}
		if(BackbufferPresented)
			SetCurrentBackbufferHasContents(false);
		if(ThrottledPresent)
			SetCurrentBackbufferHasContents(false);
		if(!WaitForCompletion)
			return PresentationSucceeded;
		const auto GpuCompletionWaitStart = m_MetalPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
		[m_CurrentCommandBuffer waitUntilCompleted];
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfGpuCompletionWaitCount;
			m_MetalPerfGpuCompletionWaitMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - GpuCompletionWaitStart).count();
		}
		return PresentationSucceeded && m_CurrentCommandBuffer.status == MTLCommandBufferStatusCompleted;
	}

	bool CommitCurrentFrameForReadback()
	{
		if(m_CurrentCommandBuffer == nil || m_CommandBufferCommitted)
			return false;
		EndActiveEncoders();
		if(!m_FrameState.FinalizeFrameWithoutPresent())
			return false;
		[m_CurrentCommandBuffer commit];
		m_CommandBufferCommitted = true;
		m_BackbufferContinuationPending = false;
		ReleaseMetalObject(m_CurrentDrawable);
		m_CurrentDrawable = nil;
		const auto GpuCompletionWaitStart = m_MetalPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
		[m_CurrentCommandBuffer waitUntilCompleted];
		if(m_MetalPerfEnabled)
		{
			++m_MetalPerfGpuCompletionWaitCount;
			m_MetalPerfGpuCompletionWaitMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - GpuCompletionWaitStart).count();
		}
		return m_CurrentCommandBuffer.status == MTLCommandBufferStatusCompleted;
	}

	id<MTLBuffer> EncodeTextureReadback(id<MTLTexture> Texture, size_t Width, size_t Height, size_t &RowBytes)
	{
		if(m_CurrentCommandBuffer == nil || Texture == nil || Width == 0 || Height == 0)
			return nil;
		EndActiveEncoders();
		size_t UnalignedRowBytes = 0;
		if(!MetalCheckedMul(Width, 4, UnalignedRowBytes) || UnalignedRowBytes > std::numeric_limits<size_t>::max() - 255)
			return nil;
		RowBytes = (UnalignedRowBytes + 255) & ~size_t(255);
		if(RowBytes > std::numeric_limits<size_t>::max() / Height)
			return nil;
		id<MTLBuffer> Readback = [m_Device newBufferWithLength:RowBytes * Height options:MTLResourceStorageModeShared];
		if(Readback == nil)
			return nil;
		if(m_MetalPerfEnabled)
			m_MetalPerfReadbackBytes += RowBytes * Height;
		m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
		if(m_CurrentBlitEncoder == nil)
		{
			ReleaseMetalObject(Readback);
			return nil;
		}
		if(m_MetalPerfEnabled)
			++m_MetalPerfEncoderCount;
		[m_CurrentBlitEncoder copyFromTexture:Texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(Width, Height, 1) toBuffer:Readback destinationOffset:0 destinationBytesPerRow:RowBytes destinationBytesPerImage:RowBytes * Height];
		return Readback;
	}

	id<MTLBuffer> EncodeDrawableReadback(size_t Width, size_t Height, size_t &RowBytes)
	{
		if(m_CurrentCommandBuffer == nil || Width == 0 || Height == 0)
			return nil;
		const SFrameSlot &Frame = m_aFrameSlots[m_CurrentFrameSlot];
		if(!HasValidBackbufferContents() || Frame.m_BackbufferWidth != Width || Frame.m_BackbufferHeight != Height)
			return nil;
		return EncodeTextureReadback(CurrentBackbufferTexture(), Width, Height, RowBytes);
	}

	void StoreLastPresentedReadback(id<MTLBuffer> Readback, uint32_t Width, uint32_t Height, size_t RowBytes)
	{
		ReleaseMetalObject(m_LastPresentedReadback);
		m_LastPresentedReadback = RetainMetalObject(Readback);
		m_LastPresentedReadbackWidth = Width;
		m_LastPresentedReadbackHeight = Height;
		m_LastPresentedReadbackRowBytes = RowBytes;
	}

	void ClearLastPresentedReadback()
	{
		ReleaseMetalObject(m_LastPresentedReadback);
		m_LastPresentedReadback = nil;
		ReleaseMetalObject(m_LastPresentedCommandBuffer);
		m_LastPresentedCommandBuffer = nil;
		m_LastPresentedReadbackWidth = 0;
		m_LastPresentedReadbackHeight = 0;
		m_LastPresentedReadbackRowBytes = 0;
	}

	static void CopyBgraToRgba(uint8_t *pDst, const uint8_t *pSrc, size_t PixelCount)
	{
		for(size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
		{
			pDst[Pixel * 4 + 0] = pSrc[Pixel * 4 + 2];
			pDst[Pixel * 4 + 1] = pSrc[Pixel * 4 + 1];
			pDst[Pixel * 4 + 2] = pSrc[Pixel * 4 + 0];
			pDst[Pixel * 4 + 3] = pSrc[Pixel * 4 + 3];
		}
	}

	ERunCommandReturnTypes Cmd_Screenshot(const CCommandBuffer::SCommand_TrySwapAndScreenshot *pCommand)
	{
		if(pCommand->m_pImage == nullptr)
		{
			SetResourceCommandError(pCommand);
			return RUN_COMMAND_COMMAND_ERROR;
		}
		pCommand->m_pImage->m_pData = nullptr;
		pCommand->m_pImage->m_Width = 0;
		pCommand->m_pImage->m_Height = 0;
		pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
		if(pCommand->m_pSwapped == nullptr)
		{
			SetResourceCommandError(pCommand);
			return RUN_COMMAND_COMMAND_ERROR;
		}

		id<MTLBuffer> Readback = nil;
		size_t RowBytes = 0;
		uint32_t Width = m_DrawableWidth;
		uint32_t Height = m_DrawableHeight;
		if(!*pCommand->m_pSwapped && m_FrameState.ConsumeReadbackPresented())
		{
			*pCommand->m_pSwapped = true;
		}
		if(!*pCommand->m_pSwapped)
		{
			Readback = EncodeDrawableReadback(Width, Height, RowBytes);
			if(Readback == nil)
			{
				ReleaseMetalObject(Readback);
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			if(!CommitCurrentFrame(true, true))
			{
				ReleaseMetalObject(Readback);
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			*pCommand->m_pSwapped = true;
			StoreLastPresentedReadback(Readback, Width, Height, RowBytes);
			ReleaseMetalObject(m_LastPresentedCommandBuffer);
			m_LastPresentedCommandBuffer = RetainMetalObject(m_CurrentCommandBuffer);
			ReleaseMetalObject(Readback);
		}
		else
		{
			if(!WaitForPresentedReadback())
			{
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			Readback = m_LastPresentedReadback;
			Width = m_LastPresentedReadbackWidth;
			Height = m_LastPresentedReadbackHeight;
			RowBytes = m_LastPresentedReadbackRowBytes;
		}
		if(Readback == nil || Width == 0 || Height == 0)
		{
			if(!*pCommand->m_pSwapped)
				ReleaseMetalObject(Readback);
			return RUN_COMMAND_COMMAND_HANDLED;
		}
		size_t DataBytes = 0;
		size_t PixelBytes = 0;
		if(!MetalCheckedMul(static_cast<size_t>(Width), static_cast<size_t>(Height), PixelBytes) || !MetalCheckedMul(PixelBytes, 4, DataBytes))
		{
			if(Readback != m_LastPresentedReadback)
				ReleaseMetalObject(Readback);
			return RUN_COMMAND_COMMAND_HANDLED;
		}
		uint8_t *pImageData = static_cast<uint8_t *>(malloc(DataBytes));
		if(pImageData == nullptr)
		{
			if(Readback != m_LastPresentedReadback)
				ReleaseMetalObject(Readback);
			return RUN_COMMAND_COMMAND_HANDLED;
		}
		const uint8_t *pReadback = static_cast<const uint8_t *>(Readback.contents);
		for(uint32_t Y = 0; Y < Height; ++Y)
			CopyBgraToRgba(pImageData + static_cast<size_t>(Y) * Width * 4, pReadback + static_cast<size_t>(Y) * RowBytes, Width);
		pCommand->m_pImage->m_Width = Width;
		pCommand->m_pImage->m_Height = Height;
		pCommand->m_pImage->m_pData = pImageData;
		if(Readback != m_LastPresentedReadback)
			ReleaseMetalObject(Readback);
		return RUN_COMMAND_COMMAND_HANDLED;
	}

	ERunCommandReturnTypes Cmd_ReadPixel(const CCommandBuffer::SCommand_TrySwapAndReadPixel *pCommand)
	{
		if(pCommand->m_pColor == nullptr || pCommand->m_pSwapped == nullptr)
		{
			SetResourceCommandError(pCommand);
			return RUN_COMMAND_COMMAND_ERROR;
		}
		*pCommand->m_pColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(!*pCommand->m_pSwapped && m_FrameState.ConsumeReadbackPresented())
		{
			*pCommand->m_pSwapped = true;
		}
		if(!*pCommand->m_pSwapped)
		{
			const int X = pCommand->m_Position.x;
			const int Y = pCommand->m_Position.y;
			if(X < 0 || Y < 0 || static_cast<uint32_t>(X) >= m_DrawableWidth || static_cast<uint32_t>(Y) >= m_DrawableHeight)
				return RUN_COMMAND_COMMAND_HANDLED;
			if(!HasValidBackbufferContents())
				return RUN_COMMAND_COMMAND_HANDLED;
			EndActiveEncoders();
			ClearLastPresentedReadback();
			size_t VideoRowBytes = 0;
			id<MTLBuffer> VideoReadback = VideoCaptureActive() ? EncodeDrawableReadback(m_DrawableWidth, m_DrawableHeight, VideoRowBytes) : nil;
			EndActiveEncoders();
			const size_t RowBytes = 256;
			id<MTLBuffer> Readback = [m_Device newBufferWithLength:RowBytes options:MTLResourceStorageModeShared];
			if(Readback == nil)
			{
				ReleaseMetalObject(VideoReadback);
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			if(m_MetalPerfEnabled)
				m_MetalPerfReadbackBytes += RowBytes;
			m_CurrentBlitEncoder = RetainMetalObject([m_CurrentCommandBuffer blitCommandEncoder]);
			if(m_CurrentBlitEncoder == nil)
			{
				ReleaseMetalObject(VideoReadback);
				ReleaseMetalObject(Readback);
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			if(m_MetalPerfEnabled)
				++m_MetalPerfEncoderCount;
			[m_CurrentBlitEncoder copyFromTexture:CurrentBackbufferTexture() sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(X, Y, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:Readback destinationOffset:0 destinationBytesPerRow:RowBytes destinationBytesPerImage:RowBytes];
			if(!CommitCurrentFrame(true, true))
			{
				ReleaseMetalObject(VideoReadback);
				ReleaseMetalObject(Readback);
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			if(VideoReadback != nil)
			{
				StoreLastPresentedReadback(VideoReadback, m_DrawableWidth, m_DrawableHeight, VideoRowBytes);
				ReleaseMetalObject(m_LastPresentedCommandBuffer);
				m_LastPresentedCommandBuffer = RetainMetalObject(m_CurrentCommandBuffer);
			}
			const uint8_t *pPixel = static_cast<const uint8_t *>(Readback.contents);
			*pCommand->m_pColor = ColorRGBA(pPixel[2] / 255.0f, pPixel[1] / 255.0f, pPixel[0] / 255.0f, 1.0f);
			*pCommand->m_pSwapped = true;
			ReleaseMetalObject(VideoReadback);
			ReleaseMetalObject(Readback);
			return RUN_COMMAND_COMMAND_HANDLED;
		}
		if(!WaitForPresentedReadback() || pCommand->m_Position.x < 0 || pCommand->m_Position.y < 0 || static_cast<uint32_t>(pCommand->m_Position.x) >= m_LastPresentedReadbackWidth || static_cast<uint32_t>(pCommand->m_Position.y) >= m_LastPresentedReadbackHeight)
			return RUN_COMMAND_COMMAND_HANDLED;
		const uint8_t *pPixel = static_cast<const uint8_t *>(m_LastPresentedReadback.contents) + static_cast<size_t>(pCommand->m_Position.y) * m_LastPresentedReadbackRowBytes + static_cast<size_t>(pCommand->m_Position.x) * 4;
		*pCommand->m_pColor = ColorRGBA(pPixel[2] / 255.0f, pPixel[1] / 255.0f, pPixel[0] / 255.0f, 1.0f);
		return RUN_COMMAND_COMMAND_HANDLED;
	}

	ERunCommandReturnTypes Cmd_Swap(const CCommandBuffer::SCommand_Swap *pCommand)
	{
		(void)pCommand;
		if(m_FrameState.ConsumeReadbackPresented())
			return RUN_COMMAND_COMMAND_HANDLED;
		if(VideoCaptureActive() && HasValidBackbufferContents())
		{
			ClearLastPresentedReadback();
			size_t RowBytes = 0;
			id<MTLBuffer> Readback = EncodeDrawableReadback(m_DrawableWidth, m_DrawableHeight, RowBytes);
			id<MTLCommandBuffer> PresentedCommandBuffer = Readback != nil ? RetainMetalObject(m_CurrentCommandBuffer) : nil;
			const bool CommitSucceeded = CommitCurrentFrame(true, false);
			if(CommitSucceeded && Readback != nil)
			{
				StoreLastPresentedReadback(Readback, m_DrawableWidth, m_DrawableHeight, RowBytes);
				ReleaseMetalObject(m_LastPresentedCommandBuffer);
				m_LastPresentedCommandBuffer = PresentedCommandBuffer;
				PresentedCommandBuffer = nil;
			}
			ReleaseMetalObject(PresentedCommandBuffer);
			ReleaseMetalObject(Readback);
			if(!CommitSucceeded)
			{
				SetSwapError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			return RUN_COMMAND_COMMAND_HANDLED;
		}
		ClearLastPresentedReadback();
		if(!CommitCurrentFrame(true, false))
		{
			SetSwapError();
			return RUN_COMMAND_COMMAND_ERROR;
		}
		return RUN_COMMAND_COMMAND_HANDLED;
	}

	bool Cmd_MultiSampling(const CCommandBuffer::SCommand_MultiSampling *pCommand)
	{
		if(pCommand->m_pRetMultiSamplingCount == nullptr || pCommand->m_pRetOk == nullptr)
			return false;
		const uint32_t RequestedCount = pCommand->m_RequestedMultiSamplingCount;
		uint32_t SupportedCount = SupportedMultiSamplingCount(RequestedCount);
		*pCommand->m_pRetMultiSamplingCount = m_MultiSamplingCount;
		*pCommand->m_pRetOk = false;
		if(m_State != EMetalBackendState::INITIALIZED || m_RenderTargetState.IsActive() || m_CurrentDrawable != nil || m_RenderEncoderStarted || m_CurrentRenderEncoder != nil || m_CurrentBlitEncoder != nil)
		{
			dbg_msg("gfx/metal", "cannot change multisampling: state=%s target=%d drawable=%d render_encoder=%d current_render=%d blit=%d", MetalBackendStateName(m_State), m_RenderTargetState.IsActive() ? 1 : 0, m_CurrentDrawable != nil ? 1 : 0, m_RenderEncoderStarted ? 1 : 0, m_CurrentRenderEncoder != nil ? 1 : 0, m_CurrentBlitEncoder != nil ? 1 : 0);
			return false;
		}
		// 仅剩上一帧 backbuffer 内容时可以安全丢弃；切换采样数会在下一次
		// BeginRenderEncoder 中重新清屏，不能把旧内容误当作当前附件。
		SetCurrentBackbufferHasContents(false);
		if(SupportedCount != RequestedCount)
			log_warn("gfx/metal", "Requested %u FSAA samples, using %u.", RequestedCount, SupportedCount);
		if(SupportedCount == m_MultiSamplingCount)
		{
			*pCommand->m_pRetMultiSamplingCount = SupportedCount;
			*pCommand->m_pRetOk = true;
			return true;
		}
		if(SupportedCount > 0 && !CreatePipelineStates(SupportedCount, m_aMultiSamplePipelineStates))
		{
			// 某些设备虽然报告支持 sample count，但并非所有 Metal pipeline
			// 变体都能以该采样数创建；回退到单采样而不是让整帧命令失败。
			log_warn("gfx/metal", "MSAA pipeline creation failed for %u samples; falling back to single-sample rendering.", SupportedCount);
			ReleasePipelineStates(m_aMultiSamplePipelineStates);
			SupportedCount = 0;
		}
		if(SupportedCount == 0)
			ReleasePipelineStates(m_aMultiSamplePipelineStates);
		if(SupportedCount > 0 && HasSdfPipelines(m_aMediaIslandSdfPipelines) && HasSdfPipelines(m_aRoundedRectSdfPipelines) &&
			!CreateSdfPipelineStates(SupportedCount, m_aMultiSampleMediaIslandSdfPipelines, m_aMultiSampleRoundedRectSdfPipelines))
		{
			dbg_msg("gfx/metal", "Qm SDF MSAA pipelines unavailable after FSAA change; using the existing geometry fallback");
			ReleasePipelineStates(m_aMultiSampleMediaIslandSdfPipelines);
			ReleasePipelineStates(m_aMultiSampleRoundedRectSdfPipelines);
		}
		if(SupportedCount > 0 && HasSdfPipelines(m_aTexturedMsdfPipelines) &&
			!CreateTexturedMsdfPipelineStates(SupportedCount, m_aMultiSampleTexturedMsdfPipelines))
		{
			dbg_msg("gfx/metal", "Textured MSDF MSAA pipelines unavailable after FSAA change; using the existing geometry fallback");
			ReleasePipelineStates(m_aMultiSampleTexturedMsdfPipelines);
		}
		if(SupportedCount == 0)
		{
			ReleasePipelineStates(m_aMultiSampleMediaIslandSdfPipelines);
			ReleasePipelineStates(m_aMultiSampleRoundedRectSdfPipelines);
			ReleasePipelineStates(m_aMultiSampleTexturedMsdfPipelines);
		}
		// Sample-count changes invalidate every in-flight attachment. Drain first
		// so no completed handler or GPU pass can still reference the old textures.
		WaitForGpuIdle();
		DestroyAllMultiSampleTextures();
		m_MultiSamplingCount = SupportedCount;
		if(m_pCapabilities != nullptr)
		{
			const bool SdfPipelinesAvailable = HasSdfSupportForCurrentSampleCount();
			m_pCapabilities->m_MediaIslandSdf = SdfPipelinesAvailable;
			m_pCapabilities->m_RoundedRectSdf = SdfPipelinesAvailable;
			m_pCapabilities->m_TexturedMsdf.store(HasTexturedMsdfSupportForCurrentSampleCount(), std::memory_order_release);
		}
		*pCommand->m_pRetMultiSamplingCount = SupportedCount;
		*pCommand->m_pRetOk = true;
		return true;
	}

	void CompleteCurrentFrameIfNeeded()
	{
		CommitCurrentFrame(false, false);
	}

	void StartCommands(size_t CommandCount, size_t EstimatedRenderCallCount) override
	{
		(void)CommandCount;
		const bool PerfEnabled = g_Config.m_QmMacosGraphicsDiagnostics != 0 || g_Config.m_QmGraphicsTrace != 0;
		if(PerfEnabled != m_MetalPerfEnabled)
		{
			m_MetalPerfEnabled = PerfEnabled;
			ResetMetalPerfStats();
			ClearMetalGpuTimingStats(m_MetalPerfGeneration.fetch_add(1, std::memory_order_acq_rel) + 1);
		}
		if(m_MetalPerfEnabled)
		{
			MaybeLogMetalPerfStats();
			m_MetalPerfFrameStart = time_get_nanoseconds();
			++m_MetalPerfFrameCount;
			++m_MetalPerfCommandBufferCount;
			m_MetalPerfEstimatedRenderCalls += EstimatedRenderCallCount;
		}
		if(m_State != EMetalBackendState::INITIALIZED || m_CommandQueue == nil)
			return;
		// 命令缓冲区可能因容量耗尽在同一逻辑帧中被切分。只要上一段已经
		// 获取了 backbuffer drawable，就跨段保留它并以 load 继续绘制，直到
		// 真正的 Swap/present；否则前半帧会被丢弃，表现为闪烁和抖动。
		const bool ContinueBackbufferFrame = m_CommandBufferCommitted && m_BackbufferContinuationPending && CurrentBackbufferHasContents();
		// 同一逻辑帧因命令缓冲区分片时继续使用当前 slot，避免把仍在 GPU
		// 使用的 backbuffer 交给另一个 slot。正常帧仍按 slot 轮转并行执行。
		const size_t Slot = ContinueBackbufferFrame ? m_CurrentFrameSlot : (m_FrameId == 0 ? 0 : (m_CurrentFrameSlot + 1) % gs_FrameSlotCount);
		SFrameSlot &Frame = m_aFrameSlots[Slot];
		if(m_FrameState.SlotState(Slot) == CMetalFrameState::ESlotState::IN_FLIGHT && Frame.m_CommandBuffer != nil)
		{
			const MTLCommandBufferStatus Status = Frame.m_CommandBuffer.status;
			if(Status != MTLCommandBufferStatusCompleted && Status != MTLCommandBufferStatusError)
			{
				const auto FrameSlotWaitStart = m_MetalPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
				[Frame.m_CommandBuffer waitUntilCompleted];
				if(m_MetalPerfEnabled)
				{
					++m_MetalPerfFrameSlotWaitCount;
					m_MetalPerfFrameSlotWaitMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - FrameSlotWaitStart).count();
				}
			}
			m_FrameState.CompleteFrame({Frame.m_FrameId, Slot}, Frame.m_CommandBuffer.status == MTLCommandBufferStatusCompleted);
		}
		ReleaseFrameSlotResources(Slot);
		if(!m_FrameState.BeginFrame(Slot))
			return;
		EndActiveEncoders();
		if(!ContinueBackbufferFrame)
			ReleaseMetalObject(m_CurrentDrawable);
		m_CurrentFrameSlot = Slot;
		Frame.m_CommandBuffer = RetainMetalObject([m_CommandQueue commandBuffer]);
		m_CurrentCommandBuffer = Frame.m_CommandBuffer;
		m_CommandBufferCommitted = false;
		m_BackbufferContinuationPending = false;
		m_RenderEncoderStarted = false;
		if(!ContinueBackbufferFrame)
		{
			m_CurrentDrawable = nil;
			Frame.m_BackbufferHasContents = false;
		}
		m_SkipCurrentFrame = false;
		m_RenderTargetState.Reset();
		Frame.m_VertexOffset = 0;
		Frame.m_FrameId = m_FrameState.CurrentFrameId();
		m_FrameId = Frame.m_FrameId;
		const CMetalFrameState::SFrameCapture Capture{Frame.m_FrameId, Slot};
		const bool RecordGpuTiming = m_MetalPerfEnabled;
		const uint64_t GpuTimingGeneration = m_MetalPerfGeneration.load(std::memory_order_acquire);
		[m_CurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> Buffer) {
			if(RecordGpuTiming)
				RecordMetalGpuExecutionTiming(Buffer, GpuTimingGeneration);
			const bool Success = Buffer.status == MTLCommandBufferStatusCompleted;
			if(!Success)
				RecordGpuFailure(Buffer, Capture.m_FrameId);
			m_FrameState.CompleteFrame(Capture, Success);
		}];
	}

	void EndCommands() override
	{
		CompleteCurrentFrameIfNeeded();
		if(m_MetalPerfEnabled && m_MetalPerfFrameStart != std::chrono::nanoseconds::zero())
		{
			const double ProcessingMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - m_MetalPerfFrameStart).count();
			m_MetalPerfCommandProcessingMs += ProcessingMs;
			m_MetalPerfCommandProcessingMaxMs = std::max(m_MetalPerfCommandProcessingMaxMs, ProcessingMs);
			m_MetalPerfFrameStart = std::chrono::nanoseconds::zero();
		}
	}

	bool SkipCurrentFrameCommand(int Command) const
	{
		if(!m_SkipCurrentFrame)
			return false;
		switch(Command)
		{
		case CCommandBuffer::CMD_CLEAR:
		case CCommandBuffer::CMD_RENDER:
		case CCommandBuffer::CMD_RENDER_MEDIA_ISLAND_SDF:
		case CCommandBuffer::CMD_RENDER_ROUNDED_RECT_SDF:
		case CCommandBuffer::CMD_RENDER_TEXTURED_MSDF:
		case CCommandBuffer::CMD_RENDER_TEX3D:
		case CCommandBuffer::CMD_RENDER_TARGET_BEGIN:
		case CCommandBuffer::CMD_RENDER_TARGET_END:
		case CCommandBuffer::CMD_RENDER_TARGET_DRAW:
		case CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS:
		case CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER:
		case CCommandBuffer::CMD_RENDER_TILE_LAYER:
		case CCommandBuffer::CMD_RENDER_BORDER_TILE:
		case CCommandBuffer::CMD_RENDER_QUAD_LAYER:
		case CCommandBuffer::CMD_RENDER_QUAD_LAYER_GROUPED:
		case CCommandBuffer::CMD_RENDER_TEXT:
		case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER:
		case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_EX:
		case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_SPRITE_MULTIPLE:
		case CCommandBuffer::CMD_TRY_SWAP_AND_SCREENSHOT:
			return true;
		default:
			return false;
		}
	}

public:
	~CCommandProcessorFragment_Metal() override
	{
		WaitForGpuIdle();
		DestroyAllBufferContainers();
		DestroyAllBuffers();
		ReleaseGpuObjects();
		if(m_MetalView != nullptr)
			SDL_Metal_DestroyView(m_MetalView);
		for(size_t Slot = 0; Slot < m_vTextureSlots.size(); ++Slot)
			DestroyTexture(static_cast<int>(Slot));
		ReleaseCommandQueue();
		ReleaseDevice();
	}

	ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *pBaseCommand) override
	{
		@autoreleasepool
		{
			m_Error = {};
			const bool IsLifecycleCommand = pBaseCommand->m_Cmd == CCommandProcessorFragment_GLBase::CMD_PRE_INIT ||
				pBaseCommand->m_Cmd == CCommandProcessorFragment_GLBase::CMD_INIT ||
				pBaseCommand->m_Cmd == CCommandProcessorFragment_GLBase::CMD_SHUTDOWN ||
				pBaseCommand->m_Cmd == CCommandProcessorFragment_GLBase::CMD_POST_SHUTDOWN;
			if(!IsLifecycleCommand && SetGpuFailureError())
				return RUN_COMMAND_COMMAND_ERROR;
			if(!IsLifecycleCommand)
				m_LastCommandId.store(pBaseCommand->m_Cmd, std::memory_order_relaxed);
			if(!IsLifecycleCommand && SkipCurrentFrameCommand(pBaseCommand->m_Cmd))
				return RUN_COMMAND_COMMAND_HANDLED;
			switch(pBaseCommand->m_Cmd)
			{
			case CCommandProcessorFragment_SDL::CMD_INIT:
			case CCommandProcessorFragment_SDL::CMD_SHUTDOWN:
				return RUN_COMMAND_COMMAND_UNHANDLED;
			case CCommandProcessorFragment_GLBase::CMD_PRE_INIT:
				Cmd_PreInit(static_cast<const SCommand_PreInit *>(pBaseCommand));
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandProcessorFragment_GLBase::CMD_INIT:
				Cmd_Init(static_cast<const SCommand_Init *>(pBaseCommand));
				// 初始化失败通过 SCommand_Init 的输出参数传播，不能污染包装器的粘滞错误，
				// 否则部分初始化清理无法提交后续 shutdown 命令。
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandProcessorFragment_GLBase::CMD_SHUTDOWN:
				Cmd_Shutdown(static_cast<const SCommand_Shutdown *>(pBaseCommand));
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandProcessorFragment_GLBase::CMD_POST_SHUTDOWN:
				Cmd_PostShutdown(static_cast<const SCommand_PostShutdown *>(pBaseCommand));
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_VSYNC:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_VSync *>(pBaseCommand);
				m_VSync = pCommand->m_VSync != 0;
				if(m_pLayer != nullptr)
	#if defined(CONF_PLATFORM_MACOS)
					((__bridge CAMetalLayer *)m_pLayer).displaySyncEnabled = m_VSync;
	#endif
				if(m_pLayer != nullptr)
					dbg_msg("gfx/metal", "layer vsync changed: requested=%d", m_VSync ? 1 : 0);
				if(pCommand->m_pRetOk != nullptr)
					*pCommand->m_pRetOk = m_pLayer != nullptr;
				return RUN_COMMAND_COMMAND_HANDLED;
			}
			case CCommandBuffer::CMD_UPDATE_VIEWPORT:
				UpdateDrawableSize();
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_CREATE_BUFFER_OBJECT:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_CreateBufferObject *>(pBaseCommand);
				const bool Success = CreateBuffer(pCommand->m_BufferIndex, pCommand->m_DataSize, pCommand->m_pUploadData, pCommand->m_Flags);
				if(pCommand->m_DeletePointer && pCommand->m_pUploadData != nullptr)
					free(pCommand->m_pUploadData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RECREATE_BUFFER_OBJECT:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_RecreateBufferObject *>(pBaseCommand);
				const bool Success = RecreateBuffer(pCommand->m_BufferIndex, pCommand->m_DataSize, pCommand->m_pUploadData, pCommand->m_Flags);
				if(pCommand->m_DeletePointer && pCommand->m_pUploadData != nullptr)
					free(pCommand->m_pUploadData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_UPDATE_BUFFER_OBJECT:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_UpdateBufferObject *>(pBaseCommand);
				const size_t Offset = reinterpret_cast<uintptr_t>(pCommand->m_pOffset);
				const bool Success = UpdateBuffer(pCommand->m_BufferIndex, Offset, pCommand->m_DataSize, pCommand->m_pUploadData);
				if(pCommand->m_DeletePointer && pCommand->m_pUploadData != nullptr)
					free(pCommand->m_pUploadData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_COPY_BUFFER_OBJECT:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_CopyBufferObject *>(pBaseCommand);
				const bool Success = CopyBuffer(pCommand->m_WriteBufferIndex, pCommand->m_ReadBufferIndex, pCommand->m_WriteOffset, pCommand->m_ReadOffset, pCommand->m_CopySize);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_DELETE_BUFFER_OBJECT:
				DestroyBuffer(static_cast<const CCommandBuffer::SCommand_DeleteBufferObject *>(pBaseCommand)->m_BufferIndex);
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_CREATE_BUFFER_CONTAINER:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_CreateBufferContainer *>(pBaseCommand);
				const bool Success = UpdateBufferContainer(pCommand->m_BufferContainerIndex, pCommand->m_Stride, pCommand->m_VertBufferBindingIndex, pCommand->m_AttrCount, pCommand->m_pAttributes, true);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_UPDATE_BUFFER_CONTAINER:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_UpdateBufferContainer *>(pBaseCommand);
				const bool Success = UpdateBufferContainer(pCommand->m_BufferContainerIndex, pCommand->m_Stride, pCommand->m_VertBufferBindingIndex, pCommand->m_AttrCount, pCommand->m_pAttributes, false);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_DELETE_BUFFER_CONTAINER:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_DeleteBufferContainer *>(pBaseCommand);
				DestroyBufferContainer(pCommand->m_BufferContainerIndex, pCommand->m_DestroyAllBO);
				return RUN_COMMAND_COMMAND_HANDLED;
			}
			case CCommandBuffer::CMD_INDICES_REQUIRED_NUM_NOTIFY:
			{
				const unsigned int RequiredIndicesNum = static_cast<const CCommandBuffer::SCommand_IndicesRequiredNumNotify *>(pBaseCommand)->m_RequiredIndicesNum;
				if(!EnsureQuadIndexCapacity(RequiredIndicesNum))
				{
					SetResourceCommandError(pBaseCommand);
					return RUN_COMMAND_COMMAND_ERROR;
				}
				m_RequiredIndicesNum = std::max(m_RequiredIndicesNum, RequiredIndicesNum);
				return RUN_COMMAND_COMMAND_HANDLED;
			}
			case CCommandBuffer::CMD_TEXTURE_CREATE:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(pBaseCommand);
				const bool Success = CreateTexture(pCommand->m_Slot, pCommand->m_Width, pCommand->m_Height, EMetalTextureFormat::RGBA8, pCommand->m_Flags, pCommand->m_pData);
				if(pCommand->m_pData != nullptr)
					free(pCommand->m_pData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_TEXTURE_UPDATE:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_Texture_Update *>(pBaseCommand);
				const bool Success = UpdateTexture(pCommand->m_Slot, pCommand->m_X, pCommand->m_Y, pCommand->m_Width, pCommand->m_Height, EMetalTextureFormat::RGBA8, pCommand->m_pData);
				if(pCommand->m_pData != nullptr)
					free(pCommand->m_pData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_TEXTURE_DESTROY:
				DestroyTexture(static_cast<const CCommandBuffer::SCommand_Texture_Destroy *>(pBaseCommand)->m_Slot);
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_TEXT_TEXTURES_CREATE:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_TextTextures_Create *>(pBaseCommand);
				const bool TextSuccess = CreateTexture(pCommand->m_Slot, pCommand->m_Width, pCommand->m_Height, EMetalTextureFormat::R8, TextureFlag::NO_MIPMAPS, pCommand->m_pTextData);
				const bool OutlineSuccess = CreateTexture(pCommand->m_SlotOutline, pCommand->m_Width, pCommand->m_Height, EMetalTextureFormat::R8, TextureFlag::NO_MIPMAPS, pCommand->m_pTextOutlineData);
				const bool Success = TextSuccess && OutlineSuccess;
				if(!Success)
				{
					if(TextSuccess)
						DestroyTexture(pCommand->m_Slot);
					if(OutlineSuccess)
						DestroyTexture(pCommand->m_SlotOutline);
				}
				if(pCommand->m_pTextData != nullptr)
					free(pCommand->m_pTextData);
				if(pCommand->m_pTextOutlineData != nullptr)
					free(pCommand->m_pTextOutlineData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_TEXT_TEXTURE_UPDATE:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_TextTexture_Update *>(pBaseCommand);
				const bool Success = UpdateTexture(pCommand->m_Slot, pCommand->m_X, pCommand->m_Y, pCommand->m_Width, pCommand->m_Height, EMetalTextureFormat::R8, pCommand->m_pData);
				if(pCommand->m_pData != nullptr)
					free(pCommand->m_pData);
				if(!Success)
					SetResourceCommandError(pCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_TEXT_TEXTURES_DESTROY:
			{
				const auto *pCommand = static_cast<const CCommandBuffer::SCommand_TextTextures_Destroy *>(pBaseCommand);
				DestroyTexture(pCommand->m_Slot);
				DestroyTexture(pCommand->m_SlotOutline);
					return RUN_COMMAND_COMMAND_HANDLED;
				}
			case CCommandBuffer::CMD_CLEAR:
				Cmd_Clear(static_cast<const CCommandBuffer::SCommand_Clear *>(pBaseCommand));
				return m_Error.m_ErrorType == GFX_ERROR_TYPE_NONE ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			case CCommandBuffer::CMD_RENDER:
				Cmd_Render(static_cast<const CCommandBuffer::SCommand_Render *>(pBaseCommand));
				return m_Error.m_ErrorType == GFX_ERROR_TYPE_NONE ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			case CCommandBuffer::CMD_RENDER_MEDIA_ISLAND_SDF:
			{
				const bool Success = DrawMediaIslandSdf(*static_cast<const CCommandBuffer::SCommand_RenderMediaIslandSdf *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_ROUNDED_RECT_SDF:
			{
				const bool Success = DrawRoundedRectSdf(*static_cast<const CCommandBuffer::SCommand_RenderRoundedRectSdf *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TEXTURED_MSDF:
			{
				const bool Success = DrawTexturedMsdf(*static_cast<const CCommandBuffer::SCommand_RenderTexturedMsdf *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TEX3D:
			{
				const bool Success = DrawTex3D(*static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_CREATE:
			{
				const bool Success = Cmd_RenderTarget_Create(static_cast<const CCommandBuffer::SCommand_RenderTarget_Create *>(pBaseCommand));
				if(!Success)
					SetResourceCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_DESTROY:
				Cmd_RenderTarget_Destroy(static_cast<const CCommandBuffer::SCommand_RenderTarget_Destroy *>(pBaseCommand));
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_RENDER_TARGET_BEGIN:
			{
				const bool Success = Cmd_RenderTarget_Begin(static_cast<const CCommandBuffer::SCommand_RenderTarget_Begin *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_END:
				Cmd_RenderTarget_End(static_cast<const CCommandBuffer::SCommand_RenderTarget_End *>(pBaseCommand));
				return RUN_COMMAND_COMMAND_HANDLED;
			case CCommandBuffer::CMD_RENDER_TARGET_DRAW:
			{
				const bool Success = Cmd_RenderTarget_Draw(static_cast<const CCommandBuffer::SCommand_RenderTarget_Draw *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS:
			{
				const bool Success = Cmd_RenderTarget_GaussianBlurPass(static_cast<const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_READBACK:
			{
				const bool Success = Cmd_RenderTarget_Readback(static_cast<const CCommandBuffer::SCommand_RenderTarget_Readback *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER:
			{
				const bool Success = Cmd_RenderTarget_CaptureBackbuffer(static_cast<const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TILE_LAYER:
			{
				const bool Success = DrawTileLayer(*static_cast<const CCommandBuffer::SCommand_RenderTileLayer *>(pBaseCommand), false, {}, {});
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_BORDER_TILE:
			{
				const bool Success = DrawBorderTile(*static_cast<const CCommandBuffer::SCommand_RenderBorderTile *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_QUAD_LAYER:
			case CCommandBuffer::CMD_RENDER_QUAD_LAYER_GROUPED:
			{
				const bool Grouped = pBaseCommand->m_Cmd == CCommandBuffer::CMD_RENDER_QUAD_LAYER_GROUPED;
				const bool Success = DrawQuadLayer(*static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand), Grouped);
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_TEXT:
			{
				const bool Success = DrawText(*static_cast<const CCommandBuffer::SCommand_RenderText *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER:
			{
				const bool Success = DrawQuadContainer(*static_cast<const CCommandBuffer::SCommand_RenderQuadContainer *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_EX:
			{
				const bool Success = DrawQuadContainerEx(*static_cast<const CCommandBuffer::SCommand_RenderQuadContainerEx *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_SPRITE_MULTIPLE:
			{
				const bool Success = DrawQuadContainerAsSpriteMultiple(*static_cast<const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}
			case CCommandBuffer::CMD_SWAP:
				return Cmd_Swap(static_cast<const CCommandBuffer::SCommand_Swap *>(pBaseCommand));
			case CCommandBuffer::CMD_TRY_SWAP_AND_READ_PIXEL:
				return Cmd_ReadPixel(static_cast<const CCommandBuffer::SCommand_TrySwapAndReadPixel *>(pBaseCommand));
			case CCommandBuffer::CMD_TRY_SWAP_AND_SCREENSHOT:
				return Cmd_Screenshot(static_cast<const CCommandBuffer::SCommand_TrySwapAndScreenshot *>(pBaseCommand));
			case CCommandBuffer::CMD_MULTISAMPLING:
			{
				const bool Success = Cmd_MultiSampling(static_cast<const CCommandBuffer::SCommand_MultiSampling *>(pBaseCommand));
				if(!Success)
					SetUnsupportedCommandError(pBaseCommand);
				return Success ? RUN_COMMAND_COMMAND_HANDLED : RUN_COMMAND_COMMAND_ERROR;
			}

			// 这些命令由组合处理器中的 SDL/General fragment 接管。
			case CCommandBuffer::CMD_SIGNAL:
			case CCommandBuffer::CMD_WINDOW_CREATE_NTF:
			case CCommandBuffer::CMD_WINDOW_DESTROY_NTF:
				return RUN_COMMAND_COMMAND_UNHANDLED;

			default:
				SetUnsupportedCommandError(pBaseCommand);
				return RUN_COMMAND_COMMAND_ERROR;
			}
		}
	}
};
} // namespace

CCommandProcessorFragment_GLBase *CreateMetalCommandProcessorFragment()
{
	return new CCommandProcessorFragment_Metal();
}
