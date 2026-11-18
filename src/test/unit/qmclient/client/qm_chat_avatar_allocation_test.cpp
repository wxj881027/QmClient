#include <game/client/components/qmclient/qm_chat_avatar.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <memory>

TEST(QmChatAvatarAllocation, FailedSnapshotLeavesExportAvatarUnsetAndAllowsRetry)
{
	CTeeRenderInfo Info;
	const auto pSkin = std::make_shared<QmChatAvatar::SSource>();
	Info.m_OriginalRenderSkin.m_QmChatAvatar = pSkin;

	std::shared_ptr<const QmChatAvatar::SSnapshot> pSnapshot;
	bool FailedAllocation;
	{
		CPredictionAllocationFailure Fault;
		pSnapshot = QmChatAvatar::Capture(Info);
		FailedAllocation = Fault.Triggered();
	}
	EXPECT_TRUE(FailedAllocation);
	EXPECT_EQ(pSnapshot, nullptr);

	pSnapshot = QmChatAvatar::Capture(Info);
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_EQ(pSnapshot->m_apParts[protocol7::SKINPART_BODY], pSkin);
}
