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

	// 左侧两个圆角；右侧尾部已经渐隐到 0，不需要额外遮罩。
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

float QmMapCover::DissolveAlpha(const SDissolve &Style, int X, int Y, int Width, int Height)
{
	if(Width <= 0 || Height <= 0)
		return 0.0f;
	const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width);
	const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
	const uint32_t Seed = Style.m_Seed;
	const float Ragged = std::max(0.0f, Style.m_Ragged);
	const float Start = std::clamp(Style.m_Start, 0.0f, 0.99f);
	const float End = std::clamp(Style.m_End, Start + 0.01f, 1.5f);
	const float SecondStart = Start + 0.08f;
	const float SecondEnd = std::max(SecondStart + 0.02f, End - 0.05f);
	const float MainBase = (U - Start) / (End - Start);
	const float SecondBase = (U - SecondStart) / (SecondEnd - SecondStart);

	// 噪声对渐隐边界的最大错切约 ±0.25（归一化 t）：完全在边界之外时直接返回，
	// 大段不透明/透明的像素不必参与 5 次 value noise 采样。
	const float WarpMargin = 0.26f * Ragged;
	float Alpha;
	if(MainBase < -WarpMargin && SecondBase < -WarpMargin)
		Alpha = 1.0f;
	else if(MainBase > 1.0f + WarpMargin && SecondBase > 1.0f + WarpMargin)
		Alpha = 0.0f;
	else
	{
		// 主层：三档频率的噪声错切同一渐变边界，形成参差的尾部。
		const float WarpMain =
			(ValueNoise(U * 5.0f, V * 2.0f, Seed) - 0.5f) * 0.30f +
			(ValueNoise(U * 13.0f, V * 4.5f, Seed + 1) - 0.5f) * 0.13f +
			(ValueNoise(U * 37.0f, V * 11.0f, Seed + 2) - 0.5f) * 0.05f;
		const float AlphaMain = 1.0f - SmoothStep(MainBase + WarpMain * Ragged);

		// 第二层起点更靠后、峰值只有 70%：叠出「层」的层次，而不是一条均匀渐变带。
		const float WarpSecond =
			(ValueNoise(U * 9.0f + 7.3f, V * 3.0f + 11.1f, Seed + 3) - 0.5f) * 0.26f +
			(ValueNoise(U * 23.0f, V * 7.0f, Seed + 4) - 0.5f) * 0.10f;
		const float AlphaSecond = (1.0f - SmoothStep(SecondBase + WarpSecond * Ragged)) * 0.70f;

		Alpha = std::max(AlphaMain, AlphaSecond);
	}

	// 左侧圆角：只处理两个左角，尾部为 0 的位置不需要。
	const float Radius = std::clamp(Style.m_CornerRadius, 0.0f, std::min(Width, Height) * 0.5f);
	if(Radius > 0.0f)
	{
		const float PixelX = static_cast<float>(X) + 0.5f;
		const float PixelY = static_cast<float>(Y) + 0.5f;
		const float TopCoverage = PixelY < Radius ? CornerCoverage(PixelX, PixelY, Radius) : 1.0f;
		const float BottomCoverage = PixelY > static_cast<float>(Height) - Radius ? CornerCoverage(PixelX, static_cast<float>(Height) - PixelY, Radius) : 1.0f;
		Alpha *= std::min(TopCoverage, BottomCoverage);
	}

	return std::clamp(Alpha, 0.0f, 1.0f);
}

uint32_t QmMapCover::SeedForUrl(const char *pUrl)
{
	// FNV-1a：同一 URL 在行条目与详情面板得到同一个尾部走向。
	uint32_t Hash = 2166136261u;
	if(pUrl != nullptr)
	{
		for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pUrl); *p != '\0'; ++p)
			Hash = (Hash ^ *p) * 16777619u;
	}
	return Hash;
}

bool QmMapCover::Compose(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out)
{
	if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0)
		return false;
	if(Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(OutWidth <= 0 || OutHeight <= 0 || OutWidth > MAX_WIDTH || OutHeight > MAX_HEIGHT)
		return false;

	const int ChannelCount = Source.m_Format == CImageInfo::FORMAT_RGBA ? 4 : 3;
	const float Scale = std::max(static_cast<float>(OutWidth) / static_cast<float>(Source.m_Width),
		static_cast<float>(OutHeight) / static_cast<float>(Source.m_Height));
	const float SourcePerPixel = 1.0f / Scale;
	// 居中裁切：缩放后超出目标矩形的部分平均分到两侧。
	const float OffsetX = (static_cast<float>(Source.m_Width) - static_cast<float>(OutWidth) * SourcePerPixel) * 0.5f;
	const float OffsetY = (static_cast<float>(Source.m_Height) - static_cast<float>(OutHeight) * SourcePerPixel) * 0.5f;

	const size_t OutputSize = static_cast<size_t>(OutWidth) * static_cast<size_t>(OutHeight) * 4;
	auto *pOutput = static_cast<uint8_t *>(malloc(OutputSize));
	if(pOutput == nullptr)
		return false;

	const float Shade = std::clamp(Art.m_Shade, 0.0f, 1.0f);
	const float MaxSourceX = static_cast<float>(Source.m_Width) - 1.0f;
	const float MaxSourceY = static_cast<float>(Source.m_Height) - 1.0f;

	for(int Y = 0; Y < OutHeight; ++Y)
	{
		const float SourceY = std::clamp(OffsetY + (static_cast<float>(Y) + 0.5f) * SourcePerPixel - 0.5f, 0.0f, MaxSourceY);
		const int Y0 = static_cast<int>(SourceY);
		const int Y1 = std::min(Y0 + 1, static_cast<int>(Source.m_Height) - 1);
		const float FractionY = SourceY - static_cast<float>(Y0);
		uint8_t *pRow = pOutput + static_cast<size_t>(Y) * static_cast<size_t>(OutWidth) * 4;
		for(int X = 0; X < OutWidth; ++X)
		{
			const float SourceX = std::clamp(OffsetX + (static_cast<float>(X) + 0.5f) * SourcePerPixel - 0.5f, 0.0f, MaxSourceX);
			const int X0 = static_cast<int>(SourceX);
			const int X1 = std::min(X0 + 1, static_cast<int>(Source.m_Width) - 1);
			const float FractionX = SourceX - static_cast<float>(X0);
			uint8_t *pPixel = pRow + static_cast<size_t>(X) * 4;
			for(int Channel = 0; Channel < 3; ++Channel)
			{
				const float TopLeft = SampleChannel(Source, ChannelCount, X0, Y0, Channel);
				const float TopRight = SampleChannel(Source, ChannelCount, X1, Y0, Channel);
				const float BottomLeft = SampleChannel(Source, ChannelCount, X0, Y1, Channel);
				const float BottomRight = SampleChannel(Source, ChannelCount, X1, Y1, Channel);
				const float Top = TopLeft + (TopRight - TopLeft) * FractionX;
				const float Bottom = BottomLeft + (BottomRight - BottomLeft) * FractionX;
				const float Value = (Top + (Bottom - Top) * FractionY) * Shade;
				pPixel[Channel] = static_cast<uint8_t>(std::clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
			const float Alpha = DissolveAlpha(Art.m_Dissolve, X, Y, OutWidth, OutHeight);
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
