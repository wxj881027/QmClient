// Bind 工坊的预设数据与蓝图编辑器：预设过滤、占用徽标、卡片几何、图编辑、分组与导出。
#include <base/system.h>

#include <engine/keys.h>

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_workshop.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

using namespace qm_bind_workshop;

// 按 id 找一条内置预设：库是按分类排的，测试关心的是特定卡片而不是下标。
static const SPreset *FindPreset(const std::vector<SPreset> &vPresets, std::string_view Id)
{
	for(const SPreset &Preset : vPresets)
	{
		if(Preset.m_Id == Id)
			return &Preset;
	}
	return nullptr;
}
TEST(QmBindWorkshop, InfersModeFromCommandInsteadOfAskingTheUser)
{
	EXPECT_EQ(InferMode("say_team /ready"), EMode::ONCE);
	EXPECT_EQ(InferMode("toggle cl_dummy 0 1"), EMode::TOGGLE);
	EXPECT_EQ(InferMode("+toggle inp_mousesens 1 66"), EMode::HOLD);
	EXPECT_EQ(InferMode("+jump"), EMode::HOLD);
}

TEST(QmBindWorkshop, BuiltinPresetsAreWellFormed)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	ASSERT_FALSE(vPresets.empty());
	std::set<std::string> Ids;
	for(const SPreset &Preset : vPresets)
	{
		EXPECT_FALSE(Preset.m_Id.empty());
		EXPECT_TRUE(Ids.insert(Preset.m_Id).second) << "duplicate preset id: " << Preset.m_Id;
		EXPECT_FALSE(Preset.m_vVariants.empty());
		for(const SVariant &Variant : Preset.m_vVariants)
			EXPECT_FALSE(Variant.m_Command.empty());
	}
	EXPECT_EQ(CountCategory(vPresets, ECategory::ALL), vPresets.size());
	EXPECT_GT(CountCategory(vPresets, ECategory::MOVEMENT), 0u);
}

TEST(QmBindWorkshop, FiltersByCategoryAndLocalizedQuery)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const auto LocalizeStub = [](const char *pText) { return pText; };
	EXPECT_EQ(FilterPresets(vPresets, ECategory::ALL, "", LocalizeStub).size(), vPresets.size());
	EXPECT_EQ(FilterPresets(vPresets, ECategory::SERVER, "", LocalizeStub).size(), CountCategory(vPresets, ECategory::SERVER));
	const std::vector<size_t> vHits = FilterPresets(vPresets, ECategory::ALL, "HDF", LocalizeStub);
	EXPECT_FALSE(vHits.empty());
	for(const size_t Index : vHits)
		EXPECT_TRUE(MatchesQuery(vPresets[Index], "HDF", LocalizeStub));
	EXPECT_TRUE(FilterPresets(vPresets, ECategory::ALL, "definitely-not-a-bind", LocalizeStub).empty());
}

TEST(QmBindWorkshop, SearchReachesPresetsOutsideTheSelectedCategory)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const auto LocalizeStub = [](const char *pText) { return pText; };
	size_t Foreign = 0;
	for(size_t Index = 0; Index < vPresets.size(); ++Index)
	{
		if(vPresets[Index].m_Category != vPresets.front().m_Category)
		{
			Foreign = Index;
			break;
		}
	}
	ASSERT_GT(Foreign, 0u) << "presets must span more than one category";
	// 拿一条不属于首个分类的预设自己的标题当查询串。
	const std::string Query = vPresets[Foreign].m_pTitle;
	const ECategory Narrowing = vPresets.front().m_Category;
	const std::vector<size_t> vHits = FilterPresets(vPresets, Narrowing, Query, LocalizeStub);
	EXPECT_NE(std::find(vHits.begin(), vHits.end(), Foreign), vHits.end())
		<< "a text search must reach presets that sit in another category";
	EXPECT_EQ(vHits, FilterPresets(vPresets, ECategory::ALL, Query, LocalizeStub));
	// 空查询串时分类照旧生效：搜索放开跨分类不等于把分类开关废掉。
	EXPECT_EQ(FilterPresets(vPresets, Narrowing, "", LocalizeStub).size(), CountCategory(vPresets, Narrowing));
}

TEST(QmBindWorkshop, GraphCompilesInTopologicalOrder)
{
	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode First;
	First.m_Command = "toggle cl_dummy_hammer 0 1";
	const int FirstId = Graph.AddNode(First);
	SNode Second;
	Second.m_Command = "echo HDF";
	const int SecondId = Graph.AddNode(Second);
	ASSERT_TRUE(Graph.Connect(EventId, FirstId));
	ASSERT_TRUE(Graph.Connect(FirstId, SecondId));
	EXPECT_FALSE(Graph.Connect(FirstId, SecondId)) << "duplicate edge must be rejected";

	bool HasCycle = true;
	const std::vector<int> vOrder = Graph.TopologicalOrder(HasCycle);
	EXPECT_FALSE(HasCycle);
	ASSERT_EQ(vOrder.size(), 3u);
	EXPECT_EQ(vOrder.front(), EventId);

	const SCompileResult Result = Graph.Compile("h");
	EXPECT_TRUE(Result.Ok()) << Result.m_ConfigLine;
	EXPECT_EQ(Result.m_Command, "toggle cl_dummy_hammer 0 1; echo HDF");
	EXPECT_NE(Result.m_ConfigLine.find("bind h"), std::string::npos);
	EXPECT_GT(Result.m_Bytes, 0u);
}

TEST(QmBindWorkshop, GraphRejectsCyclesOrphansAndEmptyCommands)
{
	CBlueprintGraph Cyclic;
	const int A = Cyclic.AddNode(SNode{});
	const int B = Cyclic.AddNode(SNode{});
	const int C = Cyclic.AddNode(SNode{});
	Cyclic.Connect(A, B);
	Cyclic.Connect(B, C);
	Cyclic.Connect(C, A);
	bool HasCycle = false;
	(void)Cyclic.TopologicalOrder(HasCycle);
	EXPECT_TRUE(HasCycle);

	CBlueprintGraph Invalid;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Invalid.AddNode(Event);
	SNode Empty;
	const int EmptyId = Invalid.AddNode(Empty);
	SNode Orphan;
	Orphan.m_Command = "echo orphan";
	Invalid.AddNode(Orphan);
	Invalid.Connect(EventId, EmptyId);
	const SCompileResult Result = Invalid.Compile("h");
	EXPECT_FALSE(Result.Ok());
	EXPECT_NE(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::EMPTY_COMMAND), Result.m_vErrors.end());
	EXPECT_NE(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::ORPHAN_NODE), Result.m_vErrors.end());
}

TEST(QmBindWorkshop, CompileRejectsOverlongAndDeeplyNestedCommands)
{
	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode LongNode;
	LongNode.m_Command = std::string(qm_bind_editor::MAX_CONFIG_LINE_BYTES + 32, 'a');
	const int LongId = Graph.AddNode(LongNode);
	Graph.Connect(EventId, LongId);
	SCompileResult Result = Graph.Compile("h");
	EXPECT_NE(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::TOO_LONG), Result.m_vErrors.end());

	CBlueprintGraph Nested;
	SNode NestedEvent;
	NestedEvent.m_Type = ENodeType::EVENT_PRESS;
	const int NestedEventId = Nested.AddNode(NestedEvent);
	SNode NestedNode;
	for(int i = 0; i <= MAX_NESTING_DEPTH + 1; ++i)
		NestedNode.m_Command += "bind x ";
	const int NestedId = Nested.AddNode(NestedNode);
	Nested.Connect(NestedEventId, NestedId);
	Result = Nested.Compile("h");
	EXPECT_NE(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::TOO_MANY_NESTING), Result.m_vErrors.end());
}

TEST(QmBindWorkshop, VisibleNodesCullsOffscreenGraph)
{
	CBlueprintGraph Graph;
	for(int Index = 0; Index < 1000; ++Index)
	{
		SNode Node;
		Node.m_X = static_cast<float>(Index % 40) * 200.0f;
		Node.m_Y = static_cast<float>(Index / 40) * 120.0f;
		Node.m_Command = "echo node";
		Graph.AddNode(Node);
	}
	EXPECT_EQ(Graph.Nodes().size(), 1000u);
	EXPECT_GT(Graph.IndexedCellCount(), 0u);

	const std::vector<int> vVisible = Graph.VisibleNodes(0.0f, 0.0f, 300.0f, 200.0f);
	EXPECT_FALSE(vVisible.empty());
	EXPECT_LT(vVisible.size(), 100u) << "viewport culling must not return the whole graph";
	const std::vector<int> vFar = Graph.VisibleNodes(100000.0f, 100000.0f, 100300.0f, 100200.0f);
	EXPECT_TRUE(vFar.empty());

	// 拖到一个空格子后，索引必须跟随，旧视口不再返回该节点。
	const int Id = vVisible.front();
	EXPECT_TRUE(Graph.MoveNode(Id, 100050.0f, 100050.0f));
	const std::vector<int> vMoved = Graph.VisibleNodes(100000.0f, 100000.0f, 100300.0f, 100200.0f);
	EXPECT_NE(std::find(vMoved.begin(), vMoved.end(), Id), vMoved.end());
}

TEST(QmBindWorkshop, PresetBecomesAnEditableGraph)
{
	CBlueprintGraph Graph = GraphFromPreset(BuiltinPresets().front(), 0);
	EXPECT_GE(Graph.Nodes().size(), 2u);
	EXPECT_FALSE(Graph.Edges().empty());
	EXPECT_TRUE(Graph.Compile("h").Ok());
}

TEST(QmBindWorkshop, LayoutKeepsGridBodyBetweenToolbarAndFooter)
{
	// 工坊骨架是所有工坊屏幕共用的生产输入。正文必须是工具条与页脚之间的
	// 那块大区域：一旦把剩余区域交给 HSplitBottom 的 pBottom，正文就会退化成
	// 页脚上方的一条细带，卡片全部沉到面板底部只露出顶边。
	const float Scale = 2.0f;
	const CUIRect MainView{0.0f, 100.0f, 1200.0f, 700.0f};
	const SWorkshopLayoutFrame Frame = ResolveWorkshopLayout(MainView, Scale);

	EXPECT_NEAR(Frame.m_Toolbar.x, MainView.x, 0.01f);
	EXPECT_NEAR(Frame.m_Toolbar.y, MainView.y, 0.01f);
	EXPECT_NEAR(Frame.m_Toolbar.w, MainView.w, 0.01f);
	EXPECT_NEAR(Frame.m_Toolbar.h, WORKSHOP_TOOLBAR_HEIGHT * Scale, 0.01f);

	const float BodyTop = MainView.y + (WORKSHOP_TOOLBAR_HEIGHT + WORKSHOP_BODY_TOP_GAP) * Scale;
	EXPECT_NEAR(Frame.m_Body.y, BodyTop, 0.01f);
	const float BodyHeight = MainView.h - (WORKSHOP_TOOLBAR_HEIGHT + WORKSHOP_BODY_TOP_GAP + WORKSHOP_FOOTER_HEIGHT + WORKSHOP_BODY_BOTTOM_GAP) * Scale;
	EXPECT_NEAR(Frame.m_Body.h, BodyHeight, 0.01f);
	EXPECT_GT(Frame.m_Body.h, MainView.h * 0.5f);

	EXPECT_NEAR(Frame.m_Footer.y, Frame.m_Body.y + Frame.m_Body.h + WORKSHOP_BODY_BOTTOM_GAP * Scale, 0.01f);
	EXPECT_NEAR(Frame.m_Footer.h, WORKSHOP_FOOTER_HEIGHT * Scale, 0.01f);
	EXPECT_NEAR(Frame.m_Footer.y + Frame.m_Footer.h, MainView.y + MainView.h, 0.01f);

	// 窗口高度变小同样成立：细带回归不依赖具体分辨率。
	const CUIRect SmallView{0.0f, 0.0f, 800.0f, 420.0f};
	const SWorkshopLayoutFrame SmallFrame = ResolveWorkshopLayout(SmallView, 1.0f);
	EXPECT_GT(SmallFrame.m_Body.h, SmallView.h * 0.5f);
	EXPECT_GE(SmallFrame.m_Body.y, SmallFrame.m_Toolbar.y + SmallFrame.m_Toolbar.h);
}

TEST(QmBindWorkshop, CanvasHitTestPrefersPortsOverNodeBody)
{
	CBlueprintGraph Graph;
	SNode Node;
	Node.m_Type = ENodeType::EVENT_PRESS;
	Node.m_X = 100.0f;
	Node.m_Y = 200.0f;
	const int Id = Graph.AddNode(Node);
	const float CenterY = 200.0f + NODE_HEIGHT * 0.5f;

	SCanvasHit Hit = HitTestCanvas(Graph, 100.0f + NODE_WIDTH * 0.5f, CenterY);
	EXPECT_EQ(Hit.m_NodeId, Id);
	EXPECT_EQ(Hit.m_Part, ECanvasPart::BODY);

	// 端口在节点边线上：同时落在节点体范围内时必须优先命中端口。
	Hit = HitTestCanvas(Graph, 100.0f + NODE_WIDTH, CenterY);
	EXPECT_EQ(Hit.m_NodeId, Id);
	EXPECT_EQ(Hit.m_Part, ECanvasPart::OUT_PORT);

	Hit = HitTestCanvas(Graph, 100.0f, CenterY);
	EXPECT_EQ(Hit.m_NodeId, Id);
	EXPECT_EQ(Hit.m_Part, ECanvasPart::IN_PORT);

	Hit = HitTestCanvas(Graph, 5000.0f, 5000.0f);
	EXPECT_TRUE(Hit.Empty());
	EXPECT_EQ(Hit.m_NodeId, 0);
}

TEST(QmBindWorkshop, DropOnInputPortConnectsAndDropOnEmptyCanvasDoesNot)
{
	CBlueprintGraph Graph;
	SNode First;
	First.m_X = 0.0f;
	First.m_Y = 0.0f;
	First.m_Command = "echo first";
	const int FirstId = Graph.AddNode(First);
	SNode Second;
	Second.m_X = 400.0f;
	Second.m_Y = 0.0f;
	Second.m_Command = "echo second";
	const int SecondId = Graph.AddNode(Second);

	EXPECT_EQ(ResolveLinkDrop(Graph, FirstId, 400.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::CONNECTED);
	ASSERT_EQ(Graph.Edges().size(), 1u);
	EXPECT_EQ(Graph.Edges().front().m_From, FirstId);
	EXPECT_EQ(Graph.Edges().front().m_To, SecondId);

	// 空白画布：取消，且不留下任何残线。
	EXPECT_EQ(ResolveLinkDrop(Graph, SecondId, 200.0f, 900.0f), ELinkOutcome::NO_TARGET);
	EXPECT_EQ(Graph.Edges().size(), 1u);
	// 落回自己的输入端口同样不建边。
	EXPECT_EQ(ResolveLinkDrop(Graph, FirstId, 0.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::SELF);
	EXPECT_EQ(Graph.Edges().size(), 1u);
	// 源节点已被删除时按失源处理，而不是连到别的节点上。
	EXPECT_EQ(ResolveLinkDrop(Graph, 9999, 400.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::NO_SOURCE);
	EXPECT_EQ(Graph.Edges().size(), 1u);
	EXPECT_FALSE(std::string(LinkOutcomeLabel(ELinkOutcome::NO_TARGET)).empty());
}

TEST(QmBindWorkshop, DuplicateAndCyclicLinksAreRejectedBeforeCreatingTheEdge)
{
	CBlueprintGraph Graph;
	SNode First;
	First.m_X = 0.0f;
	First.m_Y = 0.0f;
	const int FirstId = Graph.AddNode(First);
	SNode Second;
	Second.m_X = 400.0f;
	Second.m_Y = 0.0f;
	const int SecondId = Graph.AddNode(Second);

	ASSERT_EQ(ResolveLinkDrop(Graph, FirstId, 400.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::CONNECTED);
	EXPECT_EQ(ResolveLinkDrop(Graph, FirstId, 400.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::DUPLICATE);
	EXPECT_EQ(Graph.Edges().size(), 1u);

	EXPECT_EQ(ResolveLinkDrop(Graph, SecondId, 0.0f, NODE_HEIGHT * 0.5f), ELinkOutcome::CYCLE);
	EXPECT_EQ(Graph.Edges().size(), 1u) << "a rejected drop must not leave a half-made edge";

	bool HasCycle = true;
	(void)Graph.TopologicalOrder(HasCycle);
	EXPECT_FALSE(HasCycle);
}

TEST(QmBindWorkshop, TemplateCardsCoverEventCommandEchoAndNote)
{
	const std::vector<SNodeTemplate> &vTemplates = NodeTemplates();
	ASSERT_FALSE(vTemplates.empty());
	bool HasEvent = false;
	bool HasCommand = false;
	bool HasEcho = false;
	bool HasNote = false;
	size_t EchoIndex = vTemplates.size();
	for(size_t Index = 0; Index < vTemplates.size(); ++Index)
	{
		switch(vTemplates[Index].m_Type)
		{
		case ENodeType::EVENT_PRESS: HasEvent = true; break;
		case ENodeType::ACTION_COMMAND: HasCommand = true; break;
		case ENodeType::ACTION_ECHO:
			HasEcho = true;
			if(EchoIndex >= vTemplates.size())
				EchoIndex = Index;
			break;
		case ENodeType::NOTE: HasNote = true; break;
		default: break;
		}
	}
	EXPECT_TRUE(HasEvent);
	EXPECT_TRUE(HasCommand);
	EXPECT_TRUE(HasEcho);
	EXPECT_TRUE(HasNote);

	CBlueprintGraph Graph;
	ASSERT_LT(EchoIndex, vTemplates.size());
	const int Id = AddNodeFromTemplate(Graph, EchoIndex, 95.0f, 181.0f);
	ASSERT_NE(Id, 0);
	const SNode *pNode = Graph.FindNode(Id);
	ASSERT_NE(pNode, nullptr);
	EXPECT_EQ(pNode->m_Type, ENodeType::ACTION_ECHO);
	EXPECT_EQ(pNode->m_Command, std::string(vTemplates[EchoIndex].m_pCommand));
	// 95/180 约 0.53、181/180 约 1.005：都吸附到网格格点，新节点不会压在既有节点上。
	EXPECT_NEAR(pNode->m_X, GRID_CELL_SIZE, 0.01f);
	EXPECT_NEAR(pNode->m_Y, GRID_CELL_SIZE, 0.01f);
	EXPECT_EQ(AddNodeFromTemplate(Graph, vTemplates.size(), 0.0f, 0.0f), 0) << "out of range template must not add a node";
	EXPECT_EQ(Graph.Nodes().size(), 1u);
}

TEST(QmBindWorkshop, DeleteAndDisconnectRemoveIncidentEdges)
{
	CBlueprintGraph Graph;
	SNode First;
	First.m_X = 0.0f;
	const int FirstId = Graph.AddNode(First);
	SNode Second;
	Second.m_X = 400.0f;
	const int SecondId = Graph.AddNode(Second);
	SNode Third;
	Third.m_X = 800.0f;
	const int ThirdId = Graph.AddNode(Third);
	ASSERT_TRUE(Graph.Connect(FirstId, SecondId));
	ASSERT_TRUE(Graph.Connect(SecondId, ThirdId));

	EXPECT_EQ(Graph.DisconnectNode(SecondId), 2);
	EXPECT_TRUE(Graph.Edges().empty());
	EXPECT_EQ(Graph.DisconnectNode(SecondId), 0);
	EXPECT_NE(Graph.FindNode(SecondId), nullptr) << "disconnect must not delete the node itself";

	ASSERT_TRUE(Graph.Connect(FirstId, SecondId));
	EXPECT_TRUE(Graph.RemoveNode(SecondId));
	EXPECT_TRUE(Graph.Edges().empty()) << "deleting a node must not leave dangling edges";
	EXPECT_NE(Graph.FindNode(FirstId), nullptr);
	EXPECT_FALSE(Graph.RemoveNode(SecondId));
}

TEST(QmBindWorkshop, DuplicateCopiesContentWithoutEdges)
{
	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Action;
	Action.m_X = 300.0f;
	Action.m_Command = "echo copy me";
	const int ActionId = Graph.AddNode(Action);
	ASSERT_TRUE(Graph.Connect(EventId, ActionId));

	const int CopyId = Graph.DuplicateNode(ActionId, NODE_WIDTH + 24.0f, 0.0f);
	ASSERT_NE(CopyId, 0);
	ASSERT_NE(CopyId, ActionId);
	const SNode *pCopy = Graph.FindNode(CopyId);
	ASSERT_NE(pCopy, nullptr);
	const SNode *pSource = Graph.FindNode(ActionId);
	ASSERT_NE(pSource, nullptr);
	EXPECT_EQ(pCopy->m_Command, pSource->m_Command);
	EXPECT_GT(pCopy->m_X, pSource->m_X);
	EXPECT_EQ(Graph.Edges().size(), 1u) << "duplicate must not clone edges";
	EXPECT_EQ(Graph.DuplicateNode(9999, 0.0f, 0.0f), 0);
}

TEST(QmBindWorkshop, SnippetInsertionKeepsCommandSegmentsUnique)
{
	EXPECT_EQ(InsertSnippet("", "echo QmClient"), "echo QmClient");
	EXPECT_EQ(InsertSnippet("toggle cl_dummy 0 1", "echo HDF"), "toggle cl_dummy 0 1; echo HDF");
	EXPECT_EQ(InsertSnippet("toggle cl_dummy 0 1;", "echo HDF"), "toggle cl_dummy 0 1; echo HDF");
	EXPECT_EQ(InsertSnippet("echo a; echo b", "echo b"), "echo a; echo b") << "clicking the same snippet twice must not duplicate it";
	EXPECT_EQ(InsertSnippet("echo a", "echo abc"), "echo a; echo abc") << "a partial match is a different command";

	const std::vector<SCommandSnippet> &vSnippets = CommandSnippets();
	ASSERT_FALSE(vSnippets.empty());
	const char *const apExpected[] = {"echo", "bind", "toggle", "+hook", "mouse1"};
	for(const char *pExpected : apExpected)
	{
		const bool Found = std::any_of(vSnippets.begin(), vSnippets.end(), [pExpected](const SCommandSnippet &Snippet) { return std::string_view(Snippet.m_pLabel) == pExpected; });
		EXPECT_TRUE(Found) << "missing quick insert snippet: " << pExpected;
	}
}

TEST(QmBindWorkshop, NodeTypeCycleWrapsAndNotesStayOutOfOutput)
{
	EXPECT_EQ(CycleNodeType(ENodeType::NOTE), ENodeType::EVENT_PRESS);
	EXPECT_EQ(CycleNodeType(ENodeType::EVENT_PRESS, -1), ENodeType::NOTE);
	EXPECT_EQ(CycleNodeType(ENodeType::ACTION_COMMAND), ENodeType::ACTION_ECHO);

	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Action;
	Action.m_X = 300.0f;
	Action.m_Command = "echo hi";
	const int ActionId = Graph.AddNode(Action);
	ASSERT_TRUE(Graph.Connect(EventId, ActionId));
	ASSERT_TRUE(Graph.Compile("h").Ok());

	// 命令节点改成注释节点：不再产出命令，因此报「没有可执行动作」而不是空命令。
	EXPECT_TRUE(Graph.SetNodeType(ActionId, ENodeType::NOTE));
	EXPECT_TRUE(Graph.SetNodeComment(ActionId, "no delay in this flow"));
	const SCompileResult Result = Graph.Compile("h");
	EXPECT_NE(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::NO_OUTPUT), Result.m_vErrors.end());
	EXPECT_EQ(std::find(Result.m_vErrors.begin(), Result.m_vErrors.end(), ECompileError::EMPTY_COMMAND), Result.m_vErrors.end());
	EXPECT_FALSE(Graph.SetNodeType(9999, ENodeType::NOTE));
}

TEST(QmBindWorkshop, NestedBindReportsDepthAndFitsConfigLine)
{
	// 命令节点里再写一段 bind：拓扑编译要把嵌套层数和配置行长度反馈给 UI。
	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Action;
	Action.m_X = 300.0f;
	Action.m_Command = "bind f1 \"toggle cl_dummy 0 1\"";
	const int ActionId = Graph.AddNode(Action);
	ASSERT_TRUE(Graph.Connect(EventId, ActionId));

	const SCompileResult Result = Graph.Compile("h");
	EXPECT_TRUE(Result.Ok()) << Result.m_ConfigLine;
	EXPECT_EQ(Result.m_Depth, 1);
	EXPECT_EQ(Result.m_Bytes, Result.m_ConfigLine.size());
	EXPECT_NE(Result.m_ConfigLine.find("bind f1 \\\"toggle cl_dummy 0 1\\\""), std::string::npos)
		<< "nested bind quotes must survive the outer bind line: " << Result.m_ConfigLine;
}

TEST(QmBindWorkshop, FlowCollectionMakesUniqueFileNamesFromTitles)
{
	EXPECT_EQ(SlugifyTitle("Dummy Hammer (HDF)"), "dummy_hammer_hdf");
	EXPECT_EQ(SlugifyTitle("   "), "flow");
	EXPECT_EQ(BlueprintFileName("Dummy Hammer (HDF)"), "qm_bind_blueprint_dummy_hammer_hdf.cfg");

	CFlowCollection Flows;
	ASSERT_EQ(Flows.Count(), 1u);
	EXPECT_TRUE(Flows.RenameFlow(0, "Dummy Hammer (HDF)"));
	EXPECT_EQ(Flows.FileName(0), "qm_bind_blueprint_dummy_hammer_hdf.cfg");

	// 同名图的文件名不能互相覆盖。
	const size_t Second = Flows.AddFlow("Dummy Hammer (HDF)");
	ASSERT_EQ(Second, 1u);
	EXPECT_EQ(Flows.FileName(1), "qm_bind_blueprint_dummy_hammer_hdf_2.cfg");
	EXPECT_NE(Flows.FileName(0), Flows.FileName(1));
	EXPECT_TRUE(Flows.SetActive(1));
	EXPECT_EQ(Flows.ActiveIndex(), 1u);
	EXPECT_TRUE(Flows.RenameFlow(1, "Second flow"));
	EXPECT_EQ(Flows.FileName(1), "qm_bind_blueprint_second_flow.cfg");

	EXPECT_FALSE(Flows.RenameFlow(0, ""));
	EXPECT_TRUE(Flows.RemoveFlow(1));
	EXPECT_EQ(Flows.Count(), 1u);
	EXPECT_FALSE(Flows.RemoveFlow(0)) << "a flow collection must keep at least one flow";
}

TEST(QmBindWorkshop, ExportedCfgCarriesHeaderAndBindLine)
{
	CFlowCollection Flows;
	ASSERT_TRUE(Flows.RenameFlow(0, "Dummy Hammer (HDF)"));
	CBlueprintGraph &Graph = Flows.Graph(0);
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Action;
	Action.m_X = 300.0f;
	Action.m_Command = "toggle cl_dummy_hammer 0 1";
	ASSERT_TRUE(Graph.Connect(EventId, Graph.AddNode(Action)));

	const SCfgBundle Bundle = Flows.BuildCfg(0, "h");
	EXPECT_TRUE(Bundle.m_Ok);
	EXPECT_EQ(Bundle.m_BindLines, 1u);
	EXPECT_NE(Bundle.m_Content.find("# QmClient bind blueprint"), std::string::npos);
	EXPECT_NE(Bundle.m_Content.find("bind h \"toggle cl_dummy_hammer 0 1\""), std::string::npos) << Bundle.m_Content;
	EXPECT_NE(Bundle.m_Content.find(Bundle.m_FileName), std::string::npos) << "the exported file must name itself so it can be exec'd";

	const std::vector<SCfgBundle> vAll = Flows.BuildAllCfg("h");
	ASSERT_EQ(vAll.size(), Flows.Count());
	EXPECT_EQ(vAll.front().m_FileName, Bundle.m_FileName);
	EXPECT_EQ(vAll.front().m_Content, Bundle.m_Content);
}

TEST(QmBindWorkshop, InvalidFlowExportsAHeaderWithoutBindLine)
{
	CFlowCollection Flows;
	const SCfgBundle Bundle = Flows.BuildCfg(0, "h");
	EXPECT_FALSE(Bundle.m_Ok);
	EXPECT_EQ(Bundle.m_BindLines, 0u);
	EXPECT_EQ(Bundle.m_Content.find("\nbind "), std::string::npos) << "an invalid flow must not write a half-working bind line";
	EXPECT_NE(Bundle.m_Content.find("# Rejected"), std::string::npos);
	EXPECT_NE(Bundle.m_Content.find(CompileErrorLabel(ECompileError::MISSING_EVENT)), std::string::npos);
	EXPECT_FALSE(Bundle.m_FileName.empty());
}

TEST(QmBindWorkshop, EveryCategoryHasPresetsAndFiltersAgreeWithCounts)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	ASSERT_FALSE(vPresets.empty());
	size_t Total = 0;
	for(int Category = 0; Category < static_cast<int>(ECategory::NUM); ++Category)
	{
		const ECategory Value = static_cast<ECategory>(Category);
		if(Value == ECategory::ALL)
			continue;
		// 空分类在下拉里不会出现，等于一个永远点不到的枚举值。
		const size_t Count = CountCategory(vPresets, Value);
		EXPECT_GT(Count, 0u) << "category without presets: " << CategoryLabel(Value);
		const std::vector<size_t> vFiltered = FilterPresets(vPresets, Value, "");
		EXPECT_EQ(vFiltered.size(), Count) << "count and filter must agree for " << CategoryLabel(Value);
		for(const size_t Index : vFiltered)
			EXPECT_EQ(vPresets[Index].m_Category, Value);
		Total += Count;
	}
	// 每条预设都必须落在某个具体分类里：漏写一个分类值就会在这里少一条。
	EXPECT_EQ(Total, vPresets.size());
	for(const SPreset &Preset : vPresets)
	{
		for(const SVariant &Variant : Preset.m_vVariants)
		{
			// bind 体只能是一行：换行会把后半截丢在控制台外面。
			EXPECT_EQ(Variant.m_Command.find('\n'), std::string::npos) << Preset.m_Id;
			EXPECT_FALSE(Variant.m_Command.empty()) << Preset.m_Id;
			EXPECT_NE(Variant.m_Command.front(), ' ') << Preset.m_Id;
			EXPECT_NE(Variant.m_Command.back(), ' ') << Preset.m_Id;
		}
	}
}

TEST(QmBindWorkshop, CategoryLabelCoversEveryCategoryWithoutFallingBackToAll)
{
	std::set<std::string> Labels;
	for(int Category = 0; Category < static_cast<int>(ECategory::NUM); ++Category)
	{
		const ECategory Value = static_cast<ECategory>(Category);
		const char *pLabel = CategoryLabel(Value);
		ASSERT_NE(pLabel, nullptr);
		const std::string Label(pLabel);
		EXPECT_FALSE(Label.empty());
		EXPECT_TRUE(Labels.insert(Label).second) << "two categories share a label: " << Label;
		if(Value == ECategory::ALL)
			EXPECT_EQ(Label, "All");
		else
			// default → All 的旧兜底会把新增分类静默显示成 All；
			// 每个枚举值都必须有自己的标签。
			EXPECT_NE(Label, "All") << "category " << Category << " fell back to All";
	}
}

TEST(QmBindWorkshop, CfgFlagMatchesCommandsAndGatesOnlyExecVariants)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	size_t NumNeedsCfg = 0;
	size_t NumGated = 0;
	for(const SPreset &Preset : vPresets)
	{
		bool AnyExec = false;
		for(const SVariant &Variant : Preset.m_vVariants)
			AnyExec = AnyExec || VariantRequiresCfg(Variant);
		// 命令只是 exec 一个 cfg 包：没有随包分发的文件时写下去就是死键，
		// 所以标记必须和命令文本一致，两个方向都不许漂。
		EXPECT_EQ(Preset.m_RequiresCfg, AnyExec) << "cfg flag must match the command: " << Preset.m_Id;
		// 一组动作是整套门控：任一变体缺 cfg 就整份不出计划，不能只写半套绑定。
		const bool GroupGated = IsActionSet(Preset) && Preset.m_RequiresCfg;
		const FBoundCommand UnboundHere = [](const CBindSlot &) { return std::string(); };
		for(size_t Variant = 0; Variant < Preset.m_vVariants.size(); ++Variant)
		{
			const std::vector<CBindSlot> vSlots = {CBindSlot(KEY_H, KeyModifier::NONE)};
			const SApplyPlan Uninstalled = MakePresetPlan(Preset, Variant, vSlots, UnboundHere, nullptr, 0, false);
			const SApplyPlan Installed = MakePresetPlan(Preset, Variant, vSlots, UnboundHere, nullptr, 0, true);
			if(VariantRequiresCfg(Preset.m_vVariants[Variant]))
			{
				// exec 变体：没装 cfg 时不能出计划（按钮是 Install and apply），
				// 装好之后才放行（按钮变 Apply）。
				++NumGated;
				EXPECT_FALSE(Uninstalled.Valid()) << "a cfg variant must not be applied before its file exists: " << Preset.m_Id;
				EXPECT_TRUE(Installed.Valid()) << Preset.m_Id;
			}
			else if(GroupGated)
			{
				// 同一组动作里的纯 bind 变体跟着整套一起门控（三格水跳的左右同跳）。
				EXPECT_FALSE(Uninstalled.Valid()) << Preset.m_Id;
			}
			else
			{
				EXPECT_TRUE(Uninstalled.Valid()) << Preset.m_Id;
			}
		}
		if(Preset.m_RequiresCfg)
			++NumNeedsCfg;
	}
	// research §5.2 的 P1 清单是 12 条依赖 cfg 的预设：少一条就说明数据被删了。
	EXPECT_GE(NumNeedsCfg, 12u);
	EXPECT_GE(NumGated, 12u);
}
TEST(QmBindWorkshop, PrecisionJumpRestoresMouseDistanceInsteadOfOverwriting)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const SPreset *pJump = FindPreset(vPresets, "precision_jump");
	ASSERT_NE(pJump, nullptr);
	ASSERT_FALSE(pJump->m_vVariants.empty());
	const std::string &Jump = pJump->m_vVariants.front().m_Command;
	// 起跳时把 cl_mouse_max_distance 拉到 2，松开时必须还回用户自己设的值。
	// 写死恢复值（旧的 260）会和 angle_45 争同一个配置项：两卡交叉按下后
	// 鼠标距离会停在 260/350，直到重启客户端。
	EXPECT_NE(Jump.find("+toggle_restore cl_mouse_max_distance 2"), std::string::npos) << Jump;
	EXPECT_EQ(Jump.find("+toggle cl_mouse_max_distance"), std::string::npos) << "a fixed restore value would overwrite the user's own setting: " << Jump;
	const SPreset *pAngle = FindPreset(vPresets, "angle_45");
	ASSERT_NE(pAngle, nullptr);
	ASSERT_FALSE(pAngle->m_vVariants.empty());
	const std::string &Angle = pAngle->m_vVariants.front().m_Command;
	EXPECT_NE(Angle.find("+toggle cl_mouse_max_distance 2 "), std::string::npos) << Angle;
	EXPECT_NE(Angle.find("+toggle_restore inp_mousesens 1"), std::string::npos) << Angle;
	// 350 现在是参数默认值：模板留占位符，替换完仍然是站点值，用户可以改。
	ASSERT_EQ(pAngle->m_vParams.size(), 1u);
	EXPECT_EQ(pAngle->m_vParams.front().m_Default, 350);
	EXPECT_EQ(Angle.find("350"), std::string::npos) << "the restore distance must come from a parameter: " << Angle;
	EXPECT_NE(ResolveVariantCommand(*pAngle, 0).find("+toggle cl_mouse_max_distance 2 350"), std::string::npos);
}

// ---- 卡片两行底栏的纯几何 ----------------
TEST(QmBindWorkshop, CardFooterKeepsTwoRowsAndNeverOverlapsTheKeySlot)
{
	SWorkshopCardFooterRequest Request;
	Request.m_UiScale = 1.0f;
	Request.m_ModeBadgeWidth = 90.0f;
	Request.m_VariantWidth = 100.0f;
	Request.m_ApplyWidth = 62.0f;
	Request.m_NumSteppers = 1;
	const CUIRect Inner{20.0f, 100.0f, 272.0f, 160.0f};
	const SWorkshopCardFooter Footer = ResolveWorkshopCardFooter(Inner, Request);

	// 两行必须上下分开：第一行模式/变体/来源，第二行键槽/应用。
	EXPECT_LE(Footer.m_ModeRow.y + Footer.m_ModeRow.h, Footer.m_ActionRow.y);
	EXPECT_NEAR(Footer.m_ActionRow.h, WORKSHOP_ACTION_ROW_HEIGHT, 0.01f);
	EXPECT_NEAR(Footer.m_ModeRow.h, WORKSHOP_MODE_ROW_HEIGHT, 0.01f);
	// 参数行与键槽同高，并且在模式行之上。
	ASSERT_EQ(Footer.m_NumSteppers, 1u);
	EXPECT_NEAR(Footer.m_aSteppers[0].h, Footer.m_ActionRow.h, 0.01f);
	EXPECT_LE(Footer.m_ParamRow.y + Footer.m_ParamRow.h, Footer.m_ModeRow.y);
	// 键槽吃满剩余宽度、不小于最小宽度，且与按钮隔着间距：DoKeyReader 的矩形
	// 不会盖在应用按钮上（旧版单行底栏正是把这两者压在一起）。
	EXPECT_GE(Footer.m_KeySlot.w, WORKSHOP_KEY_SLOT_MIN_WIDTH);
	EXPECT_NEAR(Footer.m_KeySlot.x, Inner.x, 0.01f);
	EXPECT_LE(Footer.m_KeySlot.x + Footer.m_KeySlot.w, Footer.m_ApplyButton.x);
	EXPECT_GE(Footer.m_ApplyButton.x - (Footer.m_KeySlot.x + Footer.m_KeySlot.w), WORKSHOP_ITEM_GAP - 0.01f);
	EXPECT_NEAR(Footer.m_ApplyButton.x + Footer.m_ApplyButton.w, Inner.x + Inner.w, 0.01f);
	// 第一行三段互不重叠，来源贴右。
	EXPECT_LE(Footer.m_ModeBadge.x + Footer.m_ModeBadge.w, Footer.m_VariantButton.x);
	EXPECT_LE(Footer.m_VariantButton.x + Footer.m_VariantButton.w, Footer.m_SourceLabel.x);
	EXPECT_NEAR(Footer.m_SourceLabel.x + Footer.m_SourceLabel.w, Inner.x + Inner.w, 0.01f);
	// 命令框还剩得下高度，并且在参数行之上。
	EXPECT_GT(Footer.m_CodeArea.h, 0.0f);
	EXPECT_LE(Footer.m_CodeArea.y + Footer.m_CodeArea.h, Footer.m_ParamRow.y + 0.01f);

	// 极窄卡片：全部矩形仍然不重叠，键槽退让而不是被压成负数。
	SWorkshopCardFooterRequest Narrow = Request;
	Narrow.m_NumSteppers = 0;
	Narrow.m_ModeBadgeWidth = 400.0f;
	const CUIRect NarrowInner{0.0f, 0.0f, 120.0f, 140.0f};
	const SWorkshopCardFooter NarrowFooter = ResolveWorkshopCardFooter(NarrowInner, Narrow);
	EXPECT_GT(NarrowFooter.m_KeySlot.w, 0.0f);
	EXPECT_LE(NarrowFooter.m_KeySlot.x + NarrowFooter.m_KeySlot.w, NarrowFooter.m_ApplyButton.x);
	EXPECT_LE(NarrowFooter.m_ModeBadge.x + NarrowFooter.m_ModeBadge.w, NarrowFooter.m_SourceLabel.x);
	EXPECT_NEAR(NarrowFooter.m_ApplyButton.x + NarrowFooter.m_ApplyButton.w, NarrowInner.x + NarrowInner.w, 0.01f);
	EXPECT_EQ(NarrowFooter.m_NumSteppers, 0u);

	// 卡片高度必须容得下两行（含间距）与一行命令框：常量就是网格行高。
	EXPECT_GT(WORKSHOP_CARD_HEIGHT, 2.0f * (WORKSHOP_MODE_ROW_HEIGHT + WORKSHOP_ACTION_ROW_HEIGHT + WORKSHOP_ROW_GAP + WORKSHOP_ITEM_GAP));
	// 动作行与模式行同高：键槽右侧的清空按钮是正方形（DoKeyReader 用 pRect->h 切右栏），
	// 20 是「图标 + 内边距」还能看清并点中的下限；再高会让卡片底部多出一条更粗的横带。
	EXPECT_NEAR(WORKSHOP_ACTION_ROW_HEIGHT, WORKSHOP_MODE_ROW_HEIGHT, 0.01f);
	EXPECT_GE(WORKSHOP_ACTION_ROW_HEIGHT, 20.0f) << "the key reader's square clear button needs a clickable row";
}

TEST(QmBindWorkshop, EdgeHitTestFindsOnlyThePolylineUnderThePointer)
{
	CBlueprintGraph Graph;
	SNode From;
	From.m_X = 0.0f;
	From.m_Y = 0.0f;
	const int FromId = Graph.AddNode(From);
	SNode To;
	To.m_X = 400.0f;
	// 不同行高：连线是 S 形折线，中段才有真正的竖直段可命中。
	To.m_Y = 200.0f;
	const int ToId = Graph.AddNode(To);
	ASSERT_TRUE(Graph.Connect(FromId, ToId));

	SEdgePolyline Line;
	ASSERT_TRUE(ResolveEdgePolyline(Graph, FromId, ToId, Line));
	EXPECT_NEAR(Line.m_X0, From.m_X + NODE_WIDTH, 0.01f);
	EXPECT_NEAR(Line.m_X1, To.m_X, 0.01f);
	EXPECT_NEAR(Line.m_Y0, From.m_Y + NODE_HEIGHT * 0.5f, 0.01f);
	EXPECT_NEAR(Line.m_MidX, (Line.m_X0 + Line.m_X1) * 0.5f, 0.01f);

	// 曲线上的采样点：贴着任意一段都能命中（折线与曲线用同一份采样）。
	const std::vector<SCanvasPoint> vCurve = SampleEdgeCurve(Line);
	ASSERT_GE(vCurve.size(), 2u);
	const SCanvasPoint Mid = vCurve[vCurve.size() / 2];
	const SEdgeHit OnLine = HitTestEdge(Graph, Mid.m_X, Mid.m_Y);
	EXPECT_EQ(OnLine.m_From, FromId);
	EXPECT_EQ(OnLine.m_To, ToId);
	// 容差之外（节点内部、画布另一角）不命中，避免误删别的线。
	EXPECT_TRUE(HitTestEdge(Graph, From.m_X + NODE_WIDTH * 0.5f, From.m_Y + NODE_HEIGHT * 0.5f).Empty());
	EXPECT_TRUE(HitTestEdge(Graph, Line.m_MidX, Line.m_Y0 + 400.0f).Empty());
	// 端点节点不存在时几何无效，调用方直接跳过。
	SEdgePolyline Missing;
	EXPECT_FALSE(ResolveEdgePolyline(Graph, FromId, 9999, Missing));
}

TEST(QmBindWorkshop, DisconnectingOneLinkKeepsNodesAndSiblingLinks)
{
	CBlueprintGraph Graph;
	SNode Press;
	Press.m_Type = ENodeType::EVENT_PRESS;
	const int PressId = Graph.AddNode(Press);
	SNode First;
	First.m_X = 300.0f;
	First.m_Y = 200.0f;
	First.m_Command = "+jump";
	const int FirstId = Graph.AddNode(First);
	SNode Second;
	Second.m_X = 600.0f;
	Second.m_Command = "+fire";
	const int SecondId = Graph.AddNode(Second);
	ASSERT_TRUE(Graph.Connect(PressId, FirstId));
	ASSERT_TRUE(Graph.Connect(PressId, SecondId));
	ASSERT_EQ(Graph.Edges().size(), 2u);

	// 点在第一条线上拿到的是这条边，剪掉后另一条线、两个节点都还在。
	SEdgePolyline Line;
	ASSERT_TRUE(ResolveEdgePolyline(Graph, PressId, FirstId, Line));
	const SEdgeHit Hit = HitTestEdge(Graph, Line.m_MidX, (Line.m_Y0 + Line.m_Y1) * 0.5f);
	ASSERT_FALSE(Hit.Empty());
	EXPECT_EQ(Hit.m_From, PressId);
	EXPECT_EQ(Hit.m_To, FirstId);
	ASSERT_TRUE(Graph.Disconnect(Hit.m_From, Hit.m_To));
	EXPECT_EQ(Graph.Edges().size(), 1u);
	EXPECT_EQ(Graph.Edges().front().m_To, SecondId);
	EXPECT_NE(Graph.FindNode(PressId), nullptr);
	EXPECT_NE(Graph.FindNode(FirstId), nullptr);
	// 已经剪掉的边再剪一次返回 false，UI 不会误报删除成功。
	EXPECT_FALSE(Graph.Disconnect(Hit.m_From, Hit.m_To));
}

TEST(QmBindWorkshop, RectSelectionPicksEveryOverlappingNodeAndIgnoresTheRest)
{
	CBlueprintGraph Graph;
	SNode A;
	A.m_X = 0.0f;
	A.m_Y = 0.0f;
	const int AId = Graph.AddNode(A);
	SNode B;
	B.m_X = 220.0f;
	B.m_Y = 0.0f;
	const int BId = Graph.AddNode(B);
	SNode C;
	C.m_X = 0.0f;
	C.m_Y = 400.0f;
	const int CId = Graph.AddNode(C);

	// 只框住 A 与 B 的上半部分：C 在远处不能被带进来。
	const std::vector<int> vTop = NodesInRect(Graph, A.m_X - 10.0f, A.m_Y - 10.0f, B.m_X + 10.0f, A.m_Y + NODE_HEIGHT * 0.5f);
	ASSERT_EQ(vTop.size(), 2u);
	EXPECT_EQ(vTop[0], AId);
	EXPECT_EQ(vTop[1], BId);

	// 刚好贴住 B 的右边缘也算选中（含边沿相接）。
	const std::vector<int> vEdge = NodesInRect(Graph, B.m_X + NODE_WIDTH, B.m_Y, B.m_X + NODE_WIDTH + 5.0f, B.m_Y + NODE_HEIGHT);
	ASSERT_EQ(vEdge.size(), 1u);
	EXPECT_EQ(vEdge[0], BId);

	// 空框不选中任何东西，C 单独框出来也只有它。
	EXPECT_TRUE(NodesInRect(Graph, 2000.0f, 2000.0f, 2100.0f, 2100.0f).empty());
	const std::vector<int> vBottom = NodesInRect(Graph, -50.0f, C.m_Y - 5.0f, C.m_X + 10.0f, C.m_Y + NODE_HEIGHT + 5.0f);
	ASSERT_EQ(vBottom.size(), 1u);
	EXPECT_EQ(vBottom[0], CId);
}

TEST(QmBindWorkshop, GraphBoundsCoverEveryNodeForTheMinimap)
{
	CBlueprintGraph Empty;
	EXPECT_FALSE(GraphBounds(Empty).m_HasNodes);

	CBlueprintGraph Graph;
	SNode First;
	First.m_X = 40.0f;
	First.m_Y = 80.0f;
	Graph.AddNode(First);
	SNode Second;
	Second.m_X = -120.0f;
	Second.m_Y = 300.0f;
	Graph.AddNode(Second);
	const SCanvasBounds Bounds = GraphBounds(Graph);
	ASSERT_TRUE(Bounds.m_HasNodes);
	EXPECT_NEAR(Bounds.m_MinX, -120.0f, 0.01f);
	EXPECT_NEAR(Bounds.m_MinY, 80.0f, 0.01f);
	EXPECT_NEAR(Bounds.m_MaxX, 40.0f + NODE_WIDTH, 0.01f);
	EXPECT_NEAR(Bounds.m_MaxY, 300.0f + NODE_HEIGHT, 0.01f);
	EXPECT_NEAR(Bounds.Width(), Bounds.m_MaxX - Bounds.m_MinX, 0.01f);
}

TEST(QmBindWorkshop, HistoryUndoRestoresTheSnapshotAndRedoBringsItBack)
{
	CBlueprintGraph Graph;
	SNode First;
	First.m_Command = "+jump";
	const int FirstId = Graph.AddNode(First);
	CBlueprintHistory History;
	EXPECT_FALSE(History.CanUndo());
	EXPECT_FALSE(History.CanRedo());

	// 改动前记一笔，再真的改：撤销回到记录点。
	History.Record(0, Graph);
	SNode Second;
	Second.m_X = 300.0f;
	Second.m_Command = "+fire";
	Graph.AddNode(Second);
	ASSERT_EQ(Graph.Nodes().size(), 2u);
	EXPECT_EQ(History.UndoDepth(), 1u);

	size_t Flow = 99;
	CBlueprintGraph Restored;
	ASSERT_TRUE(History.PopUndo(Flow, Restored));
	EXPECT_EQ(Flow, 0u);
	EXPECT_EQ(Restored.Nodes().size(), 1u) << "undo must return the graph as it was before the edit";
	EXPECT_EQ(Restored.Nodes().front().m_Command, "+jump");
	EXPECT_NE(Restored.FindNode(FirstId), nullptr) << "snapshot must keep node IDs stable";
	EXPECT_FALSE(History.CanUndo());

	// 撤销时的现场进重做栈，重做再拿回来。
	History.PushRedo(0, Graph.Clone());
	ASSERT_TRUE(History.CanRedo());
	size_t RedoFlow = 0;
	CBlueprintGraph Redone;
	ASSERT_TRUE(History.PopRedo(RedoFlow, Redone));
	EXPECT_EQ(Redone.Nodes().size(), 2u);
	EXPECT_FALSE(History.CanRedo());
}

TEST(QmBindWorkshop, NewEditDropsTheRedoBranchAndDepthIsBounded)
{
	CBlueprintGraph Graph;
	CBlueprintHistory History;
	History.Record(0, Graph);
	History.Record(0, Graph);
	ASSERT_EQ(History.UndoDepth(), 2u);
	size_t Flow = 0;
	CBlueprintGraph Snapshot;
	ASSERT_TRUE(History.PopUndo(Flow, Snapshot));
	History.PushRedo(Flow, std::move(Snapshot));
	ASSERT_TRUE(History.CanRedo());
	// 下一次编辑把「未来」清掉：不能重做到一条已不存在的分支。
	History.Record(0, Graph);
	EXPECT_FALSE(History.CanRedo());
	EXPECT_EQ(History.UndoDepth(), 2u);

	// 深度上限：连续记录不会无限涨，最老的一条被挤掉。
	for(size_t Step = 0; Step < CBlueprintHistory::MAX_DEPTH + 5; ++Step)
		History.Record(0, Graph);
	EXPECT_EQ(History.UndoDepth(), CBlueprintHistory::MAX_DEPTH);
}

TEST(QmBindWorkshop, GroupFrameOwnsTheNodesCenteredInsideItAndStaysOutOfTheOutput)
{
	CBlueprintGraph Graph;
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Inside;
	Inside.m_X = 40.0f;
	Inside.m_Y = 40.0f;
	Inside.m_Command = "+jump";
	const int InsideId = Graph.AddNode(Inside);
	SNode Outside;
	Outside.m_X = 900.0f;
	Outside.m_Y = 900.0f;
	Outside.m_Command = "+fire";
	const int OutsideId = Graph.AddNode(Outside);
	ASSERT_TRUE(Graph.Connect(EventId, InsideId));
	ASSERT_TRUE(Graph.Connect(EventId, OutsideId));

	SNode Group;
	Group.m_Type = ENodeType::GROUP;
	Group.m_X = 0.0f;
	Group.m_Y = 0.0f;
	Group.m_W = GROUP_WIDTH;
	Group.m_H = GROUP_HEIGHT;
	const int GroupId = Graph.AddNode(Group);

	const std::vector<int> vMembers = NodesContainedIn(Graph, GroupId);
	ASSERT_EQ(vMembers.size(), 2u) << "the event and the inner command are inside the frame";
	EXPECT_EQ(vMembers[0], EventId);
	EXPECT_EQ(vMembers[1], InsideId);
	EXPECT_EQ(std::find(vMembers.begin(), vMembers.end(), OutsideId), vMembers.end());

	// 分组框没有端口，只当容器：命中的是节点体。
	const SCanvasHit Hit = HitTestCanvas(Graph, Group.m_X + Group.m_W * 0.5f, Group.m_Y + Group.m_H * 0.5f);
	EXPECT_EQ(Hit.m_NodeId, GroupId);
	EXPECT_TRUE(Hit.m_Part == ECanvasPart::BODY);

	// 分组框不产出命令：编译结果只来自事件 -> 命令这条链。
	const SCompileResult Compile = Graph.Compile("h");
	EXPECT_TRUE(Compile.Ok());
	EXPECT_EQ(Compile.m_Command.find("Group"), std::string::npos);
}

TEST(QmBindWorkshop, OverlappingGroupFramesGiveEachNodeASingleOwner)
{
	CBlueprintGraph Graph;
	// 两个框都罩住这个节点：归属必须唯一，否则拖任一个框它都会被移动两次。
	SNode Node;
	Node.m_X = 60.0f;
	Node.m_Y = 60.0f;
	Node.m_Command = "+jump";
	const int NodeId = Graph.AddNode(Node);

	SNode Small;
	Small.m_Type = ENodeType::GROUP;
	Small.m_X = 0.0f;
	Small.m_Y = 0.0f;
	Small.m_W = GROUP_WIDTH;
	Small.m_H = GROUP_HEIGHT;
	const int SmallId = Graph.AddNode(Small);

	SNode Large;
	Large.m_Type = ENodeType::GROUP;
	Large.m_X = -60.0f;
	Large.m_Y = -60.0f;
	Large.m_W = GROUP_WIDTH * 3.0f;
	Large.m_H = GROUP_HEIGHT * 3.0f;
	const int LargeId = Graph.AddNode(Large);

	EXPECT_EQ(GroupOwningNode(Graph, NodeId), SmallId) << "the tighter frame is the specific one";
	EXPECT_EQ(GroupOwningNode(Graph, LargeId), 0) << "a group frame is never owned by another frame";
	const std::vector<int> vSmall = NodesContainedIn(Graph, SmallId);
	const std::vector<int> vLarge = NodesContainedIn(Graph, LargeId);
	ASSERT_EQ(vSmall.size(), 1u);
	EXPECT_EQ(vSmall.front(), NodeId);
	EXPECT_TRUE(vLarge.empty()) << "the outer frame must not claim a node the inner one owns";
}

TEST(QmBindWorkshop, GroupOwnershipBreaksEqualAreaTiesByNodeId)
{
	CBlueprintGraph Graph;
	SNode Node;
	Node.m_Command = "+jump";
	Node.m_X = 100.0f;
	Node.m_Y = 100.0f;
	const int NodeId = Graph.AddNode(Node);

	SNode First;
	First.m_Type = ENodeType::GROUP;
	First.m_X = 0.0f;
	First.m_Y = 0.0f;
	First.m_W = GROUP_WIDTH;
	First.m_H = GROUP_HEIGHT;
	const int FirstId = Graph.AddNode(First);

	// 同面积、只平移：两个框都罩住节点中心，归属按 Id 小的定，不随遍历顺序变。
	SNode Second = First;
	Second.m_X = First.m_X + 40.0f;
	const int SecondId = Graph.AddNode(Second);
	ASSERT_LT(FirstId, SecondId);

	EXPECT_EQ(GroupOwningNode(Graph, NodeId), FirstId);
	EXPECT_TRUE(NodesContainedIn(Graph, SecondId).empty());
	EXPECT_EQ(NodesContainedIn(Graph, FirstId).size(), 1u);
}

TEST(QmBindWorkshop, HitTestFollowsThePerNodeSizeOfAGroupFrame)
{
	CBlueprintGraph Graph;
	SNode Group;
	Group.m_Type = ENodeType::GROUP;
	Group.m_X = 0.0f;
	Group.m_Y = 0.0f;
	Group.m_W = GROUP_WIDTH;
	Group.m_H = GROUP_HEIGHT;
	const int GroupId = Graph.AddNode(Group);
	SNode Normal;
	Normal.m_X = 0.0f;
	Normal.m_Y = 400.0f;
	const int NormalId = Graph.AddNode(Normal);

	// 远在普通节点尺寸之外、但仍在分组框内的点要命中分组框。
	const SCanvasHit Wide = HitTestCanvas(Graph, GROUP_WIDTH - 4.0f, GROUP_HEIGHT - 4.0f);
	EXPECT_EQ(Wide.m_NodeId, GroupId);
	EXPECT_TRUE(Wide.m_Part == ECanvasPart::BODY);

	// 普通节点尺寸没变：同样的相对位置（超出 NODE_HEIGHT 一半）不该命中端口。
	const SCanvasHit Tall = HitTestCanvas(Graph, Normal.m_X + NODE_WIDTH * 0.5f, Normal.m_Y + NODE_HEIGHT * 2.0f);
	EXPECT_TRUE(Tall.Empty());
}

TEST(QmBindWorkshop, GroupResizeClampsToTheMinimumInsteadOfCollapsing)
{
	CBlueprintGraph Graph;
	SNode Group;
	Group.m_Type = ENodeType::GROUP;
	Group.m_W = GROUP_WIDTH;
	Group.m_H = GROUP_HEIGHT;
	const int GroupId = Graph.AddNode(Group);

	ASSERT_TRUE(Graph.SetNodeSize(GroupId, 520.0f, 300.0f));
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_W, 520.0f);
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_H, 300.0f);

	// 拖到反向或拖回原点：夹到下限，不会被拖成 0 或负数。
	ASSERT_TRUE(Graph.SetNodeSize(GroupId, -80.0f, 4.0f));
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_W, GROUP_MIN_WIDTH);
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_H, GROUP_MIN_HEIGHT);

	// 普通节点的下限是它自己的尺寸，不能缩成比默认更小。
	SNode Normal;
	Normal.m_X = 400.0f;
	const int NormalId = Graph.AddNode(Normal);
	ASSERT_TRUE(Graph.SetNodeSize(NormalId, 10.0f, 10.0f));
	EXPECT_FLOAT_EQ(Graph.FindNode(NormalId)->m_W, NODE_WIDTH);
	EXPECT_FLOAT_EQ(Graph.FindNode(NormalId)->m_H, NODE_HEIGHT);
	EXPECT_FALSE(Graph.SetNodeSize(9999, 200.0f, 200.0f));
}

TEST(QmBindWorkshop, AlignmentSnapsNearbyEdgesAndIgnoresDistantOnes)
{
	CBlueprintGraph Graph;
	SNode Anchor;
	Anchor.m_X = 200.0f;
	Anchor.m_Y = 100.0f;
	const int AnchorId = Graph.AddNode(Anchor);
	SNode Mover;
	Mover.m_X = 20.0f;
	Mover.m_Y = 400.0f;
	const int MoverId = Graph.AddNode(Mover);

	// 左边缘差 3px：吸附到锚点左边缘，并给出竖直参考线。
	const SNodeAlignment Near = ResolveNodeAlignment(Graph, MoverId, 203.0f, 400.0f);
	EXPECT_TRUE(Near.m_SnappedX);
	EXPECT_FLOAT_EQ(Near.m_X, Anchor.m_X);
	EXPECT_FLOAT_EQ(Near.m_GuideX, Anchor.m_X);
	EXPECT_FALSE(Near.m_SnappedY) << "Y is far from the anchor, nothing to snap";

	// 中枢对中枢：y 也吸附到锚点中线。
	const float AnchorCenterY = Anchor.m_Y + NODE_HEIGHT * 0.5f;
	const SNodeAlignment Center = ResolveNodeAlignment(Graph, MoverId, 60.0f, AnchorCenterY - NODE_HEIGHT * 0.5f + 4.0f);
	EXPECT_TRUE(Center.m_SnappedY);
	EXPECT_FLOAT_EQ(Center.m_Y, Anchor.m_Y);

	// 差得远就不动：坐标原样返回，不产生参考线。
	const SNodeAlignment Far = ResolveNodeAlignment(Graph, MoverId, 2000.0f, 2000.0f);
	EXPECT_FALSE(Far.m_SnappedX);
	EXPECT_FALSE(Far.m_SnappedY);
	EXPECT_FLOAT_EQ(Far.m_X, 2000.0f);
	EXPECT_FLOAT_EQ(Far.m_Y, 2000.0f);
	EXPECT_EQ(ResolveNodeAlignment(Graph, 9999, 0.0f, 0.0f).m_X, 0.0f);
	(void)AnchorId;
}

// ---- 属性面板的几何字段：夹紧、步进、多选包围盒（纯逻辑，UI 只调用） ----
TEST(QmBindWorkshop, NodeGeometryClampsPositionAndSizeToTheEditableRange)
{
	// 位置夹到 ±上限，尺寸不低于该类型的最小宽高。
	const SNodeGeometry Huge = ClampNodeGeometry(ENodeType::EVENT_PRESS, SNodeGeometry{999999.0f, -999999.0f, 1.0f, 1.0f});
	EXPECT_FLOAT_EQ(Huge.m_X, NODE_GEOMETRY_LIMIT);
	EXPECT_FLOAT_EQ(Huge.m_Y, -NODE_GEOMETRY_LIMIT);
	EXPECT_FLOAT_EQ(Huge.m_W, NODE_WIDTH);
	EXPECT_FLOAT_EQ(Huge.m_H, NODE_HEIGHT);

	// 分组框用自己的下限，上限同样生效。
	const SNodeGeometry Group = ClampNodeGeometry(ENodeType::GROUP, SNodeGeometry{200.0f, 120.0f, -400.0f, 3.0f});
	EXPECT_FLOAT_EQ(Group.m_W, GROUP_MIN_WIDTH);
	EXPECT_FLOAT_EQ(Group.m_H, GROUP_MIN_HEIGHT);
	const SNodeGeometry GroupGiant = ClampNodeGeometry(ENodeType::GROUP,
		SNodeGeometry{0.0f, 0.0f, NODE_GEOMETRY_LIMIT * 4.0f, NODE_GEOMETRY_LIMIT * 4.0f});
	EXPECT_FLOAT_EQ(GroupGiant.m_W, NODE_GEOMETRY_LIMIT);
	EXPECT_FLOAT_EQ(GroupGiant.m_H, NODE_GEOMETRY_LIMIT);

	// 读节点几何原样返回；只有面板显示会四舍五入。
	SNode Node;
	Node.m_X = 37.4f;
	Node.m_Y = -12.6f;
	const SNodeGeometry Read = ReadNodeGeometry(Node);
	EXPECT_FLOAT_EQ(Read.m_X, 37.4f);
	EXPECT_FLOAT_EQ(Read.m_Y, -12.6f);
	EXPECT_EQ(NodeGeometryDisplayValue(Read, ENodeGeometryField::X), 37);
	EXPECT_EQ(NodeGeometryDisplayValue(Read, ENodeGeometryField::Y), -13);
}

TEST(QmBindWorkshop, NodeGeometryStepSnapsToTheGridAndStopsAtTheBounds)
{
	// 非整格的值先归到最近的刻度，再加一格。
	const SNodeGeometry Snapped = StepNodeGeometry(ENodeType::EVENT_PRESS,
		SNodeGeometry{103.0f, 101.0f, NODE_WIDTH, NODE_HEIGHT}, ENodeGeometryField::X, 1);
	EXPECT_FLOAT_EQ(Snapped.m_X, 104.0f + NODE_GEOMETRY_STEP);

	// 连按多格：按步长整数倍走，方向为负就往回退。
	const SNodeGeometry Back = StepNodeGeometry(ENodeType::EVENT_PRESS,
		SNodeGeometry{96.0f, 96.0f, NODE_WIDTH, NODE_HEIGHT}, ENodeGeometryField::X, -1, 2);
	EXPECT_FLOAT_EQ(Back.m_X, 96.0f - 2.0f * NODE_GEOMETRY_STEP);

	// 撞到上限就停在边界上，不会越过去。
	const SNodeGeometry AtLimit = StepNodeGeometry(ENodeType::GROUP,
		SNodeGeometry{NODE_GEOMETRY_LIMIT - 3.0f, 0.0f, GROUP_MIN_WIDTH, GROUP_MIN_HEIGHT}, ENodeGeometryField::X, 1, 4);
	EXPECT_FLOAT_EQ(AtLimit.m_X, NODE_GEOMETRY_LIMIT);

	// 宽高往下按到下限就停：分组框不会被步进成比最小尺寸还小。
	const SNodeGeometry Shrunk = StepNodeGeometry(ENodeType::GROUP,
		SNodeGeometry{0.0f, 0.0f, GROUP_MIN_WIDTH, GROUP_MIN_HEIGHT}, ENodeGeometryField::WIDTH, -1, 3);
	EXPECT_FLOAT_EQ(Shrunk.m_W, GROUP_MIN_WIDTH);

	// Direction 为 0 只做对齐，不动值。
	const SNodeGeometry Still = StepNodeGeometry(ENodeType::EVENT_PRESS,
		SNodeGeometry{0.0f, 101.0f, NODE_WIDTH, NODE_HEIGHT}, ENodeGeometryField::Y, 0);
	EXPECT_FLOAT_EQ(Still.m_Y, 104.0f);
}

TEST(QmBindWorkshop, SelectionBoundsCoversEverySelectedNodeAndIgnoresStaleIds)
{
	CBlueprintGraph Graph;
	SNode Normal;
	Normal.m_X = 100.0f;
	Normal.m_Y = 60.0f;
	const int NormalId = Graph.AddNode(Normal);
	SNode Group;
	Group.m_Type = ENodeType::GROUP;
	Group.m_X = 400.0f;
	Group.m_Y = 10.0f;
	Group.m_W = GROUP_WIDTH;
	Group.m_H = GROUP_HEIGHT;
	const int GroupId = Graph.AddNode(Group);

	// 多选：包围盒取并集，图里已经不存在的 id 直接忽略。
	const SCanvasBounds Bounds = SelectionBounds(Graph, {NormalId, GroupId, 9999});
	ASSERT_TRUE(Bounds.m_HasNodes);
	EXPECT_FLOAT_EQ(Bounds.m_MinX, 100.0f);
	EXPECT_FLOAT_EQ(Bounds.m_MinY, 10.0f);
	EXPECT_FLOAT_EQ(Bounds.m_MaxX, 400.0f + GROUP_WIDTH);
	EXPECT_FLOAT_EQ(Bounds.m_MaxY, 10.0f + GROUP_HEIGHT);

	// 空集合 / 全都失效：没有包围盒，UI 据此不画摘要数字。
	EXPECT_FALSE(SelectionBounds(Graph, {}).m_HasNodes);
	EXPECT_FALSE(SelectionBounds(Graph, {9999}).m_HasNodes);

	// 单节点：包围盒就是它自己。
	const SCanvasBounds Single = SelectionBounds(Graph, {NormalId});
	EXPECT_FLOAT_EQ(Single.Width(), NODE_WIDTH);
	EXPECT_FLOAT_EQ(Single.Height(), NODE_HEIGHT);

	// 写回图：位置所有节点都生效，宽高只对分组框生效。
	ASSERT_TRUE(ApplyNodeGeometry(Graph, NormalId, SNodeGeometry{200.0f, 300.0f, 10.0f, 10.0f}));
	EXPECT_FLOAT_EQ(Graph.FindNode(NormalId)->m_X, 200.0f);
	EXPECT_FLOAT_EQ(Graph.FindNode(NormalId)->m_Y, 300.0f);
	EXPECT_FLOAT_EQ(Graph.FindNode(NormalId)->m_W, NODE_WIDTH);
	ASSERT_TRUE(ApplyNodeGeometry(Graph, GroupId, SNodeGeometry{1.0f, 2.0f, 640.0f, 480.0f}));
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_X, 1.0f);
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_W, 640.0f);
	EXPECT_FLOAT_EQ(Graph.FindNode(GroupId)->m_H, 480.0f);
	EXPECT_FALSE(ApplyNodeGeometry(Graph, 9999, SNodeGeometry{}));
}
