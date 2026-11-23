#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_MECHANISM_BUILDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_MECHANISM_BUILDER_H

#include <game/client/prediction/gameworld.h>

#include <vector>

class CCollision;

namespace qm_map_mechanisms
{
	struct SSnapshotLaser
	{
		vec2 m_Hub;
		vec2 m_From;
		int m_StartTick;
		int m_Type;
		int m_SwitchNumber;
		bool m_Used = false;
	};

	// 从地图图层重建门、拖拉、静态激光等机关实体。
	//
	// 快速练习需要这些实体进入可玩分支世界，并注册门碰撞；影子回放只需要
	// 一份只读几何，绝不能写入共享 CCollision。两处使用同一份地图扫描规则，
	// 避免“练习里会开、回放里却画错”的分叉。
	struct SBuildOptions
	{
		bool m_BuildPickups = true;
		// true：门几何写入共享 CCollision；false：只准备渲染几何。
		bool m_RegisterDoorCollision = true;
		// 练习分支可继承快照里同位置的激光相位；影子世界没有源世界，默认关闭。
		bool m_InheritSnapshotLights = true;
		std::vector<SSnapshotLaser> *m_pSnapshots = nullptr;
	};

	struct SBuildCounts
	{
		int m_Pickups = 0;
		int m_Doors = 0;
		int m_Draggers = 0;
		int m_Lights = 0;
	};

	// NextEntityId 由调用方独占；返回实际构建数量，并推进 NextEntityId。
	SBuildCounts Build(CGameWorld &World, CCollision *pCollision, int &NextEntityId, const SBuildOptions &Options = {});

	// 影子世界没有逐 tick 的 CGameWorld::Tick，静态激光与拖拉需要按回放播放头
	// 单独推进相位和移动平台位置。支持后退拖动：内部会从时间线零点重算。
	void AdvanceMechanisms(CGameWorld &World, int PhaseTick, int RenderTick);
}

#endif
