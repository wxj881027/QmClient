#include <engine/storage.h>

#include <game/client/components/qmclient/bind_workshop.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

using namespace qm_bind_workshop;

namespace
{
	std::string ReadCfg(IStorage &Storage, const char *pFileName)
	{
		std::unique_ptr<char, decltype(&std::free)> pContent(Storage.ReadFileStr(pFileName, IStorage::TYPE_SAVE), std::free);
		return pContent ? pContent.get() : "";
	}
}

TEST(QmBindWorkshopCfgSave, RejectedFlowPreservesItsLastExport)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CFlowCollection Flows;
	CBlueprintGraph &Graph = Flows.Graph(0);
	SNode Event;
	Event.m_Type = ENodeType::EVENT_PRESS;
	const int EventId = Graph.AddNode(Event);
	SNode Action;
	Action.m_Type = ENodeType::ACTION_COMMAND;
	Action.m_Command = "echo example";
	const int ActionId = Graph.AddNode(Action);
	ASSERT_TRUE(Graph.Connect(EventId, ActionId));
	const SCfgBundle Exported = Flows.BuildCfg(0, "h");
	ASSERT_TRUE(SaveBlueprintCfg(*pStorage, Exported));
	ASSERT_TRUE(Graph.RemoveNode(ActionId));
	const auto vBundles = Flows.BuildAllCfg("h");
	ASSERT_EQ(vBundles.size(), 1u);
	ASSERT_FALSE(vBundles.front().m_Ok);
	EXPECT_FALSE(SaveBlueprintCfg(*pStorage, vBundles.front()));
	EXPECT_EQ(ReadCfg(*pStorage, Exported.m_FileName.c_str()), Exported.m_Content);
}

TEST(QmBindWorkshopCfgSave, ReexportReplacesTheWholeCfg)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(SaveCfgFile(*pStorage, "flow.cfg", "echo original longer line\n"));
	ASSERT_TRUE(SaveCfgFile(*pStorage, "flow.cfg", "echo latest\n"));
	EXPECT_EQ(ReadCfg(*pStorage, "flow.cfg"), "echo latest\n");
}

TEST(QmBindWorkshopCfgSave, FailedReplacementLeavesDestinationAndRemovesCandidate)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("blocked.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(SaveCfgFile(*pStorage, "blocked.cfg", "echo example\n"));
	EXPECT_TRUE(pStorage->FolderExists("blocked.cfg", IStorage::TYPE_SAVE));
	int NumFiles = 0;
	pStorage->ListDirectory(IStorage::TYPE_SAVE, "", [](const char *, int IsDir, int, void *pUser) {
		if(!IsDir)
			++*static_cast<int *>(pUser);
		return 0; }, &NumFiles);
	EXPECT_EQ(NumFiles, 0);
}

TEST(QmBindWorkshopCfgSave, PresetIsInstalledOnlyWhenBothCfgsExist)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder(CFG_FOLDER, IStorage::TYPE_SAVE));
	const auto &vPresets = BuiltinPresets();
	const auto It = std::find_if(vPresets.begin(), vPresets.end(), [](const SPreset &Preset) { return Preset.m_Id == "keys_lock"; });
	ASSERT_NE(It, vPresets.end());
	const SPreset &Preset = *It;
	ASSERT_TRUE(SaveCfgFile(*pStorage, CfgFileName(Preset, 0).c_str(), BuildCfgContent(Preset, 0)));
	EXPECT_FALSE(HasCompleteBuiltinCfgSet(*pStorage, Preset));
	ASSERT_TRUE(SaveCfgFile(*pStorage, CfgFileName(Preset, 1).c_str(), BuildCfgContent(Preset, 1)));
	EXPECT_TRUE(HasCompleteBuiltinCfgSet(*pStorage, Preset));
}
