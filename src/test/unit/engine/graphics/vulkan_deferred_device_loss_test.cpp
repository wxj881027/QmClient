#include <engine/client/backend/vulkan/backend_vulkan_qm_ext.h>

#include <gtest/gtest.h>

namespace
{
	class VulkanDeferredDeviceLoss : public ::testing::Test
	{
	protected:
		qm_vulkan_ext::CDeferredDeviceLoss m_Pending;
		qm_vulkan_ext::EEnhancedMode m_Mode = qm_vulkan_ext::EEnhancedMode::AUTO;
		qm_vulkan_ext::EDisableReason m_Reason = qm_vulkan_ext::EDisableReason::NONE;
		bool m_SessionDisabled = false;
		bool m_ReportDuringMode = false;
		bool m_ReportDuringDisable = false;
		int m_ModeReads = 0;
		int m_Disables = 0;

		bool Consume()
		{
			return m_Pending.ConsumeAfterRecording(m_SessionDisabled, [&]() noexcept {
				++m_ModeReads;
				if(m_ReportDuringMode)
				{
					m_ReportDuringMode = false;
					m_Pending.Report();
				}
				return m_Mode; }, [&](qm_vulkan_ext::EDisableReason Reason) noexcept {
				++m_Disables;
				m_Reason = Reason;
				if(m_ReportDuringDisable)
				{
					m_ReportDuringDisable = false;
					m_Pending.Report();
				} });
		}
	};
}

TEST_F(VulkanDeferredDeviceLoss, EmptyConsumptionDoesNotReadModeOrDisable)
{
	EXPECT_FALSE(Consume());
	EXPECT_EQ(m_ModeReads, 0);
	EXPECT_EQ(m_Disables, 0);
}

TEST_F(VulkanDeferredDeviceLoss, RepeatedReportsDisableAutoOnceWhenConsumed)
{
	m_Pending.Report();
	m_Pending.Report();
	EXPECT_EQ(m_ModeReads, 0);
	EXPECT_EQ(m_Disables, 0);
	EXPECT_TRUE(Consume());
	EXPECT_EQ(m_Reason, qm_vulkan_ext::EDisableReason::DEVICE_LOST);
	EXPECT_FALSE(Consume());
	EXPECT_EQ(m_ModeReads, 1);
	EXPECT_EQ(m_Disables, 1);
}

TEST_F(VulkanDeferredDeviceLoss, ExplicitModesConsumeReportWithoutDisabling)
{
	for(const auto Mode : {qm_vulkan_ext::EEnhancedMode::OFF, qm_vulkan_ext::EEnhancedMode::ON})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		m_Mode = Mode;
		m_Pending.Report();
		EXPECT_FALSE(Consume());
		m_Mode = qm_vulkan_ext::EEnhancedMode::AUTO;
		EXPECT_FALSE(Consume());
	}
	EXPECT_EQ(m_ModeReads, 2);
	EXPECT_EQ(m_Disables, 0);
}

TEST_F(VulkanDeferredDeviceLoss, DisabledSessionConsumesReportWithoutReadingMode)
{
	m_SessionDisabled = true;
	m_Pending.Report();
	EXPECT_FALSE(Consume());
	m_SessionDisabled = false;
	EXPECT_FALSE(Consume());
	EXPECT_EQ(m_ModeReads, 0);
	EXPECT_EQ(m_Disables, 0);
}

TEST_F(VulkanDeferredDeviceLoss, ReportDuringModeReadRemainsForNextConsumption)
{
	m_ReportDuringMode = true;
	m_Pending.Report();
	EXPECT_TRUE(Consume());
	EXPECT_TRUE(Consume());
	EXPECT_FALSE(Consume());
	EXPECT_EQ(m_ModeReads, 2);
	EXPECT_EQ(m_Disables, 2);
}

TEST_F(VulkanDeferredDeviceLoss, ReportDuringDisableRemainsForNextConsumption)
{
	m_ReportDuringDisable = true;
	m_Pending.Report();
	EXPECT_TRUE(Consume());
	EXPECT_TRUE(Consume());
	EXPECT_FALSE(Consume());
	EXPECT_EQ(m_ModeReads, 2);
	EXPECT_EQ(m_Disables, 2);
}
