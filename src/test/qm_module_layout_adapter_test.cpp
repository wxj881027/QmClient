#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/QmModuleLayoutAdapter.h>

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace qm_module;

namespace
{
	struct SConfigBackup
	{
		char m_aGlobalCardOrder[sizeof(g_Config.m_QmGlobalCardOrder)];
		char m_aSidebarCardOrder[sizeof(g_Config.m_QmSidebarCardOrder)];
		char m_aSettingsCardOrder[sizeof(g_Config.m_QmSettingsCardOrder)];
		int m_CardOrderMigrated;

		SConfigBackup()
		{
			str_copy(m_aGlobalCardOrder, g_Config.m_QmGlobalCardOrder, sizeof(m_aGlobalCardOrder));
			str_copy(m_aSidebarCardOrder, g_Config.m_QmSidebarCardOrder, sizeof(m_aSidebarCardOrder));
			str_copy(m_aSettingsCardOrder, g_Config.m_QmSettingsCardOrder, sizeof(m_aSettingsCardOrder));
			m_CardOrderMigrated = g_Config.m_QmCardOrderMigrated;
		}

		~SConfigBackup()
		{
			str_copy(g_Config.m_QmGlobalCardOrder, m_aGlobalCardOrder, sizeof(g_Config.m_QmGlobalCardOrder));
			str_copy(g_Config.m_QmSidebarCardOrder, m_aSidebarCardOrder, sizeof(g_Config.m_QmSidebarCardOrder));
			str_copy(g_Config.m_QmSettingsCardOrder, m_aSettingsCardOrder, sizeof(g_Config.m_QmSettingsCardOrder));
			g_Config.m_QmCardOrderMigrated = m_CardOrderMigrated;
		}
	};
} // namespace

// 测试用小规模 defaults（3 卡：Full/Left/Right 各一），复用真实 EQmModuleId 占位。
// ParseLegacyQmLayout 按 m_pKey 匹配，与 m_Id 数值无关。
static std::vector<SQmModuleEntry> MakeTestDefaults()
{
	return {
		{EQmModuleId::Info, EQmModuleColumn::Full, 0, "info"},
		{EQmModuleId::ChatBubble, EQmModuleColumn::Left, 0, "chat_bubble"},
		{EQmModuleId::CameraView, EQmModuleColumn::Right, 0, "camera_view"},
	};
}

// 真实栖梦 s_aQmModuleDefaults 全部卡片（key/column/order 与 menus_qmclient.cpp 一致）。
// 用于全量往返自洽测试——这是"行为等价"承诺的可信凭证。
static std::vector<SQmModuleEntry> MakeAllDefaults()
{
	return {
		{EQmModuleId::Info, EQmModuleColumn::Full, 0, "info"},
		{EQmModuleId::ChatBubble, EQmModuleColumn::Left, 0, "chat_bubble"},
		{EQmModuleId::SkinTransition, EQmModuleColumn::Left, 1, "skin_transition"},
		{EQmModuleId::FocusMode, EQmModuleColumn::Left, 2, "focus_mode"},
		{EQmModuleId::GoresActor, EQmModuleColumn::Left, 3, "gores_actor"},
		{EQmModuleId::Gores, EQmModuleColumn::Left, 4, "gores"},
		{EQmModuleId::KeyBinds, EQmModuleColumn::Left, 5, "key_binds"},
		{EQmModuleId::MiniFeatures, EQmModuleColumn::Left, 6, "mini_features"},
		{EQmModuleId::JumpHint, EQmModuleColumn::Left, 7, "jump_hint"},
		{EQmModuleId::WeaponTrajectory, EQmModuleColumn::Left, 8, "weapon_trajectory"},
		{EQmModuleId::Coords, EQmModuleColumn::Left, 9, "coords"},
		{EQmModuleId::Streamer, EQmModuleColumn::Left, 10, "streamer"},
		{EQmModuleId::FriendNotify, EQmModuleColumn::Left, 11, "friend_notify"},
		{EQmModuleId::BlockWords, EQmModuleColumn::Left, 12, "block_words"},
		{EQmModuleId::Translate, EQmModuleColumn::Left, 14, "translate"},
		{EQmModuleId::TranslateUi, EQmModuleColumn::Left, 15, "translate_ui"},
		{EQmModuleId::QiaFen, EQmModuleColumn::Left, 13, "qiafen"},
		{EQmModuleId::PieMenu, EQmModuleColumn::Left, 16, "pie_menu"},
		{EQmModuleId::CameraView, EQmModuleColumn::Right, 0, "camera_view"},
		{EQmModuleId::WeaponAnimation, EQmModuleColumn::Right, 1, "weapon_animation"},
		{EQmModuleId::EntityOverlay, EQmModuleColumn::Right, 2, "entity_overlay"},
		{EQmModuleId::Laser, EQmModuleColumn::Right, 3, "laser"},
		{EQmModuleId::PlayerStats, EQmModuleColumn::Right, 4, "player_stats"},
		{EQmModuleId::CollisionHitbox, EQmModuleColumn::Right, 5, "collision_hitbox"},
		{EQmModuleId::FavoriteMaps, EQmModuleColumn::Right, 6, "favorite_maps"},
		{EQmModuleId::HJAssist, EQmModuleColumn::Right, 7, "hj_assist"},
		{EQmModuleId::SpeedrunTimer, EQmModuleColumn::Right, 8, "speedrun_timer"},
		{EQmModuleId::DebugGraph, EQmModuleColumn::Right, 9, "debug_graph"},
		{EQmModuleId::InputOverlay, EQmModuleColumn::Right, 10, "input_overlay"},
		{EQmModuleId::HudNotifications, EQmModuleColumn::Right, 11, "hud_notifications"},
		{EQmModuleId::Voice, EQmModuleColumn::Right, 12, "voice"},
		{EQmModuleId::DummyMiniView, EQmModuleColumn::Right, 13, "dummy_miniview"},
		{EQmModuleId::DynamicIsland, EQmModuleColumn::Right, 14, "dynamic_island"},
		{EQmModuleId::SystemMediaControls, EQmModuleColumn::Right, 15, "system_media_controls"},
		{EQmModuleId::Lyrics, EQmModuleColumn::Right, 16, "lyrics"},
		{EQmModuleId::Background3D, EQmModuleColumn::Right, 17, "background_3d"},
		{EQmModuleId::DebugMode, EQmModuleColumn::Right, 19, "debug_mode"},
		{EQmModuleId::BindStatusHud, EQmModuleColumn::Right, 20, "bind_status_hud"},
	};
}

TEST(QmModuleLayoutAdapter, LegacyMigrationWritesIntoProvidedGlobalModel)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	ASSERT_TRUE(LoadLegacyQmLayoutIntoModel(Model, "chat_bubble:right:0"));
	const int Index = Model.FindByStableId("qm:chat_bubble");
	ASSERT_GE(Index, 0);
	EXPECT_EQ(Model.Entry(Index).m_Column, 2);
	EXPECT_EQ(Model.Entry(Index).m_OrderInColumn, 0);
}

TEST(QmModuleLayoutAdapter, ExplicitModelMoveAndSerializeUsesProvidedModel)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	Model.ClearDirty();

	ASSERT_TRUE(MoveQmModuleInModel(Model, EQmModuleId::ChatBubble, EQmModuleColumn::Right, 0));
	EXPECT_TRUE(Model.IsDirty());
	char aLayout[4096];
	ASSERT_TRUE(SerializeLegacyQmLayoutFromModel(Model, aLayout, sizeof(aLayout)));
	EXPECT_NE(std::string(aLayout).find("chat_bubble:right:0"), std::string::npos);
	EXPECT_FALSE(MoveQmModuleInModel(Model, EQmModuleId::ChatBubble, EQmModuleColumn::Full, 0));
	EXPECT_FALSE(MoveQmModuleInModel(Model, EQmModuleId::Info, EQmModuleColumn::Left, 0));
	EXPECT_FALSE(MoveQmModuleToTabInModel(Model, EQmModuleId::Info, "search", EQmModuleColumn::Left, 0));
}

TEST(QmModuleLayoutAdapter, ColumnIntRoundtrip)
{
	EXPECT_EQ(QmModuleColumnToInt(EQmModuleColumn::Full), 0);
	EXPECT_EQ(QmModuleColumnToInt(EQmModuleColumn::Left), 1);
	EXPECT_EQ(QmModuleColumnToInt(EQmModuleColumn::Right), 2);
	EXPECT_EQ(QmModuleColumnFromInt(0), EQmModuleColumn::Full);
	EXPECT_EQ(QmModuleColumnFromInt(1), EQmModuleColumn::Left);
	EXPECT_EQ(QmModuleColumnFromInt(2), EQmModuleColumn::Right);
}

TEST(QmModuleLayoutAdapter, ColumnStringRoundtrip)
{
	EXPECT_STREQ(QmModuleColumnToString(EQmModuleColumn::Full), "full");
	EXPECT_STREQ(QmModuleColumnToString(EQmModuleColumn::Left), "left");
	EXPECT_STREQ(QmModuleColumnToString(EQmModuleColumn::Right), "right");
	EQmModuleColumn Col;
	ASSERT_TRUE(ParseQmModuleColumnString("left", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Left);
	ASSERT_TRUE(ParseQmModuleColumnString("right", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Right);
	ASSERT_TRUE(ParseQmModuleColumnString("full", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Full);
	EXPECT_FALSE(ParseQmModuleColumnString("invalid", &Col));
	EXPECT_FALSE(ParseQmModuleColumnString(nullptr, &Col));
}

// 意图：折叠配置必须继续接受历史 key[:...] 条目，写回时只保留注册 key，
// 不能退化成依赖分号包围的子串匹配，避免 key 前后缀或附加字段破坏状态。
TEST(QmModuleLayoutAdapter, LegacyCollapsedConfigAcceptsSuffixAndNormalizes)
{
	const auto Defaults = MakeTestDefaults();
	std::array<bool, QmModuleCount> aCollapsed = {};
	EXPECT_TRUE(ParseLegacyQmCollapsed("chat_bubble:legacy;unknown;camera_view", Defaults, aCollapsed));
	EXPECT_FALSE(aCollapsed[static_cast<size_t>(EQmModuleId::Info)]);
	EXPECT_TRUE(aCollapsed[static_cast<size_t>(EQmModuleId::ChatBubble)]);
	EXPECT_TRUE(aCollapsed[static_cast<size_t>(EQmModuleId::CameraView)]);

	char aSerialized[128];
	SerializeLegacyQmCollapsed(Defaults, aCollapsed, aSerialized, sizeof(aSerialized));
	EXPECT_STREQ(aSerialized, "chat_bubble;camera_view");
}

// 意图：旧 defaults 的排列不是 EQmModuleId 顺序，折叠状态仍必须按模块状态数组读写。
TEST(QmModuleLayoutAdapter, LegacyCollapsedConfigUsesModuleStateIndices)
{
	const std::array<SQmModuleEntry, 2> aDefaults = {
		SQmModuleEntry{EQmModuleId::SkinTransition, EQmModuleColumn::Left, 0, "skin_transition"},
		SQmModuleEntry{EQmModuleId::GoresActor, EQmModuleColumn::Right, 0, "gores_actor"},
	};
	std::array<bool, QmModuleCount> aCollapsed = {};
	ASSERT_TRUE(ParseLegacyQmCollapsed("skin_transition", aDefaults, aCollapsed));
	EXPECT_TRUE(aCollapsed[static_cast<size_t>(EQmModuleId::SkinTransition)]);
	EXPECT_FALSE(aCollapsed[static_cast<size_t>(EQmModuleId::GoresActor)]);

	char aSerialized[128];
	SerializeLegacyQmCollapsed(aDefaults, aCollapsed, aSerialized, sizeof(aSerialized));
	EXPECT_STREQ(aSerialized, "skin_transition");
}

// 意图：ParseQmModuleColumnString 必须与栖梦 ParseQmModuleColumn 等价——
// 接受大小写不敏感（LEFT/Right/FULL）+ 整数形式 0/1/2（历史/手改 config 可能含这些）。
TEST(QmModuleLayoutAdapter, ParseColumnAcceptsNocaseAndInteger)
{
	EQmModuleColumn Col;
	ASSERT_TRUE(ParseQmModuleColumnString("LEFT", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Left);
	ASSERT_TRUE(ParseQmModuleColumnString("Right", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Right);
	ASSERT_TRUE(ParseQmModuleColumnString("FULL", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Full);
	ASSERT_TRUE(ParseQmModuleColumnString("0", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Full);
	ASSERT_TRUE(ParseQmModuleColumnString("1", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Left);
	ASSERT_TRUE(ParseQmModuleColumnString("2", &Col));
	EXPECT_EQ(Col, EQmModuleColumn::Right);
	EXPECT_FALSE(ParseQmModuleColumnString("3", &Col)); // 越界整数
}

// 意图：stableId 映射是迁移兜底与 CModel 接入的单一事实源。
// QiaFen 以持久化 key qiafen 为权威（非 UI 名 keyword_reply），否则迁移丢用户布局。
TEST(QmModuleLayoutAdapter, StableIdMappingUsesPersistentKey)
{
	EXPECT_STREQ(QmModuleStableId(EQmModuleId::ChatBubble), "qm:chat_bubble");
	EXPECT_STREQ(QmModuleStableId(EQmModuleId::QiaFen), "qm:qiafen");
	EXPECT_STREQ(QmModuleStableId(EQmModuleId::Info), "qm:info");
}

TEST(QmModuleLayoutAdapter, StableIdReverseLookup)
{
	EQmModuleId Id;
	ASSERT_TRUE(QmModuleIdFromStableId("qm:chat_bubble", &Id));
	EXPECT_EQ(Id, EQmModuleId::ChatBubble);
	ASSERT_TRUE(QmModuleIdFromStableId("qm:qiafen", &Id));
	EXPECT_EQ(Id, EQmModuleId::QiaFen);
	EXPECT_FALSE(QmModuleIdFromStableId("qm:unknown", &Id));
	EXPECT_FALSE(QmModuleIdFromStableId("qm:nameplate_text", &Id)); // 栖梦枚举无它（已移除）
	EXPECT_FALSE(QmModuleIdFromStableId(nullptr, &Id));
}

// 意图：37 枚举的 stableId 必须唯一且双向可反查（迁移兜底全覆盖）。
TEST(QmModuleLayoutAdapter, AllModulesHaveUniqueReversibleStableId)
{
	std::set<std::string> Ids;
	for(size_t i = 0; i < QmModuleCount; ++i)
	{
		EQmModuleId Id = static_cast<EQmModuleId>(i);
		const char *pStable = QmModuleStableId(Id);
		ASSERT_NE(pStable, nullptr);
		EXPECT_TRUE(Ids.insert(pStable).second) << "重复 stableId: " << pStable;
		EQmModuleId Back;
		EXPECT_TRUE(QmModuleIdFromStableId(pStable, &Back));
		EXPECT_EQ(Back, Id);
	}
	EXPECT_EQ(Ids.size(), QmModuleCount);
}

// 意图：Parse/Serialize 往返保持用户布局，缺失卡用 defaults 兜底（栖梦已验证的"全量+补全"模型）。
TEST(QmModuleLayoutAdapter, ParseSerializeRoundtripKeepsDefaults)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_TRUE(ParseLegacyQmLayout("chat_bubble:left:0;camera_view:right:0", Defaults, Out));
	char aBuf[256];
	SerializeLegacyQmLayout(Out, aBuf, sizeof(aBuf));
	const std::string Result(aBuf);
	EXPECT_NE(Result.find("chat_bubble:left:0"), std::string::npos);
	EXPECT_NE(Result.find("camera_view:right:0"), std::string::npos);
	EXPECT_NE(Result.find("info:full:0"), std::string::npos); // 缺失卡 defaults 兜底
}

// 意图：Full 列保护——info（默认 Full）不可改列；非 Full 卡不可拖成 Full（回退默认列）。
TEST(QmModuleLayoutAdapter, FullColumnProtection)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_TRUE(ParseLegacyQmLayout("info:left:0;chat_bubble:full:0", Defaults, Out));
	ASSERT_EQ(Out.size(), 3u);
	EXPECT_EQ(Out[0].m_Column, EQmModuleColumn::Full); // info 强制 Full
	EXPECT_EQ(Out[1].m_Column, EQmModuleColumn::Left); // chat_bubble 解析成 full 回退 Left
}

// 意图：容错——未知 key 跳过；重复 key 取首次；非法 column/order 字段跳过整条（回退默认）。
TEST(QmModuleLayoutAdapter, ParseToleratesBadKeys)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_TRUE(ParseLegacyQmLayout("unknown:left:0;chat_bubble:left:0;chat_bubble:right:1;camera_view:bad:0", Defaults, Out));
	ASSERT_EQ(Out.size(), 3u);
	EXPECT_EQ(Out[1].m_Column, EQmModuleColumn::Left); // chat_bubble 取首次 left:0
	EXPECT_EQ(Out[1].m_OrderInColumn, 0);
	EXPECT_EQ(Out[2].m_Column, EQmModuleColumn::Right); // camera_view 非法 column → 默认 right
}

// 意图：空 config 返回 raw defaults（未经 SmartDefaults 均衡），且 Parsed=false。
// SmartDefaults 均衡由调用方（SyncQmModuleLayout）负责，适配器不掺和渲染高度耦合的均衡算法。
TEST(QmModuleLayoutAdapter, EmptyConfigReturnsRawDefaultsNotParsed)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_FALSE(ParseLegacyQmLayout("", Defaults, Out)); // 空 config 不算解析到
	ASSERT_EQ(Out.size(), 3u);
	EXPECT_EQ(Out[0].m_Column, EQmModuleColumn::Full); // raw defaults
	EXPECT_EQ(Out[1].m_Column, EQmModuleColumn::Left);
	EXPECT_EQ(Out[2].m_Column, EQmModuleColumn::Right);
}

// 意图：全未知 key（损坏 config / 迁移失败）Parsed=false，调用方据此走 SmartDefaults 回退。
TEST(QmModuleLayoutAdapter, ZeroParsedOnAllUnknownReturnsFalse)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_FALSE(ParseLegacyQmLayout("unknown1:left:0;unknown2:right:1", Defaults, Out));
	ASSERT_EQ(Out.size(), Defaults.size()); // 回退 raw defaults
}

// 意图：order 空洞由 Normalize 连续化（消除拖拽/迁移后的间距）。
TEST(QmModuleLayoutAdapter, NormalizeFillsOrderGaps)
{
	auto Defaults = MakeTestDefaults();
	std::vector<SQmModuleEntry> Out;
	EXPECT_TRUE(ParseLegacyQmLayout("chat_bubble:left:5", Defaults, Out));
	EXPECT_EQ(Out[1].m_OrderInColumn, 0); // Left 列仅 chat_bubble，Normalize 后 0
}

// 意图：全部卡片 serialize→parse 等价（行为等价回归基线的核心凭证）。
TEST(QmModuleLayoutAdapter, AllLegacyKeysRoundtripPreservesColumns)
{
	auto Defaults = MakeAllDefaults();
	ASSERT_EQ(Defaults.size(), QmModuleCount);
	char aConfig[2048];
	SerializeLegacyQmLayout(Defaults, aConfig, sizeof(aConfig));
	std::vector<SQmModuleEntry> Out;
	EXPECT_TRUE(ParseLegacyQmLayout(aConfig, Defaults, Out));
	ASSERT_EQ(Out.size(), Defaults.size());
	for(size_t i = 0; i < Defaults.size(); ++i)
	{
		EXPECT_EQ(Out[i].m_Id, Defaults[i].m_Id) << "卡 " << Defaults[i].m_pKey;
		EXPECT_EQ(Out[i].m_Column, Defaults[i].m_Column) << "卡 " << Defaults[i].m_pKey << " 列变化";
	}
}

// === Step 2: CModel 接入 ===

// 意图：CModel 路径（Load+Serialize）必须等价 Step 1b 旧基线（Parse+Serialize），否则 CModel 接入破坏等价性。
TEST(QmModuleLayoutAdapter, LoadSerializeMatchesLegacyBaseline)
{
	auto Defaults = MakeAllDefaults();
	const char *pConfig = "chat_bubble:left:0;camera_view:right:0;coords:left:3";
	std::vector<SQmModuleEntry> Legacy;
	ParseLegacyQmLayout(pConfig, Defaults, Legacy);
	char aLegacy[2048];
	SerializeLegacyQmLayout(Legacy, aLegacy, sizeof(aLegacy));
	LoadQmLayoutIntoModel(pConfig, Defaults);
	char aModel[2048];
	SerializeQmLayoutFromModel(aModel, sizeof(aModel));
	EXPECT_STREQ(aModel, aLegacy);
}

// 意图：Move 改 CModel 布局，Serialize 反映新位置；Move 后 dirty（触发序列化）。
TEST(QmModuleLayoutAdapter, MoveUpdatesModelAndSetsDirty)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:left:0;camera_view:right:0", Defaults);
	EXPECT_FALSE(IsQmLayoutModelDirty()); // Load 后清 dirty
	EXPECT_TRUE(MoveQmModuleInModel(EQmModuleId::ChatBubble, EQmModuleColumn::Right, 0));
	EXPECT_TRUE(IsQmLayoutModelDirty()); // Move 置 dirty
	char aBuf[2048];
	SerializeQmLayoutFromModel(aBuf, sizeof(aBuf));
	const std::string Result(aBuf);
	EXPECT_NE(Result.find("chat_bubble:right:"), std::string::npos);
	EXPECT_EQ(Result.find("chat_bubble:left:"), std::string::npos);
}

TEST(QmModuleLayoutAdapter, MoveToTabUpdatesGlobalPlacementAndSetsDirty)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:left:0;camera_view:right:0", Defaults);
	EXPECT_FALSE(IsQmLayoutModelDirty());

	EXPECT_TRUE(MoveQmModuleToTabInModel(EQmModuleId::ChatBubble, "search", EQmModuleColumn::Right, 0));

	EXPECT_TRUE(IsQmLayoutModelDirty());
	char aBuf[4096];
	QmModuleLayoutModel().Serialize(aBuf, sizeof(aBuf));
	const std::string Result(aBuf);
	EXPECT_NE(Result.find("qm:chat_bubble|search|right|0;"), std::string::npos);
	EXPECT_EQ(Result.find("qm:chat_bubble|visual|left|"), std::string::npos);
}

// 意图：Full 保护——非 Full 卡不可拖成 Full（目标 Full 拒绝）。
TEST(QmModuleLayoutAdapter, MoveRejectsFullTarget)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:left:0", Defaults);
	EXPECT_FALSE(MoveQmModuleInModel(EQmModuleId::ChatBubble, EQmModuleColumn::Full, 0));
}

// 意图：SyncModelToLegacyLayout 把 CModel 转 SQmModuleEntry[]（全卡 + 列保持），供 Step 4 Refresh + Serialize 复用。
TEST(QmModuleLayoutAdapter, SyncModelToLegacyLayoutPreservesColumns)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:left:0;camera_view:right:0", Defaults);
	std::vector<SQmModuleEntry> vEntries = SyncModelToLegacyLayout();
	ASSERT_EQ(vEntries.size(), Defaults.size()); // 全部卡片
	bool FoundChatBubble = false;
	bool FoundCameraView = false;
	for(const SQmModuleEntry &E : vEntries)
	{
		std::string Key = E.m_pKey ? E.m_pKey : "";
		if(Key == "chat_bubble")
		{
			EXPECT_EQ(E.m_Column, EQmModuleColumn::Left);
			FoundChatBubble = true;
		}
		if(Key == "camera_view")
		{
			EXPECT_EQ(E.m_Column, EQmModuleColumn::Right);
			FoundCameraView = true;
		}
	}
	EXPECT_TRUE(FoundChatBubble);
	EXPECT_TRUE(FoundCameraView);
}

// 意图：全局排序成为权威时，Qm 只读取 qm:* 卡片，忽略其他域卡片，并补齐缺失 Qm defaults。
TEST(QmModuleLayoutAdapter, LoadGlobalOrderIntoModelKeepsOnlyQmCardsAndFillsDefaults)
{
	auto Defaults = MakeAllDefaults();
	EXPECT_TRUE(LoadQmLayoutModelFromGlobalOrder(
		"tc:auto_reply|tclient|1|0;qm:chat_bubble|visual|2|0;qm:camera_view|visual|1|0;",
		Defaults));

	std::vector<SQmModuleEntry> vEntries = SyncModelToLegacyLayout();
	ASSERT_EQ(vEntries.size(), Defaults.size());
	bool FoundChatBubble = false;
	bool FoundCameraView = false;
	bool FoundCoords = false;
	for(const SQmModuleEntry &E : vEntries)
	{
		const std::string Key = E.m_pKey != nullptr ? E.m_pKey : "";
		if(Key == "chat_bubble")
		{
			EXPECT_EQ(E.m_Column, EQmModuleColumn::Right);
			EXPECT_EQ(E.m_OrderInColumn, 0);
			FoundChatBubble = true;
		}
		else if(Key == "camera_view")
		{
			EXPECT_EQ(E.m_Column, EQmModuleColumn::Left);
			EXPECT_EQ(E.m_OrderInColumn, 0);
			FoundCameraView = true;
		}
		else if(Key == "coords")
		{
			EXPECT_EQ(E.m_Column, EQmModuleColumn::Left);
			FoundCoords = true;
		}
		EXPECT_FALSE(Key.starts_with("tc:"));
	}
	EXPECT_TRUE(FoundChatBubble);
	EXPECT_TRUE(FoundCameraView);
	EXPECT_TRUE(FoundCoords);
}

// 意图：从全局配置加载 Qm 子模型后，用户可变 tab placement 必须保留，
// 且 tab 指针不能引用解析临时模型的 owned string；后续序列化仍应稳定。
TEST(QmModuleLayoutAdapter, LoadGlobalOrderKeepsMovableTabsWithStableLifetime)
{
	auto Defaults = MakeAllDefaults();
	ASSERT_TRUE(LoadQmLayoutModelFromGlobalOrder("qm:chat_bubble|search|2|0;qm:camera_view|search|1|0;", Defaults));

	char aBuf[4096];
	QmModuleLayoutModel().Serialize(aBuf, sizeof(aBuf));
	const std::string Result(aBuf);
	EXPECT_NE(Result.find("qm:chat_bubble|search|right|"), std::string::npos);
	EXPECT_NE(Result.find("qm:camera_view|search|left|"), std::string::npos);
}

// 意图：Qm 子模型回写全局排序时，只更新 qm:* 条目，必须保留 tclient/deck 等非 Qm 卡片。
// 否则 Qm 页面一次拖拽会把全局卡片配置退化成 Qm 子集，破坏全局唯一权威。
TEST(QmModuleLayoutAdapter, SerializeMergedGlobalOrderPreservesNonQmCards)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:right:0;camera_view:left:0", Defaults);
	char aMerged[4096];
	SerializeMergedGlobalCardOrderFromQmModel(
		"tclient:visual-nameplates|tclient|1|0;qm:chat_bubble|visual|1|5;deck:graphics-display|graphics|2|0;",
		aMerged,
		sizeof(aMerged));

	const std::string Result(aMerged);
	EXPECT_NE(Result.find("tclient:visual-nameplates|tclient|1|0;"), std::string::npos);
	EXPECT_NE(Result.find("deck:graphics-display|graphics|2|0;"), std::string::npos);
	EXPECT_NE(Result.find("qm:chat_bubble|visual|right|"), std::string::npos);
	EXPECT_NE(Result.find("qm:camera_view|visual|left|"), std::string::npos);
	EXPECT_EQ(Result.find("qm:chat_bubble|visual|1|5;"), std::string::npos);
}

// 意图：全局合并应丢弃注册表外的旧 qm:* 残留。
// 删除/未知卡片残留继续写回会让全局配置越来越脏，并可能让搜索/组件编辑器看到幽灵卡。
TEST(QmModuleLayoutAdapter, SerializeMergedGlobalOrderDropsUnknownQmCards)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:right:0", Defaults);
	char aMerged[4096];
	SerializeMergedGlobalCardOrderFromQmModel(
		"qm:removed_card|visual|left|0;tclient:visual-nameplates|tclient|left|0;",
		aMerged,
		sizeof(aMerged));

	const std::string Result(aMerged);
	EXPECT_EQ(Result.find("qm:removed_card"), std::string::npos);
	EXPECT_NE(Result.find("tclient:visual-nameplates|tclient|left|0;"), std::string::npos);
	EXPECT_NE(Result.find("qm:chat_bubble|visual|right|"), std::string::npos);
}

// 意图：全局合并直接写固定长度 config buffer；容量不足必须显式失败，
// 不能静默写回截断配置，否则下一次启动会把丢尾部卡片当成用户布局。
TEST(QmModuleLayoutAdapter, SerializeMergedGlobalOrderReportsTruncation)
{
	auto Defaults = MakeAllDefaults();
	LoadQmLayoutIntoModel("chat_bubble:right:0;camera_view:left:0", Defaults);

	char aSmall[32];
	EXPECT_FALSE(SerializeMergedGlobalCardOrderFromQmModel(
		"tclient:visual-nameplates|tclient|left|0;",
		aSmall,
		sizeof(aSmall)));

	char aFull[4096];
	EXPECT_TRUE(SerializeMergedGlobalCardOrderFromQmModel(
		"tclient:visual-nameplates|tclient|left|0;",
		aFull,
		sizeof(aFull)));
	EXPECT_NE(std::string(aFull).find("tclient:visual-nameplates|tclient|left|0;"), std::string::npos);
	EXPECT_NE(std::string(aFull).find("qm:chat_bubble|visual|right|"), std::string::npos);
}

// 意图：首次迁移把旧 Qm 排序落到全局 stableId|tab|column|order 格式，并标记完成。
TEST(QmModuleLayoutAdapter, MigrateGlobalCardOrderWritesPipeFormatAndMarksMigrated)
{
	SConfigBackup Backup;
	auto Defaults = MakeAllDefaults();
	str_copy(g_Config.m_QmSidebarCardOrder, "chat_bubble:right:0;camera_view:left:0", sizeof(g_Config.m_QmSidebarCardOrder));
	g_Config.m_QmGlobalCardOrder[0] = '\0';
	g_Config.m_QmCardOrderMigrated = 0;

	EXPECT_TRUE(MigrateQmLayoutToGlobalCardOrder(Defaults));

	EXPECT_EQ(g_Config.m_QmCardOrderMigrated, 1);
	const std::string Result(g_Config.m_QmGlobalCardOrder);
	EXPECT_NE(Result.find("qm:chat_bubble|visual|right|"), std::string::npos);
	EXPECT_NE(Result.find("qm:camera_view|visual|left|"), std::string::npos);
	EXPECT_NE(Result.find("tclient:auto-reply|tclient|left|4;"), std::string::npos);
	EXPECT_NE(Result.find("deck:sound-audio-pack|sound|right|0;"), std::string::npos);
	EXPECT_NE(Result.find("|"), std::string::npos);
	EXPECT_EQ(Result.find("chat_bubble:right"), std::string::npos);
}

// 意图：首次迁移从全局 registry 默认值出发，再合并 Qm 子模型。
// registry 里存在 Qm 页面暂未反查到 EQmModuleId 的卡（如 qm:nameplate_text），这些也必须保留，
// 否则迁移会把全局卡片默认表退化成 Qm 子模型 37 卡，违背"注册表是唯一事实源"。
TEST(QmModuleLayoutAdapter, MigrateGlobalCardOrderPreservesRegistryOnlyQmCards)
{
	SConfigBackup Backup;
	auto Defaults = MakeAllDefaults();
	str_copy(g_Config.m_QmSidebarCardOrder, "chat_bubble:right:0", sizeof(g_Config.m_QmSidebarCardOrder));
	g_Config.m_QmGlobalCardOrder[0] = '\0';
	g_Config.m_QmCardOrderMigrated = 0;

	EXPECT_TRUE(MigrateQmLayoutToGlobalCardOrder(Defaults));

	const std::string Result(g_Config.m_QmGlobalCardOrder);
	EXPECT_NE(Result.find("qm:chat_bubble|visual|right|"), std::string::npos);
	EXPECT_NE(Result.find("qm:nameplate_text|hud|right|18;"), std::string::npos);
}

// 意图：首次迁移不能只把 Qm 旧排序合入全局配置；一旦 registry 默认 Tclient 卡片写入
// qm_global_card_order，Tclient 页面就会停止读取旧 qm_settings_card_order。必须在同一迁移内保留老用户顺序。
TEST(QmModuleLayoutAdapter, MigrateGlobalCardOrderPreservesLegacyTClientOrder)
{
	SConfigBackup Backup;
	auto Defaults = MakeAllDefaults();
	str_copy(g_Config.m_QmSidebarCardOrder, "chat_bubble:right:0", sizeof(g_Config.m_QmSidebarCardOrder));
	str_copy(g_Config.m_QmSettingsCardOrder, "tclient:input:1:0;tclient:visual-nameplates:0:0", sizeof(g_Config.m_QmSettingsCardOrder));
	g_Config.m_QmGlobalCardOrder[0] = '\0';
	g_Config.m_QmCardOrderMigrated = 0;

	EXPECT_TRUE(MigrateQmLayoutToGlobalCardOrder(Defaults));

	const std::string Result(g_Config.m_QmGlobalCardOrder);
	EXPECT_NE(Result.find("tclient:visual-nameplates|tclient|left|0;"), std::string::npos);
	EXPECT_NE(Result.find("tclient:input|tclient|right|0;"), std::string::npos);
	EXPECT_EQ(Result.find("tclient:input|tclient|left|3;"), std::string::npos);
}

// 意图：迁移只有完整写出全局配置后才能覆盖 config 和标记完成；
// 如果默认注册表未来超过固定 buffer，不能留下半截 qm_global_card_order。
TEST(QmModuleLayoutAdapter, MigrateGlobalCardOrderDoesNotMarkOrMutateOnSerializeFailure)
{
	SConfigBackup Backup;
	std::vector<SQmModuleEntry> Defaults;
	for(int i = 0; i < 256; ++i)
		Defaults.push_back({EQmModuleId::ChatBubble, EQmModuleColumn::Left, i, "chat_bubble"});
	str_copy(g_Config.m_QmSidebarCardOrder, "chat_bubble:right:0", sizeof(g_Config.m_QmSidebarCardOrder));
	g_Config.m_QmGlobalCardOrder[0] = '\0';
	g_Config.m_QmCardOrderMigrated = 0;

	EXPECT_FALSE(MigrateQmLayoutToGlobalCardOrder(Defaults));

	EXPECT_STREQ(g_Config.m_QmGlobalCardOrder, "");
	EXPECT_EQ(g_Config.m_QmCardOrderMigrated, 0);
}

// 意图：已有全局排序或已迁移时不覆盖用户当前配置。
TEST(QmModuleLayoutAdapter, MigrateGlobalCardOrderIsIdempotent)
{
	SConfigBackup Backup;
	auto Defaults = MakeAllDefaults();
	str_copy(g_Config.m_QmSidebarCardOrder, "chat_bubble:right:0", sizeof(g_Config.m_QmSidebarCardOrder));
	str_copy(g_Config.m_QmGlobalCardOrder, "qm:chat_bubble|search|1|0;", sizeof(g_Config.m_QmGlobalCardOrder));
	g_Config.m_QmCardOrderMigrated = 1;

	EXPECT_FALSE(MigrateQmLayoutToGlobalCardOrder(Defaults));

	EXPECT_EQ(g_Config.m_QmCardOrderMigrated, 1);
	EXPECT_STREQ(g_Config.m_QmGlobalCardOrder, "qm:chat_bubble|search|1|0;");
}
