#include <engine/client/graphics_threaded.h>
#include <engine/client/graphics_vertex_budget.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace
{
	// 真实调度/计数接口连接真实命令数据区，设备边界同步消费暂存。
	template<typename TVertex, typename TCommand>
	class CVertexCommandStream
	{
	public:
		CCommandBuffer m_Commands{4096, 2 * GRAPHICS_VERTEX_CAPACITY * sizeof(TVertex)};
		std::unique_ptr<TVertex[]> m_pVertices = std::make_unique<TVertex[]>(GRAPHICS_VERTEX_CAPACITY);
		int m_Pending = 0;
		EPrimitiveType m_Type;
		int m_VerticesPerPrimitive;
		CVertexCommandStream(EPrimitiveType Type, int VerticesPerPrimitive) :
			m_Type(Type), m_VerticesPerPrimitive(VerticesPerPrimitive) {}
		void Flush()
		{
			if(m_Pending == 0)
				return;
			TCommand Command;
			Command.m_State = {};
			Command.m_PrimType = m_Type;
			Command.m_PrimCount = m_Pending / m_VerticesPerPrimitive;
			Command.m_pVertices = static_cast<TVertex *>(m_Commands.AllocData(m_Pending * sizeof(TVertex)));
			ASSERT_NE(Command.m_pVertices, nullptr);
			std::copy_n(m_pVertices.get(), m_Pending, Command.m_pVertices);
			ASSERT_TRUE(m_Commands.AddCommandUnsafe(Command));
			m_Pending = 0;
		}
		bool Append(const std::vector<TVertex> &vSource, int VerticesPerItem)
		{
			const auto Write = [&](size_t Offset, int Batch) {
				const int Count = Batch * VerticesPerItem;
				std::copy_n(vSource.data() + Offset * VerticesPerItem, Count, m_pVertices.get() + m_Pending);
				EXPECT_TRUE(CommitGraphicsVertices(Count, m_Pending, [&] { Flush(); }));
			};
			return WriteGraphicsVertexBatches(vSource.size() / VerticesPerItem, VerticesPerItem, m_Pending, Write, [&] { Flush(); });
		}
	};
}

TEST(GraphicsVertexCommandStream, ExistingVerticesAndEveryNewVertexSurviveTheBufferBoundary)
{
	CVertexCommandStream<CCommandBuffer::SVertex, CCommandBuffer::SCommand_Render> Stream(EPrimitiveType::QUADS, 4);
	Stream.m_Pending = 4;
	for(int i = 0; i < 4; ++i)
		Stream.m_pVertices[i].m_Pos = {-1.0f, 0.0f};
	std::vector<CCommandBuffer::SVertex> vSource(36000);
	for(size_t i = 0; i < vSource.size(); ++i)
	{
		vSource[i].m_Pos = {static_cast<float>(i), 3.0f};
		vSource[i].m_Color = {1, 2, 3, 123};
	}
	ASSERT_TRUE(Stream.Append(vSource, 4));
	Stream.Flush();
	ASSERT_EQ(Stream.m_Commands.m_CommandCount, 2u);
	const auto *pFirst = static_cast<const CCommandBuffer::SCommand_Render *>(Stream.m_Commands.Head());
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_PrimCount, 8192u);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[0].m_Pos.x, -1.0f);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[4].m_Pos.x, 0.0f);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[32767].m_Pos.x, 32763.0f);
	const auto *pSecond = static_cast<const CCommandBuffer::SCommand_Render *>(pFirst->m_pNext);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_EQ(pSecond->m_PrimCount, 809u);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[0].m_Pos.x, 32764.0f);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[3235].m_Pos.x, 35999.0f);
	EXPECT_EQ(pSecond->m_pVertices[3235].m_Color.a, 123);
	EXPECT_EQ(pSecond->m_pNext, nullptr);
}

TEST(GraphicsVertexCommandStream, TypedArrayVertexDataKeepsItsLayerAndCompleteTriangleQuads)
{
	CVertexCommandStream<CCommandBuffer::SVertexTex3DStream, CCommandBuffer::SCommand_RenderTex3D> Stream(EPrimitiveType::TRIANGLES, 3);
	std::vector<CCommandBuffer::SVertexTex3DStream> vSource(36000);
	for(size_t i = 0; i < vSource.size(); ++i)
	{
		vSource[i].m_Pos = {static_cast<float>(i), 3.0f};
		vSource[i].m_Tex = vec3(0.25f, 0.75f, 9.0f);
	}
	ASSERT_TRUE(Stream.Append(vSource, 6));
	Stream.Flush();
	ASSERT_EQ(Stream.m_Commands.m_CommandCount, 2u);
	const auto *pFirst = static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(Stream.m_Commands.Head());
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_Cmd, CCommandBuffer::CMD_RENDER_TEX3D);
	EXPECT_EQ(pFirst->m_PrimCount, 10922u);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[32765].m_Pos.x, 32765.0f);
	EXPECT_FLOAT_EQ(pFirst->m_pVertices[32765].m_Tex.w, 9.0f);
	const auto *pSecond = static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(pFirst->m_pNext);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_EQ(pSecond->m_Cmd, CCommandBuffer::CMD_RENDER_TEX3D);
	EXPECT_EQ(pSecond->m_PrimCount, 1078u);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[0].m_Pos.x, 32766.0f);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[3233].m_Pos.x, 35999.0f);
	EXPECT_FLOAT_EQ(pSecond->m_pVertices[3233].m_Tex.w, 9.0f);
	EXPECT_EQ(pSecond->m_pNext, nullptr);
}
