#include "translate_detect.h"

#include <base/system.h>

#include <algorithm>

namespace qm_translate
{
	bool IsChineseLanguage(const char *pLanguage)
	{
		if(!pLanguage || pLanguage[0] == '\0')
			return false;
		return str_comp_nocase(pLanguage, "zh") == 0 ||
		       str_comp_nocase(pLanguage, "zh-cn") == 0 ||
		       str_comp_nocase(pLanguage, "zh-tw") == 0;
	}

	bool IsChineseVariantLanguage(const char *pLanguage)
	{
		if(!pLanguage || pLanguage[0] == '\0')
			return false;
		return str_comp_nocase(pLanguage, "zh-cn") == 0 ||
		       str_comp_nocase(pLanguage, "zh-tw") == 0;
	}

	namespace
	{
		bool IsNeutralCodepoint(int Codepoint)
		{
			return Codepoint <= 0x20 || (Codepoint < 0x80 && !((Codepoint >= 'A' && Codepoint <= 'Z') || (Codepoint >= 'a' && Codepoint <= 'z') || (Codepoint >= '0' && Codepoint <= '9'))) ||
			       Codepoint == 0xA0 || (Codepoint >= 0x2000 && Codepoint <= 0x206F) ||
			       (Codepoint >= 0x3000 && Codepoint <= 0x303F) ||
			       (Codepoint >= 0xFE00 && Codepoint <= 0xFE0F) ||
			       (Codepoint >= 0xFF01 && Codepoint <= 0xFF0F) ||
			       (Codepoint >= 0xFF1A && Codepoint <= 0xFF20) ||
			       (Codepoint >= 0xFF3B && Codepoint <= 0xFF40) ||
			       (Codepoint >= 0xFF5B && Codepoint <= 0xFF65) ||
			       (Codepoint >= 0x2190 && Codepoint <= 0x2BFF) ||
			       (Codepoint >= 0x1F000 && Codepoint <= 0x1FAFF) ||
			       (Codepoint >= 0xE0100 && Codepoint <= 0xE01EF);
		}

		bool IsReferenceBoundary(const char *pText)
		{
			if(!*pText)
				return true;
			const int Codepoint = str_utf8_decode(&pText);
			return Codepoint > 0 && IsNeutralCodepoint(Codepoint);
		}

		const char *AfterAddressColon(const char *pText)
		{
			while(*pText == ' ' || *pText == '\t')
				++pText;
			if(*pText == ':')
				return pText + 1;
			if(str_startswith(pText, "："))
				return pText + 3;
			return nullptr;
		}

		int ReferenceLength(const char *pText, bool AtStart, bool AtBoundary, const SPlayerReference *pPlayers, int NumPlayers)
		{
			const bool Mention = *pText == '@' && AtBoundary;
			if(!pPlayers || NumPlayers <= 0 || (!Mention && !AtStart))
				return 0;
			const char *pName = pText + (Mention ? 1 : 0);
			int BestLength = 0;
			for(int i = 0; i < NumPlayers; ++i)
			{
				const char *pPlayerName = pPlayers[i].m_pName;
				if(!pPlayerName || !*pPlayerName)
					continue;
				const int Length = str_length(pPlayerName);
				if(str_comp_num(pName, pPlayerName, Length) != 0)
					continue;
				const char *pEnd = pName + Length;
				if(Mention ? IsReferenceBoundary(pEnd) : AfterAddressColon(pEnd) != nullptr)
					BestLength = std::max(BestLength, Length + (Mention ? 1 : 0));
			}
			// 数字 ID 只接受显式 @ID，且必须对应当前连接的玩家。
			if(Mention && *pName >= '0' && *pName <= '9')
			{
				const char *pEnd = pName;
				int Id = 0;
				while(*pEnd >= '0' && *pEnd <= '9' && pEnd - pName < 4)
					Id = Id * 10 + *pEnd++ - '0';
				if(IsReferenceBoundary(pEnd))
				{
					for(int i = 0; i < NumPlayers; ++i)
						if(pPlayers[i].m_ClientId == Id)
							BestLength = std::max(BestLength, static_cast<int>(pEnd - pText));
				}
			}
			return BestLength;
		}
	}

	SLanguageStats AnalyzeLanguage(const char *pText, const SPlayerReference *pPlayers, int NumPlayers)
	{
		SLanguageStats Stats;
		if(!pText)
			return Stats;

		const char *p = pText;
		bool AtStart = true;
		bool AtBoundary = true;
		while(*p)
		{
			const int Skip = ReferenceLength(p, AtStart, AtBoundary, pPlayers, NumPlayers);
			if(Skip > 0)
			{
				p += Skip;
				AtStart = false;
				AtBoundary = false;
				continue;
			}
			const int Codepoint = str_utf8_decode(&p);
			if(Codepoint <= 0)
			{
				AtStart = false;
				AtBoundary = false;
				continue;
			}
			AtStart = AtStart && (Codepoint == ' ' || Codepoint == '\t');
			AtBoundary = IsNeutralCodepoint(Codepoint);

			const bool IsHan = (Codepoint >= 0x4E00 && Codepoint <= 0x9FFF) ||
					   (Codepoint >= 0x3400 && Codepoint <= 0x4DBF);
			if(IsHan)
			{
				++Stats.m_Han;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
			else if(Codepoint >= 0x3040 && Codepoint <= 0x30FF)
			{
				++Stats.m_Kana;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
			else if(Codepoint >= 0xAC00 && Codepoint <= 0xD7AF)
			{
				++Stats.m_Hangul;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
			else if(Codepoint >= 0x0400 && Codepoint <= 0x04FF)
			{
				++Stats.m_Cyrillic;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
			else if((Codepoint >= 'A' && Codepoint <= 'Z') || (Codepoint >= 'a' && Codepoint <= 'z') ||
				(Codepoint >= 0x00C0 && Codepoint <= 0x024F))
			{
				++Stats.m_Latin;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
			else if(Codepoint >= '0' && Codepoint <= '9')
			{
				++Stats.m_Digits;
				++Stats.m_Meaningful;
			}
			else if(!IsNeutralCodepoint(Codepoint))
			{
				++Stats.m_Other;
				++Stats.m_Meaningful;
				++Stats.m_ScriptTotal;
			}
		}

		return Stats;
	}

	bool PassLocalDetectThreshold(int Count, int Total, int Ratio)
	{
		if(Count <= 0 || Total <= 0)
			return false;
		const int Threshold = std::clamp(Ratio, 50, 100);
		return Count * 100 >= Total * Threshold;
	}

	bool IsPredominantlyNumeric(const SLanguageStats &Stats, int MinChars, int Ratio)
	{
		MinChars = std::clamp(MinChars, 1, 12);
		return Stats.m_Digits >= MinChars && PassLocalDetectThreshold(Stats.m_Digits, Stats.m_Meaningful, Ratio);
	}

	bool MatchesTargetLanguageHeuristically(const SLanguageStats &Stats, const char *pTarget, int MinChars, int Ratio)
	{
		if(!pTarget || pTarget[0] == '\0' || Stats.m_ScriptTotal <= 0)
			return false;

		if(IsChineseLanguage(pTarget))
		{
			if(IsChineseVariantLanguage(pTarget))
				return false;
			// 纯汉字短句无需满足混合文本的最少字符门槛；中文变体仍交给后端转换。
			if(Stats.m_Han == Stats.m_ScriptTotal)
				return true;
			MinChars = std::clamp(MinChars, 1, 12);
			return Stats.m_Han >= MinChars && Stats.m_Kana == 0 && Stats.m_Hangul == 0 &&
			       PassLocalDetectThreshold(Stats.m_Han, Stats.m_ScriptTotal, Ratio);
		}
		if(str_comp_nocase(pTarget, "ja") == 0)
		{
			MinChars = std::clamp(MinChars, 1, 12);
			return Stats.m_Kana + Stats.m_Han >= MinChars &&
			       PassLocalDetectThreshold(Stats.m_Kana + Stats.m_Han, Stats.m_ScriptTotal, Ratio);
		}
		if(str_comp_nocase(pTarget, "ko") == 0)
		{
			MinChars = std::clamp(MinChars, 1, 12);
			return Stats.m_Hangul >= MinChars &&
			       PassLocalDetectThreshold(Stats.m_Hangul, Stats.m_ScriptTotal, Ratio);
		}
		if(str_comp_nocase(pTarget, "ru") == 0)
		{
			MinChars = std::clamp(MinChars, 1, 12);
			return Stats.m_Cyrillic >= MinChars &&
			       PassLocalDetectThreshold(Stats.m_Cyrillic, Stats.m_ScriptTotal, Ratio);
		}

		return false;
	}

	bool ShouldTranslateIncoming(const SLanguageStats &Stats, const char *pTarget, int MinChars, int Ratio, bool Always)
	{
		return Stats.m_ScriptTotal > 0 && !IsPredominantlyNumeric(Stats, MinChars, Ratio) &&
		       (Always || !MatchesTargetLanguageHeuristically(Stats, pTarget, MinChars, Ratio));
	}
}
