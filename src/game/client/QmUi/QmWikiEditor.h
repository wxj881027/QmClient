// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKIEDITOR_H
#define GAME_CLIENT_QMUI_QMWIKIEDITOR_H

#include "QmWikiDocs.h"
#include "QmWikiMarkdown.h"
#include "QmWikiStore.h"

#include <string>
#include <vector>

// ⓘ 面板的编辑状态机：内容加载、草稿生命周期、保存与还原内置。
// 面板只做绘制与输入转发，状态转换集中在这里，才能在不启动完整 UI 的情况下
// 通过真实存储与 Markdown 解析验证「保存后内容来自用户目录」「还原真的删文件」
// 「Esc 退出编辑但草稿还在」这类跨模块行为。
// 草稿是普通字符串而不是 CLineInput：状态机不承担编辑框职责，测试也不需要文字引擎。
struct SWikiEditorState
{
	// 内置兜底内容：随包文件与用户覆盖都不存在时使用，也是「还原内置」的目标。
	SWikiDocument m_Fallback{};
	char m_aId[96] = "";
	bool m_IsCard = true;

	SWikiContent m_Content;
	CWikiMarkdown m_Markdown;
	// 目录只收前三级标题；少于两个标题时不显示目录。
	std::vector<int> m_vTocBlocks;

	bool m_Editing = false;
	// 有未保存草稿时不再用磁盘内容覆盖编辑框（Esc / Done 退出编辑后草稿仍可继续）。
	bool m_EditDirty = false;
	std::string m_Draft;

	char m_aStatus[256] = "";
	bool m_StatusIsError = false;

	// 换文档、开始编辑或还原后，编辑区要回到顶部。
	bool m_ResetEditScroll = true;

	// 换文档：记录 id 并首次解析内容，同时丢掉上一份草稿。
	void Load(const char *pId, const SWikiDocument &Fallback, bool IsCard);
	// 用已记录的 id 重新解析（保存、还原之后调用）。
	void Reload();
	// 进入编辑：没有未保存草稿才把当前内容变成草稿。
	// MaxDraftBytes 是编辑框缓冲的实际容量：放不下的文档返回 false 并给出错误状态，
	// 不能让人在客户端里改一份被截断的副本——那样保存会直接覆盖掉后半篇正文。
	bool BeginEditing(size_t MaxDraftBytes);
	// 退出编辑：草稿保留，再次进入编辑可以接着改。
	void LeaveEditing();
	// 编辑框内容变化时调用。
	void SetDraft(const std::string &Text);
	// 把草稿写入用户目录并重新载入；返回是否成功，失败原因进 m_aStatus。
	bool Save();
	// 删除用户覆盖并重新载入内置内容。
	bool ResetToBuiltIn();
	void SetStatus(const char *pText, bool Error);
};

#endif
