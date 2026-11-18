#ifndef GAME_CLIENT_QMUI_MAPTAGSPANEL_H
#define GAME_CLIENT_QMUI_MAPTAGSPANEL_H

#include "QmPieOptionPool.h"

#include <game/client/components/qmclient/local_map_tags.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <array>
#include <map>
#include <string>
#include <vector>

class CGameClient;

class CQmMapTagsPanel
{
	// 输入框上限 10 字（服务器同样按 10 字校验），但输入框只显示约 5 字宽。
	CLineInputBuffered<41, 10> m_Input;
	CButtonContainer m_UploadButton;
	CButtonContainer m_RefreshButton;
	// 「当前标签」行末的 + 与输入框旁的「添加」是两个动作，用各自的容器。
	CButtonContainer m_AddTagButton;
	CButtonContainer m_ComposerAddButton;
	std::array<CButtonContainer, 4> m_aPresetButtons;
	std::map<std::string, CButtonContainer> m_TagButtons;
	// 本地标签本体（点击 = 展开/收起它自己的 ×，按住拖动 = 改顺序）。
	std::map<std::string, CButtonContainer> m_LocalTagChipButtons;
	// 每个本地标签左侧的删除按钮，按标签文本取容器（同一帧内稳定）。
	std::map<std::string, CButtonContainer> m_LocalTagRemoveButtons;
	// 本地标签的编辑态：**点击某颗标签才展开它左侧的 ×**，× 的宽度与透明度随
	// 进度从 0 生长出来；再点这颗标签、点 ×、或点别处都会收起。同一时刻只展开一颗。
	std::string m_ExpandedTag;
	// 正在绘制 × 的那颗：收起动画播完才清空，否则 × 会在半途突然消失。
	std::string m_VisibleTag;
	float m_ExpandProgress = 0.0f;
	double m_ExpandAnimationTime = 0.0;
	// 拖动排序直接复用饼菜单选项池的拖动骨架（起手阈值、幽灵矩形、抓取偏移）：
	// 那套交互已经在选项池里验证过，标签池只是把「换池」换成「池内插入」。
	SQmPieOptionPoolDrag m_LocalTagDrag;
	// 用户自己的标签：只存本地、不上传；点右上角上传按钮才提交给服务器审核。
	QmMapTags::CLocalTags m_LocalTags;
	// 提交是单通道（同一时刻只允许一个写请求），多个标签按队列逐个提交。
	std::vector<std::string> m_vUploadQueue;
	// 点 + 之后才展开输入框，避免常驻一行空输入框占高度。
	bool m_bComposing = false;
	std::string m_Context;

public:
	void Render(CGameClient *pGameClient, CUIRect View);
};

#endif
