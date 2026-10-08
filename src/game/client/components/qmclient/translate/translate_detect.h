#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_DETECT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_DETECT_H

// 玩家引用只影响语言统计，原始消息保持不变。
namespace qm_translate
{
	struct SPlayerReference
	{
		const char *m_pName;
		int m_ClientId;
	};

	struct SLanguageStats
	{
		int m_Han = 0;
		int m_Kana = 0;
		int m_Hangul = 0;
		int m_Cyrillic = 0;
		int m_Latin = 0;
		int m_Other = 0;
		int m_Digits = 0;
		int m_Meaningful = 0;
		int m_ScriptTotal = 0;
	};

	SLanguageStats AnalyzeLanguage(const char *pText, const SPlayerReference *pPlayers = nullptr, int NumPlayers = 0);
	bool IsChineseLanguage(const char *pLanguage);
	bool IsChineseVariantLanguage(const char *pLanguage);
	bool PassLocalDetectThreshold(int Count, int Total, int Ratio);
	bool IsPredominantlyNumeric(const SLanguageStats &Stats, int MinChars, int Ratio);
	bool MatchesTargetLanguageHeuristically(const SLanguageStats &Stats, const char *pTarget, int MinChars, int Ratio);
	bool ShouldTranslateIncoming(const SLanguageStats &Stats, const char *pTarget, int MinChars, int Ratio, bool Always);
	bool ShouldTranslateOutgoing(const SLanguageStats &Stats, const char *pTarget, const char *pSource, int MinChars, int Ratio, bool Always);
}

#endif
