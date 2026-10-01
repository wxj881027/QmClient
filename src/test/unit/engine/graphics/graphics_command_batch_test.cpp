// 图形命令合并的生产接口行为回归测试。
#include <engine/client/graphics_threaded.h>

#include <gtest/gtest.h>

namespace
{
	class CGraphicsCommandBatchTest : public ::testing::Test
	{
	protected:
		CCommandBuffer m_Buffer{8192, CCommandBuffer::MAX_VERTICES * sizeof(CCommandBuffer::SVertex) + 1024};

		CCommandBuffer::SCommand_Render MakeCommand(unsigned PrimCount = 1, EPrimitiveType PrimType = EPrimitiveType::QUADS)
		{
			CCommandBuffer::SCommand_Render Command;
			Command.m_State = {};
			Command.m_State.m_BlendMode = EBlendMode::ALPHA;
			Command.m_State.m_Texture = -1;
			Command.m_State.m_ScreenBR = {1920.0f, 1080.0f};
			Command.m_PrimType = PrimType;
			Command.m_PrimCount = PrimCount;
			const unsigned VerticesPerPrimitive = PrimType == EPrimitiveType::QUADS ? 4 : (PrimType == EPrimitiveType::TRIANGLES ? 3 : 2);
			Command.m_pVertices = static_cast<CCommandBuffer::SVertex *>(m_Buffer.AllocData(PrimCount * VerticesPerPrimitive * sizeof(CCommandBuffer::SVertex)));
			return Command;
		}

		void AddInitial(const CCommandBuffer::SCommand_Render &Command)
		{
			ASSERT_NE(Command.m_pVertices, nullptr);
			ASSERT_TRUE(m_Buffer.AddCommandUnsafe(Command));
			m_Buffer.AddRenderCalls(1);
		}
	};
}

TEST_F(CGraphicsCommandBatchTest, AdjacentBatchesPreserveVerticesAndDrawOrder)
{
	const auto First = MakeCommand();
	AddInitial(First);
	const auto Second = MakeCommand();
	ASSERT_NE(Second.m_pVertices, nullptr);
	for(unsigned i = 0; i < 4; ++i)
	{
		First.m_pVertices[i].m_Pos = {float(i), 1.0f};
		First.m_pVertices[i].m_Color = {255, 0, 0, 128};
		Second.m_pVertices[i].m_Pos = {float(i), 2.0f};
		Second.m_pVertices[i].m_Color = {0, 255, 0, 128};
	}
	ASSERT_TRUE(m_Buffer.TryMergeRenderCommand(Second));
	const auto *pMerged = static_cast<const CCommandBuffer::SCommand_Render *>(m_Buffer.Head());
	EXPECT_EQ(pMerged->m_PrimCount, 2u);
	EXPECT_EQ(pMerged->m_pVertices, First.m_pVertices);
	EXPECT_EQ(pMerged->m_pNext, nullptr);
	EXPECT_EQ(m_Buffer.m_CommandCount, 1u);
	EXPECT_EQ(m_Buffer.m_RenderCallCount, 1u);
	for(unsigned i = 0; i < 8; ++i)
	{
		EXPECT_FLOAT_EQ(pMerged->m_pVertices[i].m_Pos.y, i < 4 ? 1.0f : 2.0f);
		EXPECT_EQ(pMerged->m_pVertices[i].m_Color.r, i < 4 ? 255 : 0);
		EXPECT_EQ(pMerged->m_pVertices[i].m_Color.a, 128);
	}
}

TEST_F(CGraphicsCommandBatchTest, EveryRenderStateDifferenceSeparatesBatches)
{
	const auto First = MakeCommand();
	AddInitial(First);
	const auto Second = MakeCommand();
	const auto RejectChange = [&](const auto &Change) {
		auto Changed = Second;
		Change(Changed.m_State);
		EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(Changed));
	};
	RejectChange([](auto &State) { State.m_Texture = 5; });
	RejectChange([](auto &State) { State.m_BlendMode = EBlendMode::ADDITIVE; });
	RejectChange([](auto &State) { State.m_WrapMode = EWrapMode::CLAMP; });
	RejectChange([](auto &State) { State.m_ScreenTL.x = 1.0f; });
	RejectChange([](auto &State) { State.m_ScreenTL.y = 1.0f; });
	RejectChange([](auto &State) { State.m_ScreenBR.x = 1.0f; });
	RejectChange([](auto &State) { State.m_ScreenBR.y = 1.0f; });
	RejectChange([](auto &State) { State.m_ClipEnable = true; });
	RejectChange([](auto &State) { State.m_ClipX = 1; });
	RejectChange([](auto &State) { State.m_ClipY = 1; });
	RejectChange([](auto &State) { State.m_ClipW = 1; });
	RejectChange([](auto &State) { State.m_ClipH = 1; });
	EXPECT_TRUE(m_Buffer.TryMergeRenderCommand(Second));
}

TEST_F(CGraphicsCommandBatchTest, ResourceReadbackAndPassCommandsSeparateBatches)
{
	for(const unsigned CommandId : {CCommandBuffer::CMD_TEXTURE_UPDATE, CCommandBuffer::CMD_RENDER_TARGET_BEGIN, CCommandBuffer::CMD_RENDER_TARGET_READBACK, CCommandBuffer::CMD_CLEAR, CCommandBuffer::CMD_SWAP, CCommandBuffer::CMD_SIGNAL})
	{
		m_Buffer.Reset();
		AddInitial(MakeCommand());
		ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CommandId)));
		EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
		EXPECT_EQ(m_Buffer.m_CommandCount, 2u);
	}
}

TEST_F(CGraphicsCommandBatchTest, DataGapsAndDifferentPrimitivesSeparateBatches)
{
	AddInitial(MakeCommand());
	ASSERT_NE(m_Buffer.AllocData(16), nullptr);
	EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
	m_Buffer.Reset();
	AddInitial(MakeCommand());
	EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(MakeCommand(2, EPrimitiveType::TRIANGLES)));
}

TEST_F(CGraphicsCommandBatchTest, TriangleAndLineBatchesRespectVertexLayout)
{
	for(const auto Primitive : {EPrimitiveType::TRIANGLES, EPrimitiveType::LINES})
	{
		m_Buffer.Reset();
		// 四个图元使下一次分配无需填充对齐字节。
		AddInitial(MakeCommand(4, Primitive));
		EXPECT_TRUE(m_Buffer.TryMergeRenderCommand(MakeCommand(4, Primitive)));
		EXPECT_EQ(static_cast<const CCommandBuffer::SCommand_Render *>(m_Buffer.Head())->m_PrimCount, 8u);
	}
}

TEST_F(CGraphicsCommandBatchTest, VertexLimitAllowsExactFitAndRejectsOverflow)
{
	AddInitial(MakeCommand(CCommandBuffer::MAX_VERTICES / 4 - 1));
	ASSERT_TRUE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
	EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
	EXPECT_EQ(static_cast<const CCommandBuffer::SCommand_Render *>(m_Buffer.Head())->m_PrimCount, unsigned(CCommandBuffer::MAX_VERTICES / 4));
}

TEST_F(CGraphicsCommandBatchTest, ResetDoesNotReuseSubmittedTail)
{
	AddInitial(MakeCommand());
	m_Buffer.Reset();
	EXPECT_FALSE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
	EXPECT_EQ(m_Buffer.Head(), nullptr);
	EXPECT_EQ(m_Buffer.m_CommandCount, 0u);
}

TEST_F(CGraphicsCommandBatchTest, MergeNeedsNoAdditionalCommandStorage)
{
	AddInitial(MakeCommand());
	const unsigned Remaining = m_Buffer.m_CmdBuffer.DataSize() - m_Buffer.m_CmdBuffer.DataUsed();
	ASSERT_NE(m_Buffer.m_CmdBuffer.Alloc(Remaining, 1), nullptr);
	EXPECT_TRUE(m_Buffer.TryMergeRenderCommand(MakeCommand()));
	EXPECT_EQ(m_Buffer.m_CommandCount, 1u);
}
