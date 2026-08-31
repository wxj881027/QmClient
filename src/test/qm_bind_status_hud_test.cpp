// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/qm_bind_status_hud.h>

#include <gtest/gtest.h>

// testrunner 不链接完整客户端；为模块编译单元引用到的组件接口提供空实现
class CConfig *CComponentInterfaces::Config() const
{
	return nullptr;
}

class IConsole *CComponentInterfaces::Console() const
{
	return nullptr;
}

class IConfigManager *CComponentInterfaces::ConfigManager() const
{
	return nullptr;
}

namespace
{
	// 解析并用默认内置四项断言
	const std::vector<SQmBindStatusEntry> &Defaults()
	{
		return QmDefaultBindStatusEntries();
	}

	const SQmBindStatusEntry *FindEntry(const std::vector<SQmBindStatusEntry> &vEntries, const char *pVarName)
	{
		for(const SQmBindStatusEntry &Entry : vEntries)
		{
			if(Entry.m_VarName == pVarName)
				return &Entry;
		}
		return nullptr;
	}

	std::string ResolveOrEmpty(const SQmBindStatusEntry &Entry, int Value)
	{
		std::string Text;
		if(QmResolveBindStatusEntry(Entry, Value, Text))
			return Text;
		return "<hidden>";
	}
} // namespace

TEST(QmBindStatusHud, ParseEmptyListProducesNoEntries)
{
	std::vector<SQmBindStatusEntry> vEntries;
	EXPECT_TRUE(QmParseBindStatusList("", vEntries));
	EXPECT_TRUE(vEntries.empty());
	EXPECT_TRUE(QmParseBindStatusList("   ; ; ;", vEntries));
	EXPECT_TRUE(vEntries.empty());
	EXPECT_FALSE(QmParseBindStatusList(nullptr, vEntries));
	EXPECT_TRUE(vEntries.empty());
}

TEST(QmBindStatusHud, DefaultsCoverTheFourBuiltInEntries)
{
	const std::vector<SQmBindStatusEntry> &vDefaults = Defaults();
	ASSERT_EQ(vDefaults.size(), 4u);
	EXPECT_EQ(vDefaults[0].m_VarName, "cl_dummy_resetonswitch");
	EXPECT_EQ(vDefaults[1].m_VarName, "qm_deepfly_mode");
	EXPECT_EQ(vDefaults[2].m_VarName, "cl_dummy_control");
	EXPECT_EQ(vDefaults[3].m_VarName, "cl_dummy_copy_moves");
}

TEST(QmBindStatusHud, DefaultsResolveLikeLegacyHud)
{
	const SQmBindStatusEntry *pHammer = FindEntry(Defaults(), "qm_deepfly_mode");
	ASSERT_NE(pHammer, nullptr);
	EXPECT_EQ(ResolveOrEmpty(*pHammer, 0), "Hammer: Normal");
	EXPECT_EQ(ResolveOrEmpty(*pHammer, 1), "Hammer: DF");
	EXPECT_EQ(ResolveOrEmpty(*pHammer, 2), "Hammer: HDF");
	EXPECT_EQ(ResolveOrEmpty(*pHammer, 3), "Hammer: Custom");
	EXPECT_EQ(ResolveOrEmpty(*pHammer, 4), "<hidden>");

	const SQmBindStatusEntry *pKey = FindEntry(Defaults(), "cl_dummy_resetonswitch");
	ASSERT_NE(pKey, nullptr);
	EXPECT_EQ(ResolveOrEmpty(*pKey, 0), "Key Sticking: On");
	EXPECT_EQ(ResolveOrEmpty(*pKey, 1), "Key Sticking: Off");
	EXPECT_EQ(ResolveOrEmpty(*pKey, 2), "Key Sticking: Reset Self");
}

TEST(QmBindStatusHud, DefaultsSerializeAndRoundTrip)
{
	const std::string Serialized = QmSerializeBindStatusList(Defaults());
	EXPECT_EQ(Serialized,
		"cl_dummy_resetonswitch|0=Key Sticking: On|1=Key Sticking: Off|2=Key Sticking: Reset Self; "
		"qm_deepfly_mode|0=Hammer: Normal|1=Hammer: DF|2=Hammer: HDF|3=Hammer: Custom; "
		"cl_dummy_control|0=Dummy Control: Off|1=Dummy Control: On; "
		"cl_dummy_copy_moves|0=Dummy copy: Off|1=Dummy copy: On");

	std::vector<SQmBindStatusEntry> vParsed;
	ASSERT_TRUE(QmParseBindStatusList(Serialized.c_str(), vParsed));
	EXPECT_EQ(QmSerializeBindStatusList(vParsed), Serialized);
}

TEST(QmBindStatusHud, SwitchTextShowsOnlyWhenNonZero)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("cl_dummy_hammer|DF", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	const SQmBindStatusEntry &Entry = vEntries[0];
	EXPECT_EQ(Entry.m_VarName, "cl_dummy_hammer");
	EXPECT_TRUE(Entry.m_HasNonZeroText);
	EXPECT_EQ(Entry.m_NonZeroText, "DF");
	EXPECT_EQ(ResolveOrEmpty(Entry, 0), "<hidden>");
	EXPECT_EQ(ResolveOrEmpty(Entry, 1), "DF");
}

TEST(QmBindStatusHud, BareVariableNameShowsItselfWhenNonZero)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("qm_deepfly_mode", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	const SQmBindStatusEntry &Entry = vEntries[0];
	EXPECT_EQ(Entry.m_VarName, "qm_deepfly_mode");
	EXPECT_TRUE(Entry.m_HasNonZeroText);
	EXPECT_EQ(Entry.m_NonZeroText, "qm_deepfly_mode");
	EXPECT_EQ(ResolveOrEmpty(Entry, 0), "<hidden>");
	EXPECT_EQ(ResolveOrEmpty(Entry, 2), "qm_deepfly_mode");
}

TEST(QmBindStatusHud, ExactValueMappingTakesPrecedenceOverSwitchText)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("cl_dummy_control|0=Off|1=On|On", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	const SQmBindStatusEntry &Entry = vEntries[0];
	EXPECT_EQ(ResolveOrEmpty(Entry, 0), "Off");
	EXPECT_EQ(ResolveOrEmpty(Entry, 1), "On");
	EXPECT_EQ(ResolveOrEmpty(Entry, 2), "On");
}

TEST(QmBindStatusHud, DuplicateValueMappingKeepsLast)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("var|0=A|0=B", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(ResolveOrEmpty(vEntries[0], 0), "B");
}

TEST(QmBindStatusHud, TrimsWhitespaceAndSkipsEmptyTokens)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("  cl_dummy_hammer | DF  ;  ; cl_dummy_control|0=Off  |  1=On ;", vEntries));
	ASSERT_EQ(vEntries.size(), 2u);
	EXPECT_EQ(vEntries[0].m_VarName, "cl_dummy_hammer");
	EXPECT_EQ(vEntries[0].m_NonZeroText, "DF");
	EXPECT_EQ(vEntries[1].m_VarName, "cl_dummy_control");
	EXPECT_EQ(ResolveOrEmpty(vEntries[1], 1), "On");
}

TEST(QmBindStatusHud, SkipsMalformedEntries)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("|; ||; var|x=; 123; var|=; ; var|", vEntries));
	// "var|x=" 与 "var|=" 是无显示配置的条目（永不显示）；"var|" 按仅变量名处理（非零显示变量名）；"123" 名字非法跳过
	ASSERT_EQ(vEntries.size(), 3u);
	EXPECT_EQ(vEntries[0].m_VarName, "var");
	EXPECT_FALSE(vEntries[0].m_HasNonZeroText);
	EXPECT_TRUE(vEntries[0].m_vValueTexts.empty());
	EXPECT_EQ(vEntries[1].m_VarName, "var");
	EXPECT_FALSE(vEntries[1].m_HasNonZeroText);
	EXPECT_TRUE(vEntries[1].m_vValueTexts.empty());
	EXPECT_EQ(vEntries[2].m_VarName, "var");
	EXPECT_TRUE(vEntries[2].m_HasNonZeroText);
	EXPECT_EQ(vEntries[2].m_NonZeroText, "var");
}

TEST(QmBindStatusHud, RejectsInvalidVariableNames)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("123|x; var.name|x; _ok|x; qm_var2|x", vEntries));
	ASSERT_EQ(vEntries.size(), 2u);
	EXPECT_EQ(vEntries[0].m_VarName, "_ok");
	EXPECT_EQ(vEntries[1].m_VarName, "qm_var2");
}

TEST(QmBindStatusHud, SupportsNegativeValueMappings)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("var|-1=Negative|1=Positive", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(ResolveOrEmpty(vEntries[0], -1), "Negative");
	EXPECT_EQ(ResolveOrEmpty(vEntries[0], 1), "Positive");
	EXPECT_EQ(ResolveOrEmpty(vEntries[0], 0), "<hidden>");
}

TEST(QmBindStatusHud, EmptyFieldBetweenSeparatorsIsIgnored)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("cl_dummy_hammer||DF", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(vEntries[0].m_NonZeroText, "DF");
}

TEST(QmBindStatusHud, TextMayContainEqualsSign)
{
	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList("var|1=Speed == fast", vEntries));
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(ResolveOrEmpty(vEntries[0], 1), "Speed == fast");
}
