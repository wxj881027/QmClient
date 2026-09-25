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
		SoloSplit,
		FocusMode,
		KeyBinds,
		MiniFeatures,
		JumpHint,
		SkinTransition,
		SkinAppearance,
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
		// 本地差异：远程把 Emoticons / MapUpload 插在枚举中段（QmModuleTypes.h:50-51），
		// 本地改为**追加到末尾**——严格增量，不重编号任何既有 ID，避免顺序迁移与任何
		// 以 (int)Id 为下标的既有表被整体位移。持久化走 m_pKey 字符串，与枚举序无关。
		Emoticons,
		MapUpload,
		Steam,
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

	constexpr size_t QmModuleCount = 42;
} // namespace qm_module

#endif // GAME_CLIENT_QMUI_QMMODULETYPES_H
