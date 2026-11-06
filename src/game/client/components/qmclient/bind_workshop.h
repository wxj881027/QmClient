#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_WORKSHOP_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_WORKSHOP_H

#include <game/client/components/binds.h>
#include <game/client/ui_rect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 指令工坊 / 蓝图的纯逻辑层。这里不依赖 CUi、IGraphics 或 CMenus，
// 所有函数都能在 testrunner 里直接调用。
//
// 设计约束（来自 v3 落地图与 2026-11-04 方案）：
// - DDNet 控制台没有 sleep / if / 循环，分号命令同帧执行，所以图的边只表达
//   「同一帧内的顺序」与「状态翻转」，不表达时间先后。
// - 所有写入都先生成 SApplyPlan，UI 层展示 Diff 后才提交，并保留前值用于撤销。
// - 节点/边用整数 ID + 单头开源哈希映射做 O(1) 反查；视口查询走网格桶，
//   不随节点总数线性增长。
namespace qm_bind_workshop
{
	// 分类既做筛选维度也是卡片浏览顺序；ALL 只表示「不过滤」。
	// 这里每加一个分类，CategoryLabel 必须同步加一个显式分支：
	// 标签函数不留 default 兜底，新分类不会静默显示成 All。
	enum class ECategory
	{
		ALL = 0,
		MOVEMENT, // 位移：跳跃、三格水跳、延时跳
		DUMMY, // 分身管理：连接、重置、同步、卡键、自动换锤
		FLYING, // 飞行模式：DF/HDF/PF/3Fly/Shitfly/Hookfly/KOG
		WEAPON, // 武器：切枪、切锤、武器大师
		AIM, // 鼠标与瞄准：灵敏度、cl_mouse_max_distance 体系
		SERVER, // 服务器聊天指令（say /...），会被他人看见
		CAMERA, // 视野与画面表现
		UTILITY, // 杂项：改名、防自杀、AFK、忍者
		NUM,
	};
	enum class EMode
	{
		ONCE,
		HOLD,
		TOGGLE,
	};

	enum class ESource
	{
		BUILTIN,
		IMPORTED,
		USER,
	};

	struct SVariant
	{
		std::string m_Label;
		std::string m_Command;
		std::vector<std::string> m_vTags;
	};

	// ---- 参数模型 ----
	// 一张卡片上的可调项只有两类：触发键（键槽）与数值。数值参数由纯逻辑层
	// 夹紧、步进并替换进命令模板，UI 只负责画 -/值/+ 并把收集到的数字交回来。
	enum class EPresetParamKind
	{
		NONE = 0,
		KEY, // 触发键：键槽，决定 bind 写到哪个键
		NUMBER, // 数值：替换命令模板里的 {token} 或 %d
		NUM,
	};

	// 一张卡最多两个数值参数（灵敏度 + 鼠标距离），参数行按数量平分宽度。
	inline constexpr size_t MAX_PRESET_PARAMS = 2;

	// 一张卡最多几个键槽行：一组动作预设每个变体一行，超出的变体在卡片上截断。
	inline constexpr size_t MAX_PRESET_KEY_ROWS = 8;

	struct SPresetParam
	{
		const char *m_pToken = ""; // 模板占位符里的名字：{sens} / {dist}
		const char *m_pLabel = ""; // Localize 键，步进器标题
		int m_Default = 0;
		int m_Min = 0;
		int m_Max = 0;
		int m_Step = 1;
	};

	// 预设和键的关系：一张卡要么是「几个做法里挑一个」，要么是「一组动作各配一个键」。
	// 判断标准只看变体本身，不看变体数量与描述措辞：
	// - 每个变体都是能各自独立绑定的一条命令 / 一份 cfg（不同动作、不同 cfg、不同提示
	//   文案……玩家会希望它们同时存在），就是 ACTION_SET，一次应用全绑上；
	// - 各变体只是同一件事的不同实现（按住还是切换、同一个开关的开与关、不同键位映射、
	//   简版还是全套、两个视角），只能选一个，是 EXCLUSIVE。
	// 不确定时看反例：三格水跳的右跳 / 左跳 / 提示 / 喊话是四件不同的事，各要一个键；
	// 卡键的开 / 关互相 exec 同一个键，是同开关的两态，只能选一个。
	enum class EBindGrouping
	{
		EXCLUSIVE = 0, // 互斥方案：一次只写一条 bind
		ACTION_SET, // 一组动作：一个动作一个键，一次全部写上
	};

	struct SPreset
	{
		std::string m_Id;
		const char *m_pTitle = ""; // Localize 键，UI 层再翻译
		const char *m_pDescription = "";
		ECategory m_Category = ECategory::UTILITY;
		ESource m_Source = ESource::BUILTIN;
		CBindSlot m_RecommendedSlot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		// 一组动作的每个变体各自的推荐键；为空表示全部沿用 m_RecommendedSlot。
		std::vector<CBindSlot> m_vVariantSlots;
		EBindGrouping m_Grouping = EBindGrouping::EXCLUSIVE;
		std::vector<SVariant> m_vVariants;
		// 数值参数：按顺序对应模板里的 {token}；模板里裸写的 %d 依次吃第 1、2 个。
		std::vector<SPresetParam> m_vParams;
		// 依赖 exec cfg 包：命令本身只是「exec 某文件」，缺文件时绑定是死键。
		// 标记后卡片走「安装并应用」流程：装好内置 cfg 才允许 MakePresetPlan 出计划。
		bool m_RequiresCfg = false;
	};

	// 内置预设：命令原文照抄 DDNet 控制台语法与玩家社区公开写法，
	// 标题与说明是自写的改写文案（站点中文说明有署名，不直接搬）。
	// P0 = 无 cfg 依赖，应用即写 bind；P1 = 需要 cfg 文件，标记 m_RequiresCfg。
	const std::vector<SPreset> &BuiltinPresets();
	const char *CategoryLabel(ECategory Category);
	const char *ModeLabel(EMode Mode);
	const char *SourceLabel(ESource Source);

	// 模式由命令串推断，不让用户手选：含 +toggle / + 前缀 = 按住，
	// 含裸 toggle = 一键切换，其余 = 执行一次。
	EMode InferMode(std::string_view Command);
	bool MatchesQuery(const SPreset &Preset, std::string_view Query, const char *(*pLocalize)(const char *) = nullptr);
	// 查询串非空时跨分类匹配（分类不参与过滤）；查询串为空时按分类过滤。
	// CountCategory 永远只数分类，与查询串无关。
	std::vector<size_t> FilterPresets(const std::vector<SPreset> &vPresets, ECategory Category, std::string_view Query, const char *(*pLocalize)(const char *) = nullptr);
	size_t CountCategory(const std::vector<SPreset> &vPresets, ECategory Category);

	// ---- 应用计划：提交前先看 Diff，撤销靠前值快照 ----
	// 一条 bind 写一个键；「一组动作」预设一次要写好几条，所以计划是列表。
	enum
	{
		APPLY_CONFLICT_NONE = 0,
		// 这个键上本来就绑着别的命令，写下去会把它顶掉（m_Previous 是原内容）。
		APPLY_CONFLICT_OVERWRITE = 1 << 0,
		// 同一份计划里前面的条目已经占了这个键：后写的那条不再进写入序列，
		// 只在 SApplyPlan::m_vSkipped 里留一条记录（先写的生效）。
		APPLY_CONFLICT_DUPLICATE_SLOT = 1 << 1,
	};

	struct SApplyEntry
	{
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		std::string m_Command;
		std::string m_Previous;
		bool m_Overwrite = false;
		// 这一条自己的冲突：多条一起写时逐条查，不会只标第一条。
		int m_Conflicts = APPLY_CONFLICT_NONE;
		// 对应预设的哪个变体：UI 用它在提示里指回具体那一行。
		size_t m_Variant = 0;
	};

	// 同一个键被两个动作占用时，后出现的条目整条不产出（先写的那条生效），
	// 只在这里留一条记录，UI 用它说明「哪一条被跳过」。
	struct SSkippedApplyEntry
	{
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		std::string m_Command;
		size_t m_Variant = 0;
	};

	struct SApplyPlan
	{
		std::vector<SApplyEntry> m_vEntries;
		// 因为重复槽位被丢掉的条目：它们不写，但要让用户看见。
		std::vector<SSkippedApplyEntry> m_vSkipped;
		bool Valid() const { return !m_vEntries.empty(); }
		// 有覆盖冲突，或有条目因为重复槽位被跳过。
		bool HasConflicts() const;
		// 冲突条目数 + 被跳过的条目数（不是冲突标记数）。
		size_t ConflictCount() const;
	};

	// 读某个键上当前的命令；没绑定时返回空串。
	using FBoundCommand = std::function<std::string(const CBindSlot &Slot)>;

	bool IsActionSet(const SPreset &Preset);
	// 一张卡要几个键槽：互斥方案恒为 1，一组动作等于变体数（无变体时也算 1）。
	size_t KeySlotCount(const SPreset &Preset);
	// 变体的默认键：先看 m_vVariantSlots，再看 m_RecommendedSlot。
	CBindSlot RecommendedSlotFor(const SPreset &Preset, size_t VariantIndex);

	// 按预设自己的分组标记生成完整计划：
	// - EXCLUSIVE：只写 VariantIndex 那一条，vSlots 只用到第 0 项；
	// - ACTION_SET：每个变体各写一条，键槽按下标与变体一一对应。
	// 键槽留空（KEY_UNKNOWN）的变体退回它的推荐键；仍然没有键的变体跳过。
	// 任一变体依赖的 cfg 没装好时整份计划为空：不能只写出半套绑定。
	// 冲突逐条记进条目的 m_Conflicts，有冲突也照旧返回完整计划。
	SApplyPlan MakePresetPlan(const SPreset &Preset, size_t VariantIndex, const std::vector<CBindSlot> &vSlots,
		const FBoundCommand &GetBoundCommand, const int *pParamValues = nullptr, size_t NumParamValues = 0,
		bool CfgInstalled = false);

	// ---- 应用撤销：一次应用可能写好几条，撤销必须整批还原 ----
	// 记的是「每条写下去的新值 + 写之前的原值」：原值非空的键还原成原值，
	// 原来没绑的键撤销时解绑（写空串）。
	struct SRestoreEntry
	{
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		std::string m_Command;
		bool m_HadBind = false;
	};

	class CApplyUndo
	{
	public:
		// 只保留最近一次应用：再应用一次就整批换掉旧记录。
		void Record(const SApplyPlan &Plan);
		bool CanUndo() const { return !m_vEntries.empty(); }
		size_t Count() const { return m_vEntries.size(); }
		const std::vector<SRestoreEntry> &Entries() const { return m_vEntries; }
		// 这次应用是否写过这个键：卡片据此显示 Applied 徽标。
		bool Covers(const CBindSlot &Slot) const;
		void Clear() { m_vEntries.clear(); }

	private:
		std::vector<SRestoreEntry> m_vEntries;
	};

	// 撤销的执行：原值非空的键写回原值，原来没绑的键写空串（空命令 = 解绑该槽位，
	// 见 CBindStorage::Bind）。绑定器用回调注入：UI 传 CBinds，测试传假绑定器验写入序列。
	using FBinder = std::function<void(const CBindSlot &Slot, const char *pCommand)>;
	// 返回实际写入的条数；没有可撤销记录或没给绑定器时返回 0、不调回调。
	size_t RestoreUndo(const CApplyUndo &Undo, const FBinder &Binder);

	// ---- 参数校验与命令生成（纯逻辑，UI 只调用） ----
	inline constexpr int PARAM_USE_DEFAULT = std::numeric_limits<int>::min();

	int ClampParamValue(const SPresetParam &Param, int Value);
	// Direction 为 +1 / -1；步进结果夹在 [m_Min, m_Max] 内，到边界后不再溢出。
	int StepParamValue(const SPresetParam &Param, int Value, int Direction, int Steps = 1);
	std::array<int, MAX_PRESET_PARAMS> ResolveParamValues(const SPreset &Preset, const int *pValues, size_t Count);
	// 先替换命名 token，再按顺序替换裸写的 %d，因此两种占位符写法都能用。
	std::string ResolveVariantCommand(const SPreset &Preset, size_t VariantIndex, const int *pValues = nullptr, size_t Count = 0);
	// 该变体的命令是否只是 exec 一个外部 cfg 文件（命令里出现 "exec "）。
	bool VariantRequiresCfg(const SVariant &Variant);

	// ---- 内置 cfg：落盘路径与 exec 命令 ----
	// 落盘位置 = SAVE 目录（%appdata%/DDNet 这类）下的 qm_bind/ 子目录。exec 走
	// 相对路径，因为 CConsole::ExecuteFileFromCommand 用 IStorage::TYPE_ALL 从
	// SAVE 目录开始找（engine/shared/console.cpp:822-833，storage.cpp:603-614）。
	inline constexpr const char *CFG_FOLDER = "qm_bind";
	// 控制台单行上限：引擎的 INPUT/命令行长度是 255（CFG_MAX_LINE_BYTES），
	// 超过这个长度的行会被截断成半条命令。内置 cfg 逐行按这个数字自检。
	inline constexpr size_t CFG_MAX_LINE_BYTES = 255;

	// 相对 SAVE 的路径：qm_bind/qm_bind_<preset>_<变体>.cfg
	std::string CfgFileName(const SPreset &Preset, size_t VariantIndex);
	// 写进 bind 的命令：exec <相对路径>，与 CfgFileName 逐字一致。
	std::string CfgExecCommand(const SPreset &Preset, size_t VariantIndex);
	bool HasBuiltinCfg(const SPreset &Preset, size_t VariantIndex);
	// 内置 cfg 正文（纯数据，不读网络）。无内置内容时返回空串。
	std::string BuildCfgContent(const SPreset &Preset, size_t VariantIndex);
	// cfg 行约束：单行 <= 255 字节、单条命令里的 bind 嵌套 <= 5 层。
	// 返回第一条越界行号（1 起）；0 表示全部通过。
	size_t FindCfgViolation(std::string_view Content);

	// ---- 键位占用 / 遮蔽 ----
	enum
	{
		OCCUPANCY_NONE = 0,
		OCCUPANCY_SHADOWED = 1 << 0, // 同键有修饰键组合，可能遮蔽无修饰键绑定
		OCCUPANCY_RESERVED_CHORD = 1 << 1, // 保留和弦，按下不会回落到无修饰键绑定
		OCCUPANCY_MULTI_ACTION = 1 << 2, // 一条命令串里串了多个动作
	};

	struct SOccupancyEntry
	{
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		std::string m_KeyName;
		std::string m_Command;
		int m_Flags = OCCUPANCY_NONE;
	};

	// 用回调传入绑定读取，避免在纯逻辑层链接 CBinds 实现（testrunner 可以直接单测，
	// 也能用伪造数据覆盖冲突场景）。
	using FGetCommand = std::function<const char *(int Key, int Modifier)>;
	using FGetKeyName = std::function<void(int Key, int Modifier, char *pBuffer, size_t BufferSize)>;

	// 多键 Diff：每条用自己那行的键名（多键计划里写死一个键名的话，
	// 每一行都会是同一个键）。冲突文案不在这里拼，UI 用 ConflictCount 自己地化。
	std::string DescribePlanDiff(const SApplyPlan &Plan, const FGetKeyName &GetKeyName);

	std::vector<SOccupancyEntry> BuildOccupancy(const FGetCommand &GetCommand, const FGetKeyName &GetKeyName);

	// ---- 蓝图：可编译节点集合 ----
	// 节点尺寸：命令最多 25 个 ASCII 字符，168 宽刚好两行放完，不出现半截单词。
	// 分组框是唯一的例外，尺寸跟着内容走，由 GROUP_WIDTH / GROUP_HEIGHT 给默认值。
	inline constexpr float NODE_WIDTH = 168.0f;
	inline constexpr float NODE_HEIGHT = 62.0f;
	inline constexpr float GROUP_WIDTH = 380.0f;
	inline constexpr float GROUP_HEIGHT = 240.0f;
	// 分组框拖角时的下限：再小就框不住内容，也不方便再抓住角。
	inline constexpr float GROUP_MIN_WIDTH = 160.0f;
	inline constexpr float GROUP_MIN_HEIGHT = 100.0f;
	// 对齐吸附阈值与连线曲线采样段数。
	inline constexpr float ALIGN_SNAP_DISTANCE = 7.0f;
	inline constexpr int EDGE_CURVE_SEGMENTS = 16;

	enum class ENodeType
	{
		EVENT_PRESS,
		STATE_TOGGLE,
		STATE_TOGGLE_RESTORE,
		ACTION_COMMAND,
		ACTION_ECHO,
		// 分组框：不参与编译，只把一片节点框起来，拖动它等于拖动框内节点。
		GROUP,
		NOTE,
		NUM,
	};

	struct SNode
	{
		int m_Id = 0;
		ENodeType m_Type = ENodeType::ACTION_COMMAND;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		// 普通节点固定尺寸；分组框用更大的一对值，命中与绘制都按这里来。
		float m_W = NODE_WIDTH;
		float m_H = NODE_HEIGHT;
		std::string m_Command;
		std::string m_Comment;
	};

	struct SEdge
	{
		int m_From = 0;
		int m_To = 0;
	};

	enum class ECompileError
	{
		MISSING_EVENT,
		MULTIPLE_EVENTS,
		CYCLE,
		ORPHAN_NODE,
		EMPTY_COMMAND,
		NO_OUTPUT,
		TOO_LONG,
		TOO_MANY_NESTING,
		NUM,
	};

	struct SCompileResult
	{
		std::string m_Command;
		std::string m_ConfigLine;
		std::vector<ECompileError> m_vErrors;
		size_t m_Bytes = 0;
		int m_Depth = 0;
		bool Ok() const { return m_vErrors.empty(); }
	};

	const char *CompileErrorLabel(ECompileError Error);
	const char *NodeTypeLabel(ENodeType Type);
	// 帮助面板用的节点说明（英文源 key，运行时过 Localize）。
	const char *NodeTypeHelp(ENodeType Type);

	// 节点尺寸的默认值见 ENodeType 之前的 NODE_WIDTH / NODE_HEIGHT。
	inline constexpr float GRID_CELL_SIZE = 180.0f;
	inline constexpr int MAX_NESTING_DEPTH = 5;

	// 工坊页的纵向骨架：工具条 / 正文 / 页脚。版式只在这里算一次，UI 层不再
	// 自己连锁 HSplit：HSplitBottom 的 pBottom 拿到的是「被切下来的那条带」，
	// 把剩余区域写进 pBottom 会把正文压成页脚上方的一条细带，卡片于是全部
	// 沉到面板底部、只露出顶边（2026-11 的实际回归）。
	inline constexpr float WORKSHOP_TOOLBAR_HEIGHT = 30.0f;
	inline constexpr float WORKSHOP_FOOTER_HEIGHT = 18.0f;
	inline constexpr float WORKSHOP_BODY_TOP_GAP = 12.0f;
	inline constexpr float WORKSHOP_BODY_BOTTOM_GAP = 8.0f;

	struct SWorkshopLayoutFrame
	{
		CUIRect m_Toolbar{};
		CUIRect m_Body{};
		CUIRect m_Footer{};
	};

	SWorkshopLayoutFrame ResolveWorkshopLayout(const CUIRect &MainView, float UiScale);

	// ---- 卡片版式：底栏两行 ----
	// 第一行 [模式徽标][变体][来源]，第二行 [键槽（吃满剩余宽度）][应用]。
	// 过去的单行版本把三段硬挤进同一行，键槽被压到几十像素，按钮叠在键槽上。
	// 行高与最小宽度都是常量，网格行高（WORKSHOP_CARD_HEIGHT）跟着它们走，
	// 同一行的三张卡片永远等高。
	inline constexpr float WORKSHOP_CARD_HEIGHT = 220.0f;
	inline constexpr float WORKSHOP_MODE_ROW_HEIGHT = 20.0f;
	// 动作行与模式行同高：键槽、应用按钮和上一行的徽标在同一条竖线节奏上。
	// 键槽曾经比模式行高出 8px，卡片底部看起来多出一条更粗的横带。
	inline constexpr float WORKSHOP_ACTION_ROW_HEIGHT = WORKSHOP_MODE_ROW_HEIGHT;
	inline constexpr float WORKSHOP_ROW_GAP = 4.0f;
	inline constexpr float WORKSHOP_ITEM_GAP = 8.0f;
	inline constexpr float WORKSHOP_KEY_SLOT_MIN_WIDTH = 96.0f;
	inline constexpr float WORKSHOP_APPLY_WIDTH = 62.0f;
	inline constexpr float WORKSHOP_APPLY_MIN_WIDTH = 44.0f;
	inline constexpr float WORKSHOP_STEPPER_WIDTH = 88.0f;
	inline constexpr float WORKSHOP_SOURCE_WIDTH = 80.0f;

	// 底栏固定段的纵向占位：参数行 + 模式行 + 键槽行 + 应用行。
	// 瀑布流算卡高和真正的底栏版式必须走同一份算法：两处各写一遍，改一处就一定会有
	// 一次漂移（先前的「量出来的高度」和「画出来的内容」各说各话就是这么来的）。
	struct SWorkshopFooterMetrics
	{
		float m_ActionHeight = 0.0f;
		float m_ModeHeight = 0.0f;
		float m_RowGap = 0.0f;
		float m_ParamHeight = 0.0f;
		// 一组动作的键槽行区总高；互斥方案（单行键槽）为 0，键槽与应用同一行。
		float m_KeyRowsHeight = 0.0f;
		size_t m_NumKeyRows = 0;
		bool m_SteppersStacked = false;
		// 包含行距的底栏总高。
		float Height() const;
	};

	SWorkshopFooterMetrics ResolveWorkshopFooterMetrics(float InnerWidth, size_t NumSteppers, size_t NumKeyRows, float UiScale);

	struct SWorkshopCardFooterRequest
	{
		float m_UiScale = 1.0f;
		float m_ModeBadgeWidth = 0.0f;
		float m_VariantWidth = 0.0f;
		// 应用按钮宽度由 UI 按文案量出来（Install and apply 比 Apply 宽）。
		float m_ApplyWidth = 0.0f;
		size_t m_NumSteppers = 0;
		bool m_ShowKeySlot = true;
		// 键槽行数：互斥方案 1 行（键槽与应用按钮同一行）；一组动作 = 变体数，
		// 每行是 [变体名][键槽]，应用按钮独占底下一行。
		size_t m_NumKeyRows = 1;
		// 变体名列宽：>0 时每行左侧留出这一列写变体名，键槽吃右边剩余宽度。
		float m_KeyLabelWidth = 0.0f;
	};

	struct SWorkshopCardFooter
	{
		// 上方的剩余区域：命令框 + TIP 行都画在这里。
		CUIRect m_CodeArea{};
		CUIRect m_ParamRow{};
		CUIRect m_ModeRow{};
		CUIRect m_ActionRow{};
		CUIRect m_ModeBadge{};
		CUIRect m_VariantButton{};
		CUIRect m_SourceLabel{};
		std::array<CUIRect, MAX_PRESET_PARAMS> m_aSteppers{};
		size_t m_NumSteppers = 0;
		// 键槽行：互斥方案只有第 0 行（与应用按钮同一行，m_KeySlot 就是它）；
		// 一组动作每行一个变体，第 i 行 = [m_aKeyLabels[i]][m_aKeySlots[i]]。
		std::array<CUIRect, MAX_PRESET_KEY_ROWS> m_aKeyLabels{};
		std::array<CUIRect, MAX_PRESET_KEY_ROWS> m_aKeySlots{};
		size_t m_NumKeyRows = 0;
		CUIRect m_KeySlot{};
		CUIRect m_ApplyButton{};
	};

	// 从卡片的内边距区域（已去掉 Padding）底部切出两行（有数值参数时再多一行
	// 步进器，行高与键槽一致）。所有子矩形互不重叠，键槽至少
	// WORKSHOP_KEY_SLOT_MIN_WIDTH*Scale（行宽不够时应用按钮先让到最小宽度）。
	SWorkshopCardFooter ResolveWorkshopCardFooter(const CUIRect &Inner, const SWorkshopCardFooterRequest &Request);

	// 图容器。实现里用 ankerl::unordered_dense 维护 Id→index 与网格桶；
	// 用 pimpl 是为了不让第三方单头库污染所有包含者的编译时间。
	class CBlueprintGraph
	{
	public:
		CBlueprintGraph();
		~CBlueprintGraph();

		CBlueprintGraph(const CBlueprintGraph &) = delete;
		CBlueprintGraph &operator=(const CBlueprintGraph &) = delete;
		CBlueprintGraph(CBlueprintGraph &&Other) noexcept;
		CBlueprintGraph &operator=(CBlueprintGraph &&Other) noexcept;

		void Clear();
		// 深拷贝整张图：撤销栈按整图快照记录，比逐操作补丁不容易漏字段。
		CBlueprintGraph Clone() const;
		int AddNode(SNode Node);
		bool RemoveNode(int Id);
		bool Connect(int From, int To);
		bool Disconnect(int From, int To);
		bool MoveNode(int Id, float X, float Y);
		bool SetNodeCommand(int Id, std::string_view Command);
		// 分组框拖角改尺寸；普通节点也允许，但最小尺寸有下限。
		bool SetNodeSize(int Id, float Width, float Height);
		bool SetNodeType(int Id, ENodeType Type);
		bool SetNodeComment(int Id, std::string_view Comment);
		// 复制一个节点（不含边），返回新节点 ID；0 表示源节点不存在。
		int DuplicateNode(int Id, float OffsetX, float OffsetY);
		// 断开该节点的全部出边与入边，返回被移除的边数。
		int DisconnectNode(int Id);

		const std::vector<SNode> &Nodes() const;
		const std::vector<SEdge> &Edges() const;
		const SNode *FindNode(int Id) const;

		std::vector<int> TopologicalOrder(bool &rHasCycle) const;
		SCompileResult Compile(std::string_view KeyName) const;

		// 只返回视口内的节点 ID，画布据此跳过屏幕外的绘制与命中测试。
		std::vector<int> VisibleNodes(float MinX, float MinY, float MaxX, float MaxY) const;
		size_t IndexedCellCount() const;

	private:
		struct SGraphData;
		std::unique_ptr<SGraphData> m_pData;
	};

	// 从内置预设造一个最小流程，用于「从预设导入」入口。
	CBlueprintGraph GraphFromPreset(const SPreset &Preset, size_t VariantIndex);

	// ---- 撤销栈：记录「改动前」的整图快照 ----
	// 粒度是一次可感知的编辑（新建/删除/连线/剪线/拖动/改命令/换类型）。
	// 图最多几十个节点，整图快照比逐操作补丁简单，也不会漏字段。
	struct SBlueprintSnapshot
	{
		size_t m_Flow = 0;
		CBlueprintGraph m_Graph;
	};

	class CBlueprintHistory
	{
	public:
		static constexpr size_t MAX_DEPTH = 32;
		// 改动发生之前调用：把当前图存进撤销栈，并清空重做栈。
		void Record(size_t Flow, const CBlueprintGraph &Graph);
		bool CanUndo() const { return !m_vUndo.empty(); }
		bool CanRedo() const { return !m_vRedo.empty(); }
		// 弹出一侧快照；调用方负责把当前现场压回另一侧。
		bool PopUndo(size_t &rFlow, CBlueprintGraph &rGraph);
		bool PopRedo(size_t &rFlow, CBlueprintGraph &rGraph);
		void PushUndo(size_t Flow, CBlueprintGraph Graph);
		void PushRedo(size_t Flow, CBlueprintGraph Graph);
		void Clear();
		size_t UndoDepth() const { return m_vUndo.size(); }
		size_t RedoDepth() const { return m_vRedo.size(); }

	private:
		std::vector<SBlueprintSnapshot> m_vUndo;
		std::vector<SBlueprintSnapshot> m_vRedo;
	};

	// ---- 画布交互：命中测试与连线落点 ----
	// 端口是节点左右边线中点的圆形接点；命中半径按画布坐标算，缩小时不放大手感。
	inline constexpr float PORT_HIT_RADIUS = 12.0f;
	// 连线的抓取范围按画布坐标给，UI 层再除以缩放，保证屏幕上手感恒定。
	inline constexpr float EDGE_HIT_TOLERANCE = 10.0f;

	enum class ECanvasPart
	{
		NONE,
		BODY,
		IN_PORT,
		OUT_PORT,
	};

	struct SCanvasHit
	{
		int m_NodeId = 0;
		ECanvasPart m_Part = ECanvasPart::NONE;
		bool Empty() const { return m_Part == ECanvasPart::NONE; }
	};

	// 命中测试只查询点附近的网格桶（走 VisibleNodes），节点再多也不线性扫描。
	SCanvasHit HitTestCanvas(const CBlueprintGraph &Graph, float X, float Y, float Radius = PORT_HIT_RADIUS);

	// 一条边在画布坐标里的正交折线：出端口 → 竖直中段 → 入端口。
	// 绘制与命中测试共用同一份几何，画出来的线和能点中的线永远是同一条。
	struct SEdgePolyline
	{
		float m_X0 = 0.0f;
		float m_Y0 = 0.0f;
		float m_MidX = 0.0f;
		float m_X1 = 0.0f;
		float m_Y1 = 0.0f;
	};

	// 任一端节点不存在时返回 false，调用方直接跳过这条边。
	bool ResolveEdgePolyline(const CBlueprintGraph &Graph, int From, int To, SEdgePolyline &rOut);

	struct SEdgeHit
	{
		int m_From = 0;
		int m_To = 0;
		bool Empty() const { return m_From == 0 || m_To == 0; }
	};

	// 点到折线距离在 Tolerance 内的边才算命中；多条边同时命中时取最近的那条。
	SEdgeHit HitTestEdge(const CBlueprintGraph &Graph, float X, float Y, float Tolerance = EDGE_HIT_TOLERANCE);

	// 框选：画布坐标矩形与节点矩形相交即算选中（贴边也算），返回值按节点 ID 升序。
	// 交互模型参考 Nelarius/imnodes（MIT）的框选语义，几何在本仓库重写。
	std::vector<int> NodesInRect(const CBlueprintGraph &Graph, float MinX, float MinY, float MaxX, float MaxY);

	// 所有节点的画布包围盒：小地图据此把整张图缩进固定大小的框里。
	struct SCanvasBounds
	{
		float m_MinX = 0.0f;
		float m_MinY = 0.0f;
		float m_MaxX = 0.0f;
		float m_MaxY = 0.0f;
		bool m_HasNodes = false;
		float Width() const { return m_MaxX - m_MinX; }
		float Height() const { return m_MaxY - m_MinY; }
	};

	SCanvasBounds GraphBounds(const CBlueprintGraph &Graph);

	// 拖动时的对齐吸附：把被拖节点的左/中/右、上/中/下跟其它节点比较，
	// 落在 ALIGN_SNAP_DISTANCE 内就吸附过去，并给出要画的参考线坐标。
	struct SNodeAlignment
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		bool m_SnappedX = false;
		bool m_SnappedY = false;
		// 参考线的画布坐标：竖直线的 X、水平线的 Y。
		float m_GuideX = 0.0f;
		float m_GuideY = 0.0f;
	};

	SNodeAlignment ResolveNodeAlignment(const CBlueprintGraph &Graph, int DragId, float X, float Y);

	// 分组框成员：中心点落在框内、且归属于这个框的节点（不含分组框自己）。
	// 拖动分组框时一起走；框重叠不会把同一个节点带走两次。
	std::vector<int> NodesContainedIn(const CBlueprintGraph &Graph, int GroupId);

	// 分组框归属：一个节点最多属于一个框。框重叠时面积小的更具体，面积相同按 Id 小的胜。
	// 返回 0 表示不在任何框里。
	int GroupOwningNode(const CBlueprintGraph &Graph, int NodeId);

	// ---- 属性面板的几何字段：X / Y / 宽 / 高（纯逻辑，UI 只调用） ----
	// 步进量与画布格点一致；尺寸下限与 CBlueprintGraph::SetNodeSize 同源，
	// 面板因此不需要自己再夹一遍。
	inline constexpr float NODE_GEOMETRY_STEP = 8.0f;
	// 坐标上限：再远就落不进可视区与缩放下限，只会在小地图里变成一个点。
	inline constexpr float NODE_GEOMETRY_LIMIT = 16384.0f;

	enum class ENodeGeometryField
	{
		X,
		Y,
		WIDTH,
		HEIGHT,
		NUM_FIELDS,
	};

	struct SNodeGeometry
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	// 该类型节点的最小宽高：分组框与普通节点各一档。
	void NodeGeometryMinSize(ENodeType Type, float &rMinW, float &rMinH);
	// 读节点当前几何（原样，不做量化）。
	SNodeGeometry ReadNodeGeometry(const SNode &Node);
	// 面板上显示的整数：四舍五入到整像素，拖动留下的长尾不跟着抖。
	int NodeGeometryDisplayValue(const SNodeGeometry &Geometry, ENodeGeometryField Field);
	// 夹紧：位置夹到 ±上限，尺寸不低于该类型的最小宽高、也不超过上限。
	SNodeGeometry ClampNodeGeometry(ENodeType Type, const SNodeGeometry &Geometry);
	// 步进：先把当前值对齐到步长整数格，再加减 Direction*Steps 格，最后夹紧。
	SNodeGeometry StepNodeGeometry(ENodeType Type, const SNodeGeometry &Geometry, ENodeGeometryField Field, int Direction, int Steps = 1);
	// 写回图：位置所有节点都写，宽高只写分组框（普通节点尺寸固定）。
	bool ApplyNodeGeometry(CBlueprintGraph &Graph, int NodeId, const SNodeGeometry &Geometry);
	// 多选包围盒：只统计图里存在的节点；一个都没命中时 m_HasNodes 保持 false。
	SCanvasBounds SelectionBounds(const CBlueprintGraph &Graph, const std::vector<int> &vIds);

	struct SCanvasPoint
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	// 双击空白画布时的落点：吸附到最近的网格格点，新节点不会压在既有节点上。
	SCanvasPoint SnapToCanvasGrid(float X, float Y);

	// 连线的三次贝塞尔采样点（画线与命中测试共用），按 EDGE_CURVE_SEGMENTS 等分。
	std::vector<SCanvasPoint> SampleEdgeCurve(const SEdgePolyline &Line);

	enum class ELinkOutcome
	{
		CONNECTED,
		NO_SOURCE,
		NO_TARGET,
		SELF,
		DUPLICATE,
		CYCLE,
		NUM,
	};

	const char *LinkOutcomeLabel(ELinkOutcome Outcome);

	// 拖拽结束：只有落在别的节点的输入端口（或节点体）上、不重复且不闭环时才建边。
	// 返回非 CONNECTED 时图完全不变，画布上不会留下悬空残线。
	ELinkOutcome ResolveLinkDrop(CBlueprintGraph &Graph, int FromId, float X, float Y);

	// ---- 常用逻辑卡片：点一下就在画布光标处生成对应节点 ----
	struct SNodeTemplate
	{
		const char *m_pTitle;
		const char *m_pDescription;
		ENodeType m_Type;
		const char *m_pCommand;
		const char *m_pComment;
	};

	const std::vector<SNodeTemplate> &NodeTemplates();
	// 返回新节点 ID；模板下标越界时返回 0 且图不变。
	int AddNodeFromTemplate(CBlueprintGraph &Graph, size_t TemplateIndex, float X, float Y);

	// ---- 命令片段：命令库下拉与快捷插入 ----
	struct SCommandSnippet
	{
		const char *m_pLabel;
		const char *m_pCommand;
	};

	const std::vector<SCommandSnippet> &CommandSnippets();
	// 追加到已有命令末尾；空串直接写入，已存在的同一段命令不重复追加。
	std::string InsertSnippet(std::string_view Existing, std::string_view Snippet);

	// 节点类型切换（检视器「类型」按钮），Direction 为 +1/-1，循环回到另一端。
	ENodeType CycleNodeType(ENodeType Type, int Direction = 1);

	// ---- 多张图：标签栏 + 每张图独立标题/文件名，导出当前或全部 ----
	struct SFlowFile
	{
		std::string m_Title;
		std::string m_FileName;
		CBlueprintGraph m_Graph;
	};

	struct SCfgBundle
	{
		std::string m_FileName;
		std::string m_Title;
		std::string m_Content;
		size_t m_BindLines = 0;
		bool m_Ok = false;
	};

	// 标题 → 文件名词干：只保留 ASCII 字母数字，其余折叠成单个下划线。
	std::string SlugifyTitle(std::string_view Title);
	// DuplicateIndex > 1 时加 _2、_3 后缀，避免两张同名图互相覆盖。
	std::string BlueprintFileName(std::string_view Title, size_t DuplicateIndex = 1);

	class CFlowCollection
	{
	public:
		CFlowCollection();
		size_t Count() const;
		size_t ActiveIndex() const;
		bool SetActive(size_t Index);
		const SFlowFile &Flow(size_t Index) const;
		CBlueprintGraph &Graph(size_t Index);
		const CBlueprintGraph &Graph(size_t Index) const;
		std::string Title(size_t Index) const;
		std::string FileName(size_t Index) const;
		size_t AddFlow(std::string_view Title);
		bool RemoveFlow(size_t Index);
		bool RenameFlow(size_t Index, std::string_view Title);
		// 头部注释 + 全部 bind 行；校验不过的图不写 bind 行，只写被拒原因。
		SCfgBundle BuildCfg(size_t Index, std::string_view KeyName) const;
		std::vector<SCfgBundle> BuildAllCfg(std::string_view KeyName) const;

	private:
		std::vector<SFlowFile> m_vFlows;
		size_t m_Active = 0;
		void RefreshFileNames();
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_WORKSHOP_H
