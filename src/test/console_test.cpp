#include "test.h"

#include <engine/console.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	struct SCommandResults
	{
		std::vector<int> m_vVictims;
	};

	void ConVictim(IConsole::IResult *pResult, void *pUser)
	{
		static_cast<SCommandResults *>(pUser)->m_vVictims.push_back(pResult->GetVictim(0));
	}

	void ConTwoVictims(IConsole::IResult *pResult, void *pUser)
	{
		auto *pResults = static_cast<SCommandResults *>(pUser);
		pResults->m_vVictims.push_back(pResult->GetVictim(0));
		pResults->m_vVictims.push_back(pResult->GetVictim(1));
	}
}

TEST(Console, QuotedVictimArgumentsAreValidated)
{
	auto pConsole = CreateConsole(CFGFLAG_SERVER);
	SCommandResults Results;
	pConsole->Register("victim", "v", CFGFLAG_SERVER, ConVictim, &Results, "");

	pConsole->ExecuteLine("victim \"all\"", 42);
	ASSERT_EQ(Results.m_vVictims.size(), MAX_CLIENTS);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		EXPECT_EQ(Results.m_vVictims[i], i);
	}

	Results.m_vVictims.clear();
	pConsole->ExecuteLine("victim \"me\"", 42);
	ASSERT_EQ(Results.m_vVictims.size(), 1);
	EXPECT_EQ(Results.m_vVictims[0], 42);

	Results.m_vVictims.clear();
	pConsole->ExecuteLine("victim \"3\"", 42);
	ASSERT_EQ(Results.m_vVictims.size(), 1);
	EXPECT_EQ(Results.m_vVictims[0], 3);

	Results.m_vVictims.clear();
	pConsole->ExecuteLine("victim \"\"", 42);
	EXPECT_TRUE(Results.m_vVictims.empty());

	Results.m_vVictims.clear();
	pConsole->ExecuteLine("victim \"invalid\"", 42);
	EXPECT_TRUE(Results.m_vVictims.empty());
}

TEST(Console, MultipleVictimSlotsResolveIndependently)
{
	// 官方 f586be3e0：多个 v 参数按槽位保存，一个槽位展开不影响其它槽位
	auto pConsole = CreateConsole(CFGFLAG_SERVER);
	SCommandResults Results;
	pConsole->Register("twovictims", "vv", CFGFLAG_SERVER, ConTwoVictims, &Results, "");

	pConsole->ExecuteLine("twovictims 3 5", 42);
	ASSERT_EQ(Results.m_vVictims.size(), 2);
	EXPECT_EQ(Results.m_vVictims[0], 3);
	EXPECT_EQ(Results.m_vVictims[1], 5);

	Results.m_vVictims.clear();
	pConsole->ExecuteLine("twovictims me 5", 42);
	ASSERT_EQ(Results.m_vVictims.size(), 2);
	EXPECT_EQ(Results.m_vVictims[0], 42);
	EXPECT_EQ(Results.m_vVictims[1], 5);

	// 单个槽位展开时其它槽位保持不变
	Results.m_vVictims.clear();
	pConsole->ExecuteLine("twovictims all 5", 42);
	ASSERT_EQ(Results.m_vVictims.size(), (size_t)MAX_CLIENTS * 2);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		EXPECT_EQ(Results.m_vVictims[i * 2], i);
		EXPECT_EQ(Results.m_vVictims[i * 2 + 1], 5);
	}

	// 只允许一个参数展开成多个客户端，两个槽位都写 all 时整条命令拒绝执行
	Results.m_vVictims.clear();
	pConsole->ExecuteLine("twovictims all all", 42);
	EXPECT_TRUE(Results.m_vVictims.empty());
}

TEST(Console, VictimParametersAreDetected)
{
	auto pConsole = CreateConsole(CFGFLAG_SERVER);
	SCommandResults Results;
	pConsole->Register("withvictim", "v[id] i[team]", CFGFLAG_SERVER, ConVictim, &Results, "");
	pConsole->Register("withoutvictim", "i[team]", CFGFLAG_SERVER, ConVictim, &Results, "");

	ASSERT_NE(pConsole->GetCommandInfo("withvictim", CFGFLAG_SERVER, false), nullptr);
	ASSERT_NE(pConsole->GetCommandInfo("withoutvictim", CFGFLAG_SERVER, false), nullptr);
	EXPECT_TRUE(pConsole->GetCommandInfo("withvictim", CFGFLAG_SERVER, false)->TakesClientId());
	EXPECT_FALSE(pConsole->GetCommandInfo("withoutvictim", CFGFLAG_SERVER, false)->TakesClientId());
}
