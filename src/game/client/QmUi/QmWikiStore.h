// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKISTORE_H
#define GAME_CLIENT_QMUI_QMWIKISTORE_H

#include <string>

class IStorage;
struct SWikiDocument;

struct SWikiContent
{
	std::string m_Markdown;
	bool m_HasUserOverride = false;
	bool m_HasShippedFile = false;
	// 相对 wiki/ 的文档路径（不含语言目录与扩展名），界面上用来提示编辑位置。
	std::string m_RelativePath;
};

namespace qm_wiki_store
{
	void Init(IStorage *pStorage);
	// 当前语言目录名：取 cl_languagefile 去掉 .txt，例如 simplified_chinese。
	const char *LanguageDir();

	// 解析顺序：本地覆盖 -> 随包 wiki/<lang>/...md -> 内置文案合成的 Markdown。
	SWikiContent ResolveCard(const char *pStableId, const SWikiDocument &Fallback);
	SWikiContent ResolveDocument(const char *pDocumentId, const SWikiDocument &Fallback);

	bool SaveOverride(const char *pRelativePath, const std::string &Markdown, std::string *pError);
	bool DeleteOverride(const char *pRelativePath);
	bool HasOverride(const char *pRelativePath);

	// stableId 转可读路径：qm:voice -> qm/voice，deck:xxx -> deck/xxx。
	std::string IdToRelativePath(const char *pId);
	// 用内置文档拼出 Markdown，供“还原内置”和编辑初始内容使用。
	std::string ComposeFromDocument(const SWikiDocument &Document);
} // namespace qm_wiki_store

#endif
