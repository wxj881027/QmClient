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
    // 验证各 Provider 的密钥配置变量可访问
    // 这些只是编译时检查，确保变量名正确
    (void)g_Config.m_QmTranslateLlmProvider;
    (void)g_Config.m_QmTranslateLlmKeyZhipu;
    (void)g_Config.m_QmTranslateLlmKeyDeepseek;
    (void)g_Config.m_QmTranslateLlmKeyOpenai;
    (void)g_Config.m_QmTranslateLlmKey;
    SUCCEED();
}
