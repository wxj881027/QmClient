// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/tclient/background_particles.h>
#include <game/client/components/tclient/qm_outline_neighbors.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace
{
	constexpr float EPSILON = 0.0001f;

	SBackgroundParticleProjection MakeProjection(float WorldWidth, float WorldHeight)
	{
		SBackgroundParticleProjection Projection;
		Projection.m_CameraCenter = vec2(100.0f, 50.0f);
		Projection.m_WorldLeft = Projection.m_CameraCenter.x - WorldWidth * 0.5f;
		Projection.m_WorldTop = Projection.m_CameraCenter.y - WorldHeight * 0.5f;
		Projection.m_WorldRight = Projection.m_CameraCenter.x + WorldWidth * 0.5f;
		Projection.m_WorldBottom = Projection.m_CameraCenter.y + WorldHeight * 0.5f;
		Projection.m_ViewportWidth = 1920.0f;
		Projection.m_ViewportHeight = 1080.0f;
		return Projection;
	}

	bool HasEdge(const SBackgroundParticleMesh &Mesh, int First, int Second)
	{
		if(First > Second)
			std::swap(First, Second);
		for(const auto &Edge : Mesh.m_vEdges)
		{
			int EdgeFirst = Edge[0];
			int EdgeSecond = Edge[1];
			if(EdgeFirst > EdgeSecond)
				std::swap(EdgeFirst, EdgeSecond);
			if(EdgeFirst == First && EdgeSecond == Second)
				return true;
		}
		return false;
	}

	void ExpectValidMesh(const SBackgroundParticleMesh &Mesh)
	{
		ASSERT_FALSE(Mesh.m_vVertices.empty());
		ASSERT_FALSE(Mesh.m_vEdges.empty());
		ASSERT_FALSE(Mesh.m_vFaces.empty());
		std::vector<std::vector<int>> vvNeighbors(Mesh.m_vVertices.size());
		std::vector<int> vFaceUseCount(Mesh.m_vVertices.size(), 0);
		std::set<std::pair<int, int>> UniqueEdges;
		for(const auto &Edge : Mesh.m_vEdges)
		{
			ASSERT_GE(Edge[0], 0);
			ASSERT_GE(Edge[1], 0);
			ASSERT_LT(Edge[0], static_cast<int>(Mesh.m_vVertices.size()));
			ASSERT_LT(Edge[1], static_cast<int>(Mesh.m_vVertices.size()));
			EXPECT_NE(Edge[0], Edge[1]);
			const auto OrderedEdge = std::minmax(Edge[0], Edge[1]);
			EXPECT_TRUE(UniqueEdges.emplace(OrderedEdge.first, OrderedEdge.second).second);
			vvNeighbors[Edge[0]].push_back(Edge[1]);
			vvNeighbors[Edge[1]].push_back(Edge[0]);
		}

		std::set<std::array<int, 3>> UniqueFaces;
		for(const auto &Face : Mesh.m_vFaces)
		{
			for(const int VertexIndex : Face)
			{
				ASSERT_GE(VertexIndex, 0);
				ASSERT_LT(VertexIndex, static_cast<int>(Mesh.m_vVertices.size()));
				++vFaceUseCount[VertexIndex];
			}
			EXPECT_NE(Face[0], Face[1]);
			EXPECT_NE(Face[1], Face[2]);
			EXPECT_NE(Face[2], Face[0]);
			auto SortedFace = Face;
			std::sort(SortedFace.begin(), SortedFace.end());
			EXPECT_TRUE(UniqueFaces.emplace(SortedFace).second);
			const vec3 First = Mesh.m_vVertices[Face[1]] - Mesh.m_vVertices[Face[0]];
			const vec3 Second = Mesh.m_vVertices[Face[2]] - Mesh.m_vVertices[Face[0]];
			const vec3 Cross(
				First.y * Second.z - First.z * Second.y,
				First.z * Second.x - First.x * Second.z,
				First.x * Second.y - First.y * Second.x);
			EXPECT_GT(length(Cross), EPSILON);
		}

		std::vector<bool> vVisited(Mesh.m_vVertices.size(), false);
		std::vector<int> vPending = {0};
		while(!vPending.empty())
		{
			const int VertexIndex = vPending.back();
			vPending.pop_back();
			if(vVisited[VertexIndex])
				continue;
			vVisited[VertexIndex] = true;
			for(const int Neighbor : vvNeighbors[VertexIndex])
				vPending.push_back(Neighbor);
		}
		for(size_t VertexIndex = 0; VertexIndex < Mesh.m_vVertices.size(); ++VertexIndex)
		{
			EXPECT_TRUE(vVisited[VertexIndex]) << "wireframe vertex " << VertexIndex << " is disconnected";
			EXPECT_GT(vFaceUseCount[VertexIndex], 0) << "mesh vertex " << VertexIndex << " is unused by faces";
		}
	}
} // namespace

TEST(QmBackgroundParticlesProjection, DepthScaleIsSingleMonotonicTransform)
{
	EXPECT_FLOAT_EQ(BackgroundParticleDepthScale(-10.0f, 300.0f), 1.0f);
	EXPECT_FLOAT_EQ(BackgroundParticleDepthScale(0.0f, 300.0f), 1.0f);
	EXPECT_GT(BackgroundParticleDepthScale(100.0f, 300.0f), BackgroundParticleDepthScale(200.0f, 300.0f));
	EXPECT_GT(BackgroundParticleDepthScale(200.0f, 300.0f), BackgroundParticleDepthScale(300.0f, 300.0f));
	EXPECT_NEAR(BackgroundParticleDepthScale(300.0f, 300.0f), 0.28f, EPSILON);
	EXPECT_NEAR(BackgroundParticleDepthScale(600.0f, 300.0f), 0.28f, EPSILON);
}

TEST(QmBackgroundParticlesProjection, ZoomChangesAnchorButNotModelPixelSize)
{
	const SBackgroundParticleProjection Normal = MakeProjection(1600.0f, 900.0f);
	const SBackgroundParticleProjection ZoomedOut = MakeProjection(3200.0f, 1800.0f);
	const vec2 WorldAnchor = Normal.m_CameraCenter + vec2(200.0f, 125.0f);
	const vec3 LocalTopLeft(-10.0f, -6.0f, -5.0f);
	const vec3 LocalBottomRight(10.0f, 6.0f, 5.0f);

	const vec2 NormalTopLeft = BackgroundParticleProjectVertex(Normal, WorldAnchor, LocalTopLeft, 150.0f, 300.0f);
	const vec2 NormalBottomRight = BackgroundParticleProjectVertex(Normal, WorldAnchor, LocalBottomRight, 150.0f, 300.0f);
	const vec2 ZoomedTopLeft = BackgroundParticleProjectVertex(ZoomedOut, WorldAnchor, LocalTopLeft, 150.0f, 300.0f);
	const vec2 ZoomedBottomRight = BackgroundParticleProjectVertex(ZoomedOut, WorldAnchor, LocalBottomRight, 150.0f, 300.0f);
	const vec2 NormalAnchor = BackgroundParticleProjectVertex(Normal, WorldAnchor, vec3(0.0f, 0.0f, 0.0f), 150.0f, 300.0f);
	const vec2 ZoomedAnchor = BackgroundParticleProjectVertex(ZoomedOut, WorldAnchor, vec3(0.0f, 0.0f, 0.0f), 150.0f, 300.0f);

	EXPECT_NEAR(NormalBottomRight.x - NormalTopLeft.x, ZoomedBottomRight.x - ZoomedTopLeft.x, EPSILON);
	EXPECT_NEAR(NormalBottomRight.y - NormalTopLeft.y, ZoomedBottomRight.y - ZoomedTopLeft.y, EPSILON);
	EXPECT_GT(NormalAnchor.x - Normal.m_ViewportWidth * 0.5f, ZoomedAnchor.x - ZoomedOut.m_ViewportWidth * 0.5f);
	EXPECT_GT(NormalAnchor.y - Normal.m_ViewportHeight * 0.5f, ZoomedAnchor.y - ZoomedOut.m_ViewportHeight * 0.5f);
}

TEST(QmBackgroundParticlesProjection, LocalZNeverScalesCameraToAnchorVector)
{
	const SBackgroundParticleProjection Projection = MakeProjection(1600.0f, 900.0f);
	const vec2 WorldAnchor = Projection.m_CameraCenter + vec2(250.0f, 125.0f);
	const vec2 Front = BackgroundParticleProjectVertex(Projection, WorldAnchor, vec3(3.0f, -7.0f, -40.0f), 200.0f, 300.0f);
	const vec2 Back = BackgroundParticleProjectVertex(Projection, WorldAnchor, vec3(3.0f, -7.0f, 40.0f), 200.0f, 300.0f);

	EXPECT_NEAR(Front.x, Back.x, EPSILON);
	EXPECT_NEAR(Front.y, Back.y, EPSILON);
}

TEST(QmBackgroundParticlesProjection, RotationLetsLocalZContributeThroughScreenAxes)
{
	const vec3 Rotated = BackgroundParticleRotateVertex(vec3(0.0f, 0.0f, 1.0f), vec3(pi * 0.5f, 0.0f, 0.0f));
	EXPECT_NEAR(Rotated.x, 0.0f, EPSILON);
	EXPECT_NEAR(Rotated.y, -1.0f, EPSILON);
	EXPECT_NEAR(Rotated.z, 0.0f, EPSILON);
}

TEST(QmBackgroundParticlesMesh, PrimitiveTopologyIsStable)
{
	const SBackgroundParticleMesh &Cube = BackgroundParticleCubeMesh();
	const SBackgroundParticleMesh &Box = BackgroundParticleBoxMesh();
	const SBackgroundParticleMesh &Pyramid = BackgroundParticlePyramidMesh();
	const SBackgroundParticleMesh &Octahedron = BackgroundParticleOctahedronMesh();
	const SBackgroundParticleMesh &Sphere = BackgroundParticleSphereMesh();
	const SBackgroundParticleMesh &Torus = BackgroundParticleTorusMesh();

	EXPECT_EQ(Cube.m_vVertices.size(), 8U);
	EXPECT_EQ(Cube.m_vEdges.size(), 12U);
	EXPECT_EQ(Cube.m_vFaces.size(), 12U);
	EXPECT_EQ(Box.m_vVertices.size(), 8U);
	EXPECT_EQ(Box.m_vEdges.size(), 12U);
	EXPECT_EQ(Box.m_vFaces.size(), 12U);
	EXPECT_EQ(Pyramid.m_vVertices.size(), 5U);
	EXPECT_EQ(Pyramid.m_vEdges.size(), 8U);
	EXPECT_EQ(Pyramid.m_vFaces.size(), 6U);
	EXPECT_EQ(Octahedron.m_vVertices.size(), 6U);
	EXPECT_EQ(Octahedron.m_vEdges.size(), 12U);
	EXPECT_EQ(Octahedron.m_vFaces.size(), 8U);
	EXPECT_EQ(Sphere.m_vVertices.size(), 62U);
	EXPECT_EQ(Sphere.m_vEdges.size(), 132U);
	EXPECT_EQ(Sphere.m_vFaces.size(), 120U);
	EXPECT_EQ(Torus.m_vVertices.size(), 72U);
	EXPECT_EQ(Torus.m_vEdges.size(), 144U);
	EXPECT_EQ(Torus.m_vFaces.size(), 144U);

	ExpectValidMesh(Cube);
	ExpectValidMesh(Box);
	ExpectValidMesh(Pyramid);
	ExpectValidMesh(Octahedron);
	ExpectValidMesh(Sphere);
	ExpectValidMesh(Torus);
}

TEST(QmBackgroundParticlesMesh, FaceTriangulationDoesNotBecomeWireframeDiagonals)
{
	const SBackgroundParticleMesh &Cube = BackgroundParticleCubeMesh();
	EXPECT_FALSE(HasEdge(Cube, 0, 2));
	EXPECT_FALSE(HasEdge(Cube, 4, 6));

	const SBackgroundParticleMesh &Torus = BackgroundParticleTorusMesh();
	EXPECT_FALSE(HasEdge(Torus, 0, 7));
}

TEST(QmOutlineNeighbors, CachePreservesPriorityAndVirtualMapEdges)
{
	std::array<int, 9> aTiles = {1, 2, 7, 3, 4, 5, 7, 6, 0};
	int Reads = 0;
	const auto GetTile = [&](int X, int Y) {
		++Reads;
		return aTiles[std::clamp(Y, 0, 2) * 3 + std::clamp(X, 0, 2)] & 7;
	};
	EXPECT_EQ(QmOutlineCachedNeighbors(aTiles[4], 1, 1, 3, 3, GetTile), 116);
	EXPECT_EQ(Reads, 8);
	for(int Frame = 0; Frame < 240; ++Frame)
		EXPECT_EQ(QmOutlineCachedNeighbors(aTiles[4], 1, 1, 3, 3, GetTile), 116);
	EXPECT_EQ(Reads, 8);
	// 缓存位不参与类型比较；地图外必须按原坐标夹取邻居。
	for(int Y = -2; Y <= 4; ++Y)
	{
		for(int X = -2; X <= 4; ++X)
		{
			int &Tile = aTiles[std::clamp(Y, 0, 2) * 3 + std::clamp(X, 0, 2)];
			const int Type = Tile & 7;
			const int aDx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
			const int aDy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
			int Expected = 0;
			for(int i = 0; i < 8; ++i)
				Expected |= (GetTile(X + aDx[i], Y + aDy[i]) >= Type) << i;
			EXPECT_EQ(QmOutlineCachedNeighbors(Tile, X, Y, 3, 3, GetTile), Expected);
		}
	}
	// 新地图覆盖原数组后，不能沿用旧地图的边界缓存。
	aTiles.fill(4);
	EXPECT_EQ(QmOutlineCachedNeighbors(aTiles[4], 1, 1, 3, 3, GetTile), 255);
}

TEST(QmBackgroundParticles, PreparedRotationPreservesAxisOrder)
{
	const std::array<vec3, 3> aRotations = {vec3(0, 0, 0), vec3(0.3f, -0.7f, 1.2f), vec3(-2.0f, 1.8f, -0.4f)};
	for(const vec3 &Rotation : aRotations)
	{
		const SBackgroundParticleRotation Prepared(Rotation);
		for(const vec3 &Vertex : BackgroundParticleCubeMesh().m_vVertices)
		{
			vec3 Expected = Vertex * 17.0f;
			Expected = vec3(Expected.x * std::cos(Rotation.z) - Expected.y * std::sin(Rotation.z), Expected.x * std::sin(Rotation.z) + Expected.y * std::cos(Rotation.z), Expected.z);
			Expected = vec3(Expected.x, Expected.y * std::cos(Rotation.x) - Expected.z * std::sin(Rotation.x), Expected.y * std::sin(Rotation.x) + Expected.z * std::cos(Rotation.x));
			Expected = vec3(Expected.x * std::cos(Rotation.y) + Expected.z * std::sin(Rotation.y), Expected.y, -Expected.x * std::sin(Rotation.y) + Expected.z * std::cos(Rotation.y));
			const vec3 Actual = Prepared.Apply(Vertex * 17.0f);
			EXPECT_FLOAT_EQ(Actual.x, Expected.x);
			EXPECT_FLOAT_EQ(Actual.y, Expected.y);
			EXPECT_FLOAT_EQ(Actual.z, Expected.z);
		}
	}
}
