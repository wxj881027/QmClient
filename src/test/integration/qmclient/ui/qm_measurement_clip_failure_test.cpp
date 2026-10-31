#include <game/client/QmUi/SettingsCardMeasure.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/tracked_allocation_resource.h>

#include <new>

namespace
{
	// 内存域先构造、最后销毁，覆盖真实UI和其裁剪存储。
	class CClipMemoryOwner
	{
	protected:
		CTrackedAllocationResource m_Memory;
		~CClipMemoryOwner() { EXPECT_EQ(m_Memory.m_Live, 0); }
	};
}

class QmMeasurementClipFailure : public CClipMemoryOwner, public qm_ui_test::CRealUiFixture
{
	const CUIRect m_Parent{20, 30, 240, 120};

protected:
	void FailNextAllocation() { m_Memory.m_FailAt = m_Memory.m_Allocations + 1; }
	void ExpectRect(const CUIRect &Actual, const CUIRect &Expected)
	{
		EXPECT_FLOAT_EQ(Actual.x, Expected.x);
		EXPECT_FLOAT_EQ(Actual.y, Expected.y);
		EXPECT_FLOAT_EQ(Actual.w, Expected.w);
		EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	}
	// 不依赖标准库的vector增长倍率；填满当前容量后在下一次增长处拒绝。
	bool PrepareFullParentClipStorage()
	{
		m_Ui.ClipEnable(&m_Parent);
		FailNextAllocation();
		for(int Index = 0; Index < 128; ++Index)
		{
			try
			{
				m_Ui.ClipEnable(&m_Parent);
			}
			catch(const std::bad_alloc &)
			{
				m_Memory.m_FailAt = -1;
				return true;
			}
		}
		m_Memory.m_FailAt = -1;
		return false;
	}
	void TearDown() override
	{
		m_Memory.m_FailAt = -1;
		// 同时允许回归断言失败时清理真实存在的裁剪，不依据期望深度盲目pop。
		while(m_Ui.IsClipped())
			m_Ui.ClipDisable();
		CRealUiFixture::TearDown();
	}

public:
	QmMeasurementClipFailure() :
		CRealUiFixture(nullptr, &m_Memory) {}
};

TEST_F(QmMeasurementClipFailure, InitialAllocationFailureLeavesMeasurementInactiveAndCanRetry)
{
	FailNextAllocation();
	EXPECT_THROW(m_Ui.BeginRenderOnly(), std::bad_alloc);
	ASSERT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_FALSE(m_Ui.DrawingSuppressed());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(Graphics().m_Clipped);
	EXPECT_EQ(Graphics().m_ClipChanges, 0);
	m_Memory.m_FailAt = -1;
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(Graphics().m_Clipped);
		EXPECT_FLOAT_EQ(Graphics().m_ClipRect.w, 0);
		EXPECT_FLOAT_EQ(Graphics().m_ClipRect.h, 0);
	}
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(Graphics().m_Clipped);
}

TEST_F(QmMeasurementClipFailure, GuardConstructionFailureDoesNotRequireADestructorToRestoreDepth)
{
	FailNextAllocation();
	EXPECT_THROW({ CUiScopedMeasurement Measurement(&m_Ui); }, std::bad_alloc);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_EQ(Graphics().m_ClipChanges, 0);
}

TEST_F(QmMeasurementClipFailure, NullAllocationResultLeavesMeasurementInactive)
{
	m_Memory.m_ReturnNull = true;
	FailNextAllocation();
	EXPECT_THROW(m_Ui.BeginRenderOnly(), std::bad_alloc);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(Graphics().m_Clipped);
}

TEST_F(QmMeasurementClipFailure, UnknownAllocatorExceptionLeavesMeasurementInactive)
{
	m_Memory.m_ThrowUnknown = true;
	FailNextAllocation();
	EXPECT_THROW(m_Ui.BeginRenderOnly(), int);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(Graphics().m_Clipped);
}

TEST_F(QmMeasurementClipFailure, FailedGrowthRetainsParentLogicalAndDeviceClipThenRetries)
{
	ASSERT_TRUE(PrepareFullParentClipStorage());
	const CUIRect Parent = *m_Ui.ClipArea();
	const CUIRect DeviceParent = Graphics().m_ClipRect;
	const int Changes = Graphics().m_ClipChanges;
	FailNextAllocation();
	EXPECT_THROW(m_Ui.BeginRenderOnly(), std::bad_alloc);
	ASSERT_FALSE(m_Ui.MeasurementOnly());
	ASSERT_TRUE(m_Ui.IsClipped());
	ExpectRect(*m_Ui.ClipArea(), Parent);
	ExpectRect(Graphics().m_ClipRect, DeviceParent);
	EXPECT_EQ(Graphics().m_ClipChanges, Changes);
	m_Memory.m_FailAt = -1;
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_FLOAT_EQ(Graphics().m_ClipRect.w, 0);
		EXPECT_FLOAT_EQ(Graphics().m_ClipRect.h, 0);
	}
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	ExpectRect(*m_Ui.ClipArea(), Parent);
	ExpectRect(Graphics().m_ClipRect, DeviceParent);
}

TEST_F(QmMeasurementClipFailure, FailurePreservesBackgroundModeWithoutPublishingMeasurement)
{
	ASSERT_TRUE(PrepareFullParentClipStorage());
	const CUIRect Parent = *m_Ui.ClipArea();
	m_Ui.BeginBackgroundRender();
	FailNextAllocation();
	EXPECT_THROW(m_Ui.BeginRenderOnly(), std::bad_alloc);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_TRUE(m_Ui.BackgroundRender());
	EXPECT_TRUE(m_Ui.RenderOnly());
	EXPECT_FALSE(m_Ui.DrawingSuppressed());
	ExpectRect(*m_Ui.ClipArea(), Parent);
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Ui.RenderOnly());
}

TEST_F(QmMeasurementClipFailure, NestedMeasurementDoesNotAllocateOrReplaceTheOuterZeroClip)
{
	{
		CUiScopedMeasurement Outer(&m_Ui);
		const CUIRect Clip = *m_Ui.ClipArea();
		const int Allocations = m_Memory.m_Allocations;
		const int Changes = Graphics().m_ClipChanges;
		FailNextAllocation();
		{
			CUiScopedMeasurement Inner(&m_Ui);
			EXPECT_TRUE(m_Ui.MeasurementOnly());
			EXPECT_EQ(m_Memory.m_Allocations, Allocations);
			ExpectRect(*m_Ui.ClipArea(), Clip);
		}
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_EQ(Graphics().m_ClipChanges, Changes);
		ExpectRect(*m_Ui.ClipArea(), Clip);
	}
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(Graphics().m_Clipped);
}

TEST_F(QmMeasurementClipFailure, MeasuredContentExceptionRestoresParentAndBackgroundMode)
{
	ASSERT_TRUE(PrepareFullParentClipStorage());
	const CUIRect Parent = *m_Ui.ClipArea();
	const CUIRect DeviceParent = Graphics().m_ClipRect;
	m_Ui.BeginBackgroundRender();
	EXPECT_THROW(MeasureSettingsCardContent(&m_Ui, [](CUIRect &) { throw std::bad_alloc(); }, 240), std::bad_alloc);
	EXPECT_FALSE(m_Ui.MeasurementOnly());
	EXPECT_TRUE(m_Ui.BackgroundRender());
	ExpectRect(*m_Ui.ClipArea(), Parent);
	ExpectRect(Graphics().m_ClipRect, DeviceParent);
	m_Ui.EndBackgroundRender();
	EXPECT_FALSE(m_Ui.RenderOnly());
}
