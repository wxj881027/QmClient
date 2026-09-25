/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_GLYPH_ATLAS_H
#define ENGINE_CLIENT_GLYPH_ATLAS_H

#include <base/dbg.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

class CAtlas
{
	struct SSectionKeyHash
	{
		size_t operator()(const std::tuple<size_t, size_t> &Key) const
		{
			// Width and height should never be above 2^16 so this hash should cause no collisions
			return (std::get<0>(Key) << 16) ^ std::get<1>(Key);
		}
	};

	struct SSectionKeyEquals
	{
		bool operator()(const std::tuple<size_t, size_t> &Lhs, const std::tuple<size_t, size_t> &Rhs) const
		{
			return std::get<0>(Lhs) == std::get<0>(Rhs) && std::get<1>(Lhs) == std::get<1>(Rhs);
		}
	};

	struct SSection
	{
		size_t m_X;
		size_t m_Y;
		size_t m_W;
		size_t m_H;

		SSection() = default;

		SSection(size_t X, size_t Y, size_t W, size_t H) :
			m_X(X), m_Y(Y), m_W(W), m_H(H)
		{
		}
	};

	/**
	 * Sections with a smaller width or height will not be created
	 * when cutting larger sections, to prevent collecting many
	 * small, mostly unusable sections.
	 */
	static constexpr size_t MIN_SECTION_DIMENSION = 6;

	/**
	 * Sections with larger width or height will be stored in m_vSections.
	 * Sections with width and height equal or smaller will be stored in m_SectionsMap.
	 * This achieves a good balance between the size of the vector storing all large
	 * sections and the map storing vectors of all sections with specific small sizes.
	 * Lowering this value will result in the size of m_vSections becoming the bottleneck.
	 * Increasing this value will result in the map becoming the bottleneck.
	 */
	static constexpr size_t MAX_SECTION_DIMENSION_MAPPED = 8 * MIN_SECTION_DIMENSION;

	size_t m_TextureDimension;
	std::vector<SSection> m_vSections;
	std::unordered_map<std::tuple<size_t, size_t>, std::vector<SSection>, SSectionKeyHash, SSectionKeyEquals> m_SectionsMap;

	void AddSection(size_t X, size_t Y, size_t W, size_t H)
	{
		std::vector<SSection> &vSections = W <= MAX_SECTION_DIMENSION_MAPPED && H <= MAX_SECTION_DIMENSION_MAPPED ? m_SectionsMap[std::make_tuple(W, H)] : m_vSections;
		vSections.emplace_back(X, Y, W, H);
	}

	void UseSection(const SSection &Section, size_t Width, size_t Height, int &PosX, int &PosY)
	{
		PosX = Section.m_X;
		PosY = Section.m_Y;

		// Create cut sections
		const size_t CutW = Section.m_W - Width;
		const size_t CutH = Section.m_H - Height;
		if(CutW == 0)
		{
			if(CutH >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X, Section.m_Y + Height, Section.m_W, CutH);
		}
		else if(CutH == 0)
		{
			if(CutW >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X + Width, Section.m_Y, CutW, Section.m_H);
		}
		else if(CutW > CutH)
		{
			if(CutW >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X + Width, Section.m_Y, CutW, Section.m_H);
			if(CutH >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X, Section.m_Y + Height, Width, CutH);
		}
		else
		{
			if(CutH >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X, Section.m_Y + Height, Section.m_W, CutH);
			if(CutW >= MIN_SECTION_DIMENSION)
				AddSection(Section.m_X + Width, Section.m_Y, CutW, Height);
		}
	}

public:
	void Clear(size_t TextureDimension)
	{
		m_TextureDimension = TextureDimension;
		m_vSections.clear();
		m_vSections.emplace_back(0, 0, m_TextureDimension, m_TextureDimension);
		m_SectionsMap.clear();
	}

	void IncreaseDimension(size_t NewTextureDimension)
	{
		dbg_assert(NewTextureDimension == m_TextureDimension * 2, "New atlas dimension must be twice the old one");
		// Create 3 square sections to cover the new area, add the sections
		// to the beginning of the vector so they are considered last.
		m_vSections.emplace_back(m_TextureDimension, m_TextureDimension, m_TextureDimension, m_TextureDimension);
		m_vSections.emplace_back(m_TextureDimension, 0, m_TextureDimension, m_TextureDimension);
		m_vSections.emplace_back(0, m_TextureDimension, m_TextureDimension, m_TextureDimension);
		std::rotate(m_vSections.rbegin(), m_vSections.rbegin() + 3, m_vSections.rend());
		m_TextureDimension = NewTextureDimension;
	}

	bool Add(size_t Width, size_t Height, int &PosX, int &PosY)
	{
		if(m_vSections.empty() || m_TextureDimension < Width || m_TextureDimension < Height)
			return false;

		// Find small section more efficiently by using maps
		if(Width <= MAX_SECTION_DIMENSION_MAPPED && Height <= MAX_SECTION_DIMENSION_MAPPED)
		{
			const auto UseSectionFromMap = [&](size_t CheckWidth, size_t CheckHeight) {
				// 查询不存在的尺寸不应创建空桶，只有实际切出空区时才插入。
				const auto It = m_SectionsMap.find(std::make_tuple(CheckWidth, CheckHeight));
				if(It == m_SectionsMap.end())
					return false;
				std::vector<SSection> &vSections = It->second;
				if(!vSections.empty())
				{
					const SSection Section = vSections.back();
					vSections.pop_back();
					UseSection(Section, Width, Height, PosX, PosY);
					return true;
				}
				return false;
			};

			if(UseSectionFromMap(Width, Height))
				return true;

			for(size_t CheckWidth = Width + 1; CheckWidth <= MAX_SECTION_DIMENSION_MAPPED; ++CheckWidth)
			{
				if(UseSectionFromMap(CheckWidth, Height))
					return true;
			}

			for(size_t CheckHeight = Height + 1; CheckHeight <= MAX_SECTION_DIMENSION_MAPPED; ++CheckHeight)
			{
				if(UseSectionFromMap(Width, CheckHeight))
					return true;
			}

			// We don't iterate sections in the map with increasing width and height at the same time,
			// because it's slower and doesn't noticeable increase the atlas utilization.
		}

		// Check vector for larger section
		size_t SmallestLossValue = std::numeric_limits<size_t>::max();
		size_t SmallestLossIndex = m_vSections.size();
		size_t SectionIndex = m_vSections.size();
		do
		{
			--SectionIndex;
			const SSection &Section = m_vSections[SectionIndex];
			if(Section.m_W < Width || Section.m_H < Height)
				continue;

			const size_t LossW = Section.m_W - Width;
			const size_t LossH = Section.m_H - Height;

			size_t Loss;
			if(LossW == 0)
				Loss = LossH;
			else if(LossH == 0)
				Loss = LossW;
			else
				Loss = LossW * LossH;

			if(Loss < SmallestLossValue)
			{
				SmallestLossValue = Loss;
				SmallestLossIndex = SectionIndex;
				if(SmallestLossValue == 0)
					break;
			}
		} while(SectionIndex > 0);
		if(SmallestLossIndex == m_vSections.size())
			return false; // No usable section found in vector

		// Use the section with the smallest loss
		const SSection Section = m_vSections[SmallestLossIndex];
		m_vSections.erase(m_vSections.begin() + SmallestLossIndex);
		UseSection(Section, Width, Height, PosX, PosY);
		return true;
	}
};

#endif
