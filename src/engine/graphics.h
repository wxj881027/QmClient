/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_GRAPHICS_H
#define ENGINE_GRAPHICS_H

#include "image.h"
#include "kernel.h"
#include "warning.h"

#include <base/color.h>
#include <base/system.h>
#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#define GRAPHICS_TYPE_UNSIGNED_BYTE 0x1401
#define GRAPHICS_TYPE_UNSIGNED_SHORT 0x1403
#define GRAPHICS_TYPE_INT 0x1404
#define GRAPHICS_TYPE_UNSIGNED_INT 0x1405
#define GRAPHICS_TYPE_FLOAT 0x1406

struct SBufferContainerInfo
{
	int m_Stride;
	int m_VertBufferBindingIndex;

	// the attributes of the container
	struct SAttribute
	{
		int m_DataTypeCount;
		unsigned int m_Type;
		bool m_Normalized;
		void *m_pOffset;

		//0: float, 1:integer
		unsigned int m_FuncType;
	};
	std::vector<SAttribute> m_vAttributes;
};

struct SQuadRenderInfo
{
	ColorRGBA m_Color;
	vec2 m_Offsets;
	float m_Rotation;
	// allows easier upload for uniform buffers because of the alignment requirements
	float m_Padding;
};

class CGraphicTile
{
public:
	vec2 m_TopLeft;
	vec2 m_TopRight;
	vec2 m_BottomRight;
	vec2 m_BottomLeft;
};

class CGraphicTileTextureCoords
{
public:
	ubvec4 m_TexCoordTopLeft;
	ubvec4 m_TexCoordTopRight;
	ubvec4 m_TexCoordBottomRight;
	ubvec4 m_TexCoordBottomLeft;
};

/*
	Structure: CVideoMode
*/
class CVideoMode
{
public:
	int m_CanvasWidth, m_CanvasHeight;
	int m_WindowWidth, m_WindowHeight;
	int m_RefreshRate;
	int m_Red, m_Green, m_Blue;
	uint32_t m_Format;
};

typedef vec2 GL_SPoint;
typedef vec2 GL_STexCoord;

struct GL_STexCoord3D
{
	GL_STexCoord3D &operator=(const GL_STexCoord &TexCoord)
	{
		u = TexCoord.u;
		v = TexCoord.v;
		return *this;
	}

	GL_STexCoord3D &operator=(const vec3 &TexCoord)
	{
		u = TexCoord.u;
		v = TexCoord.v;
		w = TexCoord.w;
		return *this;
	}

	float u, v, w;
};

typedef ColorRGBA GL_SColorf;
//use normalized color values
typedef vector4_base<unsigned char> GL_SColor;

struct GL_SVertex
{
	GL_SPoint m_Pos;
	GL_STexCoord m_Tex;
	GL_SColor m_Color;
};

struct GL_SVertexTex3D
{
	GL_SPoint m_Pos;
	GL_SColorf m_Color;
	GL_STexCoord3D m_Tex;
};

struct GL_SVertexTex3DStream
{
	GL_SPoint m_Pos;
	GL_SColor m_Color;
	GL_STexCoord3D m_Tex;
};

static constexpr size_t gs_GraphicsMaxQuadsRenderCount = 256;
static constexpr size_t gs_GraphicsMaxParticlesRenderCount = 512;

enum EGraphicsDriverAgeType
{
	GRAPHICS_DRIVER_AGE_TYPE_LEGACY = 0,
	GRAPHICS_DRIVER_AGE_TYPE_DEFAULT,
	GRAPHICS_DRIVER_AGE_TYPE_MODERN,

	GRAPHICS_DRIVER_AGE_TYPE_COUNT,
};

enum EBackendType
{
	BACKEND_TYPE_OPENGL = 0,
	BACKEND_TYPE_OPENGL_ES,
	BACKEND_TYPE_VULKAN,

	// special value to tell the backend to identify the current backend
	BACKEND_TYPE_AUTO,

	BACKEND_TYPE_COUNT,
};

struct SOpenGLVersion
{
	int m_Major;
	int m_Minor;
	int m_Patch;
};

constexpr SOpenGLVersion AutoOpenGLProbeVersion(EBackendType BackendType)
{
	// 这里只定义渲染后端和平台能够尝试的 API 上限，设置项显示仍以 context 探测结果为准。
	if(BackendType == EBackendType::BACKEND_TYPE_OPENGL_ES)
		return {3, 0, 0};
#if defined(CONF_PLATFORM_MACOS)
	return {4, 1, 0};
#else
	return {4, 6, 0};
#endif
}

constexpr bool IsOpenGLVersionAtLeast(SOpenGLVersion Available, SOpenGLVersion Required)
{
	if(Available.m_Major != Required.m_Major)
		return Available.m_Major > Required.m_Major;
	if(Available.m_Minor != Required.m_Minor)
		return Available.m_Minor > Required.m_Minor;
	return Available.m_Patch >= Required.m_Patch;
}

constexpr bool NextAutoOpenGLProbeVersion(SOpenGLVersion &Version)
{
	if(Version.m_Major == 4 && Version.m_Minor > 0)
	{
		--Version.m_Minor;
		return true;
	}
	if(Version.m_Major == 4)
	{
		Version = {3, 3, 0};
		return true;
	}
	if(Version.m_Major == 3 && Version.m_Minor > 0)
	{
		--Version.m_Minor;
		return true;
	}
	return false;
}

constexpr bool ShouldSyncActualOpenGLVersion(EBackendType BackendType, SOpenGLVersion Requested, SOpenGLVersion Actual)
{
	const bool RequestedModernOpenGL = BackendType == EBackendType::BACKEND_TYPE_OPENGL && ((Requested.m_Major == 3 && Requested.m_Minor == 3) || Requested.m_Major >= 4);
	const bool RequestedModernOpenGLES = BackendType == EBackendType::BACKEND_TYPE_OPENGL_ES && Requested.m_Major >= 3;
	return (RequestedModernOpenGL || RequestedModernOpenGLES) && Actual.m_Major > 0;
}

struct STWGraphicGpu
{
	enum ETWGraphicsGpuType
	{
		GRAPHICS_GPU_TYPE_DISCRETE = 0,
		GRAPHICS_GPU_TYPE_INTEGRATED,
		GRAPHICS_GPU_TYPE_VIRTUAL,
		GRAPHICS_GPU_TYPE_CPU,

		// should stay at last position in this enum
		GRAPHICS_GPU_TYPE_INVALID,
	};

	struct STWGraphicGpuItem
	{
		char m_aName[256];
		ETWGraphicsGpuType m_GpuType;
	};
	std::vector<STWGraphicGpuItem> m_vGpus;
	STWGraphicGpuItem m_AutoGpu;
};

typedef STWGraphicGpu TTwGraphicsGpuList;

typedef std::function<void()> WINDOW_RESIZE_FUNC;
typedef std::function<void()> WINDOW_PROPS_CHANGED_FUNC;
// 图形资源被整体丢弃时触发（例如 Vulkan 设备丢失后重建了设备）。
// 回调里必须丢弃并重新加载所有 GPU 资源，例如皮肤、字体、图标图集、四边形容器。
typedef std::function<void()> GRAPHICS_RESOURCES_RESET_FUNC;

typedef std::function<bool(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData)> TGLBackendReadPresentedImageData;

struct CDataSprite;

class IGraphics : public IInterface
{
	MACRO_INTERFACE("graphics")
protected:
	int m_ScreenWidth;
	int m_ScreenHeight;
	int m_ScreenRefreshRate;
	float m_ScreenHiDPIScale;
	float m_GameScreenAspectOverride = 0.0f;

public:
	static constexpr int MEDIA_ISLAND_SDF_MAX_ITEMS = 12;
	static constexpr int GAUSSIAN_BLUR_MAX_RADIUS = 10;

	struct SGaussianBlurParams
	{
		// The one-dimensional kernel size is m_Radius * 2 + 1 (3 through 21).
		int m_Radius = 4;
		float m_Sigma = 2.0f;
	};

	static bool CalculateGaussianBlurKernel(const SGaussianBlurParams &Params, std::array<float, GAUSSIAN_BLUR_MAX_RADIUS + 1> &aWeights);

	// Fixed vec4-only layout shared by the threaded command buffer, GLSL and
	// Vulkan std140 UBO. The shader treats this as 45 consecutive vec4 values:
	// [0..7] are common fields, every item occupies three vec4 values and the
	// final value maps the SDF quad to an optional backdrop texture.
	struct SMediaIslandSdfParams
	{
		static constexpr int DATA_RECT = 0;
		static constexpr int DATA_MAIN_RECT = 1;
		static constexpr int DATA_CAPSULE_RECT = 2;
		static constexpr int DATA_BACKGROUND = 3;
		static constexpr int DATA_MAIN_PARAMS = 4; // radius, disabled radius, ring radius, ring thickness
		static constexpr int DATA_METADATA = 5; // item count, corners, has capsule, screen pixel size
		static constexpr int DATA_CAPSULE_PARAMS = 6; // radius, smooth union, unused, unused
		static constexpr int DATA_RESERVED = 7; // outer shadow size, opacity, unused, unused
		static constexpr int DATA_ITEM_BASE = 8;
		static constexpr int DATA_ITEM_STRIDE = 3;
		static constexpr int DATA_BACKDROP_UV = DATA_ITEM_BASE + MEDIA_ISLAND_SDF_MAX_ITEMS * DATA_ITEM_STRIDE;
		static constexpr int DATA_COUNT = DATA_BACKDROP_UV + 1;

		std::array<vec4, DATA_COUNT> m_aData{};

		void Clear()
		{
			m_aData.fill(vec4(0.0f, 0.0f, 0.0f, 0.0f));
		}

		void SetItemCount(int Count)
		{
			m_aData[DATA_METADATA].x = (float)std::clamp(Count, 0, MEDIA_ISLAND_SDF_MAX_ITEMS);
		}
		int ItemCount() const { return std::clamp((int)m_aData[DATA_METADATA].x, 0, MEDIA_ISLAND_SDF_MAX_ITEMS); }

		void SetMainCorners(int Corners) { m_aData[DATA_METADATA].y = (float)Corners; }
		int MainCorners() const { return (int)m_aData[DATA_METADATA].y; }

		void SetHasRightCapsule(bool HasCapsule) { m_aData[DATA_METADATA].z = HasCapsule ? 1.0f : 0.0f; }
		bool HasRightCapsule() const { return m_aData[DATA_METADATA].z > 0.5f; }

		vec4 &Item(int Index, int Field)
		{
			return m_aData[DATA_ITEM_BASE + std::clamp(Index, 0, MEDIA_ISLAND_SDF_MAX_ITEMS - 1) * DATA_ITEM_STRIDE + std::clamp(Field, 0, DATA_ITEM_STRIDE - 1)];
		}
		const vec4 &Item(int Index, int Field) const
		{
			return m_aData[DATA_ITEM_BASE + std::clamp(Index, 0, MEDIA_ISLAND_SDF_MAX_ITEMS - 1) * DATA_ITEM_STRIDE + std::clamp(Field, 0, DATA_ITEM_STRIDE - 1)];
		}
	};
	static_assert(sizeof(vec4) == sizeof(float) * 4);
	static_assert(sizeof(SMediaIslandSdfParams) == SMediaIslandSdfParams::DATA_COUNT * sizeof(vec4));

	// A compact rounded-rectangle primitive shared by UI chrome. It intentionally
	// remains separate from the Media Island data layout so UI controls do not
	// inherit HUD-specific shape semantics.
	struct SRoundedRectSdfParams
	{
		vec4 m_Rect{};
		vec4 m_FillColor{};
		vec4 m_BorderColor{};
		// 左上、右上、右下、左下圆角半径
		vec4 m_CornerRadii{};
		// 边框宽度、逻辑像素大小、外部 quad padding、保留字段
		vec4 m_Params{};
	};
	static_assert(sizeof(SRoundedRectSdfParams) == sizeof(vec4) * 5);

	enum
	{
		TEXLOAD_TO_3D_TEXTURE = 1 << 0,
		TEXLOAD_TO_2D_ARRAY_TEXTURE = 1 << 1,
		TEXLOAD_NO_2D_TEXTURE = 1 << 2,
		TEXLOAD_NO_MIPMAPS = 1 << 3,
	};

	class CTextureHandle
	{
		friend class IGraphics;
		int m_Id;
		uint32_t m_Generation;

	public:
		CTextureHandle() :
			m_Id(-1),
			m_Generation(0)
		{
		}

		bool IsValid() const { return Id() >= 0; }
		bool IsNullTexture() const { return IsValid() && Id() == 0; }
		int Id() const { return m_Id; }
		uint32_t Generation() const { return m_Generation; }
		void Invalidate()
		{
			m_Id = -1;
			m_Generation = 0;
		}
	};

	struct STexturedMsdfParams
	{
		CTextureHandle m_Texture;
		vec4 m_Rect{};
		vec4 m_UvRect{};
		ColorRGBA m_Color{};
		float m_PxRange = 0.0f;
		float m_AtlasWidth = 0.0f;
		float m_AtlasHeight = 0.0f;
		float m_Rotation = 0.0f;
	};

	class CRenderTargetHandle
	{
		friend class IGraphics;
		int m_Id;

	public:
		CRenderTargetHandle() :
			m_Id(-1)
		{
		}

		bool IsValid() const { return Id() >= 0; }
		int Id() const { return m_Id; }
		void Invalidate() { m_Id = -1; }
	};

	enum class ERenderTargetReadbackState
	{
		INVALID = 0,
		PENDING,
		READY,
		FAILED,
	};

	class CRenderTargetReadbackHandle
	{
		friend class IGraphics;
		int m_Id;
		uint32_t m_Generation;

	public:
		CRenderTargetReadbackHandle() :
			m_Id(-1),
			m_Generation(0)
		{
		}

		bool IsValid() const { return Id() >= 0; }
		int Id() const { return m_Id; }
		uint32_t Generation() const { return m_Generation; }
		void Invalidate()
		{
			m_Id = -1;
			m_Generation = 0;
		}
	};

	int ScreenWidth() const { return m_ScreenWidth; }
	int ScreenHeight() const { return m_ScreenHeight; }
	float ScreenAspect() const { return (float)ScreenWidth() / (float)ScreenHeight(); }
	float GameScreenAspect() const { return m_GameScreenAspectOverride > 0.0f ? m_GameScreenAspectOverride : ScreenAspect(); }
	float ScreenHiDPIScale() const { return m_ScreenHiDPIScale; }
	int WindowWidth() const { return m_ScreenWidth / m_ScreenHiDPIScale; }
	int WindowHeight() const { return m_ScreenHeight / m_ScreenHiDPIScale; }

	virtual void WarnPngliteIncompatibleImages(bool Warn) = 0;
	virtual void SetWindowParams(int FullscreenMode, bool IsBorderless) = 0;
	virtual bool SetWindowScreen(int Index, bool MoveToCenter) = 0;
	virtual bool SwitchWindowScreen(int Index, bool MoveToCenter) = 0;
	virtual bool SetVSync(bool State) = 0;
	virtual bool SetMultiSampling(uint32_t ReqMultiSamplingCount, uint32_t &MultiSamplingCountBackend) = 0;
	virtual int GetWindowScreen() = 0;
	virtual void Move(int x, int y) = 0;
	virtual bool Resize(int w, int h, int RefreshRate) = 0;
	virtual void ResizeToScreen() = 0;
	virtual void GotResized(int w, int h, int RefreshRate) = 0;
	virtual void UpdateViewport(int X, int Y, int W, int H, bool ByResize) = 0;
	virtual bool IsScreenKeyboardShown() = 0;

	/**
	 * Listens to a resize event of the canvas, which is usually caused by a window resize.
	 * Will only be triggered if the actual size changed.
	 */
	virtual void AddWindowResizeListener(WINDOW_RESIZE_FUNC pFunc) = 0;
	/**
	 * Listens to various window property changes, such as minimize, maximize, move, fullscreen mode
	 */
	virtual void AddWindowPropChangeListener(WINDOW_PROPS_CHANGED_FUNC pFunc) = 0;
	/**
	 * 监听「图形资源整体重置」事件。设备重建后所有 GPU 资源都已失效，
	 * 由引擎在安全时机（主线程、非活动渲染目标）统一广播，监听者据此重建自己的资源。
	 */
	virtual void AddGraphicsResourcesResetListener(GRAPHICS_RESOURCES_RESET_FUNC pFunc) = 0;
	/**
	 * 图形资源重置版本号。每次设备重建都会自增，可用于判断自己缓存的资源是否过期。
	 */
	virtual uint32_t GraphicsResourcesResetVersion() const = 0;

	virtual void WindowDestroyNtf(uint32_t WindowId) = 0;
	virtual void WindowCreateNtf(uint32_t WindowId) = 0;

	// ForceClearNow forces the backend to trigger a clear, even at performance cost, else it might be delayed by one frame
	virtual void Clear(float r, float g, float b, bool ForceClearNow = false) = 0;

	virtual void ClipEnable(int x, int y, int w, int h) = 0;
	virtual void ClipDisable() = 0;

	virtual void MapScreen(float TopLeftX, float TopLeftY, float BottomRightX, float BottomRightY) = 0;

	// helper functions
	void CalcScreenParams(float Aspect, float Zoom, float *pWidth, float *pHeight) const;
	void MapScreenToWorld(float CenterX, float CenterY, float ParallaxX, float ParallaxY,
		float ParallaxZoom, float OffsetX, float OffsetY, float Aspect, float Zoom, float *pPoints) const;
	void MapScreenToInterface(float CenterX, float CenterY, float Zoom = 1.0f);
	void MapScreenToGameInterface(float CenterX, float CenterY, float Zoom = 1.0f);

	virtual void GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY) const = 0;

	// TODO: These should perhaps not be virtuals
	virtual void BlendNone() = 0;
	virtual void BlendNormal() = 0;
	virtual void BlendAdditive() = 0;
	virtual void WrapNormal() = 0;
	virtual void WrapClamp() = 0;

	virtual uint64_t TextureMemoryUsage() const = 0;
	virtual uint64_t BufferMemoryUsage() const = 0;
	virtual uint64_t StreamedMemoryUsage() const = 0;
	virtual uint64_t StagingMemoryUsage() const = 0;

	virtual const TTwGraphicsGpuList &GetGpus() const = 0;

	virtual bool LoadPng(CImageInfo &Image, const char *pFilename, int StorageType) = 0;
	virtual bool LoadPng(CImageInfo &Image, const uint8_t *pData, size_t DataSize, const char *pContextName) = 0;

	virtual bool CheckImageDivisibility(const char *pContextName, CImageInfo &Image, int DivX, int DivY, bool AllowResize) = 0;
	virtual bool IsImageFormatRgba(const char *pContextName, const CImageInfo &Image) = 0;

	virtual void UnloadTexture(CTextureHandle *pIndex) = 0;
	virtual CTextureHandle LoadTextureRaw(const CImageInfo &Image, int Flags, const char *pTexName = nullptr) = 0;
	virtual CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pTexName = nullptr) = 0;
	virtual CTextureHandle LoadTexture(const char *pFilename, int StorageType, int Flags = 0) = 0;
	virtual void TextureSet(CTextureHandle Texture) = 0;
	void TextureClear() { TextureSet(CTextureHandle()); }

	virtual bool IsRenderTargetSupported() const = 0;
	virtual bool IsRenderTargetGaussianBlurSupported() const = 0;
	virtual bool IsBackbufferCaptureSupported() const = 0;
	virtual const char *RenderTargetSupportReason() const = 0;
	virtual CRenderTargetHandle CreateRenderTarget(int Width, int Height) = 0;
	virtual void DestroyRenderTarget(CRenderTargetHandle *pTarget) = 0;
	virtual bool BeginRenderTarget(CRenderTargetHandle Target, ColorRGBA ClearColor) = 0;
	virtual void EndRenderTarget() = 0;
	struct SRenderTargetDrawParams
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
		float m_Alpha = 1.0f;
		int m_Corners = 0;
		float m_Rounding = 0.0f;
		float m_U0 = 0.0f;
		float m_V0 = 1.0f;
		float m_U1 = 1.0f;
		float m_V1 = 0.0f;
	};
	virtual void DrawRenderTarget(CRenderTargetHandle Target, const SRenderTargetDrawParams &Params) = 0;
	// Captures all drawing submitted before this call and scales the current backbuffer
	// into Target without a CPU readback. Must be called outside an active render target.
	virtual bool CaptureBackbufferToRenderTarget(CRenderTargetHandle Target) = 0;
	// Must be called outside an active render target. Source, Temporary and Destination
	// must be distinct render targets with identical dimensions.
	virtual bool GaussianBlurRenderTarget(CRenderTargetHandle Source, CRenderTargetHandle Temporary, CRenderTargetHandle Destination, const SGaussianBlurParams &Params) = 0;
	// 必须在非活动渲染目标状态下调用。Source 和 Destination 为完整分辨率目标，
	// 三个中间目标必须使用相同的较小尺寸。操作依次执行降采样、模糊和升采样。
	virtual bool DualBlurRenderTarget(CRenderTargetHandle Source, CRenderTargetHandle Downsample, CRenderTargetHandle DownsampleTemporary, CRenderTargetHandle DownsampleBlurred, CRenderTargetHandle Destination, const SGaussianBlurParams &Params) = 0;
	virtual CRenderTargetReadbackHandle BeginRenderTargetReadback(CRenderTargetHandle Target) = 0;
	virtual ERenderTargetReadbackState PollRenderTargetReadback(CRenderTargetReadbackHandle Handle) = 0;
	virtual bool ResolveRenderTargetReadback(CRenderTargetReadbackHandle *pHandle, CImageInfo &Image) = 0;
	virtual void CancelRenderTargetReadback(CRenderTargetReadbackHandle *pHandle) = 0;
	virtual bool ReadRenderTarget(CRenderTargetHandle Target, CImageInfo &Image) = 0;

	// pTextData & pTextOutlineData are automatically free'd
	virtual bool LoadTextTextures(size_t Width, size_t Height, CTextureHandle &TextTexture, CTextureHandle &TextOutlineTexture, uint8_t *pTextData, uint8_t *pTextOutlineData) = 0;
	virtual bool UnloadTextTextures(CTextureHandle &TextTexture, CTextureHandle &TextOutlineTexture) = 0;
	virtual bool UpdateTextTexture(CTextureHandle TextureId, int x, int y, size_t Width, size_t Height, uint8_t *pData, bool IsMovedPointer) = 0;
	virtual bool UpdateTexture(CTextureHandle TextureId, int x, int y, size_t Width, size_t Height, uint8_t *pData, bool IsMovedPointer) = 0;

	virtual CTextureHandle LoadSpriteTexture(const CImageInfo &FromImageInfo, const struct CDataSprite *pSprite) = 0;

	virtual bool IsImageSubFullyTransparent(const CImageInfo &FromImageInfo, int x, int y, int w, int h) = 0;
	virtual bool IsSpriteTextureFullyTransparent(const CImageInfo &FromImageInfo, const struct CDataSprite *pSprite) = 0;

	virtual void FlushVertices(bool KeepVertices = false) = 0;
	virtual void FlushVerticesTex3D() = 0;

	// specific render functions
	virtual void RenderTileLayer(int BufferContainerIndex, const ColorRGBA &Color, char **pOffsets, unsigned int *pIndicedVertexDrawNum, size_t NumIndicesOffset) = 0;
	virtual void RenderBorderTiles(int BufferContainerIndex, const ColorRGBA &Color, char *pIndexBufferOffset, const vec2 &Offset, const vec2 &Scale, uint32_t DrawNum) = 0;
	virtual void RenderQuadLayer(int BufferContainerIndex, SQuadRenderInfo *pQuadInfo, size_t QuadNum, int QuadOffset, bool Grouped = false) = 0;
	virtual void RenderText(int BufferContainerIndex, int TextQuadNum, int TextureSize, int TextureTextIndex, int TextureTextOutlineIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) = 0;
	// Render a media-island SDF quad in a supported programmable backend.
	// Callers use a geometry approximation when HasMediaIslandSdf() is false.
	virtual void RenderMediaIslandSdf(const SMediaIslandSdfParams &Params, CRenderTargetHandle Backdrop = CRenderTargetHandle()) = 0;
	// Render a fill and optional border in one anti-aliased rounded-rectangle pass.
	virtual void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) = 0;
	// Draw only the outward anti-alias fringe of a rounded rectangle.
	virtual void DrawRoundedRectAntialias(float x, float y, float w, float h, float Radius, int Corners, const ColorRGBA &Color) = 0;
	// Render a textured multi-channel signed-distance field with runtime tint.
	// This is intentionally separate from normal alpha-texture quads and text glyphs.
	virtual void RenderTexturedMsdf(const STexturedMsdfParams &Params) = 0;

	// opengl 3.3 functions

	enum EBufferObjectCreateFlags
	{
		// tell the backend that the buffer only needs to be valid for the span of one frame. Buffer size is not allowed to be bigger than GL_SVertex * MAX_VERTICES
		BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT = 1 << 0,
	};

	// if a pointer is passed as moved pointer, it requires to be allocated with malloc()
	virtual int CreateBufferObject(size_t UploadDataSize, void *pUploadData, int CreateFlags, bool IsMovedPointer = false) = 0;
	virtual void RecreateBufferObject(int BufferIndex, size_t UploadDataSize, void *pUploadData, int CreateFlags, bool IsMovedPointer = false) = 0;
	virtual void DeleteBufferObject(int BufferIndex) = 0;

	virtual int CreateBufferContainer(struct SBufferContainerInfo *pContainerInfo) = 0;
	// destroying all buffer objects means, that all referenced VBOs are destroyed automatically, so the user does not need to save references to them
	virtual void DeleteBufferContainer(int &ContainerIndex, bool DestroyAllBO = true) = 0;
	virtual void IndicesNumRequiredNotify(unsigned int RequiredIndicesCount) = 0;

	// returns true if the driver age type is supported, passing BACKEND_TYPE_AUTO for BackendType will query the values for the currently used backend
	virtual bool GetDriverVersion(EGraphicsDriverAgeType DriverAgeType, int &Major, int &Minor, int &Patch, const char *&pName, EBackendType BackendType) = 0;
	// 返回当前图形后端实际生效的 API 版本，不能用配置请求版本代替。
	virtual bool GetDetectedContextVersion(int &Major, int &Minor, int &Patch, const char *&pName)
	{
		Major = 0;
		Minor = 0;
		Patch = 0;
		pName = "";
		return false;
	}
	virtual bool IsConfigModernAPI() = 0;
	virtual bool HasMediaIslandSdf() = 0;
	virtual bool HasRoundedRectSdf() = 0;
	virtual bool HasTexturedMsdf() = 0;
	virtual bool IsTileBufferingEnabled() = 0;
	virtual bool IsQuadBufferingEnabled() = 0;
	virtual bool IsTextBufferingEnabled() = 0;
	virtual bool IsQuadContainerBufferingEnabled() = 0;
	virtual bool Uses2DTextureArrays() = 0;
	virtual bool HasTextureArraysSupport() const = 0;

	virtual const char *GetVendorString() = 0;
	virtual const char *GetVersionString() = 0;
	virtual const char *GetRendererString() = 0;
	virtual const char *GetFatalError() const = 0;
	/**
	 * 非破坏性查询图形后端是否已经记录了致命错误。
	 *
	 * 后端一旦把致命错误提交出去（CGraphicsBackend_Threaded::ProcessError 会调用
	 * dbg_assert_failed 直接中止进程），就没有任何恢复余地，所以主循环必须在提交
	 * 之前轮询这个接口，才能在设备丢失这类故障上主动做恢复/退出，而不是让进程
	 * 卡在断言弹出的模态错误框里（那会让心跳停止并写出误导性的 hang 报告）。
	 *
	 * @return true 表示后端已记录致命错误，图形输出不可继续信任。
	 */
	virtual bool HasFatalError() const { return false; }
	/**
	 * 检查并清除图形后端的致命错误标记。
	 *
	 * 只有在「已经决定不再信任本帧图形输出、准备收尾/重启」时才允许调用：
	 * 清除标记的目的是让随后的收尾流程（关停仍会提交清理命令）不再重复触发断言。
	 *
	 * @return true 表示刚刚消费掉一个致命错误。
	 */
	virtual bool TakeFatalError() { return false; }

	class CLineItem
	{
	public:
		float m_X0, m_Y0, m_X1, m_Y1;
		CLineItem() = default;
		CLineItem(float x0, float y0, float x1, float y1) :
			m_X0(x0), m_Y0(y0), m_X1(x1), m_Y1(y1) {}
		CLineItem(vec2 From, vec2 To)
		{
			m_X0 = From.x;
			m_Y0 = From.y;
			m_X1 = To.x;
			m_Y1 = To.y;
		}
	};
	virtual void LinesBegin() = 0;
	virtual void LinesEnd() = 0;
	virtual void LinesDraw(const CLineItem *pArray, size_t Num) = 0;

	class CLineItemBatch
	{
	public:
		IGraphics::CLineItem m_aItems[256];
		size_t m_NumItems = 0;
	};
	virtual void LinesBatchBegin(CLineItemBatch *pBatch) = 0;
	virtual void LinesBatchEnd(CLineItemBatch *pBatch) = 0;
	virtual void LinesBatchDraw(CLineItemBatch *pBatch, const CLineItem *pArray, size_t Num) = 0;

	virtual void QuadsBegin() = 0;
	virtual void QuadsEnd() = 0;
	virtual void QuadsTex3DBegin() = 0;
	virtual void QuadsTex3DEnd() = 0;
	virtual void TrianglesBegin() = 0;
	virtual void TrianglesEnd() = 0;
	virtual void QuadsEndKeepVertices() = 0;
	virtual void QuadsDrawCurrentVertices(bool KeepVertices = true) = 0;
	virtual void QuadsSetRotation(float Angle) = 0;
	virtual void QuadsSetSubset(float TopLeftU, float TopLeftV, float BottomRightU, float BottomRightV) = 0;
	virtual void QuadsSetSubsetFree(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int Index = -1) = 0;

	struct CFreeformItem
	{
		float m_X0, m_Y0, m_X1, m_Y1, m_X2, m_Y2, m_X3, m_Y3;
		CFreeformItem() = default;
		CFreeformItem(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) :
			m_X0(x0), m_Y0(y0), m_X1(x1), m_Y1(y1), m_X2(x2), m_Y2(y2), m_X3(x3), m_Y3(y3) {}
		CFreeformItem(vec2 Point1, vec2 Point2, vec2 Point3, vec2 Point4) :
			m_X0(Point1.x), m_Y0(Point1.y), m_X1(Point2.x), m_Y1(Point2.y), m_X2(Point3.x), m_Y2(Point3.y), m_X3(Point4.x), m_Y3(Point4.y) {}
	};

	struct CQuadItem
	{
		float m_X, m_Y, m_Width, m_Height;
		CQuadItem() = default;
		CQuadItem(float x, float y, float w, float h) :
			m_X(x), m_Y(y), m_Width(w), m_Height(h) {}
		CQuadItem(vec2 Position, vec2 Size) :
			m_X(Position.x), m_Y(Position.y), m_Width(Size.x), m_Height(Size.y) {}
	};
	virtual void QuadsDraw(CQuadItem *pArray, int Num) = 0;
	virtual void QuadsDrawTL(const CQuadItem *pArray, int Num) = 0;

	virtual void QuadsTex3DDrawTL(const CQuadItem *pArray, int Num) = 0;

	virtual int CreateQuadContainer(bool AutomaticUpload = true) = 0;
	virtual void QuadContainerChangeAutomaticUpload(int ContainerIndex, bool AutomaticUpload) = 0;
	virtual void QuadContainerUpload(int ContainerIndex) = 0;
	virtual int QuadContainerAddQuads(int ContainerIndex, CQuadItem *pArray, int Num) = 0;
	virtual int QuadContainerAddQuads(int ContainerIndex, CFreeformItem *pArray, int Num) = 0;
	virtual void QuadContainerReset(int ContainerIndex) = 0;
	virtual void DeleteQuadContainer(int &ContainerIndex) = 0;
	virtual void RenderQuadContainer(int ContainerIndex, int QuadDrawNum) = 0;
	virtual void RenderQuadContainer(int ContainerIndex, int QuadOffset, int QuadDrawNum, bool ChangeWrapMode = true) = 0;
	virtual void RenderQuadContainerEx(int ContainerIndex, int QuadOffset, int QuadDrawNum, float X, float Y, float ScaleX = 1.f, float ScaleY = 1.f) = 0;
	virtual void RenderQuadContainerAsSprite(int ContainerIndex, int QuadOffset, float X, float Y, float ScaleX = 1.f, float ScaleY = 1.f) = 0;

	struct SRenderSpriteInfo
	{
		vec2 m_Pos;
		float m_Scale;
		float m_Rotation;
	};

	virtual void RenderQuadContainerAsSpriteMultiple(int ContainerIndex, int QuadOffset, int DrawCount, SRenderSpriteInfo *pRenderInfo) = 0;

	virtual void QuadsDrawFreeform(const CFreeformItem *pArray, int Num) = 0;
	virtual void QuadsText(float x, float y, float Size, const char *pText) = 0;

	// sprites
	enum
	{
		SPRITE_FLAG_FLIP_Y = 1,
		SPRITE_FLAG_FLIP_X = 2,
	};
	virtual void SelectSprite(int Id, int Flags = 0) = 0;
	virtual void SelectSprite7(int Id, int Flags = 0) = 0;

	virtual void GetSpriteScale(const CDataSprite *pSprite, float &ScaleX, float &ScaleY) const = 0;
	virtual void GetSpriteScale(int Id, float &ScaleX, float &ScaleY) const = 0;
	virtual void GetSpriteScaleImpl(int Width, int Height, float &ScaleX, float &ScaleY) const = 0;

	virtual void DrawSprite(float x, float y, float Size) = 0;
	virtual void DrawSprite(float x, float y, float ScaledWidth, float ScaledHeight) = 0;

	virtual int QuadContainerAddSprite(int QuadContainerIndex, float x, float y, float Size) = 0;
	virtual int QuadContainerAddSprite(int QuadContainerIndex, float Size) = 0;
	virtual int QuadContainerAddSprite(int QuadContainerIndex, float Width, float Height) = 0;
	virtual int QuadContainerAddSprite(int QuadContainerIndex, float X, float Y, float Width, float Height) = 0;

	enum
	{
		CORNER_NONE = 0,
		CORNER_TL = 1,
		CORNER_TR = 2,
		CORNER_BL = 4,
		CORNER_BR = 8,

		CORNER_T = CORNER_TL | CORNER_TR,
		CORNER_B = CORNER_BL | CORNER_BR,
		CORNER_R = CORNER_TR | CORNER_BR,
		CORNER_L = CORNER_TL | CORNER_BL,

		CORNER_ALL = CORNER_T | CORNER_B,
	};
	virtual void DrawRectExt(float x, float y, float w, float h, float r, int Corners) = 0;
	virtual void DrawRectExt4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, float r, int Corners) = 0;
	virtual int CreateRectQuadContainer(float x, float y, float w, float h, float r, int Corners) = 0;
	virtual void DrawRect(float x, float y, float w, float h, ColorRGBA Color, int Corners, float Rounding) = 0;
	virtual void DrawRect4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, int Corners, float Rounding) = 0;
	virtual void DrawCircle(float CenterX, float CenterY, float Radius, int Segments) = 0;

	struct CColorVertex
	{
		int m_Index;
		float m_R, m_G, m_B, m_A;
		CColorVertex() = default;
		CColorVertex(int i, float r, float g, float b, float a) :
			m_Index(i), m_R(r), m_G(g), m_B(b), m_A(a) {}
		CColorVertex(int i, ColorRGBA Color) :
			m_Index(i), m_R(Color.r), m_G(Color.g), m_B(Color.b), m_A(Color.a) {}
	};
	virtual void SetColorVertex(const CColorVertex *pArray, size_t Num) = 0;
	virtual void SetColor(float r, float g, float b, float a) = 0;
	virtual void SetColor(ColorRGBA Color) = 0;
	virtual void SetColor4(ColorRGBA TopLeft, ColorRGBA TopRight, ColorRGBA BottomLeft, ColorRGBA BottomRight) = 0;
	virtual void ChangeColorOfCurrentQuadVertices(float r, float g, float b, float a) = 0;
	virtual void ChangeColorOfQuadVertices(size_t QuadOffset, unsigned char r, unsigned char g, unsigned char b, unsigned char a) = 0;

	/**
	 * Reads the color at the specified position from the backbuffer once,
	 * after the next swap operation.
	 *
	 * @param Position The pixel position to read.
	 * @param pColor Pointer that will receive the read pixel color.
	 * The pointer must be valid until the next swap operation.
	 */
	virtual void ReadPixel(ivec2 Position, ColorRGBA *pColor) = 0;
	using FScreenshotCallback = std::function<void(CImageInfo &&)>;
	virtual void TakeScreenshot(const char *pFilename) = 0;
	virtual void TakeScreenshot(const char *pFilename, FScreenshotCallback pfnCallback) = 0;
	virtual void TakeCustomScreenshot(const char *pFilename) = 0;
	virtual int GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen) = 0;
	virtual void GetCurrentVideoMode(CVideoMode &CurMode, int Screen) = 0;
	virtual void Swap() = 0;
	virtual int GetNumScreens() const = 0;
	virtual const char *GetScreenName(int Screen) const = 0;

	// synchronization
	virtual void InsertSignal(class CSemaphore *pSemaphore) = 0;
	virtual bool IsIdle() const = 0;
	virtual void WaitForIdle() = 0;

	virtual void SetWindowGrab(bool Grab) = 0;
	virtual void NotifyWindow() = 0;

	// be aware that this function should only be called from the graphics thread, and even then you should really know what you are doing
	// this function always returns the pixels in RGB
	virtual TGLBackendReadPresentedImageData &GetReadPresentedImageDataFuncUnsafe() = 0;

	virtual std::optional<SWarning> CurrentWarning() = 0;

	/**
	 * Type of a message box popup.
	 *
	 * @see CMessageBox
	 */
	enum class EMessageBoxType
	{
		ERROR,
		WARNING,
		INFO,
	};
	/**
	 * Description of a message box popup button.
	 *
	 * @see CMessageBox
	 */
	class CMessageBoxButton
	{
	public:
		/**
		 * The label of this button.
		 *
		 * @remark This needs to be short because some systems do not increase the button sizes.
		 */
		const char *m_pLabel = nullptr;
		/**
		 * Whether the enter key activates this button.
		 */
		bool m_Confirm = false;
		/**
		 * Whether the escape key activates this button.
		 *
		 * @remark Closing the popup with the window manager will also cause this button to be activated.
		 */
		bool m_Cancel = false;
	};
	/**
	 * Description of a message box popup.
	 *
	 * @see ShowMessageBox
	 */
	class CMessageBox
	{
	public:
		/**
		 * Title of the message box.
		 */
		const char *m_pTitle = nullptr;
		/**
		 * Main message of the message box.
		 */
		const char *m_pMessage = nullptr;
		/**
		 * Type of the message box.
		 */
		EMessageBoxType m_Type = EMessageBoxType::ERROR;
		/**
		 * Buttons shown in the message box. At least one button is required.
		 * The buttons are laid out from left to right.
		 */
		std::vector<CMessageBoxButton> m_vButtons = {{.m_pLabel = "OK", .m_Confirm = true, .m_Cancel = true}};
	};
	/**
	 * Shows a modal message box with configuration title, message and buttons.
	 *
	 * @param MessageBox Description of the message box.
	 *
	 * @return Optional containing the index of the pressed button if the popup was shown successfully.
	 * @return Empty optional if the message box was not shown successfully.
	 *
	 * @remark Note that calling this function will destroy the current window,
	 *         so it only makes sense for fatal errors at the moment.
	 */
	virtual std::optional<int> ShowMessageBox(const CMessageBox &MessageBox) = 0;

	virtual bool IsBackendInitialized() = 0;

protected:
	CTextureHandle CreateTextureHandle(int Index, uint32_t Generation = 0)
	{
		CTextureHandle Tex;
		Tex.m_Id = Index;
		Tex.m_Generation = Generation;
		return Tex;
	}

	CRenderTargetHandle CreateRenderTargetHandle(int Index)
	{
		CRenderTargetHandle Target;
		Target.m_Id = Index;
		return Target;
	}

	CRenderTargetReadbackHandle CreateRenderTargetReadbackHandle(int Index, uint32_t Generation)
	{
		CRenderTargetReadbackHandle Readback;
		Readback.m_Id = Index;
		Readback.m_Generation = Generation;
		return Readback;
	}

public:
	// TClient
	virtual void SetForcedAspect(bool Force) = 0;
	virtual void SetGameScreenAspectOverride(float Aspect) = 0;
};

class IEngineGraphics : public IGraphics
{
	MACRO_INTERFACE("enginegraphics")
public:
	virtual int Init() = 0;
	void Shutdown() override = 0;

	virtual void Minimize() = 0;

	virtual int WindowActive() = 0;
	virtual int WindowOpen() = 0;
};

extern IEngineGraphics *CreateEngineGraphicsThreaded();

/**
 * This function should only be used when the graphics are not initialized or when @link IGraphics::ShowMessageBox @endlink failed.
 *
 * @see IGraphics::ShowMessageBox
 */
extern std::optional<int> ShowMessageBoxWithoutGraphics(const IGraphics::CMessageBox &MessageBox);

#endif
