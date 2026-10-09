#ifndef ENGINE_GFX_IMAGE_LOADER_H
#define ENGINE_GFX_IMAGE_LOADER_H

#include <base/types.h>

#include <engine/image.h>

#include <limits>
#include <string>
#include <vector>

class CByteBufferReader
{
	const uint8_t *m_pData;
	size_t m_Size;
	size_t m_ReadOffset = 0;
	bool m_Error = false;

public:
	CByteBufferReader(const uint8_t *pData, size_t Size) :
		m_pData(pData),
		m_Size(Size) {}

	bool Read(void *pData, size_t Size);
	bool Error() const { return m_Error; }
	size_t Size() const { return m_Size; }
};

class CByteBufferWriter
{
	std::vector<uint8_t> m_vBuffer;

public:
	void Write(const void *pData, size_t Size);
	const uint8_t *Data() const { return m_vBuffer.data(); }
	size_t Size() const { return m_vBuffer.size(); }
};

class CImageLoader
{
public:
	CImageLoader() = delete;

	enum
	{
		PNGLITE_COLOR_TYPE = 1 << 0,
		PNGLITE_BIT_DEPTH = 1 << 1,
		PNGLITE_INTERLACE_TYPE = 1 << 2,
		PNGLITE_COMPRESSION_TYPE = 1 << 3,
		PNGLITE_FILTER_TYPE = 1 << 4,
	};

	static bool LoadPng(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, int &PngliteIncompatible, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());
	static bool LoadPng(IOHANDLE File, const char *pFilename, CImageInfo &Image, int &PngliteIncompatible);
	static bool LoadPng(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());

	static bool LoadWebP(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());
	static bool LoadWebP(IOHANDLE File, const char *pFilename, CImageInfo &Image);
	static bool LoadWebP(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());

	// Comment 使用 UTF-8 iTXt 写在像素前；读取只扫描 PNG 头，不解码图片。
	static bool ReadPngComment(IOHANDLE File, const char *pFilename, std::string &Comment);
	static bool SavePng(CByteBufferWriter &Writer, const CImageInfo &Image, const char *pComment = nullptr);
	static bool SavePng(IOHANDLE File, const char *pFilename, const CImageInfo &Image, const char *pComment = nullptr);

	static bool SaveWebP(CByteBufferWriter &Writer, const CImageInfo &Image);
	static bool SaveWebP(IOHANDLE File, const char *pFilename, const CImageInfo &Image);
};

#endif // ENGINE_GFX_IMAGE_LOADER_H
