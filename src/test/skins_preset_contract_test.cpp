// Skins 静态源码合同：skins_preset_contract_test.cpp.
// 源码合同测试：皮肤资源、菜单集成和队列策略。运行时行为保留在 skins_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

extern CDataContainer *g_pData;

static std::string FunctionBody(const std::string &Source, const std::string &Signature)
{
	const size_t FunctionStart = Source.find(Signature);
	EXPECT_NE(FunctionStart, std::string::npos) << Signature;
	const size_t BodyStart = Source.find("{", FunctionStart);
	EXPECT_NE(BodyStart, std::string::npos) << Signature;
	int Depth = 0;
	for(size_t Index = BodyStart; Index < Source.size(); ++Index)
	{
		if(Source[Index] == '{')
			++Depth;
		else if(Source[Index] == '}')
		{
			--Depth;
			if(Depth == 0)
				return Source.substr(BodyStart, Index - BodyStart);
		}
	}
	ADD_FAILURE() << Signature;
	return {};
}

TEST(SkinsContract, MapPlayerSkinQueueSyncReplacesCurrentQueue)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t SyncPos = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)");
	ASSERT_NE(SyncPos, std::string::npos);
	const size_t SyncEnd = Source.find("bool CSkins::IsInSkinQueue", SyncPos);
	ASSERT_NE(SyncEnd, std::string::npos);
	const std::string SyncBody = Source.substr(SyncPos, SyncEnd - SyncPos);
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);
	const size_t ApplyPresetPos = Source.find("bool CSkins::ApplySkinQueuePreset(size_t PresetIndex, int Dummy)");
	ASSERT_NE(ApplyPresetPos, std::string::npos);
	const size_t ApplyPresetEnd = Source.find("bool CSkins::RemoveSkinQueuePreset", ApplyPresetPos);
	ASSERT_NE(ApplyPresetEnd, std::string::npos);
	const std::string ApplyPresetBody = Source.substr(ApplyPresetPos, ApplyPresetEnd - ApplyPresetPos);

	// Sync replaces the queue in place from map players (no grow-only merge).
	EXPECT_EQ(SyncBody.find("std::vector<CSkinQueueEntry> vMapSkins"), std::string::npos);
	EXPECT_EQ(SyncBody.find("SyncSkinQueueEntriesInPlace("), std::string::npos);
	EXPECT_NE(SyncBody.find("Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);"), std::string::npos);
	EXPECT_NE(SyncBody.find("std::array<CSkinQueueEntry, MAX_CLIENTS> aMapSkins"), std::string::npos);
	EXPECT_NE(Source.find("#include <array>"), std::string::npos);
	// Sync only runs in server-rotation mode, so the queue is attributed to the
	// Server preset and mirrored into the Server preset's template (never the
	// Default preset at index 0).
	EXPECT_NE(SyncBody.find("m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue = Queue;"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)SKIN_QUEUE_SERVER_PRESET;"), std::string::npos);
	EXPECT_EQ(SyncBody.find("m_vSkinQueuePresets[0].m_Queue"), std::string::npos);
	// The content-matching fallback was removed; Applied is set explicitly.
	EXPECT_EQ(SyncBody.find("SkinQueueCurrentPresetIndex"), std::string::npos);
	EXPECT_EQ(Source.find("FindMatchingSkinQueuePresetIndex"), std::string::npos);
	// ApplySkinQueuePreset copies the preset into the playing queue, marks it
	// clean, and resets the rotation timer (click-to-apply model).
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueue[Dummy] = Presets[PresetIndex].m_Queue;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("SkinQueueIndexVar(Dummy) = 0;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)PresetIndex;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueueDirty[Dummy] = false;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("m_aActiveSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("SyncSkinQueueEntriesInPlace("), std::string::npos);
	EXPECT_EQ(UpdateBody.find("TrimSkinQueueToLimit(Dummy);"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("TrimSkinQueueToLimit(Dummy);"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("SkinQueueLengthVar(Dummy)"), std::string::npos);
}

TEST(SkinsContract, SkinQueuePresetsAreSelectableEditableQueues)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string Header = HeaderBuffer.str();

	std::ifstream SourceFile(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();
	const size_t ApplyPresetPos = Source.find("bool CSkins::ApplySkinQueuePreset(size_t PresetIndex, int Dummy)");
	ASSERT_NE(ApplyPresetPos, std::string::npos);
	const size_t ApplyPresetEnd = Source.find("bool CSkins::RemoveSkinQueuePreset", ApplyPresetPos);
	ASSERT_NE(ApplyPresetEnd, std::string::npos);
	const std::string ApplyPresetBody = Source.substr(ApplyPresetPos, ApplyPresetEnd - ApplyPresetPos);
	const size_t ClearPos = Source.find("void CSkins::ClearSkinQueue(int Dummy)");
	ASSERT_NE(ClearPos, std::string::npos);
	const size_t ClearEnd = Source.find("bool CSkins::SaveSkinQueueToAppliedPreset", ClearPos);
	ASSERT_NE(ClearEnd, std::string::npos);
	const std::string ClearBody = Source.substr(ClearPos, ClearEnd - ClearPos);

	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string Menus = MenusBuffer.str();

	// Preset model: Default(0, USER) + Server(1, SERVER) built-ins, then user presets.
	EXPECT_NE(Header.find("static constexpr size_t SKIN_QUEUE_DEFAULT_PRESET = 0;"), std::string::npos);
	EXPECT_NE(Header.find("static constexpr size_t SKIN_QUEUE_SERVER_PRESET = 1;"), std::string::npos);
	EXPECT_NE(Header.find("bool IsBuiltInSkinQueuePreset(size_t PresetIndex) const"), std::string::npos);
	EXPECT_NE(Header.find("int AppliedSkinQueuePresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_NE(Header.find("bool SkinQueueDirty(int Dummy) const"), std::string::npos);
	EXPECT_NE(Header.find("bool SaveSkinQueueToAppliedPreset(int Dummy)"), std::string::npos);
	EXPECT_NE(Header.find("bool AddActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Header.find("bool RemoveActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Header.find("void MoveActiveSkinQueueItem("), std::string::npos);
	EXPECT_NE(Header.find("bool ApplySkinQueueIndex(size_t QueueIndex, int Dummy)"), std::string::npos);
	EXPECT_NE(Header.find("void TrimActiveSkinQueueToLimit("), std::string::npos);
	EXPECT_NE(Header.find("std::array<int, NUM_DUMMIES> m_aAppliedSkinQueuePresetIndex"), std::string::npos);
	EXPECT_NE(Header.find("std::array<bool, NUM_DUMMIES> m_aSkinQueueDirty"), std::string::npos);
	EXPECT_NE(Header.find("std::vector<CSkinQueuePreset> m_vSkinQueuePresets"), std::string::npos);
	EXPECT_EQ(Header.find("std::array<std::vector<CSkinQueuePreset>, NUM_DUMMIES> m_aSkinQueuePresets"), std::string::npos);
	// Removed: the old "active/edit-state" preset selection model.
	EXPECT_EQ(Header.find("int ActiveSkinQueuePresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("bool SelectSkinQueuePreset(size_t PresetIndex, int Dummy)"), std::string::npos);
	EXPECT_EQ(Header.find("void ClearSkinQueuePresetSelection(int Dummy)"), std::string::npos);
	EXPECT_EQ(Header.find("const std::vector<CSkinQueueEntry> &ActiveSkinQueue(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("int SkinQueueCurrentPresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("std::array<int, NUM_DUMMIES> m_aActiveSkinQueuePresetIndex"), std::string::npos);

	EXPECT_NE(Source.find("std::fill(m_aAppliedSkinQueuePresetIndex.begin(), m_aAppliedSkinQueuePresetIndex.end(), -1);"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Default preset\", {}, CSkinQueuePreset::EKind::USER});"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Server preset\", {}, CSkinQueuePreset::EKind::SERVER});"), std::string::npos);
	EXPECT_EQ(Source.find("std::fill(m_aActiveSkinQueuePresetIndex.begin(), m_aActiveSkinQueuePresetIndex.end(), -1);"), std::string::npos);
	EXPECT_EQ(Source.find("ActiveSkinQueueMutable"), std::string::npos);
	EXPECT_EQ(Source.find("FindMatchingSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(Source.find("m_aSkinQueuePresets[Dummy]"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Preset %d\")"), std::string::npos);
	EXPECT_EQ(Source.find("\"Preset %d\""), Source.find("Localize(\"Preset %d\")") + strlen("Localize("));

	// Apply = click-to-apply; presets are read-only templates until Save/Save-As.
	EXPECT_NE(ApplyPresetBody.find("if(PresetIndex == SKIN_QUEUE_SERVER_PRESET)"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueue[Dummy] = Presets[PresetIndex].m_Queue;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)PresetIndex;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueueDirty[Dummy] = false;"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("m_aActiveSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("if(PresetIndex < 2)"), std::string::npos);

	// Save writes the playing queue back to the applied preset; Server is not writable.
	EXPECT_NE(Source.find("m_vSkinQueuePresets[PresetIndex].m_Queue = m_aSkinQueue[Dummy];"), std::string::npos);
	EXPECT_NE(Source.find("if(!IsSkinQueuePresetWritable(PresetIndex, m_vSkinQueuePresets.size()))"), std::string::npos);

	// Clear empties the playing queue, keeps Applied (clear-then-save writes back), marks dirty.
	EXPECT_NE(ClearBody.find("m_aSkinQueue[Dummy].clear();"), std::string::npos);
	EXPECT_NE(ClearBody.find("SkinQueueRotateMapVar(Dummy) = 0;"), std::string::npos);
	EXPECT_NE(ClearBody.find("m_aSkinQueueDirty[Dummy] = true;"), std::string::npos);
	EXPECT_EQ(ClearBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = -1;"), std::string::npos);

	// UI: preset bar uses Save / Save-as / Rename / Delete; clicking a preset applies it.
	EXPECT_NE(Menus.find("Localize(\"Save\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Save as\")"), std::string::npos);
	EXPECT_NE(Menus.find("SaveSkinQueueToAppliedPreset(QueueDummy)"), std::string::npos);
	EXPECT_NE(Menus.find("AddSkinQueuePresetFromCurrent(QueueDummy)"), std::string::npos);
	EXPECT_NE(Menus.find("const int AppliedPresetIndex = GameClient()->m_Skins.AppliedSkinQueuePresetIndex(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("const bool QueueDirty = GameClient()->m_Skins.SkinQueueDirty(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("const auto &SkinQueue = GameClient()->m_Skins.SkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueDirty ? \"● \""), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Enable rotation\")"), std::string::npos);
	EXPECT_EQ(Menus.find("CurrentQueueRect"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueHeader.VSplitLeft(QueueHeader.w * 0.48f"), std::string::npos);
	EXPECT_NE(Menus.find("DoSettingsButton_CheckBox(SETTINGS_TEE, -1, &QueueEnabled, QueueDummy ? \"tee-dummy-skin-queue-enabled\" : \"tee-player-skin-queue-enabled\", Localize(\"Enable rotation\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Enable skin queue\"), QueueEnabled"), std::string::npos);
	EXPECT_EQ(Menus.find("CUIRect QueueEnabledRect"), std::string::npos);
	EXPECT_NE(Menus.find("ResolveSettingsTeeQueuePanelGeometry(TeeMetrics, (int)SkinQueue.size(), (int)vQueuePresets.size())"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueueListSurfaceHeight"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueuePresetHeight"), std::string::npos);
	EXPECT_NE(Menus.find("QueueListBody.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueListBody);"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueueListViewportHeight"), std::string::npos);
	EXPECT_NE(Menus.find("s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Menus.find("s_QueueListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Menus.find("s_PresetListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Menus.find("QueueList.HSplitTop(TeeMetrics.m_LineHeight, &QueueListHeader"), std::string::npos);
	EXPECT_NE(Menus.find("TeeMetrics.m_ButtonHeight), &QueueListHeader, &ClearQueueRect"), std::string::npos);
	EXPECT_NE(Menus.find("TeeMetrics.m_ButtonHeight), &QueueListHeaderLabel, &QueueRandomRect"), std::string::npos);
	EXPECT_NE(Menus.find("CurrentQueueLabelProps.m_MaxWidth = QueueListHeaderLabel.w;"), std::string::npos);
	EXPECT_NE(Menus.find("Ui()->DoLabel(&QueueListHeaderLabel, aCurrentQueueLabel"), std::string::npos);
	EXPECT_EQ(Menus.find("DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, \"tee_queue_list_label\", &QueueListHeaderLabel, Localize(\"Skin queue\")"), std::string::npos);
	EXPECT_NE(Menus.find("s_TeeClearCurrentSkinQueueButton"), std::string::npos);
	EXPECT_NE(Menus.find("IsBuiltInSkinQueuePreset(i)"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ApplySkinQueuePreset((size_t)SelectPresetIndex, QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ClearSkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.MoveActiveSkinQueueItem("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.RemoveActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.AddActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ApplySkinQueueIndex((size_t)ApplyQueueIndex, QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Queue preset: %s\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Custom\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Default preset\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Rotate all server player skins\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Clear current queue\")"), std::string::npos);
	EXPECT_NE(Menus.find("static ui_widget::SNumericFieldState s_aQueueIntervalStates[NUM_DUMMIES];"), std::string::npos);
	EXPECT_NE(Menus.find("IUiContext TeeSkinQueueIntervalCtx;"), std::string::npos);
	EXPECT_NE(Menus.find("TeeSkinQueueIntervalCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_skin_queue_interval_text_input\");"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_CommitPolicy = ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT;"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_NE(Menus.find("ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 0, 120000, IntervalInputGroup, QueueIntervalOptions);"), std::string::npos);
	EXPECT_EQ(Menus.find("Ui()->DoEditBox(&QueueIntervalInput, &IntervalInput"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Server preset\", {}, CSkinQueuePreset::EKind::SERVER});"), std::string::npos);
	// Removed UI: Apply/Save-current buttons, the select/cancel-select calls, and the
	// old edit-state wiring. Clicking a preset now applies it directly.
	EXPECT_EQ(Menus.find("const int ActivePresetIndex = GameClient()->m_Skins.ActiveSkinQueuePresetIndex(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("const auto &SkinQueue = GameClient()->m_Skins.ActiveSkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("SkinQueueCurrentPresetIndex(QueueDummy)"), std::string::npos);
	EXPECT_EQ(Menus.find("SelectSkinQueuePreset("), std::string::npos);
	EXPECT_EQ(Menus.find("ClearSkinQueuePresetSelection("), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Apply\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Apply this preset to the current queue\")"), std::string::npos);
	EXPECT_EQ(Menus.find("if(SelectPresetIndex == 0 || SelectPresetIndex == 1)"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Editing: %s\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Editing: current queue\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Queue capacity\")"), std::string::npos);
	EXPECT_EQ(Menus.find("return Localize(vQueuePresets[PresetIndex].m_Name.c_str());"), std::string::npos);
	EXPECT_EQ(Source.find("if(PresetIndex == 1)"), std::string::npos);
	EXPECT_EQ(Source.find("m_aActiveSkinQueuePresetIndex[Dummy] = -1;"), std::string::npos);
	EXPECT_EQ(Source.find("m_vSkinQueuePresets[PresetIndex].IsProtected()"), std::string::npos);
}

TEST(SkinsContract, SkinQueuePresetCompatibilityKeepsLimitAndMigratesLegacyDummyPresetCommands)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t AddQueuePos = Source.find("bool CSkins::AddSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)");
	ASSERT_NE(AddQueuePos, std::string::npos);
	const size_t AddQueueEnd = Source.find("bool CSkins::AddActiveSkinQueue", AddQueuePos);
	ASSERT_NE(AddQueueEnd, std::string::npos);
	const std::string AddQueueBody = Source.substr(AddQueuePos, AddQueueEnd - AddQueuePos);
	const size_t AddActivePos = AddQueueEnd;
	const size_t AddActiveEnd = Source.find("bool CSkins::RemoveSkinQueue", AddActivePos);
	ASSERT_NE(AddActiveEnd, std::string::npos);
	const std::string AddActiveBody = Source.substr(AddActivePos, AddActiveEnd - AddActivePos);
	const size_t AddPresetItemPos = Source.find("bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)");
	ASSERT_NE(AddPresetItemPos, std::string::npos);
	const size_t AddPresetItemEnd = Source.find("bool CSkins::AddSkinQueuePresetFromCurrent", AddPresetItemPos);
	ASSERT_NE(AddPresetItemEnd, std::string::npos);
	const std::string AddPresetItemBody = Source.substr(AddPresetItemPos, AddPresetItemEnd - AddPresetItemPos);
	const size_t AddPresetPos = Source.find("bool CSkins::AddSkinQueuePreset(const char *pName, int Dummy)");
	ASSERT_NE(AddPresetPos, std::string::npos);
	ASSERT_LT(AddPresetPos, AddPresetItemPos);
	const std::string AddPresetBody = Source.substr(AddPresetPos, AddPresetItemPos - AddPresetPos);
	const size_t DummyPresetPos = Source.find("void CSkins::ConAddDummySkinQueuePreset(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetPos, std::string::npos);
	const size_t DummyPresetEnd = Source.find("void CSkins::ConAddSkinQueuePresetItem", DummyPresetPos);
	ASSERT_NE(DummyPresetEnd, std::string::npos);
	const std::string DummyPresetBody = Source.substr(DummyPresetPos, DummyPresetEnd - DummyPresetPos);
	const size_t DummyPresetItemPos = Source.find("void CSkins::ConAddDummySkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetItemPos, std::string::npos);
	const size_t DummyPresetItemEnd = Source.find("void CSkins::ConAddSkinQueuePresetItemEx", DummyPresetItemPos);
	ASSERT_NE(DummyPresetItemEnd, std::string::npos);
	const std::string DummyPresetItemBody = Source.substr(DummyPresetItemPos, DummyPresetItemEnd - DummyPresetItemPos);
	const size_t DummyPresetItemExPos = Source.find("void CSkins::ConAddDummySkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetItemExPos, std::string::npos);
	const size_t DummyPresetItemExEnd = Source.find("void CSkins::ConfigSaveCallback", DummyPresetItemExPos);
	ASSERT_NE(DummyPresetItemExEnd, std::string::npos);
	const std::string DummyPresetItemExBody = Source.substr(DummyPresetItemExPos, DummyPresetItemExEnd - DummyPresetItemExPos);
	const size_t SavePos = Source.find("void CSkins::OnQueueConfigSave(IConfigManager *pConfigManager)");
	ASSERT_NE(SavePos, std::string::npos);
	const std::string SaveBody = Source.substr(SavePos);

	EXPECT_NE(AddQueueBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));"), std::string::npos);
	EXPECT_NE(AddQueueBody.find("if((int)Queue.size() >= Limit)"), std::string::npos);
	// AddActiveSkinQueue forwards to AddSkinQueue (the limit check lives in the latter).
	EXPECT_NE(AddActiveBody.find("return AddSkinQueue(pName, UseCustomColor, ColorBody, ColorFeet, Dummy);"), std::string::npos);
	EXPECT_EQ(AddActiveBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(Source.find("SKIN_QUEUE_PRESET_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("Presets.size() >= SKIN_QUEUE_PRESET_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("std::find_if(Presets.begin(), Presets.end()"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("str_comp(Preset.m_Name.c_str(), aPresetName) == 0"), std::string::npos);
	EXPECT_NE(AddPresetItemBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));"), std::string::npos);
	EXPECT_NE(AddPresetItemBody.find("if((int)Queue.size() >= Limit)"), std::string::npos);

	EXPECT_NE(DummyPresetBody.find("AddSkinQueuePreset("), std::string::npos);
	EXPECT_NE(DummyPresetItemBody.find("AddSkinQueuePresetItem("), std::string::npos);
	EXPECT_NE(DummyPresetItemExBody.find("AddSkinQueuePresetItem("), std::string::npos);
	EXPECT_EQ(DummyPresetBody.find("Ignoring legacy dummy skin queue preset"), std::string::npos);
	EXPECT_EQ(DummyPresetItemBody.find("Ignoring legacy dummy skin queue preset item"), std::string::npos);
	EXPECT_EQ(DummyPresetItemExBody.find("Ignoring legacy dummy skin queue preset item"), std::string::npos);
	EXPECT_EQ(SaveBody.find("add_dummy_skin_queue_preset"), std::string::npos);
	EXPECT_EQ(SaveBody.find("QueuePresetIndex < 2"), std::string::npos);
	EXPECT_NE(SaveBody.find("QueuePresetIndex != SKIN_QUEUE_DEFAULT_PRESET"), std::string::npos);
	EXPECT_NE(SaveBody.find("WriteQueueEntry(QueueSkin, false, (int)QueuePresetIndex);"), std::string::npos);
}
