// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qm_console_log_filter.h>

#include <gtest/gtest.h>

// 控制台分类按钮是「风扇式多选」：分类只认日志行的 system 名，不靠内容关键字猜。
// 这里覆盖分类边界、整行解析与掩码匹配三块。

TEST(QmConsoleLogFilter, BindsSystemIsItsOwnCategory)
{
	EXPECT_EQ(QmClassifyConsoleLogSystem("binds"), QM_CONSOLE_LOG_CATEGORY_BINDS);
	EXPECT_EQ(QmClassifyConsoleLogSystem("binds/extra"), QM_CONSOLE_LOG_CATEGORY_BINDS);
	// 前缀相同但不同 system 不能被吞进 Binds
	EXPECT_EQ(QmClassifyConsoleLogSystem("bindsomething"), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmNormalizeConsoleLogFilterMask(QM_CONSOLE_LOG_CATEGORY_BINDS), QM_CONSOLE_LOG_CATEGORY_BINDS);
}

TEST(QmConsoleLogFilter, PlayerChatIsPlayerCategory)
{
	EXPECT_EQ(QmClassifyConsoleLogSystem("chat/all"), QM_CONSOLE_LOG_CATEGORY_PLAYER);
	EXPECT_EQ(QmClassifyConsoleLogSystem("chat/team"), QM_CONSOLE_LOG_CATEGORY_PLAYER);
	EXPECT_EQ(QmClassifyConsoleLogSystem("chat/whisper"), QM_CONSOLE_LOG_CATEGORY_PLAYER);
	// system 名大小写不敏感
	EXPECT_EQ(QmClassifyConsoleLogSystem("CHAT/TEAM"), QM_CONSOLE_LOG_CATEGORY_PLAYER);
}

TEST(QmConsoleLogFilter, CommandEchoesAreCommandCategory)
{
	// 你敲命令的服务器回复
	EXPECT_EQ(QmClassifyConsoleLogSystem("chatresp"), QM_CONSOLE_LOG_CATEGORY_COMMAND);
	// exec / echo / 报错提示
	EXPECT_EQ(QmClassifyConsoleLogSystem("console"), QM_CONSOLE_LOG_CATEGORY_COMMAND);
	// 脚本与菜单输出
	EXPECT_EQ(QmClassifyConsoleLogSystem("chat/client"), QM_CONSOLE_LOG_CATEGORY_COMMAND);
}

TEST(QmConsoleLogFilter, ServerAndEngineLogsAreSystemCategory)
{
	// 服务器播报与投票走 chat/server，与玩家说话分开
	EXPECT_EQ(QmClassifyConsoleLogSystem("chat/server"), QM_CONSOLE_LOG_CATEGORY_COMMAND);
	EXPECT_EQ(QmClassifyConsoleLogSystem("client"), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogSystem("client/network"), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogSystem("gfx/vulkan"), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogSystem("storage"), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogSystem(nullptr), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogSystem(""), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
}

TEST(QmConsoleLogFilter, ClassifiesWholeLogLineBySystemName)
{
	const char *pBindLine = "2026-09-11 20:45:42 I binds: bound w = +weapon1";
	EXPECT_EQ(QmClassifyConsoleLogLine(pBindLine, str_length(pBindLine)), QM_CONSOLE_LOG_CATEGORY_BINDS);

	const char *pPlayerLine = "2026-09-11 20:45:42 I chat/team: hd: go left";
	EXPECT_EQ(QmClassifyConsoleLogLine(pPlayerLine, str_length(pPlayerLine)), QM_CONSOLE_LOG_CATEGORY_PLAYER);

	const char *pExecLine = "2026-09-11 20:50:40 I console: executing 'bind\\shift\\s.cfg'";
	EXPECT_EQ(QmClassifyConsoleLogLine(pExecLine, str_length(pExecLine)), QM_CONSOLE_LOG_CATEGORY_COMMAND);

	const char *pMenuLine = "2026-09-11 20:50:40 I chat/client: === 车车 菜单 ===";
	EXPECT_EQ(QmClassifyConsoleLogLine(pMenuLine, str_length(pMenuLine)), QM_CONSOLE_LOG_CATEGORY_COMMAND);

	// 内容里出现 "binds" 字样但 system 不是 binds，不应误判
	const char *pTrapLine = "2026-09-11 20:45:42 I chat/all: someone: binds are broken";
	EXPECT_EQ(QmClassifyConsoleLogLine(pTrapLine, str_length(pTrapLine)), QM_CONSOLE_LOG_CATEGORY_PLAYER);

	// 取不到 system 名时退化为系统类，而不是崩或者误判
	const char *pNoSystem = "plain message without system prefix";
	EXPECT_EQ(QmClassifyConsoleLogLine(pNoSystem, str_length(pNoSystem)), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogLine(nullptr, 0), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
	EXPECT_EQ(QmClassifyConsoleLogLine("", 0), QM_CONSOLE_LOG_CATEGORY_SYSTEM);
}

TEST(QmConsoleLogFilter, ExtractConsoleLogSystem)
{
	char aBuf[64];
	const char *pLine = "2026-09-11 20:45:42 I binds: bound w = +weapon1";
	ASSERT_TRUE(QmExtractConsoleLogSystem(pLine, str_length(pLine), aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "binds");

	const char *pPlayerLine = "2026-09-11 20:45:42 I chat/whisper: hd: hi";
	ASSERT_TRUE(QmExtractConsoleLogSystem(pPlayerLine, str_length(pPlayerLine), aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "chat/whisper");

	// 只有长度限制到冒号前时，system 名不完整
	ASSERT_TRUE(QmExtractConsoleLogSystem(pLine, 24, aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "I");

	EXPECT_FALSE(QmExtractConsoleLogSystem("no separator here", 17, aBuf, sizeof(aBuf)));
	EXPECT_FALSE(QmExtractConsoleLogSystem(nullptr, 0, aBuf, sizeof(aBuf)));
}

TEST(QmConsoleLogFilter, FilterMaskSelectsCategories)
{
	// 只点亮 Binds：只有 binds 行通过
	EXPECT_TRUE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_BINDS, QM_CONSOLE_LOG_CATEGORY_BINDS));
	EXPECT_FALSE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_PLAYER, QM_CONSOLE_LOG_CATEGORY_BINDS));
	EXPECT_FALSE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_COMMAND, QM_CONSOLE_LOG_CATEGORY_BINDS));
	EXPECT_FALSE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_SYSTEM, QM_CONSOLE_LOG_CATEGORY_BINDS));

	// 玩家 + 系统：聊天与引擎日志显示，指令与 binds 隐藏
	const int PlayerAndSystem = QM_CONSOLE_LOG_CATEGORY_PLAYER | QM_CONSOLE_LOG_CATEGORY_SYSTEM;
	EXPECT_TRUE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_PLAYER, PlayerAndSystem));
	EXPECT_TRUE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_SYSTEM, PlayerAndSystem));
	EXPECT_FALSE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_COMMAND, PlayerAndSystem));
	EXPECT_FALSE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_BINDS, PlayerAndSystem));

	// 全亮：全部通过
	for(const int Category : {QM_CONSOLE_LOG_CATEGORY_SYSTEM, QM_CONSOLE_LOG_CATEGORY_PLAYER, QM_CONSOLE_LOG_CATEGORY_COMMAND, QM_CONSOLE_LOG_CATEGORY_BINDS})
	{
		EXPECT_TRUE(QmConsoleLogCategoryPassesFilter(Category, QM_CONSOLE_LOG_CATEGORY_ALL));
	}
}

TEST(QmConsoleLogFilter, EmptyMaskFallsBackToAllCategories)
{
	// 全部熄灭会让控制台一行不剩且无按钮可点回来，因此归一化为全亮
	EXPECT_EQ(QmNormalizeConsoleLogFilterMask(0), QM_CONSOLE_LOG_CATEGORY_ALL);
	EXPECT_TRUE(QmConsoleLogCategoryPassesFilter(QM_CONSOLE_LOG_CATEGORY_BINDS, 0));

	// 非法位被清掉
	EXPECT_EQ(QmNormalizeConsoleLogFilterMask(0x7F), QM_CONSOLE_LOG_CATEGORY_ALL);
	EXPECT_EQ(QmNormalizeConsoleLogFilterMask(1 << 20), QM_CONSOLE_LOG_CATEGORY_ALL);
	EXPECT_EQ(QmNormalizeConsoleLogFilterMask(QM_CONSOLE_LOG_CATEGORY_PLAYER | (1 << 20)), QM_CONSOLE_LOG_CATEGORY_PLAYER);
}
