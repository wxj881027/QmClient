// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMCARDWIKI_H
#define GAME_CLIENT_QMUI_QMCARDWIKI_H

#include <cstddef>

// 卡片 Wiki 文案：按 stableId 提供的三段式说明（这是什么 / 怎么用 / 建议）。
// 纯数据表，不依赖 UI；悬浮摘要与二级面板共用同一条条目，卡片本身不改结构。
struct SCardWikiCopy
{
	const char *m_pStableId;
	const char *m_pWhat; // 一句话说明这张卡控制什么
	const char *m_pHow; // 操作路径，使用界面上真实存在的控件名
	const char *m_pAdvice; // 新手建议或需要留意的副作用
};

// 弹出的 Wiki 文档：一张卡的三段说明、一个弹窗的完整说明都编译成同一种文档，
// 面板只认文档，不关心内容来自卡片表还是弹窗表。
struct SWikiSection
{
	const char *m_pHeading; // Localize key
	const char *const *m_apParagraphs; // Localize key 数组
	int m_ParagraphCount;
};

struct SWikiDocument
{
	const char *m_pId; // 稳定标识，供测试与调试定位
	const char *m_pTitle; // Localize key
	const SWikiSection *m_pSections;
	int m_SectionCount;
};

// 首段同时对卡片 ⓘ 的悬浮提示提供摘要，因此条目一出现就带悬浮说明。
const SCardWikiCopy *FindCardWikiCopy(const char *pStableId);
bool HasCardWikiCopy(const char *pStableId);
size_t CardWikiCopyCount();
const SCardWikiCopy *CardWikiCopyAt(size_t Index);

#endif
