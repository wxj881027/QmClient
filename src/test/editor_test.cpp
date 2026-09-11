#include <base/str.h>

#include <engine/shared/localization.h>

#include <game/editor/mapitems.h>
#include <game/localization.h>

#include <gtest/gtest.h>

#include <array>

bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool IsValidEditorTooltip(const char *pTooltip, char *pErrorMsg, int ErrorMsgSize)
{
	pErrorMsg[0] = '\0';
	char aHotkey[512];
	aHotkey[0] = '\0';

	if(pTooltip[0] == '[')
	{
		str_copy(aHotkey, pTooltip + 1);
		const char *pHotkeyEnd = str_find(aHotkey, "]");
		if(!pHotkeyEnd)
		{
			str_copy(pErrorMsg, "tooltip missing closing square bracket", ErrorMsgSize);
			return false;
		}
		aHotkey[pHotkeyEnd - aHotkey] = '\0';

		for(int i = 0; aHotkey[i]; i++)
		{
			bool ExpectLowerCase = true;
			if(i == 0)
			{
				ExpectLowerCase = false;
			}
			else if(!is_letter(aHotkey[i - 1]))
			{
				// the first character of a word should be uppercase
				ExpectLowerCase = false;
			}

			bool IsLower = aHotkey[i] >= 'a' && aHotkey[i] <= 'z';
			bool IsUpper = aHotkey[i] >= 'A' && aHotkey[i] <= 'Z';

			if(ExpectLowerCase && IsUpper)
			{
				str_format(pErrorMsg, ErrorMsgSize, "expected character '%c' at index %d to be lower case", aHotkey[i], i + 1);
				return false;
			}
			if(!ExpectLowerCase && IsLower)
			{
				str_format(pErrorMsg, ErrorMsgSize, "expected character '%c' at index %d to be upper case", aHotkey[i], i + 1);
				return false;
			}
		}
	}

	const char *pParenthesis = str_find(pTooltip, "(");
	if(pParenthesis)
	{
		const char *pHotkey = str_find_nocase(pParenthesis, "ctrl");
		if(!pHotkey)
			pHotkey = str_find_nocase(pParenthesis, "shift");
		if(!pHotkey)
			pHotkey = str_find_nocase(pParenthesis, "home");

		if(pHotkey)
		{
			int Offset = pHotkey - pTooltip;
			str_format(pErrorMsg, ErrorMsgSize, "found hotkey at offset %d. Hotkeys must be defined at the start.", Offset);
			return false;
		}
	}

	if(!str_endswith(pTooltip, "."))
	{
		str_copy(pErrorMsg, "tooltip has to end with a dot", ErrorMsgSize);
		return false;
	}
	return true;
}

void AssertTooltip(const char *pTooltip)
{
	char aError[512];
	EXPECT_TRUE(IsValidEditorTooltip(pTooltip, aError, sizeof(aError))) << "Invalid tooltip: " << pTooltip << "\nError: " << aError;
}

TEST(Editor, QuickActionNames)
{
	char aError[512];
	EXPECT_TRUE(IsValidEditorTooltip("hello world.", aError, sizeof(aError)));
	EXPECT_TRUE(IsValidEditorTooltip("[Ctrl+H] hello world.", aError, sizeof(aError)));
	EXPECT_FALSE(IsValidEditorTooltip("[Ctrl+H hello world.", aError, sizeof(aError)));
	EXPECT_FALSE(IsValidEditorTooltip("[ctrl+h] hello world.", aError, sizeof(aError)));
	EXPECT_FALSE(IsValidEditorTooltip("hello world", aError, sizeof(aError)));
	EXPECT_FALSE(IsValidEditorTooltip("hello world (Ctrl+H).", aError, sizeof(aError)));

#define REGISTER_QUICK_ACTION(name, text, callback, disabled, active, button_color, description) AssertTooltip(description);
#include <game/editor/quick_actions.h>
#undef REGISTER_QUICK_ACTION
}

TEST(Editor, LocalizationContextDoesNotFallBackToAnotherLanguage)
{
	CLocalizationDatabase Localization;
	Localization.AddString("Save", "Translated save", "");

	const unsigned SaveHash = str_quickhash("Save");
	EXPECT_STREQ(Localization.FindString(SaveHash, str_quickhash("Other context")), "Translated save");
	EXPECT_EQ(Localization.FindString(SaveHash, str_quickhash("Editor"), false), nullptr);
	EXPECT_EQ(Localization.FindString(SaveHash, str_quickhash("Editor tile action"), false), nullptr);
}

TEST(Editor, LayerKindFromFlags)
{
	// 正常地图里实体标记互斥，单独置位时对应各自的图层种类
	EXPECT_EQ(LayerKindFromFlags(false, false, false, false, false, false), ELayerKind::TILES);
	EXPECT_EQ(LayerKindFromFlags(true, false, false, false, false, false), ELayerKind::GAME);
	EXPECT_EQ(LayerKindFromFlags(false, true, false, false, false, false), ELayerKind::FRONT);
	EXPECT_EQ(LayerKindFromFlags(false, false, true, false, false, false), ELayerKind::TELE);
	EXPECT_EQ(LayerKindFromFlags(false, false, false, true, false, false), ELayerKind::SPEEDUP);
	EXPECT_EQ(LayerKindFromFlags(false, false, false, false, true, false), ELayerKind::SWITCH);
	EXPECT_EQ(LayerKindFromFlags(false, false, false, false, false, true), ELayerKind::TUNE);
	// 异常数据（多个标记同时置位）时按固定优先级取第一个，保证画笔层匹配结果可预期
	EXPECT_EQ(LayerKindFromFlags(true, true, true, true, true, true), ELayerKind::GAME);
	EXPECT_EQ(LayerKindFromFlags(false, true, true, false, true, false), ELayerKind::FRONT);
	EXPECT_EQ(LayerKindFromFlags(false, false, true, false, false, true), ELayerKind::TELE);
}

TEST(Editor, ShouldAutoGrabEntityLayer)
{
	// 复制区域时只有传送/速度/开关/调参层需要自动补进画笔，否则粘贴出来的区块会缺这些数据
	EXPECT_TRUE(ShouldAutoGrabEntityLayer(ELayerKind::TELE, false));
	EXPECT_TRUE(ShouldAutoGrabEntityLayer(ELayerKind::SPEEDUP, false));
	EXPECT_TRUE(ShouldAutoGrabEntityLayer(ELayerKind::SWITCH, false));
	EXPECT_TRUE(ShouldAutoGrabEntityLayer(ELayerKind::TUNE, false));
	// 已经选中的层不需要重复抓取
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::TELE, true));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::SWITCH, true));
	// 游戏层/普通图块层/前景层以及非图块层都不自动补抓
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::GAME, false));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::TILES, false));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::FRONT, false));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::QUADS, false));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::SOUNDS, false));
	EXPECT_FALSE(ShouldAutoGrabEntityLayer(ELayerKind::INVALID, false));
}

TEST(Editor, AnyInRect)
{
	// 4x3 图层，只在 (2,1) 放一个非空图块
	constexpr int Width = 4;
	constexpr int Height = 3;
	std::array<CTile, Width * Height> aTiles{};
	aTiles[1 * Width + 2] = CTile{TILE_SOLID, 0, 0, 0};
	const auto HasContent = [&](int x, int y, int w, int h) {
		return AnyInRect(Width, Height, x, y, w, h, [&](int px, int py) { return aTiles[py * Width + px].m_Index != TILE_AIR; });
	};

	EXPECT_TRUE(HasContent(2, 1, 1, 1)); // 命中唯一非空图块
	EXPECT_TRUE(HasContent(0, 0, Width, Height)); // 覆盖整层
	EXPECT_TRUE(HasContent(-10, -10, 100, 100)); // 越界矩形按图层边界裁剪
	EXPECT_TRUE(HasContent(0, 0, 3, 2)); // 矩形含该格但不含右下角
	EXPECT_FALSE(HasContent(0, 0, 2, 1)); // 不包含该格
	EXPECT_FALSE(HasContent(3, 2, 1, 1)); // 右下角为空
	EXPECT_FALSE(HasContent(0, 0, 0, 0)); // 空矩形
	EXPECT_FALSE(HasContent(0, 0, -3, -3)); // 负尺寸
	EXPECT_FALSE(HasContent(Width + 1, Height + 1, 2, 2)); // 完全在图层外
	// 空图层即使矩形合法也不该认为有内容
	EXPECT_FALSE(AnyInRect(0, 0, 0, 0, 1, 1, [](int, int) { return true; }));
	EXPECT_FALSE(AnyInRect(Width, Height, 0, 0, 0, 0, [](int, int) { return true; }));
}
