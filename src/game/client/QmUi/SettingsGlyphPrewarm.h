#ifndef GAME_CLIENT_QMUI_SETTINGSGLYPHPREWARM_H
#define GAME_CLIENT_QMUI_SETTINGSGLYPHPREWARM_H

#include <base/str.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

inline std::optional<int> SettingsGlyphPixelSize(float FontSize, float ScreenHeight, float GraphicsHeight) noexcept
{
	if(!std::isfinite(FontSize) || FontSize <= 0.0f)
		return std::nullopt;
	// 与文本容器的实际像素换算及至少一像素规则一致。
	const float SafeGraphicsHeight = std::isfinite(GraphicsHeight) && GraphicsHeight > 0.0f ? GraphicsHeight : 1.0f;
	const float Scale = std::isfinite(ScreenHeight) && ScreenHeight >= 1.0f ? SafeGraphicsHeight / ScreenHeight : 1.0f;
	const float Pixels = FontSize * Scale;
	if(!std::isfinite(Pixels) || static_cast<double>(Pixels) > std::numeric_limits<int>::max())
		return std::nullopt;
	return std::max(1, static_cast<int>(Pixels));
}

inline bool PrepareSettingsGlyphQueue(std::vector<std::pair<int, int>> &vQueue, size_t &Cursor, size_t &PlanCursor, size_t Target, const char *pText, int PixelSize) noexcept
{
	if(PixelSize <= 0)
		return false;
	try
	{
		std::vector<std::pair<int, int>> vCandidate;
		std::unordered_set<int> Seen;
		if(pText != nullptr)
		{
			for(const char *pCursor = pText; *pCursor != '\0';)
			{
				const int Chr = str_utf8_decode(&pCursor);
				if(Chr > 0 && Seen.insert(Chr).second)
					vCandidate.emplace_back(Chr, PixelSize);
			}
		}
		vQueue.swap(vCandidate);
		Cursor = 0;
		PlanCursor = Target;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

// 这里只确认void预热接口已返回；引擎内部的替代字形/重试仍由引擎管理。
template<typename FContinue, typename FPrewarm>
bool AdvanceSettingsGlyphQueueWhile(const std::vector<std::pair<int, int>> &vQueue, size_t &Cursor, FContinue &&Continue, FPrewarm &&Prewarm) noexcept
{
	while(Cursor < vQueue.size())
	{
		const auto Glyph = vQueue[Cursor];
		try
		{
			if(!Continue())
				return false;
			Prewarm(Glyph.first, Glyph.second);
		}
		catch(...)
		{
			return false;
		}
		++Cursor;
	}
	return true;
}

template<typename FPrewarm>
bool AdvanceSettingsGlyphQueue(const std::vector<std::pair<int, int>> &vQueue, size_t &Cursor, int Budget, FPrewarm &&Prewarm) noexcept
{
	int Done = 0;
	return AdvanceSettingsGlyphQueueWhile(vQueue, Cursor, [&] { return Done++ < Budget; }, std::forward<FPrewarm>(Prewarm));
}

#endif
