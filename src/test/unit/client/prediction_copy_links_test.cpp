#include <game/client/prediction/copy_links.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	// 测试节点只提供生产引用接口，关系建立、替换和解除全部调用生产实现。
	struct CNode
	{
		CNode *m_pParent = nullptr;
		CNode *m_pChild = nullptr;
		~CNode() { QmCopyLinks::Unlink(*this); }
	};
}

TEST(PredictionCopyLinks, AttachingEstablishesBothDirections)
{
	CNode Parent, Child;
	QmCopyLinks::Attach(Parent, Child);
	EXPECT_EQ(Parent.m_pChild, &Child);
	EXPECT_EQ(Child.m_pParent, &Parent);
	EXPECT_EQ(Parent.m_pParent, nullptr);
	EXPECT_EQ(Child.m_pChild, nullptr);
}

TEST(PredictionCopyLinks, ReplacingChildDetachesDisplacedCopy)
{
	CNode Parent, OldCopy, NewCopy;
	QmCopyLinks::Attach(Parent, OldCopy);
	QmCopyLinks::Attach(Parent, NewCopy);
	EXPECT_EQ(OldCopy.m_pParent, nullptr);
	EXPECT_EQ(Parent.m_pChild, &NewCopy);
	EXPECT_EQ(NewCopy.m_pParent, &Parent);
}

TEST(PredictionCopyLinks, ReparentingClearsTheOldParentsReference)
{
	CNode OldParent, NewParent, Copy;
	QmCopyLinks::Attach(OldParent, Copy);
	QmCopyLinks::Attach(NewParent, Copy);
	EXPECT_EQ(OldParent.m_pChild, nullptr);
	EXPECT_EQ(Copy.m_pParent, &NewParent);
	EXPECT_EQ(NewParent.m_pChild, &Copy);
}

TEST(PredictionCopyLinks, RepeatedAttachKeepsOneReciprocalPair)
{
	CNode Parent, Copy;
	QmCopyLinks::Attach(Parent, Copy);
	QmCopyLinks::Attach(Parent, Copy);
	EXPECT_EQ(Parent.m_pChild, &Copy);
	EXPECT_EQ(Copy.m_pParent, &Parent);
}

TEST(PredictionCopyLinks, DetachingStaleParentReferenceDoesNotClearCurrentChild)
{
	CNode Parent, Current, Stale;
	QmCopyLinks::Attach(Parent, Current);
	Stale.m_pParent = &Parent;
	QmCopyLinks::DetachParent(Stale);
	EXPECT_EQ(Stale.m_pParent, nullptr);
	EXPECT_EQ(Parent.m_pChild, &Current);
	EXPECT_EQ(Current.m_pParent, &Parent);
}

TEST(PredictionCopyLinks, DetachingStaleChildReferenceDoesNotClearCurrentParent)
{
	CNode Current, Child, Stale;
	QmCopyLinks::Attach(Current, Child);
	Stale.m_pChild = &Child;
	QmCopyLinks::DetachChild(Stale);
	EXPECT_EQ(Stale.m_pChild, nullptr);
	EXPECT_EQ(Current.m_pChild, &Child);
	EXPECT_EQ(Child.m_pParent, &Current);
}

TEST(PredictionCopyLinks, UnlinkingMiddleNodeDetachesBothNeighbors)
{
	CNode Parent, Middle, Child;
	QmCopyLinks::Attach(Parent, Middle);
	QmCopyLinks::Attach(Middle, Child);
	QmCopyLinks::Unlink(Middle);
	EXPECT_EQ(Parent.m_pChild, nullptr);
	EXPECT_EQ(Child.m_pParent, nullptr);
	EXPECT_EQ(Middle.m_pParent, nullptr);
	EXPECT_EQ(Middle.m_pChild, nullptr);
	QmCopyLinks::Unlink(Middle);
	EXPECT_EQ(Parent.m_pChild, nullptr);
	EXPECT_EQ(Child.m_pParent, nullptr);
}

TEST(PredictionCopyLinks, DestroyingDisplacedCopyKeepsReplacementConnected)
{
	CNode Parent, Replacement;
	auto OldCopy = std::make_unique<CNode>();
	QmCopyLinks::Attach(Parent, *OldCopy);
	QmCopyLinks::Attach(Parent, Replacement);
	OldCopy.reset();
	EXPECT_EQ(Parent.m_pChild, &Replacement);
	EXPECT_EQ(Replacement.m_pParent, &Parent);
}

TEST(PredictionCopyLinks, DestroyingParentClearsCurrentCopyAndLeavesDisplacedCopyDetached)
{
	CNode Displaced, Current;
	auto Parent = std::make_unique<CNode>();
	QmCopyLinks::Attach(*Parent, Displaced);
	QmCopyLinks::Attach(*Parent, Current);
	Parent.reset();
	EXPECT_EQ(Displaced.m_pParent, nullptr);
	EXPECT_EQ(Current.m_pParent, nullptr);
}

TEST(PredictionCopyLinks, DestroyingOldParentDoesNotBreakReparentedCopy)
{
	CNode NewParent, Copy;
	auto OldParent = std::make_unique<CNode>();
	QmCopyLinks::Attach(*OldParent, Copy);
	QmCopyLinks::Attach(NewParent, Copy);
	OldParent.reset();
	EXPECT_EQ(NewParent.m_pChild, &Copy);
	EXPECT_EQ(Copy.m_pParent, &NewParent);
}

TEST(PredictionCopyLinks, DestroyingChildClearsSurvivingParent)
{
	CNode Parent;
	auto Copy = std::make_unique<CNode>();
	QmCopyLinks::Attach(Parent, *Copy);
	Copy.reset();
	EXPECT_EQ(Parent.m_pChild, nullptr);
}

TEST(PredictionCopyLinks, ResettingNewlyCopiedPointersDoesNotDetachSourcesRelationships)
{
	CNode Parent, Source, ExistingChild;
	QmCopyLinks::Attach(Parent, Source);
	QmCopyLinks::Attach(Source, ExistingChild);
	CNode Copy = Source;
	QmCopyLinks::ResetCopied(Copy);
	EXPECT_EQ(Copy.m_pParent, nullptr);
	EXPECT_EQ(Copy.m_pChild, nullptr);
	EXPECT_EQ(Parent.m_pChild, &Source);
	EXPECT_EQ(Source.m_pParent, &Parent);
	EXPECT_EQ(Source.m_pChild, &ExistingChild);
	EXPECT_EQ(ExistingChild.m_pParent, &Source);
}

TEST(PredictionCopyLinks, AttachingNewlyCopiedNodeCannotKeepSourcesOldChild)
{
	CNode Source, OldCopy;
	QmCopyLinks::Attach(Source, OldCopy);
	CNode NewCopy = Source;
	QmCopyLinks::ResetCopied(NewCopy);
	QmCopyLinks::Attach(Source, NewCopy);
	EXPECT_EQ(OldCopy.m_pParent, nullptr);
	EXPECT_EQ(NewCopy.m_pChild, nullptr);
	EXPECT_EQ(Source.m_pChild, &NewCopy);
	EXPECT_EQ(NewCopy.m_pParent, &Source);
	QmCopyLinks::Unlink(OldCopy);
	EXPECT_EQ(NewCopy.m_pParent, &Source);
}

TEST(PredictionCopyLinks, SelfAttachDoesNotChangeAnExistingChain)
{
	CNode Parent, Middle, Child;
	QmCopyLinks::Attach(Parent, Middle);
	QmCopyLinks::Attach(Middle, Child);
	QmCopyLinks::Attach(Middle, Middle);
	EXPECT_EQ(Parent.m_pChild, &Middle);
	EXPECT_EQ(Middle.m_pParent, &Parent);
	EXPECT_EQ(Middle.m_pChild, &Child);
	EXPECT_EQ(Child.m_pParent, &Middle);
}
