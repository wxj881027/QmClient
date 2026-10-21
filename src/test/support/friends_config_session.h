#ifndef TEST_SUPPORT_FRIENDS_CONFIG_SESSION_H
#define TEST_SUPPORT_FRIENDS_CONFIG_SESSION_H

#include <engine/client/friends.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <memory>

// 每次使用独立的生产控制台和配置管理器；调用方让隔离存储覆盖整个会话生命周期。
class CFriendsConfigSession
{
	std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
	std::unique_ptr<IConsole> m_pConsole{CreateConsole(CFGFLAG_CLIENT)};
	std::unique_ptr<IConfigManager> m_pConfig{CreateConfigManager()};
	std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();

public:
	explicit CFriendsConfigSession(IStorage *pStorage)
	{
		m_pKernel->RegisterInterface(pStorage, false);
		m_pKernel->RegisterInterface(m_pConsole.get(), false);
		m_pKernel->RegisterInterface(m_pConfig.get(), false);
		m_pKernel->RegisterInterface(m_pFriends.get(), false);
		m_pConsole->Init();
		m_pConfig->Init();
		m_pFriends->Init();
		m_pConsole->StoreCommands(false);
		g_Config.m_ClSaveSettings = 1;
		g_Config.m_ClFriendsIgnoreClan = 0;
	}

	CFriends *Friends() { return m_pFriends.get(); }
	IConsole *Console() { return m_pConsole.get(); }
	IConfigManager *Config() { return m_pConfig.get(); }
	bool Load()
	{
		return m_pConsole->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE, true);
	}
};

#endif
