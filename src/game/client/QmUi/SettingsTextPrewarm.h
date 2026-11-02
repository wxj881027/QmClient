#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H

#include "SettingsGlyphPrewarm.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct SSettingsTextPrewarmBudget
{
	int m_RemainingGlyphs;
	int64_t m_Deadline;

	bool HasTime(int64_t Now) const { return Now < m_Deadline; }
	bool ConsumeGlyph(int64_t Now)
	{
		if(m_RemainingGlyphs <= 0 || !HasTime(Now))
			return false;
		--m_RemainingGlyphs;
		return true;
	}
};

// 每条文本保留独立进度；同一轮中的多条文本共用调用方的预算。
class CSettingsTextPrewarm
{
	std::string m_Text;
	std::vector<std::pair<int, int>> m_vGlyphs;
	size_t m_PlanCursor = 0;
	size_t m_Cursor = 0;
	int m_FontSize = 0;

public:
	void Reset()
	{
		m_Text.clear();
		m_vGlyphs.clear();
		m_Cursor = 0;
		m_FontSize = 0;
	}

	bool Prepare(const std::string &Text, int FontSize) noexcept
	{
		if(m_Text == Text && m_FontSize == FontSize)
			return true;
		try
		{
			std::string CandidateText = Text;
			if(!PrepareSettingsGlyphQueue(m_vGlyphs, m_Cursor, m_PlanCursor, 0, CandidateText.c_str(), FontSize))
				return false;
			m_Text.swap(CandidateText);
			m_FontSize = FontSize;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	template<typename TNow, typename TPrewarm>
	bool Advance(SSettingsTextPrewarmBudget &Budget, TNow &&Now, TPrewarm &&Prewarm)
	{
		if(!AdvanceSettingsGlyphQueueWhile(m_vGlyphs, m_Cursor, [&] { return Budget.ConsumeGlyph(Now()); }, std::forward<TPrewarm>(Prewarm)))
			return false;
		// 单次字形生成不可中断，超时后把容器构建留给下一轮。
		return Budget.HasTime(Now());
	}
};

#endif // GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H
