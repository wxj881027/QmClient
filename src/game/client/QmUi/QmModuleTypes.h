#ifndef GAME_CLIENT_QMUI_QMMODULETYPES_H
#define GAME_CLIENT_QMUI_QMMODULETYPES_H

#include <cstddef>

// 栖梦侧栏模块的类型定义。
// 从 RenderSettingsQmClient 函数体提取到公共头，供 QmModuleLayoutAdapter 适配层与渲染层共享。
// 注：QmModuleIdName（id→UI 名，如 QiaFen→keyword_reply）与 s_aQmModuleDefaults.m_pKey（持久化 key，如 qiafen）
// 是数据/lambda，留 menus_qmclient.cpp；本头只含类型与计数。
namespace qm_module
{
	enum class EQmModuleId
	{
		Info,
		ChatBubble,
		GoresActor,
		Gores,
		FocusMode,
		KeyBinds,
		MiniFeatures,
		JumpHint,
		SkinTransition,
		CameraView,
		DummyMiniView,
		Coords,
		Streamer,
		FriendNotify,
		BlockWords,
		Translate,
		TranslateUi,
		QiaFen,
		PieMenu,
		EntityOverlay,
		Laser,
		PlayerStats,
		CollisionHitbox,
		FavoriteMaps,
		HJAssist,
		SpeedrunTimer,
		DebugGraph,
		InputOverlay,
		HudNotifications,
		Voice,
		DynamicIsland,
		SystemMediaControls,
		Lyrics,
		Background3D,
		WeaponTrajectory,
		WeaponAnimation,
		DebugMode,
		BindStatusHud,
	};

	enum class EQmModuleColumn
	{
		Full,
		Left,
		Right,
	};

	struct SQmModuleEntry
	{
		EQmModuleId m_Id;
		EQmModuleColumn m_Column;
		int m_OrderInColumn;
		const char *m_pKey;
	};

	constexpr size_t QmModuleCount = 38;
} // namespace qm_module

#endif // GAME_CLIENT_QMUI_QMMODULETYPES_H
