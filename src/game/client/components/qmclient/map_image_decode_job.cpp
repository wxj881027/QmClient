#include "map_image_decode_job.h"

#include <base/log.h>
#include <base/perf_timer.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <zlib.h>

#include <cstdlib>
#include <memory>

CMapImageDecodeJob::CMapImageDecodeJob(IStorage *pStorage, const char *pPath, bool Trace) :
	m_pStorage(pStorage), m_Trace(Trace), m_Path(pPath)
{
	Abortable(true);
}

CMapImageDecodeJob::~CMapImageDecodeJob()
{
	m_Image.Free();
}

void CMapImageDecodeJob::Run()
{
	if(State() == STATE_ABORTED)
		return;
	CPerfTimer Timer(m_Trace);
	if(m_pStorage)
	{
		void *pData = nullptr;
		unsigned Size = 0;
		const bool Read = m_pStorage->ReadFile(m_Path.c_str(), IStorage::TYPE_ALL, &pData, &Size);
		std::unique_ptr<void, decltype(&free)> pFile(pData, free);
		m_ReadMs = Timer.ElapsedMs();
		if(!Read || State() == STATE_ABORTED)
		{
			if(!Read)
				log_error("mapimages", "Failed to read map texture '%s'.", m_Path.c_str());
			return;
		}
		Timer.Reset();
		CByteBufferReader Reader(static_cast<const uint8_t *>(pData), Size);
		// 文件贴图沿用 LoadTexture 的 RGB/RGBA PNG 与 WebP 回退语义。
		if(!CImageLoader::LoadPng(Reader, m_Path.c_str(), m_Image, m_PngliteIncompatible) ||
			(m_Image.m_Format != CImageInfo::FORMAT_RGB && m_Image.m_Format != CImageInfo::FORMAT_RGBA))
		{
			m_Image.Free();
			m_PngliteIncompatible = 0;
			if(!CImageLoader::LoadWebP(pData, Size, m_Path.c_str(), m_Image))
				log_error("mapimages", "Failed to decode map texture '%s'.", m_Path.c_str());
		}
		if(m_Image.m_pData && m_Image.m_Format != CImageInfo::FORMAT_RGBA)
		{
			ConvertToRgba(m_Image);
			if(m_Image.m_Format != CImageInfo::FORMAT_RGBA)
				m_Image.Free();
		}
	}
	else
	{
		size_t PixelBytes;
		if(!m_Image.DataSize(PixelBytes) || PixelBytes == 0 || PixelBytes > m_UncompressedSize)
			return;
		m_Image.m_pData = static_cast<uint8_t *>(malloc(m_UncompressedSize));
		unsigned long Size = m_UncompressedSize;
		if(!m_Image.m_pData || uncompress(m_Image.m_pData, &Size, m_vCompressed.data(), static_cast<unsigned long>(m_vCompressed.size())) != Z_OK || Size != m_UncompressedSize)
			m_Image.Free();
	}
	m_DecodeMs = Timer.ElapsedMs();
}
