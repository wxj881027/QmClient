#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/dummy_hammer_input.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <vector>

namespace
{
	class CQmDeepflyBind : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_pConsole = CreateConsole(CFGFLAG_CLIENT);
			m_pConfigManager.reset(CreateConfigManager());
			m_pKernel->RegisterInterface(m_pStorage.get(), false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfigManager.get(), false);
			m_pConsole->Init();
			m_pConfigManager->Init();
			m_pConsole->StoreCommands(false);
			g_Config.m_ClDummyHammer = 0;
			// 用事件记录器替换主控开火消费者；配置解析和分身输入使用生产实现。
			m_pConsole->Register("+fire", "", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
				static_cast<CQmDeepflyBind *>(pUser)->m_vFireStrokes.push_back(pResult->GetInteger(0));
			}, this, "");
			m_pConsole->Chain("cl_dummy_hammer", [](IConsole::IResult *pResult, void *pUser, IConsole::FCommandCallback pfnCallback, void *pCallbackUser) {
				pfnCallback(pResult, pCallbackUser);
				if(pResult->NumArguments())
					static_cast<CQmDeepflyBind *>(pUser)->m_Hammer.SetEnabled(g_Config.m_ClDummyHammer != 0);
			}, this);
		}

		void TearDown() override { g_Config = m_PreviousConfig; }

		bool Snap()
		{
			m_Hammer.SynchronizeEnabled(g_Config.m_ClDummyHammer != 0);
			return m_Hammer.SnapInput(m_Output, m_BaseInput, vec2(30.0f, -40.0f), true, false);
		}

		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IConfigManager> m_pConfigManager;
		CQmDummyHammerInput m_Hammer;
		CNetObj_PlayerInput m_BaseInput{};
		CNetObj_PlayerInput m_Output{};
		std::vector<int> m_vFireStrokes;
	};
}

TEST_F(CQmDeepflyBind, HdfPressAndReleaseBeforeSamplingRetainsTheHammerRequest)
{
	m_pConsole->ExecuteLineStroked(1, "+toggle cl_dummy_hammer 1 0");
	m_pConsole->ExecuteLineStroked(0, "+toggle cl_dummy_hammer 1 0");
	EXPECT_EQ(g_Config.m_ClDummyHammer, 0);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
	EXPECT_EQ(m_Output.m_Fire & 1, 0);
	EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_HAMMER + 1);
}

TEST_F(CQmDeepflyBind, DfShortClickPreservesBothMainFireStrokesAndTheDummyHammer)
{
	const char *pCommand = "+fire; +toggle cl_dummy_hammer 1 0";
	m_pConsole->ExecuteLineStroked(1, pCommand);
	m_pConsole->ExecuteLineStroked(0, pCommand);
	EXPECT_EQ(m_vFireStrokes, (std::vector<int>{1, 0}));
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
	EXPECT_EQ(m_Output.m_Fire & 1, 0);
}

TEST_F(CQmDeepflyBind, ReadingHammerConfigurationDoesNotCreateAPress)
{
	m_pConsole->ExecuteLine("cl_dummy_hammer");
	EXPECT_FALSE(Snap());
}
