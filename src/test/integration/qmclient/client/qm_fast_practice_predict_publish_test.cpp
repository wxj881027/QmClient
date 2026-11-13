#include <generated/protocol.h>

#include <game/client/components/qmclient/practice_predict_publish.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>

// 预测状态写回的运行时行为。生产代码把 CGameClient 字段转发给模板，
// 测试用同形状的记录槽位接收写回，世界与角色仍是生产类型。
// 覆盖：快照入口、追赶循环的上一帧/当前帧分界、外推帧不写他人核心、
//       200 tick 位置环索引。

namespace
{
	// 与 CGameClient 写回槽位一一对应的记录结构。
	class CTestPracticePredictSink
	{
	public:
		CGameWorld m_PredictedWorld;
		CGameWorld m_PrevPredictedWorld;
		CCharacterCore m_PredictedChar;
		CCharacterCore m_PredictedPrevChar;
		CCharacterCore m_aPredicted[MAX_CLIENTS];
		CCharacterCore m_aPrevPredicted[MAX_CLIENTS];
		vec2 m_aPredPos[MAX_CLIENTS][200] = {};
		int m_aPredTick[MAX_CLIENTS][200] = {};
		int m_PredictedTick = 0;

		CGameWorld &PredictedWorld() { return m_PredictedWorld; }
		CGameWorld &PrevPredictedWorld() { return m_PrevPredictedWorld; }
		CCharacterCore &PredictedChar() { return m_PredictedChar; }
		CCharacterCore &PredictedPrevChar() { return m_PredictedPrevChar; }
		CCharacterCore &ClientPredicted(int ClientId) { return m_aPredicted[ClientId]; }
		CCharacterCore &ClientPrevPredicted(int ClientId) { return m_aPrevPredicted[ClientId]; }
		vec2 &ClientPredPos(int ClientId, int Slot) { return m_aPredPos[ClientId][Slot]; }
		int &ClientPredTick(int ClientId, int Slot) { return m_aPredTick[ClientId][Slot]; }
	};

	// 只提供空碰撞与默认 tuning，足以构造快照角色；本测试不推进世界物理。
	class CPracticePredictPublishRuntime : public ::testing::Test
	{
	protected:
		CCollision m_Collision;
		CTuningParams m_aTuning[NUM_TUNEZONES];
		CGameWorld m_Source;
		CCharacter *m_pLocalChar = nullptr;
		CCharacter *m_pDummyChar = nullptr;

		CCharacter *AddCharacter(int ClientId, vec2 Pos)
		{
			CNetObj_Character Obj = {};
			Obj.m_X = round_to_int(Pos.x);
			Obj.m_Y = round_to_int(Pos.y);
			CCharacter *pChar = new CCharacter(&m_Source, ClientId, &Obj, nullptr);
			// 构造函数只建对象；注册到世界后 GetCharacterById 才能找到。
			m_Source.InsertEntity(pChar);
			return pChar;
		}

		void MoveCharacter(CCharacter *pChar, vec2 Pos)
		{
			CCharacterCore Core = pChar->GetCore();
			Core.m_Pos = Pos;
			pChar->SetCore(Core);
		}

		void SetUp() override
		{
			m_Source.Init(&m_Collision, m_aTuning, nullptr);
			m_Source.m_GameTick = 104;
			m_pLocalChar = AddCharacter(4, vec2(320.0f, 160.0f));
			m_pDummyChar = AddCharacter(7, vec2(640.0f, 320.0f));
		}
	};
}

TEST_F(CPracticePredictPublishRuntime, SnapshotPublishesWorldAndBothCores)
{
	CTestPracticePredictSink Sink;
	QmPublishPracticeWorldSnapshot(Sink, m_Source, 4, 7, Sink.m_PredictedTick);

	EXPECT_EQ(Sink.m_PredictedTick, 104);
	EXPECT_EQ(Sink.m_PredictedWorld.GameTick(), 104);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 104);
	ASSERT_NE(Sink.m_PredictedWorld.GetCharacterById(4), nullptr);
	ASSERT_NE(Sink.m_PredictedWorld.GetCharacterById(7), nullptr);
	EXPECT_FLOAT_EQ(Sink.m_PredictedChar.m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_PredictedPrevChar.m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[7].m_Pos.x, 640.0f);
}

TEST_F(CPracticePredictPublishRuntime, SnapshotSkipsTheDummyCoreWhenItIsNotAttached)
{
	CTestPracticePredictSink Sink;
	Sink.m_aPredicted[7].m_Pos = vec2(1.0f, 2.0f);
	Sink.m_aPrevPredicted[7].m_Pos = vec2(1.0f, 2.0f);

	QmPublishPracticeWorldSnapshot(Sink, m_Source, 4, -1, Sink.m_PredictedTick);

	// 世界整体复制仍包含分身实体，但分身核心不写回。
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[7].m_Pos.x, 1.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPrevPredicted[7].m_Pos.x, 1.0f);
}

TEST_F(CPracticePredictPublishRuntime, CatchUpPublishesPreviousOnlyAtTheFinalTick)
{
	CTestPracticePredictSink Sink;
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	// 中间 tick 不写上一帧世界。
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 101, false, 4);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 0);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GetCharacterById(4), nullptr);

	// 末帧写入步进前世界，本体与分身的上一帧核心同样收口。
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 104, false, 4);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 104);
	ASSERT_NE(Sink.m_PrevPredictedWorld.GetCharacterById(4), nullptr);
	EXPECT_FLOAT_EQ(Sink.m_PredictedPrevChar.m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPrevPredicted[4].m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPrevPredicted[7].m_Pos.x, 640.0f);
}

TEST_F(CPracticePredictPublishRuntime, StepPublishesPreviousAndCurrentCoresSeparately)
{
	CTestPracticePredictSink Sink;
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 104, false, 4);
	// 模拟这一步移动了本体与分身。
	MoveCharacter(m_pLocalChar, vec2(400.0f, 200.0f));
	MoveCharacter(m_pDummyChar, vec2(700.0f, 300.0f));
	QmPublishPracticePredictionAfterStep(Sink, m_Source, Plan, 104, 4);

	// 上一帧保持步进前位置，当前帧写入步进后位置。
	EXPECT_FLOAT_EQ(Sink.m_PredictedPrevChar.m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPrevPredicted[4].m_Pos.x, 320.0f);
	EXPECT_FLOAT_EQ(Sink.m_PredictedChar.m_Pos.x, 400.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, 400.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[7].m_Pos.x, 700.0f);
}

TEST_F(CPracticePredictPublishRuntime, VisualExtrapolationStopsOtherCoresAtTheRealFinalTick)
{
	CTestPracticePredictSink Sink;
	// 主循环先跑到真实末帧 104，发布本体与分身。
	const SPracticePredictTickPlan MainPlan = QmBuildPracticePredictTickPlan(100, 104, 0, true);
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, MainPlan, 104, false, 4);
	MoveCharacter(m_pLocalChar, vec2(400.0f, 200.0f));
	MoveCharacter(m_pDummyChar, vec2(700.0f, 300.0f));
	QmPublishPracticePredictionAfterStep(Sink, m_Source, MainPlan, 104, 4);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, 400.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[7].m_Pos.x, 700.0f);

	// 视觉循环接管后关闭他人外推：外推帧只更新本体。
	const SPracticePredictTickPlan VisualPlan = QmBuildPracticePredictTickPlan(104, 104, 6, false);
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, VisualPlan, 110, true, 4);
	MoveCharacter(m_pLocalChar, vec2(420.0f, 210.0f));
	MoveCharacter(m_pDummyChar, vec2(900.0f, 500.0f));
	QmPublishPracticePredictionAfterStep(Sink, m_Source, VisualPlan, 110, 4);

	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, 420.0f);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[7].m_Pos.x, 700.0f);
}

TEST_F(CPracticePredictPublishRuntime, RepeatedPublishAtTheFinalTickKeepsTheSameState)
{
	CTestPracticePredictSink Sink;
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 104, false, 4);
	MoveCharacter(m_pLocalChar, vec2(400.0f, 200.0f));
	QmPublishPracticePredictionAfterStep(Sink, m_Source, Plan, 104, 4);
	const CCharacterCore FirstPredicted = Sink.m_aPredicted[4];
	const int FirstTick = Sink.m_PredictedTick;

	// 重复调用是纯赋值：状态与预测 tick 都不累计、不抖动。
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 104, false, 4);
	QmPublishPracticePredictionAfterStep(Sink, m_Source, Plan, 104, 4);
	EXPECT_FLOAT_EQ(Sink.m_aPredicted[4].m_Pos.x, FirstPredicted.m_Pos.x);
	EXPECT_EQ(Sink.m_PredictedTick, FirstTick);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 104);
}

TEST_F(CPracticePredictPublishRuntime, CopyToPredictedOnlyHappensAtTheFinalTick)
{
	CTestPracticePredictSink Sink;
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	// 中间 tick 即使请求复制当前世界也不执行，视觉循环只在末帧接管预测世界。
	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 101, true, 4);
	EXPECT_EQ(Sink.m_PredictedWorld.GameTick(), 0);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 0);

	QmPublishPracticePredictionBeforeStep(Sink, m_Source, Plan, 104, true, 4);
	EXPECT_EQ(Sink.m_PredictedWorld.GameTick(), 104);
	EXPECT_EQ(Sink.m_PrevPredictedWorld.GameTick(), 104);
}

TEST_F(CPracticePredictPublishRuntime, PositionsRingUsesTheTickModuloSlot)
{
	CTestPracticePredictSink Sink;

	// 槽位 0 与末槽位 199 都按 tick % 200 写入。
	QmPublishPracticePredictedPositions(Sink, m_Source, 0);
	EXPECT_FLOAT_EQ(Sink.m_aPredPos[4][0].x, 320.0f);
	EXPECT_EQ(Sink.m_aPredTick[4][0], 0);

	QmPublishPracticePredictedPositions(Sink, m_Source, 199);
	EXPECT_FLOAT_EQ(Sink.m_aPredPos[4][199].x, 320.0f);
	EXPECT_EQ(Sink.m_aPredTick[4][199], 199);

	// 305 与 105 共用同一个环槽；记录必须被新 tick 覆盖。
	QmPublishPracticePredictedPositions(Sink, m_Source, 305);
	EXPECT_FLOAT_EQ(Sink.m_aPredPos[4][105].x, 320.0f);
	EXPECT_EQ(Sink.m_aPredTick[4][105], 305);
	EXPECT_FLOAT_EQ(Sink.m_aPredPos[7][105].x, 640.0f);
	EXPECT_EQ(Sink.m_aPredTick[7][105], 305);
	// 其他槽位不被写。
	EXPECT_EQ(Sink.m_aPredTick[4][1], 0);
}
