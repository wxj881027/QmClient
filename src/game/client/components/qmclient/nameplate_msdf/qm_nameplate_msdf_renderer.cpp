#include "qm_nameplate_msdf_renderer.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	constexpr const char *kManifestBase = "qmclient/nameplate_msdf/nameplate_base_msdf.json";
	constexpr const char *kManifestCjk = "qmclient/nameplate_msdf/nameplate_cjk_msdf.json";

	// 描边/光晕用 8 向偏移近似，方向斜向按 1/sqrt(2) 归一
	constexpr float s_aOffsetDirs[8][2] = {
		{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
		{0.70710678f, 0.70710678f}, {-0.70710678f, 0.70710678f}, {0.70710678f, -0.70710678f}, {-0.70710678f, -0.70710678f}};

	bool IsSpace(unsigned char C)
	{
		return C == ' ' || C == '\t' || C == '\n' || C == '\r';
	}

	uint32_t DecodeUtf8(const char *&p)
	{
		const unsigned char C0 = (unsigned char)*p;
		if(C0 == 0)
			return 0;
		if(C0 < 0x80)
		{
			++p;
			return C0;
		}
		int Len = 1;
		uint32_t Cp = 0;
		if((C0 & 0xE0) == 0xC0)
		{
			Len = 2;
			Cp = C0 & 0x1Fu;
		}
		else if((C0 & 0xF0) == 0xE0)
		{
			Len = 3;
			Cp = C0 & 0x0Fu;
		}
		else if((C0 & 0xF8) == 0xF0)
		{
			Len = 4;
			Cp = C0 & 0x07u;
		}
		else
		{
			++p;
			return 0xFFFD;
		}
		for(int i = 1; i < Len; ++i)
		{
			const unsigned char C = (unsigned char)p[i];
			if((C & 0xC0) != 0x80)
			{
				++p;
				return 0xFFFD;
			}
			Cp = (Cp << 6) | (C & 0x3Fu);
		}
		p += Len;
		return Cp;
	}

	ColorRGBA RainbowAt(float Time)
	{
		const float Hue = std::fmod(Time * 0.15f, 1.0f);
		return color_cast<ColorRGBA>(ColorHSLA(Hue, 0.7f, 0.65f, 1.0f));
	}

	// 有界子串查找：manifest 很大，不能用依赖 NUL 终止的 str_find
	const char *FindSpan(const char *pBegin, const char *pEnd, const char *pNeedle)
	{
		const size_t NeedleLen = str_length(pNeedle);
		if(NeedleLen == 0 || pBegin == nullptr || pEnd == nullptr || (size_t)(pEnd - pBegin) < NeedleLen)
			return nullptr;
		const char *pLast = pEnd - NeedleLen;
		for(const char *p = pBegin; p <= pLast; ++p)
		{
			if(p[0] == pNeedle[0] && mem_comp(p, pNeedle, NeedleLen) == 0)
				return p;
		}
		return nullptr;
	}

	// 在 [pBegin,pEnd) 中定位 key（形如 "key"），返回其后冒号的位置
	const char *FindKey(const char *pBegin, const char *pEnd, const char *pKey)
	{
		const char *p = FindSpan(pBegin, pEnd, pKey);
		if(p == nullptr)
			return nullptr;
		return p + str_length(pKey);
	}

	bool ReadDouble(const char *p, const char *pEnd, double &Out)
	{
		char aBuffer[64];
		size_t i = 0;
		while(p < pEnd && IsSpace((unsigned char)*p))
			++p;
		while(p < pEnd && i + 1 < sizeof(aBuffer) && (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9') || *p == 'e' || *p == 'E'))
		{
			aBuffer[i++] = *p++;
		}
		aBuffer[i] = '\0';
		if(i == 0)
			return false;
		Out = strtod(aBuffer, nullptr);
		return true;
	}

	bool ReadBool(const char *p, const char *pEnd, bool &Out)
	{
		while(p < pEnd && IsSpace((unsigned char)*p))
			++p;
		if(p + 4 <= pEnd && strncmp(p, "true", 4) == 0)
		{
			Out = true;
			return true;
		}
		if(p + 5 <= pEnd && strncmp(p, "false", 5) == 0)
		{
			Out = false;
			return true;
		}
		return false;
	}
}

CQmNameplateMsdfRenderer::~CQmNameplateMsdfRenderer()
{
	Shutdown();
}

CQmNameplateMsdfRenderer &QmNameplateMsdf()
{
	static CQmNameplateMsdfRenderer s_Renderer;
	return s_Renderer;
}

void CQmNameplateMsdfRenderer::UnloadPages()
{
	for(SPage &Page : m_vPages)
	{
		if(m_pGraphics != nullptr && Page.m_Texture.IsValid())
			m_pGraphics->UnloadTexture(&Page.m_Texture);
		Page.m_Texture = IGraphics::CTextureHandle();
	}
	m_vPages.clear();
}

void CQmNameplateMsdfRenderer::Shutdown()
{
	UnloadPages();
	m_Glyphs.clear();
	m_Ready = false;
	// 刻意保留 m_InitAttempted：否则每帧的 EnsureInitialized 会在图集缺失/后端不支持时反复读盘重试
	m_NextInitAttempt = 0;
	m_RefEmPixels = 0.0f;
	m_Error.clear();
	m_pStorage = nullptr;
	m_pGraphics = nullptr;
}

void CQmNameplateMsdfRenderer::EnsureInitialized(IStorage *pStorage, IGraphics *pGraphics)
{
	if(m_Ready || m_FatalError)
		return;
	// 失败退避：图集缺失或损坏时不要每帧重试读盘
	if(m_InitAttempted && time_get() < m_NextInitAttempt)
		return;
	m_InitAttempted = false;
	if(!Init(pStorage, pGraphics))
		m_NextInitAttempt = time_get() + time_freq() * 10;
}

bool CQmNameplateMsdfRenderer::Init(IStorage *pStorage, IGraphics *pGraphics)
{
	if(m_InitAttempted)
		return m_Ready;
	m_InitAttempted = true;
	m_pStorage = pStorage;
	m_pGraphics = pGraphics;
	if(m_pStorage == nullptr || m_pGraphics == nullptr)
	{
		m_Error = "storage or graphics unavailable";
		return false;
	}
	if(!m_pGraphics->HasTexturedMsdf())
	{
		// 该后端不支持 MSDF 管线（例如 GLES），静默回退到 FreeType 名牌
		m_Error = "backend has no textured MSDF pipeline";
		m_FatalError = true;
		log_info("nameplate_msdf", "TexturedMsdf unsupported, nameplate MSDF disabled");
		return false;
	}

	if(!LoadPage(kManifestBase))
	{
		log_error("nameplate_msdf", "failed to load base atlas: %s", m_Error.c_str());
		Shutdown();
		return false;
	}
	// CJK 页是可选的：缺失时仍可用基础脚本渲染
	if(!LoadPage(kManifestCjk))
		log_info("nameplate_msdf", "CJK page not loaded (%s); CJK names fall back to FreeType", m_Error.c_str());

	m_Ready = !m_vPages.empty() && !m_Glyphs.empty();
	if(m_Ready)
	{
		log_info("nameplate_msdf", "Nameplate MSDF ready: %zu page(s), %zu glyphs, refEm=%.0f pxRange=%.1f",
			m_vPages.size(), m_Glyphs.size(), m_RefEmPixels, m_vPages.empty() ? 0.0f : m_vPages[0].m_PxRange);
	}
	else
	{
		Shutdown();
	}
	return m_Ready;
}

bool CQmNameplateMsdfRenderer::LoadPage(const char *pManifestPath)
{
	void *pData = nullptr;
	unsigned Size = 0;
	if(!m_pStorage->ReadFile(pManifestPath, IStorage::TYPE_ALL, &pData, &Size))
	{
		m_Error = "manifest missing: ";
		m_Error += pManifestPath;
		return false;
	}
	std::string Text((const char *)pData, Size);
	free(pData);
	return ParseManifest(Text.c_str(), pManifestPath);
}

bool CQmNameplateMsdfRenderer::ParseManifest(const char *pText, const std::string &ManifestPath)
{
	const char *pEnd = pText + str_length(pText);

	const char *pPxRange = FindKey(pText, pEnd, "px_range");
	const char *pEmPixels = FindKey(pText, pEnd, "em_pixels");
	const char *pImage = FindKey(pText, pEnd, "image");
	double PxRange = 0.0;
	double EmPixels = 0.0;
	if(pPxRange == nullptr || pEmPixels == nullptr || pImage == nullptr ||
		!ReadDouble(pPxRange, pEnd, PxRange) || !ReadDouble(pEmPixels, pEnd, EmPixels) || PxRange <= 0.0 || EmPixels <= 0.0)
	{
		m_Error = "manifest malformed: ";
		m_Error += ManifestPath;
		return false;
	}

	// image 字段为相对 data/ 的路径，取其文件名与本 manifest 同目录拼接
	std::string ImageName(pImage, pEnd);
	{
		const size_t End = ImageName.find('"');
		if(End != std::string::npos)
			ImageName.resize(End);
		const size_t Slash = ImageName.find_last_of('/');
		if(Slash != std::string::npos)
			ImageName = ImageName.substr(Slash + 1);
	}
	const size_t DirEnd = ManifestPath.find_last_of('/');
	const std::string ImagePath = (DirEnd == std::string::npos ? std::string() : ManifestPath.substr(0, DirEnd + 1)) + ImageName;

	IGraphics::CTextureHandle Texture = m_pGraphics->LoadTexture(ImagePath.c_str(), IStorage::TYPE_ALL, IGraphics::TEXLOAD_NO_MIPMAPS);
	if(!Texture.IsValid())
	{
		m_Error = "atlas image missing: ";
		m_Error += ImagePath;
		return false;
	}

	const char *pAtlasKey = FindKey(pText, pEnd, "\"atlas\"");
	if(pAtlasKey == nullptr)
	{
		m_Error = "manifest has no atlas block: ";
		m_Error += ManifestPath;
		return false;
	}
	// atlas 块内的 width/height 紧跟其后，避免误取 glyphs 里的同名字段
	const char *pAtlasEnd = pAtlasKey;
	while(pAtlasEnd < pEnd && *pAtlasEnd != '}')
		++pAtlasEnd;
	const char *pWidth = FindKey(pAtlasKey, pAtlasEnd, "width");
	const char *pHeight = FindKey(pAtlasKey, pAtlasEnd, "height");
	double AtlasWidth = 0.0;
	double AtlasHeight = 0.0;
	if(pWidth == nullptr || pHeight == nullptr ||
		!ReadDouble(pWidth, pAtlasEnd, AtlasWidth) || !ReadDouble(pHeight, pAtlasEnd, AtlasHeight) ||
		AtlasWidth <= 0.0 || AtlasHeight <= 0.0)
	{
		m_Error = "manifest atlas block malformed: ";
		m_Error += ManifestPath;
		m_pGraphics->UnloadTexture(&Texture);
		return false;
	}

	const int PageIndex = (int)m_vPages.size();
	SPage Page;
	Page.m_Texture = Texture;
	Page.m_Info.m_ImageName = ImagePath;
	Page.m_Info.m_Width = (int)AtlasWidth;
	Page.m_Info.m_Height = (int)AtlasHeight;
	Page.m_PxRange = (float)PxRange;
	m_vPages.push_back(Page);
	if(m_RefEmPixels <= 0.0f)
		m_RefEmPixels = (float)EmPixels;

	// glyphs 块：逐个解析 "<codepoint>": { ... }
	const char *pGlyphs = FindKey(pText, pEnd, "\"glyphs\"");
	if(pGlyphs == nullptr)
	{
		m_Error = "manifest has no glyphs block: ";
		m_Error += ManifestPath;
		return false;
	}
	const char *p = pGlyphs;
	size_t Parsed = 0;
	while(p < pEnd)
	{
		while(p < pEnd && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',' || *p == '{' || *p == '}'))
			++p;
		if(p >= pEnd || *p != '"')
			break;
		++p;
		char *pCodeEnd = nullptr;
		const long Codepoint = strtol(p, &pCodeEnd, 10);
		if(pCodeEnd == nullptr || pCodeEnd == p || Codepoint <= 0 || Codepoint > 0x10FFFF)
			break;
		p = pCodeEnd;
		const char *pObjectBegin = FindSpan(p, pEnd, "{");
		if(pObjectBegin == nullptr)
			break;
		const char *pObjectEnd = FindSpan(pObjectBegin, pEnd, "}");
		if(pObjectEnd == nullptr)
			break;

		SGlyph Glyph;
		Glyph.m_Page = PageIndex;
		double Value = 0.0;
		const char *pField = FindKey(pObjectBegin, pObjectEnd, "\"x\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_X = (int)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"y\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_Y = (int)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"w\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_W = (int)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"h\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_H = (int)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"adv\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_Advance = (float)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"bx\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_BearingX = (float)Value;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"by\"");
		if(pField != nullptr && ReadDouble(pField, pObjectEnd, Value))
			Glyph.m_BearingY = (float)Value;
		bool Outline = false;
		pField = FindKey(pObjectBegin, pObjectEnd, "\"outline\"");
		if(pField != nullptr && ReadBool(pField, pObjectEnd, Outline))
			Glyph.m_HasOutline = Outline;
		Glyph.m_Valid = true;

		// 同名码位跨页时以先加载的页为准（基础页优先），避免 CJK 页覆盖拉丁字形
		m_Glyphs.emplace((uint32_t)Codepoint, Glyph);
		++Parsed;
		p = pObjectEnd + 1;
	}
	if(Parsed == 0)
	{
		// 回滚本页，保持 m_vPages 与 m_Glyphs 一致
		SPage &Back = m_vPages.back();
		if(Back.m_Texture.IsValid())
			m_pGraphics->UnloadTexture(&Back.m_Texture);
		m_vPages.pop_back();
		m_Error = "manifest glyphs block empty: ";
		m_Error += ManifestPath;
		return false;
	}
	return true;
}

bool CQmNameplateMsdfRenderer::SupportsText(const char *pText) const
{
	if(!m_Ready || pText == nullptr)
		return false;
	const char *p = pText;
	while(*p != '\0')
	{
		const uint32_t Cp = DecodeUtf8(p);
		if(Cp == 0)
			break;
		// 换行与制表符按空白处理，不需要字形
		if(Cp == '\n' || Cp == '\r' || Cp == '\t')
			continue;
		if(m_Glyphs.find(Cp) == m_Glyphs.end())
			return false;
	}
	return true;
}

vec2 CQmNameplateMsdfRenderer::Measure(const char *pText, float FontSize) const
{
	if(!m_Ready || pText == nullptr || FontSize <= 0.0f)
		return vec2(0.0f, 0.0f);
	const float Scale = FontSize / m_RefEmPixels;
	const char *p = pText;
	float Width = 0.0f;
	float MaxY = 0.0f;
	float MinY = 0.0f;
	while(*p != '\0')
	{
		const uint32_t Cp = DecodeUtf8(p);
		if(Cp == 0)
			break;
		auto It = m_Glyphs.find(Cp);
		if(It == m_Glyphs.end())
			continue;
		const SGlyph &Glyph = It->second;
		Width += Glyph.m_Advance * Scale;
		if(Glyph.m_HasOutline)
		{
			// 字形顶相对基线的偏移（by - pxRange）为负值，底为 by - pxRange + h
			const float Top = (Glyph.m_BearingY - m_vPages[Glyph.m_Page].m_PxRange) * Scale;
			const float Bottom = Top + Glyph.m_H * Scale;
			MinY = std::min(MinY, Top);
			MaxY = std::max(MaxY, Bottom);
		}
	}
	return vec2(Width, MaxY - MinY);
}

void CQmNameplateMsdfRenderer::EmitGlyphQuad(const SGlyph &Glyph, float X, float Y, float W, float H, const ColorRGBA &Color)
{
	const SPage &Page = m_vPages[Glyph.m_Page];
	IGraphics::STexturedMsdfParams Params;
	Params.m_Texture = Page.m_Texture;
	Params.m_Rect = vec4(X, Y, W, H);
	Params.m_UvRect = vec4(
		(float)Glyph.m_X / (float)Page.m_Info.m_Width,
		(float)Glyph.m_Y / (float)Page.m_Info.m_Height,
		(float)(Glyph.m_X + Glyph.m_W) / (float)Page.m_Info.m_Width,
		(float)(Glyph.m_Y + Glyph.m_H) / (float)Page.m_Info.m_Height);
	Params.m_Color = Color;
	Params.m_PxRange = Page.m_PxRange;
	Params.m_AtlasWidth = (float)Page.m_Info.m_Width;
	Params.m_AtlasHeight = (float)Page.m_Info.m_Height;
	m_pGraphics->RenderTexturedMsdf(Params);
}

vec2 CQmNameplateMsdfRenderer::Draw(const char *pText, float X, float Y, float FontSize, const SQmNameplateMsdfTextStyle &Style)
{
	if(!m_Ready || pText == nullptr || FontSize <= 0.0f || m_RefEmPixels <= 0.0f)
		return vec2(0.0f, 0.0f);
	if(Style.m_TextColor.a <= 0.0f && Style.m_OutlineColor.a <= 0.0f)
		return vec2(0.0f, 0.0f);

	// 先收集字形，确保整条文本都可渲染（调用方负责整名回退判定，这里再兜一层）
	struct SPlaced
	{
		const SGlyph *m_pGlyph = nullptr;
		float m_PenX = 0.0f;
	};
	std::vector<SPlaced> vPlaced;
	vPlaced.reserve(str_length(pText));
	{
		const char *p = pText;
		float PenX = X;
		while(*p != '\0')
		{
			const uint32_t Cp = DecodeUtf8(p);
			if(Cp == 0)
				break;
			if(Cp == '\n' || Cp == '\r')
				break; // 名牌为单行文本，不支持换行
			if(Cp == '\t')
			{
				PenX += FontSize * 0.5f;
				continue;
			}
			auto It = m_Glyphs.find(Cp);
			if(It == m_Glyphs.end())
				return vec2(0.0f, 0.0f);
			if(It->second.m_HasOutline)
				vPlaced.push_back({&It->second, PenX});
			PenX += It->second.m_Advance * (FontSize / m_RefEmPixels);
		}
	}

	const float Scale = FontSize / m_RefEmPixels;
	// 基线必须由真实度量推出，不能写死 em 比例：字形顶相对基线为 (bearingY - pxRange)，
	// 取本串最大值即可让整串墨迹顶端正好落在 Y，与 Measure() 的包围盒口径一致（否则垂直居中会偏）。
	float MaxTop = 0.0f;
	for(const SPlaced &Placed : vPlaced)
		MaxTop = std::max(MaxTop, (Placed.m_pGlyph->m_BearingY - m_vPages[Placed.m_pGlyph->m_Page].m_PxRange) * Scale);
	const float Baseline = Y - MaxTop;

	// 描边：8 向偏移 + 按宽度分档加重
	if(Style.m_OutlineColor.a > 0.0f && Style.m_OutlineWidth > 0.5f)
	{
		const int Passes = std::clamp((int)std::lround(Style.m_OutlineWidth), 1, 4);
		for(int Pass = 1; Pass <= Passes; ++Pass)
		{
			const float Radius = (float)Pass;
			const float PassAlpha = Style.m_OutlineColor.a / (float)Passes;
			const ColorRGBA Outline = Style.m_OutlineColor.WithAlpha(PassAlpha);
			for(const auto &Dir : s_aOffsetDirs)
			{
				for(const SPlaced &Placed : vPlaced)
				{
					const SGlyph &Glyph = *Placed.m_pGlyph;
					EmitGlyphQuad(Glyph,
						Placed.m_PenX + (Glyph.m_BearingX - m_vPages[Glyph.m_Page].m_PxRange) * Scale + Dir[0] * Radius,
						Baseline + (Glyph.m_BearingY - m_vPages[Glyph.m_Page].m_PxRange) * Scale + Dir[1] * Radius,
						Glyph.m_W * Scale, Glyph.m_H * Scale, Outline);
				}
			}
		}
	}

	// 主填充
	for(const SPlaced &Placed : vPlaced)
	{
		const SGlyph &Glyph = *Placed.m_pGlyph;
		const SPage &Page = m_vPages[Glyph.m_Page];
		ColorRGBA Color = Style.m_TextColor;
		if(Style.m_RainbowEnabled)
		{
			const ColorRGBA Rb = RainbowAt(Style.m_RainbowTime + Placed.m_PenX * 0.01f);
			Color = ColorRGBA(Rb.r, Rb.g, Rb.b, Color.a);
		}
		EmitGlyphQuad(Glyph,
			Placed.m_PenX + (Glyph.m_BearingX - Page.m_PxRange) * Scale,
			Baseline + (Glyph.m_BearingY - Page.m_PxRange) * Scale,
			Glyph.m_W * Scale, Glyph.m_H * Scale, Color);
	}

	return Measure(pText, FontSize);
}

vec2 CQmNameplateMsdfRenderer::DrawCentered(const char *pText, float CenterX, float CenterY, float FontSize, const SQmNameplateMsdfTextStyle &Style)
{
	const vec2 Size = Measure(pText, FontSize);
	return Draw(pText, CenterX - Size.x * 0.5f, CenterY - Size.y * 0.5f, FontSize, Style);
}
