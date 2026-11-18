// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "map_cover_art.h"

#include <base/mem.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
	uint32_t HashMix(uint32_t Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	float LatticeValue(int X, int Y, uint32_t Seed)
	{
		const uint32_t Hash = HashMix(static_cast<uint32_t>(X) * 0x9e3779b1u ^ static_cast<uint32_t>(Y) * 0x85ebca6bu ^ Seed);
		// 只取低 24 位，浮点除法精度足够且没有符号问题。
		return static_cast<float>(Hash & 0x00ffffffu) / static_cast<float>(0x01000000u);
	}

	float SmoothStep(float Value)
	{
		if(Value <= 0.0f)
			return 0.0f;
		if(Value >= 1.0f)
			return 1.0f;
		return Value * Value * (3.0f - 2.0f * Value);
	}

	// 左侧两个圆角；右侧拖尾渐隐到 0，不需要额外遮罩。
	float CornerCoverage(float LocalX, float LocalY, float Radius)
	{
		const float DeltaX = LocalX - Radius;
		const float DeltaY = LocalY - Radius;
		const float Distance = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
		// 1 像素羽化，避免硬边阶梯；覆盖度按「在圆内多少」给出。
		return std::clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
	}

	std::unique_ptr<uint8_t, decltype(&std::free)> AllocatePixels(size_t Size, const QmMapCover::SOutputAllocation *pAllocation)
	{
		const auto Allocate = pAllocation != nullptr ? pAllocation->m_pfnAllocate : nullptr;
		void *pData = Allocate != nullptr ? Allocate(Size, pAllocation->m_pUser) : std::malloc(Size);
		return {static_cast<uint8_t *>(pData), &std::free};
	}

	// 在缓存工作线程内把原图和径向模糊图混合，文字与 HUD 不参与采样。
	bool ApplySummaryBlur(uint8_t *pPixels, int Width, int Height, const QmMapCover::SOutputAllocation *pAllocation)
	{
		const size_t Size = static_cast<size_t>(Width) * Height * 4;
		const auto pSharp = AllocatePixels(Size, pAllocation);
		if(!pSharp)
			return false;
		mem_copy(pSharp.get(), pPixels, Size);
		const float CenterY = (Height - 1) * 0.5f;
		constexpr int Samples = 24;
		for(int Y = 0; Y < Height; ++Y)
			for(int X = 0; X < Width; ++X)
			{
				const float Mask = 0.10f + 0.70f * X / (Width - 1);
				int aSum[3]{};
				for(int i = 0; i < Samples; ++i)
				{
					const float T = 0.065f * i / (Samples - 1);
					const int SampleX = static_cast<int>(X * (1.0f - T) + 0.5f);
					const int SampleY = static_cast<int>(Y + (CenterY - Y) * T + 0.5f);
					const uint8_t *pSample = pSharp.get() + (static_cast<size_t>(SampleY) * Width + SampleX) * 4;
					for(int c = 0; c < 3; ++c)
						aSum[c] += pSample[c];
				}
				uint8_t *pPixel = pPixels + (static_cast<size_t>(Y) * Width + X) * 4;
				for(int c = 0; c < 3; ++c)
					pPixel[c] = static_cast<uint8_t>(pPixel[c] + (aSum[c] / static_cast<float>(Samples) - pPixel[c]) * Mask + 0.5f);
			}
		return true;
	}

	float SampleChannel(const CImageInfo &Image, int ChannelCount, int X, int Y, int Channel, bool FlattenAlpha)
	{
		const uint8_t *pPixel = Image.m_pData + (static_cast<size_t>(Y) * Image.m_Width + X) * ChannelCount;
		const float Value = static_cast<float>(pPixel[Channel]) / 255.0f;
		return FlattenAlpha ? Value * (static_cast<float>(pPixel[ChannelCount - 1]) / 255.0f) : Value;
	}

	float BilinearChannel(const CImageInfo &Image, int ChannelCount, float X, float Y, int Channel, bool FlattenAlpha)
	{
		const int X0 = std::clamp(static_cast<int>(X), 0, static_cast<int>(Image.m_Width) - 1);
		const int Y0 = std::clamp(static_cast<int>(Y), 0, static_cast<int>(Image.m_Height) - 1);
		const int X1 = std::min(X0 + 1, static_cast<int>(Image.m_Width) - 1);
		const int Y1 = std::min(Y0 + 1, static_cast<int>(Image.m_Height) - 1);
		const float FractionX = std::clamp(X - static_cast<float>(X0), 0.0f, 1.0f);
		const float FractionY = std::clamp(Y - static_cast<float>(Y0), 0.0f, 1.0f);
		const float TopLeft = SampleChannel(Image, ChannelCount, X0, Y0, Channel, FlattenAlpha);
		const float TopRight = SampleChannel(Image, ChannelCount, X1, Y0, Channel, FlattenAlpha);
		const float BottomLeft = SampleChannel(Image, ChannelCount, X0, Y1, Channel, FlattenAlpha);
		const float BottomRight = SampleChannel(Image, ChannelCount, X1, Y1, Channel, FlattenAlpha);
		const float Top = TopLeft + (TopRight - TopLeft) * FractionX;
		const float Bottom = BottomLeft + (BottomRight - BottomLeft) * FractionX;
		return Top + (Bottom - Top) * FractionY;
	}
} // namespace

bool QmMapCover::ValidThumbnailUrl(const char *pUrl)
{
	if(pUrl == nullptr)
		return false;
	const size_t PrefixLength = str_length(THUMBNAIL_URL_PREFIX);
	if(str_startswith(pUrl, THUMBNAIL_URL_PREFIX) == nullptr)
		return false;
	const char *pLeaf = pUrl + PrefixLength;
	const size_t LeafLength = str_length(pLeaf);
	// 至少 <名>.png，最长也留出余量；完整 URL 太长直接视为异常。
	if(LeafLength < 5 || LeafLength > 160 || PrefixLength + LeafLength > 256)
		return false;
	if(str_endswith(pLeaf, ".png") == nullptr)
		return false;
	for(size_t i = 0; i + 4 < LeafLength; ++i)
	{
		const unsigned char Character = static_cast<unsigned char>(pLeaf[i]);
		const bool Allowed =
			(Character >= 'a' && Character <= 'z') ||
			(Character >= 'A' && Character <= 'Z') ||
			(Character >= '0' && Character <= '9') ||
			Character == '_' || Character == '-' || Character == '.';
		if(!Allowed)
			return false;
	}
	return true;
}

bool QmMapCover::ThumbnailCacheFileName(const char *pUrl, char *pOut, size_t OutSize)
{
	if(pOut == nullptr || OutSize == 0)
		return false;
	pOut[0] = '\0';
	if(!ValidThumbnailUrl(pUrl))
		return false;
	const char *pLeaf = pUrl + str_length(THUMBNAIL_URL_PREFIX);
	// str_format 会静默截断，先按完整长度校验，避免把残缺路径当成缓存位置。
	const size_t Required = str_length(CACHE_FOLDER) + 1 + str_length(CACHE_FILE_PREFIX) + str_length(pLeaf) + 1;
	if(OutSize < Required)
		return false;
	str_format(pOut, OutSize, "%s/%s%s", CACHE_FOLDER, CACHE_FILE_PREFIX, pLeaf);
	return true;
}

float QmMapCover::ValueNoise(float X, float Y, uint32_t Seed)
{
	const float FloorX = std::floor(X);
	const float FloorY = std::floor(Y);
	const int X0 = static_cast<int>(FloorX);
	const int Y0 = static_cast<int>(FloorY);
	const float FractionX = X - FloorX;
	const float FractionY = Y - FloorY;
	const float WeightX = FractionX * FractionX * (3.0f - 2.0f * FractionX);
	const float WeightY = FractionY * FractionY * (3.0f - 2.0f * FractionY);
	const float TopLeft = LatticeValue(X0, Y0, Seed);
	const float TopRight = LatticeValue(X0 + 1, Y0, Seed);
	const float BottomLeft = LatticeValue(X0, Y0 + 1, Seed);
	const float BottomRight = LatticeValue(X0 + 1, Y0 + 1, Seed);
	const float Top = TopLeft + (TopRight - TopLeft) * WeightX;
	const float Bottom = BottomLeft + (BottomRight - BottomLeft) * WeightX;
	return Top + (Bottom - Top) * WeightY;
}

uint32_t QmMapCover::SeedForUrl(const char *pUrl)
{
	// FNV-1a：同一 URL 在行条目与详情条得到同一个拖尾走向。
	uint32_t Hash = 2166136261u;
	if(pUrl != nullptr)
	{
		for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pUrl); *p != '\0'; ++p)
			Hash = (Hash ^ *p) * 16777619u;
	}
	return Hash;
}

float QmMapCover::TailAlpha(const SArt &Art, int X, int Y, int Width, int Height, int ImageWidth)
{
	const int TailWidth = Width - ImageWidth;
	if(TailWidth <= 0 || Height <= 0 || X < ImageWidth || X >= Width)
		return 0.0f;
	const float T = (static_cast<float>(X - ImageWidth) + 0.5f) / static_cast<float>(TailWidth);
	const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
	const uint32_t Seed = Art.m_Seed;
	const float Ragged = std::max(0.0f, Art.m_Ragged);
	// 噪声在 T=0 处权重为 0，保证缩略图右边缘与拖尾起点无缝衔接。
	const float Ramp = std::min(1.0f, T * 5.0f);

	// 主层最长，第二层更短、更虚，第三层再短一些：叠加后就是层次不齐的尾巴。
	const float WarpMain = ((ValueNoise(V * 2.5f, T * 3.0f, Seed) - 0.5f) * 0.34f +
				       (ValueNoise(V * 6.0f, T * 9.0f, Seed + 1) - 0.5f) * 0.14f) *
			       Ragged * Ramp;
	const float WarpSecond = ((ValueNoise(V * 4.0f + 5.0f, T * 5.0f + 3.0f, Seed + 2) - 0.5f) * 0.30f +
					 (ValueNoise(V * 11.0f, T * 17.0f, Seed + 3) - 0.5f) * 0.10f) *
				 Ragged * Ramp;
	const float WarpThird = (ValueNoise(V * 7.0f + 11.0f, T * 9.0f + 7.0f, Seed + 4) - 0.5f) * 0.30f * Ragged * Ramp;

	const float Main = 1.0f - SmoothStep(T * 1.05f - 0.02f + WarpMain);
	const float Second = (1.0f - SmoothStep(T / 0.66f + WarpSecond)) * 0.62f;
	const float Third = (1.0f - SmoothStep(T / 0.42f + WarpThird)) * 0.30f;
	// 尾端独立收束到透明，避免噪声把残余覆盖率带到纹理边界。
	const float EdgeFade = SmoothStep(static_cast<float>(Width - 1 - X) / std::max(1.0f, static_cast<float>(TailWidth) * 0.2f));
	return std::clamp(std::max({Main, Second, Third}), 0.0f, 1.0f) * EdgeFade;
}

QmMapCover::EPrepareResult QmMapCover::ComposeChecked(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out, const SOutputAllocation *pAllocation)
{
	if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0)
		return EPrepareResult::INVALID;
	const bool Grayscale = Source.m_Format == CImageInfo::FORMAT_R || Source.m_Format == CImageInfo::FORMAT_RA;
	if(!Grayscale && Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA)
		return EPrepareResult::INVALID;
	if(OutWidth <= 0 || OutHeight <= 0 || OutWidth > MAX_WIDTH || OutHeight > MAX_HEIGHT || OutWidth < 2)
		return EPrepareResult::INVALID;

	const bool FullImage = Art.m_ImageFraction >= 1.0f;
	const int ImageWidth = FullImage ? OutWidth : std::clamp(static_cast<int>(std::lround(static_cast<float>(OutWidth) * std::clamp(Art.m_ImageFraction, 0.3f, 0.95f))), 1, OutWidth - 1);
	const int TailWidth = OutWidth - ImageWidth;
	const int ChannelCount = static_cast<int>(CImageInfo::PixelSize(Source.m_Format));
	const int ColorChannelCount = Grayscale ? 1 : 3;
	// 完整封面在插值前将原始像素合成到黑底，避免透明像素的颜色渗入可见区域。
	const bool FlattenAlpha = FullImage && (Source.m_Format == CImageInfo::FORMAT_RA || Source.m_Format == CImageInfo::FORMAT_RGBA);
	const float Shade = std::clamp(Art.m_Shade, 0.0f, 1.0f);

	// 缩略图区域按目标高度等比缩放并居中裁切。
	const float Scale = std::max(static_cast<float>(ImageWidth) / static_cast<float>(Source.m_Width),
		static_cast<float>(OutHeight) / static_cast<float>(Source.m_Height));
	const float SourcePerPixel = 1.0f / Scale;
	const float OffsetX = (static_cast<float>(Source.m_Width) - static_cast<float>(ImageWidth) * SourcePerPixel) * 0.5f;
	const float OffsetY = (static_cast<float>(Source.m_Height) - static_cast<float>(OutHeight) * SourcePerPixel) * 0.5f;
	const float MaxSourceX = static_cast<float>(Source.m_Width) - 1.0f;
	const float MaxSourceY = static_cast<float>(Source.m_Height) - 1.0f;
	const auto SourceXAt = [&](float PixelX) {
		return std::clamp(OffsetX + PixelX * SourcePerPixel - 0.5f, 0.0f, MaxSourceX);
	};
	const float ThumbnailLeftX = SourceXAt(0.5f);
	const float ThumbnailRightX = SourceXAt(static_cast<float>(ImageWidth) - 0.5f);
	const float Radius = std::clamp(Art.m_CornerRadius, 0.0f, static_cast<float>(std::min(ImageWidth, OutHeight)) * 0.5f);

	const size_t OutputSize = static_cast<size_t>(OutWidth) * static_cast<size_t>(OutHeight) * 4;
	auto pOutput = AllocatePixels(OutputSize, pAllocation);
	if(!pOutput)
		return EPrepareResult::RESOURCE_ERROR;

	for(int Y = 0; Y < OutHeight; ++Y)
	{
		const float SourceY = std::clamp(OffsetY + (static_cast<float>(Y) + 0.5f) * SourcePerPixel - 0.5f, 0.0f, MaxSourceY);
		const float CornerY = static_cast<float>(std::min(Y, OutHeight - 1 - Y)) + 0.5f;
		uint8_t *pRow = pOutput.get() + static_cast<size_t>(Y) * static_cast<size_t>(OutWidth) * 4;
		for(int X = 0; X < OutWidth; ++X)
		{
			uint8_t *pPixel = pRow + static_cast<size_t>(X) * 4;
			float Alpha = 1.0f;
			float SourceX;
			if(X < ImageWidth)
			{
				const float PixelX = static_cast<float>(X) + 0.5f;
				SourceX = SourceXAt(PixelX);
				if(PixelX < Radius && CornerY < Radius)
					Alpha = CornerCoverage(PixelX, CornerY, Radius);
			}
			else
			{
				// 首列与裁切后的缩略图共用同一采样位置，之后在裁切范围内往左拖出残影。
				const float T = static_cast<float>(X - ImageWidth) / static_cast<float>(TailWidth);
				SourceX = ThumbnailRightX - (ThumbnailRightX - ThumbnailLeftX) * (0.10f * T * T);
				Alpha = TailAlpha(Art, X, Y, OutWidth, OutHeight, ImageWidth);
			}
			for(int Channel = 0; Channel < ColorChannelCount; ++Channel)
			{
				const float Value = BilinearChannel(Source, ChannelCount, SourceX, SourceY, Channel, FlattenAlpha) * Shade;
				pPixel[Channel] = static_cast<uint8_t>(std::clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
			if(Grayscale)
				pPixel[1] = pPixel[2] = pPixel[0];
			pPixel[3] = static_cast<uint8_t>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
	}

	if(Art.m_SummaryBlur && !ApplySummaryBlur(pOutput.get(), OutWidth, OutHeight, pAllocation))
		return EPrepareResult::RESOURCE_ERROR;

	Out.Free();
	Out.m_Width = static_cast<size_t>(OutWidth);
	Out.m_Height = static_cast<size_t>(OutHeight);
	Out.m_Format = CImageInfo::FORMAT_RGBA;
	Out.m_pData = pOutput.release();
	return EPrepareResult::SUCCESS;
}

bool QmMapCover::Compose(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out)
{
	return ComposeChecked(Source, OutWidth, OutHeight, Art, Out) == EPrepareResult::SUCCESS;
}

QmMapCover::EPrepareResult QmMapCover::DecodeAndComposeChecked(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out,
	CPngReadAllocator *pPngAllocator, const SOutputAllocation *pOutputAllocation)
{
	if(pPngData == nullptr || DataSize < 8)
		return EPrepareResult::INVALID;
	static constexpr uint8_t s_aPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if(mem_comp(pPngData, s_aPngSignature, sizeof(s_aPngSignature)) != 0)
		return EPrepareResult::INVALID;

	CImageInfo Source;
	struct SFreeSource
	{
		CImageInfo &m_Image;
		~SFreeSource() { m_Image.Free(); }
	} FreeSource{Source};
	const auto Decoded = CImageLoader::LoadPngChecked(pPngData, DataSize, pContextName != nullptr ? pContextName : "map_cover", Source, MAX_DECODED_BYTES, pPngAllocator);
	if(Decoded == CImageLoader::EPngLoadResult::RESOURCE_ERROR)
		return EPrepareResult::RESOURCE_ERROR;
	if(Decoded != CImageLoader::EPngLoadResult::SUCCESS)
		return EPrepareResult::INVALID;
	return ComposeChecked(Source, OutWidth, OutHeight, Art, Out, pOutputAllocation);
}

bool QmMapCover::DecodeAndCompose(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out)
{
	return DecodeAndComposeChecked(pPngData, DataSize, pContextName, OutWidth, OutHeight, Art, Out) == EPrepareResult::SUCCESS;
}
