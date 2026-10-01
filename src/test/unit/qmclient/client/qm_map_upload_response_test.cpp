#include <game/client/components/qmclient/qm_map_upload.h>

#include <gtest/gtest.h>

TEST(QmMapUpload, ResponseRequiresBothHttpAndBusinessSuccess)
{
	const std::string Success = R"({"success":true,"message":"上传成功"})";
	for(const int StatusCode : {200, 201, 299})
	{
		const auto Result = QmMapUpload::ParseResponse(StatusCode, Success.data(), Success.size());
		EXPECT_TRUE(Result.m_Valid);
		EXPECT_TRUE(Result.m_Success);
		EXPECT_EQ(Result.m_Message, "上传成功");
	}
	for(const int StatusCode : {0, 199, 300, 401, 500})
	{
		const auto Result = QmMapUpload::ParseResponse(StatusCode, Success.data(), Success.size());
		EXPECT_TRUE(Result.m_Valid);
		EXPECT_FALSE(Result.m_Success);
	}
	const std::string Failure = R"({"success":false,"message":"地图格式错误"})";
	for(const int StatusCode : {200, 400, 500})
	{
		const auto Result = QmMapUpload::ParseResponse(StatusCode, Failure.data(), Failure.size());
		EXPECT_TRUE(Result.m_Valid);
		EXPECT_FALSE(Result.m_Success);
		EXPECT_EQ(Result.m_Message, "地图格式错误");
	}
}

TEST(QmMapUpload, ResponseRejectsMalformedJsonAndMissingBooleanSuccess)
{
	for(const std::string_view Json : {"", "<html>error</html>", "{", "[]", "null", "true", "{}", "{\"success\":1}", "{\"success\":\"true\"}"})
	{
		const auto Result = QmMapUpload::ParseResponse(200, Json.data(), Json.size());
		EXPECT_FALSE(Result.m_Valid) << Json;
		EXPECT_FALSE(Result.m_Success) << Json;
	}
}

TEST(QmMapUpload, ResponseUsesExplicitLengthAndOnlyStringMessages)
{
	// 输入可以不是以零结尾的字符串，后续缓冲区内容不能参与解析。
	const std::string Json = R"({"success":true})";
	const std::string Buffer = Json + "trailing bytes";
	const auto Bounded = QmMapUpload::ParseResponse(200, Buffer.data(), Json.size());
	EXPECT_TRUE(Bounded.m_Valid);
	EXPECT_TRUE(Bounded.m_Success);
	EXPECT_TRUE(Bounded.m_Message.empty());
	const std::string NonStringMessage = R"({"success":false,"message":123})";
	const auto Failure = QmMapUpload::ParseResponse(400, NonStringMessage.data(), NonStringMessage.size());
	EXPECT_TRUE(Failure.m_Valid);
	EXPECT_FALSE(Failure.m_Success);
	EXPECT_TRUE(Failure.m_Message.empty());
}
