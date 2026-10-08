// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H

#include <base/hash.h>

#include <cstddef>
#include <string>

inline bool QmNewsShouldAnnounce(bool Offline, bool StartMenu, bool PopupOpen, bool ReleaseChanged, bool UnreadBroadcast)
{
	return Offline && StartMenu && !PopupOpen && (ReleaseChanged || UnreadBroadcast);
}

class CQmMarkdownBroadcast
{
	static constexpr size_t MAX_BYTES = 64 * 1024;
	std::string m_Markdown;
	int m_Version = -1;
	int m_Revision = 0;
	char m_aContentId[SHA256_MAXSTRSIZE] = {};

public:
	bool Apply(const std::string &Markdown, int Version);
	bool HasMarkdown() const { return !m_Markdown.empty(); }
	const char *Markdown() const { return m_Markdown.c_str(); }
	int Version() const { return m_Version; }
	int Revision() const { return m_Revision; }
	const char *ContentId() const { return m_aContentId; }
	bool IsUnread(int ReadVersion, const char *pReadContentId) const;
};

#endif
