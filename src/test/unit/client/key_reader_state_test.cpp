#include <game/client/components/key_binder.h>

#include <gtest/gtest.h>

TEST(KeyReaderState, CancellingAHiddenReaderStopsInterceptingInput)
{
	CButtonContainer Reader;
	CButtonContainer OtherReader;
	CKeyReaderState State;
	State.Start(&Reader);
	ASSERT_TRUE(State.IsCapturing());
	ASSERT_TRUE(State.HasPendingInput());

	EXPECT_TRUE(State.Abort());
	EXPECT_FALSE(State.IsCapturing());
	EXPECT_FALSE(State.HasPendingInput());
	EXPECT_FALSE(State.Abort());
	EXPECT_FALSE(State.TakeResult(&OtherReader));

	const auto Result = State.TakeResult(&Reader);
	ASSERT_TRUE(Result);
	EXPECT_EQ(Result->m_Key, KEY_ESCAPE);
	EXPECT_FALSE(State.TakeResult(&Reader));
}

TEST(KeyReaderState, CancellingBeforeDeliveryDiscardsTheCapturedBinding)
{
	CButtonContainer Reader;
	CKeyReaderState State;
	State.Start(&Reader);
	State.Capture({KEY_A, KeyModifier::NONE});
	ASSERT_FALSE(State.IsCapturing());
	ASSERT_TRUE(State.HasPendingInput());

	ASSERT_TRUE(State.Abort());
	const auto Result = State.TakeResult(&Reader);
	ASSERT_TRUE(Result);
	EXPECT_EQ(Result->m_Key, KEY_ESCAPE);
}

TEST(KeyReaderState, AnotherReaderCanCaptureWithoutInheritingCancellation)
{
	CButtonContainer Reader;
	CButtonContainer OtherReader;
	CKeyReaderState State;
	State.Start(&Reader);
	ASSERT_TRUE(State.Abort());

	State.Start(&OtherReader);
	EXPECT_TRUE(State.IsCapturing());
	State.Capture({KEY_A, KeyModifier::NONE});
	EXPECT_FALSE(State.TakeResult(&Reader));
	const auto Result = State.TakeResult(&OtherReader);
	ASSERT_TRUE(Result);
	EXPECT_EQ(Result->m_Key, KEY_A);
	EXPECT_FALSE(State.HasPendingInput());
}

TEST(KeyReaderState, MissingReaderStopsCapturingEvenWhenAnotherReaderIsRendered)
{
	CButtonContainer Reader;
	CButtonContainer OtherReader;
	CKeyReaderState State;
	State.Start(&Reader);
	EXPECT_FALSE(State.TakeResult(&Reader));
	State.FinishRenderFrame();
	ASSERT_TRUE(State.IsCapturing());

	EXPECT_FALSE(State.TakeResult(&Reader));
	State.FinishRenderFrame();
	ASSERT_TRUE(State.IsCapturing());

	EXPECT_FALSE(State.TakeResult(&OtherReader));
	State.FinishRenderFrame();
	EXPECT_FALSE(State.IsCapturing());
	EXPECT_FALSE(State.HasPendingInput());
	const auto Result = State.TakeResult(&Reader);
	ASSERT_TRUE(Result);
	EXPECT_EQ(Result->m_Key, KEY_ESCAPE);
}

TEST(KeyReaderState, MissingReaderDiscardsAnUndeliveredCapture)
{
	CButtonContainer Reader;
	CKeyReaderState State;
	State.Start(&Reader);
	EXPECT_FALSE(State.TakeResult(&Reader));
	State.FinishRenderFrame();

	State.Capture({KEY_A, KeyModifier::NONE});
	ASSERT_TRUE(State.HasPendingInput());
	State.FinishRenderFrame();
	EXPECT_FALSE(State.HasPendingInput());
	const auto Result = State.TakeResult(&Reader);
	ASSERT_TRUE(Result);
	EXPECT_EQ(Result->m_Key, KEY_ESCAPE);
}
