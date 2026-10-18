#ifndef ENGINE_CLIENT_QM_FONT_COLLECTION_H
#define ENGINE_CLIENT_QM_FONT_COLLECTION_H

#include <base/log.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

// 字体面借用整份文件；必须先销毁所有 face，再释放原始字节。
class CQmFontCollection
{
public:
	using TData = std::unique_ptr<void, decltype(&free)>;

private:
	TData m_pData;
	FT_Long m_Size;
	std::vector<FT_Face> m_vFaces;

public:
	CQmFontCollection(TData pData, FT_Long Size) :
		m_pData(std::move(pData)), m_Size(Size) {}
	~CQmFontCollection()
	{
		for(FT_Face Face : m_vFaces)
			FT_Done_Face(Face);
	}
	CQmFontCollection(const CQmFontCollection &) = delete;
	CQmFontCollection &operator=(const CQmFontCollection &) = delete;

	const FT_Byte *Data() const { return static_cast<const FT_Byte *>(m_pData.get()); }
	FT_Long Size() const { return m_Size; }
	const std::vector<FT_Face> &Faces() const { return m_vFaces; }

	static std::unique_ptr<CQmFontCollection> Load(FT_Library Library, const char *pName, TData pData, size_t Size)
	{
		if(Library == nullptr || pData == nullptr || Size == 0 || Size > static_cast<size_t>(std::numeric_limits<FT_Long>::max()))
			return nullptr;
		auto pCollection = std::make_unique<CQmFontCollection>(std::move(pData), static_cast<FT_Long>(Size));
		FT_Face Face = nullptr;
		const FT_Error CollectionError = FT_New_Memory_Face(Library, pCollection->Data(), pCollection->Size(), -1, &Face);
		if(CollectionError)
		{
			log_error("textrender", "Failed to load font file '%s': %s", pName, FT_Error_String(CollectionError));
			return nullptr;
		}
		const FT_Long NumFaces = Face->num_faces;
		FT_Done_Face(Face);
		for(FT_Long Index = 0; Index < NumFaces; ++Index)
		{
			const FT_Error FaceError = FT_New_Memory_Face(Library, pCollection->Data(), pCollection->Size(), Index, &Face);
			if(FaceError)
			{
				log_error("textrender", "Failed to load font face %ld from font file '%s': %s", Index, pName, FT_Error_String(FaceError));
				continue;
			}
			// 容器扩容失败时，当前面与此前完成的面都仍有 owner。
			std::unique_ptr<FT_FaceRec, decltype(&FT_Done_Face)> pFace(Face, &FT_Done_Face);
			pCollection->m_vFaces.push_back(Face);
			pFace.release();
		}
		if(pCollection->m_vFaces.empty())
		{
			log_error("textrender", "Failed to load font file '%s': no font faces could be loaded", pName);
			return nullptr;
		}
		return pCollection;
	}
};

#endif
