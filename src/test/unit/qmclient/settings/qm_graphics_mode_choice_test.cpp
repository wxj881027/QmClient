#include <game/client/QmUi/cards/QmCardCatalogInternal.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

using qm_card_catalog::HasGraphicsModeChoice;

TEST(QmGraphicsModeChoice, AutomaticOpenGlKeepsTheSelectorAvailableWithoutAllocating)
{
	const auto HasDriver = [](EBackendType Backend, EGraphicsDriverAgeType Age) {
		return Backend == BACKEND_TYPE_OPENGL && Age == GRAPHICS_DRIVER_AGE_TYPE_LEGACY;
	};
	bool Available;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure(0);
		Available = HasGraphicsModeChoice(false, HasDriver);
		AllocationFailed = Failure.Triggered();
	}

	EXPECT_TRUE(Available);
	EXPECT_FALSE(AllocationFailed);
	EXPECT_FALSE(HasGraphicsModeChoice(true, HasDriver));
}

TEST(QmGraphicsModeChoice, BlockedOpenGlKeepsOnlyLegacyAndNeedsASecondCandidate)
{
	const auto OpenGlOnly = [](EBackendType Backend, EGraphicsDriverAgeType) { return Backend == BACKEND_TYPE_OPENGL; };
	const auto WithVulkan = [](EBackendType Backend, EGraphicsDriverAgeType Age) {
		return Backend == BACKEND_TYPE_OPENGL || (Backend == BACKEND_TYPE_VULKAN && Age == GRAPHICS_DRIVER_AGE_TYPE_DEFAULT);
	};

	EXPECT_FALSE(HasGraphicsModeChoice(true, OpenGlOnly));
	EXPECT_TRUE(HasGraphicsModeChoice(true, WithVulkan));
}

TEST(QmGraphicsModeChoice, SingleVulkanCandidateDoesNotShowTheSelector)
{
	const auto VulkanOnly = [](EBackendType Backend, EGraphicsDriverAgeType Age) {
		return Backend == BACKEND_TYPE_VULKAN && Age == GRAPHICS_DRIVER_AGE_TYPE_DEFAULT;
	};

	EXPECT_FALSE(HasGraphicsModeChoice(false, VulkanOnly));
}
