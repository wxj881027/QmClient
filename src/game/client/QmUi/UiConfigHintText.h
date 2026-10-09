#ifndef GAME_CLIENT_QMUI_UICONFIGHINTTEXT_H
#define GAME_CLIENT_QMUI_UICONFIGHINTTEXT_H

#include <string>
#include <string_view>

// 命令与说明分开保存，重复登记不会叠加命令，也不会清掉已有说明。
class CUiConfigHintText
{
	std::string m_First;
	std::string m_Second;
	std::string m_Description;
	std::string m_FallbackDescription;
	std::string m_Text;

	void Rebuild()
	{
		m_Text = m_Description.empty() ? m_FallbackDescription : m_Description;
		for(const auto *pCommand : {&m_First, &m_Second})
		{
			if(!pCommand->empty())
			{
				if(!m_Text.empty())
					m_Text += '\n';
				m_Text += *pCommand;
			}
		}
	}

public:
	void SetCommands(const char *pFirst, const char *pSecond = nullptr)
	{
		if(pFirst == nullptr)
		{
			pFirst = pSecond;
			pSecond = nullptr;
		}
		pFirst = pFirst != nullptr ? pFirst : "";
		pSecond = pSecond != nullptr ? pSecond : "";
		if(std::string_view(pFirst) == pSecond)
			pSecond = "";
		if(m_First != pFirst || m_Second != pSecond)
		{
			// 同一控件被重新绑定时，不沿用前一个选项的说明。
			if(!m_First.empty())
			{
				m_Description.clear();
				m_FallbackDescription.clear();
			}
			m_First = pFirst;
			m_Second = pSecond;
			Rebuild();
		}
	}
	void SetDescription(const char *pDescription)
	{
		if(pDescription != nullptr && m_Description != pDescription)
		{
			m_Description = pDescription;
			Rebuild();
		}
	}
	void SetFallbackDescription(const char *pDescription)
	{
		pDescription = pDescription != nullptr ? pDescription : "";
		if(m_FallbackDescription != pDescription)
		{
			m_FallbackDescription = pDescription;
			Rebuild();
		}
	}
	const char *Text() const { return m_Text.c_str(); }
};

#endif
