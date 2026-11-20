#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <limits>
#include <new>

namespace
{
	class QmUiElementAllocation : public qm_ui_test::CRealUiFixture
	{
	protected:
		void Populate(CUIElement &Element)
		{
			const CUIRect Rect{0, 0, 96, 18};
			m_Ui.DoLabelStreamed(*Element.Rect(0), &Rect, "Retry", 12, TEXTALIGN_ML);
		}
	};

	// 只计量生产创建操作；UI、Google Test 和断言均在故障域之外。
	size_t ColdInitializationAllocations()
	{
		CUi Ui;
		CUIElement Element;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Element.Init(&Ui, 1);
			Attempts = Count.Attempts();
		}
		return Attempts;
	}

	size_t OwnedCreationAllocations(int RectCount)
	{
		CUi Ui;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Ui.GetNewUIElement(RectCount);
			Attempts = Count.Attempts();
		}
		return Attempts;
	}
}

TEST_F(QmUiElementAllocation, ColdFailureLeavesTheElementUnregisteredAndCanRetryTextRendering)
{
	CUIElement Element;
	bool Threw = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		try
		{
			Element.Init(&m_Ui, 1);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_TRUE(Threw);
	ASSERT_FALSE(Element.IsRegistered());
	ASSERT_FALSE(Element.AreRectsInit());
	Element.Init(&m_Ui, 1);
	ASSERT_TRUE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0)->m_pParent, &Element);
	Populate(Element);
	ASSERT_TRUE(Element.Rect(0)->m_UITextContainer.Valid());
	m_Ui.OnElementsReset();
	EXPECT_FALSE(Element.Rect(0)->m_UITextContainer.Valid());
	Populate(Element);
	EXPECT_TRUE(Element.Rect(0)->m_UITextContainer.Valid());
}

TEST_F(QmUiElementAllocation, RegistryGrowthFailureDoesNotPublishNewRects)
{
	const size_t Allocations = ColdInitializationAllocations();
	ASSERT_GT(Allocations, 0u);
	CUIElement Element;
	bool Threw = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault(Allocations - 1);
		try
		{
			Element.Init(&m_Ui, 1);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	ASSERT_TRUE(Threw);
	ASSERT_FALSE(Element.IsRegistered());
	ASSERT_FALSE(Element.AreRectsInit());
	Element.Init(&m_Ui, 1);
	EXPECT_TRUE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0)->m_pParent, &Element);
	Populate(Element);
	EXPECT_TRUE(Element.Rect(0)->m_UITextContainer.Valid());
}

TEST_F(QmUiElementAllocation, RegistryFailurePreservesPreinitializedRectsForRetry)
{
	CUIElement Element;
	Element.InitRects(1);
	Element.Rect(0)->m_Text = "Prepared";
	auto *pRect = Element.Rect(0);
	bool Threw = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		try
		{
			Element.Init(&m_Ui, 1);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	ASSERT_TRUE(Threw);
	ASSERT_FALSE(Element.IsRegistered());
	ASSERT_TRUE(Element.AreRectsInit());
	EXPECT_EQ(Element.Rect(0), pRect);
	EXPECT_EQ(Element.Rect(0)->m_Text, "Prepared");
	EXPECT_EQ(Element.Rect(0)->m_pParent, &Element);
	Element.Init(&m_Ui, 1);
	EXPECT_TRUE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0)->m_Text, "Prepared");
}

TEST(QmUiOwnedElementAllocation, EveryCreationAllocationCanFailAndRetryWithinTheSameUi)
{
	const size_t Allocations = OwnedCreationAllocations(2);
	ASSERT_GT(Allocations, 0u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmUiLifetime::CWeakRef Ref;
		{
			CUi Ui;
			bool Threw = false, Triggered = false;
			{
				CPredictionAllocationFailure Fault(Budget);
				try
				{
					Ui.GetNewUIElement(2);
				}
				catch(const std::bad_alloc &)
				{
					Threw = true;
				}
				Triggered = Fault.Triggered();
			}
			ASSERT_TRUE(Triggered);
			ASSERT_TRUE(Threw);
			auto *pElement = Ui.GetNewUIElement(2);
			ASSERT_TRUE(pElement->IsRegistered());
			EXPECT_EQ(pElement->Rect(0)->m_pParent, pElement);
			EXPECT_EQ(pElement->Rect(1)->m_pParent, pElement);
			Ref = pElement->WeakRef();
			EXPECT_TRUE(Ref.IsAlive());
		}
		EXPECT_FALSE(Ref.IsAlive());
	}
}

TEST(QmUiOwnedElementAllocation, FailedOwnerAdmissionReturnsTheReadyRegistrySlot)
{
	const size_t Allocations = OwnedCreationAllocations(1);
	ASSERT_GT(Allocations, 0u);
	CUIElement Probe;
	{
		CUi Ui;
		bool Threw = false, Triggered = false;
		{
			CPredictionAllocationFailure Fault(Allocations - 1);
			try
			{
				Ui.GetNewUIElement(1);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Fault.Triggered();
		}
		ASSERT_TRUE(Triggered);
		ASSERT_TRUE(Threw);
		bool ProbeThrew = false, ProbeFault = false;
		{
			CPredictionAllocationFailure Fault;
			try
			{
				Probe.Init(&Ui, 0);
			}
			catch(const std::bad_alloc &)
			{
				ProbeThrew = true;
			}
			ProbeFault = Fault.Triggered();
		}
		EXPECT_FALSE(ProbeThrew);
		EXPECT_FALSE(ProbeFault);
		EXPECT_TRUE(Probe.IsRegistered());
	}
	EXPECT_FALSE(Probe.IsRegistered());
}

TEST(QmUiOwnedElementAllocation, EmptyRectsReuseTheWarmRegistryWithoutAllocation)
{
	CUi Ui;
	{
		CUIElement Warm;
		Warm.Init(&Ui, 0);
	}
	CUIElement Element;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Element.Init(&Ui, 0);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_TRUE(Element.IsRegistered());
	EXPECT_FALSE(Element.AreRectsInit());
}

TEST(QmUiOwnedElementAllocation, PreinitializedRectsReuseTheWarmRegistryWithoutAllocation)
{
	CUi Ui;
	{
		CUIElement Warm;
		Warm.Init(&Ui, 0);
	}
	CUIElement Element;
	Element.InitRects(1);
	auto *pRect = Element.Rect(0);
	pRect->m_Text = "Prepared";
	bool Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Element.Init(&Ui, 1);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_TRUE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0), pRect);
	EXPECT_EQ(pRect->m_Text, "Prepared");
	EXPECT_EQ(pRect->m_pParent, &Element);
}

TEST(QmUiElementTryInit, ColdAllocationFailureReturnsFalseAndTheSameElementCanRetry)
{
	CUi Ui;
	CUIElement Element;
	bool Accepted = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Accepted = Element.TryInit(&Ui, 1);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Accepted);
	ASSERT_FALSE(Element.IsRegistered());
	ASSERT_FALSE(Element.AreRectsInit());
	ASSERT_TRUE(Element.TryInit(&Ui, 1));
	EXPECT_TRUE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0)->m_pParent, &Element);
}

TEST(QmUiElementTryInit, RegistryFailureKeepsPreparedRectsAndReturnsFalse)
{
	CUi Ui;
	CUIElement Element;
	Element.InitRects(1);
	auto *pRect = Element.Rect(0);
	pRect->m_Text = "Prepared";
	bool Accepted = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Accepted = Element.TryInit(&Ui, 1);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Accepted);
	ASSERT_FALSE(Element.IsRegistered());
	EXPECT_EQ(Element.Rect(0), pRect);
	EXPECT_EQ(pRect->m_Text, "Prepared");
	ASSERT_TRUE(Element.TryInit(&Ui, 1));
	EXPECT_EQ(Element.Rect(0), pRect);
	EXPECT_EQ(pRect->m_Text, "Prepared");
	EXPECT_EQ(pRect->m_pParent, &Element);
}
