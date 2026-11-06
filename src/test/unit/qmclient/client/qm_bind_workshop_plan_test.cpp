// Bind 工坊的应用计划：多键计划、冲突标记、批量撤销、参数解析与内置 cfg 的落盘约束。
#include <base/system.h>

#include <engine/keys.h>

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_workshop.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

using namespace qm_bind_workshop;

// 按 id 找一条内置预设：库是按分类排的，测试关心的是特定卡片而不是下标。
static const SPreset *FindPreset(const std::vector<SPreset> &vPresets, std::string_view Id)
{
	for(const SPreset &Preset : vPresets)
	{
		if(Preset.m_Id == Id)
			return &Preset;
	}
	return nullptr;
}
TEST(QmBindWorkshop, PlanCapturesPreviousValueForUndo)
{
	const SPreset &Preset = BuiltinPresets().front();
	const CBindSlot Slot(KEY_H, KeyModifier::NONE);
	const FBoundCommand HammerBound = [](const CBindSlot &) { return std::string("toggle cl_dummy 0 1"); };
	const SApplyPlan Plan = MakePresetPlan(Preset, 0, {Slot}, HammerBound);
	ASSERT_TRUE(Plan.Valid());
	ASSERT_EQ(Plan.m_vEntries.size(), 1u);
	EXPECT_EQ(Plan.m_vEntries.front().m_Slot.m_Key, KEY_H);
	EXPECT_TRUE(Plan.m_vEntries.front().m_Overwrite);
	EXPECT_EQ(Plan.m_vEntries.front().m_Previous, "toggle cl_dummy 0 1");
	EXPECT_FALSE(MakePresetPlan(Preset, 99, {Slot}, HammerBound).Valid());
}

// ---- 双按键：一组动作预设一次写多条 bind ----------------
TEST(QmBindWorkshop, ActionSetPresetDefaultsMapEveryActionToItsOwnKey)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const SPreset *pPreset = FindPreset(vPresets, "mouse_swift_weapons");
	ASSERT_NE(pPreset, nullptr);
	ASSERT_TRUE(IsActionSet(*pPreset)) << "one side button per weapon direction is one action per key";
	ASSERT_EQ(pPreset->m_vVariants.size(), 2u);
	EXPECT_EQ(KeySlotCount(*pPreset), pPreset->m_vVariants.size());
	EXPECT_NE(RecommendedSlotFor(*pPreset, 0), RecommendedSlotFor(*pPreset, 1))
		<< "two actions on one default key would leave one of them unusable";

	// 所有标成一组动作的内置预设都要满足同一条：键数等于变体数、默认键互不相同且可用。
	size_t NumGrouped = 0;
	for(const SPreset &Preset : vPresets)
	{
		if(!IsActionSet(Preset))
			continue;
		++NumGrouped;
		EXPECT_EQ(KeySlotCount(Preset), Preset.m_vVariants.size()) << Preset.m_Id;
		for(size_t Left = 0; Left < Preset.m_vVariants.size(); ++Left)
		{
			const CBindSlot Slot = RecommendedSlotFor(Preset, Left);
			EXPECT_NE(Slot.m_Key, KEY_UNKNOWN) << Preset.m_Id;
			for(size_t Right = Left + 1; Right < Preset.m_vVariants.size(); ++Right)
				EXPECT_NE(Slot, RecommendedSlotFor(Preset, Right)) << Preset.m_Id << " shares one default key";
		}
	}
	EXPECT_EQ(NumGrouped, 8u) << "the eight grouped presets must stay grouped";
}

TEST(QmBindWorkshop, ActionSetPlanWritesOneBindPerActionWithItsOwnKey)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "mouse_swift_weapons");
	ASSERT_NE(pPreset, nullptr);
	const FBoundCommand Unbound = [](const CBindSlot &) { return std::string(); };

	// 不给键槽：两个动作各落到自己的推荐键上，命令各写各的。
	const SApplyPlan Defaults = MakePresetPlan(*pPreset, 0, {}, Unbound);
	ASSERT_TRUE(Defaults.Valid());
	ASSERT_EQ(Defaults.m_vEntries.size(), 2u) << "a group of actions needs one bind per action";
	EXPECT_EQ(Defaults.m_vEntries[0].m_Command, "+prevweapon");
	EXPECT_EQ(Defaults.m_vEntries[1].m_Command, "+nextweapon");
	EXPECT_EQ(Defaults.m_vEntries[0].m_Slot.m_Key, KEY_MOUSE_4);
	EXPECT_EQ(Defaults.m_vEntries[1].m_Slot.m_Key, KEY_MOUSE_5);
	EXPECT_EQ(Defaults.m_vEntries[0].m_Variant, 0u);
	EXPECT_EQ(Defaults.m_vEntries[1].m_Variant, 1u);
	EXPECT_FALSE(Defaults.HasConflicts());

	// 多键 Diff 每行用自己的键名，不会两行都写同一个键。
	const FGetKeyName GetKeyName = [](int Key, int Modifier, char *pBuffer, size_t BufferSize) {
		(void)Modifier;
		str_copy(pBuffer, Key == KEY_MOUSE_4 ? "mouse4" : "mouse5", static_cast<int>(BufferSize));
	};
	const std::string Diff = DescribePlanDiff(Defaults, GetKeyName);
	EXPECT_NE(Diff.find("mouse4"), std::string::npos) << Diff;
	EXPECT_NE(Diff.find("mouse5"), std::string::npos) << Diff;

	// 用户改过的键按变体下标生效：命令不串行，修饰键也跟着各自的槽位走。
	const std::vector<CBindSlot> vSlots = {CBindSlot(KEY_Z, KeyModifier::NONE), CBindSlot(KEY_C, 1 << KeyModifier::SHIFT)};
	const SApplyPlan Custom = MakePresetPlan(*pPreset, 0, vSlots, Unbound);
	ASSERT_EQ(Custom.m_vEntries.size(), 2u);
	EXPECT_EQ(Custom.m_vEntries[0].m_Slot.m_Key, KEY_Z);
	EXPECT_EQ(Custom.m_vEntries[1].m_Slot.m_Key, KEY_C);
	EXPECT_EQ(Custom.m_vEntries[1].m_Slot.m_ModifierMask, 1 << KeyModifier::SHIFT);
	EXPECT_EQ(Custom.m_vEntries[0].m_Command, "+prevweapon");
	EXPECT_EQ(Custom.m_vEntries[1].m_Command, "+nextweapon");
}

TEST(QmBindWorkshop, ExclusivePresetPlanKeepsWritingOnlyTheChosenVariant)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "dummy_switch");
	ASSERT_NE(pPreset, nullptr);
	ASSERT_GE(pPreset->m_vVariants.size(), 2u);
	EXPECT_FALSE(IsActionSet(*pPreset)) << "plain vs switch-hammer are two ways of doing one thing";
	EXPECT_EQ(KeySlotCount(*pPreset), 1u);
	const FBoundCommand Unbound = [](const CBindSlot &) { return std::string(); };

	// 给两个键槽也只写选中的那一条：互斥方案不能同时绑上两个动作。
	const std::vector<CBindSlot> vSlots = {CBindSlot(KEY_X, KeyModifier::NONE), CBindSlot(KEY_V, KeyModifier::NONE)};
	const SApplyPlan Plan = MakePresetPlan(*pPreset, 1, vSlots, Unbound);
	ASSERT_TRUE(Plan.Valid());
	ASSERT_EQ(Plan.m_vEntries.size(), 1u) << "an exclusive preset keeps writing a single bind";
	EXPECT_EQ(Plan.m_vEntries.front().m_Variant, 1u);
	EXPECT_EQ(Plan.m_vEntries.front().m_Slot.m_Key, KEY_X) << "the second key slot is not part of an exclusive scheme";
	EXPECT_NE(Plan.m_vEntries.front().m_Command.find("+weapon1"), std::string::npos);

	// 单条入口给出同一条：两个入口对互斥方案不产生分歧。
	const SApplyPlan Single = MakePresetPlan(*pPreset, 1, {CBindSlot(KEY_X, KeyModifier::NONE)}, Unbound);
	ASSERT_EQ(Single.m_vEntries.size(), 1u);
	EXPECT_EQ(Single.m_vEntries.front().m_Command, Plan.m_vEntries.front().m_Command);
}

// 用户口径：三格水跳的四个变体是四件不同的事（右跳 / 左跳 / 提示 / 喊话），
// 每件都得有自己的键；提示那条依赖 cfg，也不能因此并进别的键里。
TEST(QmBindWorkshop, MultiActionPresetBindsTheCfgHintOnItsOwnKey)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "three_tiles_jump");
	ASSERT_NE(pPreset, nullptr);
	ASSERT_EQ(pPreset->m_vVariants.size(), 4u);
	EXPECT_TRUE(IsActionSet(*pPreset)) << "right/left/hint/chat are four actions, not four ways of one";
	EXPECT_EQ(KeySlotCount(*pPreset), 4u);

	const FBoundCommand Unbound = [](const CBindSlot &) { return std::string(); };
	// cfg 没装好时整份为空：提示那条不能留个指向不存在文件的键。
	EXPECT_FALSE(MakePresetPlan(*pPreset, 0, {}, Unbound, nullptr, 0, false).Valid());

	const SApplyPlan Plan = MakePresetPlan(*pPreset, 0, {}, Unbound, nullptr, 0, true);
	ASSERT_EQ(Plan.m_vEntries.size(), 4u) << "every variant gets its own bind";
	EXPECT_EQ(Plan.m_vEntries[0].m_Command, "+right; +jump");
	EXPECT_EQ(Plan.m_vEntries[1].m_Command, "+left; +jump");
	EXPECT_EQ(Plan.m_vEntries[2].m_Command.find("exec "), 0u) << Plan.m_vEntries[2].m_Command;
	EXPECT_EQ(Plan.m_vEntries[3].m_Command.find("say "), 0u) << Plan.m_vEntries[3].m_Command;
	EXPECT_EQ(Plan.m_vEntries[0].m_Slot.m_Key, KEY_RIGHT);
	EXPECT_NE(Plan.m_vEntries[0].m_Slot.m_ModifierMask, KeyModifier::NONE)
		<< "the plain arrow keys are spectate binds by default";
	EXPECT_EQ(Plan.m_vEntries[1].m_Slot.m_Key, KEY_LEFT);
	EXPECT_NE(Plan.m_vEntries[1].m_Slot.m_ModifierMask, KeyModifier::NONE);
	for(size_t Index = 1; Index < Plan.m_vEntries.size(); ++Index)
		EXPECT_NE(Plan.m_vEntries[Index].m_Slot, Plan.m_vEntries[0].m_Slot) << "two actions on one key would drop one of them";
}

TEST(QmBindWorkshop, UndoRestoresEveryBindOfAGroupedApply)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "mouse_swift_weapons");
	ASSERT_NE(pPreset, nullptr);
	// 后侧键原来绑着 +jump、前侧键本来是空的：撤销要写回前者、解绑后者。
	const FBoundCommand Bound = [](const CBindSlot &Slot) {
		return Slot.m_Key == KEY_MOUSE_4 ? std::string("+jump") : std::string();
	};
	const SApplyPlan Plan = MakePresetPlan(*pPreset, 0, {}, Bound);
	ASSERT_EQ(Plan.m_vEntries.size(), 2u);

	CApplyUndo Undo;
	EXPECT_FALSE(Undo.CanUndo());
	EXPECT_FALSE(Undo.Covers(CBindSlot(KEY_MOUSE_4, KeyModifier::NONE)));
	Undo.Record(Plan);
	ASSERT_TRUE(Undo.CanUndo());
	ASSERT_EQ(Undo.Count(), 2u) << "a grouped apply has to be undone as one batch";
	ASSERT_EQ(Undo.Entries().size(), 2u);
	EXPECT_EQ(Undo.Entries()[0].m_Slot.m_Key, KEY_MOUSE_4);
	EXPECT_EQ(Undo.Entries()[0].m_Command, "+jump");
	EXPECT_TRUE(Undo.Entries()[0].m_HadBind);
	EXPECT_EQ(Undo.Entries()[1].m_Slot.m_Key, KEY_MOUSE_5);
	EXPECT_EQ(Undo.Entries()[1].m_Command, "");
	EXPECT_FALSE(Undo.Entries()[1].m_HadBind) << "a key without a bind is unbound on undo, not left holding the new command";
	EXPECT_TRUE(Undo.Covers(CBindSlot(KEY_MOUSE_5, KeyModifier::NONE)));

	// 再应用一次：旧的一批整体让位，撤销不会把两次应用的组合一起还原。
	const SPreset *pExclusive = FindPreset(BuiltinPresets(), "precision_jump");
	ASSERT_NE(pExclusive, nullptr);
	const FBoundCommand HookOnC = [](const CBindSlot &Slot) {
		return Slot.m_Key == KEY_C ? std::string("+hook") : std::string();
	};
	Undo.Record(MakePresetPlan(*pExclusive, 0, {CBindSlot(KEY_C, KeyModifier::NONE)}, HookOnC));
	EXPECT_EQ(Undo.Count(), 1u);
	EXPECT_EQ(Undo.Entries().front().m_Command, "+hook");
	EXPECT_FALSE(Undo.Covers(CBindSlot(KEY_MOUSE_4, KeyModifier::NONE)));
	Undo.Clear();
	EXPECT_FALSE(Undo.CanUndo());
}

TEST(QmBindWorkshop, UndoRestoreWritesOldValuesBackAndUnbindsTheEmptyOnes)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "mouse_swift_weapons");
	ASSERT_NE(pPreset, nullptr);
	// 后侧键原来绑着 +jump、前侧键本来是空的。
	const FBoundCommand Bound = [](const CBindSlot &Slot) {
		return Slot.m_Key == KEY_MOUSE_4 ? std::string("+jump") : std::string();
	};
	CApplyUndo Undo;
	Undo.Record(MakePresetPlan(*pPreset, 0, {}, Bound));

	struct SRestoreCall
	{
		int m_Key = KEY_UNKNOWN;
		int m_ModifierMask = KeyModifier::NONE;
		std::string m_Command;
	};
	std::vector<SRestoreCall> vCalls;
	const FBinder Binder = [&vCalls](const CBindSlot &Slot, const char *pCommand) {
		SRestoreCall Call;
		Call.m_Key = Slot.m_Key;
		Call.m_ModifierMask = Slot.m_ModifierMask;
		Call.m_Command = pCommand != nullptr ? pCommand : "<null>";
		vCalls.push_back(std::move(Call));
	};
	EXPECT_EQ(RestoreUndo(Undo, Binder), 2u);
	ASSERT_EQ(vCalls.size(), 2u) << "a grouped apply is undone as one batch";
	EXPECT_EQ(vCalls[0].m_Key, KEY_MOUSE_4);
	EXPECT_EQ(vCalls[0].m_Command, "+jump") << "a key that had a bind gets its old command back";
	EXPECT_EQ(vCalls[1].m_Key, KEY_MOUSE_5);
	EXPECT_EQ(vCalls[1].m_Command, "") << "a key that had no bind is unbound instead of keeping the new command";
	// 没有记录、没有绑定器时不调回调、不报写入。
	EXPECT_EQ(RestoreUndo(CApplyUndo{}, Binder), 0u);
	EXPECT_EQ(vCalls.size(), 2u);
	const CApplyUndo Empty;
	EXPECT_EQ(RestoreUndo(Empty, FBinder{}), 0u);
}

TEST(QmBindWorkshop, EachConflictingBindIsMarkedAndThePlanStaysUsable)
{
	const SPreset *pPreset = FindPreset(BuiltinPresets(), "mouse_swift_weapons");
	ASSERT_NE(pPreset, nullptr);
	// 一条键上已有别的命令、另一条键是空的：只有前者算覆盖冲突。
	const FBoundCommand Occupied = [](const CBindSlot &Slot) {
		return Slot.m_Key == KEY_MOUSE_5 ? std::string("+hook") : std::string();
	};
	const SApplyPlan Plan = MakePresetPlan(*pPreset, 0, {}, Occupied);
	ASSERT_TRUE(Plan.Valid()) << "a conflict on one bind must not drop the other bind";
	ASSERT_EQ(Plan.m_vEntries.size(), 2u);
	EXPECT_TRUE(Plan.HasConflicts());
	EXPECT_EQ(Plan.ConflictCount(), 1u);
	EXPECT_EQ(Plan.m_vEntries[0].m_Conflicts, APPLY_CONFLICT_NONE);
	EXPECT_NE(Plan.m_vEntries[1].m_Conflicts & APPLY_CONFLICT_OVERWRITE, 0);
	EXPECT_EQ(Plan.m_vEntries[1].m_Previous, "+hook");

	// 两个动作被塞到同一个键上：只有先出现的那条进写入序列，后写的那条整条丢掉，
	// 这样按计划顺序逐条 Bind 就是第一个动作生效，不会出现「标了白写却真的写进去」。
	const std::vector<CBindSlot> vSameKey = {CBindSlot(KEY_MOUSE_4, KeyModifier::NONE), CBindSlot(KEY_MOUSE_4, KeyModifier::NONE)};
	const SApplyPlan Duplicated = MakePresetPlan(*pPreset, 0, vSameKey, [](const CBindSlot &) { return std::string(); });
	ASSERT_TRUE(Duplicated.Valid());
	ASSERT_EQ(Duplicated.m_vEntries.size(), 1u) << "only the first action enters the write sequence";
	EXPECT_EQ(Duplicated.m_vEntries[0].m_Command, "+prevweapon");
	EXPECT_EQ(Duplicated.m_vEntries[0].m_Conflicts, APPLY_CONFLICT_NONE);
	ASSERT_EQ(Duplicated.m_vSkipped.size(), 1u) << "the dropped action still has to be reported";
	EXPECT_EQ(Duplicated.m_vSkipped[0].m_Variant, 1u);
	EXPECT_EQ(Duplicated.m_vSkipped[0].m_Command, "+nextweapon");
	EXPECT_EQ(Duplicated.m_vSkipped[0].m_Slot, CBindSlot(KEY_MOUSE_4, KeyModifier::NONE));
	EXPECT_TRUE(Duplicated.HasConflicts());
	EXPECT_EQ(Duplicated.ConflictCount(), 1u);
}

TEST(QmBindWorkshop, OccupancyMarksShadowedAndMultiActionSlots)
{
	const auto GetCommand = [](int Key, int Modifier) -> const char * {
		if(Key == KEY_H && Modifier == KeyModifier::NONE)
			return "toggle cl_dummy_hammer 0 1; echo HDF";
		if(Key == KEY_H && Modifier == (1 << KeyModifier::SHIFT))
			return "toggle cl_dummy 0 1";
		return "";
	};
	const auto GetKeyName = [](int Key, int Modifier, char *pBuffer, size_t BufferSize) {
		(void)Key;
		(void)Modifier;
		if(BufferSize > 0)
			str_copy(pBuffer, "h", BufferSize);
	};
	const std::vector<SOccupancyEntry> vEntries = BuildOccupancy(GetCommand, GetKeyName);
	ASSERT_EQ(vEntries.size(), 2u);
	int NumShadowed = 0;
	int NumMultiAction = 0;
	for(const SOccupancyEntry &Entry : vEntries)
	{
		EXPECT_EQ(Entry.m_Slot.m_Key, KEY_H);
		if((Entry.m_Flags & OCCUPANCY_SHADOWED) != 0)
			++NumShadowed;
		if((Entry.m_Flags & OCCUPANCY_MULTI_ACTION) != 0)
			++NumMultiAction;
	}
	EXPECT_EQ(NumShadowed, 1);
	EXPECT_EQ(NumMultiAction, 1);
}

// ---- 参数模型：模板替换、越界夹紧 ----------------
TEST(QmBindWorkshop, ParamTemplatesResolveToConcreteCommands)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const SPreset *pSens = FindPreset(vPresets, "low_sens_aim");
	ASSERT_NE(pSens, nullptr);
	ASSERT_EQ(pSens->m_vParams.size(), 2u);
	// 不给数值时按默认值替换：默认值就是研究文档里的站点值。
	const std::string Defaults = ResolveVariantCommand(*pSens, 0);
	EXPECT_EQ(Defaults, "+showhookcoll; +toggle inp_mousesens 1 66") << Defaults;
	EXPECT_EQ(Defaults.find('{'), std::string::npos) << "no named placeholder may survive";
	const int aValues[] = {123, 500};
	const std::string Custom = ResolveVariantCommand(*pSens, 1, aValues, 2);
	EXPECT_NE(Custom.find("inp_mousesens 1 123"), std::string::npos) << Custom;
	EXPECT_NE(Custom.find("cl_mouse_max_distance 5000 500"), std::string::npos) << Custom;
	EXPECT_EQ(Custom.find('{'), std::string::npos) << Custom;
	// 越界输入先夹紧再替换：模板里永远不会出现用户手输的越界值。
	const int aHuge[] = {999999, 0};
	const std::string Clamped = ResolveVariantCommand(*pSens, 1, aHuge, 2);
	EXPECT_NE(Clamped.find("inp_mousesens 1 20000"), std::string::npos) << Clamped;
	EXPECT_NE(Clamped.find("cl_mouse_max_distance 5000 1"), std::string::npos) << Clamped;

	// 裸 %d 写法同样要能被按顺序吃掉（站点命令原文里常见写法）。
	SPreset Bare;
	SVariant BareVariant;
	BareVariant.m_Label = "Hold";
	BareVariant.m_Command = "+toggle cl_mouse_max_distance 2 %d";
	Bare.m_vVariants.push_back(BareVariant);
	SPresetParam Dist;
	Dist.m_pToken = "dist";
	Dist.m_Default = 350;
	Dist.m_Min = 1;
	Dist.m_Max = 1000;
	Dist.m_Step = 10;
	Bare.m_vParams.push_back(Dist);
	EXPECT_EQ(ResolveVariantCommand(Bare, 0), "+toggle cl_mouse_max_distance 2 350");
	const int aBare[] = {420};
	EXPECT_EQ(ResolveVariantCommand(Bare, 0, aBare, 1), "+toggle cl_mouse_max_distance 2 420");
}

TEST(QmBindWorkshop, ParamValuesClampAndStepInsideTheirRange)
{
	SPresetParam Param;
	Param.m_Default = 66;
	Param.m_Min = 1;
	Param.m_Max = 20000;
	Param.m_Step = 100;
	EXPECT_EQ(ClampParamValue(Param, 0), 1);
	EXPECT_EQ(ClampParamValue(Param, -5), 1);
	EXPECT_EQ(ClampParamValue(Param, 999999), 20000);
	EXPECT_EQ(ClampParamValue(Param, 66), 66);
	EXPECT_EQ(StepParamValue(Param, 66, 1), 166);
	EXPECT_EQ(StepParamValue(Param, 66, -1), 1) << "stepping below the minimum clamps instead of wrapping";
	EXPECT_EQ(StepParamValue(Param, 19950, 1), 20000) << "stepping above the maximum clamps";
	EXPECT_EQ(StepParamValue(Param, 100, 1, 100000000), 20000) << "a huge step must not overflow int";
	EXPECT_EQ(StepParamValue(Param, 100, 0), 100);

	// 内置预设的每个参数范围都要自洽，默认值必须落在范围内。
	size_t NumSens = 0;
	size_t NumDist = 0;
	for(const SPreset &Preset : BuiltinPresets())
	{
		for(const SPresetParam &Slot : Preset.m_vParams)
		{
			EXPECT_LT(Slot.m_Min, Slot.m_Max) << Preset.m_Id;
			EXPECT_GE(Slot.m_Default, Slot.m_Min) << Preset.m_Id;
			EXPECT_LE(Slot.m_Default, Slot.m_Max) << Preset.m_Id;
			EXPECT_GT(Slot.m_Step, 0) << Preset.m_Id;
			EXPECT_FALSE(std::string(Slot.m_pToken).empty()) << Preset.m_Id;
			const std::string Token(Slot.m_pToken);
			if(Token == "sens")
			{
				++NumSens;
				EXPECT_GE(Slot.m_Max, 20000) << "sensitivity range must reach the researched ceiling";
			}
			if(Token == "dist")
			{
				++NumDist;
				EXPECT_LE(Slot.m_Max, 1000);
			}
		}
	}
	// research §5.2 里带灵敏度的条目必须被参数模型覆盖到。
	EXPECT_GT(NumSens, 0u);
	EXPECT_GT(NumDist, 0u);
}

// ---- 内置 cfg：落盘路径、exec 命令与正文约束 ----------------
TEST(QmBindWorkshop, CfgVariantsShipContentWithinConsoleLimits)
{
	size_t NumCfgVariants = 0;
	for(const SPreset &Preset : BuiltinPresets())
	{
		for(size_t Variant = 0; Variant < Preset.m_vVariants.size(); ++Variant)
		{
			if(!VariantRequiresCfg(Preset.m_vVariants[Variant]))
				continue;
			++NumCfgVariants;
			// 每条 exec 变体都必须自带正文：缺一份，安装流程就会写出死键。
			ASSERT_TRUE(HasBuiltinCfg(Preset, Variant)) << Preset.m_Id;
			const std::string Content = BuildCfgContent(Preset, Variant);
			ASSERT_FALSE(Content.empty()) << Preset.m_Id;
			// 单行 255 字节、bind 嵌套 <= 5 层：控制台的硬限制。
			EXPECT_EQ(FindCfgViolation(Content), 0u) << Preset.m_Id << " cfg breaks a console limit";
			// cfg 里互相 exec 的文件名必须也由本预设提供，不能指向不存在的文件。
			size_t Pos = 0;
			while((Pos = Content.find("exec ", Pos)) != std::string::npos)
			{
				const size_t End = Content.find('\n', Pos);
				const std::string Target = Content.substr(Pos + 5, End == std::string::npos ? std::string::npos : End - Pos - 5);
				bool Known = false;
				for(size_t Other = 0; Other < Preset.m_vVariants.size(); ++Other)
					Known = Known || (HasBuiltinCfg(Preset, Other) && CfgFileName(Preset, Other) == Target);
				EXPECT_TRUE(Known) << Preset.m_Id << " execs an unshipped file: " << Target;
				Pos = End == std::string::npos ? Content.size() : End;
			}
		}
	}
	EXPECT_GE(NumCfgVariants, 12u) << "the researched cfg presets must keep their content";

	// 校验器本身要有牙：超长行与 6 层嵌套都必须被点出行号。
	EXPECT_EQ(FindCfgViolation("echo ok\nbind h \"echo hi\"\n"), 0u);
	EXPECT_EQ(FindCfgViolation(std::string(CFG_MAX_LINE_BYTES, 'a') + "\n"), 0u) << "255 bytes is still legal";
	EXPECT_EQ(FindCfgViolation("echo ok\n" + std::string(CFG_MAX_LINE_BYTES + 1, 'a') + "\n"), 2u);
	std::string Deep;
	for(int Layer = 0; Layer <= MAX_NESTING_DEPTH + 1; ++Layer)
		Deep += "bind x ";
	EXPECT_EQ(FindCfgViolation("echo ok\n" + Deep + "\n"), 2u);
}

TEST(QmBindWorkshop, InstalledCfgPlanWritesExecOfTheStableRelativePath)
{
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const SPreset *pLock = FindPreset(vPresets, "keys_lock");
	ASSERT_NE(pLock, nullptr);
	ASSERT_GE(pLock->m_vVariants.size(), 2u);
	const CBindSlot Slot(KEY_H, KeyModifier::NONE);
	const FBoundCommand UnboundHere = [](const CBindSlot &) { return std::string(); };

	// 未安装：没有计划，卡片只能显示 Install and apply。
	EXPECT_FALSE(MakePresetPlan(*pLock, 0, {Slot}, UnboundHere, nullptr, 0, false).Valid());

	// 安装后：写的是 exec 我们自己的相对路径，不是站点原文里的中文文件名。
	const SApplyPlan Plan = MakePresetPlan(*pLock, 0, {Slot}, UnboundHere, nullptr, 0, true);
	ASSERT_TRUE(Plan.Valid());
	const std::string &Command = Plan.m_vEntries.front().m_Command;
	EXPECT_EQ(Command, CfgExecCommand(*pLock, 0));
	EXPECT_EQ(Command, "exec " + CfgFileName(*pLock, 0));
	// exec 的解析规则是相对 SAVE 目录（console.cpp ExecuteFileFromCommand -> TYPE_ALL），
	// 所以路径必须是 qm_bind/ 开头、没有盘符也不是绝对路径。
	const std::string FileName = CfgFileName(*pLock, 0);
	EXPECT_EQ(FileName.find(std::string(CFG_FOLDER) + "/"), 0u) << FileName;
	EXPECT_EQ(FileName.find(' '), std::string::npos) << "exec takes the path as one argument: " << FileName;
	EXPECT_EQ(FileName.find('\n'), std::string::npos) << FileName;
	EXPECT_EQ(FileName.find('\\'), std::string::npos) << FileName;
	// 同一预设的不同变体各写一份文件，不能互相覆盖。
	EXPECT_NE(CfgFileName(*pLock, 0), CfgFileName(*pLock, 1));
	EXPECT_EQ(CfgFileName(*pLock, 0).find(".cfg"), CfgFileName(*pLock, 0).size() - 4);

	// 互斥方案里不依赖 cfg 的变体照旧直接写 bind，不受安装门控影响。
	const SPreset *pJump = FindPreset(vPresets, "precision_jump");
	ASSERT_NE(pJump, nullptr);
	ASSERT_FALSE(VariantRequiresCfg(pJump->m_vVariants.front()));
	const SApplyPlan Direct = MakePresetPlan(*pJump, 0, {Slot}, UnboundHere, nullptr, 0, false);
	ASSERT_TRUE(Direct.Valid());
	EXPECT_EQ(Direct.m_vEntries.front().m_Command, pJump->m_vVariants.front().m_Command);
}
