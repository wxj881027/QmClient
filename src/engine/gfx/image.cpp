#include <base/system.h>

#include <engine/image.h>

#include <limits>

CImageInfo::CImageInfo(CImageInfo &&Other) noexcept
{
	*this = std::move(Other);
}

CImageInfo &CImageInfo::operator=(CImageInfo &&Other) noexcept
{
	if(this == &Other)
		return *this;

	Free();
	m_Width = Other.m_Width;
	m_Height = Other.m_Height;
	m_Format = Other.m_Format;
	m_pData = Other.m_pData;
	Other.m_Width = 0;
	Other.m_Height = 0;
	Other.m_Format = FORMAT_UNDEFINED;
	Other.m_pData = nullptr;
	return *this;
}

void CImageInfo::Free() noexcept
{
	m_Width = 0;
	m_Height = 0;
	m_Format = FORMAT_UNDEFINED;
	free(m_pData);
	m_pData = nullptr;
}

size_t CImageInfo::PixelSize(EImageFormat Format)
{
	dbg_assert(Format != FORMAT_UNDEFINED, "Format undefined");
	static const size_t s_aSizes[] = {3, 4, 1, 2};
	return s_aSizes[(int)Format];
}

const char *CImageInfo::FormatName(EImageFormat Format)
{
	static const char *s_apNames[] = {"UNDEFINED", "RGB", "RGBA", "R", "RA"};
	return s_apNames[(int)Format + 1];
}

size_t CImageInfo::PixelSize() const
{
	return PixelSize(m_Format);
}

const char *CImageInfo::FormatName() const
{
	return FormatName(m_Format);
}

size_t CImageInfo::DataSize() const
{
	size_t Size = 0;
	const bool ValidSize = DataSize(Size);
	dbg_assert(ValidSize, "Image data size overflow");
	return ValidSize ? Size : 0;
}

bool CImageInfo::DataSize(size_t &Size) const
{
	if(m_Format == FORMAT_UNDEFINED)
	{
		Size = 0;
		return false;
	}
	const size_t FormatPixelSize = PixelSize(m_Format);
	if(m_Height != 0 && m_Width > std::numeric_limits<size_t>::max() / m_Height)
	{
		Size = 0;
		return false;
	}
	const size_t PixelCount = m_Width * m_Height;
	if(FormatPixelSize != 0 && PixelCount > std::numeric_limits<size_t>::max() / FormatPixelSize)
	{
		Size = 0;
		return false;
	}
	Size = PixelCount * FormatPixelSize;
	return true;
}

bool CImageInfo::DataEquals(const CImageInfo &Other) const
{
	if(m_Width != Other.m_Width || m_Height != Other.m_Height || m_Format != Other.m_Format)
		return false;
	if(m_pData == nullptr && Other.m_pData == nullptr)
		return true;
	if(m_pData == nullptr || Other.m_pData == nullptr)
		return false;
	return mem_comp(m_pData, Other.m_pData, DataSize()) == 0;
}

ColorRGBA CImageInfo::PixelColor(size_t x, size_t y) const
{
	const size_t PixelStartIndex = (x + m_Width * y) * PixelSize();

	ColorRGBA Color;
	if(m_Format == FORMAT_R)
	{
		Color.r = Color.g = Color.b = 1.0f;
		Color.a = m_pData[PixelStartIndex] / 255.0f;
	}
	else if(m_Format == FORMAT_RA)
	{
		Color.r = Color.g = Color.b = m_pData[PixelStartIndex] / 255.0f;
		Color.a = m_pData[PixelStartIndex + 1] / 255.0f;
	}
	else
	{
		Color.r = m_pData[PixelStartIndex + 0] / 255.0f;
		Color.g = m_pData[PixelStartIndex + 1] / 255.0f;
		Color.b = m_pData[PixelStartIndex + 2] / 255.0f;
		if(m_Format == FORMAT_RGBA)
		{
			Color.a = m_pData[PixelStartIndex + 3] / 255.0f;
		}
		else
		{
			Color.a = 1.0f;
		}
	}
	return Color;
}

void CImageInfo::SetPixelColor(size_t x, size_t y, ColorRGBA Color) const
{
	const size_t PixelStartIndex = (x + m_Width * y) * PixelSize();

	if(m_Format == FORMAT_R)
	{
		m_pData[PixelStartIndex] = round_to_int(Color.a * 255.0f);
	}
	else if(m_Format == FORMAT_RA)
	{
		m_pData[PixelStartIndex] = round_to_int((Color.r + Color.g + Color.b) / 3.0f * 255.0f);
		m_pData[PixelStartIndex + 1] = round_to_int(Color.a * 255.0f);
	}
	else
	{
		m_pData[PixelStartIndex + 0] = round_to_int(Color.r * 255.0f);
		m_pData[PixelStartIndex + 1] = round_to_int(Color.g * 255.0f);
		m_pData[PixelStartIndex + 2] = round_to_int(Color.b * 255.0f);
		if(m_Format == FORMAT_RGBA)
		{
			m_pData[PixelStartIndex + 3] = round_to_int(Color.a * 255.0f);
		}
	}
}

void CImageInfo::CopyRectFrom(const CImageInfo &SrcImage, size_t SrcX, size_t SrcY, size_t Width, size_t Height, size_t DestX, size_t DestY) const
{
	const size_t PixelSize = SrcImage.PixelSize();
	const size_t CopySize = Width * PixelSize;
	for(size_t Y = 0; Y < Height; ++Y)
	{
		const size_t SrcOffset = ((SrcY + Y) * SrcImage.m_Width + SrcX) * PixelSize;
		const size_t DestOffset = ((DestY + Y) * m_Width + DestX) * PixelSize;
		mem_copy(&m_pData[DestOffset], &SrcImage.m_pData[SrcOffset], CopySize);
	}
}

CImageInfo CImageInfo::DeepCopy() const
{
	size_t Size = 0;
	if(m_pData == nullptr || !DataSize(Size))
		return CImageInfo();

	CImageInfo Copy;
	Copy.m_Width = m_Width;
	Copy.m_Height = m_Height;
	Copy.m_Format = m_Format;
	Copy.m_pData = static_cast<uint8_t *>(malloc(Size));
	if(Copy.m_pData == nullptr)
	{
		Copy.Free();
		return CImageInfo();
	}
	mem_copy(Copy.m_pData, m_pData, Size);
	return Copy;
}
