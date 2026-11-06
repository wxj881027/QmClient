#include <game/client/components/qmclient/translate/translate_backend.h>

#include <gtest/gtest.h>

#include <string>

TEST(TranslateLocalEndpoint, RecognizesExistingLoopbackHostsWithSchemeCaseAndAuthorityTerminators)
{
	static_assert(noexcept(IsLocalTranslateEndpoint(nullptr)));
	for(const char *pEndpoint : {"http://localhost", "HTTPS://LOCALHOST:8000/v1", "http://127.0.0.1:5000?q=remote.test", "https://[::1]:11434#remote.test", "https://localhost?x=@remote.test", "http://127.0.0.1/path/@remote.test", "https://[::1]/chat/completions"})
	{
		SCOPED_TRACE(pEndpoint);
		EXPECT_TRUE(IsLocalTranslateEndpoint(pEndpoint));
	}
}

TEST(TranslateLocalEndpoint, RejectsAbsentUnsupportedUserinfoAndLookalikeHosts)
{
	EXPECT_FALSE(IsLocalTranslateEndpoint(nullptr));
	for(const char *pEndpoint : {"", "localhost", "ftp://localhost", "http://", "http:///path", "http://?query", "http://#fragment", "https://remote.test", "http://localhost.remote.test", "http://localhost@remote.test", "http://remote.test@localhost", "http://127.0.0.1.remote.test", "http://[::1].remote.test", "http://[::1]suffix", "http://[::1", "http://::1", "http://LOCALHOST ", " http://localhost"})
	{
		SCOPED_TRACE(pEndpoint);
		EXPECT_FALSE(IsLocalTranslateEndpoint(pEndpoint));
	}
}

TEST(TranslateLocalEndpoint, PreservesHostOnlyClassificationWithoutAddingPortValidation)
{
	// 该接口只分类主机，端口合法性仍由既有请求解析路径处理。
	for(const char *pEndpoint : {"http://localhost:", "http://localhost:service", "http://127.0.0.1:65536", "http://[::1]:", "http://[::1]:service"})
	{
		SCOPED_TRACE(pEndpoint);
		EXPECT_TRUE(IsLocalTranslateEndpoint(pEndpoint));
	}
}

TEST(TranslateLocalEndpoint, LongUnrelatedUrlPartsDoNotChangeLoopbackIdentity)
{
	const std::string Suffix(2048, 'x');
	EXPECT_TRUE(IsLocalTranslateEndpoint(("http://localhost/" + Suffix).c_str()));
	EXPECT_TRUE(IsLocalTranslateEndpoint(("https://[::1]?" + Suffix).c_str()));
	EXPECT_TRUE(IsLocalTranslateEndpoint(("https://127.0.0.1#" + Suffix).c_str()));
	EXPECT_FALSE(IsLocalTranslateEndpoint(("http://localhost" + Suffix + "/").c_str()));
}
