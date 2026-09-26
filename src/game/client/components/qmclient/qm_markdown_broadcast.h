// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H

#include <cstddef>
#include <string>

class CQmMarkdownBroadcast
{
	static constexpr size_t MAX_BYTES = 64 * 1024;
	std::string m_Markdown;
	int m_Version = -1;
	int m_Revision = 0;

public:
	bool Apply(const std::string &Markdown, int Version);
	bool HasMarkdown() const { return !m_Markdown.empty(); }
	const char *Markdown() const { return m_Markdown.c_str(); }
	int Version() const { return m_Version; }
	int Revision() const { return m_Revision; }
};

#endif
