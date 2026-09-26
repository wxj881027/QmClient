// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	// 一个独立内核 + 存储 + 配置管理器，模拟客户端“启动加载 / 退出保存”两个进程生命周期。
	// 构造时配置变量会被重置为默认值，与实际进程在读取配置文件之前的状态一致。
	class CQmConfigSession
	{
	public:
		explicit CQmConfigSession(IStorage *pStorage) :
			m_pKernel(IKernel::Create()),
			m_pConsole(CreateConsole(CFGFLAG_CLIENT)),
			m_pConfigManager(CreateConfigManager())
		{
			m_pKernel->RegisterInterface(pStorage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfigManager.get(), false);
			m_pConsole->Init();
			m_pConfigManager->Init();
		}

		IConsole *Console() { return m_pConsole.get(); }
		IConfigManager *Config() { return m_pConfigManager.get(); }

	private:
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IConfigManager> m_pConfigManager;
	};

	std::string ReadSavedConfigText(IStorage *pStorage)
	{
		IOHANDLE File = pStorage->OpenFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return {};
		char *pText = io_read_all_str(File);
		io_close(File);
		const std::string Text = pText != nullptr ? pText : "";
		free(pText);
		return Text;
	}

	// g_Config 是全局对象，用例必须恢复原状，避免影响同进程内的其他测试。
	struct SConfigRestore
	{
		CConfig m_Config = g_Config;
		~SConfigRestore() { g_Config = m_Config; }
	};
} // namespace

// 回归：禅模式把配置项临时改成隐藏值（例如 cl_show_direction 3 -> 0）后退出游戏时，
// 配置文件里曾经记录的是隐藏值；下次启动无法再分辨，关闭禅模式也恢复不了用户设置。
TEST(QmConfigSaveValueOverride, KeepsUserValueWhenRuntimeValueIsTemporarilyOverridden)
{
	SConfigRestore ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	{
		CQmConfigSession Session(pStorage.get());
		g_Config.m_ClSaveSettings = 1;
		// 用户真实设置是 3，禅模式临时隐藏为 0，并登记写盘覆盖。
		g_Config.m_ClShowDirection = 0;
		Session.Config()->SetSaveValueOverride("cl_show_direction", true, 3);
		ASSERT_TRUE(Session.Config()->Save());
		EXPECT_NE(ReadSavedConfigText(pStorage.get()).find("cl_show_direction 3"), std::string::npos);
	}

	{
		CQmConfigSession Session(pStorage.get());
		ASSERT_TRUE(Session.Console()->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE));
		EXPECT_EQ(g_Config.m_ClShowDirection, 3);
	}
}

// 用户真实值恰好等于默认值时，隐藏值同样不能落盘；重启后应当回到默认值（这里是开启 HUD）。
TEST(QmConfigSaveValueOverride, OmitsVariableWhoseUserValueIsDefault)
{
	SConfigRestore ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	{
		CQmConfigSession Session(pStorage.get());
		g_Config.m_ClSaveSettings = 1;
		g_Config.m_ClShowhud = 0;
		Session.Config()->SetSaveValueOverride("cl_showhud", true, DefaultConfig::ClShowhud);
		ASSERT_TRUE(Session.Config()->Save());
		EXPECT_EQ(ReadSavedConfigText(pStorage.get()).find("cl_showhud 0"), std::string::npos);
	}

	{
		CQmConfigSession Session(pStorage.get());
		ASSERT_TRUE(Session.Console()->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE));
		EXPECT_EQ(g_Config.m_ClShowhud, DefaultConfig::ClShowhud);
	}
}

// 覆盖解除后（关闭禅模式，或用户在禅模式中自行修改过该值）必须恢复普通写盘行为。
TEST(QmConfigSaveValueOverride, PersistsRuntimeValueAfterOverrideIsCleared)
{
	SConfigRestore ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	{
		CQmConfigSession Session(pStorage.get());
		g_Config.m_ClSaveSettings = 1;
		Session.Config()->SetSaveValueOverride("cl_show_direction", true, 3);
		Session.Config()->SetSaveValueOverride("cl_show_direction", false);
		g_Config.m_ClShowDirection = 2;
		ASSERT_TRUE(Session.Config()->Save());
		EXPECT_NE(ReadSavedConfigText(pStorage.get()).find("cl_show_direction 2"), std::string::npos);
	}

	{
		CQmConfigSession Session(pStorage.get());
		ASSERT_TRUE(Session.Console()->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE));
		EXPECT_EQ(g_Config.m_ClShowDirection, 2);
	}
}
