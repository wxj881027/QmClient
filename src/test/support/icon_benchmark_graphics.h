// 只替换设备边界：字形查找、光栅化、缓存和容器仍由生产实现执行。
#ifndef TEST_SUPPORT_ICON_BENCHMARK_GRAPHICS_H
#define TEST_SUPPORT_ICON_BENCHMARK_GRAPHICS_H
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/storage.h>

#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
class CIconBenchmarkGraphics : public IGraphics
{
public:
	IStorage *m_pStorage = nullptr;
	uint64_t m_Draws = 0, m_Uploads = 0, m_UploadBytes = 0, m_BufferBytes = 0, m_Quads = 0;
	// 生命周期校验仅在预检执行，测量期间不维护哈希表，避免设备替身偏置。
	bool m_ValidateLifecycle = true;
	std::unordered_map<int, int> m_Containers;
	std::unordered_map<int, size_t> m_Buffers;
	int m_NextId = 1;
	CIconBenchmarkGraphics()
	{
		m_ScreenWidth = m_DrawableWidth = 1920;
		m_ScreenHeight = m_DrawableHeight = 1080;
		m_ScreenHiDPIScale = 1;
		m_ScreenRefreshRate = 60;
	}
	void ResetCounters() { m_Draws = m_Uploads = m_UploadBytes = m_BufferBytes = m_Quads = 0; }
	void Shutdown() override {}
	void WarnPngliteIncompatibleImages(bool Warn) override { throw std::logic_error("unexpected graphics boundary: WarnPngliteIncompatibleImages"); }
	void SetWindowParams(int FullscreenMode, bool IsBorderless) override { throw std::logic_error("unexpected graphics boundary: SetWindowParams"); }
	bool SetWindowScreen(int Index, bool MoveToCenter) override { throw std::logic_error("unexpected graphics boundary: SetWindowScreen"); }
	bool SwitchWindowScreen(int Index, bool MoveToCenter) override { throw std::logic_error("unexpected graphics boundary: SwitchWindowScreen"); }
	bool SetVSync(bool State) override { throw std::logic_error("unexpected graphics boundary: SetVSync"); }
	bool SetMultiSampling(uint32_t ReqMultiSamplingCount, uint32_t &MultiSamplingCountBackend) override { throw std::logic_error("unexpected graphics boundary: SetMultiSampling"); }
	int GetWindowScreen() override { throw std::logic_error("unexpected graphics boundary: GetWindowScreen"); }
	void Move(int x, int y) override { throw std::logic_error("unexpected graphics boundary: Move"); }
	bool Resize(int w, int h, int RefreshRate) override { throw std::logic_error("unexpected graphics boundary: Resize"); }
	void ResizeToScreen() override { throw std::logic_error("unexpected graphics boundary: ResizeToScreen"); }
	void GotResized(int w, int h, int RefreshRate) override { throw std::logic_error("unexpected graphics boundary: GotResized"); }
	void UpdateViewport(int X, int Y, int W, int H, bool ByResize) override { throw std::logic_error("unexpected graphics boundary: UpdateViewport"); }
	bool IsScreenKeyboardShown() override { throw std::logic_error("unexpected graphics boundary: IsScreenKeyboardShown"); }
	void AddWindowResizeListener(WINDOW_RESIZE_FUNC pFunc) override { throw std::logic_error("unexpected graphics boundary: AddWindowResizeListener"); }
	void AddWindowPropChangeListener(WINDOW_PROPS_CHANGED_FUNC pFunc) override { throw std::logic_error("unexpected graphics boundary: AddWindowPropChangeListener"); }
	void AddGraphicsResourcesResetListener(GRAPHICS_RESOURCES_RESET_FUNC pFunc) override { throw std::logic_error("unexpected graphics boundary: AddGraphicsResourcesResetListener"); }
	uint32_t GraphicsResourcesResetVersion() const override { return 0; }
	void WindowDestroyNtf(uint32_t WindowId) override { throw std::logic_error("unexpected graphics boundary: WindowDestroyNtf"); }
	void WindowCreateNtf(uint32_t WindowId) override { throw std::logic_error("unexpected graphics boundary: WindowCreateNtf"); }
	void Clear(float r, float g, float b, bool ForceClearNow = false) override { throw std::logic_error("unexpected graphics boundary: Clear"); }
	void ClipEnable(int x, int y, int w, int h) override { throw std::logic_error("unexpected graphics boundary: ClipEnable"); }
	void ClipDisable() override { throw std::logic_error("unexpected graphics boundary: ClipDisable"); }
	void MapScreen(float TopLeftX, float TopLeftY, float BottomRightX, float BottomRightY) override {}
	void GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY) const override
	{
		*pTopLeftX = *pTopLeftY = 0;
		*pBottomRightX = 1920;
		*pBottomRightY = 1080;
	}
	void BlendNone() override { throw std::logic_error("unexpected graphics boundary: BlendNone"); }
	void BlendNormal() override { throw std::logic_error("unexpected graphics boundary: BlendNormal"); }
	void BlendAdditive() override { throw std::logic_error("unexpected graphics boundary: BlendAdditive"); }
	void WrapNormal() override { throw std::logic_error("unexpected graphics boundary: WrapNormal"); }
	void WrapClamp() override { throw std::logic_error("unexpected graphics boundary: WrapClamp"); }
	uint64_t TextureMemoryUsage() const override { throw std::logic_error("unexpected graphics boundary: TextureMemoryUsage"); }
	uint64_t BufferMemoryUsage() const override { throw std::logic_error("unexpected graphics boundary: BufferMemoryUsage"); }
	uint64_t StreamedMemoryUsage() const override { throw std::logic_error("unexpected graphics boundary: StreamedMemoryUsage"); }
	uint64_t StagingMemoryUsage() const override { throw std::logic_error("unexpected graphics boundary: StagingMemoryUsage"); }
	const TTwGraphicsGpuList &GetGpus() const override { throw std::logic_error("unexpected graphics boundary: GetGpus"); }
	bool LoadPng(CImageInfo &Image, const char *pFilename, int StorageType) override
	{
		IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
		if(!File)
			return false;
		int Incompatible = 0;
		const bool Success = CImageLoader::LoadPng(File, pFilename, Image, Incompatible);
		return Success;
	}
	bool LoadPng(CImageInfo &Image, const uint8_t *pData, size_t DataSize, const char *pContextName) override { throw std::logic_error("unexpected graphics boundary: LoadPng"); }
	bool CheckImageDivisibility(const char *pContextName, CImageInfo &Image, int DivX, int DivY, bool AllowResize) override { throw std::logic_error("unexpected graphics boundary: CheckImageDivisibility"); }
	bool IsImageFormatRgba(const char *pContextName, const CImageInfo &Image) override { throw std::logic_error("unexpected graphics boundary: IsImageFormatRgba"); }
	void UnloadTexture(CTextureHandle *pIndex) override {}
	CTextureHandle LoadTextureRaw(const CImageInfo &Image, int Flags, const char *pTexName = nullptr) override
	{
		++m_Uploads;
		m_UploadBytes += Image.DataSize();
		return CreateTextureHandle(m_NextId++);
	}
	CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pTexName = nullptr) override
	{
		auto Handle = CreateTextureHandle(m_NextId++);
		++m_Uploads;
		m_UploadBytes += Image.DataSize();
		Image.Free();
		return Handle;
	}
	CTextureHandle LoadTexture(const char *pFilename, int StorageType, int Flags = 0) override
	{
		CImageInfo Image;
		if(!LoadPng(Image, pFilename, StorageType))
			return {};
		return LoadTextureRawMove(Image, Flags, pFilename);
	}
	bool IsTextureHandleAllocated(CTextureHandle Handle) const override { return Handle.IsValid(); }
	void TextureSet(CTextureHandle Texture) override {}
	bool IsRenderTargetSupported() const override { throw std::logic_error("unexpected graphics boundary: IsRenderTargetSupported"); }
	bool IsRenderTargetGaussianBlurSupported() const override { throw std::logic_error("unexpected graphics boundary: IsRenderTargetGaussianBlurSupported"); }
	bool IsBackbufferCaptureSupported() const override { throw std::logic_error("unexpected graphics boundary: IsBackbufferCaptureSupported"); }
	const char *RenderTargetSupportReason() const override { throw std::logic_error("unexpected graphics boundary: RenderTargetSupportReason"); }
	CRenderTargetHandle CreateRenderTarget(int Width, int Height) override { throw std::logic_error("unexpected graphics boundary: CreateRenderTarget"); }
	void DestroyRenderTarget(CRenderTargetHandle *pTarget) override { throw std::logic_error("unexpected graphics boundary: DestroyRenderTarget"); }
	bool BeginRenderTarget(CRenderTargetHandle Target, ColorRGBA ClearColor) override { throw std::logic_error("unexpected graphics boundary: BeginRenderTarget"); }
	void EndRenderTarget() override { throw std::logic_error("unexpected graphics boundary: EndRenderTarget"); }
	void DrawRenderTarget(CRenderTargetHandle Target, const SRenderTargetDrawParams &Params) override { throw std::logic_error("unexpected graphics boundary: DrawRenderTarget"); }
	bool CaptureBackbufferToRenderTarget(CRenderTargetHandle Target) override { throw std::logic_error("unexpected graphics boundary: CaptureBackbufferToRenderTarget"); }
	bool GaussianBlurRenderTarget(CRenderTargetHandle Source, const std::array<CRenderTargetHandle, DUAL_KAWASE_PYRAMID_LEVELS> &aTemporary, CRenderTargetHandle Destination, const SGaussianBlurParams &Params) override { throw std::logic_error("unexpected graphics boundary: GaussianBlurRenderTarget"); }
	bool DualBlurRenderTarget(CRenderTargetHandle Source, CRenderTargetHandle Downsample, CRenderTargetHandle DownsampleTemporary, CRenderTargetHandle DownsampleBlurred, CRenderTargetHandle Destination, const SGaussianBlurParams &Params) override { throw std::logic_error("unexpected graphics boundary: DualBlurRenderTarget"); }
	CRenderTargetReadbackHandle BeginRenderTargetReadback(CRenderTargetHandle Target) override { throw std::logic_error("unexpected graphics boundary: BeginRenderTargetReadback"); }
	ERenderTargetReadbackState PollRenderTargetReadback(CRenderTargetReadbackHandle Handle) override { throw std::logic_error("unexpected graphics boundary: PollRenderTargetReadback"); }
	bool ResolveRenderTargetReadback(CRenderTargetReadbackHandle *pHandle, CImageInfo &Image) override { throw std::logic_error("unexpected graphics boundary: ResolveRenderTargetReadback"); }
	void CancelRenderTargetReadback(CRenderTargetReadbackHandle *pHandle) override { throw std::logic_error("unexpected graphics boundary: CancelRenderTargetReadback"); }
	bool ReadRenderTarget(CRenderTargetHandle Target, CImageInfo &Image) override { throw std::logic_error("unexpected graphics boundary: ReadRenderTarget"); }
	bool LoadTextTextures(size_t Width, size_t Height, CTextureHandle &TextTexture, CTextureHandle &TextOutlineTexture, uint8_t *pTextData, uint8_t *pTextOutlineData) override
	{
		TextTexture = CreateTextureHandle(m_NextId++);
		TextOutlineTexture = CreateTextureHandle(m_NextId++);
		m_Uploads += 2;
		m_UploadBytes += Width * Height * 2;
		free(pTextData);
		free(pTextOutlineData);
		return true;
	}
	bool UnloadTextTextures(CTextureHandle &TextTexture, CTextureHandle &TextOutlineTexture) override
	{
		TextTexture.Invalidate();
		TextOutlineTexture.Invalidate();
		return true;
	}
	bool UpdateTextTexture(CTextureHandle TextureId, int x, int y, size_t Width, size_t Height, uint8_t *pData, bool IsMovedPointer) override
	{
		++m_Uploads;
		m_UploadBytes += Width * Height;
		if(IsMovedPointer)
			free(pData);
		return true;
	}
	bool UpdateTexture(CTextureHandle TextureId, int x, int y, size_t Width, size_t Height, uint8_t *pData, bool IsMovedPointer) override { throw std::logic_error("unexpected graphics boundary: UpdateTexture"); }
	CTextureHandle LoadSpriteTexture(const CImageInfo &FromImageInfo, const std::optional<CImageInfo> &FallbackImageInfo, const struct CDataSprite *pSprite) override { throw std::logic_error("unexpected graphics boundary: LoadSpriteTexture"); }
	bool IsImageSubFullyTransparent(const CImageInfo &FromImageInfo, int x, int y, int w, int h) override { throw std::logic_error("unexpected graphics boundary: IsImageSubFullyTransparent"); }
	bool IsSpriteTextureFullyTransparent(const CImageInfo &FromImageInfo, const struct CDataSprite *pSprite) override { throw std::logic_error("unexpected graphics boundary: IsSpriteTextureFullyTransparent"); }
	void FlushVertices(bool KeepVertices = false) override {}
	void FlushVerticesTex3D() override { throw std::logic_error("unexpected graphics boundary: FlushVerticesTex3D"); }
	void RenderTileLayer(int BufferContainerIndex, const ColorRGBA &Color, char **pOffsets, unsigned int *pIndicedVertexDrawNum, size_t NumIndicesOffset) override { throw std::logic_error("unexpected graphics boundary: RenderTileLayer"); }
	void RenderBorderTiles(int BufferContainerIndex, const ColorRGBA &Color, char *pIndexBufferOffset, const vec2 &Offset, const vec2 &Scale, uint32_t DrawNum) override { throw std::logic_error("unexpected graphics boundary: RenderBorderTiles"); }
	void RenderQuadLayer(int BufferContainerIndex, SQuadRenderInfo *pQuadInfo, size_t QuadNum, int QuadOffset, bool Grouped = false) override { throw std::logic_error("unexpected graphics boundary: RenderQuadLayer"); }
	void RenderText(int BufferContainerIndex, int TextQuadNum, int TextureSize, int TextureTextIndex, int TextureTextOutlineIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override
	{
		if(m_ValidateLifecycle && !m_Containers.contains(BufferContainerIndex))
			throw std::logic_error("draw of deleted container");
		++m_Draws;
		m_Quads += TextQuadNum;
	}
	void RenderMediaIslandSdf(const SMediaIslandSdfParams &Params, CRenderTargetHandle Backdrop = CRenderTargetHandle()) override { throw std::logic_error("unexpected graphics boundary: RenderMediaIslandSdf"); }
	void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override { throw std::logic_error("unexpected graphics boundary: RenderRoundedRectSdf"); }
	void DrawRoundedRectAntialias(float x, float y, float w, float h, float Radius, int Corners, const ColorRGBA &Color) override { throw std::logic_error("unexpected graphics boundary: DrawRoundedRectAntialias"); }
	void RenderProceduralRing(const SProceduralRingParams &Params) override
	{
		++m_Draws;
		++m_Quads;
	}
	int CreateBufferObject(size_t UploadDataSize, void *pUploadData, int CreateFlags, bool IsMovedPointer = false) override
	{
		m_BufferBytes += UploadDataSize;
		if(IsMovedPointer)
			free(pUploadData);
		const int Id = m_NextId++;
		if(m_ValidateLifecycle)
			m_Buffers.emplace(Id, UploadDataSize);
		return Id;
	}
	void RecreateBufferObject(int BufferIndex, size_t UploadDataSize, void *pUploadData, int CreateFlags, bool IsMovedPointer = false) override
	{
		if(m_ValidateLifecycle)
		{
			if(!m_Buffers.contains(BufferIndex))
				throw std::logic_error("upload to deleted buffer");
			m_Buffers[BufferIndex] = UploadDataSize;
		}
		m_BufferBytes += UploadDataSize;
		if(IsMovedPointer)
			free(pUploadData);
	}
	void DeleteBufferObject(int BufferIndex) override
	{
		if(m_ValidateLifecycle)
			m_Buffers.erase(BufferIndex);
	}
	int CreateBufferContainer(struct SBufferContainerInfo *pContainerInfo) override
	{
		const int Id = m_NextId++;
		if(m_ValidateLifecycle)
			m_Containers.emplace(Id, pContainerInfo->m_VertBufferBindingIndex);
		return Id;
	}
	void DeleteBufferContainer(int &ContainerIndex, bool DestroyAllBO = true) override
	{
		if(auto Entry = m_ValidateLifecycle ? m_Containers.find(ContainerIndex) : m_Containers.end(); Entry != m_Containers.end())
		{
			if(DestroyAllBO)
				m_Buffers.erase(Entry->second);
			m_Containers.erase(Entry);
		}
		ContainerIndex = -1;
	}
	void IndicesNumRequiredNotify(unsigned int RequiredIndicesCount) override {}
	bool GetDriverVersion(EGraphicsDriverAgeType DriverAgeType, int &Major, int &Minor, int &Patch, const char *&pName, EBackendType BackendType) override { throw std::logic_error("unexpected graphics boundary: GetDriverVersion"); }
	bool IsConfigModernAPI() override { throw std::logic_error("unexpected graphics boundary: IsConfigModernAPI"); }
	bool HasMediaIslandSdf() override { throw std::logic_error("unexpected graphics boundary: HasMediaIslandSdf"); }
	bool HasRoundedRectSdf() override { throw std::logic_error("unexpected graphics boundary: HasRoundedRectSdf"); }
	bool HasProceduralRing() override { return true; }
	bool IsTileBufferingEnabled() override { throw std::logic_error("unexpected graphics boundary: IsTileBufferingEnabled"); }
	bool IsQuadBufferingEnabled() override { throw std::logic_error("unexpected graphics boundary: IsQuadBufferingEnabled"); }
	bool IsTextBufferingEnabled() override { return true; }
	bool IsQuadContainerBufferingEnabled() override { throw std::logic_error("unexpected graphics boundary: IsQuadContainerBufferingEnabled"); }
	bool Uses2DTextureArrays() override { throw std::logic_error("unexpected graphics boundary: Uses2DTextureArrays"); }
	bool HasTextureArraysSupport() const override { throw std::logic_error("unexpected graphics boundary: HasTextureArraysSupport"); }
	const char *GetVendorString() override { throw std::logic_error("unexpected graphics boundary: GetVendorString"); }
	const char *GetVersionString() override { throw std::logic_error("unexpected graphics boundary: GetVersionString"); }
	const char *GetRendererString() override { throw std::logic_error("unexpected graphics boundary: GetRendererString"); }
	const char *GetFatalError() const override { throw std::logic_error("unexpected graphics boundary: GetFatalError"); }
	void LinesBegin() override { throw std::logic_error("unexpected graphics boundary: LinesBegin"); }
	void LinesEnd() override { throw std::logic_error("unexpected graphics boundary: LinesEnd"); }
	void LinesDraw(const CLineItem *pArray, size_t Num) override { throw std::logic_error("unexpected graphics boundary: LinesDraw"); }
	void LinesBatchBegin(CLineItemBatch *pBatch) override { throw std::logic_error("unexpected graphics boundary: LinesBatchBegin"); }
	void LinesBatchEnd(CLineItemBatch *pBatch) override { throw std::logic_error("unexpected graphics boundary: LinesBatchEnd"); }
	void LinesBatchDraw(CLineItemBatch *pBatch, const CLineItem *pArray, size_t Num) override { throw std::logic_error("unexpected graphics boundary: LinesBatchDraw"); }
	void QuadsBegin() override {}
	void QuadsEnd() override {}
	void QuadsTex3DBegin() override { throw std::logic_error("unexpected graphics boundary: QuadsTex3DBegin"); }
	void QuadsTex3DEnd() override { throw std::logic_error("unexpected graphics boundary: QuadsTex3DEnd"); }
	void TrianglesBegin() override { throw std::logic_error("unexpected graphics boundary: TrianglesBegin"); }
	void TrianglesEnd() override { throw std::logic_error("unexpected graphics boundary: TrianglesEnd"); }
	void QuadsEndKeepVertices() override {}
	void QuadsDrawCurrentVertices(bool KeepVertices = true) override {}
	void QuadsSetRotation(float Angle) override {}
	void QuadsSetSubset(float TopLeftU, float TopLeftV, float BottomRightU, float BottomRightV) override {}
	void QuadsSetSubsetFree(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int Index = -1) override { throw std::logic_error("unexpected graphics boundary: QuadsSetSubsetFree"); }
	void QuadsDraw(CQuadItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadsDraw"); }
	void QuadsDrawTL(const CQuadItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadsDrawTL"); }
	void QuadsTex3DDrawTL(const CQuadItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadsTex3DDrawTL"); }
	int CreateQuadContainer(bool AutomaticUpload = true) override { throw std::logic_error("unexpected graphics boundary: CreateQuadContainer"); }
	void QuadContainerChangeAutomaticUpload(int ContainerIndex, bool AutomaticUpload) override { throw std::logic_error("unexpected graphics boundary: QuadContainerChangeAutomaticUpload"); }
	void QuadContainerUpload(int ContainerIndex) override { throw std::logic_error("unexpected graphics boundary: QuadContainerUpload"); }
	int QuadContainerAddQuads(int ContainerIndex, CQuadItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddQuads"); }
	int QuadContainerAddQuads(int ContainerIndex, CFreeformItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddQuads"); }
	void QuadContainerReset(int ContainerIndex) override { throw std::logic_error("unexpected graphics boundary: QuadContainerReset"); }
	void DeleteQuadContainer(int &ContainerIndex) override {}
	void RenderQuadContainer(int ContainerIndex, int QuadDrawNum) override { throw std::logic_error("unexpected graphics boundary: RenderQuadContainer"); }
	void RenderQuadContainer(int ContainerIndex, int QuadOffset, int QuadDrawNum, bool ChangeWrapMode = true) override { throw std::logic_error("unexpected graphics boundary: RenderQuadContainer"); }
	void RenderQuadContainerEx(int ContainerIndex, int QuadOffset, int QuadDrawNum, float X, float Y, float ScaleX = 1.f, float ScaleY = 1.f) override { throw std::logic_error("unexpected graphics boundary: RenderQuadContainerEx"); }
	void RenderQuadContainerAsSprite(int ContainerIndex, int QuadOffset, float X, float Y, float ScaleX = 1.f, float ScaleY = 1.f) override { throw std::logic_error("unexpected graphics boundary: RenderQuadContainerAsSprite"); }
	void RenderQuadContainerAsSpriteMultiple(int ContainerIndex, int QuadOffset, int DrawCount, SRenderSpriteInfo *pRenderInfo) override { throw std::logic_error("unexpected graphics boundary: RenderQuadContainerAsSpriteMultiple"); }
	void QuadsDrawFreeform(const CFreeformItem *pArray, int Num) override { throw std::logic_error("unexpected graphics boundary: QuadsDrawFreeform"); }
	void QuadsText(float x, float y, float Size, const char *pText) override { throw std::logic_error("unexpected graphics boundary: QuadsText"); }
	void SelectSprite(int Id, int Flags = 0) override { throw std::logic_error("unexpected graphics boundary: SelectSprite"); }
	void SelectSprite7(int Id, int Flags = 0) override { throw std::logic_error("unexpected graphics boundary: SelectSprite7"); }
	void GetSpriteScale(const CDataSprite *pSprite, float &ScaleX, float &ScaleY) const override { throw std::logic_error("unexpected graphics boundary: GetSpriteScale"); }
	void GetSpriteScale(int Id, float &ScaleX, float &ScaleY) const override { throw std::logic_error("unexpected graphics boundary: GetSpriteScale"); }
	void GetSpriteScaleImpl(int Width, int Height, float &ScaleX, float &ScaleY) const override { throw std::logic_error("unexpected graphics boundary: GetSpriteScaleImpl"); }
	void DrawSprite(float x, float y, float Size) override { throw std::logic_error("unexpected graphics boundary: DrawSprite"); }
	void DrawSprite(float x, float y, float ScaledWidth, float ScaledHeight) override { throw std::logic_error("unexpected graphics boundary: DrawSprite"); }
	int QuadContainerAddSprite(int QuadContainerIndex, float x, float y, float Size) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddSprite"); }
	int QuadContainerAddSprite(int QuadContainerIndex, float Size) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddSprite"); }
	int QuadContainerAddSprite(int QuadContainerIndex, float Width, float Height) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddSprite"); }
	int QuadContainerAddSprite(int QuadContainerIndex, float X, float Y, float Width, float Height) override { throw std::logic_error("unexpected graphics boundary: QuadContainerAddSprite"); }
	void DrawRectExt(float x, float y, float w, float h, float r, int Corners) override { throw std::logic_error("unexpected graphics boundary: DrawRectExt"); }
	void DrawRectExt4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, float r, int Corners) override { throw std::logic_error("unexpected graphics boundary: DrawRectExt4"); }
	int CreateRectQuadContainer(float x, float y, float w, float h, float r, int Corners) override { throw std::logic_error("unexpected graphics boundary: CreateRectQuadContainer"); }
	void DrawRect(float x, float y, float w, float h, ColorRGBA Color, int Corners, float Rounding) override { throw std::logic_error("unexpected graphics boundary: DrawRect"); }
	void DrawRect4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, int Corners, float Rounding) override { throw std::logic_error("unexpected graphics boundary: DrawRect4"); }
	void DrawCircle(float CenterX, float CenterY, float Radius, int Segments) override { throw std::logic_error("unexpected graphics boundary: DrawCircle"); }
	void SetColorVertex(const CColorVertex *pArray, size_t Num) override { throw std::logic_error("unexpected graphics boundary: SetColorVertex"); }
	void SetColor(float r, float g, float b, float a) override {}
	void SetColor(ColorRGBA Color) override {}
	void SetColor2(ColorRGBA First, ColorRGBA Second) override { throw std::logic_error("unexpected graphics boundary: SetColor2"); }
	void SetColor4(ColorRGBA TopLeft, ColorRGBA TopRight, ColorRGBA BottomLeft, ColorRGBA BottomRight) override {}
	void ChangeColorOfQuadVertices(size_t QuadOffset, unsigned char r, unsigned char g, unsigned char b, unsigned char a) override {}
	void ReadPixel(ivec2 Position, ColorRGBA *pColor) override { throw std::logic_error("unexpected graphics boundary: ReadPixel"); }
	void TakeScreenshot(const char *pFilename) override { throw std::logic_error("unexpected graphics boundary: TakeScreenshot"); }
	void TakeScreenshot(const char *pFilename, FScreenshotCallback pfnCallback, FScreenshotProcessor pfnProcessor = nullptr) override { throw std::logic_error("unexpected graphics boundary: TakeScreenshot"); }
	void TakeCustomScreenshot(const char *pFilename) override { throw std::logic_error("unexpected graphics boundary: TakeCustomScreenshot"); }
	int GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen) override { throw std::logic_error("unexpected graphics boundary: GetVideoModes"); }
	void GetCurrentVideoMode(CVideoMode &CurMode, int Screen) override { throw std::logic_error("unexpected graphics boundary: GetCurrentVideoMode"); }
	void Swap() override { throw std::logic_error("unexpected graphics boundary: Swap"); }
	int GetNumScreens() const override { throw std::logic_error("unexpected graphics boundary: GetNumScreens"); }
	const char *GetScreenName(int Screen) const override { throw std::logic_error("unexpected graphics boundary: GetScreenName"); }
	void InsertSignal(class CSemaphore *pSemaphore) override { throw std::logic_error("unexpected graphics boundary: InsertSignal"); }
	bool IsIdle() const override { throw std::logic_error("unexpected graphics boundary: IsIdle"); }
	void WaitForIdle() override { throw std::logic_error("unexpected graphics boundary: WaitForIdle"); }
	void SetWindowGrab(bool Grab) override { throw std::logic_error("unexpected graphics boundary: SetWindowGrab"); }
	void NotifyWindow() override {}
	TGLBackendReadPresentedImageData &GetReadPresentedImageDataFuncUnsafe() override { throw std::logic_error("unexpected graphics boundary: GetReadPresentedImageDataFuncUnsafe"); }
	std::optional<SWarning> CurrentWarning() override { throw std::logic_error("unexpected graphics boundary: CurrentWarning"); }
	std::optional<int> ShowMessageBox(const CMessageBox &MessageBox) override { throw std::logic_error("unexpected graphics boundary: ShowMessageBox"); }
	bool IsBackendInitialized() override { throw std::logic_error("unexpected graphics boundary: IsBackendInitialized"); }
	void SetForcedAspect(bool Force) override { throw std::logic_error("unexpected graphics boundary: SetForcedAspect"); }
	void SetGameScreenAspectOverride(float Aspect) override { throw std::logic_error("unexpected graphics boundary: SetGameScreenAspectOverride"); }
};
#endif
