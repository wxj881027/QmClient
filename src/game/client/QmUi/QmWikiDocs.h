// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKIDOCS_H
#define GAME_CLIENT_QMUI_QMWIKIDOCS_H

#include "QmCardWiki.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

class IStorage;

namespace qm_wiki_docs
{
	const SWikiDocument &VideoExport();

	enum EDdnetWikiLanguage
	{
		ENGLISH,
		SIMPLIFIED_CHINESE,
		NUM_LANGUAGES,
	};
	inline constexpr int NUM_DDNET_WIKI_ARTICLES = 2;

	struct SDdnetWikiArticle
	{
		const char *m_pSlug;
		const char *m_pCardId;
		std::array<const char *, NUM_LANGUAGES> m_apTitles;
		std::array<const char *, NUM_LANGUAGES> m_apIntroductions;
	};

	struct SDdnetWikiMatch
	{
		int m_Article;
		std::array<bool, NUM_LANGUAGES> m_aAvailable;
		std::array<int, NUM_LANGUAGES> m_aBlocks;
		std::array<std::string, NUM_LANGUAGES> m_aExcerpts;
	};

	const SDdnetWikiArticle &DdnetWikiArticle(int Article);
	std::string ReadDdnetWikiArticle(IStorage &Storage, int Article, EDdnetWikiLanguage Language);

	class CDdnetWikiIndex
	{
		struct SSearchBlock
		{
			std::string m_Text;
			std::string m_Excerpt;
			std::string m_Key;
			int m_Block;
			bool m_Heading;
		};
		using TIndexedTranslation = std::optional<std::vector<SSearchBlock>>;
		std::array<std::array<TIndexedTranslation, NUM_LANGUAGES>, NUM_DDNET_WIKI_ARTICLES> m_aTranslations;
		IStorage *m_pStorage = nullptr;

	public:
		std::vector<SDdnetWikiMatch> Search(const char *pQuery, IStorage *pStorage);
	};
}

#endif
