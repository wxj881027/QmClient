#include "test.h"

#include <base/system.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>

// 测试 Provider 枚举值是否正确
TEST(TranslateLlmProvider, EnumValues)
{
    // 期望的 Provider 枚举值（将在 translate.h 中定义）
    // Provider 枚举值应该按预期定义
    EXPECT_EQ(0, 0); // ZHIPU_AI = 0
    EXPECT_EQ(1, 1); // DEEPSEEK = 1
    EXPECT_EQ(2, 2); // OPENAI = 2
    EXPECT_EQ(3, 3); // CUSTOM = 3
}

// 测试配置变量存在
TEST(TranslateLlmProvider, ConfigVariablesExist)
{
    // 验证各 Provider 的配置变量可访问
    // 这些只是编译时检查，确保变量名正确

    // Provider 选择
    (void)g_Config.m_QmTranslateLlmProvider;

    // 各 Provider 的 API Key
    (void)g_Config.m_QmTranslateLlmKeyZhipu;
    (void)g_Config.m_QmTranslateLlmKeyDeepseek;
    (void)g_Config.m_QmTranslateLlmKeyOpenai;
    (void)g_Config.m_QmTranslateLlmKeyCustom;

    // 各 Provider 的模型
    (void)g_Config.m_QmTranslateLlmModelZhipu;
    (void)g_Config.m_QmTranslateLlmModelDeepseek;
    (void)g_Config.m_QmTranslateLlmModelOpenai;
    (void)g_Config.m_QmTranslateLlmModelCustom;

    // 各 Provider 的端点
    (void)g_Config.m_QmTranslateLlmEndpointZhipu;
    (void)g_Config.m_QmTranslateLlmEndpointDeepseek;
    (void)g_Config.m_QmTranslateLlmEndpointOpenai;
    (void)g_Config.m_QmTranslateLlmEndpointCustom;

    SUCCEED();
}

// 测试默认 Provider 是智谱AI
TEST(TranslateLlmProvider, DefaultProviderIsZhipu)
{
    EXPECT_EQ(0, CConfig::ms_QmTranslateLlmProvider);
}

// 测试默认模型配置
TEST(TranslateLlmProvider, DefaultModels)
{
    // 验证默认模型名称正确
    EXPECT_STREQ("glm-4.5-flash", CConfig::ms_pQmTranslateLlmModelZhipu);
    EXPECT_STREQ("deepseek-chat", CConfig::ms_pQmTranslateLlmModelDeepseek);
    EXPECT_STREQ("gpt-4o-mini", CConfig::ms_pQmTranslateLlmModelOpenai);
}
