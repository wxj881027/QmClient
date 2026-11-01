// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_H

#include <base/hash.h>

#include <cstddef>
#include <string>
#include <string_view>

inline bool QmNewsShouldAnnounce(bool Offline, bool StartMenu, bool PopupOpen, bool ReleaseChanged, bool UnreadBroadcast)
{
	return Offline && StartMenu && !PopupOpen && (ReleaseChanged || UnreadBroadcast);
}

class CQmMarkdownBroadcast
{
	std::string m_Markdown;
	int m_Version = -1;
	int m_Revision = 0;
	char m_aContentId[SHA256_MAXSTRSIZE] = {};

public:
	static constexpr size_t MAX_BYTES = 64 * 1024;
	enum class EApplyResult
	{
		REJECTED,
		UNCHANGED,
		APPLIED,
		RETRY
	};

private:
	EApplyResult CheckApply(std::string_view Markdown, int Version) const;
	void Publish(std::string &Markdown, int Version);

public:
	// 复制准备失败保留已发布内容；调用方持有输入并在后续帧重试。
	EApplyResult TryApply(std::string_view Markdown, int Version);
	// 拒收时不消费输入；成功时交换所有权，不复制消息正文。
	EApplyResult ApplyOwned(std::string &&Markdown, int Version);
	bool Apply(const std::string &Markdown, int Version);
	bool HasMarkdown() const { return !m_Markdown.empty(); }
	const char *Markdown() const { return m_Markdown.c_str(); }
	int Version() const { return m_Version; }
	int Revision() const { return m_Revision; }
	const char *ContentId() const { return m_aContentId; }
	bool IsUnread(int ReadVersion, const char *pReadContentId) const;
};

#endif
