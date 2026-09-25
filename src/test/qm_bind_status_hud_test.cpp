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

// 面板尺寸必须随可见行数增长：自定义条目数量变化时背景不能停在固定高度
TEST(QmBindStatusHud, PanelSizeGrowsWithLineCountAndContainsEveryLine)
{
	const float LineHeight = 9.0f;
	const float PaddingX = 4.0f;
	const float PaddingY = 3.0f;
	const float MaxLineWidth = 120.0f;

	const SQmBindStatusPanelSize Empty = QmComputeBindStatusPanelSize(0, MaxLineWidth, LineHeight, PaddingX, PaddingY);
	EXPECT_FLOAT_EQ(Empty.m_W, 0.0f);
	EXPECT_FLOAT_EQ(Empty.m_H, 0.0f);

	float PrevHeight = 0.0f;
	for(int LineCount = 1; LineCount <= 32; ++LineCount)
	{
		const SQmBindStatusPanelSize Size = QmComputeBindStatusPanelSize(LineCount, MaxLineWidth, LineHeight, PaddingX, PaddingY);
		// 背景包住每一行：最后一行底部（行数 * 行高）不得超出面板高度
		EXPECT_GE(Size.m_H, LineHeight * LineCount);
		// 宽度包住最宽行
		EXPECT_GE(Size.m_W, MaxLineWidth);
		// 行数增加时高度必须增长，不能沿用固定高度
		EXPECT_GT(Size.m_H, PrevHeight);
		PrevHeight = Size.m_H;
	}
}

// 宽度只由最宽行决定：行数增加只长高，不长宽
TEST(QmBindStatusHud, PanelWidthFollowsWidestLineOnly)
{
	const SQmBindStatusPanelSize OneLine = QmComputeBindStatusPanelSize(1, 40.0f, 9.0f, 4.0f, 3.0f);
	const SQmBindStatusPanelSize SixLines = QmComputeBindStatusPanelSize(6, 40.0f, 9.0f, 4.0f, 3.0f);
	EXPECT_FLOAT_EQ(OneLine.m_W, SixLines.m_W);
	EXPECT_GT(SixLines.m_H, OneLine.m_H);

	const SQmBindStatusPanelSize Wider = QmComputeBindStatusPanelSize(1, 90.0f, 9.0f, 4.0f, 3.0f);
	EXPECT_GT(Wider.m_W, OneLine.m_W);
	EXPECT_FLOAT_EQ(Wider.m_H, OneLine.m_H);
}

// 复现报告场景：四项自定义条目（卡键/锤/分控/同步）逐条解析并各自给出可见行，
// 行数等于条目数（自定义列表完全替换内置四项），面板高度随之容纳全部行
TEST(QmBindStatusHud, FourEntryCustomListProducesOneVisibleLinePerEntry)
{
	const char *pList =
		"cl_dummy_resetonswitch|0=Key Sticking: On|1=Key Sticking: Off|2=Key Sticking: Reset Self; "
		"qm_deepfly_mode|0=Hammer: Normal|1=Hammer: DF|2=Hammer: HDF|3=Hammer: Custom; "
		"cl_dummy_control|0=Dummy Control: Off|1=Dummy Control: On; "
		"cl_dummy_copy_moves|0=Dummy copy: Off|1=Dummy copy: On";

	std::vector<SQmBindStatusEntry> vEntries;
	ASSERT_TRUE(QmParseBindStatusList(pList, vEntries));
	ASSERT_EQ(vEntries.size(), 4u);
	EXPECT_EQ(vEntries[0].m_VarName, "cl_dummy_resetonswitch");
	EXPECT_EQ(vEntries[1].m_VarName, "qm_deepfly_mode");
	EXPECT_EQ(vEntries[2].m_VarName, "cl_dummy_control");
	EXPECT_EQ(vEntries[3].m_VarName, "cl_dummy_copy_moves");

	// 报告截图中的取值：卡键 1(Off) / 锤 0(Normal) / 分控 0(Off) / 同步 0(Off)
	const int aValues[4] = {1, 0, 0, 0};
	const char *apExpected[4] = {"Key Sticking: Off", "Hammer: Normal", "Dummy Control: Off", "Dummy copy: Off"};
	int VisibleLines = 0;
	for(size_t i = 0; i < vEntries.size(); ++i)
	{
		std::string Text;
		ASSERT_TRUE(QmResolveBindStatusEntry(vEntries[i], aValues[i], Text)) << vEntries[i].m_VarName;
		EXPECT_EQ(Text, apExpected[i]);
		++VisibleLines;
	}

	const SQmBindStatusPanelSize Size = QmComputeBindStatusPanelSize(VisibleLines, 100.0f, 9.0f, 4.0f, 3.0f);
	EXPECT_GE(Size.m_H, 9.0f * (float)VisibleLines);
}

// 回归：自定义列表生效时内置四项一条都不画。此前内置的锤/分控/同步三行会被追加到自定义行
// 之后，画到按自定义行数算出的面板背景之外（报告截图中的溢出）
TEST(QmBindStatusHud, CustomListReplacesBuiltinLinesInsteadOfAppendingThem)
{
	// 自定义行与内置行用互不相同的文本，便于断言内置行一条都没混进来
	const std::vector<std::string> vCustomLines = {
		"Key Sticking: Off",
		"Hammer: Normal",
		"Dummy Control: Off",
		"Dummy copy: Off",
	};
	const std::vector<SQmBindStatusBuiltinLine> vBuiltinLines = {
		{true, "Key Sticking: Reset Self", EQmBindStatusTone::WARNING},
		{true, "Hammer: HDF", EQmBindStatusTone::WARNING},
		{true, "Dummy Control: On", EQmBindStatusTone::OK},
		{true, "Dummy copy: On", EQmBindStatusTone::OK},
	};

	const std::vector<SQmBindStatusRenderLine> vLines = QmBuildBindStatusRenderLines(true, vCustomLines, vBuiltinLines);

	ASSERT_EQ(vLines.size(), vCustomLines.size());
	for(size_t i = 0; i < vLines.size(); ++i)
	{
		EXPECT_EQ(vLines[i].m_Text, vCustomLines[i]);
		// 自定义条目没有状态语义，不套用内置配色
		EXPECT_EQ(vLines[i].m_Tone, EQmBindStatusTone::NONE);
	}
	for(const SQmBindStatusBuiltinLine &Builtin : vBuiltinLines)
	{
		for(const SQmBindStatusRenderLine &Line : vLines)
			EXPECT_NE(Line.m_Text, std::string(Builtin.m_pText));
	}

	// 面板高度由实际绘制行数推导，背景包住每一行
	const float LineHeight = 9.0f;
	const SQmBindStatusPanelSize Size = QmComputeBindStatusPanelSize((int)vLines.size(), 100.0f, LineHeight, 4.0f, 3.0f);
	EXPECT_GE(Size.m_H, LineHeight * (float)vLines.size());
}

// 自定义列表未生效时按内置四项绘制：只画开启了显示的行，保持绘制顺序与状态配色
TEST(QmBindStatusHud, BuiltinLinesAreUsedWhenCustomListIsInactive)
{
	const std::vector<SQmBindStatusBuiltinLine> vBuiltinLines = {
		{false, "Key Sticking: Off", EQmBindStatusTone::DANGER}, // 该项未开启显示
		{true, "Hammer: Normal", EQmBindStatusTone::OK},
		{true, "Dummy Control: Off", EQmBindStatusTone::OK},
		{false, "Dummy copy: Off", EQmBindStatusTone::OK}, // 该项未开启显示
	};

	const std::vector<SQmBindStatusRenderLine> vLines = QmBuildBindStatusRenderLines(false, {}, vBuiltinLines);

	ASSERT_EQ(vLines.size(), 2u);
	EXPECT_EQ(vLines[0].m_Text, "Hammer: Normal");
	EXPECT_EQ(vLines[0].m_Tone, EQmBindStatusTone::OK);
	EXPECT_EQ(vLines[1].m_Text, "Dummy Control: Off");
	EXPECT_EQ(vLines[1].m_Tone, EQmBindStatusTone::OK);
}

// 自定义列表生效但没有任何可见行时不绘制：内置四项也不能回退显示，
// 否则会画在按零行算出的空面板之外
TEST(QmBindStatusHud, EmptyCustomListDrawsNothingInsteadOfFallingBackToBuiltin)
{
	const std::vector<SQmBindStatusBuiltinLine> vBuiltinLines = {
		{true, "Hammer: Normal", EQmBindStatusTone::OK},
	};

	const std::vector<SQmBindStatusRenderLine> vLines = QmBuildBindStatusRenderLines(true, {}, vBuiltinLines);
	EXPECT_TRUE(vLines.empty());

	const SQmBindStatusPanelSize Size = QmComputeBindStatusPanelSize((int)vLines.size(), 0.0f, 9.0f, 4.0f, 3.0f);
	EXPECT_FLOAT_EQ(Size.m_W, 0.0f);
	EXPECT_FLOAT_EQ(Size.m_H, 0.0f);
}

// 绘制行数变化时面板高度同步变化：条目增减不会残留固定高度，也不会漏包最后一行
TEST(QmBindStatusHud, PanelHeightFollowsDrawnLineCount)
{
	const std::vector<SQmBindStatusBuiltinLine> vBuiltinLines = {
		{true, "Hammer: Normal", EQmBindStatusTone::OK},
		{true, "Dummy Control: Off", EQmBindStatusTone::OK},
	};
	const float LineHeight = 9.0f;
	const float PaddingY = 3.0f;

	float PrevHeight = 0.0f;
	for(int Count = 1; Count <= 12; ++Count)
	{
		std::vector<std::string> vCustomLines;
		for(int i = 0; i < Count; ++i)
			vCustomLines.push_back("Line " + std::to_string(i));

		const std::vector<SQmBindStatusRenderLine> vLines = QmBuildBindStatusRenderLines(true, vCustomLines, vBuiltinLines);
		ASSERT_EQ(vLines.size(), (size_t)Count) << "Count=" << Count;

		const SQmBindStatusPanelSize Size = QmComputeBindStatusPanelSize((int)vLines.size(), 40.0f, LineHeight, 4.0f, PaddingY);
		// 背景包住每一行（含上下内边距）
		EXPECT_FLOAT_EQ(Size.m_H, LineHeight * (float)Count + PaddingY * 2.0f);
		// 行数增加时高度必须增长
		EXPECT_GT(Size.m_H, PrevHeight);
		PrevHeight = Size.m_H;
	}
}

// 关闭彩虹色 HUD 后内置四项的语义配色：开/正常=绿(OK)，关/DF=红(DANGER)，Reset Self/HDF/Custom=黄(WARNING)
TEST(QmBindStatusHud, BuiltInTonesClassifyKeyStickingByValue)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 0), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 1), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 2), EQmBindStatusTone::WARNING);
	// 越界值只显示 "Key Sticking: ?"，不参与配色
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 3), EQmBindStatusTone::NONE);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, -1), EQmBindStatusTone::NONE);
}

TEST(QmBindStatusHud, BuiltInTonesClassifyHammerByValue)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 0), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 1), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 2), EQmBindStatusTone::WARNING);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 3), EQmBindStatusTone::WARNING);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 4), EQmBindStatusTone::NONE);
}

TEST(QmBindStatusHud, BuiltInTonesClassifyDummySwitches)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_CONTROL, 0), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_CONTROL, 1), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_COPY, 0), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_COPY, 1), EQmBindStatusTone::OK);
}
