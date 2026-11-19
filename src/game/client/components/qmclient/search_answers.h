#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SEARCH_ANSWERS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SEARCH_ANSWERS_H

#include <vector>

namespace qm_search_answers
{

	struct SAnswer
	{
		const char *m_pId;
		const char *m_pQuestion;
		const char *m_pAnswer;
		std::vector<const char *> m_vAliases;
		std::vector<const char *> m_vRelatedCards;
		const char *m_pCommand = nullptr;
		// 相对于 TYPE_SAVE 的固定目录；不接收搜索文本或外部文档中的路径。
		const char *m_pDirectory = nullptr;
	};

	struct SResult
	{
		std::vector<const SAnswer *> m_vCandidates;
		const SAnswer *m_pAnswer = nullptr;
	};

	// 问答表随客户端发布，返回的条目地址在进程内保持有效。
	const std::vector<SAnswer> &Answers();
	SResult Search(const char *pQuery);
	// 查询及静态索引完整准备后才替换输出；资源不足时保留原结果供后续重试。
	bool TrySearch(const char *pQuery, SResult &Out);
}

#endif
