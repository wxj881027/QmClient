#include <engine/client/input_sdl3.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>

namespace
{
	struct SJoystick
	{
		int m_References = 1, m_Closes = 0;
		void Close() noexcept
		{
			--m_References;
			++m_Closes;
		}
	};
}

TEST(SdlJoystickAttachment, EmptyOpenResultCallsNeitherAttachmentNorClose)
{
	int Attachments = 0, Closes = 0;
	EXPECT_FALSE(AttachSdlJoystick(static_cast<SJoystick *>(nullptr), [&](auto *) noexcept { ++Closes; }, [&](auto *) {
		++Attachments;
		return true; }));
	EXPECT_EQ(Attachments, 0);
	EXPECT_EQ(Closes, 0);
}

TEST(SdlJoystickAttachment, SuccessfulAttachmentOwnsTheReferenceUntilItCloses)
{
	SJoystick Joystick;
	SJoystick *pPublished = nullptr;
	EXPECT_TRUE(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [&](auto *pHandle) {
		pPublished = pHandle;
		return true; }));
	ASSERT_EQ(pPublished, &Joystick);
	EXPECT_EQ(Joystick.m_References, 1);
	EXPECT_EQ(Joystick.m_Closes, 0);
	pPublished->Close();
	EXPECT_EQ(Joystick.m_References, 0);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, RejectedAttachmentReturnsTheNewReference)
{
	SJoystick Joystick;
	EXPECT_FALSE(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) { return false; }));
	EXPECT_EQ(Joystick.m_References, 0);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, DuplicateOpenDoesNotCloseTheAlreadyPublishedReference)
{
	SJoystick Joystick;
	Joystick.m_References = 2;
	EXPECT_FALSE(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) { return false; }));
	EXPECT_EQ(Joystick.m_References, 1);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, AllocationFailureClosesTheUnpublishedReference)
{
	SJoystick Joystick;
	EXPECT_THROW(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) -> bool { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Joystick.m_References, 0);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, LengthFailureClosesTheUnpublishedReference)
{
	SJoystick Joystick;
	EXPECT_THROW(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) -> bool { throw std::length_error("attachment limit"); }), std::length_error);
	EXPECT_EQ(Joystick.m_References, 0);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, OtherAttachmentFailureAlsoPropagatesAfterClosing)
{
	SJoystick Joystick;
	EXPECT_THROW(AttachSdlJoystick(&Joystick, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) -> bool { throw std::runtime_error("attachment failed"); }), std::runtime_error);
	EXPECT_EQ(Joystick.m_References, 0);
	EXPECT_EQ(Joystick.m_Closes, 1);
}

TEST(SdlJoystickAttachment, FailureDoesNotPreventAttachingTheNextOpenedDevice)
{
	SJoystick First, Second;
	EXPECT_THROW(AttachSdlJoystick(&First, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) -> bool { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_TRUE(AttachSdlJoystick(&Second, [](auto *pHandle) noexcept { pHandle->Close(); }, [](auto *) { return true; }));
	EXPECT_EQ(First.m_References, 0);
	EXPECT_EQ(Second.m_References, 1);
	EXPECT_EQ(Second.m_Closes, 0);
	Second.Close();
}
