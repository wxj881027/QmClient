#ifndef GAME_CLIENT_QMUI_SETTINGSCARDHELP_H
#define GAME_CLIENT_QMUI_SETTINGSCARDHELP_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

// 每张卡持有自己的说明缓存。高度取本卡所有已注册文案的最大值，不随悬浮目标变化。
class CSettingsCardHelp
{
	struct SEntry
	{
		std::string m_Text;
		float m_Height = 0.0f;
	};
	std::unordered_map<uintptr_t, SEntry> m_Entries;
	std::string m_Overview;
	std::string m_Current;
	float m_Width = -1.0f;
	float m_FontSize = -1.0f;
	float m_Height = 0.0f;
	uint64_t m_Revision = 0;
	int m_TargetPriority = 0;

public:
	template<typename TMeasure>
	bool Configure(float Width, float FontSize, uint64_t Revision, const char *pOverview, TMeasure Measure)
	{
		const std::string Overview = pOverview != nullptr ? pOverview : "";
		if(m_Width != Width || m_FontSize != FontSize || m_Revision != Revision || m_Overview != Overview)
		{
			m_Entries.clear();
			m_Width = Width;
			m_FontSize = FontSize;
			m_Revision = Revision;
			m_Overview = Overview;
			m_Height = std::max(FontSize, Measure(Overview.c_str()));
			return true;
		}
		return false;
	}

	void BeginFrame()
	{
		m_Current = m_Overview;
		m_TargetPriority = 0;
	}

	template<typename TMeasure>
	void Register(uintptr_t Id, const char *pText, bool Hovered, bool Focused, TMeasure Measure)
	{
		if(pText == nullptr || pText[0] == '\0')
			return;
		auto [Iter, Inserted] = m_Entries.try_emplace(Id);
		SEntry &Entry = Iter->second;
		if(Inserted || Entry.m_Text != pText)
		{
			Entry.m_Text = pText;
			Entry.m_Height = std::max(m_FontSize, Measure(pText));
			m_Height = std::max(m_Height, Entry.m_Height);
		}
		const int Priority = Hovered ? 2 : (Focused ? 1 : 0);
		if(Priority > 0 && Priority >= m_TargetPriority)
		{
			m_Current = Entry.m_Text;
			m_TargetPriority = Priority;
		}
	}

	float Width() const { return m_Width; }
	float FontSize() const { return m_FontSize; }
	float Height() const { return m_Height; }
	const char *Text() const { return m_Current.c_str(); }
};

#endif
