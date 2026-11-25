#include <ft2build.h>
#include FT_FREETYPE_H

#include <ext/import-font.h>
#include <msdfgen.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

int main(int argc, char **argv)
{
	if(argc < 6 || (argc - 3) % 3 != 0)
	{
		std::fprintf(stderr, "Usage: %s atlas.rgb rows.txt [font face_index codepoints.txt]...\n", argv[0]);
		return 1;
	}
	constexpr int SIZE = 2048;
	constexpr int EM = 32;
	constexpr int PADDING = 6;
	constexpr double PX_RANGE = 8.0;
	std::vector<unsigned char> vPixels(SIZE * SIZE * 3);
	std::ofstream Rows(argv[2]);
	if(!Rows)
		return 1;
	FT_Library Library = nullptr;
	if(FT_Init_FreeType(&Library))
		return 1;
	const std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(&FT_Done_FreeType)> pLibrary(Library, FT_Done_FreeType);
	int X = 1;
	int Y = 1;
	int RowHeight = 0;
	for(int Arg = 3; Arg < argc; Arg += 3)
	{
		FT_Face Face = nullptr;
		if(FT_New_Face(Library, argv[Arg], std::stoi(argv[Arg + 1]), &Face) || FT_Select_Charmap(Face, FT_ENCODING_UNICODE))
			return 1;
		const std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)> pFace(Face, FT_Done_Face);
		const std::unique_ptr<msdfgen::FontHandle, decltype(&msdfgen::destroyFont)> pFont(msdfgen::adoptFreetypeFont(Face), msdfgen::destroyFont);
		std::ifstream Codepoints(argv[Arg + 2]);
		if(!Codepoints || !pFont)
			return 1;
		const int Font = (Arg - 3) / 3;
		unsigned Codepoint;
		while(Codepoints >> Codepoint)
		{
			msdfgen::GlyphIndex Index;
			msdfgen::Shape Shape;
			if(!msdfgen::getGlyphIndex(Index, pFont.get(), Codepoint) || !msdfgen::loadGlyph(Shape, pFont.get(), Index, msdfgen::FONT_SCALING_EM_NORMALIZED))
				continue;
			if(Shape.edgeCount() == 0)
			{
				Rows << Font << ' ' << Codepoint << " 0 0 0 0 0 0\n";
				continue;
			}
			Shape.normalize();
			Shape.orientContours();
			msdfgen::edgeColoringInkTrap(Shape, 3.0);
			const auto Bounds = Shape.getBounds();
			const int Left = static_cast<int>(std::floor(Bounds.l * EM)) - PADDING;
			const int Bottom = static_cast<int>(std::floor(Bounds.b * EM)) - PADDING;
			const int Width = static_cast<int>(std::ceil(Bounds.r * EM)) + PADDING - Left;
			const int Height = static_cast<int>(std::ceil(Bounds.t * EM)) + PADDING - Bottom;
			if(X + Width >= SIZE)
			{
				X = 1;
				Y += RowHeight + 1;
				RowHeight = 0;
			}
			if(Y + Height >= SIZE || X + Width >= SIZE)
			{
				std::fprintf(stderr, "Atlas exhausted at U+%04X\n", Codepoint);
				return 1;
			}
			msdfgen::Bitmap<float, 3> Bitmap(Width, Height);
			const msdfgen::Projection Projection(msdfgen::Vector2(EM), msdfgen::Vector2(-double(Left) / EM, -double(Bottom) / EM));
			msdfgen::generateMSDF(Bitmap, Shape, Projection, msdfgen::Range(PX_RANGE / EM));
			for(int Row = 0; Row < Height; ++Row)
			{
				for(int Col = 0; Col < Width; ++Col)
				{
					const float *pPixel = Bitmap(Col, Height - Row - 1);
					const size_t Offset = (static_cast<size_t>(Y + Row) * SIZE + X + Col) * 3;
					for(int Channel = 0; Channel < 3; ++Channel)
						vPixels[Offset + Channel] = static_cast<unsigned char>(std::clamp(pPixel[Channel], 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}
			Rows << Font << ' ' << Codepoint << ' ' << X << ' ' << Y << ' ' << Width << ' ' << Height << ' ' << Left << ' ' << Bottom + Height << '\n';
			X += Width + 1;
			RowHeight = std::max(RowHeight, Height);
		}
		if(!Codepoints.eof())
			return 1;
	}
	std::ofstream Pixels(argv[1], std::ios::binary);
	Pixels.write(reinterpret_cast<const char *>(vPixels.data()), vPixels.size());
	return Pixels && Rows ? 0 : 1;
}
