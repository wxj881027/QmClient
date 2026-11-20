#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

TEST(SettingsSkinLoadViewportFailure, FailedReplacementPreservesTheCurrentFrameAndCanRetry)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	Viewport.Publish({"cat", "default"});
	ASSERT_TRUE(Viewport.Contains("cat"));

	std::vector<std::string> vNewNames{"dog", "default"};
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Viewport.Publish(std::move(vNewNames));
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(Viewport.Contains("cat"));
	EXPECT_TRUE(Viewport.Contains("default"));
	EXPECT_FALSE(Viewport.Contains("dog"));

	Viewport.Publish(std::move(vNewNames));
	EXPECT_FALSE(Viewport.Contains("cat"));
	EXPECT_TRUE(Viewport.Contains("default"));
	EXPECT_TRUE(Viewport.Contains("dog"));
}

TEST(SettingsSkinLoadViewportFailure, FailedReplacementDoesNotRepublishAnExpiredFrame)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	Viewport.Publish({"cat"});
	ASSERT_TRUE(Viewport.Contains("cat"));

	Viewport.BeginFrame();
	std::vector<std::string> vNewNames{"dog"};
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Viewport.Publish(std::move(vNewNames));
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Viewport.Contains("cat"));
	EXPECT_FALSE(Viewport.Contains("dog"));

	Viewport.Publish({"dog"});
	EXPECT_TRUE(Viewport.Contains("dog"));
}

TEST(SettingsSkinLoadViewportFailure, FailedRankAllocationDefersRequestsUntilRetry)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	Viewport.Publish({"current"});

	const std::vector<std::string> vRequests{"prefetch", "previous", "current"};
	std::vector<std::string_view> vVisited;
	vVisited.reserve(vRequests.size());
	const auto VisitBatch = [&] {
		SettingsVisitSkinLoadRequests(
			vRequests,
			[](const std::string &Name) { return Name == "prefetch" ? ESettingsResourcePriority::PREFETCH : ESettingsResourcePriority::VISIBLE; },
			[&](const std::string &Name) { return Viewport.Contains(Name); },
			[&](const std::string &Name) { vVisited.emplace_back(Name); });
	};

	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		VisitBatch();
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(vVisited.empty());

	VisitBatch();
	EXPECT_EQ(vVisited, (std::vector<std::string_view>{"current", "previous", "prefetch"}));
}
