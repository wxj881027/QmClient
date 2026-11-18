#ifndef GAME_CLIENT_QMUI_UITAG_H
#define GAME_CLIENT_QMUI_UITAG_H

#include "UiTheme.h"
#include "UiTokens.h"

#include <base/color.h>
#include <base/detect.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

class CUi;
class ITextRender;

// 标签（tag）：一颗**固定尺寸**的胶囊，承载「本地 / 网络 / 跟随 / 地图标签」这类
// 状态词或分类词。它不是按钮——可点的语义（命中、动画、禁用）留在调用点，这里只管
// 外形与颜色公式；需要药丸样式的动作按钮仍走 ui_widget::DrawButtonSurface。
//
// 尺寸恒定是这份组件的核心约束：高度、水平内边距、字号、圆角都是常量，只有文本长度
// 影响宽度。于是同一排的若干标签永远一样高、一样圆，跨页面也读作同一个东西
// （以前资源卡片是 24~36 宽 18 高的自绘块，好友列表是 13 高的药丸，两种都不像）。
//
// 颜色来源**只有三种**，调用点不许自己写 fill：
//   1. RANDOM_FIXED —— 由 key 派生的固定色：哈希落在莫奈 48 色板的同一格上。同一个词
//      永远同一色，跨会话、跨机器都稳定；「随机」只发生一次，之后由哈希钉死，所以
//      不用存配置也不会每次进游戏变色（地图标签、用户自建标签用这一档）。
//   2. FIXED —— 调用点给一个语义色（本地=绿、网络=蓝灰），整颗胶囊用它。
//   3. DERIVED —— 调用点给一个基色，按固定公式加深一档：跟随标签跟好友卡片同色系，
//      但比卡片底色更实，读作「这张卡上的状态」而不是另一张卡。
//
// 胶囊**不描边**：1px 亮边在卡片上会读成「又一张卡片的外轮廓」，和真正的列表卡片
// 抢同一层语义；文字也不再按底色自动翻黑/翻白，统一跟随 `qm_ui_text_color_mode`
// （用户在「图像」里选的那一档），否则同一屏里会出现两种文字色。
namespace ui_tag
{
	// 胶囊几何：全部固定，不随调用点、行高或文本长度变化。
	inline constexpr float HEIGHT = 16.0f;
	inline constexpr float PADDING_X = 6.0f;
	inline constexpr float RADIUS = HEIGHT * 0.5f;
	inline constexpr float FONT_SIZE = ui_token::font::SMALL;

	enum class EColorMode
	{
		RANDOM_FIXED,
		FIXED,
		DERIVED,
	};

	struct SColors
	{
		ColorRGBA m_Fill{};
		ColorRGBA m_Text{};
	};

	// 「随机固定」用的**莫奈色板**：12 个色相 × 4 个变体 = 48 色，足够让常用标签基本不撞色。
	//
	// 为什么是固定色板而不是 HSL 等距随机生成：莫奈的关键在于「低饱和 + 相近明度」，
	// 直接按色相均分会跑出高饱和的青绿与紫红；这里每个色相的明度都是**按感知亮度反解**
	// 出来的（不是 HSL 里的 L 相同），所以黄不会亮到发白、紫不会暗到发黑，整排看过去
	// 是同一幅画里的雾状色差。
	//
	// 四个变体 = 明度两档（浅 / 深，感知亮度 0.50 / 0.42）× 饱和度两档（灰 / 艳，0.22 / 0.34）。
	// 排列是**色相分组**（每组四行：浅灰、浅艳、深灰、深艳），取色时再打散成
	// 「变体在外、色相在内」，所以相邻哈希不会落在同一色相的深浅变体上（那样看着像同色）。
	//
	// 索引由**标签文本的哈希**取模决定，不按添加顺序轮转：顺序是玩家拖动排的，
	// 按位置取色会让一次拖动把整排颜色全换一遍（这也是项目规范里禁止的
	// 「每次进游戏重新随机一次颜色」同一条：颜色只能是词的函数）。
	inline constexpr int MONET_HUE_COUNT = 12;
	inline constexpr int MONET_VARIANTS_PER_HUE = 4;
	inline constexpr ColorRGBA MONET_PALETTE[] = {
		// 罂粟红
		ColorRGBA(0.630f, 0.449f, 0.421f, 1.0f), ColorRGBA(0.695f, 0.423f, 0.382f, 1.0f),
		ColorRGBA(0.542f, 0.372f, 0.346f, 1.0f), ColorRGBA(0.614f, 0.344f, 0.302f, 1.0f),
		// 赭橙
		ColorRGBA(0.577f, 0.486f, 0.369f, 1.0f), ColorRGBA(0.615f, 0.480f, 0.303f, 1.0f),
		ColorRGBA(0.484f, 0.409f, 0.310f, 1.0f), ColorRGBA(0.517f, 0.403f, 0.254f, 1.0f),
		// 麦黄
		ColorRGBA(0.529f, 0.517f, 0.338f, 1.0f), ColorRGBA(0.542f, 0.524f, 0.267f, 1.0f),
		ColorRGBA(0.445f, 0.434f, 0.284f, 1.0f), ColorRGBA(0.455f, 0.440f, 0.224f, 1.0f),
		// 橄榄
		ColorRGBA(0.467f, 0.546f, 0.349f, 1.0f), ColorRGBA(0.452f, 0.567f, 0.279f, 1.0f),
		ColorRGBA(0.392f, 0.459f, 0.293f, 1.0f), ColorRGBA(0.380f, 0.476f, 0.235f, 1.0f),
		// 苔绿
		ColorRGBA(0.381f, 0.585f, 0.374f, 1.0f), ColorRGBA(0.320f, 0.629f, 0.310f, 1.0f),
		ColorRGBA(0.320f, 0.491f, 0.314f, 1.0f), ColorRGBA(0.269f, 0.528f, 0.260f, 1.0f),
		// 灰绿
		ColorRGBA(0.367f, 0.574f, 0.470f, 1.0f), ColorRGBA(0.301f, 0.610f, 0.455f, 1.0f),
		ColorRGBA(0.308f, 0.482f, 0.395f, 1.0f), ColorRGBA(0.252f, 0.513f, 0.383f, 1.0f),
		// 雾青
		ColorRGBA(0.358f, 0.560f, 0.560f, 1.0f), ColorRGBA(0.290f, 0.589f, 0.589f, 1.0f),
		ColorRGBA(0.301f, 0.471f, 0.471f, 1.0f), ColorRGBA(0.244f, 0.495f, 0.495f, 1.0f),
		// 湖蓝
		ColorRGBA(0.394f, 0.532f, 0.612f, 1.0f), ColorRGBA(0.336f, 0.550f, 0.673f, 1.0f),
		ColorRGBA(0.330f, 0.447f, 0.516f, 1.0f), ColorRGBA(0.280f, 0.463f, 0.568f, 1.0f),
		// 群青
		ColorRGBA(0.442f, 0.502f, 0.643f, 1.0f), ColorRGBA(0.414f, 0.503f, 0.711f, 1.0f),
		ColorRGBA(0.361f, 0.422f, 0.565f, 1.0f), ColorRGBA(0.323f, 0.423f, 0.657f, 1.0f),
		// 紫罗兰
		ColorRGBA(0.505f, 0.467f, 0.659f, 1.0f), ColorRGBA(0.507f, 0.452f, 0.730f, 1.0f),
		ColorRGBA(0.426f, 0.382f, 0.598f, 1.0f), ColorRGBA(0.428f, 0.364f, 0.687f, 1.0f),
		// 丁香紫
		ColorRGBA(0.562f, 0.441f, 0.642f, 1.0f), ColorRGBA(0.591f, 0.413f, 0.711f, 1.0f),
		ColorRGBA(0.482f, 0.360f, 0.564f, 1.0f), ColorRGBA(0.522f, 0.323f, 0.655f, 1.0f),
		// 玫瑰粉
		ColorRGBA(0.628f, 0.418f, 0.586f, 1.0f), ColorRGBA(0.693f, 0.377f, 0.630f, 1.0f),
		ColorRGBA(0.538f, 0.344f, 0.500f, 1.0f), ColorRGBA(0.608f, 0.300f, 0.546f, 1.0f),
	};
	inline constexpr int MONET_PALETTE_SIZE = (int)(sizeof(MONET_PALETTE) / sizeof(MONET_PALETTE[0]));

	// 哈希 → 色板下标：先按色相轮转、再换变体，相邻种子落在不同色相上。
	inline int MonetPaletteIndex(unsigned Seed)
	{
		const int Slot = (int)(Seed % (unsigned)MONET_PALETTE_SIZE);
		const int HueIndex = Slot % MONET_HUE_COUNT;
		const int Variant = Slot / MONET_HUE_COUNT;
		return HueIndex * MONET_VARIANTS_PER_HUE + Variant;
	}

	// 相对亮度：判断填充是否够亮（只用于派生的填充微调，不做 gamma 精确换算）。
	inline float RelativeLuminance(const ColorRGBA &Color)
	{
		return std::clamp(0.299f * Color.r + 0.587f * Color.g + 0.114f * Color.b, 0.0f, 1.0f);
	}

	// FNV-1a：同一个标签文本永远得到同一个种子（不依赖进程随机数、不依赖容器顺序）。
	inline unsigned SeedFromKey(const char *pKey)
	{
		unsigned Hash = 2166136261u;
		for(const char *p = pKey; p != nullptr && *p != '\0'; ++p)
		{
			Hash ^= static_cast<unsigned char>(*p);
			Hash *= 16777619u;
		}
		// 再混一步，短词（"hj"）的低位分布不至于全挤在同一段色相。
		Hash ^= Hash >> 13;
		Hash *= 2654435761u;
		Hash ^= Hash >> 16;
		return Hash;
	}

	// 只乘 RGB、保留 alpha：加深/提亮都不改变透明度语义。
	inline ColorRGBA Shade(const ColorRGBA &Color, float Factor)
	{
		return ColorRGBA(
			std::clamp(Color.r * Factor, 0.0f, 1.0f),
			std::clamp(Color.g * Factor, 0.0f, 1.0f),
			std::clamp(Color.b * Factor, 0.0f, 1.0f),
			Color.a);
	}

	inline SColors Resolve(EColorMode Mode, const ColorRGBA &Base, const char *pKey, float Alpha = 1.0f)
	{
		const float A = std::clamp(Alpha, 0.0f, 1.0f);
		SColors Colors;
		switch(Mode)
		{
		case EColorMode::RANDOM_FIXED:
		{
			// 哈希 → 莫奈色板下标：同一个词永远落在同一格，不同词在 48 色里轮转。
			const unsigned Seed = SeedFromKey(pKey != nullptr ? pKey : "");
			Colors.m_Fill = MONET_PALETTE[MonetPaletteIndex(Seed)].WithAlpha(0.92f * A);
			break;
		}
		case EColorMode::DERIVED:
		{
			// 基色 → 更深一档（0.72）：同色系但更“实”，明确读作「当前卡片上的状态」。
			const ColorRGBA Fill = Shade(Base, 0.72f);
			Colors.m_Fill = Fill.WithAlpha(std::max(Base.a, 0.0f) > 0.0f ? Base.a * A : A);
			break;
		}
		case EColorMode::FIXED:
		default:
		{
			Colors.m_Fill = Base.WithAlpha((Base.a > 0.0f ? Base.a : 1.0f) * A);
			break;
		}
		}
		// 文字统一跟随用户的「文本颜色模式」，不按填充亮度自动翻色：标签和旁边的
		// 标题读作同一套文字，而不是一块自成一体的彩色块。
		Colors.m_Text = ResolveConfiguredTextColor().WithMultipliedAlpha(A);
		return Colors;
	}

	// 胶囊宽度 = 文本实测宽 + 两侧固定内边距；内部边距不随调用点变。
	// UiScale 只用于「整个面板按可用空间缩放」的场景（服务器信息面板），
	// 同一次绘制里所有标签传同一个值，彼此仍然等高。
	float MeasureWidth(ITextRender *pTextRender, const char *pText, float UiScale = 1.0f);
	// 把固定高度的胶囊放进一行里：垂直居中、水平从 Slot 左缘起（或调用点已算好槽位）。
	CUIRect FitRect(const CUIRect &Slot, float Width, float UiScale = 1.0f);
	// 在 Slot 里画一颗标签，返回实际占用的矩形（调用点据此推进后续内容）。
	CUIRect Draw(CUi *pUi, const CUIRect &Slot, const char *pText, EColorMode Mode, const ColorRGBA &Base = ColorRGBA(), const char *pKey = nullptr, float Alpha = 1.0f, float UiScale = 1.0f);
	// 调用点已经算好精确矩形时用它：不再测量、不再居中，只负责配色与文字。
	void DrawInRect(CUi *pUi, const CUIRect &Rect, const char *pText, EColorMode Mode, const ColorRGBA &Base = ColorRGBA(), const char *pKey = nullptr, float Alpha = 1.0f, float UiScale = 1.0f);
} // namespace ui_tag

#endif
