#ifndef ENGINE_CLIENT_QM_FONT_FACE_REGISTRY_H
#define ENGINE_CLIENT_QM_FONT_FACE_REGISTRY_H

#include "qm_font_names.h"

#include <algorithm>
#include <stdexcept>

// 只借用 collection 持有的字体面；名称与预览标记必须和 face 列表一起发布。
class CQmFontFaceRegistry
{
	std::vector<FT_Face> m_vFaces;
	std::vector<FT_Face> m_vPreviewFaces;
	std::unordered_map<FT_Face, SQmFontFaceNames> m_Names;

	static void ReserveFaces(std::vector<FT_Face> &vFaces, size_t Count)
	{
		if(Count > vFaces.max_size() - vFaces.size())
			throw std::length_error("font face registry is full");
		const size_t Required = vFaces.size() + Count;
		if(Required <= vFaces.capacity())
			return;
		// 成批发布前留足空间，同时保留摊销增长，避免每个文件都复制整个列表。
		const size_t Grown = vFaces.capacity() > vFaces.max_size() / 2 ? vFaces.max_size() : vFaces.capacity() * 2;
		vFaces.reserve(std::max(Required, Grown));
	}

public:
	const std::vector<FT_Face> &Faces() const { return m_vFaces; }
	const std::unordered_map<FT_Face, SQmFontFaceNames> &Names() const { return m_Names; }
	bool IsPreview(FT_Face Face) const { return std::find(m_vPreviewFaces.begin(), m_vPreviewFaces.end(), Face) != m_vPreviewFaces.end(); }

	template<typename TGetNames>
	void AddCollection(const std::vector<FT_Face> &vNewFaces, bool Preview, TGetNames &&GetNames)
	{
		const size_t OldCount = m_vFaces.size();
		const size_t OldPreviewCount = m_vPreviewFaces.size();
		ReserveFaces(m_vFaces, vNewFaces.size());
		if(Preview)
			ReserveFaces(m_vPreviewFaces, vNewFaces.size());
		try
		{
			for(FT_Face Face : vNewFaces)
			{
				if(Face == nullptr || m_Names.count(Face))
					continue;
				m_Names.emplace(Face, GetNames(Face));
				// 两个指针数组已留足空间，名称登记成功后不会再分配。
				m_vFaces.push_back(Face);
				if(Preview)
					m_vPreviewFaces.push_back(Face);
			}
		}
		catch(...)
		{
			while(m_vFaces.size() > OldCount)
			{
				m_Names.erase(m_vFaces.back());
				m_vFaces.pop_back();
			}
			m_vPreviewFaces.resize(OldPreviewCount);
			throw;
		}
	}

	void AddCollection(const std::vector<FT_Face> &vNewFaces, bool Preview)
	{
		AddCollection(vNewFaces, Preview, QmFontFaceNames);
	}
};

#endif
