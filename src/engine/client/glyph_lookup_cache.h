#ifndef ENGINE_CLIENT_GLYPH_LOOKUP_CACHE_H
#define ENGINE_CLIENT_GLYPH_LOOKUP_CACHE_H

#include <array>
#include <cstddef>

// 固定容量的近期字形索引，不分配内存；冲突只导致重新走原查找路径。
// 指针由字形表持有，清空字形表或更改字体回退链前必须 Reset。
template<typename TGlyph>
class CQmGlyphLookupCache
{
	struct SEntry
	{
		const void *m_pSelectedFace = nullptr;
		int m_Chr = 0;
		int m_FontSize = 0;
		int m_Profile = 0;
		const TGlyph *m_pGlyph = nullptr;
	};
	std::array<SEntry, 256> m_aEntries{};

	static size_t Slot(int Chr, int FontSize, int Profile)
	{
		return (static_cast<size_t>(Chr) * 31 + static_cast<size_t>(FontSize) + static_cast<size_t>(Profile) * 131) % 256;
	}

public:
	const TGlyph *Find(const void *pSelectedFace, int Chr, int FontSize, int Profile = 0) const
	{
		const SEntry &Entry = m_aEntries[Slot(Chr, FontSize, Profile)];
		return Entry.m_pSelectedFace == pSelectedFace && Entry.m_Chr == Chr && Entry.m_FontSize == FontSize && Entry.m_Profile == Profile ? Entry.m_pGlyph : nullptr;
	}

	void Store(const void *pSelectedFace, int Chr, int FontSize, const TGlyph *pGlyph, int Profile = 0)
	{
		m_aEntries[Slot(Chr, FontSize, Profile)] = {pSelectedFace, Chr, FontSize, Profile, pGlyph};
	}

	void Reset()
	{
		m_aEntries = {};
	}
};

#endif
