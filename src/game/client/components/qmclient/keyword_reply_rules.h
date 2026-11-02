// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_KEYWORD_REPLY_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_KEYWORD_REPLY_RULES_H

#include <base/system.h>

#include <engine/external/regex.h>

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace QmKeywordReplyRules
{
	inline std::vector<std::string> ReplyTokens(const char *pReplies)
	{
		std::vector<std::string> vReplies;
		std::string_view Remaining(pReplies ? pReplies : "");
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
		return vReplies;
	}

	template<typename F>
	inline bool PickReply(const char *pReplies, char *pOut, size_t OutSize, F &&RandomBelow)
	{
		if(!pOut || OutSize == 0)
			return false;
		pOut[0] = '\0';
		const auto vReplies = ReplyTokens(pReplies);
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

	// 先完成编码并检查容量，失败时保持调用方的原配置。
	inline bool EncodeForConfig(const char *pRules, char *pOut, size_t OutSize)
	{
		if(!pOut || OutSize == 0)
			return false;
		std::string Encoded;
		if(pRules)
		{
			for(const char *pCursor = pRules; *pCursor; ++pCursor)
			{
				if(*pCursor == '\\')
					Encoded += "\\\\";
				else if(*pCursor == '\r' && pCursor[1] == '\n')
					continue;
				else if(*pCursor == '\n' || *pCursor == '\r')
					Encoded += "\\n";
				else
					Encoded += *pCursor;
				if(Encoded.size() >= OutSize)
					return false;
			}
		}
		str_copy(pOut, Encoded.c_str(), OutSize);
		return true;
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

	struct SRule
	{
		std::string m_Keywords;
		std::string m_Reply;
		bool m_AutoRename = false;
		bool m_Regex = false;
	};

	inline std::string Trim(std::string Text)
	{
		str_utf8_trim_right(Text.data());
		return str_utf8_skip_whitespaces(Text.c_str());
	}

	inline std::vector<SRule> ParseDecoded(const char *pRules)
	{
		std::vector<SRule> vRules;
		std::string_view Remaining(pRules ? pRules : "");
		while(!Remaining.empty())
		{
			const size_t End = Remaining.find_first_of("\r\n");
			std::string Line = Trim(std::string(Remaining.substr(0, End)));
			Remaining = End == std::string_view::npos ? std::string_view{} : Remaining.substr(End + 1);
			if(Line.empty() || Line.front() == '#')
				continue;
			SRule Rule;
			const char *pText = Line.c_str();
			while(true)
			{
				const char *pNext = str_startswith_nocase(pText, "[rename]");
				if(!pNext)
					pNext = str_startswith_nocase(pText, "[r]");
				if(pNext)
					Rule.m_AutoRename = true;
				else
				{
					pNext = str_startswith_nocase(pText, "[regex]");
					if(!pNext)
						pNext = str_startswith_nocase(pText, "[re]");
					if(!pNext)
						pNext = str_startswith_nocase(pText, "[rx]");
					if(pNext)
						Rule.m_Regex = true;
				}
				if(!pNext)
					break;
				pText = str_utf8_skip_whitespaces(pNext);
			}
			const char *pArrow = str_find(pText, "=>");
			if(!pArrow)
				continue;
			Rule.m_Keywords = Trim(std::string(pText, pArrow));
			Rule.m_Reply = Trim(pArrow + 2);
			if(!Rule.m_Keywords.empty() && !Rule.m_Reply.empty())
				vRules.push_back(std::move(Rule));
		}
		return vRules;
	}

	inline bool EncodeRules(const std::vector<SRule> &vRules, char *pOut, size_t OutSize)
	{
		std::string Decoded;
		for(const auto &Rule : vRules)
		{
			if(Rule.m_Keywords.empty() || Rule.m_Reply.empty())
				continue;
			if(!Decoded.empty())
				Decoded += '\n';
			if(Rule.m_AutoRename)
				Decoded += "[rename] ";
			if(Rule.m_Regex)
				Decoded += "[regex] ";
			Decoded += Rule.m_Keywords + "=>" + Rule.m_Reply;
		}
		return EncodeForConfig(Decoded.c_str(), pOut, OutSize);
	}

	inline std::vector<std::string> LiteralTokens(const std::string &Keywords)
	{
		std::vector<std::string> vTokens;
		const char *pStart = Keywords.c_str();
		const char *pCursor = pStart;
		while(*pCursor)
		{
			const char *pChar = pCursor;
			const int Codepoint = str_utf8_decode(&pCursor);
			if(Codepoint == ',' || Codepoint == ';' || Codepoint == '|' || Codepoint == '\n' || Codepoint == '\r' ||
				Codepoint == 0xFF0C || Codepoint == 0xFF1B || Codepoint == 0xFF5C)
			{
				std::string Token = Trim(std::string(pStart, pChar));
				if(!Token.empty())
					vTokens.push_back(std::move(Token));
				pStart = pCursor;
			}
		}
		std::string Token = Trim(pStart);
		if(!Token.empty())
			vTokens.push_back(std::move(Token));
		return vTokens;
	}

	inline std::string RegexPattern(const std::string &Keywords)
	{
		if(Keywords.size() >= 2 && Keywords.front() == '/')
		{
			bool Escaped = false;
			for(size_t i = 1; i < Keywords.size(); ++i)
			{
				if(Escaped)
					Escaped = false;
				else if(Keywords[i] == '\\')
					Escaped = true;
				else if(Keywords[i] == '/')
				{
					const std::string Flags = Keywords.substr(i + 1);
					if(Flags.empty() || Flags == "i" || Flags == "I")
						return Keywords.substr(1, i - 1);
					break;
				}
			}
		}
		return Keywords;
	}

	class CRuleSet
	{
		struct SCompiledRule
		{
			SRule m_Rule;
			std::vector<std::string> m_vTokens;
			std::vector<std::string> m_vReplies;
			std::optional<Regex> m_Regex;
		};
		std::string m_Config;
		std::vector<SCompiledRule> m_vRules;

	public:
		void SetConfig(const char *pConfig)
		{
			if(!pConfig)
				pConfig = "";
			if(m_Config == pConfig)
				return;
			m_Config = pConfig;
			std::vector<char> vDecoded(m_Config.size() + 1);
			DecodeFromConfig(m_Config.c_str(), vDecoded.data(), vDecoded.size());
			m_vRules.clear();
			for(auto &Rule : ParseDecoded(vDecoded.data()))
			{
				SCompiledRule Compiled;
				Compiled.m_Rule = std::move(Rule);
				Compiled.m_vReplies = ReplyTokens(Compiled.m_Rule.m_Reply.c_str());
				if(Compiled.m_Rule.m_Regex)
					Compiled.m_Regex.emplace(RegexPattern(Compiled.m_Rule.m_Keywords), true);
				else
					Compiled.m_vTokens = LiteralTokens(Compiled.m_Rule.m_Keywords);
				m_vRules.push_back(std::move(Compiled));
			}
		}

		template<typename F>
		bool Match(const char *pMessage, char *pReply, size_t ReplySize, bool &AutoRename, F &&RandomBelow)
		{
			AutoRename = false;
			if(!pMessage || !pReply || ReplySize == 0)
				return false;
			pReply[0] = '\0';
			std::vector<const SCompiledRule *> vMatches;
			for(auto &Compiled : m_vRules)
			{
				bool Matched = false;
				if(Compiled.m_Regex)
				{
					try
					{
						Matched = Compiled.m_Regex->error().empty() && Compiled.m_Regex->test(pMessage);
					}
					catch(const std::regex_error &)
					{
						// 正则运行时复杂度异常不能中断聊天消息处理。
					}
				}
				else
					Matched = std::any_of(Compiled.m_vTokens.begin(), Compiled.m_vTokens.end(), [&](const std::string &Token) { return str_utf8_find_nocase(pMessage, Token.c_str()) != nullptr; });
				if(Matched && !Compiled.m_vReplies.empty())
					vMatches.push_back(&Compiled);
			}
			if(vMatches.empty())
				return false;
			const SCompiledRule &Rule = *vMatches[RandomBelow(static_cast<int>(vMatches.size()))];
			const int Index = Rule.m_vReplies.size() == 1 ? 0 : RandomBelow(static_cast<int>(Rule.m_vReplies.size()));
			str_copy(pReply, Rule.m_vReplies[Index].c_str(), ReplySize);
			AutoRename = Rule.m_Rule.m_AutoRename;
			return pReply[0] != '\0';
		}
	};

} // namespace QmKeywordReplyRules

#endif
