#ifndef GAME_CLIENT_PREDICTION_COPY_LINKS_H
#define GAME_CLIENT_PREDICTION_COPY_LINKS_H

namespace QmCopyLinks
{
	// 世界和实体各自维护一对借用引用，解除时只修改仍指向本节点的反向引用。
	template<typename T>
	void DetachParent(T &Node)
	{
		if(Node.m_pParent && Node.m_pParent->m_pChild == &Node)
			Node.m_pParent->m_pChild = nullptr;
		Node.m_pParent = nullptr;
	}

	template<typename T>
	void DetachChild(T &Node)
	{
		if(Node.m_pChild && Node.m_pChild->m_pParent == &Node)
			Node.m_pChild->m_pParent = nullptr;
		Node.m_pChild = nullptr;
	}

	template<typename T>
	void Unlink(T &Node)
	{
		DetachParent(Node);
		DetachChild(Node);
	}

	template<typename T>
	void Attach(T &Parent, T &Child)
	{
		if(&Parent == &Child)
			return;
		DetachParent(Child);
		DetachChild(Parent);
		Parent.m_pChild = &Child;
		Child.m_pParent = &Parent;
	}

	// 新复制对象里的指针属于源对象，尚未建立本对象关系，不能沿指针解除源关系。
	template<typename T>
	void ResetCopied(T &Node)
	{
		Node.m_pParent = nullptr;
		Node.m_pChild = nullptr;
	}
}

#endif
