// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "map_cover_art.h"

#include <base/mem.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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
		if(Radius <= 0.0f)
			return 1.0f;
		const float DeltaX = LocalX - Radius;
		const float DeltaY = LocalY - Radius;
		const float Distance = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
		// 1 像素羽化，避免硬边阶梯；覆盖度按「在圆内多少」给出。
		return std::clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
	}

	float SampleChannel(const CImageInfo &Image, int ChannelCount, int X, int Y, int Channel)
	{
		const size_t Index = (static_cast<size_t>(Y) * Image.m_Width + X) * ChannelCount + Channel;
		return static_cast<float>(Image.m_pData[Index]) / 255.0f;
	}

	float BilinearChannel(const CImageInfo &Image, int ChannelCount, float X, float Y, int Channel)
	{
		const int X0 = std::clamp(static_cast<int>(X), 0, static_cast<int>(Image.m_Width) - 1);
		const int Y0 = std::clamp(static_cast<int>(Y), 0, static_cast<int>(Image.m_Height) - 1);
		const int X1 = std::min(X0 + 1, static_cast<int>(Image.m_Width) - 1);
		const int Y1 = std::min(Y0 + 1, static_cast<int>(Image.m_Height) - 1);
		const float FractionX = std::clamp(X - static_cast<float>(X0), 0.0f, 1.0f);
		const float FractionY = std::clamp(Y - static_cast<float>(Y0), 0.0f, 1.0f);
		const float Top = SampleChannel(Image, ChannelCount, X0, Y0, Channel) + (SampleChannel(Image, ChannelCount, X1, Y0, Channel) - SampleChannel(Image, ChannelCount, X0, Y0, Channel)) * FractionX;
		const float Bottom = SampleChannel(Image, ChannelCount, X0, Y1, Channel) + (SampleChannel(Image, ChannelCount, X1, Y1, Channel) - SampleChannel(Image, ChannelCount, X0, Y1, Channel)) * FractionX;
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

bool QmMapCover::Compose(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out)
{
	if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0)
		return false;
	if(Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(OutWidth <= 0 || OutHeight <= 0 || OutWidth > MAX_WIDTH || OutHeight > MAX_HEIGHT || OutWidth < 2)
		return false;

	const int ImageWidth = std::clamp(static_cast<int>(std::lround(static_cast<float>(OutWidth) * std::clamp(Art.m_ImageFraction, 0.3f, 0.95f))), 1, OutWidth - 1);
	const int ChannelCount = Source.m_Format == CImageInfo::FORMAT_RGBA ? 4 : 3;
	const float Shade = std::clamp(Art.m_Shade, 0.0f, 1.0f);

	// 缩略图区域按目标高度等比缩放并居中裁切。
	const float Scale = std::max(static_cast<float>(ImageWidth) / static_cast<float>(Source.m_Width),
		static_cast<float>(OutHeight) / static_cast<float>(Source.m_Height));
	const float SourcePerPixel = 1.0f / Scale;
	const float OffsetX = (static_cast<float>(Source.m_Width) - static_cast<float>(ImageWidth) * SourcePerPixel) * 0.5f;
	const float OffsetY = (static_cast<float>(Source.m_Height) - static_cast<float>(OutHeight) * SourcePerPixel) * 0.5f;
	const float MaxSourceX = static_cast<float>(Source.m_Width) - 1.0f;
	const float MaxSourceY = static_cast<float>(Source.m_Height) - 1.0f;
	const float Radius = std::clamp(Art.m_CornerRadius, 0.0f, static_cast<float>(std::min(ImageWidth, OutHeight)) * 0.5f);

	const size_t OutputSize = static_cast<size_t>(OutWidth) * static_cast<size_t>(OutHeight) * 4;
	auto *pOutput = static_cast<uint8_t *>(malloc(OutputSize));
	if(pOutput == nullptr)
		return false;

	for(int Y = 0; Y < OutHeight; ++Y)
	{
		const float SourceY = std::clamp(OffsetY + (static_cast<float>(Y) + 0.5f) * SourcePerPixel - 0.5f, 0.0f, MaxSourceY);
		uint8_t *pRow = pOutput + static_cast<size_t>(Y) * static_cast<size_t>(OutWidth) * 4;
		for(int X = 0; X < OutWidth; ++X)
		{
			uint8_t *pPixel = pRow + static_cast<size_t>(X) * 4;
			float Alpha = 1.0f;
			float SourceX;
			if(X < ImageWidth)
			{
				SourceX = std::clamp(OffsetX + (static_cast<float>(X) + 0.5f) * SourcePerPixel - 0.5f, 0.0f, MaxSourceX);
				if(Radius > 0.0f)
				{
					const float PixelX = static_cast<float>(X) + 0.5f;
					const float PixelY = static_cast<float>(Y) + 0.5f;
					const float TopCoverage = PixelY < Radius ? CornerCoverage(PixelX, PixelY, Radius) : 1.0f;
					const float BottomCoverage = PixelY > static_cast<float>(OutHeight) - Radius ? CornerCoverage(PixelX, static_cast<float>(OutHeight) - PixelY, Radius) : 1.0f;
					Alpha = std::min(TopCoverage, BottomCoverage);
				}
			}
			else
			{
				// 拖尾采样缩略图右边缘的列：T=0 从最右列开始，越往右缓慢向内扩，
				// 这样拖尾和缩略图边缘严格连续，看起来像画面被拉出的残影。
				const float T = (static_cast<float>(X - ImageWidth) + 0.5f) / static_cast<float>(OutWidth - ImageWidth);
				SourceX = (1.0f - 0.10f * T * T) * MaxSourceX;
				Alpha = TailAlpha(Art, X, Y, OutWidth, OutHeight, ImageWidth);
			}
			for(int Channel = 0; Channel < 3; ++Channel)
			{
				const float Value = BilinearChannel(Source, ChannelCount, SourceX, SourceY, Channel) * Shade;
				pPixel[Channel] = static_cast<uint8_t>(std::clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
			pPixel[3] = static_cast<uint8_t>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
	}

	Out.Free();
	Out.m_Width = static_cast<size_t>(OutWidth);
	Out.m_Height = static_cast<size_t>(OutHeight);
	Out.m_Format = CImageInfo::FORMAT_RGBA;
	Out.m_pData = pOutput;
	return true;
}

bool QmMapCover::DecodeAndCompose(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out)
{
	if(pPngData == nullptr || DataSize < 8)
		return false;
	static constexpr uint8_t s_aPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if(mem_comp(pPngData, s_aPngSignature, sizeof(s_aPngSignature)) != 0)
		return false;

	CImageInfo Source;
	if(!CImageLoader::LoadPng(pPngData, DataSize, pContextName != nullptr ? pContextName : "map_cover", Source, MAX_DECODED_BYTES))
	{
		Source.Free();
		return false;
	}
	const bool Success = Compose(Source, OutWidth, OutHeight, Art, Out);
	Source.Free();
	return Success;
}
