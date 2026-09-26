#include "image_manipulation.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <cstdlib>
#include <limits>
#include <utility>

static bool CalculateImageBufferSize(size_t Width, size_t Height, size_t PixelSize, size_t &Size)
{
	if(Width == 0 || Height == 0 || PixelSize == 0)
	{
		Size = 0;
		return false;
	}
	if(Width > std::numeric_limits<size_t>::max() / Height)
	{
		Size = 0;
		return false;
	}
	const size_t PixelCount = Width * Height;
	if(PixelCount > std::numeric_limits<size_t>::max() / PixelSize)
	{
		Size = 0;
		return false;
	}
	Size = PixelCount * PixelSize;
	return true;
}

static bool ConvertToRgbaImpl(uint8_t *pDest, const CImageInfo &SourceImage, bool &AlreadyRgba)
{
	AlreadyRgba = false;
	if(pDest == nullptr || SourceImage.m_pData == nullptr || SourceImage.m_Width == 0 || SourceImage.m_Height == 0)
		return false;

	if(SourceImage.m_Format == CImageInfo::FORMAT_RGBA)
	{
		size_t SourceDataSize = 0;
		if(!SourceImage.DataSize(SourceDataSize))
			return false;
		mem_copy(pDest, SourceImage.m_pData, SourceDataSize);
		AlreadyRgba = true;
		return true;
	}
	if(SourceImage.m_Format != CImageInfo::FORMAT_RGB && SourceImage.m_Format != CImageInfo::FORMAT_RA && SourceImage.m_Format != CImageInfo::FORMAT_R)
		return false;

	const size_t SrcChannelCount = CImageInfo::PixelSize(SourceImage.m_Format);
	const size_t DstChannelCount = CImageInfo::PixelSize(CImageInfo::FORMAT_RGBA);
	for(size_t Y = 0; Y < SourceImage.m_Height; ++Y)
	{
		for(size_t X = 0; X < SourceImage.m_Width; ++X)
		{
			size_t ImgOffsetSrc = (Y * SourceImage.m_Width * SrcChannelCount) + (X * SrcChannelCount);
			size_t ImgOffsetDest = (Y * SourceImage.m_Width * DstChannelCount) + (X * DstChannelCount);
			if(SourceImage.m_Format == CImageInfo::FORMAT_RGB)
			{
				mem_copy(&pDest[ImgOffsetDest], &SourceImage.m_pData[ImgOffsetSrc], SrcChannelCount);
				pDest[ImgOffsetDest + 3] = 255;
			}
			else if(SourceImage.m_Format == CImageInfo::FORMAT_RA)
			{
				pDest[ImgOffsetDest + 0] = SourceImage.m_pData[ImgOffsetSrc];
				pDest[ImgOffsetDest + 1] = SourceImage.m_pData[ImgOffsetSrc];
				pDest[ImgOffsetDest + 2] = SourceImage.m_pData[ImgOffsetSrc];
				pDest[ImgOffsetDest + 3] = SourceImage.m_pData[ImgOffsetSrc + 1];
			}
			else if(SourceImage.m_Format == CImageInfo::FORMAT_R)
			{
				pDest[ImgOffsetDest + 0] = 255;
				pDest[ImgOffsetDest + 1] = 255;
				pDest[ImgOffsetDest + 2] = 255;
				pDest[ImgOffsetDest + 3] = SourceImage.m_pData[ImgOffsetSrc];
			}
			else
			{
				dbg_assert_failed("SourceImage.m_Format invalid");
				return false;
			}
		}
	}
	return true;
}

bool ConvertToRgba(uint8_t *pDest, const CImageInfo &SourceImage)
{
	bool AlreadyRgba = false;
	return ConvertToRgbaImpl(pDest, SourceImage, AlreadyRgba) && AlreadyRgba;
}

bool ConvertToRgbaAlloc(uint8_t *&pDest, const CImageInfo &SourceImage)
{
	pDest = nullptr;

	size_t DestDataSize = 0;
	if(!CalculateImageBufferSize(SourceImage.m_Width, SourceImage.m_Height, CImageInfo::PixelSize(CImageInfo::FORMAT_RGBA), DestDataSize))
		return false;

	pDest = static_cast<uint8_t *>(malloc(DestDataSize));
	if(pDest == nullptr)
		return false;

	bool AlreadyRgba = false;
	if(!ConvertToRgbaImpl(pDest, SourceImage, AlreadyRgba))
	{
		free(pDest);
		pDest = nullptr;
		return false;
	}
	return AlreadyRgba;
}

bool ConvertToRgba(CImageInfo &Image)
{
	if(Image.m_Format == CImageInfo::FORMAT_RGBA)
		return true;

	uint8_t *pRgbaData = nullptr;
	ConvertToRgbaAlloc(pRgbaData, Image);
	if(pRgbaData == nullptr)
		return false;

	free(Image.m_pData);
	Image.m_pData = pRgbaData;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	return false;
}

static inline void ConvertToGrayscalePixel(const CImageInfo &Image, size_t PixelIndex, size_t Step)
{
	const uint8_t R = Image.m_pData[PixelIndex * Step];
	const uint8_t G = Image.m_pData[PixelIndex * Step + 1];
	const uint8_t B = Image.m_pData[PixelIndex * Step + 2];
	const uint8_t Luma = (uint8_t)(0.2126f * R + 0.7152f * G + 0.0722f * B);

	Image.m_pData[PixelIndex * Step] = Luma;
	Image.m_pData[PixelIndex * Step + 1] = Luma;
	Image.m_pData[PixelIndex * Step + 2] = Luma;
}

void ConvertToGrayscale(const CImageInfo &Image)
{
	if(Image.m_Format == CImageInfo::FORMAT_R || Image.m_Format == CImageInfo::FORMAT_RA)
		return;

	const size_t Step = Image.PixelSize();
	for(size_t PixelIndex = 0; PixelIndex < Image.m_Width * Image.m_Height; ++PixelIndex)
	{
		ConvertToGrayscalePixel(Image, PixelIndex, Step);
	}
}

void ConvertToGrayscaleRect(const CImageInfo &Image, size_t StartX, size_t StartY, size_t Width, size_t Height)
{
	if(Image.m_Format == CImageInfo::FORMAT_R || Image.m_Format == CImageInfo::FORMAT_RA)
		return;

	const size_t Step = Image.PixelSize();
	for(size_t PixelY = StartY; PixelY < StartY + Height; ++PixelY)
	{
		for(size_t PixelX = StartX; PixelX < StartX + Width; ++PixelX)
		{
			const size_t PixelIndex = PixelY * Image.m_Width + PixelX;
			ConvertToGrayscalePixel(Image, PixelIndex, Step);
		}
	}
}

void ColorizeWithHueRect(CImageInfo &Image, float Hue, float Sat, size_t StartX, size_t StartY, size_t Width, size_t Height)
{
	dbg_assert(Hue >= 0.0f && Hue <= 1.0f, "Invalid hue");
	dbg_assert(Sat >= 0.0f && Sat <= 1.0f, "Invalid saturation");
	dbg_assert(Image.m_Format == CImageInfo::FORMAT_RGB || Image.m_Format == CImageInfo::FORMAT_RGBA, "Invalid image format");
	dbg_assert(StartX + Width <= Image.m_Width && StartY + Height <= Image.m_Height, "Image rect is out of range");

	const size_t Step = Image.PixelSize();
	for(size_t PixelY = StartY; PixelY < StartY + Height; ++PixelY)
	{
		for(size_t PixelX = StartX; PixelX < StartX + Width; ++PixelX)
		{
			const size_t PixelIndex = PixelY * Image.m_Width + PixelX;
			uint8_t &R = Image.m_pData[PixelIndex * Step];
			uint8_t &G = Image.m_pData[PixelIndex * Step + 1];
			uint8_t &B = Image.m_pData[PixelIndex * Step + 2];

			ColorRGBA PixelColor(R / 255.0f, G / 255.0f, B / 255.0f, 1.0f);
			ColorHSLA PixelColorHSLA = color_cast<ColorHSLA>(PixelColor);
			PixelColorHSLA.h = Hue;
			PixelColorHSLA.s = Sat;
			PixelColor = color_cast<ColorRGBA>(PixelColorHSLA);

			R = static_cast<uint8_t>(PixelColor.r * 255.0f);
			G = static_cast<uint8_t>(PixelColor.g * 255.0f);
			B = static_cast<uint8_t>(PixelColor.b * 255.0f);
		}
	}
}

static constexpr int DILATE_BPP = 4; // RGBA assumed
static constexpr uint8_t DILATE_ALPHA_THRESHOLD = 10;

static void Dilate(int w, int h, const uint8_t *pSrc, uint8_t *pDest)
{
	const int aDirX[] = {0, -1, 1, 0};
	const int aDirY[] = {-1, 0, 0, 1};

	int m = 0;
	for(int y = 0; y < h; y++)
	{
		for(int x = 0; x < w; x++, m += DILATE_BPP)
		{
			for(int i = 0; i < DILATE_BPP; ++i)
				pDest[m + i] = pSrc[m + i];
			if(pSrc[m + DILATE_BPP - 1] > DILATE_ALPHA_THRESHOLD)
				continue;

			// --- Implementation Note ---
			// The sum and counter variable can be used to compute a smoother dilated image.
			// In this reference implementation, the loop breaks as soon as Counter == 1.
			// We break the loop here to match the selection of the previously used algorithm.
			int aSumOfOpaque[] = {0, 0, 0};
			int Counter = 0;
			for(int c = 0; c < 4; c++)
			{
				const int ClampedX = std::clamp(x + aDirX[c], 0, w - 1);
				const int ClampedY = std::clamp(y + aDirY[c], 0, h - 1);
				const int SrcIndex = ClampedY * w * DILATE_BPP + ClampedX * DILATE_BPP;
				if(pSrc[SrcIndex + DILATE_BPP - 1] > DILATE_ALPHA_THRESHOLD)
				{
					for(int p = 0; p < DILATE_BPP - 1; ++p)
						aSumOfOpaque[p] += pSrc[SrcIndex + p];
					++Counter;
					break;
				}
			}

			if(Counter > 0)
			{
				for(int i = 0; i < DILATE_BPP - 1; ++i)
				{
					aSumOfOpaque[i] /= Counter;
					pDest[m + i] = (uint8_t)aSumOfOpaque[i];
				}

				pDest[m + DILATE_BPP - 1] = 255;
			}
		}
	}
}

static void CopyColorValues(int w, int h, const uint8_t *pSrc, uint8_t *pDest)
{
	int m = 0;
	for(int y = 0; y < h; y++)
	{
		for(int x = 0; x < w; x++, m += DILATE_BPP)
		{
			if(pDest[m + DILATE_BPP - 1] == 0)
			{
				mem_copy(&pDest[m], &pSrc[m], DILATE_BPP - 1);
			}
		}
	}
}

void DilateImage(uint8_t *pImageBuff, int w, int h)
{
	DilateImageSub(pImageBuff, w, h, 0, 0, w, h);
}

void DilateImage(const CImageInfo &Image)
{
	dbg_assert(Image.m_Format == CImageInfo::FORMAT_RGBA, "Dilate requires RGBA format");
	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
		return;
	DilateImage(Image.m_pData, Image.m_Width, Image.m_Height);
}

void DilateImageSub(uint8_t *pImageBuff, int w, int h, int x, int y, int SubWidth, int SubHeight)
{
	if(pImageBuff == nullptr || w <= 0 || h <= 0 || x < 0 || y < 0 || SubWidth <= 0 || SubHeight <= 0 || x + SubWidth > w || y + SubHeight > h)
		return;

	uint8_t *apBuffer[2] = {nullptr, nullptr};

	size_t ImageSize = 0;
	if(!CalculateImageBufferSize(SubWidth, SubHeight, DILATE_BPP, ImageSize))
		return;
	apBuffer[0] = (uint8_t *)malloc(ImageSize);
	apBuffer[1] = (uint8_t *)malloc(ImageSize);
	uint8_t *pBufferOriginal = (uint8_t *)malloc(ImageSize);
	if(apBuffer[0] == nullptr || apBuffer[1] == nullptr || pBufferOriginal == nullptr)
	{
		free(apBuffer[0]);
		free(apBuffer[1]);
		free(pBufferOriginal);
		return;
	}

	for(int Y = 0; Y < SubHeight; ++Y)
	{
		int SrcImgOffset = ((y + Y) * w * DILATE_BPP) + (x * DILATE_BPP);
		int DstImgOffset = (Y * SubWidth * DILATE_BPP);
		int CopySize = SubWidth * DILATE_BPP;
		mem_copy(&pBufferOriginal[DstImgOffset], &pImageBuff[SrcImgOffset], CopySize);
	}

	Dilate(SubWidth, SubHeight, pBufferOriginal, apBuffer[0]);

	for(int i = 0; i < 5; i++)
	{
		Dilate(SubWidth, SubHeight, apBuffer[0], apBuffer[1]);
		Dilate(SubWidth, SubHeight, apBuffer[1], apBuffer[0]);
	}

	CopyColorValues(SubWidth, SubHeight, apBuffer[0], pBufferOriginal);

	free(apBuffer[0]);
	free(apBuffer[1]);

	for(int Y = 0; Y < SubHeight; ++Y)
	{
		int SrcImgOffset = ((y + Y) * w * DILATE_BPP) + (x * DILATE_BPP);
		int DstImgOffset = (Y * SubWidth * DILATE_BPP);
		int CopySize = SubWidth * DILATE_BPP;
		mem_copy(&pImageBuff[SrcImgOffset], &pBufferOriginal[DstImgOffset], CopySize);
	}

	free(pBufferOriginal);
}

static float CubicHermite(float A, float B, float C, float D, float t)
{
	float a = -A / 2.0f + (3.0f * B) / 2.0f - (3.0f * C) / 2.0f + D / 2.0f;
	float b = A - (5.0f * B) / 2.0f + 2.0f * C - D / 2.0f;
	float c = -A / 2.0f + C / 2.0f;
	float d = B;

	return (a * t * t * t) + (b * t * t) + (c * t) + d;
}

static void GetPixelClamped(const uint8_t *pSourceImage, int x, int y, uint32_t W, uint32_t H, size_t BPP, uint8_t aSample[4])
{
	x = std::clamp<int>(x, 0, (int)W - 1);
	y = std::clamp<int>(y, 0, (int)H - 1);

	mem_copy(aSample, &pSourceImage[x * BPP + (W * BPP * y)], BPP);
}

static void SampleBicubic(const uint8_t *pSourceImage, float u, float v, uint32_t W, uint32_t H, size_t BPP, uint8_t aSample[4])
{
	float X = (u * W) - 0.5f;
	const int RoundedX = (int)X;
	const float FractionX = X - std::floor(X);

	float Y = (v * H) - 0.5f;
	const int RoundedY = (int)Y;
	const float FractionY = Y - std::floor(Y);

	uint8_t aaaSamples[4][4][4];
	for(int y = 0; y < 4; ++y)
	{
		for(int x = 0; x < 4; ++x)
		{
			GetPixelClamped(pSourceImage, RoundedX + x - 1, RoundedY + y - 1, W, H, BPP, aaaSamples[x][y]);
		}
	}

	for(size_t i = 0; i < BPP; i++)
	{
		float aRows[4];
		for(int y = 0; y < 4; ++y)
		{
			aRows[y] = CubicHermite(aaaSamples[0][y][i], aaaSamples[1][y][i], aaaSamples[2][y][i], aaaSamples[3][y][i], FractionX);
		}
		aSample[i] = (uint8_t)std::clamp<float>(CubicHermite(aRows[0], aRows[1], aRows[2], aRows[3], FractionY), 0.0f, 255.0f);
	}
}

static void ResizeImage(const uint8_t *pSourceImage, uint32_t SW, uint32_t SH, uint8_t *pDestinationImage, uint32_t W, uint32_t H, size_t BPP)
{
	for(int y = 0; y < (int)H; ++y)
	{
		const float V = H > 1 ? (float)y / (float)(H - 1) : 0.0f;
		for(int x = 0; x < (int)W; ++x)
		{
			const float U = W > 1 ? (float)x / (float)(W - 1) : 0.0f;
			uint8_t aSample[4];
			SampleBicubic(pSourceImage, U, V, SW, SH, BPP, aSample);
			mem_copy(&pDestinationImage[x * BPP + ((W * BPP) * y)], aSample, BPP);
		}
	}
}

uint8_t *ResizeImage(const uint8_t *pImageData, int Width, int Height, int NewWidth, int NewHeight, int BPP)
{
	if(pImageData == nullptr || Width <= 0 || Height <= 0 || NewWidth <= 0 || NewHeight <= 0 || BPP <= 0)
		return nullptr;

	size_t DataSize = 0;
	if(!CalculateImageBufferSize(NewWidth, NewHeight, BPP, DataSize))
		return nullptr;

	uint8_t *pTmpData = (uint8_t *)malloc(DataSize);
	if(pTmpData == nullptr)
		return nullptr;

	ResizeImage(pImageData, Width, Height, pTmpData, NewWidth, NewHeight, BPP);
	return pTmpData;
}

void ResizeImage(CImageInfo &Image, int NewWidth, int NewHeight)
{
	uint8_t *pNewData = ResizeImage(Image.m_pData, Image.m_Width, Image.m_Height, NewWidth, NewHeight, Image.PixelSize());
	if(pNewData == nullptr)
		return;

	free(Image.m_pData);
	Image.m_pData = pNewData;
	Image.m_Width = NewWidth;
	Image.m_Height = NewHeight;
}

int HighestBit(int OfVar)
{
	if(!OfVar)
		return 0;

	int RetV = 1;

	while(OfVar >>= 1)
		RetV <<= 1;

	return RetV;
}

bool ResolveSpritePixelRect(size_t ImageWidth, size_t ImageHeight, int GridX, int GridY,
	int SpriteX, int SpriteY, int SpriteW, int SpriteH,
	size_t &OutX, size_t &OutY, size_t &OutW, size_t &OutH, bool *pOutOfBounds)
{
	if(pOutOfBounds != nullptr)
		*pOutOfBounds = false;
	if(GridX <= 0 || GridY <= 0 || SpriteX < 0 || SpriteY < 0 || SpriteW <= 0 || SpriteH <= 0)
		return false;

	const size_t GridCountX = (size_t)GridX;
	const size_t GridCountY = (size_t)GridY;
	if(ImageWidth == 0 || ImageHeight == 0 || ImageWidth % GridCountX != 0 || ImageHeight % GridCountY != 0)
		return false;

	const size_t CellWidth = ImageWidth / GridCountX;
	const size_t CellHeight = ImageHeight / GridCountY;
	const size_t SpriteXU = (size_t)SpriteX;
	const size_t SpriteYU = (size_t)SpriteY;
	const size_t SpriteWU = (size_t)SpriteW;
	const size_t SpriteHU = (size_t)SpriteH;
	const size_t MaxSize = std::numeric_limits<size_t>::max();
	if(SpriteXU > MaxSize / CellWidth || SpriteYU > MaxSize / CellHeight ||
		SpriteWU > MaxSize / CellWidth || SpriteHU > MaxSize / CellHeight)
	{
		return false;
	}

	OutX = SpriteXU * CellWidth;
	OutY = SpriteYU * CellHeight;
	OutW = SpriteWU * CellWidth;
	OutH = SpriteHU * CellHeight;
	if(OutW == 0 || OutH == 0 || OutX > ImageWidth || OutY > ImageHeight ||
		OutW > ImageWidth - OutX || OutH > ImageHeight - OutY)
	{
		// 图集网格整除但 sprite 矩形超出图集：视为「图集比默认布局小」，
		// 与真正的坏包（不可整除/无数据）区分开。
		if(pOutOfBounds != nullptr)
			*pOutOfBounds = true;
		return false;
	}
	return true;
}

bool ExtractSpriteImage(const CImageInfo &FromImageInfo, const CDataSprite *pSprite, CImageInfo &Result)
{
	const char *pSpriteName = pSprite && pSprite->m_pName ? pSprite->m_pName : "(no name)";
	size_t x = 0;
	size_t y = 0;
	size_t w = 0;
	size_t h = 0;
	const bool RectValid = pSprite != nullptr && pSprite->m_pSet != nullptr &&
			       ResolveSpritePixelRect(FromImageInfo.m_Width, FromImageInfo.m_Height,
				       pSprite->m_pSet->m_Gridx, pSprite->m_pSet->m_Gridy,
				       pSprite->m_X, pSprite->m_Y, pSprite->m_W, pSprite->m_H,
				       x, y, w, h);
	if(FromImageInfo.m_pData == nullptr || !RectValid)
	{
		log_error("graphics/texture", "Ignoring invalid sprite texture '%s'.", pSpriteName);
		return false;
	}

	CImageInfo SpriteInfo;
	SpriteInfo.m_Width = w;
	SpriteInfo.m_Height = h;
	SpriteInfo.m_Format = FromImageInfo.m_Format;
	size_t SpriteDataSize = 0;
	if(!SpriteInfo.DataSize(SpriteDataSize))
	{
		log_error("graphics/texture", "Ignoring sprite texture '%s' with invalid data size.", pSpriteName);
		return false;
	}
	SpriteInfo.m_pData = static_cast<uint8_t *>(malloc(SpriteDataSize));
	if(SpriteInfo.m_pData == nullptr)
	{
		log_error("graphics/texture", "Failed to allocate sprite texture '%s'.", pSpriteName);
		SpriteInfo.Free();
		return false;
	}
	SpriteInfo.CopyRectFrom(FromImageInfo, x, y, w, h, 0, 0);
	Result = std::move(SpriteInfo);
	return true;
}

bool IsImageRectFullyTransparent(const CImageInfo &Image, size_t X, size_t Y, size_t Width, size_t Height)
{
	if(Image.m_Format != CImageInfo::FORMAT_R && Image.m_Format != CImageInfo::FORMAT_RA && Image.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(Image.m_pData == nullptr || Width == 0 || Height == 0)
		return false;
	if(X > Image.m_Width || Y > Image.m_Height || Width > Image.m_Width - X || Height > Image.m_Height - Y)
		return false;

	size_t ImageDataSize = 0;
	if(!Image.DataSize(ImageDataSize))
		return false;

	// 与引擎原有判定保持一致：PixelSize - 1 处为 alpha（FORMAT_R 时即唯一通道），
	// 该字节为 0 视为像素完全透明。
	const size_t PixelSize = Image.PixelSize();
	for(size_t iy = 0; iy < Height; ++iy)
	{
		for(size_t ix = 0; ix < Width; ++ix)
		{
			const size_t Offset = ((Y + iy) * Image.m_Width + (X + ix)) * PixelSize;
			if(Offset >= ImageDataSize || PixelSize - 1 >= ImageDataSize - Offset)
				return false;
			if(Image.m_pData[Offset + (PixelSize - 1)] > 0)
				return false;
		}
	}
	return true;
}

void ClearImageToTransparent(CImageInfo &Image)
{
	size_t DataSize = 0;
	if(Image.m_pData == nullptr || !Image.DataSize(DataSize))
		return;
	mem_zero(Image.m_pData, DataSize);
}

bool CopyFallbackOverBlankRect(CImageInfo &Image, const CImageInfo &FallbackImage,
	size_t X, size_t Y, size_t Width, size_t Height,
	size_t FallbackX, size_t FallbackY, size_t FallbackWidth, size_t FallbackHeight)
{
	if(Image.m_Format != FallbackImage.m_Format || Image.m_pData == nullptr || FallbackImage.m_pData == nullptr)
		return false;
	if(!IsImageRectFullyTransparent(Image, X, Y, Width, Height))
		return false;
	if(FallbackWidth == 0 || FallbackHeight == 0 ||
		FallbackX > FallbackImage.m_Width || FallbackY > FallbackImage.m_Height ||
		FallbackWidth > FallbackImage.m_Width - FallbackX || FallbackHeight > FallbackImage.m_Height - FallbackY)
	{
		return false;
	}

	const size_t PixelSize = Image.PixelSize();
	if(PixelSize == 0 || PixelSize != FallbackImage.PixelSize())
		return false;

	uint8_t *pDstData = static_cast<uint8_t *>(Image.m_pData);
	const uint8_t *pSrcData = static_cast<const uint8_t *>(FallbackImage.m_pData);
	for(size_t Row = 0; Row < Height; ++Row)
	{
		// 分辨率不同的画布上，同一格位按比例取最近邻样本。
		const size_t SampleY = FallbackY + static_cast<size_t>((static_cast<int64_t>(Row) * static_cast<int64_t>(FallbackHeight)) / static_cast<int64_t>(Height));
		for(size_t Column = 0; Column < Width; ++Column)
		{
			const size_t SampleX = FallbackX + static_cast<size_t>((static_cast<int64_t>(Column) * static_cast<int64_t>(FallbackWidth)) / static_cast<int64_t>(Width));
			const size_t DstOffset = ((Y + Row) * Image.m_Width + (X + Column)) * PixelSize;
			const size_t SrcOffset = (SampleY * FallbackImage.m_Width + SampleX) * PixelSize;
			mem_copy(&pDstData[DstOffset], &pSrcData[SrcOffset], PixelSize);
		}
	}
	return true;
}
