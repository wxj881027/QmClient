#ifndef GAME_CLIENT_QMUI_CARDS_QMKEYWORDREPLYCARDPERSISTENCE_H
#define GAME_CLIENT_QMUI_CARDS_QMKEYWORDREPLYCARDPERSISTENCE_H

#include <base/str.h>

#include <game/client/components/qmclient/keyword_reply_rules.h>

#include <cstddef>
#include <vector>

namespace qm_keyword_reply_card
{
	enum class ESaveResult
	{
		SAVED,
		TOO_LONG,
		UNREPRESENTABLE,
		UNAVAILABLE,
	};

	inline bool CompleteRulesMatch(const std::vector<QmKeywordReplyRules::SRule> &vEditorRules, const std::vector<QmKeywordReplyRules::SRule> &vParsedRules)
	{
		auto It = vParsedRules.begin();
		for(const auto &Rule : vEditorRules)
		{
			if(Rule.m_Keywords.empty() || Rule.m_Reply.empty())
				continue;
			if(It == vParsedRules.end() || It->m_Keywords != Rule.m_Keywords ||
				It->m_Reply != Rule.m_Reply || It->m_AutoRename != Rule.m_AutoRename ||
				It->m_Regex != Rule.m_Regex)
				return false;
			++It;
		}
		return It == vParsedRules.end();
	}

	template<std::size_t Size>
	ESaveResult SaveRules(const std::vector<QmKeywordReplyRules::SRule> &vRules, char (&aConfig)[Size])
	{
		char aEncoded[Size];
		if(!QmKeywordReplyRules::EncodeRules(vRules, aEncoded, Size))
			return ESaveResult::TOO_LONG;

		// 用运行时的读取规则核对触发词、回复和选项，确认能原样读回再写入配置。
		char aDecoded[Size];
		QmKeywordReplyRules::DecodeFromConfig(aEncoded, aDecoded, Size);
		if(!CompleteRulesMatch(vRules, QmKeywordReplyRules::ParseDecoded(aDecoded)))
			return ESaveResult::UNREPRESENTABLE;

		str_copy(aConfig, aEncoded, Size);
		return ESaveResult::SAVED;
	}
}

#endif
