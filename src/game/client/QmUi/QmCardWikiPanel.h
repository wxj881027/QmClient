// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMCARDWIKIPANEL_H
#define GAME_CLIENT_QMUI_QMCARDWIKIPANEL_H

struct IUiContext;
struct SWikiDocument;

// 卡片 ⓘ 打开的三段式说明面板。正文按 stableId 解析，标题沿用卡片自己的
// 标题，保证面板与卡片写法一致；没有 Wiki 条目的卡片不会被打开。
void OpenCardWikiPopup(const IUiContext &Ctx, const char *pStableId, const char *pTitle);

// 弹窗内 ⓘ 打开的多段说明（视频导出等长文档）；标题走文档自己的 Localize key。
void OpenWikiDocumentPopup(const IUiContext &Ctx, const SWikiDocument &Document);

#endif
