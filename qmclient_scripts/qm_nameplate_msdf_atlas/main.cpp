// QmClient 名牌 MSDF 字形图集生成工具（离线资源制作，不参与客户端构建）。
//
// 流程：FreeType 取字形轮廓 → msdfgen core 生成多通道距离场 → shelf 打包进单页图集
//      → 输出原始 RGBA（由 Python 侧编码 PNG）+ JSON manifest。
//
// 关键约定（均由探针实测确认，勿随意改动）：
//  1. 轮廓在「形状空间」里就把 y 翻成向下（y' = bearingY - y），投影保持正缩放。
//     若改用负 y 缩放或用 Y_DOWNWARD 位图，BitmapRef::reorient 会重排行序导致字形上下颠倒。
//  2. 翻 y 会反转环绕方向，之后必须调用 Shape::orientContours()，否则整个距离场极性相反
//     （字形本体变成透明、外部变成实心）。
//  3. 输出满足 map = (signedDistance + pxRange/2) / pxRange，内侧为负。
//     与 data/shader/textured_msdf.frag 的 median(rgb) - 0.5 > 0 填充判据一致。
#include <ft2build.h>
#include <msdfgen.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	struct SGlyphEntry
	{
		uint32_t m_Codepoint = 0;
		int m_X = 0;
		int m_Y = 0;
		int m_W = 0;
		int m_H = 0;
		// 相对参考 em 的度量，单位：参考 em 像素
		double m_AdvanceX = 0.0;
		double m_BearingX = 0.0;
		double m_BearingY = 0.0;
		bool m_HasOutline = false;
	};

	struct SOptions
	{
		std::string m_FontPath;
		std::string m_CharsetPath;
		std::string m_OutputPrefix;
		int m_EmPixels = 48;
		double m_PxRange = 6.0;
		int m_Size = 4096;
		int m_MaxGlyphs = 0;
		unsigned m_FontIndex = 0;
	};

	struct SOutlineContext
	{
		msdfgen::Shape *m_pShape = nullptr;
		msdfgen::Contour *m_pContour = nullptr;
		msdfgen::Point2 m_Last;
		double m_Scale = 1.0;
		double m_FlipTop = 0.0;
	};

	msdfgen::Point2 FlipPoint(const FT_Vector *p, const SOutlineContext *pCtx)
	{
		const double X = p->x * pCtx->m_Scale;
		const double Y = p->y * pCtx->m_Scale;
		return msdfgen::Point2(X, pCtx->m_FlipTop - Y);
	}

	int MoveTo(const FT_Vector *pTo, void *pUser)
	{
		auto *pCtx = static_cast<SOutlineContext *>(pUser);
		pCtx->m_Last = FlipPoint(pTo, pCtx);
		pCtx->m_pContour = &pCtx->m_pShape->addContour();
		return 0;
	}

	int LineTo(const FT_Vector *pTo, void *pUser)
	{
		auto *pCtx = static_cast<SOutlineContext *>(pUser);
		pCtx->m_pContour->addEdge(msdfgen::EdgeHolder(pCtx->m_Last, FlipPoint(pTo, pCtx)));
		pCtx->m_Last = FlipPoint(pTo, pCtx);
		return 0;
	}

	int ConicTo(const FT_Vector *pControl, const FT_Vector *pTo, void *pUser)
	{
		auto *pCtx = static_cast<SOutlineContext *>(pUser);
		pCtx->m_pContour->addEdge(msdfgen::EdgeHolder(pCtx->m_Last, FlipPoint(pControl, pCtx), FlipPoint(pTo, pCtx)));
		pCtx->m_Last = FlipPoint(pTo, pCtx);
		return 0;
	}

	int CubicTo(const FT_Vector *pControl1, const FT_Vector *pControl2, const FT_Vector *pTo, void *pUser)
	{
		auto *pCtx = static_cast<SOutlineContext *>(pUser);
		pCtx->m_pContour->addEdge(msdfgen::EdgeHolder(pCtx->m_Last, FlipPoint(pControl1, pCtx), FlipPoint(pControl2, pCtx), FlipPoint(pTo, pCtx)));
		pCtx->m_Last = FlipPoint(pTo, pCtx);
		return 0;
	}

	void Usage()
	{
		printf("usage: qm-nameplate-msdf-atlas --font <path> [--font-index N] --charset <file> --output <prefix>\n"
		       "       [--em-pixels 48] [--px-range 6] [--size 4096] [--max-glyphs N]\n"
		       "charset file: one U+XXXX per line (blank lines and # comments ignored)\n");
	}

	bool ParseArgs(int argc, char **argv, SOptions &Options)
	{
		for(int i = 1; i < argc; ++i)
		{
			const char *pArg = argv[i];
			auto Next = [&](const char *pName) -> const char * {
				if(i + 1 >= argc)
				{
					printf("missing value for %s\n", pName);
					return nullptr;
				}
				return argv[++i];
			};
			if(strcmp(pArg, "--font") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_FontPath = pValue;
			}
			else if(strcmp(pArg, "--font-index") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_FontIndex = (unsigned)strtoul(pValue, nullptr, 10);
			}
			else if(strcmp(pArg, "--charset") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_CharsetPath = pValue;
			}
			else if(strcmp(pArg, "--output") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_OutputPrefix = pValue;
			}
			else if(strcmp(pArg, "--em-pixels") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_EmPixels = (int)strtol(pValue, nullptr, 10);
			}
			else if(strcmp(pArg, "--px-range") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_PxRange = strtod(pValue, nullptr);
			}
			else if(strcmp(pArg, "--size") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_Size = (int)strtol(pValue, nullptr, 10);
			}
			else if(strcmp(pArg, "--max-glyphs") == 0)
			{
				const char *pValue = Next(pArg);
				if(pValue == nullptr)
					return false;
				Options.m_MaxGlyphs = (int)strtol(pValue, nullptr, 10);
			}
			else
			{
				printf("unknown argument: %s\n", pArg);
				return false;
			}
		}
		return !Options.m_FontPath.empty() && !Options.m_CharsetPath.empty() && !Options.m_OutputPrefix.empty() &&
		       Options.m_EmPixels > 0 && Options.m_PxRange > 0.0 && Options.m_Size > 0;
	}

	bool ReadCharset(const char *pPath, std::vector<uint32_t> &vCodepoints)
	{
		FILE *pFile = fopen(pPath, "rb");
		if(pFile == nullptr)
		{
			printf("cannot open charset file: %s\n", pPath);
			return false;
		}
		char aLine[256];
		while(fgets(aLine, sizeof(aLine), pFile) != nullptr)
		{
			char *p = aLine;
			while(*p == ' ' || *p == '\t')
				++p;
			if(*p == '#' || *p == '\r' || *p == '\n' || *p == '\0')
				continue;
			unsigned long Value = 0;
			if(sscanf(p, "U+%lx", &Value) != 1 && sscanf(p, "%lx", &Value) != 1)
				continue;
			if(Value == 0 || Value > 0x10FFFFul)
				continue;
			vCodepoints.push_back((uint32_t)Value);
		}
		fclose(pFile);
		std::sort(vCodepoints.begin(), vCodepoints.end());
		// 保持调用方给定的优先级顺序：图集放不下时先满足靠前的字形，因此这里只去重不排序
		vCodepoints.erase(std::unique(vCodepoints.begin(), vCodepoints.end()), vCodepoints.end());
		return !vCodepoints.empty();
	}

	// shelf 打包：按行高降序放置，行内从左到右。返回 false 表示放不下。
	struct SShelf
	{
		int m_Y = 0;
		int m_Height = 0;
		int m_UsedX = 0;
	};

	bool PackTile(std::vector<SShelf> &vShelves, int Size, int Padding, int W, int H, int &OutX, int &OutY)
	{
		const int TileW = W + Padding * 2;
		const int TileH = H + Padding * 2;
		if(TileW > Size || TileH > Size)
			return false;
		for(SShelf &Shelf : vShelves)
		{
			if(Shelf.m_Height < TileH)
				continue;
			if(Shelf.m_UsedX + TileW > Size)
				continue;
			OutX = Shelf.m_UsedX + Padding;
			OutY = Shelf.m_Y + Padding + (Shelf.m_Height - TileH) / 2;
			Shelf.m_UsedX += TileW;
			return true;
		}
		// 新开一行
		int NextY = 0;
		for(const SShelf &Shelf : vShelves)
			NextY = std::max(NextY, Shelf.m_Y + Shelf.m_Height);
		if(NextY + TileH > Size)
			return false;
		SShelf Shelf;
		Shelf.m_Y = NextY;
		Shelf.m_Height = TileH;
		Shelf.m_UsedX = TileW;
		vShelves.push_back(Shelf);
		OutX = Padding;
		OutY = NextY + Padding;
		return true;
	}
}

int main(int argc, char **argv)
{
	SOptions Options;
	if(!ParseArgs(argc, argv, Options))
	{
		Usage();
		return 2;
	}

	std::vector<uint32_t> vCodepoints;
	if(!ReadCharset(Options.m_CharsetPath.c_str(), vCodepoints))
	{
		printf("charset is empty or unreadable\n");
		return 1;
	}

	FT_Library Library = nullptr;
	if(FT_Init_FreeType(&Library) != 0)
	{
		printf("FT_Init_FreeType failed\n");
		return 1;
	}
	FT_Face Face = nullptr;
	if(FT_New_Face(Library, Options.m_FontPath.c_str(), (FT_Long)Options.m_FontIndex, &Face) != 0)
	{
		printf("FT_New_Face failed: %s\n", Options.m_FontPath.c_str());
		FT_Done_FreeType(Library);
		return 1;
	}
	const bool Scalable = FT_IS_SCALABLE(Face) != 0;
	if(!Scalable)
	{
		printf("font is not scalable: %s\n", Options.m_FontPath.c_str());
		FT_Done_Face(Face);
		FT_Done_FreeType(Library);
		return 1;
	}
	if(FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)Options.m_EmPixels) != 0)
	{
		printf("FT_Set_Pixel_Sizes failed\n");
		FT_Done_Face(Face);
		FT_Done_FreeType(Library);
		return 1;
	}

	const int Size = Options.m_Size;
	const int Padding = (int)std::ceil(Options.m_PxRange) + 1;
	std::vector<uint8_t> vPage((size_t)Size * (size_t)Size * 4u, 0u);
	std::vector<SShelf> vShelves;
	std::vector<SGlyphEntry> vEntries;
	vEntries.reserve(vCodepoints.size());

	int Generated = 0;
	int Skipped = 0;
	for(const uint32_t Codepoint : vCodepoints)
	{
		if(Options.m_MaxGlyphs > 0 && Generated >= Options.m_MaxGlyphs)
			break;

		const FT_UInt Index = FT_Get_Char_Index(Face, (FT_ULong)Codepoint);
		if(Index == 0)
		{
			++Skipped;
			continue;
		}
		// 不用 hinting：MSDF 需要原始轮廓，hinting 会把曲线打成折线
		if(FT_Load_Glyph(Face, Index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0)
		{
			++Skipped;
			continue;
		}
		const FT_GlyphSlot Slot = Face->glyph;

		SGlyphEntry Entry;
		Entry.m_Codepoint = Codepoint;
		Entry.m_AdvanceX = Slot->metrics.horiAdvance / 64.0;
		Entry.m_BearingX = Slot->metrics.horiBearingX / 64.0;
		Entry.m_BearingY = Slot->metrics.horiBearingY / 64.0;

		if(Slot->format != FT_GLYPH_FORMAT_OUTLINE || Slot->outline.n_points == 0)
		{
			// 空白字形（空格等）：只记 advance
			vEntries.push_back(Entry);
			++Generated;
			continue;
		}

		msdfgen::Shape Shape;
		SOutlineContext Ctx;
		Ctx.m_pShape = &Shape;
		Ctx.m_Scale = 1.0 / 64.0;
		Ctx.m_FlipTop = Entry.m_BearingY;
		FT_Outline_Funcs Funcs{};
		Funcs.move_to = MoveTo;
		Funcs.line_to = LineTo;
		Funcs.conic_to = ConicTo;
		Funcs.cubic_to = CubicTo;
		FT_Outline_Decompose(&Slot->outline, &Funcs, &Ctx);

		if(Shape.contours.empty())
		{
			vEntries.push_back(Entry);
			++Generated;
			continue;
		}
		// 翻转 y 后必须重新定向轮廓，否则极性相反
		Shape.orientContours();
		Shape.normalize();
		msdfgen::edgeColoringSimple(Shape, 3.0);

		const msdfgen::Shape::Bounds Bounds = Shape.getBounds();
		if(!(Bounds.r > Bounds.l) || !(Bounds.t > Bounds.b))
		{
			vEntries.push_back(Entry);
			++Generated;
			continue;
		}

		const int W = (int)std::ceil(Bounds.r - Bounds.l + 2.0 * Options.m_PxRange);
		const int H = (int)std::ceil(Bounds.t - Bounds.b + 2.0 * Options.m_PxRange);
		int PosX = 0;
		int PosY = 0;
		if(!PackTile(vShelves, Size, Padding, W, H, PosX, PosY))
		{
			// 单页放不下就停下：由调用方决定缩小字符集或放大页面
			printf("atlas full at U+%04X (%d glyphs packed)\n", Codepoint, Generated);
			break;
		}

		msdfgen::Bitmap<float, 3> Msdf(W, H, msdfgen::Y_UPWARD);
		// 形状已是 y 向下，正缩放 + 边距平移
		msdfgen::Projection Projection(msdfgen::Vector2(1.0, 1.0), msdfgen::Vector2(Options.m_PxRange - Bounds.l, Options.m_PxRange - Bounds.b));
		msdfgen::MSDFGeneratorConfig Config;
		msdfgen::generateMSDF(Msdf, Shape, Projection, msdfgen::Range(Options.m_PxRange), Config);

		for(int y = 0; y < H; ++y)
		{
			for(int x = 0; x < W; ++x)
			{
				const float *p = Msdf(x, y);
				const size_t Dst = ((size_t)(PosY + y) * (size_t)Size + (size_t)(PosX + x)) * 4u;
				for(int c = 0; c < 3; ++c)
				{
					const double Value = p[c];
					const double Clamped = Value < 0.0 ? 0.0 : (Value > 1.0 ? 1.0 : Value);
					vPage[Dst + (size_t)c] = (uint8_t)(Clamped * 255.0 + 0.5);
				}
				vPage[Dst + 3] = 255u;
			}
		}

		Entry.m_X = PosX;
		Entry.m_Y = PosY;
		Entry.m_W = W;
		Entry.m_H = H;
		Entry.m_HasOutline = true;
		vEntries.push_back(Entry);
		++Generated;
	}

	// 原始 RGBA 页
	const std::string PagePath = Options.m_OutputPrefix + ".rgba";
	FILE *pPage = fopen(PagePath.c_str(), "wb");
	if(pPage == nullptr)
	{
		printf("cannot write %s\n", PagePath.c_str());
		FT_Done_Face(Face);
		FT_Done_FreeType(Library);
		return 1;
	}
	fwrite(vPage.data(), 1u, vPage.size(), pPage);
	fclose(pPage);

	// manifest：度量按参考 em 像素记录，运行时按目标字号线性缩放
	const std::string ManifestPath = Options.m_OutputPrefix + ".json";
	FILE *pManifest = fopen(ManifestPath.c_str(), "wb");
	if(pManifest == nullptr)
	{
		printf("cannot write %s\n", ManifestPath.c_str());
		FT_Done_Face(Face);
		FT_Done_FreeType(Library);
		return 1;
	}
	// 只记录文件名：路径可能含反斜杠，写进 JSON 会造成非法转义（调用方补全 image 字段）
	std::string FontName = Options.m_FontPath;
	const size_t Slash = FontName.find_last_of("/\\");
	if(Slash != std::string::npos)
		FontName = FontName.substr(Slash + 1);
	fprintf(pManifest, "{\n");
	fprintf(pManifest, "  \"version\": 1,\n");
	fprintf(pManifest, "  \"kind\": \"msdf-glyphs\",\n");
	fprintf(pManifest, "  \"px_range\": %g,\n", Options.m_PxRange);
	fprintf(pManifest, "  \"em_pixels\": %d,\n", Options.m_EmPixels);
	fprintf(pManifest, "  \"padding\": %d,\n", Padding);
	fprintf(pManifest, "  \"source_font\": \"%s\",\n", FontName.c_str());
	fprintf(pManifest, "  \"atlas\": {\"width\": %d, \"height\": %d},\n", Size, Size);
	fprintf(pManifest, "  \"glyphs\": {\n");
	for(size_t i = 0; i < vEntries.size(); ++i)
	{
		const SGlyphEntry &Entry = vEntries[i];
		fprintf(pManifest,
			"    \"%u\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"adv\": %.4f, \"bx\": %.4f, \"by\": %.4f, \"outline\": %s}%s\n",
			Entry.m_Codepoint, Entry.m_X, Entry.m_Y, Entry.m_W, Entry.m_H,
			Entry.m_AdvanceX, Entry.m_BearingX, Entry.m_BearingY,
			Entry.m_HasOutline ? "true" : "false",
			i + 1u < vEntries.size() ? "," : "");
	}
	fprintf(pManifest, "  }\n");
	fprintf(pManifest, "}\n");
	fclose(pManifest);

	const size_t Requested = vCodepoints.size();
	printf("packed %d of %zu requested glyphs (missing %d, oversize-or-full %d, em=%d pxRange=%g size=%d padding=%d)\n",
		Generated, Requested, Skipped, (int)(Requested - (size_t)Generated - (size_t)Skipped),
		Options.m_EmPixels, Options.m_PxRange, Size, Padding);

	FT_Done_Face(Face);
	FT_Done_FreeType(Library);
	return 0;
}
