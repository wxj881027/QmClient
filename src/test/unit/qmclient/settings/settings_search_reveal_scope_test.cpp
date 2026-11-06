#include <game/client/QmUi/SettingsSearchReveal.h>

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace
{
	class SettingsSearchRevealScope : public testing::Test
	{
	protected:
		void SetUp() override
		{
			qm_settings_reveal::Clear();
			qm_settings_reveal::Request("card", {"key"});
			// 此处只检查卡片持有期，不依赖五秒定位请求期限。
			qm_settings_reveal::State().m_ExpireSeconds = std::numeric_limits<double>::infinity();
		}
		void TearDown() override { qm_settings_reveal::Clear(); }
	};
}

TEST_F(SettingsSearchRevealScope, CompletedCardReleasesBorrowedKeysAndKeepsTheRequest)
{
	{
		const qm_settings_reveal::CCardScope Scope("card", nullptr, true);
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal("key", nullptr));
	}
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("key", nullptr));
	EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
	EXPECT_TRUE(qm_settings_reveal::Pending());
}

TEST_F(SettingsSearchRevealScope, DrawingExceptionReleasesCardStateAndAllowsNextDraw)
{
	EXPECT_THROW(([]() {
		const qm_settings_reveal::CCardScope Scope("card", nullptr, true);
		if(!qm_settings_reveal::ShouldReveal("key", nullptr))
			throw 7;
		throw std::runtime_error("draw failed");
	}()),
		std::runtime_error);
	EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
	EXPECT_FALSE(qm_settings_reveal::State().m_Interactive);
	const qm_settings_reveal::CCardScope Retry("card", nullptr, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("key", nullptr));
}
