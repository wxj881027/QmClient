// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_markdown_broadcast.h"

#include <base/str.h>

#include <new>
#include <stdexcept>

CQmMarkdownBroadcast::EApplyResult CQmMarkdownBroadcast::CheckApply(std::string_view Markdown, int Version) const
{
	if(Markdown.size() > MAX_BYTES || Version < 0 || (m_Version >= 0 && Version < m_Version))
		return EApplyResult::REJECTED;
	if(m_Version == Version && m_Markdown == Markdown)
		return EApplyResult::UNCHANGED;
	return EApplyResult::APPLIED;
}

void CQmMarkdownBroadcast::Publish(std::string &Markdown, int Version)
{
	// 完整哈希准备完成后才发布正文、版本和内容标记。
	char aContentId[SHA256_MAXSTRSIZE];
	sha256_str(sha256(Markdown.data(), Markdown.size()), aContentId, sizeof(aContentId));
	m_Markdown.swap(Markdown);
	m_Version = Version;
	str_copy(m_aContentId, aContentId);
	++m_Revision;
}

CQmMarkdownBroadcast::EApplyResult CQmMarkdownBroadcast::TryApply(std::string_view Markdown, int Version)
{
	const auto Result = CheckApply(Markdown, Version);
	if(Result != EApplyResult::APPLIED)
		return Result;
	try
	{
		std::string Candidate(Markdown);
		Publish(Candidate, Version);
		return EApplyResult::APPLIED;
	}
	catch(const std::bad_alloc &)
	{
		return EApplyResult::RETRY;
	}
	catch(const std::length_error &)
	{
		return EApplyResult::RETRY;
	}
}

CQmMarkdownBroadcast::EApplyResult CQmMarkdownBroadcast::ApplyOwned(std::string &&Markdown, int Version)
{
	const auto Result = CheckApply(Markdown, Version);
	if(Result == EApplyResult::APPLIED)
		Publish(Markdown, Version);
	return Result;
}

bool CQmMarkdownBroadcast::Apply(const std::string &Markdown, int Version)
{
	return TryApply(Markdown, Version) == EApplyResult::APPLIED;
}

bool CQmMarkdownBroadcast::IsUnread(int ReadVersion, const char *pReadContentId) const
{
	return HasMarkdown() && (m_Version > ReadVersion ||
					(m_Version == ReadVersion && (pReadContentId == nullptr || str_comp(m_aContentId, pReadContentId) != 0)));
}
