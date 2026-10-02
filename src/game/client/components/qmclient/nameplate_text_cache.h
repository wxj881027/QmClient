#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_TEXT_CACHE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_TEXT_CACHE_H

#include <engine/textrender.h>

// 空文字也可能是有效布局结果，不能用容器不存在判断是否需要每帧重试。
class CQmNameplateTextCache
{
	bool m_Updated = false;
	uint64_t m_GlyphAtlasRevision = 0;

public:
	bool NeedsUpdate(bool Visible, bool Changed) const { return Visible && (Changed || !m_Updated); }
	bool ResourcesChanged(uint64_t GlyphAtlasRevision) const { return m_Updated && m_GlyphAtlasRevision != GlyphAtlasRevision; }
	void OnUpdate(uint64_t GlyphAtlasRevision)
	{
		m_Updated = true;
		m_GlyphAtlasRevision = GlyphAtlasRevision;
	}
	void Reset()
	{
		m_Updated = false;
		m_GlyphAtlasRevision = 0;
	}
};

// 坐标文字持续变化时复用缓冲；窗口重建使容器失效后重新创建。
template<typename TTextRender>
void QmUpdateNameplateTextContainer(TTextRender *pTextRender, STextContainerIndex &Index, CTextCursor *pCursor, const char *pText)
{
	if(Index.Valid())
		pTextRender->RecreateTextContainerSoft(Index, pCursor, pText);
	else
		pTextRender->CreateOrAppendTextContainer(Index, pCursor, pText);
}

#endif
