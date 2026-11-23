#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHQUERY_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHQUERY_H

#include <base/str.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace qm_settings_search
{
	inline std::string NormalizeQuery(const char *pText)
	{
		std::string Text;
		if(pText == nullptr)
			return Text;
		while(*pText != '\0')
		{
			int Code = str_utf8_decode(&pText);
			if(Code < 0)
				return {};
			if(Code >= 0xff01 && Code <= 0xff5e)
				Code -= 0xfee0;
			if(str_utf8_isspace(Code))
				Code = ' ';
			else if(Code == 0x3001)
				Code = ',';
			else if(Code == 0x3002)
				Code = '.';
			char aEncoded[4];
			const int Length = str_utf8_encode(aEncoded, str_utf8_tolower_codepoint(Code));
			Text.append(aEncoded, Length);
		}
		return Text;
	}

	inline std::string_view StripQuestion(std::string_view Query)
	{
		const size_t Last = Query.find_last_not_of(" !?,.");
		if(Last == std::string_view::npos)
			return {};
		Query = Query.substr(0, Last + 1);
		Query.remove_prefix(Query.find_first_not_of(' '));
		constexpr std::string_view PLEASE = "请问";
		if(Query.starts_with(PLEASE))
		{
			Query.remove_prefix(PLEASE.size());
			Query.remove_prefix(std::min(Query.find_first_not_of(" ,"), Query.size()));
		}
		constexpr std::string_view aPrefixes[] = {
			"我想修改", "怎么调整", "怎么设置", "怎么改", "怎么调", "怎么用", "如何调整", "如何设置", "如何使用", "在哪里改", "在哪改",
			"how do i change ", "how do i set ", "how do i use ", "how to change ", "how to set ", "how to use ", "where can i change "};
		for(const std::string_view Prefix : aPrefixes)
		{
			if(!Query.starts_with(Prefix))
				continue;
			Query.remove_prefix(Prefix.size());
			Query.remove_prefix(std::min(Query.find_first_not_of(' '), Query.size()));
			if(Query.starts_with("the "))
				Query.remove_prefix(4);
			break;
		}
		constexpr std::string_view aSuffixes[] = {
			"该怎么调整", "该怎么设置", "怎么调整", "怎么设置", "怎么修改", "怎么改", "怎么调", "在哪里设置", "在哪设置",
			"在哪里改", "在哪改", "在哪里调", "在哪调", "设置在哪", "在哪里", "在哪儿", "在哪",
			"有什么用", "是什么", "怎么用", "如何使用", "的用法", "的作用"};
		for(const std::string_view Suffix : aSuffixes)
		{
			if(!Query.ends_with(Suffix))
				continue;
			Query.remove_suffix(Suffix.size());
			break;
		}
		const size_t End = Query.find_last_not_of(' ');
		return End != std::string_view::npos ? Query.substr(0, End + 1) : std::string_view{};
	}
}

#endif
