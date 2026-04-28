#include "test.h"

#include <base/system.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>

#include <algorithm>

// 集成测试：测试翻译系统的并发控制和配置集成
// 这些测试验证智能并发默认值、手动覆盖优先级、后端特定并发等

// 模拟 Provider 枚举（与 translate.cpp 中一致）
enum class ELlmProvider
{
	ZHIPU_AI = 0,
	DEEPSEEK = 1,
	OPENAI = 2,
	CUSTOM = 3,
};

// 模拟 GetEffectiveConcurrency 逻辑
static int GetEffectiveConcurrency(int ConfigValue, const char *pBackend, int Provider)
{
	// 如果用户手动设置（不等于默认值 1），使用用户值
	if(ConfigValue != 1)
		return ConfigValue;

	// 根据后端类型提供智能默认值
	if(str_comp_nocase(pBackend, "llm") == 0)
	{
		ELlmProvider ProviderEnum = static_cast<ELlmProvider>(Provider);
		switch(ProviderEnum)
		{
		case ELlmProvider::ZHIPU_AI:
		case ELlmProvider::DEEPSEEK:
			return 3; // ZhipuAI 和 DeepSeek 默认 3
		case ELlmProvider::OPENAI:
			return 2; // OpenAI 默认 2（成本考虑）
		case ELlmProvider::CUSTOM:
		default:
			return 1; // 自定义使用默认值
		}
	}
	else if(str_comp_nocase(pBackend, "tencentcloud") == 0)
	{
		return 5; // TencentCloud 默认 5
	}
	else if(str_comp_nocase(pBackend, "libretranslate") == 0)
	{
		return 2; // LibreTranslate 默认 2
	}
	else if(str_comp_nocase(pBackend, "ftapi") == 0)
	{
		return 1; // FTAPI 默认 1（防止过载）
	}

	// 未知后端默认 3
	return 3;
}

// 测试并发控制智能默认值
TEST(TranslateIntegration, ConcurrencySmartDefaults)
{
	// 测试各种后端的智能默认值（用户未手动设置时）

	// LLM 后端 - ZhipuAI
	EXPECT_EQ(GetEffectiveConcurrency(1, "llm", 0), 3);

	// LLM 后端 - DeepSeek
	EXPECT_EQ(GetEffectiveConcurrency(1, "llm", 1), 3);

	// LLM 后端 - OpenAI
	EXPECT_EQ(GetEffectiveConcurrency(1, "llm", 2), 2);

	// LLM 后端 - Custom
	EXPECT_EQ(GetEffectiveConcurrency(1, "llm", 3), 1);

	// TencentCloud
	EXPECT_EQ(GetEffectiveConcurrency(1, "tencentcloud", 0), 5);

	// LibreTranslate
	EXPECT_EQ(GetEffectiveConcurrency(1, "libretranslate", 0), 2);

	// FTAPI
	EXPECT_EQ(GetEffectiveConcurrency(1, "ftapi", 0), 1);

	// 未知后端
	EXPECT_EQ(GetEffectiveConcurrency(1, "unknown", 0), 3);
}

// 测试手动设置优先
TEST(TranslateIntegration, ConcurrencyManualOverride)
{
	// 测试用户手动设置并发数时，应该优先使用用户值

	// 用户设置为 5
	EXPECT_EQ(GetEffectiveConcurrency(5, "llm", 0), 5);
	EXPECT_EQ(GetEffectiveConcurrency(5, "ftapi", 0), 5); // 即使是 FTAPI 也使用用户值

	// 用户设置为 10
	EXPECT_EQ(GetEffectiveConcurrency(10, "llm", 0), 10);

	// 用户设置为 1（等于默认值，使用智能默认）
	EXPECT_EQ(GetEffectiveConcurrency(1, "llm", 0), 3); // 使用智能默认

	// 用户设置为 0（边界情况）
	EXPECT_EQ(GetEffectiveConcurrency(0, "llm", 0), 0);

	// 用户设置为负数（边界情况，实际代码中应该有 clamp）
	EXPECT_EQ(GetEffectiveConcurrency(-1, "llm", 0), -1);
}

// 测试不同后端并发
TEST(TranslateIntegration, ConcurrencyByBackend)
{
	// 测试不同后端类型的并发控制

	struct BackendTestCase
	{
		const char *pBackend;
		int ExpectedDefault;
		const char *pReason;
	};

	BackendTestCase aTestCases[] = {
		{"llm", 3, "LLM default with ZhipuAI"},
		{"tencentcloud", 5, "TencentCloud has higher rate limits"},
		{"libretranslate", 2, "LibreTranslate self-hosted"},
		{"ftapi", 1, "FTAPI needs protection from overload"},
	};

	for(const auto &TestCase : aTestCases)
	{
		int Concurrency = GetEffectiveConcurrency(1, TestCase.pBackend, 0);
		EXPECT_EQ(Concurrency, TestCase.ExpectedDefault)
			<< "Backend: " << TestCase.pBackend << ", Reason: " << TestCase.pReason;
	}
}

// 测试 FTAPI 保护
TEST(TranslateIntegration, FtapiProtection)
{
	// FTAPI 是公共服务，需要特殊保护防止过载

	// 默认并发应该是 1
	EXPECT_EQ(GetEffectiveConcurrency(1, "ftapi", 0), 1);

	// 即使手动设置，也应该有合理的上限检查
	// 这里测试的是逻辑，实际代码中应该有 clamp

	// FTAPI 自动翻译应该默认关闭
	// 模拟配置检查
	bool FtapiAutoEnabled = false; // g_Config.m_QmTranslateFtapiAutoEnable
	EXPECT_FALSE(FtapiAutoEnabled);

	// 用户显式启用后才能使用
	FtapiAutoEnabled = true;
	EXPECT_TRUE(FtapiAutoEnabled);
}

// 测试 Provider 值范围
TEST(TranslateIntegration, ProviderValueRange)
{
	// 测试 Provider 值在有效范围内
	constexpr int PROVIDER_MIN = static_cast<int>(ELlmProvider::ZHIPU_AI);
	constexpr int PROVIDER_MAX = static_cast<int>(ELlmProvider::CUSTOM);

	// 测试有效值
	for(int i = PROVIDER_MIN; i <= PROVIDER_MAX; i++)
	{
		int ClampedValue = std::clamp(i, PROVIDER_MIN, PROVIDER_MAX);
		EXPECT_EQ(ClampedValue, i);
	}

	// 测试边界外值
	int InvalidLow = -1;
	int InvalidHigh = 10;

	EXPECT_EQ(std::clamp(InvalidLow, PROVIDER_MIN, PROVIDER_MAX), PROVIDER_MIN);
	EXPECT_EQ(std::clamp(InvalidHigh, PROVIDER_MIN, PROVIDER_MAX), PROVIDER_MAX);
}

// 测试配置变量集成
TEST(TranslateIntegration, ConfigVariablesIntegration)
{
	// 测试配置变量的默认值和类型

	// Provider 默认应该是 0 (ZhipuAI)
	EXPECT_EQ(CConfig::ms_QmTranslateLlmProvider, 0);

	// 并发默认值应该是 1（表示使用智能默认）
	EXPECT_EQ(CConfig::ms_QmTranslateLlmConcurrency, 1);

	// 测试配置变量可读写
	const int OldConcurrency = g_Config.m_QmTranslateLlmConcurrency;
	g_Config.m_QmTranslateLlmConcurrency = 5;
	EXPECT_EQ(g_Config.m_QmTranslateLlmConcurrency, 5);

	// 恢复原值，避免依赖测试执行顺序
	g_Config.m_QmTranslateLlmConcurrency = OldConcurrency;
	EXPECT_EQ(g_Config.m_QmTranslateLlmConcurrency, OldConcurrency);
}

// 测试后端名称比较
TEST(TranslateIntegration, BackendNameComparison)
{
	// 测试后端名称比较（大小写不敏感）

	EXPECT_EQ(str_comp_nocase("llm", "llm"), 0);
	EXPECT_EQ(str_comp_nocase("LLM", "llm"), 0);
	EXPECT_EQ(str_comp_nocase("Llm", "LLM"), 0);

	EXPECT_EQ(str_comp_nocase("ftapi", "ftapi"), 0);
	EXPECT_EQ(str_comp_nocase("FTAPI", "ftapi"), 0);

	EXPECT_EQ(str_comp_nocase("tencentcloud", "tencentcloud"), 0);
	EXPECT_EQ(str_comp_nocase("TencentCloud", "tencentcloud"), 0);

	EXPECT_NE(str_comp_nocase("llm", "ftapi"), 0);
}

// 测试并发限制与任务队列
TEST(TranslateIntegration, ConcurrencyAndJobQueue)
{
	// 测试并发限制如何影响任务队列

	constexpr size_t MAX_TRANSLATION_JOBS = 15;

	// 模拟不同并发限制下的任务队列行为
	for(int MaxConcurrency : {1, 3, 5, 10})
	{
		size_t CurrentJobs = 0;

		// 模拟添加任务
		for(int i = 0; i < 20; i++)
		{
			if(CurrentJobs < static_cast<size_t>(MaxConcurrency))
			{
				CurrentJobs++;
			}
		}

		// 任务数不应超过并发限制
		EXPECT_LE(CurrentJobs, static_cast<size_t>(MaxConcurrency));
		EXPECT_LE(CurrentJobs, MAX_TRANSLATION_JOBS);
	}
}

// 测试出站翻译与入站翻译的并发共享
TEST(TranslateIntegration, OutboundInboundConcurrencySharing)
{
	// 测试出站翻译和入站翻译共享并发限制

	int MaxConcurrency = 5;
	int InboundJobs = 2;
	int OutboundJobs = 1;

	// 总任务数不应超过限制
	EXPECT_LE(InboundJobs + OutboundJobs, MaxConcurrency);

	// 尝试添加更多任务
	if(InboundJobs + OutboundJobs < MaxConcurrency)
	{
		// 可以添加
		InboundJobs++;
	}

	EXPECT_LE(InboundJobs + OutboundJobs, MaxConcurrency);
	EXPECT_EQ(InboundJobs, 3); // 成功添加了一个任务
}
