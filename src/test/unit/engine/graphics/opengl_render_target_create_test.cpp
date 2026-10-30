#include <base/borrowed_allocator.h>

#include <engine/client/backend/opengl/render_target_create.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <array>
#include <limits>
#include <vector>

namespace
{
	using STarget = opengl_render_target::STarget;
	using SBindings = opengl_render_target::SBindings;
	constexpr STarget OLD{5, 7, 32, 16};

	class COperations : public opengl_render_target::IOperations
	{
	public:
		SBindings m_Bindings{40, 41, 42};
		bool m_FailTexture = false;
		bool m_FailFramebuffer = false;
		bool m_Complete = true;
		int m_Snapshots = 0, m_TextureCreates = 0, m_FramebufferCreates = 0, m_Checks = 0, m_Restores = 0;
		int m_FramebufferDeletes = 0, m_TextureDeletes = 0;
		std::array<TWGLuint, 8> m_aDeletedFramebuffers{}, m_aDeletedTextures{};
		SBindings GetBindings() noexcept override
		{
			++m_Snapshots;
			return m_Bindings;
		}
		TWGLuint CreateTexture(int, int) noexcept override
		{
			++m_TextureCreates;
			if(m_FailTexture)
				return 0;
			m_Bindings.m_Texture = 21;
			return 21;
		}
		TWGLuint CreateFramebuffer(TWGLuint) noexcept override
		{
			++m_FramebufferCreates;
			if(m_FailFramebuffer)
				return 0;
			m_Bindings.m_DrawFramebuffer = m_Bindings.m_ReadFramebuffer = 22;
			return 22;
		}
		bool IsFramebufferComplete() noexcept override
		{
			++m_Checks;
			return m_Complete;
		}
		void RestoreBindings(const SBindings &Bindings) noexcept override
		{
			++m_Restores;
			m_Bindings = Bindings;
		}
		void DeleteFramebuffer(TWGLuint Id) noexcept override
		{
			m_aDeletedFramebuffers[m_FramebufferDeletes++] = Id;
			if(m_Bindings.m_DrawFramebuffer == Id)
				m_Bindings.m_DrawFramebuffer = 0;
			if(m_Bindings.m_ReadFramebuffer == Id)
				m_Bindings.m_ReadFramebuffer = 0;
		}
		void DeleteTexture(TWGLuint Id) noexcept override
		{
			m_aDeletedTextures[m_TextureDeletes++] = Id;
			if(m_Bindings.m_Texture == Id)
				m_Bindings.m_Texture = 0;
		}
	};

	class OpenGLRenderTargetCreate : public ::testing::Test
	{
	protected:
		COperations m_Operations;
		std::vector<STarget> m_vTargets{OLD};
		void ExpectOld()
		{
			ASSERT_EQ(m_vTargets.size(), 1u);
			EXPECT_EQ(m_vTargets[0].m_Framebuffer, OLD.m_Framebuffer);
			EXPECT_EQ(m_vTargets[0].m_Texture, OLD.m_Texture);
			EXPECT_EQ(m_vTargets[0].m_Width, OLD.m_Width);
			EXPECT_EQ(m_vTargets[0].m_Height, OLD.m_Height);
		}
		void ExpectOriginalBindings()
		{
			EXPECT_EQ(m_Operations.m_Bindings.m_DrawFramebuffer, 40u);
			EXPECT_EQ(m_Operations.m_Bindings.m_ReadFramebuffer, 41u);
			EXPECT_EQ(m_Operations.m_Bindings.m_Texture, 42u);
		}
	};

	class OpenGLRenderTargetAllocation : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_Memory;
		COperations m_Operations;
		std::vector<STarget, CBorrowedAllocator<STarget>> m_vTargets{CBorrowedAllocator<STarget>{&m_Memory}};
		int m_LiveBeforeGrow = 0;
		void SetUp() override
		{
			m_vTargets.push_back(OLD);
			ASSERT_LT(m_vTargets.capacity(), static_cast<size_t>(std::numeric_limits<int>::max()));
			m_LiveBeforeGrow = m_Memory.m_Live;
			m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
		}
		int GrowthIndex() const { return static_cast<int>(m_vTargets.capacity()); }
		void ExpectUnchanged()
		{
			ASSERT_EQ(m_vTargets.size(), 1u);
			EXPECT_EQ(m_vTargets[0].m_Texture, OLD.m_Texture);
			EXPECT_EQ(m_vTargets[0].m_Framebuffer, OLD.m_Framebuffer);
			EXPECT_EQ(m_Operations.m_Snapshots, 0);
			EXPECT_EQ(m_Operations.m_TextureCreates, 0);
			EXPECT_EQ(m_Memory.m_Live, m_LiveBeforeGrow);
		}
	};
}

TEST_F(OpenGLRenderTargetCreate, NewTargetPreservesIndependentReadDrawAndTextureBindings)
{
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 1, 64, 48, m_Operations));
	ASSERT_EQ(m_vTargets.size(), 2u);
	EXPECT_EQ(m_vTargets[1].m_Framebuffer, 22u);
	EXPECT_EQ(m_vTargets[1].m_Texture, 21u);
	EXPECT_EQ(m_vTargets[1].m_Width, 64);
	EXPECT_EQ(m_vTargets[1].m_Height, 48);
	ExpectOriginalBindings();
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 0);
	EXPECT_EQ(m_Operations.m_TextureDeletes, 0);
}

TEST_F(OpenGLRenderTargetCreate, SuccessfulReplacementReleasesOnlyThePreviousNativeObjects)
{
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	EXPECT_EQ(m_vTargets[0].m_Framebuffer, 22u);
	EXPECT_EQ(m_vTargets[0].m_Texture, 21u);
	EXPECT_EQ(m_Operations.m_Checks, 1);
	ASSERT_EQ(m_Operations.m_FramebufferDeletes, 1);
	ASSERT_EQ(m_Operations.m_TextureDeletes, 1);
	EXPECT_EQ(m_Operations.m_aDeletedFramebuffers[0], OLD.m_Framebuffer);
	EXPECT_EQ(m_Operations.m_aDeletedTextures[0], OLD.m_Texture);
	ExpectOriginalBindings();
}

TEST_F(OpenGLRenderTargetCreate, ZeroTextureKeepsThePreviousTargetWithoutCreatingAFramebuffer)
{
	m_Operations.m_FailTexture = true;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ExpectOld();
	ExpectOriginalBindings();
	EXPECT_EQ(m_Operations.m_FramebufferCreates, 0);
	EXPECT_EQ(m_Operations.m_TextureDeletes, 0);
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 0);
}

TEST_F(OpenGLRenderTargetCreate, ZeroFramebufferReleasesTheCandidateTextureAndSkipsDefaultFramebufferValidation)
{
	m_Operations.m_FailFramebuffer = true;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ExpectOld();
	ExpectOriginalBindings();
	EXPECT_EQ(m_Operations.m_Checks, 0);
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 0);
	ASSERT_EQ(m_Operations.m_TextureDeletes, 1);
	EXPECT_EQ(m_Operations.m_aDeletedTextures[0], 21u);
}

TEST_F(OpenGLRenderTargetCreate, IncompleteFramebufferReleasesBothCandidatesAndKeepsThePreviousTarget)
{
	m_Operations.m_Complete = false;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ExpectOld();
	ExpectOriginalBindings();
	ASSERT_EQ(m_Operations.m_FramebufferDeletes, 1);
	ASSERT_EQ(m_Operations.m_TextureDeletes, 1);
	EXPECT_EQ(m_Operations.m_aDeletedFramebuffers[0], 22u);
	EXPECT_EQ(m_Operations.m_aDeletedTextures[0], 21u);
}

TEST_F(OpenGLRenderTargetCreate, ReplacingTheCurrentlyBoundDrawTargetIsRejectedBeforeNativeCreation)
{
	m_Operations.m_Bindings.m_DrawFramebuffer = OLD.m_Framebuffer;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Bindings.m_DrawFramebuffer, OLD.m_Framebuffer);
	EXPECT_EQ(m_Operations.m_TextureCreates, 0);
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 0);
}

TEST_F(OpenGLRenderTargetCreate, ReplacingTheCurrentlyBoundReadTargetIsRejectedBeforeNativeCreation)
{
	m_Operations.m_Bindings.m_ReadFramebuffer = OLD.m_Framebuffer;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Bindings.m_ReadFramebuffer, OLD.m_Framebuffer);
	EXPECT_EQ(m_Operations.m_TextureCreates, 0);
}

TEST_F(OpenGLRenderTargetCreate, ReplacingTheBoundOldTextureRestoresTheNewLogicalTargetsTexture)
{
	m_Operations.m_Bindings.m_Texture = OLD.m_Texture;
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	EXPECT_EQ(m_Operations.m_Bindings.m_Texture, 21u);
	EXPECT_EQ(m_Operations.m_Bindings.m_DrawFramebuffer, 40u);
	EXPECT_EQ(m_Operations.m_Bindings.m_ReadFramebuffer, 41u);
}

TEST_F(OpenGLRenderTargetCreate, InvalidIndexOrDimensionsDoNotTouchPoolOrDevice)
{
	const std::array<std::array<int, 3>, 3> aCases{{{-1, 64, 48}, {0, 0, 48}, {0, 64, -1}}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case[0]);
		EXPECT_FALSE(opengl_render_target::Create(m_vTargets, Case[0], Case[1], Case[2], m_Operations));
	}
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Snapshots, 0);
}

TEST_F(OpenGLRenderTargetCreate, FailedNewSlotRestoresThePreviousPoolSizeAndOtherTargets)
{
	m_Operations.m_Complete = false;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, 3, 64, 48, m_Operations));
	ExpectOld();
	ExpectOriginalBindings();
	EXPECT_EQ(m_Operations.m_TextureDeletes, 1);
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 1);
}

TEST_F(OpenGLRenderTargetCreate, FailedReplacementCanRetryWithoutDestroyingThePreviousObjectsTwice)
{
	m_Operations.m_Complete = false;
	ASSERT_FALSE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	m_Operations.m_Complete = true;
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	ASSERT_EQ(m_Operations.m_FramebufferDeletes, 2);
	ASSERT_EQ(m_Operations.m_TextureDeletes, 2);
	EXPECT_EQ(m_Operations.m_aDeletedFramebuffers[1], OLD.m_Framebuffer);
	EXPECT_EQ(m_Operations.m_aDeletedTextures[1], OLD.m_Texture);
	EXPECT_EQ(m_vTargets[0].m_Width, 64);
}

TEST_F(OpenGLRenderTargetCreate, CreatingFromDefaultBindingsRestoresZeroInsteadOfLeavingTheCandidateBound)
{
	m_Operations.m_Bindings = {};
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 1, 64, 48, m_Operations));
	EXPECT_EQ(m_Operations.m_Bindings.m_DrawFramebuffer, 0u);
	EXPECT_EQ(m_Operations.m_Bindings.m_ReadFramebuffer, 0u);
	EXPECT_EQ(m_Operations.m_Bindings.m_Texture, 0u);
}

TEST_F(OpenGLRenderTargetCreate, CreatingAnotherSlotDuringAnExistingPassPreservesItsBoundTarget)
{
	m_Operations.m_Bindings = {OLD.m_Framebuffer, 41, OLD.m_Texture};
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 1, 64, 48, m_Operations));
	EXPECT_EQ(m_vTargets[0].m_Framebuffer, OLD.m_Framebuffer);
	EXPECT_EQ(m_vTargets[0].m_Width, OLD.m_Width);
	EXPECT_EQ(m_Operations.m_Bindings.m_DrawFramebuffer, OLD.m_Framebuffer);
	EXPECT_EQ(m_Operations.m_Bindings.m_ReadFramebuffer, 41u);
	EXPECT_EQ(m_Operations.m_Bindings.m_Texture, OLD.m_Texture);
}

TEST_F(OpenGLRenderTargetAllocation, FailedPoolGrowthLeavesOldStateWithoutCallingTheDevice)
{
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, GrowthIndex(), 64, 48, m_Operations));
	ExpectUnchanged();
}

TEST_F(OpenGLRenderTargetAllocation, NullPoolAllocationReturnsFailureWithoutCallingTheDevice)
{
	m_Memory.m_ReturnNull = true;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, GrowthIndex(), 64, 48, m_Operations));
	ExpectUnchanged();
}

TEST_F(OpenGLRenderTargetAllocation, UnknownPoolAllocationExceptionReturnsFailureWithoutCallingTheDevice)
{
	m_Memory.m_ThrowUnknown = true;
	EXPECT_FALSE(opengl_render_target::Create(m_vTargets, GrowthIndex(), 64, 48, m_Operations));
	ExpectUnchanged();
}

TEST_F(OpenGLRenderTargetAllocation, ExistingSlotReplacementDoesNotAllocateAnotherPoolBuffer)
{
	const int Allocations = m_Memory.m_Allocations;
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, 0, 64, 48, m_Operations));
	EXPECT_EQ(m_Memory.m_Allocations, Allocations);
	EXPECT_EQ(m_vTargets[0].m_Framebuffer, 22u);
}

TEST_F(OpenGLRenderTargetAllocation, FailedPoolGrowthCanRetryWithoutLosingTheExistingTarget)
{
	const int Index = GrowthIndex();
	ASSERT_FALSE(opengl_render_target::Create(m_vTargets, Index, 64, 48, m_Operations));
	ExpectUnchanged();
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(opengl_render_target::Create(m_vTargets, Index, 64, 48, m_Operations));
	EXPECT_EQ(m_vTargets[0].m_Framebuffer, OLD.m_Framebuffer);
	EXPECT_EQ(m_vTargets[0].m_Texture, OLD.m_Texture);
	EXPECT_EQ(m_vTargets[Index].m_Width, 64);
	EXPECT_EQ(m_Operations.m_FramebufferDeletes, 0);
	EXPECT_EQ(m_Operations.m_TextureDeletes, 0);
}
