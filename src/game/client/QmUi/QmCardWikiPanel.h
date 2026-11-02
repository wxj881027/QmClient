// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMCARDWIKIPANEL_H
#define GAME_CLIENT_QMUI_QMCARDWIKIPANEL_H

#include <game/client/ui_rect.h>

struct IUiContext;
struct SWikiDocument;

// 卡片 ⓘ 打开的三段式说明面板。正文按 stableId 解析，标题沿用卡片自己的
// 标题，保证面板与卡片写法一致；没有 Wiki 条目的卡片不会被打开。
void OpenCardWikiPopup(const IUiContext &Ctx, const char *pStableId, const char *pTitle);

// 弹窗内 ⓘ 打开的多段说明（视频导出等长文档）；标题走文档自己的 Localize key。
void OpenWikiDocumentPopup(const IUiContext &Ctx, const SWikiDocument &Document);

// ⓘ 图标的点击登记：调用方在**绘制阶段**把刚画出来的图标矩形传进来，
// 命中即在同一次绘制里打开面板，不需要第二下。
// 抽成公共入口是为了让「按钮身份 + 当前命中标志 + 面板入口」只有一份实现：
// 卡片绘制和集成测试都走这里，不会各自拼一套 ID。
// 返回 true 表示这次点击已经把面板打开。
bool DoCardWikiInfoButton(const IUiContext &Ctx, const char *pStableId, const char *pTitle, const CUIRect &Button);

#if defined(DDNET_TEST_SOURCE_DIR)
// 集成测试观察口：面板状态只活在实现文件里，测试需要断言
// 「Esc 两种语义」「目录跳转是否真的滚动」「面板是否还在弹层栈上」。
// 只在测试目标编译（DDNET_TEST_SOURCE_DIR 由 CMake 注入），不进发布二进制。
namespace qm_wiki_panel_test
{
	bool IsOpen();
	bool IsEditing();
	// 阅读态滚动区当前偏移：向下滚内容时为正，未滚动为 0。
	float ScrollOffsetY();
	// 上一帧画出来的目录行矩形与数量；命中区域就是画出来的那一行。
	int TocRowCount();
	CUIRect TocRowRect(int Index);
	// 最近一次目录点击命中的块下标（-1 表示还没点过）。
	int LastTocClick();
	// 上一帧鼠标是否真的落在该目录行上（含裁剪与弹层输入屏蔽）。
	bool TocRowHovered(int Index);
	// 清掉面板持有的 CUi 指针与本次会话状态（每个测试用例新建 CUi 时调用）。
	void ResetState();
	// 等价于点工具条上的「Edit」：避免测试复刻面板尺寸和按钮宽度。
	bool BeginEditing();
}
#endif

#endif
