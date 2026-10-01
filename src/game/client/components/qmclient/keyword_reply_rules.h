// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_KEYWORD_REPLY_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_KEYWORD_REPLY_RULES_H

#include <base/system.h>

#include <string>
#include <string_view>
#include <vector>

namespace QmKeywordReplyRules
{
	template<typename F>
	inline bool PickReply(const char *pReplies, char *pOut, size_t OutSize, F &&RandomBelow)
	{
		if(!pOut || OutSize == 0)
			return false;
		pOut[0] = '\0';
		if(!pReplies)
			return false;

		std::vector<std::string> vReplies;
		std::string_view Remaining(pReplies);
		while(!Remaining.empty())
		{
			const size_t Separator = Remaining.find('|');
			std::string Reply(Remaining.substr(0, Separator));
			char *pTrimmedReply = Reply.data() + (str_utf8_skip_whitespaces(Reply.c_str()) - Reply.c_str());
			str_utf8_trim_right(pTrimmedReply);
			if(pTrimmedReply[0] != '\0')
				vReplies.emplace_back(pTrimmedReply);
			if(Separator == std::string_view::npos)
				break;
			Remaining.remove_prefix(Separator + 1);
		}
		if(vReplies.empty())
			return false;

		const int PickedIndex = vReplies.size() == 1 ? 0 : RandomBelow(static_cast<int>(vReplies.size()));
		str_copy(pOut, vReplies[PickedIndex].c_str(), OutSize);
		return pOut[0] != '\0';
	}

	struct SEditorChanges
	{
		bool m_Added = false;
		bool m_Removed = false;
		bool m_TriggerText = false;
		bool m_ReplyText = false;
		bool m_Rename = false;
		bool m_Regex = false;

		bool Any() const
		{
			return m_Added || m_Removed || m_TriggerText || m_ReplyText || m_Rename || m_Regex;
		}

		bool ShouldCommit(bool RenderOnly) const
		{
			return Any() && !RenderOnly;
		}
	};

	inline bool EditorConfigChanged(bool Initialized, const char *pCachedConfig, const char *pConfig)
	{
		return !Initialized || str_comp(pCachedConfig != nullptr ? pCachedConfig : "", pConfig != nullptr ? pConfig : "") != 0;
	}

	inline void EncodeForConfig(const char *pRules, char *pOut, size_t OutSize)
	{
		if(!pOut || OutSize == 0)
			return;

		pOut[0] = '\0';
		if(!pRules)
			return;

		size_t OutPos = 0;
		for(const char *pCursor = pRules; *pCursor && OutPos + 1 < OutSize; ++pCursor)
		{
			if(*pCursor == '\\')
			{
				if(OutPos + 2 >= OutSize)
					break;
				pOut[OutPos++] = '\\';
				pOut[OutPos++] = '\\';
			}
			else if(*pCursor == '\r')
			{
				if(pCursor[1] == '\n')
					continue;
				if(OutPos + 2 >= OutSize)
					break;
				pOut[OutPos++] = '\\';
				pOut[OutPos++] = 'n';
			}
			else if(*pCursor == '\n')
			{
				if(OutPos + 2 >= OutSize)
					break;
				pOut[OutPos++] = '\\';
				pOut[OutPos++] = 'n';
			}
			else
			{
				pOut[OutPos++] = *pCursor;
			}
		}
		pOut[OutPos] = '\0';
	}

	inline void DecodeFromConfig(const char *pRules, char *pOut, size_t OutSize)
	{
		if(!pOut || OutSize == 0)
			return;

		pOut[0] = '\0';
		if(!pRules)
			return;

		size_t OutPos = 0;
		for(const char *pCursor = pRules; *pCursor && OutPos + 1 < OutSize; ++pCursor)
		{
			if(pCursor[0] == '\\' && pCursor[1] == '\\')
			{
				pOut[OutPos++] = '\\';
				++pCursor;
			}
			else if(pCursor[0] == '\\' && pCursor[1] == 'n')
			{
				pOut[OutPos++] = '\n';
				++pCursor;
			}
			else
			{
				pOut[OutPos++] = *pCursor;
			}
		}
		pOut[OutPos] = '\0';
	}

} // namespace QmKeywordReplyRules

#endif
