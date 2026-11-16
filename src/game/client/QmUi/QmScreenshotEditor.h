#ifndef GAME_CLIENT_QMUI_QMSCREENSHOTEDITOR_H
#define GAME_CLIENT_QMUI_QMSCREENSHOTEDITOR_H

#include <game/client/components/qmclient/screenshot_manager.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

class CMenus;

// 编辑会话归菜单所有；工作线程与系统文件选择器只发布独立结果。
class CQmScreenshotEditor
{
	struct SLogoDialog;
	bool m_Open = false;
	bool m_Dirty = false;
	bool m_Failed = false;
	int m_ActiveLayer = 0;
	int m_SourceType = 0;
	std::string m_SourcePath;
	std::string m_TargetPath;
	CQmScreenshotManager::SWatermarkOptions m_Options;
	char m_aText[512]{};
	CLineInput m_TextInput{m_aText, sizeof(m_aText)};
	CButtonContainer m_aButtons[20];
	CScrollRegion m_Scroll;
	IGraphics::CTextureHandle m_Texture;
	int m_Width = 0, m_Height = 0;
	std::array<SQmWatermarkBounds, 2> m_aBounds{};
	std::shared_ptr<CQmScreenshotWatermarkJob> m_pPreview;
	std::shared_ptr<CQmScreenshotWatermarkJob> m_pExport;
	std::shared_ptr<SLogoDialog> m_pDialog;
	int64_t m_LastRequest = 0;
	vec2 m_DragStart{}, m_DragPosition{}, m_DragSpace{};
	void ChooseLogo();
	void Pump(CMenus *pMenus);
	void RenderImage(CMenus *pMenus, CUIRect Rect);

public:
	bool IsOpen() const { return m_Open; }
	void Open(CMenus *pMenus, const char *pPath, int StorageType);
	void Close(CMenus *pMenus);
	void Render(CMenus *pMenus, CUIRect Rect);
};

#endif
