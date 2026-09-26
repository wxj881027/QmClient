#include <game/client/QmUi/QmCardOrderModel.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstring>
#include <limits>
#include <string>

TEST(QmCardOrderModel, MoveReordersWithinColumn)
{
	qm_card_order::CModel M;
	M.SetEntries({{"a", nullptr, 1, 0}, {"b", nullptr, 1, 1}, {"c", nullptr, 1, 2}});
	M.ClearDirty();
	M.Move("a", 1, 2); // a 移到末尾
	auto Col = M.ColumnIndices(1);
	ASSERT_EQ(Col.size(), 3u);
	EXPECT_STREQ(M.Entry(Col[0]).m_pStableId, "b");
	EXPECT_STREQ(M.Entry(Col[1]).m_pStableId, "c");
	EXPECT_STREQ(M.Entry(Col[2]).m_pStableId, "a");
	EXPECT_TRUE(M.IsDirty());
}

TEST(QmCardOrderModel, MoveAcrossColumnsNormalizesSourceColumn)
{
	qm_card_order::CModel M;
	M.SetEntries({{"a", nullptr, 1, 0}, {"b", nullptr, 1, 1}, {"c", nullptr, 1, 2}, {"d", nullptr, 2, 0}});
	M.ClearDirty();

	M.Move("b", 2, 1);

	auto Left = M.ColumnIndices(1);
	ASSERT_EQ(Left.size(), 2u);
	EXPECT_STREQ(M.Entry(Left[0]).m_pStableId, "a");
	EXPECT_EQ(M.Entry(Left[0]).m_OrderInColumn, 0);
	EXPECT_STREQ(M.Entry(Left[1]).m_pStableId, "c");
	EXPECT_EQ(M.Entry(Left[1]).m_OrderInColumn, 1);

	auto Right = M.ColumnIndices(2);
	ASSERT_EQ(Right.size(), 2u);
	EXPECT_STREQ(M.Entry(Right[0]).m_pStableId, "d");
	EXPECT_EQ(M.Entry(Right[0]).m_OrderInColumn, 0);
	EXPECT_STREQ(M.Entry(Right[1]).m_pStableId, "b");
	EXPECT_EQ(M.Entry(Right[1]).m_OrderInColumn, 1);
}

TEST(QmCardOrderModel, MoveToTabChangesPlacementTabAndReordersTargetColumn)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "visual", 1, 0}, {"qm:b", "hud", 1, 0}, {"qm:c", "hud", 1, 1}});
	M.ClearDirty();

	M.MoveToTab("qm:a", "hud", 1, 1);

	auto VisualLeft = M.ColumnIndices("visual", 1);
	EXPECT_TRUE(VisualLeft.empty());
	auto HudLeft = M.ColumnIndices("hud", 1);
	ASSERT_EQ(HudLeft.size(), 3u);
	EXPECT_STREQ(M.Entry(HudLeft[0]).m_pStableId, "qm:b");
	EXPECT_STREQ(M.Entry(HudLeft[1]).m_pStableId, "qm:a");
	EXPECT_STREQ(M.Entry(HudLeft[1]).m_pDefaultTab, "hud");
	EXPECT_STREQ(M.Entry(HudLeft[2]).m_pStableId, "qm:c");
	EXPECT_TRUE(M.IsDirty());
}

TEST(QmCardOrderModel, MoveToTabNormalizesSourceTabColumn)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "visual", 1, 0}, {"qm:b", "visual", 1, 1}, {"qm:c", "visual", 1, 2}, {"qm:d", "hud", 1, 0}});
	M.ClearDirty();

	M.MoveToTab("qm:b", "hud", 1, 1);

	auto VisualLeft = M.ColumnIndices("visual", 1);
	ASSERT_EQ(VisualLeft.size(), 2u);
	EXPECT_STREQ(M.Entry(VisualLeft[0]).m_pStableId, "qm:a");
	EXPECT_EQ(M.Entry(VisualLeft[0]).m_OrderInColumn, 0);
	EXPECT_STREQ(M.Entry(VisualLeft[1]).m_pStableId, "qm:c");
	EXPECT_EQ(M.Entry(VisualLeft[1]).m_OrderInColumn, 1);

	auto HudLeft = M.ColumnIndices("hud", 1);
	ASSERT_EQ(HudLeft.size(), 2u);
	EXPECT_STREQ(M.Entry(HudLeft[0]).m_pStableId, "qm:d");
	EXPECT_EQ(M.Entry(HudLeft[0]).m_OrderInColumn, 0);
	EXPECT_STREQ(M.Entry(HudLeft[1]).m_pStableId, "qm:b");
	EXPECT_EQ(M.Entry(HudLeft[1]).m_OrderInColumn, 1);
}

TEST(QmCardOrderModel, MoveToTabSerializesNewTabPlacement)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "visual", 1, 0}, {"qm:b", "hud", 1, 0}});

	M.MoveToTab("qm:a", "hud", 2, 0);

	char aBuf[256];
	M.Serialize(aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "qm:a|hud|right|0;qm:b|hud|left|0;");
}

TEST(QmCardOrderModel, SerializeParseRoundtrip)
{
	qm_card_order::CModel M;
	M.SetEntries({{"a", "visual", 1, 0}, {"b", "visual", 1, 1}, {"c", "hud", 2, 0}});
	char aBuf[256];
	M.Serialize(aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a|visual|left|0;b|visual|left|1;c|hud|right|0;");

	qm_card_order::CModel M2;
	std::vector<const char *> vValidIds = {"a", "b", "c"};
	ASSERT_TRUE(M2.Parse(aBuf, vValidIds));
	EXPECT_EQ(M2.Count(), 3);
	auto Col1 = M2.ColumnIndices("visual", 1);
	ASSERT_EQ(Col1.size(), 2u);
	EXPECT_STREQ(M2.Entry(Col1[0]).m_pStableId, "a");
	EXPECT_STREQ(M2.Entry(Col1[0]).m_pDefaultTab, "visual");
	EXPECT_STREQ(M2.Entry(Col1[1]).m_pStableId, "b");
	auto HudRight = M2.ColumnIndices("hud", 2);
	ASSERT_EQ(HudRight.size(), 1u);
	EXPECT_STREQ(M2.Entry(HudRight[0]).m_pStableId, "c");
	EXPECT_STREQ(M2.Entry(HudRight[0]).m_pDefaultTab, "hud");
}

// 意图：全局卡片配置依赖固定长度 config buffer；序列化必须暴露截断状态，
// 否则新增卡片后可能静默丢尾部 placement，下一次启动再把布局误当成用户配置。
TEST(QmCardOrderModel, SerializeReportsTruncation)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:first", "visual", 1, 0}, {"qm:second", "visual", 1, 1}});

	char aSmallBuf[20];
	EXPECT_FALSE(M.Serialize(aSmallBuf, sizeof(aSmallBuf)));
	EXPECT_EQ(aSmallBuf[0], '\0');

	char aFullBuf[128];
	EXPECT_TRUE(M.Serialize(aFullBuf, sizeof(aFullBuf)));
	EXPECT_STREQ(aFullBuf, "qm:first|visual|left|0;qm:second|visual|left|1;");
}

// 意图：各页面写回全局顺序时不应各自手写字符串拼接。
// 共享合并逻辑必须只替换指定 stableId 命名空间，并保留其它命名空间的原始 token。
TEST(QmCardOrderModel, SerializeMergedReplacingPrefixPreservesOtherNamespaces)
{
	const std::vector<qm_card_order::SEntry> ReplacementEntries = {
		{"tclient:new-left", "tclient", 1, 0},
		{"tclient:new-right", "tclient", 2, 0},
	};

	char aBuf[512];
	EXPECT_TRUE(qm_card_order::SerializeMergedReplacingPrefix(
		"qm:a|visual|left|0;tclient:old|tclient|left|0;deck:sound|sound|left|0;",
		"tclient:", ReplacementEntries, aBuf, sizeof(aBuf)));

	EXPECT_STREQ(aBuf, "qm:a|visual|left|0;deck:sound|sound|left|0;tclient:new-left|tclient|left|0;tclient:new-right|tclient|right|0;");
}

// 意图：全局配置 buffer 有固定长度；合并写回也必须暴露截断状态，避免静默写入半条布局。
TEST(QmCardOrderModel, SerializeMergedReplacingPrefixReportsTruncation)
{
	const std::vector<qm_card_order::SEntry> ReplacementEntries = {
		{"tclient:new-left", "tclient", 1, 0},
	};

	char aSmallBuf[24];
	EXPECT_FALSE(qm_card_order::SerializeMergedReplacingPrefix(
		"qm:a|visual|left|0;", "tclient:", ReplacementEntries, aSmallBuf, sizeof(aSmallBuf)));
	EXPECT_EQ(aSmallBuf[0], '\0');
}

// 意图：非法泛化列与容量不足一样属于完整序列化失败，不能把已保留的其它命名空间半写入输出。
TEST(QmCardOrderModel, SerializeMergedReplacingPrefixRejectsInvalidColumnAtomically)
{
	const std::vector<qm_card_order::SEntry> ReplacementEntries = {
		{"tclient:invalid", "tclient", -1, 0},
	};

	char aBuf[128] = "stale";
	EXPECT_FALSE(qm_card_order::SerializeMergedReplacingPrefix(
		"qm:a|visual|left|0;", "tclient:", ReplacementEntries, aBuf, sizeof(aBuf)));
	EXPECT_EQ(aBuf[0], '\0');
}

// 意图：合并写回必须逐 token 保留旧配置，不能因固定临时缓冲区把较长的其它命名空间拆碎。
TEST(QmCardOrderModel, SerializeMergedReplacingPrefixPreservesLongExistingToken)
{
	const std::string LongStableId = "qm:" + std::string(220, 'a');
	const std::string Existing = LongStableId + "|visual|left|0;tclient:old|tclient|left|0;";
	const std::vector<qm_card_order::SEntry> ReplacementEntries = {
		{"tclient:new", "tclient", 3, 0},
	};

	char aBuf[1024];
	ASSERT_TRUE(qm_card_order::SerializeMergedReplacingPrefix(Existing.c_str(), "tclient:", ReplacementEntries, aBuf, sizeof(aBuf)));
	EXPECT_EQ(std::string(aBuf), LongStableId + "|visual|left|0;tclient:new|tclient|3|0;");
}

// 意图：程序化 entry 不受旧 160 字节临时数组限制，且泛化列最大边界可无损往返。
TEST(QmCardOrderModel, SerializeSupportsLongFieldsAndMaximumColumn)
{
	const std::string LongStableId = "deck:" + std::string(180, 'x');
	const std::string LongTab = "tab-" + std::string(180, 'y');
	qm_card_order::CModel Model;
	Model.SetEntries({{LongStableId.c_str(), LongTab.c_str(), std::numeric_limits<int>::max(), 7}});

	char aBuf[1024];
	ASSERT_TRUE(Model.Serialize(aBuf, sizeof(aBuf)));
	EXPECT_EQ(std::string(aBuf), LongStableId + "|" + LongTab + "|" + std::to_string(std::numeric_limits<int>::max()) + "|7;");
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aBuf, {{LongStableId.c_str(), LongTab.c_str(), 0, 0}}));
	const auto &ReloadedEntry = Reloaded.Entry(Reloaded.FindByStableId(LongStableId.c_str()));
	EXPECT_STREQ(ReloadedEntry.m_pDefaultTab, LongTab.c_str());
	EXPECT_EQ(ReloadedEntry.m_Column, std::numeric_limits<int>::max());
	EXPECT_EQ(ReloadedEntry.m_OrderInColumn, 0);
}

// 意图：全局卡片加载必须以注册表默认全量为基准，再用用户配置覆盖。
// 缺失卡补默认、未知/非法残留跳过，这是全局卡片唯一权威模型的核心兼容语义。
TEST(QmCardOrderModel, LoadMergedPreservesDefaultsAndAppliesValidUserPlacement)
{
	qm_card_order::CModel M;
	std::vector<qm_card_order::SEntry> Defaults = {
		{"qm:a", "visual", 1, 0},
		{"qm:b", "visual", 1, 1},
		{"qm:c", "hud", 2, 0},
	};

	EXPECT_TRUE(M.LoadMerged("qm:unknown|visual|left|0;qm:b|search|right|0;qm:c|hud|bad|4;", Defaults));

	EXPECT_EQ(M.Count(), 3);
	EXPECT_GE(M.StateIndexForStableId("qm:a"), 0);
	EXPECT_GE(M.StateIndexForStableId("qm:b"), 0);
	EXPECT_GE(M.StateIndexForStableId("qm:c"), 0);
	EXPECT_EQ(M.StateIndexForStableId("qm:unknown"), -1);

	const auto SearchRight = M.ColumnIndices("search", 2);
	ASSERT_EQ(SearchRight.size(), 1u);
	EXPECT_STREQ(M.Entry(SearchRight[0]).m_pStableId, "qm:b");
	EXPECT_EQ(M.Entry(SearchRight[0]).m_OrderInColumn, 0);

	const auto HudRight = M.ColumnIndices("hud", 2);
	ASSERT_EQ(HudRight.size(), 1u);
	EXPECT_STREQ(M.Entry(HudRight[0]).m_pStableId, "qm:c");
	EXPECT_EQ(M.Entry(HudRight[0]).m_OrderInColumn, 0);
}

// 意图：旧冒号格式没有 tab 字段；合并加载时必须继承默认 tab，
// 否则旧用户配置迁入全局模型后会从对应页面消失。
TEST(QmCardOrderModel, LoadMergedBackfillsDefaultTabForLegacyEntries)
{
	qm_card_order::CModel M;
	std::vector<qm_card_order::SEntry> Defaults = {
		{"a", "visual", 1, 0},
		{"b", "hud", 2, 0},
	};

	EXPECT_TRUE(M.LoadMerged("a:2:0", Defaults));

	const auto VisualRight = M.ColumnIndices("visual", 2);
	ASSERT_EQ(VisualRight.size(), 1u);
	EXPECT_STREQ(M.Entry(VisualRight[0]).m_pStableId, "a");
	EXPECT_STREQ(M.Entry(VisualRight[0]).m_pDefaultTab, "visual");
}

// 意图：Tclient/deck 运行时读取全局配置时只需要用户显式写入的条目，
// 不能用 LoadMerged 补全默认全集，否则会把"没有配置"误判成"用户已有顺序"。
TEST(QmCardOrderModel, LoadExplicitUsesDefaultsAsValidIdsWithoutBackfillingMissingCards)
{
	qm_card_order::CModel M;
	std::vector<qm_card_order::SEntry> Defaults = {
		{"tclient:a", "tclient", 1, 0},
		{"tclient:b", "tclient", 1, 1},
		{"deck:sound", "sound", 1, 0},
	};

	EXPECT_TRUE(M.LoadExplicit("tclient:b|tclient|right|0;unknown|tclient|left|0;", Defaults));

	EXPECT_EQ(M.Count(), 1);
	EXPECT_GE(M.StateIndexForStableId("tclient:b"), 0);
	EXPECT_EQ(M.StateIndexForStableId("tclient:a"), -1);
	EXPECT_EQ(M.StateIndexForStableId("deck:sound"), -1);
	const auto TClientRight = M.ColumnIndices("tclient", 2);
	ASSERT_EQ(TClientRight.size(), 1u);
	EXPECT_STREQ(M.Entry(TClientRight[0]).m_pStableId, "tclient:b");
}

TEST(QmCardOrderModel, ParsePipeFormatKeepsMovableTabPlacement)
{
	qm_card_order::CModel M;
	std::vector<const char *> vValidIds = {"qm:chat_bubble", "tclient:visual-nameplates", "deck:graphics-display"};
	ASSERT_TRUE(M.Parse("qm:chat_bubble|search|left|0;tclient:visual-nameplates|tclient|right|0;deck:graphics-display|graphics|full|0;", vValidIds));
	EXPECT_EQ(M.Count(), 3);

	auto SearchLeft = M.ColumnIndices("search", 1);
	ASSERT_EQ(SearchLeft.size(), 1u);
	EXPECT_STREQ(M.Entry(SearchLeft[0]).m_pStableId, "qm:chat_bubble");
	EXPECT_STREQ(M.Entry(SearchLeft[0]).m_pDefaultTab, "search");

	auto TClientRight = M.ColumnIndices("tclient", 2);
	ASSERT_EQ(TClientRight.size(), 1u);
	EXPECT_STREQ(M.Entry(TClientRight[0]).m_pStableId, "tclient:visual-nameplates");
	EXPECT_STREQ(M.Entry(TClientRight[0]).m_pDefaultTab, "tclient");

	auto GraphicsFull = M.ColumnIndices("graphics", 0);
	ASSERT_EQ(GraphicsFull.size(), 1u);
	EXPECT_STREQ(M.Entry(GraphicsFull[0]).m_pStableId, "deck:graphics-display");
	EXPECT_STREQ(M.Entry(GraphicsFull[0]).m_pDefaultTab, "graphics");
}

TEST(QmCardOrderModel, ParsePipeFormatStillAcceptsLegacyNumericColumns)
{
	qm_card_order::CModel M;
	std::vector<const char *> vValidIds = {"qm:chat_bubble", "tclient:visual-nameplates"};
	ASSERT_TRUE(M.Parse("qm:chat_bubble|visual|1|0;tclient:visual-nameplates|tclient|2|0;", vValidIds));
	EXPECT_EQ(M.Count(), 2);

	auto VisualLeft = M.ColumnIndices("visual", 1);
	ASSERT_EQ(VisualLeft.size(), 1u);
	EXPECT_STREQ(M.Entry(VisualLeft[0]).m_pStableId, "qm:chat_bubble");

	auto TClientRight = M.ColumnIndices("tclient", 2);
	ASSERT_EQ(TClientRight.size(), 1u);
	EXPECT_STREQ(M.Entry(TClientRight[0]).m_pStableId, "tclient:visual-nameplates");
}

TEST(QmCardOrderModel, ParseToleratesBadKeys)
{
	qm_card_order::CModel M;
	std::vector<const char *> vValidIds = {"a", "b"};
	// "x" 未知跳过，"a" 重复跳过，"b" column 非法(-1) 跳过
	ASSERT_TRUE(M.Parse("x:1:0;a:1:0;a:1:1;b:-1:0;b:1:0", vValidIds));
	EXPECT_EQ(M.Count(), 2); // a + b（合法）
	auto Col1 = M.ColumnIndices(1);
	ASSERT_EQ(Col1.size(), 2u);
	EXPECT_STREQ(M.Entry(Col1[0]).m_pStableId, "a");
	EXPECT_STREQ(M.Entry(Col1[1]).m_pStableId, "b");
}

TEST(QmCardOrderModel, ParseSkipsNonNumericLegacyFields)
{
	qm_card_order::CModel M;
	std::vector<const char *> vValidIds = {"a", "b"};

	ASSERT_TRUE(M.Parse("a:x:0;a:1:nope;b:1:0", vValidIds));

	EXPECT_EQ(M.Count(), 1);
	auto Col1 = M.ColumnIndices(1);
	ASSERT_EQ(Col1.size(), 1u);
	EXPECT_STREQ(M.Entry(Col1[0]).m_pStableId, "b");
}

TEST(QmCardOrderModel, ParseSkipsNonNumericPipeFields)
{
	qm_card_order::CModel M;
	std::vector<const char *> vValidIds = {"qm:a", "qm:b"};

	ASSERT_TRUE(M.Parse("qm:a|visual|wat|0;qm:a|visual|left|nope;qm:b|visual|left|0", vValidIds));

	EXPECT_EQ(M.Count(), 1);
	auto VisualLeft = M.ColumnIndices("visual", 1);
	ASSERT_EQ(VisualLeft.size(), 1u);
	EXPECT_STREQ(M.Entry(VisualLeft[0]).m_pStableId, "qm:b");
}

TEST(QmCardOrderModel, NormalizeColumnsFillsGaps)
{
	qm_card_order::CModel M;
	M.SetEntries({{"a", nullptr, 1, 5}, {"b", nullptr, 1, 10}, {"c", nullptr, 2, 3}});
	M.ClearDirty();
	M.NormalizeColumns();
	auto Col1 = M.ColumnIndices(1);
	EXPECT_EQ(M.Entry(Col1[0]).m_OrderInColumn, 0);
	EXPECT_EQ(M.Entry(Col1[1]).m_OrderInColumn, 1);
	EXPECT_STREQ(M.Entry(Col1[0]).m_pStableId, "a");
	EXPECT_STREQ(M.Entry(Col1[1]).m_pStableId, "b");
	auto Col2 = M.ColumnIndices(2);
	ASSERT_EQ(Col2.size(), 1u);
	EXPECT_EQ(M.Entry(Col2[0]).m_OrderInColumn, 0);
}

TEST(QmCardOrderModel, NormalizeColumnsMarksDirtyWhenOrdersChange)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "visual", 1, 5}, {"qm:b", "visual", 1, 9}});
	M.ClearDirty();

	M.NormalizeColumns();

	EXPECT_TRUE(M.IsDirty());
}

TEST(QmCardOrderModel, NormalizeColumnsKeepsIndependentTabColumns)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "visual", 1, 5}, {"qm:b", "hud", 1, 7}, {"qm:c", "visual", 1, 9}, {"qm:d", "hud", 2, 4}});

	M.NormalizeColumns();

	auto VisualLeft = M.ColumnIndices("visual", 1);
	ASSERT_EQ(VisualLeft.size(), 2u);
	EXPECT_STREQ(M.Entry(VisualLeft[0]).m_pStableId, "qm:a");
	EXPECT_EQ(M.Entry(VisualLeft[0]).m_OrderInColumn, 0);
	EXPECT_STREQ(M.Entry(VisualLeft[1]).m_pStableId, "qm:c");
	EXPECT_EQ(M.Entry(VisualLeft[1]).m_OrderInColumn, 1);

	auto HudLeft = M.ColumnIndices("hud", 1);
	ASSERT_EQ(HudLeft.size(), 1u);
	EXPECT_STREQ(M.Entry(HudLeft[0]).m_pStableId, "qm:b");
	EXPECT_EQ(M.Entry(HudLeft[0]).m_OrderInColumn, 0);

	auto HudRight = M.ColumnIndices("hud", 2);
	ASSERT_EQ(HudRight.size(), 1u);
	EXPECT_STREQ(M.Entry(HudRight[0]).m_pStableId, "qm:d");
	EXPECT_EQ(M.Entry(HudRight[0]).m_OrderInColumn, 0);
}

TEST(QmCardOrderModel, MoveMarksDirty)
{
	qm_card_order::CModel M;
	M.SetEntries({{"a", nullptr, 1, 0}, {"b", nullptr, 1, 1}});
	M.ClearDirty();
	EXPECT_FALSE(M.IsDirty());
	M.Move("a", 1, 1);
	EXPECT_TRUE(M.IsDirty());
	M.ClearDirty();
	EXPECT_FALSE(M.IsDirty());
}

// 意图：组件编辑器按 tab+column 筛选本页卡片——这是"页面是展示层"的核心查询。
// tab 是可变位置维度（非卡片固有归属），column 是列，二者共同定位一张卡在画布上的位置。
TEST(QmCardOrderModel, ColumnIndicesFiltersByTabAndColumn)
{
	qm_card_order::CModel M;
	std::vector<qm_card_order::SEntry> E = {
		{"qm:chat_bubble", "visual", 1, 0}, // tab="visual", Left
		{"qm:coords", "hud", 1, 0}, // tab="hud", Left
		{"qm:camera_view", "visual", 2, 0}, // tab="visual", Right
	};
	M.SetEntries(E);
	// visual tab 的 Left 列：只应含 chat_bubble，不含 coords（hud）
	auto VisLeft = M.ColumnIndices("visual", 1);
	ASSERT_EQ(VisLeft.size(), 1u);
	EXPECT_STREQ(M.Entry(VisLeft[0]).m_pStableId, "qm:chat_bubble");
}

// 意图：让位 lerp 每帧每卡查 state index，必须 O(1)——否则 69 卡拖拽掉帧。
// 栖梦靠 EQmModuleId 连续枚举白嫖 O(1) state 下标；全局卡用 stableId 无连续枚举，
// 故建 stableId→连续 index 注册表，让位 lerp 保持 O(1) 查找（性能地基，禁线性退化 O(N²)）。
TEST(QmCardOrderModel, StateIndexIsOLookupByStableId)
{
	qm_card_order::CModel M;
	M.SetEntries({{"qm:a", "t", 1, 0}, {"qm:b", "t", 1, 1}, {"qm:c", "t", 1, 2}});
	M.BuildStateIndex(); // 构建 stableId→连续 index
	// 已知 id O(1) 命中
	EXPECT_EQ(M.StateIndexForStableId("qm:b"), 1);
	// 未知 id 返回 -1（容错，不崩溃）
	EXPECT_EQ(M.StateIndexForStableId("qm:unknown"), -1);
}

// 意图：页面运行时不应该各自手写解析 qm_global_card_order。
// 公共模型需要能按 stableId 命名空间 + tab + column 导出顺序，供 Tclient/deck 等页面复用。
TEST(QmCardOrderModel, StableIdOrderFiltersByPrefixTabAndColumn)
{
	qm_card_order::CModel M;
	M.SetEntries({
		{"tclient:visual-effects", "tclient", 1, 2},
		{"qm:chat_bubble", "visual", 1, 0},
		{"tclient:visual-nameplates", "tclient", 1, 0},
		{"deck:graphics-display", "graphics", 1, 0},
		{"tclient:input", "tclient", 2, 0},
		{"tclient:anti-latency-tools", "search", 1, 0},
	});

	const std::vector<std::string> TClientLeft = M.StableIdOrder("tclient:", "tclient", 1);
	ASSERT_EQ(TClientLeft.size(), 2u);
	EXPECT_EQ(TClientLeft[0], "tclient:visual-nameplates");
	EXPECT_EQ(TClientLeft[1], "tclient:visual-effects");

	const std::vector<std::string> TClientRight = M.StableIdOrder("tclient:", "tclient", 2);
	ASSERT_EQ(TClientRight.size(), 1u);
	EXPECT_EQ(TClientRight[0], "tclient:input");
}

// 意图：设置 deck 运行时从全局模型读取顺序时，必须按 deck/tab 隔离，并可由调用方合并左右列。
TEST(QmCardOrderModel, StableIdOrderSupportsDeckTabColumnProjection)
{
	qm_card_order::CModel M;
	M.SetEntries({
		{"deck:sound-volume", "sound", 1, 1},
		{"deck:graphics-display", "graphics", 1, 0},
		{"deck:sound-toggle", "sound", 1, 0},
		{"deck:sound-audio-pack", "sound", 2, 0},
		{"tclient:input", "tclient", 1, 0},
		{"deck:ddnet-demo", "ddnet", 1, 0},
	});

	std::vector<std::string> SoundOrder = M.StableIdOrder("deck:", "sound", 1);
	const std::vector<std::string> SoundRightOrder = M.StableIdOrder("deck:", "sound", 2);
	SoundOrder.insert(SoundOrder.end(), SoundRightOrder.begin(), SoundRightOrder.end());

	ASSERT_EQ(SoundOrder.size(), 3u);
	EXPECT_EQ(SoundOrder[0], "deck:sound-toggle");
	EXPECT_EQ(SoundOrder[1], "deck:sound-volume");
	EXPECT_EQ(SoundOrder[2], "deck:sound-audio-pack");
}
