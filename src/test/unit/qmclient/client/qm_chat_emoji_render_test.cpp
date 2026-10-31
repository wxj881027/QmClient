#include <game/client/components/qmclient/chat_emoji_render.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <limits>
#include <vector>

namespace
{
	constexpr float Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float Inf = std::numeric_limits<float>::infinity();
	constexpr float Max = std::numeric_limits<float>::max();
	enum class ECall
	{
		TEXTURE,
		BEGIN,
		SUBSET,
		ROTATION,
		COLOR,
		DRAW,
		END
	};
	class CGraphics : public CIconBenchmarkGraphics
	{
	public:
		using CIconBenchmarkGraphics::SetColor;
		using IGraphics::CreateTextureHandle;
		std::vector<ECall> m_vCalls;
		std::vector<ColorRGBA> m_vColors;
		CQuadItem m_Quad{};
		CTextureHandle m_Texture;
		CTextureHandle m_DrawTexture;
		std::array<float, 4> m_aSubset{};
		float m_Rotation = 1;
		bool m_FailDraw = false;
		void TextureSet(CTextureHandle Texture) override
		{
			m_vCalls.push_back(ECall::TEXTURE);
			m_Texture = Texture;
		}
		void QuadsBegin() override { m_vCalls.push_back(ECall::BEGIN); }
		void QuadsEnd() override { m_vCalls.push_back(ECall::END); }
		void QuadsSetSubset(float U0, float V0, float U1, float V1) override
		{
			m_vCalls.push_back(ECall::SUBSET);
			m_aSubset = {U0, V0, U1, V1};
		}
		void QuadsSetRotation(float Angle) override
		{
			m_vCalls.push_back(ECall::ROTATION);
			m_Rotation = Angle;
		}
		void SetColor(ColorRGBA Color) override
		{
			m_vCalls.push_back(ECall::COLOR);
			m_vColors.push_back(Color);
		}
		void QuadsDraw(CQuadItem *pItems, int Count) override
		{
			m_vCalls.push_back(ECall::DRAW);
			if(m_FailDraw)
				throw std::bad_alloc();
			EXPECT_EQ(Count, 1);
			m_DrawTexture = m_Texture;
			m_Quad = pItems[0];
		}
	};
}

TEST(QmChatEmojiRender, NormalDrawPreservesCenterSubsetAndRestoresDefaultState)
{
	CGraphics Graphics;
	ASSERT_TRUE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7, 123), 10, 20, 30, 40, 0.6f));
	EXPECT_EQ(Graphics.m_DrawTexture.Id(), 7);
	EXPECT_EQ(Graphics.m_DrawTexture.Generation(), 123u);
	EXPECT_EQ(Graphics.m_Quad.m_X, 25);
	EXPECT_EQ(Graphics.m_Quad.m_Y, 40);
	EXPECT_EQ(Graphics.m_Quad.m_Width, 30);
	EXPECT_EQ(Graphics.m_Quad.m_Height, 40);
	EXPECT_EQ(Graphics.m_aSubset, (std::array<float, 4>{0, 0, 1, 1}));
	EXPECT_EQ(Graphics.m_Rotation, 0);
	ASSERT_EQ(Graphics.m_vColors.size(), 2u);
	EXPECT_EQ(Graphics.m_vColors[0], ColorRGBA(1, 1, 1, 0.6f));
	EXPECT_EQ(Graphics.m_vColors[1], ui_token::color::NEUTRAL_WHITE);
	EXPECT_FALSE(Graphics.m_Texture.IsValid());
	const std::vector<ECall> Expected{ECall::TEXTURE, ECall::BEGIN, ECall::SUBSET, ECall::ROTATION, ECall::COLOR, ECall::DRAW, ECall::END, ECall::TEXTURE, ECall::COLOR};
	EXPECT_EQ(Graphics.m_vCalls, Expected);
}

TEST(QmChatEmojiRender, FiniteAlphaAboveOneRetainsTheExistingClamp)
{
	CGraphics Graphics;
	ASSERT_TRUE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, 40, Max));
	EXPECT_EQ(Graphics.m_vColors[0].a, 1);
}

TEST(QmChatEmojiRender, NegativeScreenCoordinatesRemainValid)
{
	CGraphics Graphics;
	ASSERT_TRUE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), -100, -80, 30, 40, 1));
	EXPECT_EQ(Graphics.m_Quad.m_X, -85);
	EXPECT_EQ(Graphics.m_Quad.m_Y, -60);
}

TEST(QmChatEmojiRender, SmallPositiveSizeHasNoNewArbitraryMinimum)
{
	CGraphics Graphics;
	ASSERT_TRUE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 0, 0, 0.001f, 0.002f, 0.001f));
	EXPECT_EQ(Graphics.m_Quad.m_Width, 0.001f);
	EXPECT_EQ(Graphics.m_Quad.m_Height, 0.002f);
}

TEST(QmChatEmojiRender, NanAlphaDoesNotReachColorConversionOrBeginABatch)
{
	CGraphics Graphics;
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, 40, Nan));
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, InfiniteAlphaDoesNotChangeGraphicsState)
{
	CGraphics Graphics;
	for(float Alpha : {Inf, -Inf})
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, 40, Alpha));
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, NonfinitePositionDoesNotChangeGraphicsState)
{
	CGraphics Graphics;
	for(float Value : {Nan, Inf, -Inf})
	{
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), Value, 20, 30, 40, 1));
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, Value, 30, 40, 1));
	}
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, NonfiniteSizeDoesNotChangeGraphicsState)
{
	CGraphics Graphics;
	for(float Value : {Nan, Inf, -Inf})
	{
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, Value, 40, 1));
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, Value, 1));
	}
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, NonpositiveSizeAndAlphaRetainTheExistingSkip)
{
	CGraphics Graphics;
	for(float Value : {0.0f, -1.0f})
	{
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, Value, 40, 1));
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, Value, 1));
		EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, 40, Value));
	}
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, FiniteInputsWithOverflowingCenterAreRejectedBeforeBinding)
{
	CGraphics Graphics;
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), Max, 20, Max, 40, 1));
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, Max, 30, Max, 1));
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, FiniteCenterWithOverflowingNativeCornerIsRejectedBeforeBinding)
{
	CGraphics Graphics;
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), Max * 0.5f, 20, Max, 40, 1));
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, Max * 0.5f, 30, Max, 1));
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, InvalidTextureDoesNotBeginABatch)
{
	CGraphics Graphics;
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, {}, 10, 20, 30, 40, 1));
	EXPECT_TRUE(Graphics.m_vCalls.empty());
}

TEST(QmChatEmojiRender, RejectedFrameDoesNotPreventASubsequentValidDraw)
{
	CGraphics Graphics;
	const auto Texture = Graphics.CreateTextureHandle(7);
	EXPECT_FALSE(QmRenderChatEmoji(Graphics, Texture, Nan, 20, 30, 40, 1));
	EXPECT_TRUE(QmRenderChatEmoji(Graphics, Texture, 10, 20, 30, 40, 1));
	EXPECT_EQ(Graphics.m_Quad.m_X, 25);
}

TEST(QmChatEmojiRender, NativeBoundaryFailureStillPropagates)
{
	CGraphics Graphics;
	Graphics.m_FailDraw = true;
	EXPECT_THROW(QmRenderChatEmoji(Graphics, Graphics.CreateTextureHandle(7), 10, 20, 30, 40, 1), std::bad_alloc);
	EXPECT_EQ(Graphics.m_vCalls.back(), ECall::DRAW);
}
