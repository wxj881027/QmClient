#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_ROUTE_RENDER_CACHE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_ROUTE_RENDER_CACHE_H

#include <base/vmath.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// 路线选择由调用方负责；缓存只复用结果，并按连续路径分块保留原绘制顺序。
class CQmRouteRenderCache
{
public:
	struct SKey
	{
		uint64_t m_Revision = 0;
		int m_StartIndex = -1;
		int m_ClientId = -1;
		int m_Dummy = 0;
		bool m_DDrace = false;
		bool m_Segment = false;

		bool operator==(const SKey &Other) const
		{
			return m_Revision == Other.m_Revision && m_StartIndex == Other.m_StartIndex &&
			       m_ClientId == Other.m_ClientId && m_Dummy == Other.m_Dummy &&
			       m_DDrace == Other.m_DDrace && m_Segment == Other.m_Segment;
		}
	};

	struct SView
	{
		float m_Left;
		float m_Top;
		float m_Right;
		float m_Bottom;

		bool operator==(const SView &Other) const
		{
			return m_Left == Other.m_Left && m_Top == Other.m_Top &&
			       m_Right == Other.m_Right && m_Bottom == Other.m_Bottom;
		}
	};

private:
	static constexpr size_t LEAF_POINTS = 64;
	struct SNode
	{
		SView m_Bounds;
		size_t m_Begin;
		size_t m_End;
		size_t m_Left = 0;
		size_t m_Right = 0;
	};

	SKey m_Key;
	SView m_View{};
	bool m_KeyValid = false;
	bool m_ViewValid = false;
	std::vector<vec2> m_vPoints;
	std::vector<SNode> m_vNodes;
	std::vector<size_t> m_vVisible;

	size_t BuildNode(size_t Begin, size_t End)
	{
		const size_t NodeIndex = m_vNodes.size();
		const vec2 First = m_vPoints[Begin];
		m_vNodes.push_back({{First.x, First.y, First.x, First.y}, Begin, End});
		if(End - Begin <= LEAF_POINTS)
		{
			SView &Bounds = m_vNodes[NodeIndex].m_Bounds;
			for(size_t Index = Begin + 1; Index < End; ++Index)
			{
				const vec2 Point = m_vPoints[Index];
				Bounds.m_Left = std::min(Bounds.m_Left, Point.x);
				Bounds.m_Top = std::min(Bounds.m_Top, Point.y);
				Bounds.m_Right = std::max(Bounds.m_Right, Point.x);
				Bounds.m_Bottom = std::max(Bounds.m_Bottom, Point.y);
			}
		}
		else
		{
			const size_t Middle = Begin + (End - Begin) / 2;
			const size_t Left = BuildNode(Begin, Middle);
			const size_t Right = BuildNode(Middle, End);
			// 递归可能扩容，只保存索引，完成后再获取节点引用。
			SNode &Node = m_vNodes[NodeIndex];
			Node.m_Left = Left;
			Node.m_Right = Right;
			const SView &LeftBounds = m_vNodes[Left].m_Bounds;
			const SView &RightBounds = m_vNodes[Right].m_Bounds;
			Node.m_Bounds = {std::min(LeftBounds.m_Left, RightBounds.m_Left), std::min(LeftBounds.m_Top, RightBounds.m_Top),
				std::max(LeftBounds.m_Right, RightBounds.m_Right), std::max(LeftBounds.m_Bottom, RightBounds.m_Bottom)};
		}
		return NodeIndex;
	}

	void QueryNode(size_t NodeIndex, const SView &View, size_t &TestedPoints)
	{
		const SNode &Node = m_vNodes[NodeIndex];
		const SView &Bounds = Node.m_Bounds;
		if(Bounds.m_Right < View.m_Left || Bounds.m_Left > View.m_Right ||
			Bounds.m_Bottom < View.m_Top || Bounds.m_Top > View.m_Bottom)
			return;
		if(Node.m_End - Node.m_Begin <= LEAF_POINTS)
		{
			TestedPoints += Node.m_End - Node.m_Begin;
			for(size_t Index = Node.m_Begin; Index < Node.m_End; ++Index)
			{
				const vec2 Point = m_vPoints[Index];
				if(Point.x >= View.m_Left && Point.x <= View.m_Right && Point.y >= View.m_Top && Point.y <= View.m_Bottom)
					m_vVisible.push_back(Index);
			}
		}
		else
		{
			QueryNode(Node.m_Left, View, TestedPoints);
			QueryNode(Node.m_Right, View, TestedPoints);
		}
	}

public:
	template<typename FBuilder>
	bool Update(const SKey &Key, const FBuilder &Builder)
	{
		if(m_KeyValid && m_Key == Key)
			return !m_vPoints.empty();
		m_Key = Key;
		m_KeyValid = true;
		m_ViewValid = false;
		m_vPoints.clear();
		m_vNodes.clear();
		m_vVisible.clear();
		if(!Builder(m_vPoints))
			m_vPoints.clear();
		if(!m_vPoints.empty())
		{
			m_vNodes.reserve(m_vPoints.size() / (LEAF_POINTS / 2) + 1);
			BuildNode(0, m_vPoints.size());
		}
		return !m_vPoints.empty();
	}

	const vec2 &Point(size_t Index) const { return m_vPoints[Index]; }

	// TestedPoints 记录实际检查的点数，供行为回归和微基准观察不可见路径是否被跳过。
	const std::vector<size_t> &QueryVisible(const SView &View, size_t *pTestedPoints = nullptr)
	{
		size_t TestedPoints = 0;
		if(!m_ViewValid || !(m_View == View))
		{
			m_View = View;
			m_ViewValid = true;
			m_vVisible.clear();
			if(!m_vNodes.empty())
				QueryNode(0, View, TestedPoints);
		}
		if(pTestedPoints)
			*pTestedPoints = TestedPoints;
		return m_vVisible;
	}

	void Invalidate()
	{
		m_KeyValid = false;
		m_ViewValid = false;
		m_vPoints.clear();
		m_vNodes.clear();
		m_vVisible.clear();
	}
};

#endif
