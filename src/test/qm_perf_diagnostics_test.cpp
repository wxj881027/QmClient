#include "test.h"

#include <game/client/components/qmclient/perf_diagnostics.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(QmPerfDiagnostics, SensitiveConfigIsDetectedByAnyKeywordPart)
{
	// 名单是子串匹配：密码/密钥类配置无论前缀如何都应命中。
	const char *apSensitive[] = {
		"qm_map_upload_token",
		"qm_netease_cookie",
		"qm_llm_key_openai",
		"qm_libre_key",
		"qm_sp_dc",
		"qm_translate_api_key",
		"qm_apikey",
		"qm_authorization_header",
		"qm_client_secret",
		"qm_server_password",
	};
	for(const char *pName : apSensitive)
		EXPECT_TRUE(QmPerfSensitiveConfig(pName)) << pName;

	// 非敏感配置不得被误判（否则日志会失去诊断价值）。
	const char *apPlain[] = {
		"qm_perf_debug",
		"cl_showfps",
		"qm_rect_corner_segments",
		"tc_hook_coll_alpha",
	};
	for(const char *pName : apPlain)
		EXPECT_FALSE(QmPerfSensitiveConfig(pName)) << pName;
}

TEST(QmPerfDiagnostics, SensitiveConfigMatchingIsCaseInsensitive)
{
	EXPECT_TRUE(QmPerfSensitiveConfig("qm_upload_TOKEN"));
	EXPECT_TRUE(QmPerfSensitiveConfig("QM_Upload_Password"));
	EXPECT_TRUE(QmPerfSensitiveConfig("qm_AUTHORIZATION"));
}

TEST(QmPerfDiagnostics, ConfigOwnerSplitsQmClientTClientAndDdnet)
{
	EXPECT_STREQ(QmPerfConfigOwner("qm_perf_debug"), "qmclient");
	EXPECT_STREQ(QmPerfConfigOwner("cl_showfps"), "ddnet");
	// 未知名字归为 ddnet，而不是留空。
	EXPECT_STREQ(QmPerfConfigOwner("qm_definitely_not_a_real_variable"), "ddnet");
}

TEST(QmPerfDiagnostics, JsonStringIsQuotedAndEscapesInnerQuotesAndBackslashes)
{
	const std::string Plain = QmPerfJsonString("abc");
	EXPECT_EQ(Plain, "\"abc\"");

	const std::string Escaped = QmPerfJsonString("he\"llo\\");
	EXPECT_EQ(Escaped.front(), '"');
	EXPECT_EQ(Escaped.back(), '"');
	// 内层引号与反斜杠都必须被转义，否则写入的 JSON 行会损坏。
	EXPECT_NE(Escaped.find("\\\""), std::string::npos);
	EXPECT_NE(Escaped.find("\\\\"), std::string::npos);
}

TEST(QmPerfDiagnostics, ConfigValueIsSplitIntoAtMost128ByteChunks)
{
	const std::string Short = "value";
	const auto vShortChunks = QmPerfConfigChunks(Short);
	ASSERT_EQ(vShortChunks.size(), 1u);
	EXPECT_EQ(vShortChunks[0], Short);

	// 300 字节 -> 128 + 128 + 44。
	const std::string Long(300, 'x');
	const auto vLongChunks = QmPerfConfigChunks(Long);
	ASSERT_EQ(vLongChunks.size(), 3u);
	EXPECT_EQ(vLongChunks[0].size(), 128u);
	EXPECT_EQ(vLongChunks[1].size(), 128u);
	EXPECT_EQ(vLongChunks[2].size(), 44u);
}

TEST(QmPerfDiagnostics, ConfigChunksNeverSplitAUtf8Character)
{
	// 每个汉字 3 字节；128 不是 3 的倍数，切分必须回退到字符边界。
	std::string Value;
	for(int i = 0; i < 60; ++i)
		Value += "测";
	const auto vChunks = QmPerfConfigChunks(Value);

	ASSERT_GT(vChunks.size(), 1u);
	std::string Rebuilt;
	for(const std::string &Chunk : vChunks)
	{
		ASSERT_FALSE(Chunk.empty());
		// 块首不得是续接字节（0b10xxxxxx），块尾也不得留下不完整序列。
		EXPECT_NE((unsigned char)Chunk.front() & 0xc0, 0x80);
		Rebuilt += Chunk;
	}
	// 分块是划分而非丢弃：拼回必须与原文一致。
	EXPECT_EQ(Rebuilt, Value);
}

TEST(QmPerfDiagnostics, EmptyConfigValueStillProducesOneChunk)
{
	// 记录当前行为：空值也会产出 1 个空块（写入端据此仍会输出一条事件）。
	const auto vChunks = QmPerfConfigChunks("");
	ASSERT_EQ(vChunks.size(), 1u);
	EXPECT_TRUE(vChunks[0].empty());
}

TEST(QmPerfDiagnostics, FrameBatchKeepsFastAndSlowFramesAndFlushesTail)
{
	CQmPerfFrameBatch Batch;
	EXPECT_FALSE(Batch.Record(10, 0.5));
	EXPECT_FALSE(Batch.Record(11, 20.0));
	EXPECT_EQ(Batch.Count(), 2u);
	const std::string Payload = Batch.TakeFields();
	EXPECT_NE(Payload.find("\"frames\":[10,11]"), std::string::npos);
	EXPECT_NE(Payload.find("\"durations_ms\":[0.500,20.000]"), std::string::npos);
	EXPECT_EQ(Batch.Count(), 0u);
	EXPECT_FALSE(Batch.Record(12, -1.0));
	EXPECT_EQ(Batch.Count(), 0u);
}

TEST(QmPerfDiagnostics, FrameBatchAutomaticallyFlushesCapacityAndElapsedTime)
{
	CQmPerfFrameBatch Batch;
	for(uint64_t Frame = 1; Frame < 64; ++Frame)
		EXPECT_FALSE(Batch.Record(Frame, 0.5));
	EXPECT_TRUE(Batch.Record(64, 0.5));
	EXPECT_EQ(Batch.Count(), 64u);
	Batch.TakeFields();
	EXPECT_TRUE(Batch.Record(65, 1000.0));
	EXPECT_EQ(Batch.Count(), 1u);
}

TEST(QmPerfDiagnostics, ReopeningStartsIndependentSessions)
{
	const uint64_t Original = QmPerfSessionId();
	QmPerfBeginSession();
	const uint64_t First = QmPerfSessionId();
	QmPerfBeginSession();
	EXPECT_GT(First, Original);
	EXPECT_GT(QmPerfSessionId(), First);
}
