// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_markdown_broadcast.h"

bool CQmMarkdownBroadcast::Apply(const std::string &Markdown, int Version)
{
	if(Markdown.size() > MAX_BYTES || Version < 0 || (m_Version >= 0 && Version < m_Version))
		return false;
	if(m_Version == Version && m_Markdown == Markdown)
		return false;
	m_Markdown = Markdown;
	m_Version = Version;
	++m_Revision;
	return true;
}
