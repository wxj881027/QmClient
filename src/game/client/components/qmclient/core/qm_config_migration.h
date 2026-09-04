/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_CONFIG_MIGRATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_CONFIG_MIGRATION_H

#include <engine/console.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class IConfigManager;

/**
 * 启动阶段的旧配置兼容层。
 *
 * 迁移规则只描述旧键到新键的映射，不拥有任何业务状态，也不依赖旧
 * 客户端聚合器。旧命令通过官方 console 解析器校验和写入新配置。
 */
class CQmConfigMigration final
{
public:
	static constexpr int CURRENT_VERSION = 1;
	static constexpr size_t MAX_CAPTURED_COMMANDS = 64;
	static constexpr size_t MAX_CAPTURED_BYTES = 16 * 1024;

	struct SRule
	{
		const char *m_pLegacyName;
		const char *m_pCurrentName;
		bool m_IsInteger;
		int m_Min;
		int m_Max;
	};

 static constexpr size_t RULE_COUNT = 14;

	void OnConsoleInit(IConsole *pConsole);
	bool OnUnknownCommand(const char *pCommand, IConfigManager *pConfigManager = nullptr);
	void OnConfigLoaded(IConsole *pConsole, IConfigManager *pConfigManager = nullptr);

	static const std::array<SRule, RULE_COUNT> &Rules();
	static bool ShouldApply(bool CurrentCommandSeen);
	static std::optional<std::string> ExtractCommandSegment(const char *pCommand);

private:
	struct SCapturedCommand
	{
		size_t m_RuleIndex;
		std::string m_Command;
	};

	struct SChainContext
	{
		CQmConfigMigration *m_pOwner;
		size_t m_RuleIndex;
	};

	IConsole *m_pConsole = nullptr;
	std::array<bool, RULE_COUNT> m_aCurrentCommandsSeen{};
	std::array<SChainContext, RULE_COUNT> m_aChainContexts{};
	std::vector<SCapturedCommand> m_vCapturedCommands;
	size_t m_CapturedCommandBytes = 0;
	bool m_HasUnmigratedCommands = false;
	bool m_Loading = false;

	static void OnCurrentCommand(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static const SRule *FindLegacyRule(const char *pCommand, size_t *pRuleIndex);
	static const char *SkipSpaces(const char *pString);
	static std::optional<std::string> TranslateCommand(const char *pCommand, const SRule &Rule);
	bool PreserveCommand(const char *pCommand, IConfigManager *pConfigManager, const char *pReason);
};

#endif
