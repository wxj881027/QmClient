// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMCARDWIKI_H
#define GAME_CLIENT_QMUI_QMCARDWIKI_H

#include <cstddef>

// 卡片 Wiki 文案：按 stableId 提供的两段式说明（原理与机制 / 建议设置）。
// 纯数据表，不依赖 UI；摘要、机制和建议共用同一条条目，卡片本身不改结构。
struct SCardWikiCopy
{
	const char *m_pStableId;
	const char *m_pSummary; // 一句话说明这张卡控制什么
	const char *m_pMechanism; // 每个开关/滑杆的作用机制，使用界面上真实存在的控件名
	const char *m_pRecommendation; // 推荐设置及理由

	constexpr SCardWikiCopy(const char *pStableId, const char *pSummary, const char *pMechanism, const char *pRecommendation) :
		m_pStableId(pStableId), m_pSummary(pSummary), m_pMechanism(pMechanism), m_pRecommendation(pRecommendation)
	{
	}
};

// 弹出的 Wiki 文档：一张卡的两段说明、一个弹窗的完整说明都编译成同一种文档，
// 面板只认文档，不关心内容来自卡片表还是弹窗表。
struct SWikiSection
{
	const char *m_pHeading; // Localize key
	const char *const *m_apParagraphs; // Localize key 数组
	int m_ParagraphCount;
	// 同一段正文的本地化上下文；空串表示按普通 key 查找。
	// 卡片 Wiki 用独立上下文，避免长说明与设置页短句共用同一个 key。
	const char *m_pContext = "";
	// 旧文档用分号拆步骤；卡片 Wiki 的机制说明本身就用分号断句，不再二次拆成要点。
	bool m_SplitSemicolons = true;
};

struct SWikiDocument
{
	const char *m_pId; // 稳定标识，供测试与调试定位
	const char *m_pTitle; // Localize key
	const SWikiSection *m_pSections;
	int m_SectionCount;
};

// 摘要和机制同属第一节，避免把“这张卡是什么”和“开关怎么工作”拆成两个标题。
const SCardWikiCopy *FindCardWikiCopy(const char *pStableId);
bool HasCardWikiCopy(const char *pStableId);
size_t CardWikiCopyCount();
const SCardWikiCopy *CardWikiCopyAt(size_t Index);

#endif
