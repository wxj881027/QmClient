#include <engine/client/graphics_threaded.h>
#include <engine/client/quad_container_render.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
	struct SQuad
	{
		CCommandBuffer::SVertex m_aVertices[4]{};
	};
	struct SArena
	{
		CCommandBuffer::SVertex m_aVertices[CCommandBuffer::MAX_VERTICES];
	};
	class CQuadContainerCommands : public ::testing::Test
	{
	protected:
		CCommandBuffer m_Commands{4096, 2 * CCommandBuffer::MAX_VERTICES * sizeof(CCommandBuffer::SVertex)};
		std::unique_ptr<SArena> m_pArena = std::make_unique<SArena>();
		std::vector<SQuad> MakeQuads(size_t Count)
		{
			std::vector<SQuad> vQuads(Count);
			for(size_t i = 0; i < Count; ++i)
				for(auto &Vertex : vQuads[i].m_aVertices)
				{
					Vertex.m_Pos = {static_cast<float>(i), 7.0f};
					Vertex.m_Color = {1, 2, 3, 4};
				}
			return vQuads;
		}
		// 生产流输出写入真实命令数据区；不启动原生前端、后端或GPU。
		void Store(int Count, EPrimitiveType Type, int VerticesPerPrimitive)
		{
			CCommandBuffer::SCommand_Render Command;
			Command.m_State = {};
			Command.m_PrimType = Type;
			Command.m_PrimCount = Count / VerticesPerPrimitive;
			Command.m_pVertices = static_cast<CCommandBuffer::SVertex *>(m_Commands.AllocData(Count * sizeof(CCommandBuffer::SVertex)));
			ASSERT_NE(Command.m_pVertices, nullptr);
			std::copy_n(m_pArena->m_aVertices, Count, Command.m_pVertices);
			ASSERT_TRUE(m_Commands.AddCommandUnsafe(Command));
		}
	};
}

TEST_F(CQuadContainerCommands, MoreThanOneQuadBatchRetainsTheCompleteCommandData)
{
	const auto vQuads = MakeQuads(9000);
	ASSERT_TRUE(StreamGraphicsQuads(vQuads, 0, -1, false, m_pArena->m_aVertices, [](auto *, int) {}, [&](int Count) { Store(Count, EPrimitiveType::QUADS, 4); }));
	ASSERT_EQ(m_Commands.m_CommandCount, 2u);
	const auto *pFirst = static_cast<const CCommandBuffer::SCommand_Render *>(m_Commands.Head());
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_PrimCount, 8192u);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[0].m_Pos.x, 0.0f);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[32767].m_Pos.x, 8191.0f);
	const auto *pSecond = static_cast<const CCommandBuffer::SCommand_Render *>(pFirst->m_pNext);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_EQ(pSecond->m_PrimCount, 808u);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[0].m_Pos.x, 8192.0f);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[3231].m_Pos.x, 8999.0f);
	EXPECT_EQ(pSecond->m_pVertices[3231].m_Color.a, 4);
	EXPECT_EQ(pSecond->m_pNext, nullptr);
}

TEST_F(CQuadContainerCommands, TriangleSubrangeSplitsOnCompleteQuadsAndKeepsTheTail)
{
	const auto vQuads = MakeQuads(6002);
	ASSERT_TRUE(StreamGraphicsQuads(vQuads, 2, -1, true, m_pArena->m_aVertices, [](auto *, int) {}, [&](int Count) { Store(Count, EPrimitiveType::TRIANGLES, 3); }));
	ASSERT_EQ(m_Commands.m_CommandCount, 2u);
	const auto *pFirst = static_cast<const CCommandBuffer::SCommand_Render *>(m_Commands.Head());
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_PrimCount, 10922u);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[0].m_Pos.x, 2.0f);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[32765].m_Pos.x, 5462.0f);
	const auto *pSecond = static_cast<const CCommandBuffer::SCommand_Render *>(pFirst->m_pNext);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_EQ(pSecond->m_PrimCount, 1078u);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[0].m_Pos.x, 5463.0f);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[3233].m_Pos.x, 6001.0f);
	EXPECT_EQ(pSecond->m_pNext, nullptr);
}
