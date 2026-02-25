/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmTree.h"

void CUiV2Tree::Reset()
{
	m_FrameNodeKeys.clear();
	m_LastNodeCount = 0;
}

void CUiV2Tree::BeginFrame()
{
	m_FrameNodeKeys.clear();
}

void CUiV2Tree::TouchNode(uint64_t NodeKey)
{
	m_FrameNodeKeys.insert(NodeKey);
}

void CUiV2Tree::EndFrame()
{
	m_LastNodeCount = static_cast<int>(m_FrameNodeKeys.size());
}

int CUiV2Tree::NodeCount() const
{
	return m_LastNodeCount;
}
