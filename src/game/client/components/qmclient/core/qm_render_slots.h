/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_RENDER_SLOTS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_RENDER_SLOTS_H

// QmClient 使用少量有明确语义的绘制槽位，避免 feature 依赖 runtime 在组件列表中的隐含位置。
enum class EQmRenderSlot
{
	// 地图背景或实体绘制之前的 Qm 背景层。
	WORLD_BACKGROUND,
	// 实体绘制之前的叠加层。
	ENTITY_UNDERLAY,
	// 实体绘制之后、HUD 之前的叠加层。
	ENTITY_OVERLAY,
	// HUD、状态提示等游戏内界面层。
	HUD_OVERLAY,
	// 菜单和外部 UI 层。
	MENU_OVERLAY,
};

#endif
