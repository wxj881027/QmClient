/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "font_size_cache.h"
#include "glyph_lookup_cache.h"
#include "glyph_outline.h"
#include "qm_font_category.h"
#include "text_layout_string.h"
#include "text_word_cursor.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/perf_logging.h>

// ft2 texture
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

// TClient
static void ReplaceHyphensWithSpaces(char *pStr)
{
	if(pStr == nullptr)
		return;
	while(*pStr)
	{
		if(*pStr == '-')
			*pStr = ' ';
		pStr++;
	}
}

enum
{
	FONT_NAME_SIZE = 128,
};

struct SGlyph
{
	enum class EState
	{
		UNINITIALIZED,
		RENDERED,
		ERROR,
	};
	EState m_State = EState::UNINITIALIZED;

	int m_FontSize;
	FT_Face m_Face;
	int m_Chr;
	FT_UInt m_GlyphIndex;

	// these values are scaled to the font size
	// width * font_size == real_size
	float m_Width;
	float m_Height;
	float m_CharWidth;
	float m_CharHeight;
	float m_OffsetX;
	float m_OffsetY;
	float m_AdvanceX;

	float m_aUVs[4];
};

struct SGlyphKeyHash
{
	size_t operator()(const std::tuple<FT_Face, int, int> &Key) const
	{
		size_t Hash = 17;
		Hash = Hash * 31 + std::hash<FT_Face>()(std::get<0>(Key));
		Hash = Hash * 31 + std::hash<int>()(std::get<1>(Key));
		Hash = Hash * 31 + std::hash<int>()(std::get<2>(Key));
		return Hash;
	}
};

struct SGlyphKeyEquals
{
	bool operator()(const std::tuple<FT_Face, int, int> &Lhs, const std::tuple<FT_Face, int, int> &Rhs) const
	{
		return std::get<0>(Lhs) == std::get<0>(Rhs) && std::get<1>(Lhs) == std::get<1>(Rhs) && std::get<2>(Lhs) == std::get<2>(Rhs);
	}
};

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

class CGlyphMap
{
public:
	enum
	{
		FONT_TEXTURE_FILL = 0, // the main text body
		FONT_TEXTURE_OUTLINE, // the text outline
		NUM_FONT_TEXTURES,
	};

private:
	FT_Library m_FTLibrary = nullptr;
	/**
	 * The initial dimension of the atlas textures.
	 *
	 * QmClient: 提高到 4096 (16 MB per texture; NUM_FONT_TEXTURES=2 → 33 MB total，
	 * 单通道 alpha 纹理)。
	 * 历史：1024 → 2048 消除了设置页首帧扩容（中文常用字集 ~3500 字）。
	 * 4096 原因：实测进服后其他玩家名/MOTD/聊天的生僻字形仍会把 2048 (~4000
	 * 字形) 的图集挤满，触发运行时 IncreaseGlyphMapSize → UnloadTextures +
	 * 全量重传——该耗时计入 glyph_rasterize_ms，perf 实测单帧 794ms/253ms
	 * 尖峰，表现为"进服后锤的前几下某一下突然卡顿，之后不再出现"（扩容后
	 * 图集翻倍不再触发；重启后图集重置，每次进服复现）。4096 容纳约 16000
	 * 字形，覆盖中文常用字集 + 多人玩家名/聊天字形，从源头消除进服后的
	 * 运行时扩容。代价：显存 33 MB（比 2048 多 25 MB），现代 GPU 可接受；
	 * 极端情况仍保留向 8192 的运行时扩容路径。
	 */
	static constexpr int INITIAL_ATLAS_DIMENSION = 4096;

	/**
	 * The maximum dimension of the atlas textures.
	 * Results in 256 MB of memory being used per texture.
	 */
	static constexpr int MAXIMUM_ATLAS_DIMENSION = 16 * 1024;

	/**
	 * The minimum supported font size.
	 */
	static constexpr int MIN_FONT_SIZE = 6;

	/**
	 * The maximum supported font size.
	 */
	static constexpr int MAX_FONT_SIZE = 128;

	/**
	 * White square to indicate missing glyph.
	 */
	static constexpr int REPLACEMENT_CHARACTER = 0x25a1;

	IGraphics *m_pGraphics;
	IGraphics *Graphics() { return m_pGraphics; }

	// Atlas textures and data
	IGraphics::CTextureHandle m_aTextures[NUM_FONT_TEXTURES];
	// Width and height are the same, all font textures have the same dimensions
	size_t m_TextureDimension = INITIAL_ATLAS_DIMENSION;
	// Keep the full texture data, because OpenGL doesn't provide texture copying
	uint8_t *m_apTextureData[NUM_FONT_TEXTURES];
	CAtlas m_TextureAtlas;
	std::unordered_map<std::tuple<FT_Face, int, int>, SGlyph, SGlyphKeyHash, SGlyphKeyEquals> m_Glyphs;
	// QmClient: 近期字形命中的直接索引。字形表在热路径上被按 (face, chr, size) 反复查询，
	// 这里用固定容量、零分配的缓存挡掉大部分哈希查找；冲突只导致回落到原查找路径。
	CQmGlyphLookupCache<SGlyph> m_GlyphLookupCache;
	// QmClient: 记录最近一次成功的 FT_Set_Pixel_Sizes(Face, Size)，避免同一 face
	// 在同一字号上每帧重复设置；绕过该入口的调用必须显式失效。
	mutable CQmFontSizeCache m_FacePixelSizeCache;

	// QmClient: 最近缓存未命中（需要光栅化）的字形 (Chr, FontSize)，供菜单在关闭时的
	// 空闲帧分帧预热，避免下次打开菜单时一次性光栅化上百个字形造成掉帧；集合会持久化，
	// 使重启客户端后的第一次打开也能命中缓存。
	static constexpr size_t QM_MAX_RECENT_GLYPH_MISSES = 4096;
	std::vector<std::pair<int, int>> m_vQmRecentGlyphMisses;
	std::unordered_set<uint64_t> m_QmRecentGlyphMissKeys;
	// 预热进度游标：集合本身保留（供退出时持久化），游标只前进不回退；
	// 游标之后新增的缺失字形会被继续预热。
	size_t m_QmRecentGlyphPrewarmCursor = 0;
	bool m_QmRecordGlyphMisses = true;

	// Font faces
	FT_Face m_DefaultFace = nullptr;
	FT_Face m_IconFace = nullptr;
	FT_Face m_IconRegularFace = nullptr;
	FT_Face m_IconBoldFace = nullptr;
	FT_Face m_IconLightFace = nullptr;
	FT_Face m_IconFillFace = nullptr;
	FT_Face m_IconDuotoneFace = nullptr;
	FT_Face m_VariantFace = nullptr;
	FT_Face m_SelectedFace = nullptr;
	int m_CustomFontWeight = 400;
	// 中文/中日韩分类面的独立可变字重；分类面未设置或与默认面重合时不用此值。
	int m_QmCjkCustomFontWeight = 400;
	// 每个字体面的可变字重轴缓存：fvar 表解析开销大，且字重坐标未变化时
	// 不应重复写入，更不能反复清空整个字形图集（下拉框预览每帧多次切换字体面）。
	// m_vFtFaces 中的 face 与 CGlyphMap 同生命周期，按指针作键安全。
	struct SQmFaceWeightState
	{
		bool m_Initialized = false;
		bool m_HasWeightAxis = false;
		int m_WeightAxisIndex = -1;
		FT_Fixed m_WeightMin = 0;
		FT_Fixed m_WeightMax = 0;
		std::vector<FT_Fixed> m_vAppliedCoords;
	};
	std::unordered_map<FT_Face, SQmFaceWeightState> m_FaceWeightStates;
	std::vector<FT_Face> m_vFallbackFaces;
	// 分类字体面：中文/图标符号可由用户单独指定，字形解析时按字符分类优先尝试。
	// nullptr 表示该分类未设置，直接走原有回退链。
	FT_Face m_QmCjkFace = nullptr;
	FT_Face m_QmIconsFace = nullptr;
	// QmClient: 字体商店预览 face（未安装字体的临时加载面）。face 会话内不释放，
	// 指针作键安全；仅用于商店卡片渲染，不得进入字体族选择列表。
	std::vector<FT_Face> m_QmPreviewFaces;
	std::vector<FT_Face> m_vFtFaces;
	int m_QmPerfGlyphNew = 0;
	int m_QmPerfGlyphUploads = 0;
	double m_QmPerfGlyphRasterizeMs = 0.0;
	double m_QmPerfGlyphUploadMs = 0.0;
	// QmClient: 字形上传合批。UploadGlyph 只写 CPU 图集并记录脏区，不再逐字形
	// 提交驱动上传；FlushPendingGlyphUploads 在渲染前把脏区按行带合并为少量
	// 矩形一次性提交。首开界面/切换字重时单帧可产生上千次逐字形小上传，是
	// 阻塞尖峰的主因之一。
	struct SQmPendingGlyphUpload
	{
		int m_X;
		int m_Y;
		size_t m_Width;
		size_t m_Height;
	};
	std::vector<SQmPendingGlyphUpload> m_avQmPendingGlyphUploads[NUM_FONT_TEXTURES];
	int m_QmPendingGlyphUploadCount = 0;
	int m_QmPerfGlyphUploadBatches = 0;
	double m_QmPerfGlyphUploadBatchMs = 0.0;

	FT_Face GetFaceByName(const char *pFamilyName)
	{
		if(pFamilyName == nullptr || pFamilyName[0] == '\0')
			return nullptr;

		FT_Face FamilyNameMatch = nullptr;
		FT_Face PreferredFamilyMatch = nullptr;
		char aFamilyStyleName[FONT_NAME_SIZE];
		char aRequestedName[FONT_NAME_SIZE];
		str_copy(aRequestedName, pFamilyName);
		ReplaceHyphensWithSpaces(aRequestedName);

		for(const auto &CurrentFace : m_vFtFaces)
		{
			// Best match: font face with matching family and style name
			str_format(aFamilyStyleName, sizeof(aFamilyStyleName), "%s %s", CurrentFace->family_name, CurrentFace->style_name);
			char aNormalizedFamilyStyle[FONT_NAME_SIZE];
			str_copy(aNormalizedFamilyStyle, aFamilyStyleName);
			ReplaceHyphensWithSpaces(aNormalizedFamilyStyle);
			if(str_comp_nocase(aRequestedName, aNormalizedFamilyStyle) == 0)
			{
				return CurrentFace;
			}

			// Second best match: font face with matching family
			char aNormalizedFamily[FONT_NAME_SIZE];
			str_copy(aNormalizedFamily, CurrentFace->family_name != nullptr ? CurrentFace->family_name : "");
			ReplaceHyphensWithSpaces(aNormalizedFamily);
			if(!FamilyNameMatch && str_comp_nocase(aRequestedName, aNormalizedFamily) == 0)
			{
				FamilyNameMatch = CurrentFace;
				if(CurrentFace->style_name != nullptr && (str_comp_nocase(CurrentFace->style_name, "Regular") == 0 || str_comp_nocase(CurrentFace->style_name, "Book") == 0 || str_comp_nocase(CurrentFace->style_name, "Normal") == 0 || str_comp_nocase(CurrentFace->style_name, "Medium") == 0))
					PreferredFamilyMatch = CurrentFace;
			}

			// TClient
			// Third best match: match the fucking font name
			char aBuf[256];
			str_copy(aBuf, FT_Get_Postscript_Name(CurrentFace));
			ReplaceHyphensWithSpaces(aBuf);
			if(!FamilyNameMatch && str_comp_nocase(aRequestedName, aBuf) == 0)
			{
				FamilyNameMatch = CurrentFace;
			}
		}

		return PreferredFamilyMatch != nullptr ? PreferredFamilyMatch : FamilyNameMatch;
	}

	bool IncreaseGlyphMapSize()
	{
		if(m_TextureDimension >= MAXIMUM_ATLAS_DIMENSION)
			return false;

		const size_t NewTextureDimension = m_TextureDimension * 2;
		log_debug("textrender", "Increasing atlas dimension to %" PRIzu " (%" PRIzu " MB used for textures)", NewTextureDimension, (NewTextureDimension / 1024) * (NewTextureDimension / 1024) * NUM_FONT_TEXTURES);
		UnloadTextures();

		for(auto &pTextureData : m_apTextureData)
		{
			uint8_t *pTmpTexBuffer = new uint8_t[NewTextureDimension * NewTextureDimension];
			for(size_t y = 0; y < m_TextureDimension; ++y)
			{
				mem_copy(&pTmpTexBuffer[y * NewTextureDimension], &pTextureData[y * m_TextureDimension], m_TextureDimension);
				mem_zero(&pTmpTexBuffer[y * NewTextureDimension + m_TextureDimension], NewTextureDimension - m_TextureDimension);
			}
			mem_zero(&pTmpTexBuffer[m_TextureDimension * NewTextureDimension], (NewTextureDimension - m_TextureDimension) * NewTextureDimension);
			delete[] pTextureData;
			pTextureData = pTmpTexBuffer;
		}

		m_TextureAtlas.IncreaseDimension(NewTextureDimension);

		m_TextureDimension = NewTextureDimension;

		UploadTextures();
		// QmClient: 扩容已整张重建纹理，脏区数据随之失效。
		DiscardPendingGlyphUploads();
		return true;
	}

	void UploadTextures()
	{
		const size_t NewTextureSize = m_TextureDimension * m_TextureDimension;
		uint8_t *pTmpTextFillData = static_cast<uint8_t *>(malloc(NewTextureSize));
		uint8_t *pTmpTextOutlineData = static_cast<uint8_t *>(malloc(NewTextureSize));
		if(pTmpTextFillData == nullptr || pTmpTextOutlineData == nullptr)
		{
			free(pTmpTextFillData);
			free(pTmpTextOutlineData);
			log_error("textrender", "Failed to allocate text texture upload buffers.");
			return;
		}
		mem_copy(pTmpTextFillData, m_apTextureData[FONT_TEXTURE_FILL], NewTextureSize);
		mem_copy(pTmpTextOutlineData, m_apTextureData[FONT_TEXTURE_OUTLINE], NewTextureSize);
		if(!Graphics()->LoadTextTextures(m_TextureDimension, m_TextureDimension, m_aTextures[FONT_TEXTURE_FILL], m_aTextures[FONT_TEXTURE_OUTLINE], pTmpTextFillData, pTmpTextOutlineData))
			log_error("textrender", "Failed to create text textures.");
	}

	void UnloadTextures()
	{
		Graphics()->UnloadTextTextures(m_aTextures[FONT_TEXTURE_FILL], m_aTextures[FONT_TEXTURE_OUTLINE]);
	}

	FT_UInt GetCharGlyph(int Chr, FT_Face *pFace, bool AllowReplacementCharacter)
	{
		// QmClient: 分类字体只在正文字体预设下生效。图标字体渲染（ICON_FONT 预设）
		// 时 m_SelectedFace 指向图标面，其私用区映射必须保持原样，不能被分类面截胡。
		FT_Face aCategoryFaces[2] = {nullptr, nullptr};
		if(m_SelectedFace == nullptr)
		{
			if(QmIsCjkCodepoint(Chr))
			{
				aCategoryFaces[0] = m_QmCjkFace;
			}
			else if(QmIsIconSymbolCodepoint(Chr))
			{
				aCategoryFaces[0] = m_QmIconsFace;
				// 图标符号的兜底语义：未指定图标字体时该分类字形优先取中文字体；
				// 指定了则按图标字体自身渲染，缺字继续走默认回退链。
				if(m_QmIconsFace == nullptr)
					aCategoryFaces[1] = m_QmCjkFace;
			}
		}
		for(FT_Face CategoryFace : aCategoryFaces)
		{
			if(CategoryFace && CategoryFace->charmap)
			{
				if(FT_UInt GlyphIndex = FT_Get_Char_Index(CategoryFace, (FT_ULong)Chr))
				{
					*pFace = CategoryFace;
					return GlyphIndex;
				}
			}
		}

		for(FT_Face Face : {m_SelectedFace, m_DefaultFace, m_VariantFace})
		{
			if(Face && Face->charmap)
			{
				FT_UInt GlyphIndex = FT_Get_Char_Index(Face, (FT_ULong)Chr);
				if(GlyphIndex)
				{
					*pFace = Face;
					return GlyphIndex;
				}
			}
		}

		for(const auto &FallbackFace : m_vFallbackFaces)
		{
			if(FallbackFace->charmap)
			{
				FT_UInt GlyphIndex = FT_Get_Char_Index(FallbackFace, (FT_ULong)Chr);
				if(GlyphIndex)
				{
					*pFace = FallbackFace;
					return GlyphIndex;
				}
			}
		}

		if(!m_DefaultFace || !m_DefaultFace->charmap || !AllowReplacementCharacter)
		{
			*pFace = nullptr;
			return 0;
		}

		FT_UInt GlyphIndex = FT_Get_Char_Index(m_DefaultFace, (FT_ULong)REPLACEMENT_CHARACTER);
		*pFace = m_DefaultFace;

		if(GlyphIndex == 0)
		{
			log_debug("textrender", "Default font has no glyph for either %d or replacement char %d.", Chr, REPLACEMENT_CHARACTER);
		}

		return GlyphIndex;
	}

	void EnsureFacePixelSize(FT_Face Face, int FontSize) const
	{
		// 同一 face 连续用同一字号时跳过重复的 FT_Set_Pixel_Sizes；
		// 任何绕过此入口直接设置字号的路径都必须先 InvalidateFacePixelSizeCache()。
		m_FacePixelSizeCache.Ensure(Face, FontSize, [&]() { return FT_Set_Pixel_Sizes(Face, 0, FontSize); });
	}

	int AdjustOutlineThicknessToFontSize(int OutlineThickness, int FontSize) const
	{
		if(FontSize > 48)
			OutlineThickness *= 4;
		else if(FontSize >= 18)
			OutlineThickness *= 2;
		return OutlineThickness;
	}

	void UploadGlyph(int TextureIndex, int PosX, int PosY, size_t Width, size_t Height, uint8_t *pData)
	{
		const auto UploadStart = QmPerfEnabled() ? time_get_nanoseconds() : std::chrono::nanoseconds(0);
		for(size_t y = 0; y < Height; ++y)
		{
			mem_copy(&m_apTextureData[TextureIndex][PosX + ((y + PosY) * m_TextureDimension)], &pData[y * Width], Width);
		}
		// QmClient: 延迟上传——只写 CPU 图集并记录脏区，渲染前由
		// FlushPendingGlyphUploads 合并提交；调用方的临时缓冲至此消费完毕，
		// 原实现通过 UpdateTextTexture(IsMovedPointer) 移交所有权，此处直接释放。
		m_avQmPendingGlyphUploads[TextureIndex].push_back({PosX, PosY, Width, Height});
		++m_QmPendingGlyphUploadCount;
		free(pData);
		if(!QmPerfEnabled())
			return;
		++m_QmPerfGlyphUploads;
		m_QmPerfGlyphUploadMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - UploadStart).count();
	}

public:
	// QmClient: 把积累的字形脏区按行带（Y 区间重叠的相邻区域）合并为少量外接
	// 矩形提交。必须在任何采样字形纹理的渲染（RenderTextContainer）之前调用；
	// 脏区为空时零成本。合并后的矩形可能覆盖未使用的图集空隙（内容为 0），
	// 重传与 GPU 现状一致，无副作用。
	void FlushPendingGlyphUploads()
	{
		if(m_QmPendingGlyphUploadCount == 0)
			return;
		const auto FlushStart = QmPerfEnabled() ? time_get_nanoseconds() : std::chrono::nanoseconds(0);
		for(size_t TextureIndex = 0; TextureIndex < NUM_FONT_TEXTURES; ++TextureIndex)
		{
			auto &vRegions = m_avQmPendingGlyphUploads[TextureIndex];
			if(vRegions.empty())
				continue;
			std::sort(vRegions.begin(), vRegions.end(), [](const SQmPendingGlyphUpload &a, const SQmPendingGlyphUpload &b) {
				return a.m_Y < b.m_Y || (a.m_Y == b.m_Y && a.m_X < b.m_X);
			});
			size_t Index = 0;
			while(Index < vRegions.size())
			{
				int BandY0 = vRegions[Index].m_Y;
				int BandY1 = BandY0 + static_cast<int>(vRegions[Index].m_Height);
				int MinX = vRegions[Index].m_X;
				int MaxX = MinX + static_cast<int>(vRegions[Index].m_Width);
				size_t MergeIndex = Index + 1;
				while(MergeIndex < vRegions.size() && vRegions[MergeIndex].m_Y < BandY1)
				{
					BandY1 = std::max(BandY1, vRegions[MergeIndex].m_Y + static_cast<int>(vRegions[MergeIndex].m_Height));
					MinX = std::min(MinX, vRegions[MergeIndex].m_X);
					MaxX = std::max(MaxX, vRegions[MergeIndex].m_X + static_cast<int>(vRegions[MergeIndex].m_Width));
					++MergeIndex;
				}
				const size_t Width = static_cast<size_t>(MaxX - MinX);
				const size_t Height = static_cast<size_t>(BandY1 - BandY0);
				// 数据必须从 CPU 图集拷出：图集内存随后会被新字形继续覆写，
				// 而纹理更新命令是异步交给渲染线程执行的。
				uint8_t *pCopy = static_cast<uint8_t *>(malloc(Width * Height));
				if(pCopy != nullptr)
				{
					for(size_t y = 0; y < Height; ++y)
					{
						mem_copy(&pCopy[y * Width], &m_apTextureData[TextureIndex][(BandY0 + static_cast<int>(y)) * m_TextureDimension + MinX], Width);
					}
					Graphics()->UpdateTextTexture(m_aTextures[TextureIndex], MinX, BandY0, Width, Height, pCopy, true);
				}
				else
				{
					log_error("textrender", "Failed to allocate merged glyph upload buffer (%" PRIzu "x%" PRIzu ").", Width, Height);
				}
				Index = MergeIndex;
			}
			vRegions.clear();
		}
		m_QmPendingGlyphUploadCount = 0;
		if(QmPerfEnabled())
		{
			++m_QmPerfGlyphUploadBatches;
			m_QmPerfGlyphUploadBatchMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - FlushStart).count();
		}
	}

	// QmClient: 图集重置（Clear/扩容）后 CPU 图集已全量重传，积累的脏区坐标
	// 不再有意义，必须丢弃以免重传过期区域。
	void DiscardPendingGlyphUploads()
	{
		for(auto &vRegions : m_avQmPendingGlyphUploads)
			vRegions.clear();
		m_QmPendingGlyphUploadCount = 0;
	}

private:
	bool FitGlyph(size_t Width, size_t Height, int &PosX, int &PosY)
	{
		return m_TextureAtlas.Add(Width, Height, PosX, PosY);
	}

	bool RenderGlyph(SGlyph &Glyph)
	{
		const auto RasterizeStart = QmPerfEnabled() ? time_get_nanoseconds() : std::chrono::nanoseconds(0);
		EnsureFacePixelSize(Glyph.m_Face, Glyph.m_FontSize);

		if(FT_Load_Glyph(Glyph.m_Face, Glyph.m_GlyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP))
		{
			log_debug("textrender", "Error loading glyph. Chr=%d GlyphIndex=%u", Glyph.m_Chr, Glyph.m_GlyphIndex);
			return false;
		}

		const FT_Bitmap *pBitmap = &Glyph.m_Face->glyph->bitmap;
		if(pBitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
		{
			log_debug("textrender", "Error loading glyph, unsupported pixel mode. Chr=%d GlyphIndex=%u PixelMode=%d", Glyph.m_Chr, Glyph.m_GlyphIndex, pBitmap->pixel_mode);
			return false;
		}

		const unsigned RealWidth = pBitmap->width;
		const unsigned RealHeight = pBitmap->rows;

		// adjust spacing
		int OutlineThickness = 0;
		int x = 0;
		int y = 0;
		if(RealWidth > 0)
		{
			OutlineThickness = AdjustOutlineThicknessToFontSize(1, Glyph.m_FontSize);
			x += (OutlineThickness + 1);
			y += (OutlineThickness + 1);
		}

		const unsigned Width = RealWidth + x * 2;
		const unsigned Height = RealHeight + y * 2;

		int X = 0;
		int Y = 0;

		if(Width > 0 && Height > 0)
		{
			// find space in atlas, or increase size if necessary
			while(!FitGlyph(Width, Height, X, Y))
			{
				if(!IncreaseGlyphMapSize())
				{
					log_debug("textrender", "Cannot fit glyph into atlas, which is already at maximum size. Chr=%d GlyphIndex=%u", Glyph.m_Chr, Glyph.m_GlyphIndex);
					return false;
				}
			}

			// prepare glyph data
			const size_t GlyphDataSize = (size_t)Width * Height * sizeof(uint8_t);
			uint8_t *pGlyphDataFill = static_cast<uint8_t *>(malloc(GlyphDataSize));
			uint8_t *pGlyphDataOutline = static_cast<uint8_t *>(malloc(GlyphDataSize));
			if(pGlyphDataFill == nullptr || pGlyphDataOutline == nullptr)
			{
				free(pGlyphDataFill);
				free(pGlyphDataOutline);
				log_debug("textrender", "Failed to allocate glyph data. Chr=%d GlyphIndex=%u", Glyph.m_Chr, Glyph.m_GlyphIndex);
				return false;
			}
			mem_zero(pGlyphDataFill, GlyphDataSize);
			for(unsigned py = 0; py < pBitmap->rows; ++py)
			{
				mem_copy(&pGlyphDataFill[(py + y) * Width + x], &pBitmap->buffer[py * pBitmap->width], pBitmap->width);
			}
			QmGrowGlyphOutline(pGlyphDataFill, pGlyphDataOutline, Width, Height, OutlineThickness);

			// upload the glyph
			UploadGlyph(FONT_TEXTURE_FILL, X, Y, Width, Height, pGlyphDataFill);
			UploadGlyph(FONT_TEXTURE_OUTLINE, X, Y, Width, Height, pGlyphDataOutline);
		}
		if(QmPerfEnabled())
		{
			++m_QmPerfGlyphNew;
			m_QmPerfGlyphRasterizeMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - RasterizeStart).count();
		}

		// set glyph info
		{
			Glyph.m_Height = Height;
			Glyph.m_Width = Width;
			Glyph.m_CharHeight = RealHeight;
			Glyph.m_CharWidth = RealWidth;
			Glyph.m_OffsetX = (Glyph.m_Face->glyph->metrics.horiBearingX >> 6);
			Glyph.m_OffsetY = -((Glyph.m_Face->glyph->metrics.height >> 6) - (Glyph.m_Face->glyph->metrics.horiBearingY >> 6));
			Glyph.m_AdvanceX = (Glyph.m_Face->glyph->advance.x >> 6);

			Glyph.m_aUVs[0] = X;
			Glyph.m_aUVs[1] = Y;
			Glyph.m_aUVs[2] = Glyph.m_aUVs[0] + Width;
			Glyph.m_aUVs[3] = Glyph.m_aUVs[1] + Height;

			Glyph.m_State = SGlyph::EState::RENDERED;
		}
		return true;
	}

public:
	CGlyphMap(IGraphics *pGraphics, FT_Library FtLibrary)
	{
		m_FTLibrary = FtLibrary;
		m_pGraphics = pGraphics;
		for(auto &pTextureData : m_apTextureData)
		{
			pTextureData = new uint8_t[m_TextureDimension * m_TextureDimension];
			mem_zero(pTextureData, m_TextureDimension * m_TextureDimension * sizeof(uint8_t));
		}

		m_TextureAtlas.Clear(m_TextureDimension);
		UploadTextures();
	}

	~CGlyphMap()
	{
		UnloadTextures();
		for(auto &pTextureData : m_apTextureData)
		{
			delete[] pTextureData;
		}
	}
	// TClient
	std::vector<FT_Face> *GetFaces() { return &m_vFtFaces; }

	// 图形设备重建后调用：字形的 CPU 侧位图数据（m_apTextureData）与图集分配都还在，
	// 因此只需要把字体纹理重新上传到新设备。旧句柄因为设备纪元自增已经失效，
	// 这里不需要（也不能）对旧句柄发删除命令。
	void OnGraphicsResourcesReset()
	{
		UploadTextures();
	}

	FT_Face DefaultFace() const
	{
		return m_DefaultFace;
	}

	// 供绕过 EnsureFacePixelSize 直接调用 FT_Set_Pixel_Sizes 的调用方使用。
	void InvalidateFacePixelSizeCache()
	{
		m_FacePixelSizeCache.Reset();
	}

	FT_Face IconFace() const
	{
		return m_IconFace;
	}

	void AddFace(FT_Face Face)
	{
		m_vFtFaces.push_back(Face);
	}

	bool SetDefaultFaceByName(const char *pFamilyName)
	{
		// 默认字体变化会改变 GetCharGlyph 的解析结果，近期索引必须失效。
		m_GlyphLookupCache.Reset();
		m_DefaultFace = GetFaceByName(pFamilyName);
		if(!m_DefaultFace)
		{
			if(!m_vFtFaces.empty())
			{
				m_DefaultFace = m_vFtFaces.front();
			}
			log_error("textrender", "The default font face '%s' could not be found", pFamilyName);
			return false;
		}
		return true;
	}

	// 用户配置可能保留了旧机器上的系统字体名；自定义字体不可用时只返回失败，
	// 由调用方保留当前默认面，避免把正常的配置迁移显示成错误。
	bool TrySetDefaultFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(!Face)
			return false;
		if(m_DefaultFace != Face)
		{
			m_GlyphLookupCache.Reset();
			m_DefaultFace = Face;
			// 字重数值未变化时 SetCustomFontWeight 会直接返回；切换字体面后仍需
			// 把当前字重轴重新写入新字体，并重建受影响的字形图集。
			ApplyCustomFontWeight();
			return true;
		}
		m_DefaultFace = Face;
		return true;
	}

	// QmClient: 查询名字能否解析为已加载 face（含商店预览面），供弹层按条目
	// 安全切换预览字体时使用。
	bool QmHasFace(const char *pFace)
	{
		return pFace != nullptr && pFace[0] != '\0' && GetFaceByName(pFace) != nullptr;
	}

	// QmClient: 查询名字（族名或族名+样式）对应的 face 是否覆盖任一 CJK 字形。
	// 取代表性码点采样（汉字/假名/片假名/谚文/CJK 标点各一），任一命中即视为
	// CJK 字体；CJK 字体下拉用它在 face 池里过滤掉纯拉丁字体。
	bool QmFaceHasCjk(const char *pFace)
	{
		FT_Face pFtFace = pFace != nullptr && pFace[0] != '\0' ? GetFaceByName(pFace) : nullptr;
		if(pFtFace == nullptr || pFtFace->charmap == nullptr)
			return false;
		static constexpr int aSampleCodepoints[] = {0x4E2D, 0x6C38, 0x3042, 0x30A2, 0xAC00, 0x3001};
		for(int Chr : aSampleCodepoints)
			if(FT_Get_Char_Index(pFtFace, (FT_ULong)Chr) != 0)
				return true;
		return false;
	}

	// QmClient: 标记/查询商店预览 face——预览面只服务商店卡片渲染，
	// UpdateCustomFontList 构建字体族选择列表时必须跳过它们。
	void QmMarkPreviewFace(FT_Face Face)
	{
		if(Face != nullptr && std::find(m_QmPreviewFaces.begin(), m_QmPreviewFaces.end(), Face) == m_QmPreviewFaces.end())
			m_QmPreviewFaces.push_back(Face);
	}

	bool QmIsPreviewFace(FT_Face Face) const
	{
		return std::find(m_QmPreviewFaces.begin(), m_QmPreviewFaces.end(), Face) != m_QmPreviewFaces.end();
	}

	bool SetIconFaceByName(const char *pFamilyName)
	{
		m_IconRegularFace = GetFaceByName(pFamilyName);
		if(!m_IconRegularFace)
		{
			log_error("textrender", "The icon font face '%s' could not be found", pFamilyName);
			return false;
		}
		m_IconFace = m_IconRegularFace;
		return true;
	}

	bool SetIconBoldFaceByName(const char *pFamilyName)
	{
		m_IconBoldFace = GetFaceByName(pFamilyName);
		if(!m_IconBoldFace)
		{
			// Bold is a preference, not a reason to leave ICON_FONT_BOLD without a face.
			m_IconBoldFace = m_IconRegularFace;
			log_warn("textrender", "The bold icon font face '%s' could not be found, falling back to regular", pFamilyName);
			return m_IconBoldFace != nullptr;
		}
		return true;
	}

	// 旧版 font index 可能没有 'icon bold' 键：沿用 regular 图标面，
	// 避免 ICON_FONT_BOLD（qm_ui_icon_weight 默认取 1）退化到默认正文字体而缺字形。
	void UseRegularFaceForIconBold()
	{
		m_IconBoldFace = m_IconRegularFace;
	}

	// 当前图标面缺失多少 FONT_ICON_* 码位；0 表示图标字体与码位匹配。
	int CountMissingIconGlyphs(const char *const *apIcons, size_t NumIcons) const
	{
		if(m_IconFace == nullptr)
			return static_cast<int>(NumIcons);
		int Missing = 0;
		for(size_t IconIndex = 0; IconIndex < NumIcons; ++IconIndex)
		{
			const char *pIcon = apIcons[IconIndex];
			const int Codepoint = str_utf8_decode(&pIcon);
			if(Codepoint <= 0 || FT_Get_Char_Index(m_IconFace, Codepoint) == 0)
				++Missing;
		}
		return Missing;
	}

	// 回退路径专用：找不到时只返回 false，不写错误日志。
	bool TrySetIconFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(Face == nullptr)
			return false;
		m_IconRegularFace = Face;
		m_IconFace = Face;
		return true;
	}

	bool TrySetIconBoldFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(Face == nullptr)
			return false;
		m_IconBoldFace = Face;
		return true;
	}

	bool AddFallbackFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(!Face)
		{
			log_error("textrender", "The fallback font face '%s' could not be found", pFamilyName);
			return false;
		}
		if(std::find(m_vFallbackFaces.begin(), m_vFallbackFaces.end(), Face) != m_vFallbackFaces.end())
		{
			log_warn("textrender", "The fallback font face '%s' was specified multiple times", pFamilyName);
			return true;
		}
		// 回退链变化同样会改变 GetCharGlyph 的解析结果，近期索引必须失效。
		m_GlyphLookupCache.Reset();
		m_vFallbackFaces.push_back(Face);
		return true;
	}

	bool SetVariantFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(m_VariantFace != Face)
		{
			m_VariantFace = Face;
			Clear(); // rebuild atlas after changing variant font
			if(!Face && pFamilyName != nullptr)
			{
				log_error("textrender", "The variant font face '%s' could not be found", pFamilyName);
				return false;
			}
		}
		return true;
	}

	// QmClient: 分类字体面（中文/图标符号）。空名字表示清除该分类设置、跟随主链；
	// 名字非空但找不到对应 face 时返回 false，由调用方记录日志。
	bool SetCjkFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(!Face && pFamilyName != nullptr && pFamilyName[0] != '\0')
			return false;
		if(m_QmCjkFace != Face)
		{
			// 分类变化会改变 GetCharGlyph 的解析结果，近期索引必须失效。
			m_GlyphLookupCache.Reset();
			m_QmCjkFace = Face;
		}
		return true;
	}

	bool SetIconsFaceByName(const char *pFamilyName)
	{
		FT_Face Face = GetFaceByName(pFamilyName);
		if(!Face && pFamilyName != nullptr && pFamilyName[0] != '\0')
			return false;
		if(m_QmIconsFace != Face)
		{
			m_GlyphLookupCache.Reset();
			m_QmIconsFace = Face;
		}
		return true;
	}

	void SetFontPreset(EFontPreset FontPreset)
	{
		switch(FontPreset)
		{
		case EFontPreset::DEFAULT_FONT:
			m_SelectedFace = nullptr;
			break;
		case EFontPreset::ICON_FONT:
			m_SelectedFace = m_IconFace;
			break;
		case EFontPreset::ICON_FONT_BOLD:
			m_SelectedFace = m_IconBoldFace;
			break;
		}
	}

	bool TrySetIconStyleFaceByName(const char *pFamilyName, FT_Face &Face)
	{
		Face = GetFaceByName(pFamilyName);
		return Face != nullptr;
	}

	void SetIconStyleFacesByName()
	{
		TrySetIconStyleFaceByName("Phosphor-Light", m_IconLightFace);
		TrySetIconStyleFaceByName("Phosphor-Fill", m_IconFillFace);
		TrySetIconStyleFaceByName("Phosphor-Duotone", m_IconDuotoneFace);
	}

	void SetIconFontWeight(const int Weight)
	{
		// 与图集侧保持相同的旧配置归一化：非法值按 Bold 处理，避免
		// MTSDF 已切到 Bold 而字体回退仍落到 Regular。
		const int NormalizedWeight = Weight >= 0 && Weight <= 5 ? Weight : 1;
		FT_Face Face = m_IconRegularFace;
		switch(NormalizedWeight)
		{
		case 1: Face = m_IconBoldFace; break;
		case 2:
		case 4: Face = m_IconLightFace; break;
		case 3: Face = m_IconFillFace; break;
		case 5: Face = m_IconDuotoneFace; break;
		default: break;
		}
		if(Face == nullptr)
			Face = m_IconRegularFace;
		if(m_IconFace != Face)
		{
			m_IconFace = Face;
			m_GlyphLookupCache.Reset();
		}
	}

	// 拿到（并按需初始化）某个 face 的可变字重轴缓存：fvar 解析开销大，每 face 只做一次。
	SQmFaceWeightState &QmEnsureFaceWeightState(FT_Face Face)
	{
		SQmFaceWeightState &State = m_FaceWeightStates[Face];
		if(!State.m_Initialized)
		{
			State.m_Initialized = true;
			FT_MM_Var *pMaster = nullptr;
			if(FT_Get_MM_Var(Face, &pMaster) == 0 && pMaster != nullptr)
			{
				State.m_vAppliedCoords.resize(pMaster->num_axis);
				for(FT_UInt AxisIndex = 0; AxisIndex < pMaster->num_axis; ++AxisIndex)
				{
					const FT_Var_Axis &Axis = pMaster->axis[AxisIndex];
					State.m_vAppliedCoords[AxisIndex] = Axis.def;
					if(Axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
					{
						State.m_HasWeightAxis = true;
						State.m_WeightAxisIndex = static_cast<int>(AxisIndex);
						State.m_WeightMin = Axis.minimum;
						State.m_WeightMax = Axis.maximum;
					}
				}
				FT_Done_MM_Var(m_FTLibrary, pMaster);
			}
		}
		return State;
	}

	void ApplyCustomFontWeight()
	{
		bool WeightCoordsChanged = false;
		for(FT_Face Face : m_vFtFaces)
		{
			SQmFaceWeightState &State = QmEnsureFaceWeightState(Face);
			if(!State.m_HasWeightAxis)
				continue;
			// 分类字重：中文/中日韩面使用独立字重；与默认面重合（同一族同一 face）
			// 时以拉丁字重为准——同一物理 face 无法承载两套可变坐标。
			const int TargetWeight = (Face == m_QmCjkFace && Face != m_DefaultFace) ? m_QmCjkCustomFontWeight : m_CustomFontWeight;
			// 字重设置可能已变化：基于缓存的轴范围重算期望坐标，只在真正变化时写入。
			const FT_Fixed Requested = static_cast<FT_Fixed>(TargetWeight * 65536);
			const FT_Fixed Clamped = std::clamp(Requested, State.m_WeightMin, State.m_WeightMax);
			if(State.m_vAppliedCoords[State.m_WeightAxisIndex] == Clamped)
				continue;
			State.m_vAppliedCoords[State.m_WeightAxisIndex] = Clamped;
			FT_Set_Var_Design_Coordinates(Face, static_cast<FT_UInt>(State.m_vAppliedCoords.size()), State.m_vAppliedCoords.data());
			WeightCoordsChanged = true;
		}
		// 只有字重坐标真正变化时才重建字形图集；单纯切换字体面不应清空图集，
		// 否则同一帧内已排版的文本会采样到被清空的纹理而全部消失。
		if(WeightCoordsChanged)
			Clear();
	}

	void SetCustomFontWeight(const int Weight)
	{
		// 只防垃圾值：真实生效范围由 ApplyCustomFontWeight 按各 face 的 wght 轴
		// 范围钳制（可变字体轴上限可达 1000，不能在此写死 900）。
		const int ClampedWeight = std::clamp(Weight, 1, 5000);
		if(m_CustomFontWeight == ClampedWeight)
			return;
		m_CustomFontWeight = ClampedWeight;
		ApplyCustomFontWeight();
	}

	void SetCustomFontWeightCjk(const int Weight)
	{
		const int ClampedWeight = std::clamp(Weight, 1, 5000);
		if(m_QmCjkCustomFontWeight == ClampedWeight)
			return;
		m_QmCjkCustomFontWeight = ClampedWeight;
		ApplyCustomFontWeight();
	}

	// 可变字重轴范围（整数字重 100~900 语义）；非可变字体返回 false。
	bool CustomFontWeightRange(const char *pFace, int &Min, int &Max)
	{
		FT_Face Resolved = GetFaceByName(pFace);
		if(Resolved == nullptr)
			return false;
		const SQmFaceWeightState &State = QmEnsureFaceWeightState(Resolved);
		if(!State.m_HasWeightAxis)
			return false;
		Min = static_cast<int>((State.m_WeightMin + 32768) / 65536);
		Max = static_cast<int>((State.m_WeightMax + 32768) / 65536);
		return true;
	}

	bool CustomFontHasVariableWeight(const char *pFace) const
	{
		// 复用按 face 缓存的轴状态，避免每次调用都做 FT_Get_MM_Var。
		if(pFace == nullptr)
			return false;
		int Min = 0, Max = 0;
		return const_cast<CGlyphMap *>(this)->CustomFontWeightRange(pFace, Min, Max);
	}

	bool IsIconFaceSelected() const
	{
		return m_SelectedFace == m_IconFace || m_SelectedFace == m_IconBoldFace;
	}

	void Clear()
	{
		for(size_t TextureIndex = 0; TextureIndex < NUM_FONT_TEXTURES; ++TextureIndex)
		{
			mem_zero(m_apTextureData[TextureIndex], m_TextureDimension * m_TextureDimension * sizeof(uint8_t));
			Graphics()->UpdateTextTexture(m_aTextures[TextureIndex], 0, 0, m_TextureDimension, m_TextureDimension, m_apTextureData[TextureIndex], false);
		}

		m_TextureAtlas.Clear(m_TextureDimension);
		m_Glyphs.clear();
		m_GlyphLookupCache.Reset();
		InvalidateFacePixelSizeCache();
		// QmClient: Clear 已整张重传纹理，积累的脏区随之失效。
		DiscardPendingGlyphUploads();
	}

	// QmClient: 记录/消费“最近缺失字形”。预热集合与缓存无关，字体图集重建（语言切换）
	// 后正好需要重新预热，因此这里不清空集合。
	static uint64_t QmRecentGlyphMissKey(int Chr, int FontSize)
	{
		return ((uint64_t)(uint32_t)Chr << 32) | (uint32_t)FontSize;
	}

	void QmRecordRecentGlyphMiss(int Chr, int FontSize)
	{
		if(Chr <= 0 || m_vQmRecentGlyphMisses.size() >= QM_MAX_RECENT_GLYPH_MISSES)
			return;
		if(m_QmRecentGlyphMissKeys.insert(QmRecentGlyphMissKey(Chr, FontSize)).second)
			m_vQmRecentGlyphMisses.emplace_back(Chr, FontSize);
	}

	size_t QmRecentGlyphMissCount() const
	{
		return m_vQmRecentGlyphMisses.size();
	}

	const std::vector<std::pair<int, int>> &QmRecentGlyphMisses() const
	{
		return m_vQmRecentGlyphMisses;
	}

	// 按游标预热缺失字形（集合保留，供持久化）；已缓存时 GetGlyph 几乎零成本。
	int QmPrewarmRecentGlyphMisses(int MaxCount)
	{
		const bool PreviousRecording = m_QmRecordGlyphMisses;
		m_QmRecordGlyphMisses = false;
		int Prewarmed = 0;
		while(Prewarmed < MaxCount && m_QmRecentGlyphPrewarmCursor < m_vQmRecentGlyphMisses.size())
		{
			const std::pair<int, int> Miss = m_vQmRecentGlyphMisses[m_QmRecentGlyphPrewarmCursor++];
			if(GetGlyph(Miss.first, Miss.second) != nullptr)
				++Prewarmed;
		}
		m_QmRecordGlyphMisses = PreviousRecording;
		return Prewarmed;
	}

	const SGlyph *GetGlyph(int Chr, int FontSize)
	{
		FontSize = std::clamp(FontSize, MIN_FONT_SIZE, MAX_FONT_SIZE);

		// 命中近期索引时直接返回，省掉一次哈希查找；索引与字形表同生共死。
		if(const SGlyph *pCached = m_GlyphLookupCache.Find(m_SelectedFace, Chr, FontSize))
			return pCached;
		const auto RememberGlyph = [&](const SGlyph *pGlyph) {
			m_GlyphLookupCache.Store(m_SelectedFace, Chr, FontSize, pGlyph);
			return pGlyph;
		};

		// Find glyph index and most appropriate font face.
		FT_Face Face;
		FT_UInt GlyphIndex = GetCharGlyph(Chr, &Face, false);
		if(GlyphIndex == 0)
		{
			// Use replacement character if glyph could not be found,
			// also retrieve replacement character from the atlas.
			return RememberGlyph(Chr == REPLACEMENT_CHARACTER ? nullptr : GetGlyph(REPLACEMENT_CHARACTER, FontSize));
		}

		// Check if glyph for this (font face, character, font size)-combination was already rendered.
		SGlyph &Glyph = m_Glyphs[std::make_tuple(Face, Chr, FontSize)];
		if(Glyph.m_State == SGlyph::EState::RENDERED)
			return RememberGlyph(&Glyph);
		else if(Glyph.m_State == SGlyph::EState::ERROR)
			return nullptr;

		// Else, render it.
		if(m_QmRecordGlyphMisses)
			QmRecordRecentGlyphMiss(Chr, FontSize);
		Glyph.m_FontSize = FontSize;
		Glyph.m_Face = Face;
		Glyph.m_Chr = Chr;
		Glyph.m_GlyphIndex = GlyphIndex;

		const bool Rendered = RenderGlyph(Glyph);
		if(Rendered)
			return RememberGlyph(&Glyph);

		// Use replacement character if the glyph could not be rendered,
		// also retrieve replacement character from the atlas.
		const SGlyph *pReplacementCharacter = Chr == REPLACEMENT_CHARACTER ? nullptr : GetGlyph(REPLACEMENT_CHARACTER, FontSize);
		if(pReplacementCharacter)
		{
			Glyph = *pReplacementCharacter;
			return RememberGlyph(&Glyph);
		}

		// Keep failed glyph in the cache so we don't attempt to render it again,
		// but set its state to ERROR so we don't return it to the text render.
		Glyph.m_State = SGlyph::EState::ERROR;
		return nullptr;
	}

	vec2 Kerning(const SGlyph *pLeft, const SGlyph *pRight) const
	{
		if(pLeft != nullptr && pRight != nullptr && pLeft->m_Face == pRight->m_Face && pLeft->m_FontSize == pRight->m_FontSize)
		{
			FT_Vector Kerning = {0, 0};
			EnsureFacePixelSize(pLeft->m_Face, pLeft->m_FontSize);
			FT_Get_Kerning(pLeft->m_Face, pLeft->m_Chr, pRight->m_Chr, FT_KERNING_DEFAULT, &Kerning);
			return vec2(Kerning.x >> 6, Kerning.y >> 6);
		}
		return vec2(0.0f, 0.0f);
	}

	void UploadEntityLayerText(const CImageInfo &TextImage, int TexSubWidth, int TexSubHeight, const char *pText, int Length, float x, float y, int FontSize)
	{
		if(FontSize < 1)
			return;

		const size_t PixelSize = TextImage.PixelSize();
		const char *pCurrent = pText;
		const char *pEnd = pCurrent + Length;
		int WidthLastChars = 0;

		while(pCurrent < pEnd)
		{
			const char *pTmp = pCurrent;
			const int NextCharacter = str_utf8_decode(&pTmp);

			if(NextCharacter)
			{
				FT_Face Face;
				FT_UInt GlyphIndex = GetCharGlyph(NextCharacter, &Face, true);
				if(GlyphIndex == 0)
				{
					pCurrent = pTmp;
					continue;
				}

				EnsureFacePixelSize(Face, FontSize);
				if(FT_Load_Char(Face, NextCharacter, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP))
				{
					log_debug("textrender", "Error loading glyph. Chr=%d GlyphIndex=%u", NextCharacter, GlyphIndex);
					pCurrent = pTmp;
					continue;
				}

				const FT_Bitmap *pBitmap = &Face->glyph->bitmap;
				if(pBitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
				{
					log_debug("textrender", "Error loading glyph, unsupported pixel mode. Chr=%d GlyphIndex=%u PixelMode=%d", NextCharacter, GlyphIndex, pBitmap->pixel_mode);
					pCurrent = pTmp;
					continue;
				}

				for(unsigned OffY = 0; OffY < pBitmap->rows; ++OffY)
				{
					for(unsigned OffX = 0; OffX < pBitmap->width; ++OffX)
					{
						const int ImgOffX = std::clamp(x + OffX + WidthLastChars, x, (x + TexSubWidth) - 1);
						const int ImgOffY = std::clamp(y + OffY, y, (y + TexSubHeight) - 1);
						const size_t ImageOffset = ImgOffY * (TextImage.m_Width * PixelSize) + ImgOffX * PixelSize;
						for(size_t i = 0; i < PixelSize - 1; ++i)
						{
							TextImage.m_pData[ImageOffset + i] = 255;
						}
						TextImage.m_pData[ImageOffset + PixelSize - 1] = pBitmap->buffer[OffY * pBitmap->width + OffX];
					}
				}

				WidthLastChars += (pBitmap->width + 1);
			}
			pCurrent = pTmp;
		}
	}

	size_t TextureDimension() const
	{
		return m_TextureDimension;
	}

	IGraphics::CTextureHandle Texture(size_t TextureIndex) const
	{
		return m_aTextures[TextureIndex];
	}

	void ConsumeQmPerfGlyphStats(int &GlyphNew, int &GlyphUploads, double &GlyphRasterizeMs, double &GlyphUploadMs, int &GlyphUploadBatches, double &GlyphUploadBatchMs)
	{
		GlyphNew = m_QmPerfGlyphNew;
		GlyphUploads = m_QmPerfGlyphUploads;
		GlyphRasterizeMs = m_QmPerfGlyphRasterizeMs;
		GlyphUploadMs = m_QmPerfGlyphUploadMs;
		GlyphUploadBatches = m_QmPerfGlyphUploadBatches;
		GlyphUploadBatchMs = m_QmPerfGlyphUploadBatchMs;
		m_QmPerfGlyphNew = 0;
		m_QmPerfGlyphUploads = 0;
		m_QmPerfGlyphRasterizeMs = 0.0;
		m_QmPerfGlyphUploadMs = 0.0;
		m_QmPerfGlyphUploadBatches = 0;
		m_QmPerfGlyphUploadBatchMs = 0.0;
	}
};

typedef vector4_base<unsigned char> STextCharQuadVertexColor;

struct STextCharQuadVertex
{
	STextCharQuadVertex()
	{
		m_Color.r = m_Color.g = m_Color.b = m_Color.a = 255;
	}
	float m_X, m_Y;
	// do not use normalized floats as coordinates, since the texture might grow
	float m_U, m_V;
	STextCharQuadVertexColor m_Color;
};

struct STextCharQuad
{
	STextCharQuadVertex m_aVertices[4];
};

struct SStringInfo
{
	int m_QuadBufferObjectIndex;
	int m_QuadBufferContainerIndex;
	int m_SelectionQuadContainerIndex;

	std::vector<STextCharQuad> m_vCharacterQuads;
};

struct STextContainer
{
	STextContainer()
	{
		Reset();
	}

	SStringInfo m_StringInfo;

	// keep these values to calculate offsets
	float m_AlignedStartX;
	float m_AlignedStartY;
	float m_X;
	float m_Y;

	int m_Flags;
	int m_LineCount;
	int m_GlyphCount;
	int m_CharCount;
	int m_MaxLines;
	float m_LineWidth;

	unsigned m_RenderFlags;

	bool m_HasCursor;
	bool m_ForceCursorRendering;
	bool m_HasSelection;

	bool m_SingleTimeUse;

	STextBoundingBox m_BoundingBox;

	// prefix of the container's text stored for debugging purposes
	char m_aDebugText[32];

	std::shared_ptr<STextContainerUsages> m_pContainerUseCount;

	void Reset()
	{
		m_StringInfo.m_QuadBufferObjectIndex = m_StringInfo.m_QuadBufferContainerIndex = m_StringInfo.m_SelectionQuadContainerIndex = -1;
		m_StringInfo.m_vCharacterQuads.clear();

		m_AlignedStartX = m_AlignedStartY = m_X = m_Y = 0.0f;
		m_Flags = m_LineCount = m_CharCount = m_GlyphCount = 0;
		m_MaxLines = -1;
		m_LineWidth = -1.0f;

		m_RenderFlags = 0;

		m_HasCursor = false;
		m_ForceCursorRendering = false;
		m_HasSelection = false;

		m_SingleTimeUse = false;

		m_BoundingBox = {0.0f, 0.0f, 0.0f, 0.0f};

		m_aDebugText[0] = '\0';

		m_pContainerUseCount.reset();
	}
};

float CTextCursor::Height() const
{
	return m_LineCount * (m_AlignedFontSize + m_AlignedLineSpacing);
}

STextBoundingBox CTextCursor::BoundingBox() const
{
	return {m_StartX, m_StartY, m_LongestLineWidth, Height()};
}

void CTextCursor::SetPosition(vec2 Position)
{
	m_StartX = Position.x;
	m_StartY = Position.y;
	m_X = Position.x;
	m_Y = Position.y;
}

struct SFontLanguageVariant
{
	char m_aLanguageFile[IO_MAX_PATH_LENGTH];
	char m_aFamilyName[FONT_NAME_SIZE];
};

class CTextRender : public IEngineTextRender
{
	IConsole *m_pConsole;
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	IConsole *Console() { return m_pConsole; }
	IGraphics *Graphics() { return m_pGraphics; }
	IStorage *Storage() { return m_pStorage; }

	CGlyphMap *m_pGlyphMap;
	std::vector<void *> m_vpFontData;

	std::vector<SFontLanguageVariant> m_vVariants;

	unsigned m_RenderFlags;

	ColorRGBA m_Color;
	ColorRGBA m_UnmodifiedColor;
	ColorRGBA m_OutlineColor;
	ColorRGBA m_SelectionColor;
	EFontPreset m_FontPreset = EFontPreset::DEFAULT_FONT;

	ColorRGBA ResolveFontPresetColor(ColorRGBA Color) const
	{
		// Font selection must not overwrite semantic colors. Qm UI controls that
		// opt into the White/Black preference pass that color explicitly.
		return Color;
	}

	FT_Library m_FTLibrary;

	std::vector<STextContainer *> m_vpTextContainers;
	std::vector<int> m_vTextContainerIndices;
	int m_FirstFreeTextContainerIndex;

	SBufferContainerInfo m_DefaultTextContainerInfo;

	std::chrono::nanoseconds m_CursorRenderTime;
	int m_QmPerfTextContainerNew = 0;
	int m_QmPerfTextContainerUploads = 0;
	double m_QmPerfTextContainerCreateMs = 0.0;
	double m_QmPerfTextContainerUploadMs = 0.0;
	SQmTextRuntimeBudgetSnapshot m_QmLastTextRuntimeBudgetSnapshot;

	// QmClient: 单帧文本统计（QmTextFrameEnd 时消费并按阈值打日志）。
	int m_QmFrameContainerCreates = 0;
	int m_QmPeakFrameContainerCreates = 0;
	double m_QmPeakFrameRasterizeMs = 0.0;
	int m_QmPeakFrameGlyphNew = 0;

	// TClient
	std::vector<std::string> m_CustomFontFaces;
	std::vector<std::string> m_CustomFontStyles;
	// 字体商店预览缓存：已通过 QmEnsurePreviewFace 加载过的文件路径（face 池内
	// 的预览面与 face 同生命周期，会话内不卸载，避免字形图集残留悬空 face 指针）。
	std::vector<std::string> m_vQmPreviewLoadedPaths;
	std::vector<std::string> m_DefaultFontFaces;
	// QmClient: 已加载的用户字体文件路径（重扫时跳过）与 CheckDefaultFaces 防重入标记。
	std::vector<std::string> m_vLoadedCustomFontPaths;
	bool m_QmDefaultFacesChecked = false;

	void ResetQmTextRuntimeBudgetCounters(bool ConsumeGlyphStats)
	{
		if(ConsumeGlyphStats && m_pGlyphMap != nullptr)
		{
			int GlyphNew = 0;
			int GlyphUploads = 0;
			double GlyphRasterizeMs = 0.0;
			double GlyphUploadMs = 0.0;
			int GlyphUploadBatches = 0;
			double GlyphUploadBatchMs = 0.0;
			m_pGlyphMap->ConsumeQmPerfGlyphStats(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs, GlyphUploadBatches, GlyphUploadBatchMs);
		}
		m_QmPerfTextContainerNew = 0;
		m_QmPerfTextContainerUploads = 0;
		m_QmPerfTextContainerCreateMs = 0.0;
		m_QmPerfTextContainerUploadMs = 0.0;
	}

	bool ShouldLogTextRuntimeBudget(int GlyphNew, int GlyphUploads, double GlyphRasterizeMs, double GlyphUploadMs) const
	{
		const int TextContainerWork = m_QmPerfTextContainerNew + m_QmPerfTextContainerUploads;
		if(GlyphNew > 0 || GlyphUploads > 0 || m_QmPerfTextContainerUploads > 0)
			return true;
		if(GlyphRasterizeMs >= QmPerfThresholdMs() || GlyphUploadMs >= QmPerfThresholdMs() ||
			m_QmPerfTextContainerCreateMs >= QmPerfThresholdMs() || m_QmPerfTextContainerUploadMs >= QmPerfThresholdMs())
			return true;
		return TextContainerWork >= 8;
	}

	void UpdateQmTextRuntimeBudgetSnapshot(int GlyphNew, int GlyphUploads, double GlyphRasterizeMs, double GlyphUploadMs)
	{
		m_QmLastTextRuntimeBudgetSnapshot.m_GlyphNew = GlyphNew;
		m_QmLastTextRuntimeBudgetSnapshot.m_GlyphUploads = GlyphUploads;
		m_QmLastTextRuntimeBudgetSnapshot.m_GlyphRasterizeMs = GlyphRasterizeMs;
		m_QmLastTextRuntimeBudgetSnapshot.m_GlyphUploadMs = GlyphUploadMs;
		m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerNew = m_QmPerfTextContainerNew;
		m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerUploads = m_QmPerfTextContainerUploads;
		m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerCreateMs = m_QmPerfTextContainerCreateMs;
		m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerUploadMs = m_QmPerfTextContainerUploadMs;
		++m_QmLastTextRuntimeBudgetSnapshot.m_Frame;
	}

	void FlushQmTextRuntimeBudgetLog() override
	{
		if(m_pGlyphMap == nullptr)
		{
			ResetQmTextRuntimeBudgetCounters(false);
			return;
		}
		int GlyphNew = 0;
		int GlyphUploads = 0;
		double GlyphRasterizeMs = 0.0;
		double GlyphUploadMs = 0.0;
		int GlyphUploadBatches = 0;
		double GlyphUploadBatchMs = 0.0;
		m_pGlyphMap->ConsumeQmPerfGlyphStats(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs, GlyphUploadBatches, GlyphUploadBatchMs);
		UpdateQmTextRuntimeBudgetSnapshot(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs);
		if(!QmPerfEnabled())
		{
			ResetQmTextRuntimeBudgetCounters(false);
			return;
		}
		if(!ShouldLogTextRuntimeBudget(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs))
		{
			ResetQmTextRuntimeBudgetCounters(false);
			return;
		}
		char aPayload[448];
		str_format(aPayload, sizeof(aPayload),
			"event=text_runtime_budget glyph_new=%d glyph_uploads=%d glyph_upload_batches=%d glyph_rasterize_ms=%.3f glyph_upload_ms=%.3f glyph_upload_batch_ms=%.3f text_container_new=%d text_container_uploads=%d text_container_create_ms=%.3f text_container_upload_ms=%.3f",
			GlyphNew, GlyphUploads, GlyphUploadBatches, GlyphRasterizeMs, GlyphUploadMs, GlyphUploadBatchMs, m_QmPerfTextContainerNew, m_QmPerfTextContainerUploads, m_QmPerfTextContainerCreateMs, m_QmPerfTextContainerUploadMs);
		QmPerfLogPayload("perf/text", aPayload);
		ResetQmTextRuntimeBudgetCounters(false);
	}

	SQmTextRuntimeBudgetSnapshot QmTextRuntimeBudgetSnapshot() const override
	{
		return m_QmLastTextRuntimeBudgetSnapshot;
	}

	// QmClient: 每渲染帧末尾由主循环调用。消费本帧的字形统计，容器创建数
	// 由 CreateTextContainer 递增。单帧超过阈值时打 text_frame_stats 日志，
	// 用于定位"进服后前几下卡顿"这类单帧文本渲染尖峰。
	void QmTextFrameEnd() override
	{
		if(m_pGlyphMap == nullptr)
			return;

		// QmClient: 帧末兜底提交——本帧只构建未渲染时（如菜单预构建）脏区
		// 不残留到下一帧；先 flush 再消费统计，帧成本统计包含合批提交。
		m_pGlyphMap->FlushPendingGlyphUploads();

		int FrameGlyphNew = 0;
		int FrameGlyphUploads = 0;
		double FrameRasterizeMs = 0.0;
		double FrameUploadMs = 0.0;
		int FrameUploadBatches = 0;
		double FrameUploadBatchMs = 0.0;
		m_pGlyphMap->ConsumeQmPerfGlyphStats(FrameGlyphNew, FrameGlyphUploads, FrameRasterizeMs, FrameUploadMs, FrameUploadBatches, FrameUploadBatchMs);
		constexpr int ContainerCreatesThreshold = 256;
		constexpr double RasterizeThresholdMs = 4.0;
		constexpr int GlyphNewThreshold = 96;

		if(FrameRasterizeMs > m_QmPeakFrameRasterizeMs)
			m_QmPeakFrameRasterizeMs = FrameRasterizeMs;
		if(m_QmFrameContainerCreates > m_QmPeakFrameContainerCreates)
			m_QmPeakFrameContainerCreates = m_QmFrameContainerCreates;
		if(FrameGlyphNew > m_QmPeakFrameGlyphNew)
			m_QmPeakFrameGlyphNew = FrameGlyphNew;

		if(QmPerfEnabled() && (m_QmFrameContainerCreates >= ContainerCreatesThreshold ||
					      FrameRasterizeMs >= RasterizeThresholdMs || FrameGlyphNew >= GlyphNewThreshold))
		{
			char aPayload[288];
			str_format(aPayload, sizeof(aPayload),
				"event=text_frame_stats container_creates=%d glyph_new=%d glyph_uploads=%d glyph_upload_batches=%d glyph_rasterize_ms=%.3f glyph_upload_batch_ms=%.3f peak_creates=%d peak_rasterize_ms=%.3f peak_glyph_new=%d",
				m_QmFrameContainerCreates, FrameGlyphNew, FrameGlyphUploads, FrameUploadBatches, FrameRasterizeMs, FrameUploadBatchMs,
				m_QmPeakFrameContainerCreates, m_QmPeakFrameRasterizeMs, m_QmPeakFrameGlyphNew);
			QmPerfLogPayload("perf/text", aPayload);
		}

		m_QmFrameContainerCreates = 0;
	}

	// QmClient: 预热单个字形。命中缓存时零成本；未命中则光栅化并写入图集。
	// 供菜单在构建文本容器前分帧调用（字形光栅化幂等可中断，文本容器构建不可）。
	void QmPrewarmGlyph(int Chr, int FontSize) override
	{
		if(m_pGlyphMap == nullptr || Chr <= 0)
			return;
		m_pGlyphMap->GetGlyph(Chr, FontSize);
	}

	// QmClient: 空闲帧预热“最近缺失字形”，返回本次实际预热数量。
	int QmPrewarmRecentGlyphs(int MaxCount) override
	{
		if(m_pGlyphMap == nullptr || MaxCount <= 0)
			return 0;
		return m_pGlyphMap->QmPrewarmRecentGlyphMisses(MaxCount);
	}

	int QmRecentGlyphMissCount() const override
	{
		return m_pGlyphMap == nullptr ? 0 : (int)m_pGlyphMap->QmRecentGlyphMissCount();
	}

	// QmClient: 跨会话持久化缺失字形集合（文本格式，每行 "Chr Size"）。
	void QmLoadRecentGlyphs(IStorage *pStorage, const char *pPath) override
	{
		if(m_pGlyphMap == nullptr || pStorage == nullptr || pPath == nullptr)
			return;
		void *pData = nullptr;
		unsigned DataSize = 0;
		if(!pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pData, &DataSize) || pData == nullptr || DataSize == 0)
			return;
		const char *pCursor = (const char *)pData;
		const char *pEnd = pCursor + DataSize;
		while(pCursor < pEnd)
		{
			const char *pLineEnd = pCursor;
			while(pLineEnd < pEnd && *pLineEnd != '\n' && *pLineEnd != '\r')
				++pLineEnd;
			int Chr = -1;
			int Size = -1;
			bool HasSize = false;
			const char *pParse = pCursor;
			while(pParse < pLineEnd)
			{
				while(pParse < pLineEnd && (*pParse < '0' || *pParse > '9'))
					++pParse;
				int Value = 0;
				bool HasValue = false;
				while(pParse < pLineEnd && *pParse >= '0' && *pParse <= '9')
				{
					Value = Value * 10 + (*pParse - '0');
					++pParse;
					HasValue = true;
				}
				if(!HasValue)
					break;
				if(Chr < 0)
					Chr = Value;
				else
				{
					Size = Value;
					HasSize = true;
					break;
				}
			}
			if(Chr > 0 && HasSize && Size > 0)
				m_pGlyphMap->QmRecordRecentGlyphMiss(Chr, Size);
			pCursor = pLineEnd;
			while(pCursor < pEnd && (*pCursor == '\n' || *pCursor == '\r'))
				++pCursor;
		}
		free(pData);
	}

	void QmSaveRecentGlyphs(IStorage *pStorage, const char *pPath) override
	{
		if(m_pGlyphMap == nullptr || pStorage == nullptr || pPath == nullptr)
			return;
		const std::vector<std::pair<int, int>> &vMisses = m_pGlyphMap->QmRecentGlyphMisses();
		if(vMisses.empty())
			return;
		IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return;
		for(const std::pair<int, int> &Miss : vMisses)
		{
			char aBuf[48];
			str_format(aBuf, sizeof(aBuf), "%d %d\n", Miss.first, Miss.second);
			io_write(File, aBuf, str_length(aBuf));
		}
		io_close(File);
	}

	int GetFreeTextContainerIndex()
	{
		if(m_FirstFreeTextContainerIndex == -1)
		{
			const int Index = (int)m_vTextContainerIndices.size();
			m_vTextContainerIndices.push_back(Index);
			return Index;
		}
		else
		{
			const int Index = m_FirstFreeTextContainerIndex;
			m_FirstFreeTextContainerIndex = m_vTextContainerIndices[Index];
			m_vTextContainerIndices[Index] = Index;
			return Index;
		}
	}

	void FreeTextContainerIndex(STextContainerIndex &Index)
	{
		m_vTextContainerIndices[Index.m_Index] = m_FirstFreeTextContainerIndex;
		m_FirstFreeTextContainerIndex = Index.m_Index;
		Index.Reset();
	}

	void FreeTextContainer(STextContainerIndex &Index)
	{
		m_vpTextContainers[Index.m_Index]->Reset();
		FreeTextContainerIndex(Index);
	}

	STextContainer &GetTextContainer(const STextContainerIndex &Index)
	{
		dbg_assert(Index.Valid(), "Text container index was invalid.");
		if(Index.m_Index >= (int)m_vpTextContainers.size())
		{
			for(int i = 0; i < Index.m_Index + 1 - (int)m_vpTextContainers.size(); ++i)
				m_vpTextContainers.push_back(new STextContainer());
		}

		if(m_vpTextContainers[Index.m_Index]->m_pContainerUseCount.get() != Index.m_UseCount.get())
		{
			m_vpTextContainers[Index.m_Index]->m_pContainerUseCount = Index.m_UseCount;
		}
		return *m_vpTextContainers[Index.m_Index];
	}

	int WordLength(const char *pText) const
	{
		const char *pCursor = pText;
		while(true)
		{
			if(*pCursor == '\0')
				return pCursor - pText;
			if(*pCursor == '\n' || *pCursor == '\t' || *pCursor == ' ')
				return pCursor - pText + 1;
			str_utf8_decode(&pCursor);
		}
	}

	// pvLoadedFaces 可选：收集本次实际加载成功的 face（商店预览加载用它标记预览 face）。
	// QmLogLoadedFaces 可选：按需加载路径（商店预览/分类字体）传 false——这类加载完全由
	// UI 触发，一次可能批量加载几十个族，逐 face 的成功日志只保留给启动期静态加载
	// （fonts/ 与 qmclient/fonts），避免控制台刷屏；加载失败的错误日志不受影响。
	bool LoadFontCollection(const char *pFontName, const FT_Byte *pFontData, FT_Long FontDataSize, std::vector<FT_Face> *pvLoadedFaces = nullptr, bool QmLogLoadedFaces = true)
	{
		FT_Face FtFace;
		FT_Error CollectionLoadError = FT_New_Memory_Face(m_FTLibrary, pFontData, FontDataSize, -1, &FtFace);
		if(CollectionLoadError)
		{
			log_error("textrender", "Failed to load font file '%s': %s", pFontName, FT_Error_String(CollectionLoadError));
			return false;
		}

		const FT_Long NumFaces = FtFace->num_faces;
		FT_Done_Face(FtFace);

		bool LoadedAny = false;
		FT_Long LoadedFaces = 0;
		for(FT_Long FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			FT_Error FaceLoadError = FT_New_Memory_Face(m_FTLibrary, pFontData, FontDataSize, FaceIndex, &FtFace);
			if(FaceLoadError)
			{
				log_error("textrender", "Failed to load font face %ld from font file '%s': %s", FaceIndex, pFontName, FT_Error_String(FaceLoadError));
				FT_Done_Face(FtFace);
				continue;
			}

			m_pGlyphMap->AddFace(FtFace);
			if(pvLoadedFaces != nullptr)
				pvLoadedFaces->push_back(FtFace);
			++LoadedFaces;
			LoadedAny = true;
		}

		// 成功路径每文件只打一条汇总：逐 face 打点会随字体库增长刷屏
		// （单 ttc 可达 10 face），失败时仍有逐 face error 可查。
		if(QmLogLoadedFaces && LoadedFaces > 0)
			log_debug("textrender", "Loaded %ld font face(s) from '%s'", LoadedFaces, pFontName);

		if(!LoadedAny)
		{
			log_error("textrender", "Failed to load font file '%s': no font faces could be loaded", pFontName);
			return false;
		}

		return true;
	}

	void SetRenderFlags(unsigned Flags) override
	{
		m_RenderFlags = Flags;
	}

	unsigned GetRenderFlags() const override
	{
		return m_RenderFlags;
	}

public:
	CTextRender()
	{
		m_pConsole = nullptr;
		m_pGraphics = nullptr;
		m_pStorage = nullptr;
		m_pGlyphMap = nullptr;

		m_Color = DefaultTextColor();
		m_UnmodifiedColor = m_Color;
		m_OutlineColor = DefaultTextOutlineColor();
		m_SelectionColor = DefaultTextSelectionColor();

		m_FTLibrary = nullptr;

		m_RenderFlags = 0;
		m_CursorRenderTime = time_get_nanoseconds();
	}

	void Init() override
	{
		m_pConsole = Kernel()->RequestInterface<IConsole>();
		m_pGraphics = Kernel()->RequestInterface<IGraphics>();
		m_pStorage = Kernel()->RequestInterface<IStorage>();
		FT_Init_FreeType(&m_FTLibrary);
		m_pGlyphMap = new CGlyphMap(m_pGraphics, m_FTLibrary);

		// print freetype version
		{
			int LMajor, LMinor, LPatch;
			FT_Library_Version(m_FTLibrary, &LMajor, &LMinor, &LPatch);
			log_info("textrender", "Freetype version %d.%d.%d (compiled = %d.%d.%d)", LMajor, LMinor, LPatch, FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH);
		}

		m_FirstFreeTextContainerIndex = -1;

		m_DefaultTextContainerInfo.m_Stride = sizeof(STextCharQuadVertex);
		m_DefaultTextContainerInfo.m_VertBufferBindingIndex = -1;

		m_DefaultTextContainerInfo.m_vAttributes.emplace_back();
		SBufferContainerInfo::SAttribute *pAttr = &m_DefaultTextContainerInfo.m_vAttributes.back();
		pAttr->m_DataTypeCount = 2;
		pAttr->m_FuncType = 0;
		pAttr->m_Normalized = false;
		pAttr->m_pOffset = nullptr;
		pAttr->m_Type = GRAPHICS_TYPE_FLOAT;

		m_DefaultTextContainerInfo.m_vAttributes.emplace_back();
		pAttr = &m_DefaultTextContainerInfo.m_vAttributes.back();
		pAttr->m_DataTypeCount = 2;
		pAttr->m_FuncType = 0;
		pAttr->m_Normalized = false;
		pAttr->m_pOffset = (void *)(sizeof(float) * 2);
		pAttr->m_Type = GRAPHICS_TYPE_FLOAT;

		m_DefaultTextContainerInfo.m_vAttributes.emplace_back();
		pAttr = &m_DefaultTextContainerInfo.m_vAttributes.back();
		pAttr->m_DataTypeCount = 4;
		pAttr->m_FuncType = 0;
		pAttr->m_Normalized = true;
		pAttr->m_pOffset = (void *)(sizeof(float) * 2 + sizeof(float) * 2);
		pAttr->m_Type = GRAPHICS_TYPE_UNSIGNED_BYTE;
	}

	void Shutdown() override
	{
		for(auto *pTextCont : m_vpTextContainers)
			delete pTextCont;
		m_vpTextContainers.clear();

		delete m_pGlyphMap;
		m_pGlyphMap = nullptr;

		if(m_FTLibrary != nullptr)
			FT_Done_FreeType(m_FTLibrary);
		m_FTLibrary = nullptr;

		for(auto *pFontData : m_vpFontData)
			free(pFontData);
		m_vpFontData.clear();

		m_DefaultTextContainerInfo.m_vAttributes.clear();

		m_pConsole = nullptr;
		m_pGraphics = nullptr;
		m_pStorage = nullptr;
	}

	// TClient
	static int LaziestFileCallback(const char *pFilename, int IsDir, int StorageType, void *pUser)
	{
		std::vector<std::string> *pVector = static_cast<std::vector<std::string> *>(pUser);
		if(IsDir)
			return 0;
		pVector->emplace_back(pFilename);
		return 0;
	}
	// TClient：递归收集字体文件，保留目录名以支持 qmclient/fonts 下的字体包。
	struct SRecursiveFontScan
	{
		CTextRender *m_pThis;
		std::string m_Directory;
		std::vector<std::string> *m_pFiles;
	};
	static int RecursiveFontFileCallback(const char *pFilename, int IsDir, int StorageType, void *pUser)
	{
		auto *pScan = static_cast<SRecursiveFontScan *>(pUser);
		if(pFilename == nullptr || pFilename[0] == '.' || pScan == nullptr)
			return 0;
		char aRelativePath[IO_MAX_PATH_LENGTH];
		str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pScan->m_Directory.c_str(), pFilename);
		if(IsDir)
		{
			SRecursiveFontScan Child{pScan->m_pThis, aRelativePath, pScan->m_pFiles};
			pScan->m_pThis->Storage()->ListDirectory(IStorage::TYPE_ALL, aRelativePath, RecursiveFontFileCallback, &Child);
			return 0;
		}
		if(str_endswith_nocase(pFilename, ".ttf") != nullptr || str_endswith_nocase(pFilename, ".otf") != nullptr || str_endswith_nocase(pFilename, ".ttc") != nullptr)
			pScan->m_pFiles->emplace_back(aRelativePath);
		return 0;
	}
	// TClient
	void CheckDefaultFaces()
	{
		if(m_QmDefaultFacesChecked)
			return;
		m_QmDefaultFacesChecked = true;
		for(const auto &CurrentFace : *m_pGlyphMap->GetFaces())
		{
			char aFamilyStyle[256];
			const char *pFamily = CurrentFace->family_name != nullptr ? CurrentFace->family_name : "";
			const char *pStyle = CurrentFace->style_name != nullptr ? CurrentFace->style_name : "";
			if(pFamily[0] != '\0' && pStyle[0] != '\0' && str_comp_nocase(pStyle, "Regular") != 0)
				str_format(aFamilyStyle, sizeof(aFamilyStyle), "%s %s", pFamily, pStyle);
			else
				str_copy(aFamilyStyle, pFamily[0] != '\0' ? pFamily : FT_Get_Postscript_Name(CurrentFace));
			ReplaceHyphensWithSpaces(aFamilyStyle);
			m_DefaultFontFaces.emplace_back(aFamilyStyle);
		}
	}
	// TClient
	void UpdateCustomFontList()
	{
		std::vector<std::string> vAllFaces;
		for(const auto &CurrentFace : *m_pGlyphMap->GetFaces())
		{
			// 商店预览 face（未安装字体的临时加载面）不是已安装字体，
			// 不得进入字体族选择列表；用户真正安装后其文件会从字体目录
			// 加载出独立 face，正常出现在列表中。
			if(m_pGlyphMap->QmIsPreviewFace(CurrentFace))
				continue;
			char aFamilyStyle[256];
			const char *pFamily = CurrentFace->family_name != nullptr ? CurrentFace->family_name : "";
			const char *pStyle = CurrentFace->style_name != nullptr ? CurrentFace->style_name : "";
			if(pFamily[0] != '\0' && pStyle[0] != '\0' && str_comp_nocase(pStyle, "Regular") != 0)
				str_format(aFamilyStyle, sizeof(aFamilyStyle), "%s %s", pFamily, pStyle);
			else
				str_copy(aFamilyStyle, pFamily[0] != '\0' ? pFamily : FT_Get_Postscript_Name(CurrentFace));
			ReplaceHyphensWithSpaces(aFamilyStyle);
			std::string DisplayName = aFamilyStyle;
			// 下拉框按字体族展示，样式由族内的静态 face 或可变字重控制。
			for(const char *pSuffix : {" Regular", " Light", " Thin", " ExtraLight", " Medium", " SemiBold", " Bold", " ExtraBold", " Black", " Heavy"})
			{
				if(DisplayName.size() > std::strlen(pSuffix) && DisplayName.compare(DisplayName.size() - std::strlen(pSuffix), std::strlen(pSuffix), pSuffix) == 0)
				{
					DisplayName.erase(DisplayName.size() - std::strlen(pSuffix));
					break;
				}
			}
			vAllFaces.emplace_back(DisplayName);
		}

		m_CustomFontFaces.clear();
		m_CustomFontFaces.emplace_back("DejaVu Sans");
		for(const auto &Face : vAllFaces)
		{
			// 图标字体不作为正文字体候选。
			if(str_find_nocase(Face.c_str(), "Phosphor") != nullptr)
				continue;
			if(std::find_if(m_DefaultFontFaces.begin(), m_DefaultFontFaces.end(), [&Face](const std::string &DefaultFace) { return str_comp_nocase(DefaultFace.c_str(), Face.c_str()) == 0; }) != m_DefaultFontFaces.end())
				continue;
			if(std::find_if(m_CustomFontFaces.begin(), m_CustomFontFaces.end(), [&Face](const std::string &Existing) { return str_comp_nocase(Existing.c_str(), Face.c_str()) == 0; }) == m_CustomFontFaces.end())
				m_CustomFontFaces.push_back(Face);
		}
	}

	void UpdateCustomFontStyles(const char *pFamily)
	{
		m_CustomFontStyles.clear();
		if(pFamily == nullptr || pFamily[0] == '\0')
			return;
		const char *pTargetFamily = pFamily;
		for(const auto &CurrentFace : *m_pGlyphMap->GetFaces())
		{
			if(CurrentFace->family_name == nullptr || CurrentFace->style_name == nullptr)
				continue;
			char aFamilyStyle[FONT_NAME_SIZE];
			str_format(aFamilyStyle, sizeof(aFamilyStyle), "%s %s", CurrentFace->family_name, CurrentFace->style_name);
			if(str_comp_nocase(aFamilyStyle, pFamily) == 0)
			{
				pTargetFamily = CurrentFace->family_name;
				break;
			}
		}
		for(const auto &CurrentFace : *m_pGlyphMap->GetFaces())
		{
			if(CurrentFace->family_name == nullptr || str_comp_nocase(CurrentFace->family_name, pTargetFamily) != 0)
				continue;
			char aName[FONT_NAME_SIZE];
			str_format(aName, sizeof(aName), "%s %s", CurrentFace->family_name, CurrentFace->style_name != nullptr ? CurrentFace->style_name : "Regular");
			if(std::find_if(m_CustomFontStyles.begin(), m_CustomFontStyles.end(), [&aName](const std::string &Existing) { return str_comp_nocase(Existing.c_str(), aName) == 0; }) == m_CustomFontStyles.end())
				m_CustomFontStyles.emplace_back(aName);
		}
		std::sort(m_CustomFontStyles.begin(), m_CustomFontStyles.end());
	}
	// TClient
	// 随包图标字体首次使用时复制到用户目录，让 qmclient/fonts 自包含，用户无需手工放置。
	void InstallBundledIconFonts()
	{
		for(const char *pName : {"Phosphor/Phosphor-Regular.ttf", "Phosphor/Phosphor-Bold.ttf", "Phosphor/Phosphor-Light.ttf", "Phosphor/Phosphor-Fill.ttf", "Phosphor/Phosphor-Duotone.ttf"})
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "qmclient/fonts/%s", pName);
			if(Storage()->FileExists(aPath, IStorage::TYPE_SAVE))
				continue;
			void *pData = nullptr;
			unsigned DataSize = 0;
			if(!Storage()->ReadFile(aPath, IStorage::TYPE_ALL, &pData, &DataSize))
			{
				log_error("textrender", "Bundled icon font '%s' is missing", aPath);
				continue;
			}
			Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("qmclient/fonts/Phosphor", IStorage::TYPE_SAVE);
			IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File == nullptr)
			{
				log_error("textrender", "Failed to install bundled icon font '%s'", aPath);
			}
			else
			{
				const bool Written = io_write(File, pData, DataSize) == DataSize;
				io_close(File);
				if(Written)
					log_info("textrender", "Installed bundled icon font '%s'", aPath);
				else
					log_error("textrender", "Failed to write bundled icon font '%s'", aPath);
			}
			free(pData);
		}
	}

	// TClient
	void LoadCustomFonts()
	{
		CheckDefaultFaces();
		InstallBundledIconFonts();
		std::vector<std::string> vCustomFonts;
		SRecursiveFontScan Scan{this, "qmclient/fonts", &vCustomFonts};
		Storage()->ListDirectory(IStorage::TYPE_ALL, "qmclient/fonts", RecursiveFontFileCallback, &Scan);
		std::sort(vCustomFonts.begin(), vCustomFonts.end());
		for(const std::string &FilePath : vCustomFonts)
		{
			// 字体商店下载后会触发重扫：跳过已加载的文件，避免重复建 face 与重复占内存。
			const bool AlreadyLoaded = std::find_if(m_vLoadedCustomFontPaths.begin(), m_vLoadedCustomFontPaths.end(), [&FilePath](const std::string &Loaded) {
				return str_comp_nocase(Loaded.c_str(), FilePath.c_str()) == 0;
			}) != m_vLoadedCustomFontPaths.end();
			if(AlreadyLoaded)
				continue;
			void *pFontData;
			unsigned FontDataSize;
			if(Storage()->ReadFile(FilePath.c_str(), IStorage::TYPE_ALL, &pFontData, &FontDataSize))
			{
				if(LoadFontCollection(FilePath.c_str(), static_cast<FT_Byte *>(pFontData), (FT_Long)FontDataSize))
				{
					m_vpFontData.push_back(pFontData);
					m_vLoadedCustomFontPaths.push_back(FilePath);
				}
				else
				{
					free(pFontData);
				}
			}
			else
			{
				log_error("textrender", "Failed to open/read font file '%s'", FilePath.c_str());
			}
		}
		UpdateCustomFontList();
	}
	// TClient
	std::vector<std::string> *GetCustomFaces() override
	{
		return &m_CustomFontFaces;
	}

	std::vector<std::string> *GetCustomFontStyles(const char *pFamily) override
	{
		UpdateCustomFontStyles(pFamily);
		return &m_CustomFontStyles;
	}
	// TClient
	void SetCustomFace(const char *pFace) override
	{
		if(!m_pGlyphMap->TrySetDefaultFaceByName(pFace))
			log_info("textrender", "Configured custom font face '%s' is not bundled; using the default bundled face", pFace != nullptr ? pFace : "");
	}

	// QmClient: 分类字体（中文/图标符号）。找不到配置的面时保持回退链并记录日志。
	void SetCustomFaceCjk(const char *pFace) override
	{
		if(!m_pGlyphMap->SetCjkFaceByName(pFace))
			log_info("textrender", "Configured Chinese font face '%s' is not available; following the default chain", pFace != nullptr ? pFace : "");
	}

	void SetCustomFaceIcons(const char *pFace) override
	{
		if(!m_pGlyphMap->SetIconsFaceByName(pFace))
			log_info("textrender", "Configured icon/symbol font face '%s' is not available; following the default chain", pFace != nullptr ? pFace : "");
	}

	// QmClient: 字体商店下载完成后重扫用户字体目录，并按当前配置重新解析各字体面。
	void ReloadCustomFonts() override
	{
		LoadCustomFonts();
		m_pGlyphMap->TrySetDefaultFaceByName(g_Config.m_TcCustomFont);
		m_pGlyphMap->SetCjkFaceByName(g_Config.m_TcCustomFontCjk);
		m_pGlyphMap->SetIconsFaceByName(g_Config.m_TcCustomFontIcons);
		m_pGlyphMap->SetCustomFontWeight(g_Config.m_TcCustomFontWeight);
		m_pGlyphMap->SetCustomFontWeightCjk(g_Config.m_TcCustomFontWeightCjk);
	}

	void SetCustomFontWeight(const int Weight) override
	{
		m_pGlyphMap->SetCustomFontWeight(Weight);
	}

	void SetCustomFontWeightCjk(const int Weight) override
	{
		m_pGlyphMap->SetCustomFontWeightCjk(Weight);
	}

	bool CustomFontWeightRange(const char *pFace, int &Min, int &Max) override
	{
		return m_pGlyphMap->CustomFontWeightRange(pFace, Min, Max);
	}

	bool QmHasCustomFace(const char *pFace) override
	{
		return m_pGlyphMap->QmHasFace(pFace);
	}

	bool QmFaceHasCjk(const char *pFace) override
	{
		return m_pGlyphMap->QmFaceHasCjk(pFace);
	}

	// QmClient: 商店预览面。字体文件尚未安装时从预览缓存目录临时加载进 face 池
	// （不刷新字体族列表，不会出现在字体下拉框）；已加载直接返回 true。
	bool QmEnsurePreviewFace(const char *pFamily, const char *pFilePath) override
	{
		if(pFamily == nullptr || pFamily[0] == '\0' || pFilePath == nullptr || pFilePath[0] == '\0')
			return false;
		if(m_pGlyphMap->QmHasFace(pFamily))
			return true;
		const std::string Path(pFilePath);
		if(std::find_if(m_vQmPreviewLoadedPaths.begin(), m_vQmPreviewLoadedPaths.end(), [&Path](const std::string &Loaded) { return str_comp_nocase(Loaded.c_str(), Path.c_str()) == 0; }) != m_vQmPreviewLoadedPaths.end())
			return m_pGlyphMap->QmHasFace(pFamily);
		void *pFontData = nullptr;
		unsigned FontDataSize = 0;
		if(!Storage()->ReadFile(pFilePath, IStorage::TYPE_ALL, &pFontData, &FontDataSize))
			return false;
		std::vector<FT_Face> vLoadedFaces;
		// 预览加载不逐 face 打成功日志（QmLogLoadedFaces=false）：商店一次可能
		// 批量加载几十个族，刷屏无诊断价值；失败仍走错误日志。
		if(LoadFontCollection(pFilePath, static_cast<const FT_Byte *>(pFontData), (FT_Long)FontDataSize, &vLoadedFaces, false))
		{
			m_vpFontData.push_back(pFontData);
			m_vQmPreviewLoadedPaths.push_back(Path);
			// 标记为预览 face：只服务商店卡片渲染，不进字体族选择列表。
			for(FT_Face Face : vLoadedFaces)
				m_pGlyphMap->QmMarkPreviewFace(Face);
			// 新 face 可能是可变字体：把当前分类字重立即应用上去（坐标未变化时不会动图集）。
			m_pGlyphMap->SetCustomFontWeight(g_Config.m_TcCustomFontWeight);
			m_pGlyphMap->SetCustomFontWeightCjk(g_Config.m_TcCustomFontWeightCjk);
			return m_pGlyphMap->QmHasFace(pFamily);
		}
		free(pFontData);
		return false;
	}

	bool CustomFontHasVariableWeight(const char *pFace) const override
	{
		return m_pGlyphMap->CustomFontHasVariableWeight(pFace);
	}

	bool LoadFonts() override
	{
		// read file data into buffer
		const char *pFilename = "fonts/index.json";
		void *pFileData;
		unsigned JsonFileSize;
		if(!Storage()->ReadFile(pFilename, IStorage::TYPE_ALL, &pFileData, &JsonFileSize))
		{
			log_error("textrender", "Failed to open/read font index file '%s'", pFilename);
			return false;
		}

		// parse json data
		json_settings JsonSettings{};
		char aError[256];
		json_value *pJsonData = JsonParseEx(&JsonSettings, static_cast<const json_char *>(pFileData), JsonFileSize, aError);
		free(pFileData);
		if(pJsonData == nullptr)
		{
			log_error("textrender", "Failed to parse font index file '%s': %s", pFilename, aError);
			return false;
		}
		if(pJsonData->type != json_object)
		{
			log_error("textrender", "Font index malformed: root must be an object in file '%s'", pFilename);
			return false;
		}

		bool Success = true;

		// extract font file definitions
		const json_value &FontFiles = (*pJsonData)["font files"];
		if(FontFiles.type == json_array)
		{
			for(unsigned FontFileIndex = 0; FontFileIndex < FontFiles.u.array.length; ++FontFileIndex)
			{
				if(FontFiles[FontFileIndex].type != json_string)
				{
					log_error("textrender", "Font index malformed: 'font files' must be an array of strings (error at index %d)", FontFileIndex);
					Success = false;
					continue;
				}

				char aFontName[IO_MAX_PATH_LENGTH];
				str_format(aFontName, sizeof(aFontName), "fonts/%s", FontFiles[FontFileIndex].u.string.ptr);
				void *pFontData;
				unsigned FontDataSize;
				if(Storage()->ReadFile(aFontName, IStorage::TYPE_ALL, &pFontData, &FontDataSize))
				{
					if(LoadFontCollection(aFontName, static_cast<FT_Byte *>(pFontData), (FT_Long)FontDataSize))
					{
						m_vpFontData.push_back(pFontData);
					}
					else
					{
						free(pFontData);
					}
				}
				else
				{
					log_error("textrender", "Failed to open/read font file '%s'", aFontName);
					Success = false;
				}
			}
		}
		else
		{
			log_error("textrender", "Font index malformed: 'font files' must be an array");
			Success = false;
		}

		// extract default family name
		const json_value &DefaultFace = (*pJsonData)["default"];
		if(DefaultFace.type == json_string)
		{
			if(!m_pGlyphMap->SetDefaultFaceByName(DefaultFace.u.string.ptr))
			{
				Success = false;
			}
		}
		else
		{
			log_error("textrender", "Font index malformed: 'default' must be a string");
			Success = false;
		}
		// TClient
		LoadCustomFonts();
		m_pGlyphMap->SetCustomFontWeight(g_Config.m_TcCustomFontWeight);
		m_pGlyphMap->AddFallbackFaceByName("DejaVu Sans");

		// extract language variant family names
		const json_value &Variants = (*pJsonData)["language variants"];
		if(Variants.type == json_object)
		{
			m_vVariants.reserve(Variants.u.object.length);
			for(size_t i = 0; i < Variants.u.object.length; ++i)
			{
				const json_value *pFamilyName = Variants.u.object.values[i].value;
				if(pFamilyName->type != json_string)
				{
					log_error("textrender", "Font index malformed: 'language variants' entries must have string values (error on entry '%s')", Variants.u.object.values[i].name);
					Success = false;
					continue;
				}

				SFontLanguageVariant Variant;
				str_format(Variant.m_aLanguageFile, sizeof(Variant.m_aLanguageFile), "languages/%s.txt", Variants.u.object.values[i].name);
				str_copy(Variant.m_aFamilyName, pFamilyName->u.string.ptr);
				m_vVariants.emplace_back(Variant);
			}
		}
		else
		{
			log_error("textrender", "Font index malformed: 'language variants' must be an array");
			Success = false;
		}

		// extract fallback family names
		const json_value &FallbackFaces = (*pJsonData)["fallbacks"];
		if(FallbackFaces.type == json_array)
		{
			for(unsigned i = 0; i < FallbackFaces.u.array.length; ++i)
			{
				if(FallbackFaces[i].type != json_string)
				{
					log_error("textrender", "Font index malformed: 'fallbacks' must be an array of strings (error at index %d)", i);
					Success = false;
					continue;
				}
				if(!m_pGlyphMap->AddFallbackFaceByName(FallbackFaces[i].u.string.ptr))
				{
					Success = false;
				}
			}
		}
		else
		{
			log_error("textrender", "Font index malformed: 'fallbacks' must be an array");
			Success = false;
		}

		// FONT_ICON_* 与 QmIcon 的字形回退统一固定到随包 Phosphor，避免用户目录
		// 中旧版 Font Awesome 索引触发无意义的缺字和粗体告警。
		if(!m_pGlyphMap->TrySetIconFaceByName("Phosphor"))
		{
			log_error("textrender", "Bundled 'Phosphor' icon face is unavailable; icon glyphs will be missing");
			Success = false;
		}
		if(!m_pGlyphMap->TrySetIconBoldFaceByName("Phosphor-Bold"))
		{
			if(!m_pGlyphMap->TrySetIconBoldFaceByName("Phosphor"))
				m_pGlyphMap->UseRegularFaceForIconBold();
		}
		m_pGlyphMap->SetIconStyleFacesByName();

		if(const FT_Face IconFace = m_pGlyphMap->IconFace())
		{
			// 图标字体来自哪一份 index.json 直接决定 FONT_ICON_* 能否显示，
			// 用户目录的同名文件会覆盖 data/fonts/index.json，这里留一条可排查的记录。
			log_info("textrender", "Icon font face: '%s'", IconFace->family_name != nullptr ? IconFace->family_name : "(unknown)");
		}

		m_pGlyphMap->SetIconFontWeight(g_Config.m_QmUiIconWeight);

		json_value_free(pJsonData);
		return Success;
	}

	void SetFontPreset(EFontPreset FontPreset) override
	{
		m_pGlyphMap->SetFontPreset(FontPreset);
		m_FontPreset = FontPreset;
		m_Color = ResolveFontPresetColor(m_UnmodifiedColor);
	}

	EFontPreset GetFontPreset() const override
	{
		return m_FontPreset;
	}

	void SetIconFontWeight(int Weight) override
	{
		m_pGlyphMap->SetIconFontWeight(Weight);
		if(m_FontPreset == EFontPreset::ICON_FONT)
			m_pGlyphMap->SetFontPreset(EFontPreset::ICON_FONT);
	}

	void SetFontLanguageVariant(const char *pLanguageFile) override
	{
		for(const auto &Variant : m_vVariants)
		{
			if(str_comp(pLanguageFile, Variant.m_aLanguageFile) == 0)
			{
				m_pGlyphMap->SetVariantFaceByName(Variant.m_aFamilyName);
				return;
			}
		}
		m_pGlyphMap->SetVariantFaceByName(nullptr);
	}

	void Text(float x, float y, float FontSize, const char *pText, float LineWidth = -1.0f) override
	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(x, y));
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = LineWidth;
		TextEx(&Cursor, pText, -1);
	}

	float TextWidth(float FontSize, const char *pText, int StrLength = -1, float LineWidth = -1.0f, int Flags = 0, const STextSizeProperties &TextSizeProps = {}) override
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_Flags = Flags;
		Cursor.m_LineWidth = LineWidth;
		Cursor.m_CalculateVisualBoundingBox = TextSizeProps.m_pVisualTop != nullptr || TextSizeProps.m_pVisualBottom != nullptr;
		TextEx(&Cursor, pText, StrLength);
		if(TextSizeProps.m_pHeight != nullptr)
			*TextSizeProps.m_pHeight = Cursor.Height();
		if(TextSizeProps.m_pAlignedFontSize != nullptr)
			*TextSizeProps.m_pAlignedFontSize = Cursor.m_AlignedFontSize;
		if(TextSizeProps.m_pMaxCharacterHeightInLine != nullptr)
			*TextSizeProps.m_pMaxCharacterHeightInLine = Cursor.m_MaxCharacterHeight;
		if(TextSizeProps.m_pVisualTop != nullptr)
			*TextSizeProps.m_pVisualTop = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualTop - Cursor.m_StartY : 0.0f;
		if(TextSizeProps.m_pVisualBottom != nullptr)
			*TextSizeProps.m_pVisualBottom = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualBottom - Cursor.m_StartY : Cursor.Height();
		if(TextSizeProps.m_pLineCount != nullptr)
			*TextSizeProps.m_pLineCount = Cursor.m_LineCount;
		return Cursor.m_LongestLineWidth;
	}

	STextBoundingBox TextBoundingBox(float FontSize, const char *pText, int StrLength = -1, float LineWidth = -1.0f, float LineSpacing = 0.0f, int Flags = 0) override
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_Flags = Flags;
		Cursor.m_LineWidth = LineWidth;
		Cursor.m_LineSpacing = LineSpacing;
		TextEx(&Cursor, pText, StrLength);
		return Cursor.BoundingBox();
	}

	void TextColor(float r, float g, float b, float a) override
	{
		m_UnmodifiedColor = ColorRGBA(r, g, b, a);
		m_Color = ResolveFontPresetColor(m_UnmodifiedColor);
	}

	void TextColor(ColorRGBA Color) override
	{
		m_UnmodifiedColor = Color;
		m_Color = ResolveFontPresetColor(m_UnmodifiedColor);
	}

	void TextOutlineColor(float r, float g, float b, float a) override
	{
		m_OutlineColor.r = r;
		m_OutlineColor.g = g;
		m_OutlineColor.b = b;
		m_OutlineColor.a = a;
	}

	void TextOutlineColor(ColorRGBA Color) override
	{
		m_OutlineColor = Color;
	}

	void TextSelectionColor(float r, float g, float b, float a) override
	{
		m_SelectionColor.r = r;
		m_SelectionColor.g = g;
		m_SelectionColor.b = b;
		m_SelectionColor.a = a;
	}

	void TextSelectionColor(ColorRGBA Color) override
	{
		m_SelectionColor = Color;
	}

	ColorRGBA GetTextColor() const override
	{
		return m_Color;
	}

	ColorRGBA GetTextOutlineColor() const override
	{
		return m_OutlineColor;
	}

	ColorRGBA GetTextSelectionColor() const override
	{
		return m_SelectionColor;
	}

	void InitTextContainer(STextContainer &TextContainer, CTextCursor *pCursor)
	{
		TextContainer.m_SingleTimeUse = (m_RenderFlags & TEXT_RENDER_FLAG_ONE_TIME_USE) != 0;

		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float ScreenWidth = ScreenX1 - ScreenX0;
		const float ScreenHeight = ScreenY1 - ScreenY0;
		const float GraphicsWidth = Graphics()->ScreenWidth();
		const float GraphicsHeight = Graphics()->ScreenHeight();
		const float SafeGraphicsWidth = GraphicsWidth > 0.0f && std::isfinite(GraphicsWidth) ? GraphicsWidth : 1.0f;
		const float SafeGraphicsHeight = GraphicsHeight > 0.0f && std::isfinite(GraphicsHeight) ? GraphicsHeight : 1.0f;
		const vec2 FakeToScreen = vec2(
			ScreenWidth >= 1.0f && std::isfinite(ScreenWidth) ? SafeGraphicsWidth / ScreenWidth : 1.0f,
			ScreenHeight >= 1.0f && std::isfinite(ScreenHeight) ? SafeGraphicsHeight / ScreenHeight : 1.0f);
		TextContainer.m_AlignedStartX = round_to_int(pCursor->m_X * FakeToScreen.x) / FakeToScreen.x;
		TextContainer.m_AlignedStartY = round_to_int(pCursor->m_Y * FakeToScreen.y) / FakeToScreen.y;
		TextContainer.m_X = pCursor->m_X;
		TextContainer.m_Y = pCursor->m_Y;
		TextContainer.m_Flags = pCursor->m_Flags;

		if(pCursor->m_LineWidth <= 0.0f)
			TextContainer.m_RenderFlags = m_RenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE;
		else
			TextContainer.m_RenderFlags = m_RenderFlags;
	}

	void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		if((pCursor->m_Flags & TEXTFLAG_RENDER) == 0)
		{
			// 仅布局的文本不占用文本容器索引，但字形查找仍可能更新图集。
			STextContainer LayoutContainer;
			InitTextContainer(LayoutContainer, pCursor);
			AppendTextContainerImpl(LayoutContainer, pCursor, pText, Length);
			return;
		}

		const unsigned OldRenderFlags = m_RenderFlags;
		m_RenderFlags |= TEXT_RENDER_FLAG_ONE_TIME_USE;
		STextContainerIndex TextCont;
		CreateTextContainer(TextCont, pCursor, pText, Length);
		m_RenderFlags = OldRenderFlags;
		if(TextCont.Valid())
		{
			if((pCursor->m_Flags & TEXTFLAG_RENDER) != 0)
			{
				ColorRGBA TextColor = DefaultTextColor();
				ColorRGBA TextColorOutline = DefaultTextOutlineColor();
				RenderTextContainer(TextCont, TextColor, TextColorOutline);
			}
			DeleteTextContainer(TextCont);
		}
	}

	bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		dbg_assert(!TextContainerIndex.Valid(), "Text container index was not cleared.");

		const bool PerfEnabled = QmPerfEnabled();
		const auto CreateStart = PerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds(0);
		if(PerfEnabled)
			++m_QmPerfTextContainerNew;
		++m_QmFrameContainerCreates;

		TextContainerIndex.Reset();
		TextContainerIndex.m_Index = GetFreeTextContainerIndex();

		STextContainer &TextContainer = GetTextContainer(TextContainerIndex);
		InitTextContainer(TextContainer, pCursor);

		AppendTextContainer(TextContainerIndex, pCursor, pText, Length);

		const bool IsRendered = (pCursor->m_Flags & TEXTFLAG_RENDER) != 0;

		if(TextContainer.m_StringInfo.m_vCharacterQuads.empty() && TextContainer.m_StringInfo.m_SelectionQuadContainerIndex == -1 && IsRendered)
		{
			if(PerfEnabled)
				m_QmPerfTextContainerCreateMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - CreateStart).count();
			FreeTextContainer(TextContainerIndex);
			return false;
		}
		else
		{
			if(PerfEnabled)
				m_QmPerfTextContainerCreateMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - CreateStart).count();
			if(Graphics()->IsTextBufferingEnabled() && IsRendered && !TextContainer.m_StringInfo.m_vCharacterQuads.empty())
			{
				if((TextContainer.m_RenderFlags & TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD) == 0)
				{
					UploadTextContainer(TextContainerIndex);
				}
			}

			TextContainer.m_LineCount = pCursor->m_LineCount;
			TextContainer.m_GlyphCount = pCursor->m_GlyphCount;
			TextContainer.m_CharCount = pCursor->m_CharCount;
			TextContainer.m_MaxLines = pCursor->m_MaxLines;
			TextContainer.m_LineWidth = pCursor->m_LineWidth;
			return true;
		}
	}

	void AppendTextContainer(STextContainerIndex TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		AppendTextContainerImpl(GetTextContainer(TextContainerIndex), pCursor, pText, Length);
	}

	void AppendTextContainerImpl(STextContainer &TextContainer, CTextCursor *pCursor, const char *pText, int Length = -1)
	{
		str_append(TextContainer.m_aDebugText, pText);

		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

		const float ScreenWidth = ScreenX1 - ScreenX0;
		const float ScreenHeight = ScreenY1 - ScreenY0;
		const float GraphicsWidth = Graphics()->ScreenWidth();
		const float GraphicsHeight = Graphics()->ScreenHeight();
		const float SafeGraphicsWidth = GraphicsWidth > 0.0f && std::isfinite(GraphicsWidth) ? GraphicsWidth : 1.0f;
		const float SafeGraphicsHeight = GraphicsHeight > 0.0f && std::isfinite(GraphicsHeight) ? GraphicsHeight : 1.0f;
		const vec2 FakeToScreen = vec2(
			ScreenWidth >= 1.0f && std::isfinite(ScreenWidth) ? SafeGraphicsWidth / ScreenWidth : 1.0f,
			ScreenHeight >= 1.0f && std::isfinite(ScreenHeight) ? SafeGraphicsHeight / ScreenHeight : 1.0f);
		const float CursorX = round_to_int(pCursor->m_X * FakeToScreen.x) / FakeToScreen.x;
		const float CursorY = round_to_int(pCursor->m_Y * FakeToScreen.y) / FakeToScreen.y;
		const int ActualSize = round_truncate(pCursor->m_FontSize * FakeToScreen.y);
		pCursor->m_AlignedFontSize = ActualSize / FakeToScreen.y;
		pCursor->m_AlignedLineSpacing = round_truncate(pCursor->m_LineSpacing * FakeToScreen.y) / FakeToScreen.y;

		// string length
		if(Length < 0)
			Length = str_length(pText);
		else
			Length = QmTextLayoutByteLength(pText, Length);

		const char *pCurrent = pText;
		const char *pEnd = pCurrent + Length;
		const char *pPrevBatchEnd = nullptr;
		const char *pEllipsis = "…";
		const SGlyph *pEllipsisGlyph = nullptr;
		// Only text that does not fit as a whole is ellipsized. Both the ellipsis glyph and
		// the width of the whole text are only determined once the line width is nearly
		// exhausted, so text that stays well within it is never measured a second time.
		bool EllipsisGlyphResolved = false;
		bool EllipsisFitResolved = false;

		const unsigned RenderFlags = TextContainer.m_RenderFlags;

		float DrawX = 0.0f, DrawY = 0.0f;
		if((RenderFlags & TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) != 0)
		{
			DrawX = pCursor->m_X;
			DrawY = pCursor->m_Y;
		}
		else
		{
			DrawX = CursorX;
			DrawY = CursorY;
		}

		int LineCount = pCursor->m_LineCount;

		const bool IsRendered = (pCursor->m_Flags & TEXTFLAG_RENDER) != 0;

		const float CursorInnerWidth = GraphicsWidth > 0.0f ? (ScreenWidth / GraphicsWidth) * 2 : 0.0f;
		const float CursorOuterWidth = CursorInnerWidth * 2;
		const float CursorOuterInnerDiff = (CursorOuterWidth - CursorInnerWidth) / 2;

		std::vector<IGraphics::CQuadItem> vSelectionQuads;
		pCursor->m_vSelectionQuads.clear();
		int SelectionQuadLine = -1;
		bool SelectionStarted = false;
		bool SelectionUsedPress = false;
		bool SelectionUsedRelease = false;
		int SelectionStartChar = -1;
		int SelectionEndChar = -1;

		const auto &&CheckInsideChar = [&](bool CheckOuter, vec2 CursorPos, float LastCharX, float LastCharWidth, float CharX, float CharWidth, float CharY) -> bool {
			return (LastCharX - LastCharWidth / 2 <= CursorPos.x &&
				       CharX + CharWidth / 2 > CursorPos.x &&
				       CursorPos.y >= CharY - pCursor->m_AlignedFontSize &&
				       CursorPos.y < CharY + pCursor->m_AlignedLineSpacing) ||
			       (CheckOuter &&
				       CursorPos.y <= CharY - pCursor->m_AlignedFontSize);
		};
		const auto &&CheckSelectionStart = [&](bool CheckOuter, vec2 CursorPos, int &SelectionChar, bool &SelectionUsedCase, float LastCharX, float LastCharWidth, float CharX, float CharWidth, float CharY) {
			if(!SelectionStarted && !SelectionUsedCase &&
				CheckInsideChar(CheckOuter, CursorPos, LastCharX, LastCharWidth, CharX, CharWidth, CharY))
			{
				SelectionChar = pCursor->m_GlyphCount;
				SelectionStarted = !SelectionStarted;
				SelectionUsedCase = true;
			}
		};
		const auto &&CheckOutsideChar = [&](bool CheckOuter, vec2 CursorPos, float CharX, float CharWidth, float CharY) -> bool {
			return (CharX + CharWidth / 2 > CursorPos.x &&
				       CursorPos.y >= CharY - pCursor->m_AlignedFontSize &&
				       CursorPos.y < CharY + pCursor->m_AlignedLineSpacing) ||
			       (CheckOuter &&
				       CursorPos.y >= CharY + pCursor->m_AlignedLineSpacing);
		};
		const auto &&CheckSelectionEnd = [&](bool CheckOuter, vec2 CursorPos, int &SelectionChar, bool &SelectionUsedCase, float CharX, float CharWidth, float CharY) {
			if(SelectionStarted && !SelectionUsedCase &&
				CheckOutsideChar(CheckOuter, CursorPos, CharX, CharWidth, CharY))
			{
				SelectionChar = pCursor->m_GlyphCount;
				SelectionStarted = !SelectionStarted;
				SelectionUsedCase = true;
			}
		};
		const auto &&CheckLineEnd = [&](vec2 CursorPos, float LineEndX, float LineY) -> bool {
			return CursorPos.x >= LineEndX &&
			       CursorPos.y >= LineY - pCursor->m_AlignedFontSize &&
			       CursorPos.y < LineY + pCursor->m_AlignedLineSpacing;
		};
		const auto &&CheckSelectionStartAtLineEnd = [&](vec2 CursorPos, int &SelectionChar, bool &SelectionUsedCase, float LineEndX, float CharY) {
			if(!SelectionStarted && !SelectionUsedCase &&
				CheckLineEnd(CursorPos, LineEndX, CharY))
			{
				SelectionChar = pCursor->m_GlyphCount;
				SelectionStarted = !SelectionStarted;
				SelectionUsedCase = true;
			}
		};

		float LastSelX = DrawX;
		float LastSelWidth = 0;
		float LastCharX = DrawX;
		float LastCharWidth = 0;

		IGraphics::CQuadItem aCursorQuads[2];
		bool HasCursor = false;

		const auto &&SetCursorQuad = [&](float CursorPosX, float CursorPosY) {
			HasCursor = true;
			aCursorQuads[0] = IGraphics::CQuadItem(CursorPosX - CursorOuterInnerDiff, CursorPosY, CursorOuterWidth, pCursor->m_AlignedFontSize);
			aCursorQuads[1] = IGraphics::CQuadItem(CursorPosX, CursorPosY + CursorOuterInnerDiff, CursorInnerWidth, pCursor->m_AlignedFontSize - CursorOuterInnerDiff * 2);
			pCursor->m_CursorRenderedPosition = vec2(CursorPosX, CursorPosY);
			pCursor->m_HasCursorRenderedPosition = true;
		};
		const auto &&CheckCursorAtCharacter = [&](float CursorPosX, float CursorPosY) {
			if(pCursor->m_CursorMode != TEXT_CURSOR_CURSOR_MODE_NONE && pCursor->m_GlyphCount == pCursor->m_CursorCharacter)
				SetCursorQuad(CursorPosX, CursorPosY);
		};
		const auto &&CheckSelectionSetAtCharacter = [&]() {
			if(pCursor->m_CalculateSelectionMode != TEXT_CURSOR_SELECTION_MODE_SET)
				return;
			if(pCursor->m_GlyphCount == pCursor->m_SelectionStart)
			{
				SelectionStarted = !SelectionStarted;
				SelectionStartChar = pCursor->m_GlyphCount;
				SelectionUsedPress = true;
			}
			if(pCursor->m_GlyphCount == pCursor->m_SelectionEnd)
			{
				SelectionStarted = !SelectionStarted;
				SelectionEndChar = pCursor->m_GlyphCount;
				SelectionUsedRelease = true;
			}
		};
		const auto &&CheckCalculatedLineEnd = [&]() {
			const float LineEndY = DrawY + pCursor->m_AlignedFontSize;
			if(pCursor->m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE && pCursor->m_CursorCharacter == -1 &&
				CheckLineEnd(pCursor->m_ReleaseMouse, LastCharX + LastCharWidth / 2.0f, LineEndY))
			{
				pCursor->m_CursorCharacter = pCursor->m_GlyphCount;
			}
			if(pCursor->m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE)
			{
				const float LineEndX = LastCharX + LastCharWidth / 2.0f;
				CheckSelectionStartAtLineEnd(pCursor->m_PressMouse, SelectionStartChar, SelectionUsedPress, LineEndX, LineEndY);
				CheckSelectionStartAtLineEnd(pCursor->m_ReleaseMouse, SelectionEndChar, SelectionUsedRelease, LineEndX, LineEndY);
				if(SelectionStarted && !SelectionUsedRelease && CheckLineEnd(pCursor->m_ReleaseMouse, LineEndX, LineEndY))
				{
					SelectionEndChar = pCursor->m_GlyphCount;
					SelectionStarted = !SelectionStarted;
					SelectionUsedRelease = true;
				}
				if(SelectionStarted && !SelectionUsedPress && CheckLineEnd(pCursor->m_PressMouse, LineEndX, LineEndY))
				{
					SelectionStartChar = pCursor->m_GlyphCount;
					SelectionStarted = !SelectionStarted;
					SelectionUsedPress = true;
				}
			}
		};

		// Returns true if line was started
		const auto &&StartNewLine = [&]() {
			if(pCursor->m_MaxLines > 0 && LineCount >= pCursor->m_MaxLines)
				return false;

			DrawX = pCursor->m_StartX;
			DrawY += pCursor->m_AlignedFontSize + pCursor->m_AlignedLineSpacing;
			if((RenderFlags & TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) == 0)
			{
				DrawX = round_to_int(DrawX * FakeToScreen.x) / FakeToScreen.x; // realign
				DrawY = round_to_int(DrawY * FakeToScreen.y) / FakeToScreen.y;
			}
			LastSelX = DrawX;
			LastSelWidth = 0;
			LastCharX = DrawX;
			LastCharWidth = 0;
			++LineCount;
			return true;
		};

		if(pCursor->m_CalculateSelectionMode != TEXT_CURSOR_SELECTION_MODE_NONE || pCursor->m_CursorMode != TEXT_CURSOR_CURSOR_MODE_NONE)
		{
			if(IsRendered)
				Graphics()->QuadContainerReset(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex);

			// if in calculate mode, also calculate the cursor
			if(pCursor->m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE)
				pCursor->m_CursorCharacter = -1;
		}

		const SGlyph *pLastGlyph = nullptr;
		bool GotNewLineLast = false;

		int ColorOption = 0;
		// QmClient：逐字符顶点偏移的游标，按字符顺序消费 m_vCharOffsets。
		int OffsetOption = 0;

		while(pCurrent < pEnd && pCurrent != pEllipsis)
		{
			bool NewLine = false;
			const char *pBatchEnd = pEnd;
			if(pCursor->m_LineWidth > 0.0f && !(pCursor->m_Flags & TEXTFLAG_STOP_AT_END) && !(pCursor->m_Flags & TEXTFLAG_ELLIPSIS_AT_END))
			{
				int Wlen = minimum(WordLength(pCurrent), (int)(pEnd - pCurrent));
				CTextCursor Compare = QmTextWordMeasureCursor(*pCursor, DrawX, DrawY);
				Compare.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_NONE;
				Compare.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_NONE;
				Compare.m_Flags &= ~TEXTFLAG_RENDER;
				Compare.m_Flags |= TEXTFLAG_DISALLOW_NEWLINE;
				Compare.m_LineWidth = -1.0f;
				TextEx(&Compare, pCurrent, Wlen);

				if(Compare.m_X - DrawX > pCursor->m_LineWidth)
				{
					// word can't be fitted in one line, cut it
					CTextCursor Cutter = QmTextWordMeasureCursor(*pCursor, DrawX, DrawY);
					Cutter.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_NONE;
					Cutter.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_NONE;
					Cutter.m_GlyphCount = 0;
					Cutter.m_CharCount = 0;
					Cutter.m_Flags &= ~TEXTFLAG_RENDER;
					Cutter.m_Flags |= TEXTFLAG_STOP_AT_END | TEXTFLAG_DISALLOW_NEWLINE;

					TextEx(&Cutter, pCurrent, Wlen);
					Wlen = str_utf8_rewind(pCurrent, Cutter.m_CharCount); // rewind once to skip the last character that did not fit
					NewLine = true;

					if(Cutter.m_GlyphCount <= 3 && !GotNewLineLast) // if we can't place 3 chars of the word on this line, take the next
						Wlen = 0;
				}
				else if(Compare.m_X - pCursor->m_StartX > pCursor->m_LineWidth && !GotNewLineLast)
				{
					NewLine = true;
					Wlen = 0;
				}

				pBatchEnd = pCurrent + Wlen;
			}

			const char *pTmp = pCurrent;
			int NextCharacter = str_utf8_decode(&pTmp);

			while(pCurrent < pBatchEnd && pCurrent != pEllipsis)
			{
				const int PrevCharCount = pCursor->m_CharCount;
				pCursor->m_CharCount += pTmp - pCurrent;
				pCurrent = pTmp;
				int Character = NextCharacter;
				NextCharacter = str_utf8_decode(&pTmp);

				if(Character == '\n')
				{
					if((pCursor->m_Flags & TEXTFLAG_DISALLOW_NEWLINE) == 0)
					{
						CheckCalculatedLineEnd();
						CheckSelectionSetAtCharacter();
						CheckCursorAtCharacter(LastSelX + LastSelWidth, DrawY);
						if(StartNewLine())
						{
							pLastGlyph = nullptr;
							continue;
						}
						else
						{
							pCurrent = pEnd;
							pCursor->m_Truncated = true;
							break;
						}
					}
					else
					{
						Character = ' ';
					}
				}

				const SGlyph *pGlyph = m_pGlyphMap->GetGlyph(Character, ActualSize);
				if(pGlyph)
				{
					const float Scale = 1.0f / pGlyph->m_FontSize;

					const bool ApplyBearingX = !(((RenderFlags & TEXT_RENDER_FLAG_NO_X_BEARING) != 0) || (pCursor->m_GlyphCount == 0 && (RenderFlags & TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING) != 0));
					const float Advance = (((RenderFlags & TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH) != 0) ? (pGlyph->m_Width) : (pGlyph->m_AdvanceX + ((!ApplyBearingX) ? (-pGlyph->m_OffsetX) : 0.f))) * Scale * pCursor->m_AlignedFontSize;

					const float OutLineRealDiff = (pGlyph->m_Width - pGlyph->m_CharWidth) * Scale * pCursor->m_AlignedFontSize;

					float CharKerning = 0.0f;
					if((RenderFlags & TEXT_RENDER_FLAG_KERNING) != 0)
						CharKerning = m_pGlyphMap->Kerning(pLastGlyph, pGlyph).x * Scale * pCursor->m_AlignedFontSize;
					pLastGlyph = pGlyph;

					if((pCursor->m_Flags & TEXTFLAG_ELLIPSIS_AT_END) != 0 && pCursor->m_LineWidth > 0.0f && pCurrent < pBatchEnd && pCurrent != pEllipsis)
					{
						if(!EllipsisGlyphResolved)
						{
							EllipsisGlyphResolved = true;
							pEllipsisGlyph = m_pGlyphMap->GetGlyph(0x2026, ActualSize); // …
							if(pEllipsisGlyph == nullptr)
							{
								// no ellipsis char in font, just stop at end instead
								pCursor->m_Flags &= ~TEXTFLAG_ELLIPSIS_AT_END;
								pCursor->m_Flags |= TEXTFLAG_STOP_AT_END;
							}
						}
						if(pEllipsisGlyph != nullptr)
						{
							float AdvanceEllipsis = (((RenderFlags & TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH) != 0) ? (pEllipsisGlyph->m_Width) : (pEllipsisGlyph->m_AdvanceX + ((!ApplyBearingX) ? (-pEllipsisGlyph->m_OffsetX) : 0.f))) * Scale * pCursor->m_AlignedFontSize;
							float CharKerningEllipsis = 0.0f;
							if((RenderFlags & TEXT_RENDER_FLAG_KERNING) != 0)
							{
								CharKerningEllipsis = m_pGlyphMap->Kerning(pGlyph, pEllipsisGlyph).x * Scale * pCursor->m_AlignedFontSize;
							}
							if(DrawX + CharKerning + Advance + CharKerningEllipsis + AdvanceEllipsis - pCursor->m_StartX > pCursor->m_LineWidth)
							{
								if(!EllipsisFitResolved)
								{
									EllipsisFitResolved = true;
									if(pCursor->m_LineWidth >= TextWidth(pCursor->m_FontSize, pText))
										pEllipsisGlyph = nullptr;
								}
								if(pEllipsisGlyph != nullptr)
								{
									// we hit the end, only render ellipsis and finish
									pTmp = pEllipsis;
									NextCharacter = 0x2026;
									pCursor->m_Truncated = true;
									continue;
								}
							}
						}
					}

					if(pCursor->m_LineWidth > 0.0f &&
						(pCursor->m_Flags & TEXTFLAG_STOP_AT_END) != 0 &&
						(DrawX + CharKerning) + Advance - pCursor->m_StartX > pCursor->m_LineWidth)
					{
						// we hit the end of the line, no more to render or count
						pCurrent = pEnd;
						pCursor->m_Truncated = true;
						break;
					}

					float BearingX = (!ApplyBearingX ? 0.f : pGlyph->m_OffsetX) * Scale * pCursor->m_AlignedFontSize;
					float CharWidth = pGlyph->m_Width * Scale * pCursor->m_AlignedFontSize;

					float BearingY = (((RenderFlags & TEXT_RENDER_FLAG_NO_Y_BEARING) != 0) ? 0.f : (pGlyph->m_OffsetY * Scale * pCursor->m_AlignedFontSize));
					float CharHeight = pGlyph->m_Height * Scale * pCursor->m_AlignedFontSize;

					if((RenderFlags & TEXT_RENDER_FLAG_NO_OVERSIZE) != 0)
					{
						if(CharHeight + BearingY > pCursor->m_AlignedFontSize)
						{
							BearingY = 0;
							float ScaleChar = (CharHeight + BearingY) / pCursor->m_AlignedFontSize;
							CharHeight = pCursor->m_AlignedFontSize;
							CharWidth /= ScaleChar;
						}
					}

					const float TmpY = (DrawY + pCursor->m_AlignedFontSize);
					const float CharX = (DrawX + CharKerning) + BearingX;
					const float CharY = TmpY - BearingY;
					if(pCursor->m_CalculateVisualBoundingBox && pGlyph->m_CharHeight > 0.0f)
					{
						const float FillTopOffset = ((pGlyph->m_Height - pGlyph->m_CharHeight) * 0.5f) * Scale * pCursor->m_AlignedFontSize;
						const float FillHeight = pGlyph->m_CharHeight * Scale * pCursor->m_AlignedFontSize;
						const float CharTop = CharY - CharHeight + FillTopOffset;
						const float CharBottom = CharTop + FillHeight;
						if(!pCursor->m_HasVisualBoundingBox)
						{
							pCursor->m_HasVisualBoundingBox = true;
							pCursor->m_VisualTop = CharTop;
							pCursor->m_VisualBottom = CharBottom;
						}
						else
						{
							pCursor->m_VisualTop = minimum(pCursor->m_VisualTop, CharTop);
							pCursor->m_VisualBottom = maximum(pCursor->m_VisualBottom, CharBottom);
						}
					}

					// Check if we have any color split
					ColorRGBA Color = m_Color;
					// QmClient：字符内横向渐变的右边缘色，未启用渐变时与 Color 相同。
					ColorRGBA ColorEnd = m_Color;
					if(ColorOption < (int)pCursor->m_vColorSplits.size())
					{
						STextColorSplit &Split = pCursor->m_vColorSplits.at(ColorOption);
						if(PrevCharCount >= Split.m_CharIndex && (Split.m_Length == -1 || PrevCharCount < Split.m_CharIndex + Split.m_Length))
						{
							Color = Split.m_Color;
							ColorEnd = Split.m_ColorEnd;
						}
						if(Split.m_Length != -1 && PrevCharCount >= (Split.m_CharIndex + Split.m_Length - 1))
						{
							ColorOption++;
							if(ColorOption < (int)pCursor->m_vColorSplits.size())
							{ // Handle splits that are
								Split = pCursor->m_vColorSplits.at(ColorOption);
								if(PrevCharCount >= Split.m_CharIndex)
								{
									Color = Split.m_Color;
									ColorEnd = Split.m_ColorEnd;
								}
							}
						}
					}

					// QmClient：逐字符顶点偏移（波浪浮动）。即使该字符不渲染也要消费游标，避免与字符错位。
					float CharOffsetX = 0.0f;
					float CharOffsetY = 0.0f;
					// 跳过序号已经落后的条目：调用方可能为换行等不产生顶点的字符也建了条目，
					// 若不跳过，游标会永久卡住，之后所有字符的偏移恒为 0。
					while(OffsetOption < (int)pCursor->m_vCharOffsets.size() && pCursor->m_vCharOffsets.at(OffsetOption).m_CharIndex < PrevCharCount)
						++OffsetOption;
					if(OffsetOption < (int)pCursor->m_vCharOffsets.size() && pCursor->m_vCharOffsets.at(OffsetOption).m_CharIndex == PrevCharCount)
					{
						const STextCharOffset &CharOffset = pCursor->m_vCharOffsets.at(OffsetOption);
						CharOffsetX = CharOffset.m_XOffset;
						CharOffsetY = CharOffset.m_YOffset;
						++OffsetOption;
					}

					// don't add text that isn't drawn, the color overwrite is used for that
					if(Color.a != 0.f && IsRendered)
					{
						TextContainer.m_StringInfo.m_vCharacterQuads.emplace_back();
						STextCharQuad &TextCharQuad = TextContainer.m_StringInfo.m_vCharacterQuads.back();

						TextCharQuad.m_aVertices[0].m_X = CharX + CharOffsetX;
						TextCharQuad.m_aVertices[0].m_Y = CharY + CharOffsetY;
						TextCharQuad.m_aVertices[0].m_U = pGlyph->m_aUVs[0];
						TextCharQuad.m_aVertices[0].m_V = pGlyph->m_aUVs[3];
						TextCharQuad.m_aVertices[0].m_Color.r = (unsigned char)(Color.r * 255.f);
						TextCharQuad.m_aVertices[0].m_Color.g = (unsigned char)(Color.g * 255.f);
						TextCharQuad.m_aVertices[0].m_Color.b = (unsigned char)(Color.b * 255.f);
						TextCharQuad.m_aVertices[0].m_Color.a = (unsigned char)(Color.a * 255.f);

						TextCharQuad.m_aVertices[1].m_X = CharX + CharWidth + CharOffsetX;
						TextCharQuad.m_aVertices[1].m_Y = CharY + CharOffsetY;
						TextCharQuad.m_aVertices[1].m_U = pGlyph->m_aUVs[2];
						TextCharQuad.m_aVertices[1].m_V = pGlyph->m_aUVs[3];
						TextCharQuad.m_aVertices[1].m_Color.r = (unsigned char)(ColorEnd.r * 255.f);
						TextCharQuad.m_aVertices[1].m_Color.g = (unsigned char)(ColorEnd.g * 255.f);
						TextCharQuad.m_aVertices[1].m_Color.b = (unsigned char)(ColorEnd.b * 255.f);
						TextCharQuad.m_aVertices[1].m_Color.a = (unsigned char)(ColorEnd.a * 255.f);

						TextCharQuad.m_aVertices[2].m_X = CharX + CharWidth + CharOffsetX;
						TextCharQuad.m_aVertices[2].m_Y = CharY - CharHeight + CharOffsetY;
						TextCharQuad.m_aVertices[2].m_U = pGlyph->m_aUVs[2];
						TextCharQuad.m_aVertices[2].m_V = pGlyph->m_aUVs[1];
						TextCharQuad.m_aVertices[2].m_Color.r = (unsigned char)(ColorEnd.r * 255.f);
						TextCharQuad.m_aVertices[2].m_Color.g = (unsigned char)(ColorEnd.g * 255.f);
						TextCharQuad.m_aVertices[2].m_Color.b = (unsigned char)(ColorEnd.b * 255.f);
						TextCharQuad.m_aVertices[2].m_Color.a = (unsigned char)(ColorEnd.a * 255.f);

						TextCharQuad.m_aVertices[3].m_X = CharX + CharOffsetX;
						TextCharQuad.m_aVertices[3].m_Y = CharY - CharHeight + CharOffsetY;
						TextCharQuad.m_aVertices[3].m_U = pGlyph->m_aUVs[0];
						TextCharQuad.m_aVertices[3].m_V = pGlyph->m_aUVs[1];
						TextCharQuad.m_aVertices[3].m_Color.r = (unsigned char)(Color.r * 255.f);
						TextCharQuad.m_aVertices[3].m_Color.g = (unsigned char)(Color.g * 255.f);
						TextCharQuad.m_aVertices[3].m_Color.b = (unsigned char)(Color.b * 255.f);
						TextCharQuad.m_aVertices[3].m_Color.a = (unsigned char)(Color.a * 255.f);
					}

					// calculate the full width from the last selection point to the end of this selection draw on screen
					const float SelWidth = (CharX + maximum(Advance, CharWidth - OutLineRealDiff / 2)) - (LastSelX + LastSelWidth);
					const float SelX = (LastSelX + LastSelWidth);

					if(pCursor->m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE)
					{
						if(pCursor->m_CursorCharacter == -1 && CheckInsideChar(pCursor->m_GlyphCount == 0, pCursor->m_ReleaseMouse, pCursor->m_GlyphCount == 0 ? std::numeric_limits<float>::lowest() : LastCharX, LastCharWidth, CharX, CharWidth, TmpY))
						{
							pCursor->m_CursorCharacter = pCursor->m_GlyphCount;
						}
					}

					if(pCursor->m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE)
					{
						if(pCursor->m_GlyphCount == 0)
						{
							CheckSelectionStart(true, pCursor->m_PressMouse, SelectionStartChar, SelectionUsedPress, std::numeric_limits<float>::lowest(), 0, CharX, CharWidth, TmpY);
							CheckSelectionStart(true, pCursor->m_ReleaseMouse, SelectionEndChar, SelectionUsedRelease, std::numeric_limits<float>::lowest(), 0, CharX, CharWidth, TmpY);
						}

						// if selection didn't start and the mouse pos is at least on 50% of the right side of the character start
						CheckSelectionStart(false, pCursor->m_PressMouse, SelectionStartChar, SelectionUsedPress, LastCharX, LastCharWidth, CharX, CharWidth, TmpY);
						CheckSelectionStart(false, pCursor->m_ReleaseMouse, SelectionEndChar, SelectionUsedRelease, LastCharX, LastCharWidth, CharX, CharWidth, TmpY);
						CheckSelectionEnd(false, pCursor->m_ReleaseMouse, SelectionEndChar, SelectionUsedRelease, CharX, CharWidth, TmpY);
						CheckSelectionEnd(false, pCursor->m_PressMouse, SelectionStartChar, SelectionUsedPress, CharX, CharWidth, TmpY);
					}
					if(pCursor->m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_SET)
					{
						CheckSelectionSetAtCharacter();
					}

					if(pCursor->m_CursorMode != TEXT_CURSOR_CURSOR_MODE_NONE)
					{
						CheckCursorAtCharacter(SelX, DrawY);
					}

					pCursor->m_MaxCharacterHeight = maximum(pCursor->m_MaxCharacterHeight, CharHeight + BearingY);

					if(NextCharacter == 0 && (RenderFlags & TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE) != 0 && Character != ' ')
						DrawX += BearingX + CharKerning + CharWidth;
					else
						DrawX += Advance + CharKerning;

					pCursor->m_GlyphCount++;

					if(SelectionStarted)
					{
						if(!vSelectionQuads.empty() && SelectionQuadLine == LineCount)
						{
							vSelectionQuads.back().m_Width += SelWidth;
						}
						else
						{
							const float SelectionHeight = pCursor->m_AlignedFontSize + pCursor->m_AlignedLineSpacing;
							const float SelectionY = DrawY + (1.0f - pCursor->m_SelectionHeightFactor) * SelectionHeight;
							const float ScaledSelectionHeight = pCursor->m_SelectionHeightFactor * SelectionHeight;
							vSelectionQuads.emplace_back(SelX, SelectionY, SelWidth, ScaledSelectionHeight);
							SelectionQuadLine = LineCount;
						}
					}

					LastSelX = SelX;
					LastSelWidth = SelWidth;
					LastCharX = CharX;
					LastCharWidth = CharWidth;
				}

				pCursor->m_LongestLineWidth = maximum(pCursor->m_LongestLineWidth, DrawX - pCursor->m_StartX);
			}

			if(NewLine)
			{
				if(pPrevBatchEnd == pBatchEnd)
					break;
				pPrevBatchEnd = pBatchEnd;
				if(!StartNewLine())
					break;
				GotNewLineLast = true;
			}
			else
				GotNewLineLast = false;
		}

		if(!TextContainer.m_StringInfo.m_vCharacterQuads.empty() && IsRendered)
		{
			// setup the buffers
			if(Graphics()->IsTextBufferingEnabled())
			{
				const size_t DataSize = TextContainer.m_StringInfo.m_vCharacterQuads.size() * sizeof(STextCharQuad);
				void *pUploadData = TextContainer.m_StringInfo.m_vCharacterQuads.data();

				if(TextContainer.m_StringInfo.m_QuadBufferObjectIndex != -1 && (TextContainer.m_RenderFlags & TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD) == 0)
				{
					Graphics()->RecreateBufferObject(TextContainer.m_StringInfo.m_QuadBufferObjectIndex, DataSize, pUploadData, TextContainer.m_SingleTimeUse ? IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT : 0);
					Graphics()->IndicesNumRequiredNotify(TextContainer.m_StringInfo.m_vCharacterQuads.size() * 6);
				}
			}
		}

		if(pCursor->m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE)
		{
			pCursor->m_SelectionStart = -1;
			pCursor->m_SelectionEnd = -1;

			if(SelectionStarted)
			{
				CheckSelectionEnd(true, pCursor->m_ReleaseMouse, SelectionEndChar, SelectionUsedRelease, std::numeric_limits<float>::max(), 0, DrawY + pCursor->m_AlignedFontSize);
				CheckSelectionEnd(true, pCursor->m_PressMouse, SelectionStartChar, SelectionUsedPress, std::numeric_limits<float>::max(), 0, DrawY + pCursor->m_AlignedFontSize);
			}
		}
		else if(pCursor->m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_SET)
		{
			CheckSelectionSetAtCharacter();
		}

		if(pCursor->m_CursorMode != TEXT_CURSOR_CURSOR_MODE_NONE)
		{
			if(pCursor->m_CursorMode == TEXT_CURSOR_CURSOR_MODE_CALCULATE && pCursor->m_CursorCharacter == -1 && CheckOutsideChar(true, pCursor->m_ReleaseMouse, std::numeric_limits<float>::max(), 0, DrawY + pCursor->m_AlignedFontSize))
			{
				pCursor->m_CursorCharacter = pCursor->m_GlyphCount;
			}

			CheckCursorAtCharacter(LastSelX + LastSelWidth, DrawY);
		}

		const bool HasSelection = !vSelectionQuads.empty() && SelectionUsedPress && SelectionUsedRelease;
		const bool HasRenderedCursor = HasCursor && pCursor->m_RenderCursor;
		const bool HasRenderedSelection = HasSelection && pCursor->m_RenderSelection;
		if((HasRenderedSelection || HasRenderedCursor) && IsRendered)
		{
			Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
			if(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex == -1)
				TextContainer.m_StringInfo.m_SelectionQuadContainerIndex = Graphics()->CreateQuadContainer(false);
			if(HasRenderedCursor)
				Graphics()->QuadContainerAddQuads(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex, aCursorQuads, std::size(aCursorQuads));
			if(HasRenderedSelection)
				Graphics()->QuadContainerAddQuads(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex, vSelectionQuads.data(), vSelectionQuads.size());
			Graphics()->QuadContainerUpload(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex);

			TextContainer.m_HasCursor = HasRenderedCursor;
			TextContainer.m_HasSelection = HasRenderedSelection;
			TextContainer.m_ForceCursorRendering = pCursor->m_ForceCursorRendering;
		}

		if(HasSelection)
		{
			pCursor->m_SelectionStart = SelectionStartChar;
			pCursor->m_SelectionEnd = SelectionEndChar;
			if(!pCursor->m_RenderSelection)
				pCursor->m_vSelectionQuads = std::move(vSelectionQuads);
		}
		else
		{
			pCursor->m_SelectionStart = -1;
			pCursor->m_SelectionEnd = -1;
		}

		// even if no text is drawn the cursor position will be adjusted
		pCursor->m_X = DrawX;
		pCursor->m_Y = DrawY;
		pCursor->m_LineCount = LineCount;

		TextContainer.m_BoundingBox = pCursor->BoundingBox();
	}

	bool CreateOrAppendTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		if(TextContainerIndex.Valid())
		{
			AppendTextContainer(TextContainerIndex, pCursor, pText, Length);
			return true;
		}
		else
		{
			return CreateTextContainer(TextContainerIndex, pCursor, pText, Length);
		}
	}

	// just deletes and creates text container
	void RecreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		DeleteTextContainer(TextContainerIndex);
		CreateTextContainer(TextContainerIndex, pCursor, pText, Length);
	}

	void RecreateTextContainerSoft(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override
	{
		STextContainer &TextContainer = GetTextContainer(TextContainerIndex);
		TextContainer.m_StringInfo.m_vCharacterQuads.clear();
		// the text buffer gets then recreated by the appended quads
		AppendTextContainer(TextContainerIndex, pCursor, pText, Length);
	}

	void DeleteTextContainer(STextContainerIndex &TextContainerIndex) override
	{
		if(!TextContainerIndex.Valid())
			return;

		STextContainer &TextContainer = GetTextContainer(TextContainerIndex);
		if(Graphics()->IsTextBufferingEnabled())
			Graphics()->DeleteBufferContainer(TextContainer.m_StringInfo.m_QuadBufferContainerIndex, true);
		Graphics()->DeleteQuadContainer(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex);
		FreeTextContainer(TextContainerIndex);
	}

	void UploadTextContainer(STextContainerIndex TextContainerIndex) override
	{
		if(Graphics()->IsTextBufferingEnabled())
		{
			const auto UploadStart = QmPerfEnabled() ? time_get_nanoseconds() : std::chrono::nanoseconds(0);
			STextContainer &TextContainer = GetTextContainer(TextContainerIndex);
			if(TextContainer.m_StringInfo.m_vCharacterQuads.empty())
				return;

			size_t DataSize = TextContainer.m_StringInfo.m_vCharacterQuads.size() * sizeof(STextCharQuad);
			void *pUploadData = TextContainer.m_StringInfo.m_vCharacterQuads.data();
			const int CreateFlags = TextContainer.m_SingleTimeUse ? IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT : 0;
			if(TextContainer.m_StringInfo.m_QuadBufferObjectIndex == -1)
				TextContainer.m_StringInfo.m_QuadBufferObjectIndex = Graphics()->CreateBufferObject(DataSize, pUploadData, CreateFlags);
			else
				Graphics()->RecreateBufferObject(TextContainer.m_StringInfo.m_QuadBufferObjectIndex, DataSize, pUploadData, CreateFlags);

			if(TextContainer.m_StringInfo.m_QuadBufferContainerIndex == -1)
			{
				m_DefaultTextContainerInfo.m_VertBufferBindingIndex = TextContainer.m_StringInfo.m_QuadBufferObjectIndex;
				TextContainer.m_StringInfo.m_QuadBufferContainerIndex = Graphics()->CreateBufferContainer(&m_DefaultTextContainerInfo);
			}
			Graphics()->IndicesNumRequiredNotify(TextContainer.m_StringInfo.m_vCharacterQuads.size() * 6);
			if(QmPerfEnabled())
			{
				++m_QmPerfTextContainerUploads;
				m_QmPerfTextContainerUploadMs += std::chrono::duration<double, std::milli>(time_get_nanoseconds() - UploadStart).count();
			}
		}
	}

	void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override
	{
		// QmClient: 渲染采样字形纹理前，把本帧积累的字形上传合并提交；
		// 唯一渲染入口（特效多 pass 也经此函数），脏区为空时零成本。
		m_pGlyphMap->FlushPendingGlyphUploads();
		STextContainer &TextContainer = GetTextContainer(TextContainerIndex);

		if(!TextContainer.m_StringInfo.m_vCharacterQuads.empty())
		{
			if(Graphics()->IsTextBufferingEnabled())
			{
				if(TextContainer.m_StringInfo.m_QuadBufferContainerIndex == -1)
					UploadTextContainer(TextContainerIndex);
				Graphics()->TextureClear();
				// render buffered text
				Graphics()->RenderText(TextContainer.m_StringInfo.m_QuadBufferContainerIndex, TextContainer.m_StringInfo.m_vCharacterQuads.size(), m_pGlyphMap->TextureDimension(), m_pGlyphMap->Texture(CGlyphMap::FONT_TEXTURE_FILL).Id(), m_pGlyphMap->Texture(CGlyphMap::FONT_TEXTURE_OUTLINE).Id(), TextColor, TextOutlineColor);
			}
			else
			{
				// render tiles
				const float UVScale = 1.0f / m_pGlyphMap->TextureDimension();

				Graphics()->FlushVertices();
				Graphics()->TextureSet(m_pGlyphMap->Texture(CGlyphMap::FONT_TEXTURE_OUTLINE));

				Graphics()->QuadsBegin();

				for(const STextCharQuad &TextCharQuad : TextContainer.m_StringInfo.m_vCharacterQuads)
				{
					Graphics()->SetColor(TextCharQuad.m_aVertices[0].m_Color.r / 255.f * TextOutlineColor.r, TextCharQuad.m_aVertices[0].m_Color.g / 255.f * TextOutlineColor.g, TextCharQuad.m_aVertices[0].m_Color.b / 255.f * TextOutlineColor.b, TextCharQuad.m_aVertices[0].m_Color.a / 255.f * TextOutlineColor.a);
					Graphics()->QuadsSetSubset(TextCharQuad.m_aVertices[0].m_U * UVScale, TextCharQuad.m_aVertices[0].m_V * UVScale, TextCharQuad.m_aVertices[2].m_U * UVScale, TextCharQuad.m_aVertices[2].m_V * UVScale);
					IGraphics::CQuadItem QuadItem(TextCharQuad.m_aVertices[0].m_X, TextCharQuad.m_aVertices[0].m_Y, TextCharQuad.m_aVertices[1].m_X - TextCharQuad.m_aVertices[0].m_X, TextCharQuad.m_aVertices[2].m_Y - TextCharQuad.m_aVertices[0].m_Y);
					Graphics()->QuadsDrawTL(&QuadItem, 1);
				}

				if(TextColor.a != 0)
				{
					Graphics()->QuadsEndKeepVertices();
					Graphics()->TextureSet(m_pGlyphMap->Texture(CGlyphMap::FONT_TEXTURE_FILL));

					int TextCharQuadIndex = 0;
					for(const STextCharQuad &TextCharQuad : TextContainer.m_StringInfo.m_vCharacterQuads)
					{
						unsigned char CR = (unsigned char)((float)(TextCharQuad.m_aVertices[0].m_Color.r) * TextColor.r);
						unsigned char CG = (unsigned char)((float)(TextCharQuad.m_aVertices[0].m_Color.g) * TextColor.g);
						unsigned char CB = (unsigned char)((float)(TextCharQuad.m_aVertices[0].m_Color.b) * TextColor.b);
						unsigned char CA = (unsigned char)((float)(TextCharQuad.m_aVertices[0].m_Color.a) * TextColor.a);
						Graphics()->ChangeColorOfQuadVertices(TextCharQuadIndex, CR, CG, CB, CA);
						++TextCharQuadIndex;
					}

					// render non outlined
					Graphics()->QuadsDrawCurrentVertices(false);
				}
				else
					Graphics()->QuadsEnd();

				// reset
				Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
			}
		}

		if(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex != -1)
		{
			if(TextContainer.m_HasSelection)
			{
				Graphics()->TextureClear();
				Graphics()->SetColor(m_SelectionColor);
				Graphics()->RenderQuadContainerEx(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex, TextContainer.m_HasCursor ? 2 : 0, -1, 0, 0);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}

			if(TextContainer.m_HasCursor)
			{
				const auto CurTime = time_get_nanoseconds();
				const bool RenderCursor = TextContainer.m_ForceCursorRendering || (CurTime - m_CursorRenderTime) > 500ms;
				Graphics()->TextureClear();
				if(RenderCursor)
				{
					Graphics()->SetColor(TextOutlineColor);
					Graphics()->RenderQuadContainerEx(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex, 0, 1, 0, 0);
					Graphics()->SetColor(TextColor);
					Graphics()->RenderQuadContainerEx(TextContainer.m_StringInfo.m_SelectionQuadContainerIndex, 1, 1, 0, 0);
				}
				if(TextContainer.m_ForceCursorRendering)
					m_CursorRenderTime = CurTime - 501ms;
				else if((CurTime - m_CursorRenderTime) > 1s)
					m_CursorRenderTime = time_get_nanoseconds();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
	}

	void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor, float X, float Y) override
	{
		STextContainer &TextContainer = GetTextContainer(TextContainerIndex);

		// remap the current screen, after render revert the change again
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

		if((TextContainer.m_RenderFlags & TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) == 0)
		{
			const float ScreenWidth = ScreenX1 - ScreenX0;
			const float ScreenHeight = ScreenY1 - ScreenY0;
			const float GraphicsWidth = Graphics()->ScreenWidth();
			const float GraphicsHeight = Graphics()->ScreenHeight();
			const float SafeGraphicsWidth = GraphicsWidth > 0.0f && std::isfinite(GraphicsWidth) ? GraphicsWidth : 1.0f;
			const float SafeGraphicsHeight = GraphicsHeight > 0.0f && std::isfinite(GraphicsHeight) ? GraphicsHeight : 1.0f;
			const vec2 FakeToScreen = vec2(
				ScreenWidth >= 1.0f && std::isfinite(ScreenWidth) ? SafeGraphicsWidth / ScreenWidth : 1.0f,
				ScreenHeight >= 1.0f && std::isfinite(ScreenHeight) ? SafeGraphicsHeight / ScreenHeight : 1.0f);
			const float AlignedX = round_to_int((TextContainer.m_X + X) * FakeToScreen.x) / FakeToScreen.x;
			const float AlignedY = round_to_int((TextContainer.m_Y + Y) * FakeToScreen.y) / FakeToScreen.y;
			X = AlignedX - TextContainer.m_AlignedStartX;
			Y = AlignedY - TextContainer.m_AlignedStartY;
		}

		TextContainer.m_BoundingBox.m_X = X;
		TextContainer.m_BoundingBox.m_Y = Y;

		Graphics()->MapScreen(ScreenX0 - X, ScreenY0 - Y, ScreenX1 - X, ScreenY1 - Y);
		RenderTextContainer(TextContainerIndex, TextColor, TextOutlineColor);
		Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
	}

	STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex TextContainerIndex) override
	{
		const STextContainer &TextContainer = GetTextContainer(TextContainerIndex);
		return TextContainer.m_BoundingBox;
	}

	void UploadEntityLayerText(const CImageInfo &TextImage, int TexSubWidth, int TexSubHeight, const char *pText, int Length, float x, float y, int FontSize) override
	{
		m_pGlyphMap->UploadEntityLayerText(TextImage, TexSubWidth, TexSubHeight, pText, Length, x, y, FontSize);
	}

	int AdjustFontSize(const char *pText, int TextLength, int MaxSize, int MaxWidth) const override
	{
		const int WidthOfText = CalculateTextWidth(pText, TextLength, 0, 100);

		int FontSize = 100.0f / ((float)WidthOfText / (float)MaxWidth);
		if(MaxSize > 0 && FontSize > MaxSize)
			FontSize = MaxSize;

		return FontSize;
	}

	float GetGlyphOffsetX(int FontSize, char TextCharacter) const override
	{
		if(m_pGlyphMap->DefaultFace() == nullptr)
			return -1.0f;

		FT_Set_Pixel_Sizes(m_pGlyphMap->DefaultFace(), 0, FontSize);
		m_pGlyphMap->InvalidateFacePixelSizeCache();
		const char *pTmp = &TextCharacter;
		const int NextCharacter = str_utf8_decode(&pTmp);

		if(NextCharacter)
		{
#if FREETYPE_MAJOR >= 2 && FREETYPE_MINOR >= 7 && (FREETYPE_MINOR > 7 || FREETYPE_PATCH >= 1)
			const FT_Int32 FTFlags = FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_BITMAP;
#else
			const FT_Int32 FTFlags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP;
#endif
			if(FT_Load_Char(m_pGlyphMap->DefaultFace(), NextCharacter, FTFlags))
			{
				log_debug("textrender", "Error loading glyph. Chr=%d", NextCharacter);
				return -1.0f;
			}

			return (float)(m_pGlyphMap->DefaultFace()->glyph->metrics.horiBearingX >> 6);
		}
		return 0.0f;
	}

	int CalculateTextWidth(const char *pText, int TextLength, int FontWidth, int FontHeight) const override
	{
		if(m_pGlyphMap->DefaultFace() == nullptr)
			return 0;

		const char *pCurrent = pText;
		const char *pEnd = pCurrent + TextLength;

		int WidthOfText = 0;
		FT_Set_Pixel_Sizes(m_pGlyphMap->DefaultFace(), FontWidth, FontHeight);
		m_pGlyphMap->InvalidateFacePixelSizeCache();
		while(pCurrent < pEnd)
		{
			const char *pTmp = pCurrent;
			const int NextCharacter = str_utf8_decode(&pTmp);
			if(NextCharacter)
			{
#if FREETYPE_MAJOR >= 2 && FREETYPE_MINOR >= 7 && (FREETYPE_MINOR > 7 || FREETYPE_PATCH >= 1)
				const FT_Int32 FTFlags = FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_BITMAP;
#else
				const FT_Int32 FTFlags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP;
#endif
				if(FT_Load_Char(m_pGlyphMap->DefaultFace(), NextCharacter, FTFlags))
				{
					log_debug("textrender", "Error loading glyph. Chr=%d", NextCharacter);
					pCurrent = pTmp;
					continue;
				}

				WidthOfText += (m_pGlyphMap->DefaultFace()->glyph->metrics.width >> 6) + 1;
			}
			pCurrent = pTmp;
		}

		return WidthOfText;
	}

	void OnPreWindowResize() override
	{
		for(auto *pTextContainer : m_vpTextContainers)
		{
			if(pTextContainer->m_pContainerUseCount != nullptr && pTextContainer->m_pContainerUseCount.use_count() <= 1)
			{
				log_error("textrender", "Found non empty text container with index %d with %" PRIzu " quads '%s'", pTextContainer->m_StringInfo.m_QuadBufferContainerIndex, pTextContainer->m_StringInfo.m_vCharacterQuads.size(), pTextContainer->m_aDebugText);
				dbg_assert_failed("Text container was forgotten by the implementation (the index was overwritten).");
			}
		}
	}

	void OnGraphicsResourcesReset() override
	{
		if(m_pGlyphMap == nullptr)
			return;
		m_pGlyphMap->OnGraphicsResourcesReset();
	}

	void OnWindowResize() override
	{
		bool HasNonEmptyTextContainer = false;
		for(auto *pTextContainer : m_vpTextContainers)
		{
			if(pTextContainer->m_StringInfo.m_QuadBufferContainerIndex != -1)
			{
				log_error("textrender", "Found non empty text container with index %d with %" PRIzu " quads '%s'", pTextContainer->m_StringInfo.m_QuadBufferContainerIndex, pTextContainer->m_StringInfo.m_vCharacterQuads.size(), pTextContainer->m_aDebugText);
				log_error("textrender", "The text container index was in use by %d ", (int)pTextContainer->m_pContainerUseCount.use_count());
				HasNonEmptyTextContainer = true;
			}
		}

		if(!HasNonEmptyTextContainer)
			return;

		// QmClient: 上面只记录，不在这里断言。真正被遗忘的容器（use_count 归 1，说明
		// 已经没有任何所有者会归还它）就地回收，避免它的 quad 继续挂在渲染器上：
		// 遗留 quad 会被重绘成幽灵文本，而且每次窗口尺寸或 UI 缩放变化都会再次触发
		// text.cpp 的“container was forgotten”断言。回收只针对已无所有者的槽位，
		// 不会影响仍有所有者的容器。
		for(auto *pTextContainer : m_vpTextContainers)
		{
			if(pTextContainer->m_pContainerUseCount == nullptr || pTextContainer->m_pContainerUseCount.use_count() > 1)
				continue;

			// 用槽位下标构造索引，保证归还的是同一个文本容器。
			int ContainerIndex = -1;
			for(size_t i = 0; i < m_vpTextContainers.size(); ++i)
			{
				if(m_vpTextContainers[i] == pTextContainer)
				{
					ContainerIndex = (int)i;
					break;
				}
			}
			if(ContainerIndex < 0)
				continue;

			STextContainerIndex OrphanIndex;
			OrphanIndex.m_Index = ContainerIndex;
			STextContainer &Orphan = GetTextContainer(OrphanIndex);
			if(Graphics()->IsTextBufferingEnabled())
				Graphics()->DeleteBufferContainer(Orphan.m_StringInfo.m_QuadBufferContainerIndex, true);
			Graphics()->DeleteQuadContainer(Orphan.m_StringInfo.m_SelectionQuadContainerIndex);
			log_error("textrender", "Reclaimed forgotten text container '%s'", pTextContainer->m_aDebugText);
			FreeTextContainer(OrphanIndex);
		}
	}
};

IEngineTextRender *CreateEngineTextRender() { return new CTextRender; }
