#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/bind_workshop.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 绑定工坊内置 cfg 的真实文件链路：生产代码生成正文与相对路径，
// 测试把正文写成真实 .cfg，再交给真实控制台的 exec 读取。
// 控制台里缺失的客户端命令用无操作桩替代，未知命令一律记录；
// bind 由测试桩记录最终键位，验证文件内容、路径与执行顺序。
//
// 这里测试的是文件边界与命令内容，不启动客户端，也不替代实机按键验收。

namespace
{
	const char *const s_apNoopEmptyCommands[] = {
		"+fire",
		"+jump",
		"+left",
		"+right",
		"+weapon1",
		"dummy_reset",
	};

	const char *const s_apNoopValueCommands[] = {
		"cl_dummy_control",
		"cl_dummy_copy_moves",
		"cl_dummy_hammer",
		"cl_dummy_resetonswitch",
		"cl_dummy_restore_weapon",
		"cl_message_client_color",
		"cl_show_hook_coll_own",
	};

	class CWorkshopCfgSession
	{
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole;

		static void Noop(IConsole::IResult *pResult, void *pUser) {}

		static void ConBind(IConsole::IResult *pResult, void *pUser)
		{
			auto *pSession = static_cast<CWorkshopCfgSession *>(pUser);
			const std::string Key = pResult->GetString(0);
			const std::string Command = pResult->NumArguments() > 1 ? pResult->GetString(1) : "";
			pSession->m_vBinds.emplace_back(Key, Command);
			pSession->m_BindMap[Key] = Command;
		}

		static void ConSentinel(IConsole::IResult *pResult, void *pUser)
		{
			static_cast<CWorkshopCfgSession *>(pUser)->m_Sentinel++;
		}

		static bool OnUnknown(const char *pCommand, void *pUser)
		{
			static_cast<CWorkshopCfgSession *>(pUser)->m_vUnknown.emplace_back(pCommand);
			return true;
		}

	public:
		std::vector<std::pair<std::string, std::string>> m_vBinds;
		std::map<std::string, std::string> m_BindMap;
		std::vector<std::string> m_vUnknown;
		int m_Sentinel = 0;

		CWorkshopCfgSession(IStorage *pStorage, int Flags) :
			m_pConsole(CreateConsole(Flags))
		{
			m_pKernel->RegisterInterface(pStorage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pConsole->Init();
			m_pConsole->StoreCommands(false);
			m_pConsole->SetUnknownCommandCallback(OnUnknown, this);

			m_pConsole->Register("bind", "s[key] ?r[command]", CFGFLAG_CLIENT, ConBind, this, "");
			// 客户端由 CChat 注册 echo；引擎内置 echo 只开放给服务器控制台。
			m_pConsole->Register("echo", "r[text]", CFGFLAG_CLIENT, Noop, this, "");
			m_pConsole->Register("sentinel", "", CFGFLAG_CLIENT, ConSentinel, this, "");
			m_pConsole->Register("+toggle", "s[a] s[b] s[c]", CFGFLAG_CLIENT, Noop, this, "");
			m_pConsole->Register("+toggle_restore", "s[a] s[b]", CFGFLAG_CLIENT, Noop, this, "");
			for(const char *pCommand : s_apNoopEmptyCommands)
				m_pConsole->Register(pCommand, "", CFGFLAG_CLIENT, Noop, this, "");
			for(const char *pCommand : s_apNoopValueCommands)
				m_pConsole->Register(pCommand, "s[value]", CFGFLAG_CLIENT, Noop, this, "");
		}

		IConsole *Console() { return m_pConsole.get(); }

		void Reset()
		{
			m_vBinds.clear();
			m_BindMap.clear();
			m_vUnknown.clear();
			m_Sentinel = 0;
		}
	};

	size_t CountBindLines(std::string_view Content)
	{
		size_t Count = 0;
		size_t Start = 0;
		while(Start < Content.size())
		{
			const size_t End = Content.find('\n', Start);
			const std::string_view Line = Content.substr(Start, End == std::string_view::npos ? Content.size() - Start : End - Start);
			if(Line.starts_with("bind "))
				Count++;
			if(End == std::string_view::npos)
				break;
			Start = End + 1;
		}
		return Count;
	}

	class CBindWorkshopCfgFileTest : public ::testing::Test
	{
	protected:
		const std::filesystem::path m_PreviousCurrentDirectory = std::filesystem::current_path();
		// 内置 cfg 有 15 份以上，超出 CTestInfo 清理器的 10 项上限；
		// 本夹具使用独立临时目录并自行删除。
		std::filesystem::path m_Root;
		std::filesystem::path m_Save;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CWorkshopCfgSession> m_pSession;

		void SetUp() override
		{
			m_Root = std::filesystem::temp_directory_path() /
				 ("qm_bind_workshop_cfg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			m_Save = m_Root / "save";
			std::filesystem::create_directories(m_Save);
			const std::string Save = m_Save.string();
			const char *apArgs[] = {Save.c_str()};
			m_pStorage = CreateTempStorage(Save.c_str(), 1, apArgs);
			ASSERT_NE(m_pStorage, nullptr);
			m_pSession = std::make_unique<CWorkshopCfgSession>(m_pStorage.get(), CFGFLAG_CLIENT);
		}

		void TearDown() override
		{
			m_pSession.reset();
			m_pStorage.reset();
			std::filesystem::current_path(m_PreviousCurrentDirectory);
			std::error_code Error;
			std::filesystem::remove_all(m_Root, Error);
		}

		void WriteFile(const std::string &RelativePath, std::string_view Content)
		{
			const std::filesystem::path Path = m_Save / RelativePath;
			std::filesystem::create_directories(Path.parent_path());
			std::ofstream File(Path, std::ios::binary);
			ASSERT_TRUE(File.is_open()) << Path;
			File.write(Content.data(), static_cast<std::streamsize>(Content.size()));
			ASSERT_TRUE(File.good()) << Path;
		}

		void Execute(const qm_bind_workshop::SPreset &Preset, size_t Variant)
		{
			const std::string Content = qm_bind_workshop::BuildCfgContent(Preset, Variant);
			ASSERT_FALSE(Content.empty()) << Preset.m_Id;
			ASSERT_EQ(qm_bind_workshop::FindCfgViolation(Content), 0u) << Preset.m_Id;

			const std::string FileName = qm_bind_workshop::CfgFileName(Preset, Variant);
			ASSERT_EQ(qm_bind_workshop::CfgExecCommand(Preset, Variant), "exec " + FileName);
			WriteFile(FileName, Content + "\nsentinel\n");

			m_pSession->Reset();
			m_pSession->Console()->ExecuteLine(("exec " + FileName).c_str());
			EXPECT_EQ(m_pSession->m_Sentinel, 1) << FileName;
			std::string Unknown;
			for(const std::string &Command : m_pSession->m_vUnknown)
				Unknown += " [" + Command + "]";
			EXPECT_TRUE(m_pSession->m_vUnknown.empty()) << FileName << " unknown:" << Unknown;
			EXPECT_EQ(m_pSession->m_vBinds.size(), CountBindLines(Content)) << FileName;
			for(const auto &[Key, Command] : m_pSession->m_vBinds)
			{
				EXPECT_FALSE(Key.empty()) << FileName;
				EXPECT_FALSE(Command.empty()) << FileName << " key " << Key;
			}
		}

		const qm_bind_workshop::SPreset *FindPreset(const char *pId) const
		{
			for(const auto &Preset : qm_bind_workshop::BuiltinPresets())
				if(Preset.m_Id == pId)
					return &Preset;
			return nullptr;
		}

		size_t FindVariant(const qm_bind_workshop::SPreset &Preset, const char *pLabel) const
		{
			for(size_t i = 0; i < Preset.m_vVariants.size(); i++)
				if(Preset.m_vVariants[i].m_Label == pLabel)
					return i;
			return Preset.m_vVariants.size();
		}
	};
}

TEST_F(CBindWorkshopCfgFileTest, EveryBuiltinCfgExecutesFromARealFile)
{
	size_t NumExecuted = 0;
	for(const qm_bind_workshop::SPreset &Preset : qm_bind_workshop::BuiltinPresets())
	{
		for(size_t Variant = 0; Variant < Preset.m_vVariants.size(); Variant++)
		{
			if(!qm_bind_workshop::HasBuiltinCfg(Preset, Variant))
				continue;
			Execute(Preset, Variant);
			NumExecuted++;
		}
	}
	EXPECT_GT(NumExecuted, 10u);
}

TEST_F(CBindWorkshopCfgFileTest, DfHdfCfgBindsRemoveStuckHammerSequence)
{
	const auto *pPreset = FindPreset("df_hdf_switch");
	ASSERT_NE(pPreset, nullptr);
	const size_t Variant = FindVariant(*pPreset, "No stuck");
	ASSERT_LT(Variant, pPreset->m_vVariants.size());

	Execute(*pPreset, Variant);

	EXPECT_EQ(m_pSession->m_BindMap["mouse1"], "dummy_reset; +fire; +toggle cl_dummy_hammer 1 0");
	EXPECT_EQ(m_pSession->m_BindMap["mouse2"], "+hook");
}

TEST_F(CBindWorkshopCfgFileTest, AutoHammerCfgPointsToItsCompanionFile)
{
	const auto *pPreset = FindPreset("auto_hammer");
	ASSERT_NE(pPreset, nullptr);
	const size_t Variant = FindVariant(*pPreset, "On");
	ASSERT_LT(Variant, pPreset->m_vVariants.size());

	Execute(*pPreset, Variant);

	EXPECT_EQ(m_pSession->m_BindMap["mouse1"], "+fire; +toggle cl_dummy_hammer 1 0");
	const auto OffFile = m_pSession->m_BindMap.find("1");
	ASSERT_NE(OffFile, m_pSession->m_BindMap.end());
	const size_t OffVariant = FindVariant(*pPreset, "Off");
	ASSERT_LT(OffVariant, pPreset->m_vVariants.size());
	EXPECT_EQ(OffFile->second, qm_bind_workshop::CfgExecCommand(*pPreset, OffVariant));
}

TEST_F(CBindWorkshopCfgFileTest, WeaponMasterKeepsNestedBindQuoted)
{
	const auto *pPreset = FindPreset("weapon_master");
	ASSERT_NE(pPreset, nullptr);
	const size_t Variant = FindVariant(*pPreset, "On");
	ASSERT_LT(Variant, pPreset->m_vVariants.size());

	Execute(*pPreset, Variant);

	const auto Nested = m_pSession->m_BindMap.find("3");
	ASSERT_NE(Nested, m_pSession->m_BindMap.end());
	EXPECT_EQ(Nested->second, "bind mouse1 \"+weapon3; +fire\"");
}
