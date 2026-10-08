// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_markdown_broadcast.h"

#include <base/str.h>

bool CQmMarkdownBroadcast::Apply(const std::string &Markdown, int Version)
{
	if(Markdown.size() > MAX_BYTES || Version < 0 || (m_Version >= 0 && Version < m_Version))
		return false;
	if(m_Version == Version && m_Markdown == Markdown)
		return false;
	m_Markdown = Markdown;
	m_Version = Version;
	// 服务允许同一版本修订已知问题，内容标记必须在重启后保持稳定。
	sha256_str(sha256(Markdown.data(), Markdown.size()), m_aContentId, sizeof(m_aContentId));
	++m_Revision;
	return true;
}

bool CQmMarkdownBroadcast::IsUnread(int ReadVersion, const char *pReadContentId) const
{
	return HasMarkdown() && (m_Version > ReadVersion ||
		(m_Version == ReadVersion && (pReadContentId == nullptr || str_comp(m_aContentId, pReadContentId) != 0)));
}
