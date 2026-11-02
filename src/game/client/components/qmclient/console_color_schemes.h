#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_COLOR_SCHEMES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_COLOR_SCHEMES_H

#include <base/color.h>

#include <array>

// 控制台配色方案库。
//
// 色值取自 Windows Terminal 内置配色（TerminalSettingsModel/defaults.json 的
// schemes 数组），顺序与那里的 ANSI 定义一致：0-7 常规、8-15 亮色。
// 这里只保存 sRGB 原始值，不做线性化：控制台渲染走的仍是同一条 ColorRGBA
// 路径，和原有手写方案保持可比。
namespace QmConsoleSchemes
{
	struct SAnsiPalette
	{
		std::array<ColorRGBA, 16> m_aColors;
		ColorRGBA m_Foreground;
		ColorRGBA m_Background;
	};

	struct SDefinition
	{
		const char *m_pName;
		SAnsiPalette m_Palette;
	};

	// 0xRRGGBB -> 不透明 ColorRGBA。方案表只填十六进制色值，换算集中在这里。
	constexpr ColorRGBA Rgb(unsigned Rgb)
	{
		return ColorRGBA(((Rgb >> 16) & 0xFF) / 255.0f, ((Rgb >> 8) & 0xFF) / 255.0f, (Rgb & 0xFF) / 255.0f, 1.0f);
	}

	constexpr SAnsiPalette MakePalette(unsigned Foreground, unsigned Background,
		unsigned Black, unsigned Red, unsigned Green, unsigned Yellow,
		unsigned Blue, unsigned Purple, unsigned Cyan, unsigned White,
		unsigned BrightBlack, unsigned BrightRed, unsigned BrightGreen, unsigned BrightYellow,
		unsigned BrightBlue, unsigned BrightPurple, unsigned BrightCyan, unsigned BrightWhite)
	{
		return {{Rgb(Black), Rgb(Red), Rgb(Green), Rgb(Yellow), Rgb(Blue), Rgb(Purple), Rgb(Cyan), Rgb(White),
				Rgb(BrightBlack), Rgb(BrightRed), Rgb(BrightGreen), Rgb(BrightYellow),
				Rgb(BrightBlue), Rgb(BrightPurple), Rgb(BrightCyan), Rgb(BrightWhite)},
			Rgb(Foreground), Rgb(Background)};
	}

	// 方案数量固定；设置卡用它在编译期声明按钮容器数组。
	static constexpr int k_Count = 16;

	// 方案顺序即设置卡里的显示顺序；索引写进配置，只在末尾追加，不重排。
	inline const std::array<SDefinition, k_Count> &All()
	{
		static const std::array<SDefinition, k_Count> s_aSchemes = {{
			{"Dimidium", MakePalette(0xBAB7B6, 0x141414,
					     0x000000, 0xCF494C, 0x60B442, 0xDB9C11,
					     0x0575D8, 0xAF5ED2, 0x1DB6BB, 0xBAB7B6,
					     0x817E7E, 0xFF643B, 0x37E57B, 0xFCCD1A,
					     0x688DFD, 0xED6FE9, 0x32E0FB, 0xDEE3E4)},
			{"Ottosson", MakePalette(0xBEBEBE, 0x000000,
					     0x000000, 0xBE2C21, 0x3FAE3A, 0xBE9A4A,
					     0x204DBE, 0xBB54BE, 0x00A7B2, 0xBEBEBE,
					     0x808080, 0xFF3E30, 0x58EA51, 0xFFC944,
					     0x2F6AFF, 0xFC74FF, 0x00E1F0, 0xFFFFFF)},
			{"Campbell", MakePalette(0xCCCCCC, 0x0C0C0C,
					     0x0C0C0C, 0xC50F1F, 0x13A10E, 0xC19C00,
					     0x0037DA, 0x881798, 0x3A96DD, 0xCCCCCC,
					     0x767676, 0xE74856, 0x16C60C, 0xF9F1A5,
					     0x3B78FF, 0xB4009E, 0x61D6D6, 0xF2F2F2)},
			{"Campbell Powershell", MakePalette(0xCCCCCC, 0x012456,
							0x0C0C0C, 0xC50F1F, 0x13A10E, 0xC19C00,
							0x0037DA, 0x881798, 0x3A96DD, 0xCCCCCC,
							0x767676, 0xE74856, 0x16C60C, 0xF9F1A5,
							0x3B78FF, 0xB4009E, 0x61D6D6, 0xF2F2F2)},
			{"Vintage", MakePalette(0xC0C0C0, 0x000000,
					    0x000000, 0x800000, 0x008000, 0x808000,
					    0x000080, 0x800080, 0x008080, 0xC0C0C0,
					    0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
					    0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF)},
			{"One Half Dark", MakePalette(0xDCDFE4, 0x282C34,
						  0x282C34, 0xE06C75, 0x98C379, 0xE5C07B,
						  0x61AFEF, 0xC678DD, 0x56B6C2, 0xDCDFE4,
						  0x5A6374, 0xE06C75, 0x98C379, 0xE5C07B,
						  0x61AFEF, 0xC678DD, 0x56B6C2, 0xDCDFE4)},
			{"One Half Light", MakePalette(0x383A42, 0xFAFAFA,
						   0x383A42, 0xE45649, 0x50A14F, 0xC18301,
						   0x0184BC, 0xA626A4, 0x0997B3, 0xFAFAFA,
						   0x4F525D, 0xDF6C75, 0x98C379, 0xE4C07A,
						   0x61AFEF, 0xC577DD, 0x56B5C1, 0xFFFFFF)},
			{"Solarized Dark", MakePalette(0x839496, 0x002B36,
						   0x002B36, 0xDC322F, 0x859900, 0xB58900,
						   0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
						   0x073642, 0xCB4B16, 0x586E75, 0x657B83,
						   0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3)},
			{"Solarized Light", MakePalette(0x657B83, 0xFDF6E3,
						    0x002B36, 0xDC322F, 0x859900, 0xB58900,
						    0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
						    0x073642, 0xCB4B16, 0x586E75, 0x657B83,
						    0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3)},
			{"Tango Dark", MakePalette(0xD3D7CF, 0x000000,
					       0x000000, 0xCC0000, 0x4E9A06, 0xC4A000,
					       0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
					       0x555753, 0xEF2929, 0x8AE234, 0xFCE94F,
					       0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC)},
			{"Tango Light", MakePalette(0x555753, 0xFFFFFF,
						0x000000, 0xCC0000, 0x4E9A06, 0xC4A000,
						0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
						0x555753, 0xEF2929, 0x8AE234, 0xFCE94F,
						0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC)},
			{"Dark+", MakePalette(0xCCCCCC, 0x1E1E1E,
					  0x000000, 0xCD3131, 0x0DBC79, 0xE5E510,
					  0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
					  0x666666, 0xF14C4C, 0x23D18B, 0xF5F543,
					  0x3B8EEA, 0xD670D6, 0x29B8DB, 0xE5E5E5)},
			{"VSCode Dark Modern", MakePalette(0xCCCCCC, 0x1F1F1F,
						       0x000000, 0xCD3131, 0x0DBC79, 0xE5E510,
						       0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
						       0x666666, 0xF14C4C, 0x23D18B, 0xF5F543,
						       0x3B8EEA, 0xD670D6, 0x29B8DB, 0xE5E5E5)},
			{"VSCode Light Modern", MakePalette(0x3B3B3B, 0xFFFFFF,
							0x000000, 0xCD3131, 0x00BC00, 0x949800,
							0x0451A5, 0xBC05BC, 0x0598BC, 0x555555,
							0x666666, 0xCD3131, 0x14CE14, 0xB5BA00,
							0x0451A5, 0xBC05BC, 0x0598BC, 0xA5A5A5)},
			{"CGA", MakePalette(0xAAAAAA, 0x000000,
					0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
					0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
					0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
					0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF)},
			{"IBM 5153", MakePalette(0xAAAAAA, 0x000000,
					     0x000000, 0xAA0000, 0x00AA00, 0xC47E00,
					     0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
					     0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
					     0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF)},
		}};
		return s_aSchemes;
	}

	constexpr int Count()
	{
		return k_Count;
	}

	// 未知索引退回第一个方案，配置被外部改坏时界面仍可用。
	inline const SDefinition &At(int Index)
	{
		const auto &Schemes = All();
		if(Index < 0 || Index >= static_cast<int>(Schemes.size()))
			return Schemes[0];
		return Schemes[Index];
	}
}

#endif
