#ifndef ENGINE_CLIENT_BACKEND_METAL_METAL_TYPES_H
#define ENGINE_CLIENT_BACKEND_METAL_METAL_TYPES_H

#if defined(__METAL_VERSION__)
using SMetalFloat4 = metal::float4;
using SMetalFloat4x4 = metal::float4x4;
#else
#include <cstddef>
#include <cstdint>
#include <limits>

enum class EMetalTextureFormat
{
	RGBA8,
	R8,
};

enum class EMetalBlendMode
{
	NONE,
	ALPHA,
	ADDITIVE,
};

enum class EMetalBlendFactor
{
	ZERO,
	ONE,
	SOURCE_ALPHA,
	ONE_MINUS_SOURCE_ALPHA,
};

struct SMetalBlendState
{
	bool m_Enabled = false;
	EMetalBlendFactor m_Source = EMetalBlendFactor::ONE;
	EMetalBlendFactor m_Destination = EMetalBlendFactor::ZERO;
};

inline SMetalBlendState MetalBlendState(EMetalBlendMode Mode)
{
	switch(Mode)
	{
	case EMetalBlendMode::NONE: return {};
	case EMetalBlendMode::ALPHA: return {true, EMetalBlendFactor::SOURCE_ALPHA, EMetalBlendFactor::ONE_MINUS_SOURCE_ALPHA};
	case EMetalBlendMode::ADDITIVE: return {true, EMetalBlendFactor::SOURCE_ALPHA, EMetalBlendFactor::ONE};
	}
	return {};
}

struct SMetalTextureLayout
{
	size_t m_BytesPerPixel = 0;
	size_t m_RowBytes = 0;
	size_t m_DataBytes = 0;
	size_t m_MipLevels = 0;
};

static constexpr size_t METAL_TEXTURE_ARRAY_LAYERS = 16 * 16;

inline size_t MetalTextureBytesPerPixel(EMetalTextureFormat Format)
{
	return Format == EMetalTextureFormat::R8 ? 1 : 4;
}

inline bool MetalCheckedMul(size_t A, size_t B, size_t &Result)
{
	if(A != 0 && B > std::numeric_limits<size_t>::max() / A)
	{
		Result = 0;
		return false;
	}
	Result = A * B;
	return true;
}

inline uint32_t MetalSelectSampleCount(uint32_t RequestedCount, bool Supports2, bool Supports4, bool Supports8)
{
	if(RequestedCount >= 8 && Supports8)
		return 8;
	if(RequestedCount >= 4 && Supports4)
		return 4;
	if(RequestedCount >= 2 && Supports2)
		return 2;
	return 0;
}

enum class EMetalPrimitiveType
{
	LINES,
	QUADS,
	TRIANGLES,
};

inline bool MetalPrimitiveVertexCount(EMetalPrimitiveType Type, size_t PrimitiveCount, size_t &VertexCount)
{
	size_t VerticesPerPrimitive = 0;
	switch(Type)
	{
	case EMetalPrimitiveType::LINES: VerticesPerPrimitive = 2; break;
	case EMetalPrimitiveType::QUADS: VerticesPerPrimitive = 4; break;
	case EMetalPrimitiveType::TRIANGLES: VerticesPerPrimitive = 3; break;
	}
	return MetalCheckedMul(PrimitiveCount, VerticesPerPrimitive, VertexCount);
}

inline size_t MetalMipLevelCount(size_t Width, size_t Height, bool NoMipmaps)
{
	if(Width == 0 || Height == 0)
		return 0;
	if(NoMipmaps)
		return 1;

	size_t Levels = 1;
	for(size_t Size = Width > Height ? Width : Height; Size > 1; Size >>= 1)
		++Levels;
	return Levels;
}

inline bool MetalTextureLayout(size_t Width, size_t Height, EMetalTextureFormat Format, bool NoMipmaps, SMetalTextureLayout &Layout)
{
	Layout = {};
	if(Width == 0 || Height == 0)
		return false;

	Layout.m_BytesPerPixel = MetalTextureBytesPerPixel(Format);
	if(!MetalCheckedMul(Width, Layout.m_BytesPerPixel, Layout.m_RowBytes))
		return false;

	Layout.m_MipLevels = MetalMipLevelCount(Width, Height, NoMipmaps);
	if(Layout.m_MipLevels == 0)
		return false;

	for(size_t Mip = 0, MipWidth = Width, MipHeight = Height; Mip < Layout.m_MipLevels; ++Mip)
	{
		size_t RowBytes = 0;
		size_t LevelBytes = 0;
		if(!MetalCheckedMul(MipWidth, Layout.m_BytesPerPixel, RowBytes) || !MetalCheckedMul(RowBytes, MipHeight, LevelBytes) ||
			Layout.m_DataBytes > std::numeric_limits<size_t>::max() - LevelBytes)
		{
			Layout = {};
			return false;
		}
		Layout.m_DataBytes += LevelBytes;
		MipWidth = MipWidth > 1 ? MipWidth / 2 : 1;
		MipHeight = MipHeight > 1 ? MipHeight / 2 : 1;
	}
	return true;
}

inline bool MetalTextureArrayLayout(size_t SourceWidth, size_t SourceHeight, EMetalTextureFormat Format, bool NoMipmaps, size_t &LayerWidth, size_t &LayerHeight, SMetalTextureLayout &Layout)
{
	LayerWidth = 0;
	LayerHeight = 0;
	Layout = {};
	if(SourceWidth == 0 || SourceHeight == 0 || SourceWidth % 16 != 0 || SourceHeight % 16 != 0)
		return false;
	LayerWidth = SourceWidth / 16;
	LayerHeight = SourceHeight / 16;
	if(!MetalTextureLayout(LayerWidth, LayerHeight, Format, NoMipmaps, Layout) || Layout.m_DataBytes > std::numeric_limits<size_t>::max() / METAL_TEXTURE_ARRAY_LAYERS)
	{
		LayerWidth = 0;
		LayerHeight = 0;
		Layout = {};
		return false;
	}
	Layout.m_DataBytes *= METAL_TEXTURE_ARRAY_LAYERS;
	return true;
}

inline bool MetalValidateSubregion(size_t TextureWidth, size_t TextureHeight, size_t X, size_t Y, size_t Width, size_t Height)
{
	return Width > 0 && Height > 0 && X <= TextureWidth && Y <= TextureHeight && Width <= TextureWidth - X && Height <= TextureHeight - Y;
}

struct SMetalBufferLayout
{
	size_t m_DataBytes = 0;
	bool m_OneTimeUse = false;
};

inline bool MetalBufferLayout(size_t DataBytes, bool OneTimeUse, SMetalBufferLayout &Layout)
{
	Layout = {};
	if(DataBytes == 0)
		return false;
	Layout.m_DataBytes = DataBytes;
	Layout.m_OneTimeUse = OneTimeUse;
	return true;
}

inline bool MetalValidateBufferRange(size_t BufferBytes, size_t Offset, size_t CopyBytes)
{
	return CopyBytes > 0 && Offset <= BufferBytes && CopyBytes <= BufferBytes - Offset;
}

enum : uint32_t
{
	METAL_GRAPHICS_TYPE_UNSIGNED_BYTE = 0x1401,
	METAL_GRAPHICS_TYPE_UNSIGNED_SHORT = 0x1403,
	METAL_GRAPHICS_TYPE_INT = 0x1404,
	METAL_GRAPHICS_TYPE_UNSIGNED_INT = 0x1405,
	METAL_GRAPHICS_TYPE_FLOAT = 0x1406,
};

struct SMetalVertexAttribute
{
	uint32_t m_DataTypeCount = 0;
	uint32_t m_Type = 0;
	bool m_Normalized = false;
	size_t m_Offset = 0;
	uint32_t m_FuncType = 0;
};

inline size_t MetalVertexDataTypeBytes(uint32_t Type)
{
	switch(Type)
	{
	case METAL_GRAPHICS_TYPE_UNSIGNED_BYTE: return 1;
	case METAL_GRAPHICS_TYPE_UNSIGNED_SHORT: return 2;
	case METAL_GRAPHICS_TYPE_INT:
	case METAL_GRAPHICS_TYPE_UNSIGNED_INT:
	case METAL_GRAPHICS_TYPE_FLOAT: return 4;
	}
	return 0;
}

inline bool MetalValidateVertexAttribute(int Stride, const SMetalVertexAttribute &Attribute)
{
	if(Stride <= 0 || Attribute.m_DataTypeCount == 0 || Attribute.m_FuncType > 1)
		return false;
	const size_t TypeBytes = MetalVertexDataTypeBytes(Attribute.m_Type);
	if(TypeBytes == 0 || Attribute.m_Offset > static_cast<size_t>(Stride))
		return false;
	return Attribute.m_DataTypeCount <= (static_cast<size_t>(Stride) - Attribute.m_Offset) / TypeBytes;
}

inline bool MetalVertexAttributeEquals(const SMetalVertexAttribute &Attribute, uint32_t DataTypeCount, uint32_t Type, bool Normalized, size_t Offset, uint32_t FuncType)
{
	return Attribute.m_DataTypeCount == DataTypeCount && Attribute.m_Type == Type && Attribute.m_Normalized == Normalized && Attribute.m_Offset == Offset && Attribute.m_FuncType == FuncType;
}

struct SMetalFloat4
{
	float m_v[4];
};

struct alignas(16) SMetalFloat4x4
{
	float m_v[16];
};
#endif

#if defined(__METAL_VERSION__)
struct SMetalUniforms
#else
struct alignas(16) SMetalUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_Color;
};

#if defined(__METAL_VERSION__)
struct SMetalTextUniforms
#else
struct alignas(16) SMetalTextUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_Color;
	SMetalFloat4 m_OutlineColor;
	SMetalFloat4 m_Params;
};

#if defined(__METAL_VERSION__)
enum : uint
{
	METAL_MAX_QUADS = 256,
	METAL_MAX_SPRITES = 256,
};
#else
static constexpr size_t METAL_MAX_QUADS = 256;
static constexpr size_t METAL_MAX_SPRITES = 256;
#endif

#if defined(__METAL_VERSION__)
struct SMetalTileUniforms
#else
struct alignas(16) SMetalTileUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_Color;
	SMetalFloat4 m_Transform;
};

#if defined(__METAL_VERSION__)
struct SMetalQuadUniforms
#else
struct alignas(16) SMetalQuadUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_aColors[METAL_MAX_QUADS];
	SMetalFloat4 m_aOffsetsRotations[METAL_MAX_QUADS];
	int m_QuadOffset;
	int m_Padding[3];
};

#if defined(__METAL_VERSION__)
struct SMetalQuadContainerUniforms
#else
struct alignas(16) SMetalQuadContainerUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_CenterRotation;
	SMetalFloat4 m_VertexColor;
};

#if defined(__METAL_VERSION__)
struct SMetalSpriteMultipleUniforms
#else
struct alignas(16) SMetalSpriteMultipleUniforms
#endif
{
	SMetalFloat4x4 m_MVP;
	SMetalFloat4 m_Center;
	SMetalFloat4 m_VertexColor;
	SMetalFloat4 m_aRenderInfo[METAL_MAX_SPRITES];
};

#if defined(__METAL_VERSION__)
struct SMetalGaussianBlurUniforms
#else
struct alignas(16) SMetalGaussianBlurUniforms
#endif
{
	SMetalFloat4 m_TexelOffsetRadius;
	SMetalFloat4 m_Weights0;
	SMetalFloat4 m_Weights1;
	SMetalFloat4 m_Weights2;
};

#if !defined(__METAL_VERSION__)
static_assert(alignof(SMetalUniforms) == 16);
static_assert(sizeof(SMetalFloat4x4) == 64);
static_assert(sizeof(SMetalFloat4) == 16);
static_assert(sizeof(SMetalUniforms) == 80);
static_assert(offsetof(SMetalUniforms, m_MVP) == 0);
static_assert(offsetof(SMetalUniforms, m_Color) == 64);
static_assert(alignof(SMetalTextUniforms) == 16);
static_assert(sizeof(SMetalTextUniforms) == 112);
static_assert(offsetof(SMetalTextUniforms, m_OutlineColor) == 80);
static_assert(offsetof(SMetalTextUniforms, m_Params) == 96);
static_assert(sizeof(SMetalTileUniforms) == 96);
static_assert(offsetof(SMetalTileUniforms, m_Transform) == 80);
static_assert(offsetof(SMetalQuadUniforms, m_QuadOffset) == 64 + METAL_MAX_QUADS * 32);
static_assert(sizeof(SMetalQuadContainerUniforms) == 96);
static_assert(sizeof(SMetalSpriteMultipleUniforms) == 64 + 16 + 16 + METAL_MAX_SPRITES * 16);
static_assert(sizeof(SMetalGaussianBlurUniforms) == 64);
static_assert(offsetof(SMetalGaussianBlurUniforms, m_TexelOffsetRadius) == 0);
static_assert(offsetof(SMetalGaussianBlurUniforms, m_Weights0) == 16);
#endif

#endif
