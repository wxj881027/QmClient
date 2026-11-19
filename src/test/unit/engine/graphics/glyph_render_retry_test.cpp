#include <engine/client/glyph_render_retry.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
	struct SGlyph
	{
		EQmGlyphState m_State = EQmGlyphState::UNINITIALIZED;
		int m_Character = 'A';
	};

	class GlyphRenderRetry : public ::testing::Test
	{
	protected:
		CQmGlyphRenderRetry m_Retry;
		SGlyph m_Glyph;
		SGlyph m_Replacement{EQmGlyphState::RENDERED, 0x25a1};
		EQmGlyphRenderResult m_Result = EQmGlyphRenderResult::RETRY;
		int m_RenderCalls = 0;

		const SGlyph *Resolve(std::chrono::nanoseconds Now = 0ns)
		{
			return m_Retry.Resolve(m_Glyph, Now, [&]() {
				++m_RenderCalls;
				return m_Result; }, [&]() { return &m_Replacement; });
		}
	};
}

TEST_F(GlyphRenderRetry, SuccessfulGlyphIsNotRasterizedAgain)
{
	m_Result = EQmGlyphRenderResult::SUCCESS;
	EXPECT_EQ(Resolve(), &m_Glyph);
	EXPECT_EQ(Resolve(), &m_Glyph);
	EXPECT_EQ(m_RenderCalls, 1);
	EXPECT_EQ(m_Glyph.m_State, EQmGlyphState::RENDERED);
	EXPECT_FALSE(m_Retry.Blocked());
}

TEST_F(GlyphRenderRetry, TransientFailureBorrowsReplacementWithoutOverwritingIdentity)
{
	EXPECT_EQ(Resolve(), &m_Replacement);
	EXPECT_EQ(m_Glyph.m_Character, 'A');
	EXPECT_EQ(m_Glyph.m_State, EQmGlyphState::RETRY);
	EXPECT_EQ(m_Replacement.m_State, EQmGlyphState::RENDERED);
	EXPECT_TRUE(m_Retry.Blocked());
}

TEST_F(GlyphRenderRetry, RepeatedLabelsDoNotRetryOrPostponeDeadline)
{
	Resolve();
	Resolve(500ms);
	Resolve(999ms);
	EXPECT_EQ(m_RenderCalls, 1);
	EXPECT_FALSE(m_Retry.BeginRetry(999ms));
	EXPECT_TRUE(m_Retry.BeginRetry(1s));
	EXPECT_FALSE(m_Retry.BeginRetry(1s));
}

TEST_F(GlyphRenderRetry, RecoveryReturnsOriginalGlyphAndCachesSuccess)
{
	Resolve();
	ASSERT_TRUE(m_Retry.BeginRetry(1s));
	m_Result = EQmGlyphRenderResult::SUCCESS;
	EXPECT_EQ(Resolve(1s), &m_Glyph);
	EXPECT_EQ(Resolve(2s), &m_Glyph);
	EXPECT_EQ(m_RenderCalls, 2);
	EXPECT_FALSE(m_Retry.BeginRetry(3s));
}

TEST_F(GlyphRenderRetry, PersistentFailureStartsAnotherBoundedCooldown)
{
	Resolve();
	ASSERT_TRUE(m_Retry.BeginRetry(1s));
	Resolve(1s);
	Resolve(1500ms);
	EXPECT_EQ(m_RenderCalls, 2);
	EXPECT_FALSE(m_Retry.BeginRetry(1999ms));
	EXPECT_TRUE(m_Retry.BeginRetry(2s));
}

TEST_F(GlyphRenderRetry, UnsupportedGlyphKeepsFallbackWithoutRepeatedRasterization)
{
	m_Result = EQmGlyphRenderResult::UNSUPPORTED;
	EXPECT_EQ(Resolve(), &m_Replacement);
	EXPECT_EQ(Resolve(2s), &m_Replacement);
	EXPECT_EQ(m_Glyph.m_State, EQmGlyphState::ERROR);
	EXPECT_EQ(m_RenderCalls, 1);
	EXPECT_FALSE(m_Retry.Blocked());
	EXPECT_FALSE(m_Retry.BeginRetry(3s));
}

TEST_F(GlyphRenderRetry, NewGlyphSharesCooldownAndCanRecoverLater)
{
	Resolve();
	SGlyph Other{EQmGlyphState::UNINITIALIZED, 'B'};
	int Calls = 0;
	const auto Render = [&]() { ++Calls; return EQmGlyphRenderResult::SUCCESS; };
	const auto Fallback = [&]() { return &m_Replacement; };
	EXPECT_EQ(m_Retry.Resolve(Other, 500ms, Render, Fallback), &m_Replacement);
	EXPECT_EQ(Calls, 0);
	ASSERT_TRUE(m_Retry.BeginRetry(1s));
	EXPECT_EQ(m_Retry.Resolve(Other, 1s, Render, Fallback), &Other);
	EXPECT_EQ(Calls, 1);
}

TEST_F(GlyphRenderRetry, RenderedGlyphRemainsUsableDuringCooldown)
{
	Resolve();
	int Calls = 0;
	EXPECT_EQ(m_Retry.Resolve(m_Replacement, 500ms, [&]() { ++Calls; return EQmGlyphRenderResult::RETRY; }, []() -> const SGlyph * { return nullptr; }), &m_Replacement);
	EXPECT_EQ(Calls, 0);
}

TEST_F(GlyphRenderRetry, AllocationExceptionRetainsRetryableIdentity)
{
	const SGlyph *pResult = nullptr;
	EXPECT_NO_THROW(pResult = m_Retry.Resolve(m_Glyph, 0ns, []() -> EQmGlyphRenderResult { throw std::bad_alloc(); }, [&]() { return &m_Replacement; }));
	EXPECT_EQ(pResult, &m_Replacement);
	EXPECT_EQ(m_Glyph.m_State, EQmGlyphState::RETRY);
	EXPECT_EQ(m_Glyph.m_Character, 'A');
	ASSERT_TRUE(m_Retry.BeginRetry(1s));
	m_Result = EQmGlyphRenderResult::SUCCESS;
	EXPECT_EQ(Resolve(1s), &m_Glyph);
}

TEST_F(GlyphRenderRetry, MissingReplacementDoesNotMakeTransientFailurePermanent)
{
	EXPECT_EQ(m_Retry.Resolve(m_Glyph, 0ns, []() { return EQmGlyphRenderResult::RETRY; }, []() -> const SGlyph * { return nullptr; }), nullptr);
	ASSERT_TRUE(m_Retry.BeginRetry(1s));
	m_Result = EQmGlyphRenderResult::SUCCESS;
	EXPECT_EQ(Resolve(1s), &m_Glyph);
}

TEST_F(GlyphRenderRetry, ResetCancelsPendingRetryForDiscardedAtlas)
{
	Resolve();
	m_Retry.Reset();
	EXPECT_FALSE(m_Retry.Blocked());
	EXPECT_FALSE(m_Retry.BeginRetry(2s));
	m_Glyph = SGlyph();
	m_Result = EQmGlyphRenderResult::SUCCESS;
	EXPECT_EQ(Resolve(), &m_Glyph);
}

TEST_F(GlyphRenderRetry, RepeatedFailureNotificationDoesNotDelayRetry)
{
	m_Retry.Failed(0ns);
	m_Retry.Failed(900ms);
	EXPECT_TRUE(m_Retry.BeginRetry(1s));
}
