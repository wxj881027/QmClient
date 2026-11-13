#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_UPDATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_UPDATE_H

#include <engine/textrender.h>

#include <new>
#include <stdexcept>
#include <utility>

// 部件仍由名牌持有；只在一次完整更新成功后开放绘制和布局读取。
// 更新回调在主线程串行执行，不重入本状态。
class CQmNameplateUpdate
{
	bool m_Ready = false;

public:
	bool Ready() const { return m_Ready; }
	void Reset() { m_Ready = false; }

	template<typename TPrepare>
	bool Run(TPrepare &&Prepare)
	{
		m_Ready = false;
		try
		{
			m_Ready = std::forward<TPrepare>(Prepare)();
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		return m_Ready;
	}
};

// 原生文字后端的颜色和字体setter只写值，不分配；成功和异常出口使用相同恢复值。
template<typename TBuild>
void QmWithNameplateTextColor(ITextRender &Text, ColorRGBA Color, TBuild &&Build)
{
	const ColorRGBA Previous = Text.GetTextColor();
	Text.TextColor(Color);
	try
	{
		std::forward<TBuild>(Build)();
	}
	catch(...)
	{
		Text.TextColor(Previous);
		throw;
	}
	Text.TextColor(Previous);
}

template<typename TBuild>
void QmWithNameplateFontPreset(ITextRender &Text, EFontPreset Preset, TBuild &&Build)
{
	const EFontPreset Previous = Text.GetFontPreset();
	Text.SetFontPreset(Preset);
	try
	{
		std::forward<TBuild>(Build)();
	}
	catch(...)
	{
		Text.SetFontPreset(Previous);
		throw;
	}
	Text.SetFontPreset(Previous);
}

#endif
