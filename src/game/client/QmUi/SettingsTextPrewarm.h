#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H

#include <base/str.h>

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
	std::vector<int> m_vGlyphs;
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

	void Prepare(const std::string &Text, int FontSize)
	{
		if(m_Text == Text && m_FontSize == FontSize)
			return;
		m_Text = Text;
		m_FontSize = FontSize;
		m_vGlyphs.clear();
		m_Cursor = 0;
		std::unordered_set<int> Seen;
		for(const char *pCursor = m_Text.c_str(); *pCursor != '\0';)
		{
			const int Chr = str_utf8_decode(&pCursor);
			if(Chr > 0 && Seen.insert(Chr).second)
				m_vGlyphs.push_back(Chr);
		}
	}

	template<typename TNow, typename TPrewarm>
	bool Advance(SSettingsTextPrewarmBudget &Budget, TNow &&Now, TPrewarm &&Prewarm)
	{
		while(m_Cursor < m_vGlyphs.size())
		{
			if(!Budget.ConsumeGlyph(Now()))
				return false;
			Prewarm(m_vGlyphs[m_Cursor], m_FontSize);
			++m_Cursor;
		}
		// 单次字形生成不可中断，超时后把容器构建留给下一轮。
		return Budget.HasTime(Now());
	}
};

#endif // GAME_CLIENT_QMUI_SETTINGSTEXTPREWARM_H
