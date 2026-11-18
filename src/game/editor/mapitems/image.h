#ifndef GAME_EDITOR_MAPITEMS_IMAGE_H
#define GAME_EDITOR_MAPITEMS_IMAGE_H

#include <engine/graphics.h>

#include <game/editor/auto_map.h>
#include <game/editor/map_object.h>

class CEditorImage : public CImageInfo, public CMapObject // NOLINT(misc-multiple-inheritance): 编辑器图像同时持有图像数据并接入地图对象生命周期。
{
public:
	explicit CEditorImage(CEditorMap *pMap);
	~CEditorImage() override;
	void OnAttach(CEditorMap *pMap) override;

	void AnalyseTileFlags();
	void Free(); // NOLINT(bugprone-derived-method-shadowing-base-method): 包装 CImageInfo::Free，并同步释放编辑器纹理资源。

	IGraphics::CTextureHandle m_Texture;
	int m_External = 0;
	char m_aName[IO_MAX_PATH_LENGTH] = "";
	unsigned char m_aTileFlags[256] = {};

	CAutomapper m_Automapper;
};

#endif
