#ifndef GAME_CLIENT_QMUI_CARDS_QMCONSOLESCHEMELIST_H
#define GAME_CLIENT_QMUI_CARDS_QMCONSOLESCHEMELIST_H

#include <game/client/components/qmclient/console_appearance.h>
#include <game/client/components/qmclient/console_color_schemes.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

// 控制台配色方案列表的统一模型。
//
// 配置里只有一份方案编号，因此界面上也只能有一份列表：历史的三项
//（现代终端 / 经典 PowerShell / 自定义）与 Windows Terminal 内置配色
// 同属一个选择集合，选中内置配色后仍能切回历史项。把它们拆成"分段选择器
// + 独立列表"会让上半部分在选中内置配色后变成不可用。
namespace qm_console_scheme_list
{
	enum class EKind
	{
		// 原有的三套语义配色，编号 0..2。
		LEGACY,
		// Windows Terminal 内置配色，编号从 k_BuiltinSchemeBase 起。
		BUILTIN,
	};

	struct SEntry
	{
		EKind m_Kind;
		int m_Value;
		const char *m_pName;
		// 没有 ANSI 色板的历史项为 nullptr，行内只显示名称。
		const QmConsoleSchemes::SAnsiPalette *m_pPalette;
	};

	// 模型里只存英文 key：它是稳定标识，测试与反查都靠它。
	inline const char *LegacyName(int Index)
	{
		// 顺序与配置语义一致：0=现代终端、1=经典 PowerShell、2=自定义。
		switch(Index)
		{
		case 0: return "Modern terminal";
		case 1: return "Classic PowerShell";
		default: return "Custom";
		}
	}

	// 显示名在绘制时才算：Localize 是运行时查表，把结果缓存进静态数组会让
	// 切换语言后名称停在旧语言上。参数必须是字面量——提取脚本只认
	// Localize("字面量")，传变量会让这三个 key 进不了 data/languages/*.txt，
	// 运行时也就没有译文可用。
	inline const char *LegacyDisplayName(int Index)
	{
		switch(Index)
		{
		case 0: return Localize("Modern terminal");
		case 1: return Localize("Classic PowerShell");
		default: return Localize("Custom");
		}
	}

	// 内置配色名是 Windows Terminal 的专有名称（Campbell、Solarized Dark 等），
	// 与原程序一样不翻译；历史三项才走本地化。
	inline const char *DisplayName(const SEntry &Entry)
	{
		return Entry.m_Kind == EKind::LEGACY ? LegacyDisplayName(Entry.m_Value) : Entry.m_pName;
	}

	// 一行预览需要的全部颜色：行底色、文字色、左侧 16 格色板。
	// 内置配色直接用它的 ANSI 定义；历史三项没有 ANSI 表，从语义色推出来，
	// 这样三行和内置配色长得一样，不会出现只有色块没有底色的半成品外观。
	struct SPreview
	{
		ColorRGBA m_Background;
		ColorRGBA m_Foreground;
		std::array<ColorRGBA, 16> m_aSwatch;
	};

	// 可读性对比的两个端点：纯白与纯黑。方案预览要在任意用户配色下都读得清，
	// 所以端点不随主题走，也不能换成 ui_token 的主题色。
	inline constexpr ColorRGBA CONTRAST_LIGHT{1.0f, 1.0f, 1.0f, 1.0f};
	inline constexpr ColorRGBA CONTRAST_DARK{0.0f, 0.0f, 0.0f, 1.0f};

	// 把颜色朝另一个颜色混合，用于推导历史项的亮/暗两排。
	// 色板格必须不透明：混合只换色相，不把行底色的透明度带进来。
	inline ColorRGBA BlendColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
	{
		return QmConsoleAppearance::Blend(From.WithAlpha(1.0f), To.WithAlpha(1.0f), Amount);
	}

	inline float RelativeLuminance(const ColorRGBA &Color)
	{
		return 0.2126f * Color.r + 0.7152f * Color.g + 0.0722f * Color.b;
	}

	// 行内标题要保证看得清：自定义方案的出厂配色是 DDNet 时代的深蓝配近黑底，
	// 照搬前景色会得到"一行全黑"。亮度差太小时把前景朝白或黑推到可读为止，
	// 仍然保持原来的色相。
	inline ColorRGBA LegibleTextColor(const ColorRGBA &Foreground, const ColorRGBA &Background)
	{
		const float BackgroundLuminance = RelativeLuminance(Background);
		const float Contrast = std::abs(RelativeLuminance(Foreground) - BackgroundLuminance);
		if(Contrast >= 0.28f)
			return Foreground;
		const ColorRGBA Target = BackgroundLuminance < 0.5f ? CONTRAST_LIGHT : CONTRAST_DARK;
		const float Amount = std::clamp((0.28f - Contrast) / 0.28f, 0.0f, 1.0f) * 0.75f + 0.25f;
		return BlendColor(Foreground, Target, Amount);
	}

	inline SPreview BuildPreview(const SEntry &Entry, int Opacity, const std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> &Custom)
	{
		SPreview Preview{};
		if(Entry.m_pPalette != nullptr)
		{
			Preview.m_Background = Entry.m_pPalette->m_Background.WithAlpha(1.0f);
			Preview.m_Foreground = LegibleTextColor(Entry.m_pPalette->m_Foreground, Preview.m_Background);
			Preview.m_aSwatch = Entry.m_pPalette->m_aColors;
			return Preview;
		}
		// 历史项：前八个格放语义色，后八个格是同色相的暗版本，
		// 与 ANSI 的"常规 / 亮色"两排保持一致的阅读方式。
		const auto Palette = QmConsoleAppearance::ResolvePalette(Entry.m_Value, Opacity, Custom);
		Preview.m_Background = Palette.m_aColors[QmConsoleAppearance::BACKGROUND].WithAlpha(1.0f);
		Preview.m_Foreground = LegibleTextColor(Palette.m_aColors[QmConsoleAppearance::TEXT], Preview.m_Background);
		const std::array<QmConsoleAppearance::EColor, 8> aOrder = {
			QmConsoleAppearance::BACKGROUND, QmConsoleAppearance::TEXT, QmConsoleAppearance::COMMAND, QmConsoleAppearance::PARAMETER,
			QmConsoleAppearance::STRING, QmConsoleAppearance::NUMBER, QmConsoleAppearance::LINK, QmConsoleAppearance::SEARCH};
		for(size_t i = 0; i < aOrder.size(); ++i)
		{
			Preview.m_aSwatch[i] = Palette.m_aColors[aOrder[i]];
			Preview.m_aSwatch[8 + i] = BlendColor(Palette.m_aColors[aOrder[i]], Preview.m_Background, 0.55f);
		}
		return Preview;
	}

	// 历史三项固定在列表顶部（界面里作为表头，不参与滚动），其余为内置配色。
	static constexpr int k_LegacyCount = 3;

	// 历史三项按配置编号排在前，内置配色按 console_color_schemes.h 的顺序排在后面。
	inline const std::array<SEntry, k_LegacyCount + QmConsoleSchemes::Count()> &All()
	{
		static const std::array<SEntry, k_LegacyCount + QmConsoleSchemes::Count()> s_aEntries = [] {
			std::array<SEntry, k_LegacyCount + QmConsoleSchemes::Count()> Entries{};
			for(int Index = 0; Index < k_LegacyCount; ++Index)
				Entries[Index] = {EKind::LEGACY, Index, LegacyName(Index), nullptr};
			for(int Index = 0; Index < QmConsoleSchemes::Count(); ++Index)
				Entries[k_LegacyCount + Index] = {EKind::BUILTIN, QmConsoleAppearance::k_BuiltinSchemeBase + Index,
					QmConsoleSchemes::At(Index).m_pName, &QmConsoleSchemes::At(Index).m_Palette};
			return Entries;
		}();
		return s_aEntries;
	}

	constexpr int Count()
	{
		return k_LegacyCount + QmConsoleSchemes::Count();
	}

	// 当前配置值落在列表中的位置；未知值返回 -1，由调用方决定如何提示。
	inline int IndexOf(int SchemeValue)
	{
		const auto &Entries = All();
		for(int Index = 0; Index < Count(); ++Index)
			if(Entries[Index].m_Value == SchemeValue)
				return Index;
		return -1;
	}
}

#endif
