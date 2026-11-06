#include <game/client/components/qmclient/qm_json_send.h>

#include <gtest/gtest.h>

#include <stdexcept>

TEST(QmJsonSend, PreparationAllocationFailureDoesNotInvokeTransport)
{
	bool Sent = false;
	EXPECT_FALSE(TrySendQmJsonMessage([](CJsonStringWriter &) { throw std::bad_alloc(); }, [&](const char *, size_t) { Sent = true; return true; }));
	EXPECT_FALSE(Sent);
}

TEST(QmJsonSend, PartialSerializedObjectIsDiscardedOnAllocationFailure)
{
	bool Sent = false;
	EXPECT_FALSE(TrySendQmJsonMessage([](CJsonStringWriter &Writer) {
		Writer.BeginObject();
		Writer.WriteAttribute("type");
		Writer.WriteStrValue("partial");
		throw std::bad_alloc(); }, [&](const char *, size_t) { Sent = true; return true; }));
	EXPECT_FALSE(Sent);
}

TEST(QmJsonSend, TransportRejectionReturnsFailureAfterCompletePreparation)
{
	bool Prepared = false;
	int Sends = 0;
	EXPECT_FALSE(TrySendQmJsonMessage([&](CJsonStringWriter &Writer) {
		Writer.WriteNullValue();
		Prepared = true; }, [&](const char *pData, size_t Size) {
		++Sends;
		EXPECT_TRUE(Prepared);
		EXPECT_EQ(std::string(pData, Size), "null\n");
		return false; }));
	EXPECT_EQ(Sends, 1);
}

TEST(QmJsonSend, ARejectedMessageDoesNotPreventLaterAdmission)
{
	int Attempts = 0;
	auto Write = [](CJsonStringWriter &Writer) { Writer.WriteBoolValue(true); };
	auto Send = [&](const char *, size_t) { return ++Attempts > 1; };
	EXPECT_FALSE(TrySendQmJsonMessage(Write, Send));
	EXPECT_TRUE(TrySendQmJsonMessage(Write, Send));
	EXPECT_EQ(Attempts, 2);
}

TEST(QmJsonSend, UnexpectedWriterLogicExceptionRemainsVisible)
{
	bool Sent = false;
	EXPECT_THROW(TrySendQmJsonMessage([](CJsonStringWriter &) { throw std::logic_error("unexpected"); }, [&](const char *, size_t) { Sent = true; return true; }), std::logic_error);
	EXPECT_FALSE(Sent);
}
