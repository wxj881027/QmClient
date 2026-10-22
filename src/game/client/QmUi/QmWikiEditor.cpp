// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiEditor.h"

#include <base/system.h>

#include <game/localization.h>

void SWikiEditorState::Load(const char *pId, const SWikiDocument &Fallback, const bool IsCard)
{
	m_Fallback = Fallback;
	m_IsCard = IsCard;
	str_copy(m_aId, pId != nullptr ? pId : "", sizeof(m_aId));
	// 换文档时丢掉上一份草稿，否则 A 卡的未保存文字会出现在 B 卡的编辑框里。
	m_Editing = false;
	m_EditDirty = false;
	m_Draft.clear();
	m_ResetEditScroll = true;
	SetStatus("", false);
	Reload();
}

void SWikiEditorState::Reload()
{
	m_Content = m_IsCard ? qm_wiki_store::ResolveCard(m_aId, m_Fallback) : qm_wiki_store::ResolveDocument(m_aId, m_Fallback);
	m_Markdown.Parse(m_Content.m_Markdown.c_str());
	m_vTocBlocks.clear();
	const std::vector<SWikiBlock> &vBlocks = m_Markdown.Blocks();
	for(size_t Index = 0; Index < vBlocks.size(); ++Index)
	{
		if(vBlocks[Index].m_Type == EWikiBlockType::HEADING && vBlocks[Index].m_Level <= 3)
			m_vTocBlocks.push_back(static_cast<int>(Index));
	}
	if(m_vTocBlocks.size() < 2)
		m_vTocBlocks.clear();
}

void SWikiEditorState::BeginEditing()
{
	m_Editing = true;
	m_ResetEditScroll = true;
	m_aStatus[0] = '\0';
	m_StatusIsError = false;
	// 上次退出编辑时有未保存草稿就接着改，否则从当前内容开始。
	if(!m_EditDirty)
		m_Draft = m_Content.m_Markdown;
}

void SWikiEditorState::LeaveEditing()
{
	m_Editing = false;
}

void SWikiEditorState::SetDraft(const std::string &Text)
{
	m_Draft = Text;
	m_EditDirty = true;
}

bool SWikiEditorState::Save()
{
	std::string Error;
	if(!qm_wiki_store::SaveOverride(m_Content.m_RelativePath.c_str(), m_Draft, &Error))
	{
		SetStatus(Error.c_str(), true);
		return false;
	}
	Reload();
	// 磁盘内容已是刚保存的文本：草稿同步回来并清掉未保存标记。
	m_EditDirty = false;
	m_Draft = m_Content.m_Markdown;
	m_ResetEditScroll = false;
	char aStatus[256];
	str_format(aStatus, sizeof(aStatus), "%s: wiki/%s/%s.md", Localize("Saved"), qm_wiki_store::LanguageDir(), m_Content.m_RelativePath.c_str());
	SetStatus(aStatus, false);
	return true;
}

bool SWikiEditorState::ResetToBuiltIn()
{
	const bool Removed = qm_wiki_store::DeleteOverride(m_Content.m_RelativePath.c_str());
	Reload();
	m_EditDirty = false;
	m_Draft = m_Content.m_Markdown;
	m_ResetEditScroll = true;
	// 重载后的覆盖状态才是真实结果：没有覆盖时 DeleteOverride 也返回 false，不能算失败。
	const bool Restored = Removed || !m_Content.m_HasUserOverride;
	SetStatus(Restored ? Localize("Restored the built-in content") : Localize("Cannot remove the wiki file"), !Restored);
	return Restored;
}

void SWikiEditorState::SetStatus(const char *pText, const bool Error)
{
	str_copy(m_aStatus, pText != nullptr ? pText : "", sizeof(m_aStatus));
	m_StatusIsError = Error;
}
