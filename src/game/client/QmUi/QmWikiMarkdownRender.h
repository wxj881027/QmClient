// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKIMARKDOWNRENDER_H
#define GAME_CLIENT_QMUI_QMWIKIMARKDOWNRENDER_H

#include <game/client/ui_rect.h>

#include <functional>
#include <vector>

struct IUiContext;
struct SWikiBlock;
class CScrollRegion;

struct SWikiRenderStyle
{
	float m_BodySize = 14.0f;
	float m_UiScale = 1.0f;
	// 颜色全部由调用方按当前主题填充，这里不写死任何色值。
	ColorRGBA m_Text;
	ColorRGBA m_Muted;
	ColorRGBA m_Accent;
	ColorRGBA m_Surface;
	ColorRGBA m_CodeSurface;
	ColorRGBA m_Line;
};

// 链接交互：点击回调 + 按钮 ID 存储。
// 用固定数组而不是 vector，保证同一文档内 ID 地址跨帧稳定；槽位按「目标 + 文字」的
// 哈希选取，因此内容滚动不会把按下状态串到别的链接上。
struct SWikiLinkContext
{
	const std::function<void(const char *)> *m_pOnClick = nullptr;
	int m_aIds[512] = {};
};

namespace qm_wiki_markdown
{
	// 从 Body 顶部开始排版并绘制全部块；Body 会被推进到内容末尾。
	// pScroll 为空时直接绘制，否则只有 AddRect 通过的行才绘制。
	// pLinks 非空时记录可点击区域；ScrollToBlock 对应的块会触发滚动定位（用于目录跳转）。
	float RenderBlocks(const IUiContext &Ctx, const std::vector<SWikiBlock> &vBlocks, CUIRect &Body, const SWikiRenderStyle &Style, CScrollRegion *pScroll, SWikiLinkContext *pLinks = nullptr, int ScrollToBlock = -1);
} // namespace qm_wiki_markdown

#endif
