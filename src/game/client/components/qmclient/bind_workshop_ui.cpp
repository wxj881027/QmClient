#include <base/system.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContainers.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_workshop.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 约束 brief（refactoring-ui Workflow B）：
//
// 内容分级 —— 每屏只有一件事最重：
//   工坊：预设标题 → 说明 → 命令 → 「应用」（唯一主操作），模式/来源/变体全部退到次级。
//   蓝图：画布 → 选中节点 → 「应用到按键」（唯一主操作）；校验与导出是次级。
//   官方：键盘编辑器（唯一主操作），冲突信息在 Bind 一览卡与键面上。
// 间距标尺（ui_token::spacing，全部再乘 UiScale）：XS 4 / SM 8 / MD 12 / LG 16 / XL 24。
//   卡片内边距 14；卡片内组间距 SM；组内行距 XS；区块之间 MD。
//   网格间距 CARD_GAP 16；页面级区块间距 12。
// 字号（ui_token::font）：标题 HEADLINE 14 / 正文 BODY 12 / 辅助 SMALL 10 / 注脚 TIP 9。
//   一屏最多三级字号，不再出现 9.5、10.4 这类手写档位。
// 颜色：全部来自 ui_token::color，只保留 正文 / 次级 / 三级 三种文字色；
//   语义色仅用于模式徽标、校验状态与冲突标记。
namespace
{
	using namespace qm_bind_workshop;

	// CMenus 的 Ui()/TextRender() 是 protected 的组件 getter，匿名命名空间里的
	// 绘制 helper 拿不到；每个渲染入口开头把当帧指针写进来即可，UI 为单线程。
	CUi *g_pUi = nullptr;
	ITextRender *g_pTextRender = nullptr;

	// ---- 本文件专属色：ui_token 里没有对应档位的蓝图配色 ----
	// 蓝图编辑器有自己的视觉语言（节点类型、端口、连线、画布、浮动面板），这些颜色
	// 不随 UI 主题走，所以不塞进全局 token；但也不在绘制点写数字字面量，统一在这里
	// 具名，调用点只出现名字。
	namespace workshop_color
	{
		// 画布底色与「打开帮助时的压暗层」。
		inline constexpr ColorRGBA BLUEPRINT_CANVAS{0.04f, 0.05f, 0.07f, 0.62f};
		inline constexpr ColorRGBA BLUEPRINT_CANVAS_DIM{0.02f, 0.03f, 0.05f, 0.55f};
		// 浮动面板/弹层底板：比 SURFACE_ELEVATED 更沉、接近不透明，压在画布上才不糊。
		inline constexpr ColorRGBA PANEL_SURFACE{0.07f, 0.08f, 0.10f, 0.98f};
		// 弹层与选中行的描边：比 CONTAINER_BORDER 强一档，暗底上才看得出边。
		inline constexpr ColorRGBA SOFT_BORDER{1.0f, 1.0f, 1.0f, 0.18f};
		// 节点类型色：事件节点偏亮蓝、备注节点偏灰白，和强调色/语义色区分开。
		inline constexpr ColorRGBA NODE_EVENT_ACCENT{0.65f, 0.85f, 1.0f, 1.0f};
		inline constexpr ColorRGBA NODE_NOTE_ACCENT{0.75f, 0.78f, 0.84f, 1.0f};
		// 未连接的入口端口与普通连线：中性白，比强调色弱、比边框强。
		inline constexpr ColorRGBA PORT_IDLE{0.75f, 0.80f, 0.88f, 1.0f};
		inline constexpr ColorRGBA LINK_IDLE{1.0f, 1.0f, 1.0f, 0.34f};
	}

	constexpr int MAX_PRESETS = 64;
	constexpr int MAX_BLUEPRINT_BUTTONS = 96;

	// 蓝图一次只编译成一条 bind，键名在蓝图页固定为 h（与官方页/工坊页一致）。
	constexpr const char *BLUEPRINT_KEY_NAME = "h";
	constexpr float BLUEPRINT_LEFT_WIDTH = 176.0f;
	constexpr float BLUEPRINT_RIGHT_WIDTH = 300.0f;
	constexpr size_t MAX_FLOW_TABS = 5;

	// 卡片网格：3 列定宽，行高固定，纵向滚动代替分页。行高来自纯逻辑层的
	// WORKSHOP_CARD_HEIGHT：底栏两行（模式行 + 动作行，两者同高）加参数行一共占掉
	// 的纵向空间已经算在这个常量里，测试锁住的就是同一个数字。
	constexpr int WORKSHOP_COLUMNS = 3;
	// 分类胶囊的高度：与工具条里的搜索框同高。
	constexpr float WORKSHOP_CHIP_HEIGHT = 26.0f;

	// 骨架高度来自纯逻辑层（可被单测锁住），但间距档位必须仍由 token 决定：
	// 两处写法一旦漂移就编译失败，而不是悄悄变成两套标尺。
	static_assert(ui_token::spacing::MD == qm_bind_workshop::WORKSHOP_BODY_TOP_GAP, "workshop body top gap must follow the spacing token");
	static_assert(ui_token::spacing::SM == qm_bind_workshop::WORKSHOP_BODY_BOTTOM_GAP, "workshop body bottom gap must follow the spacing token");

	// 一张卡按内容量出来的尺寸：瀑布流按它装箱，绘制时按同一组数字切上下两段，
	// 所以「量出来的高度」和「画出来的内容」不会各说各话。
	struct SWorkshopCardMetrics
	{
		float m_Height = 0.0f;
		float m_DescHeight = 0.0f;
		float m_TipHeight = 0.0f;
		float m_CodeHeight = 0.0f;
		int m_DescLines = 1;
		int m_CodeLines = 0;
	};

	// 一张卡画几行键槽：一组动作每个变体一行，互斥方案恒 1 行（就是现在那一格）。
	size_t PresetKeyRows(const SPreset &Preset)
	{
		const size_t Rows = IsActionSet(Preset) && !Preset.m_vVariants.empty() ? Preset.m_vVariants.size() : 1;
		return std::min(Rows, MAX_PRESET_KEY_ROWS);
	}

	// 第 Row 行键槽对应哪个变体：一组动作是行号本身，互斥方案只有选中的那一版。
	size_t VariantForRow(const SPreset &Preset, size_t SelectedVariant, size_t Row)
	{
		if(!IsActionSet(Preset) || Preset.m_vVariants.empty())
			return SelectedVariant;
		return std::min(Row, Preset.m_vVariants.size() - 1);
	}

	struct SWorkshopState
	{
		ECategory m_Category = ECategory::ALL;
		CLineInputBuffered<128> m_Filter;
		std::array<size_t, MAX_PRESETS> m_aVariant{};
		// 键槽按「卡片 × 变体」存：一组动作每个变体一个键，互斥方案只用第 0 行。
		// 用 vector 而不是 std::array：CBindSlot 没有默认构造函数，数组装不满。
		std::array<std::vector<CBindSlot>, MAX_PRESETS> m_aaSKeySlots;
		std::array<CButtonContainer, MAX_PRESETS> m_aApplyButtons{};
		// 卡片外框也走按钮表面管线，需要一个稳定的 UI ID（地址参与动画节点键）。
		std::array<unsigned char, MAX_PRESETS> m_aCardSurfaceIds{};
		// 键槽的读键/清除按钮：每个 (卡片, 变体) 一份，控件地址就是它的 ID。
		std::array<CButtonContainer, MAX_PRESETS * MAX_PRESET_KEY_ROWS> m_aKeyReaderButtons{};
		std::array<CButtonContainer, MAX_PRESETS * MAX_PRESET_KEY_ROWS> m_aKeyClearButtons{};
		// 数值参数的步进器按钮：每张卡最多 MAX_PRESET_PARAMS 个。
		std::array<CButtonContainer, MAX_PRESETS * MAX_PRESET_PARAMS> m_aParamMinusButtons{};
		std::array<CButtonContainer, MAX_PRESETS * MAX_PRESET_PARAMS> m_aParamPlusButtons{};
		// 当前参数值：进入页面时按预设默认值初始化，步进后写回。
		std::array<std::array<int, MAX_PRESET_PARAMS>, MAX_PRESETS> m_aaParamValues{};
		// cfg 安装状态：每个预设每一位置 1 表示该变体的内置 cfg 已经落盘。
		std::array<uint32_t, MAX_PRESETS> m_aCfgInstalled{};
		std::array<std::string, MAX_PRESETS> m_aCfgError{};
		bool m_CfgProbed = false;
		CButtonContainer m_UndoButton;
		CButtonContainer m_ConfirmButton;
		CButtonContainer m_CancelButton;
		// 滚动状态与设置页列表同一套实现（CScrollRegion）：地址参与 UI 元素 ID，
		// 必须跟着页面状态常驻，不能每帧新建。
		CScrollRegion m_ScrollRegion;
		// 卡片高度缓存：列宽、缩放、变体、参数或 cfg 状态变了才重新测量。
		std::array<SWorkshopCardMetrics, MAX_PRESETS> m_aCardMetrics{};
		uint64_t m_CardMetricsKey = ~0ULL;
		uint64_t m_ScrollSignature = ~0ULL;
		size_t m_PendingPreset = MAX_PRESETS;
		SApplyPlan m_PendingPlan;
		// 撤销是整批的：一次应用写几条 bind，撤销就整批还原（见纯逻辑层 CApplyUndo）。
		CApplyUndo m_Undo;
		bool m_ConfirmOpen = false;
		std::string m_Status;
		double m_StatusTime = 0.0;
		bool m_Initialized = false;
	};

	SWorkshopState &WorkshopState()
	{
		static SWorkshopState s_State;
		if(!s_State.m_Initialized)
		{
			const std::vector<SPreset> &vPresets = BuiltinPresets();
			for(size_t Index = 0; Index < vPresets.size() && Index < MAX_PRESETS; ++Index)
			{
				// 键槽初值取预设自己的推荐键：一组动作每个变体一个，互斥方案只有第 0 行。
				const size_t KeyRows = PresetKeyRows(vPresets[Index]);
				s_State.m_aaSKeySlots[Index].assign(KeyRows, CBindSlot(KEY_UNKNOWN, KeyModifier::NONE));
				for(size_t Row = 0; Row < KeyRows; ++Row)
					s_State.m_aaSKeySlots[Index][Row] = RecommendedSlotFor(vPresets[Index], Row);
				const size_t NumParams = std::min(vPresets[Index].m_vParams.size(), MAX_PRESET_PARAMS);
				for(size_t Param = 0; Param < NumParams; ++Param)
					s_State.m_aaParamValues[Index][Param] = vPresets[Index].m_vParams[Param].m_Default;
			}
			s_State.m_Initialized = true;
		}
		return s_State;
	}

	struct SBlueprintState
	{
		CFlowCollection m_Flows;
		float m_PanX = 0.0f;
		float m_PanY = 0.0f;
		float m_Zoom = 1.0f;
		int m_SelectedNode = 0;
		// 多选集合：框选或 Shift/Ctrl 点击累加。m_SelectedNode 是主选，
		// 检视器、右键菜单只认它，拖动与删除作用于整个集合。
		std::vector<int> m_vSelectedNodes;
		bool m_BoxSelecting = false;
		// 分组框拖角改尺寸：m_ResizeNode 非 0 时指针在改这个框的大小。
		int m_ResizeNode = 0;
		// 对齐参考线：本帧拖到与别的节点对齐时的线坐标。
		bool m_HasGuideX = false;
		bool m_HasGuideY = false;
		float m_GuideX = 0.0f;
		float m_GuideY = 0.0f;
		float m_BoxStartX = 0.0f;
		float m_BoxStartY = 0.0f;
		// 选中的连线：Delete 与右键都能删它，和选中节点互斥。
		int m_SelectedEdgeFrom = 0;
		int m_SelectedEdgeTo = 0;
		int m_LibraryIndex = 0;
		int m_DragNode = 0;
		int m_LinkFrom = 0;
		// 抓取点在画布坐标里的偏移：拖拽期间节点相对指针保持不动，
		// 不用「上一帧指针位置」反推，避免掉帧时累积漂移。
		float m_GrabX = 0.0f;
		float m_GrabY = 0.0f;
		bool m_Linking = false;
		bool m_Panning = false;
		// 最近一次指针落在画布上的位置：右侧卡片就在这个格点生成节点。
		float m_CursorX = 120.0f;
		float m_CursorY = 120.0f;
		bool m_HasCursor = false;
		// 双击空白处的节点类型菜单。
		bool m_TypeMenuOpen = false;
		float m_TypeMenuCanvasX = 0.0f;
		float m_TypeMenuCanvasY = 0.0f;
		float m_TypeMenuScreenX = 0.0f;
		float m_TypeMenuScreenY = 0.0f;
		// 右键节点的上下文菜单。
		int m_ContextNode = 0;
		float m_ContextScreenX = 0.0f;
		float m_ContextScreenY = 0.0f;
		char m_DoubleClickId = 0;
		CLineInputBuffered<96> m_TitleInput;
		int m_TitleFlow = -1;
		CLineInputBuffered<256> m_CommandInput;
		int m_EditedNode = -1;
		bool m_FocusCommand = false;
		// 标签栏切换/新增延迟到帧尾执行：CFlowCollection 扩容会让当帧捕获的
		// 图引用失效，延迟一帧换掉引用比到处重新取安全。
		int m_PendingActiveFlow = -1;
		// 属性面板几何字段的按钮 id：X / Y / 宽 / 高 各一组「−」「+」。
		std::array<CButtonContainer, 8> m_aGeometryButtons{};
		bool m_PendingAddFlow = false;
		// 关闭标签延迟到帧尾：移除流程会让当帧捕获的图引用失效。
		int m_PendingCloseFlow = -1;
		// 帮助面板：i 按钮打开，先给二级菜单，再进具体章节；打开期间画布不收手势。
		bool m_HelpOpen = false;
		int m_HelpSection = 0;
		std::array<CButtonContainer, MAX_BLUEPRINT_BUTTONS> m_aButtons{};
		size_t m_ButtonIndex = 0;
		std::string m_Status;
		double m_StatusTime = 0.0;
		bool m_Initialized = false;
		// 撤销栈：0 无、1 撤销、2 重做；在帧尾执行，避免中途换掉图引用。
		CBlueprintHistory m_History;
		int m_PendingHistory = 0;
	};

	SBlueprintState &BlueprintState()
	{
		static SBlueprintState s_State;
		if(!s_State.m_Initialized)
		{
			// 第一条流程是「示例流程」而不是某个预设：预设有卡片可看，蓝图是空的，
			// 新用户第一次进来必须先知道每个节点干什么，再谈怎么连。
			CBlueprintGraph &Example = s_State.m_Flows.Graph(0);
			Example.Clear();
			s_State.m_Flows.RenameFlow(0, Localize("Example flow"));
			SNode Note;
			Note.m_Type = ENodeType::NOTE;
			Note.m_X = 24.0f;
			Note.m_Y = 24.0f;
			// 示例备注是一整段使用说明，默认 168×62 的节点根本放不下：给足宽高，
			// 让正文按节点高度铺满（正文行数由节点高度算，见节点绘制处）。
			Note.m_W = 300.0f;
			Note.m_H = 156.0f;
			Note.m_Comment = Localize("Press a key, do a command: drag from the right dot of one node to the left dot of the next. Double-click a node to edit it, right-click a link to delete it, Ctrl+Z to undo.");
			Example.AddNode(Note);
			SNode Event;
			Event.m_Type = ENodeType::EVENT_PRESS;
			Event.m_X = 48.0f;
			Event.m_Y = 210.0f;
			Event.m_Comment = Localize("Pick the key that starts this flow.");
			const int EventId = Example.AddNode(Event);
			SNode Command;
			Command.m_Type = ENodeType::ACTION_COMMAND;
			Command.m_X = 300.0f;
			Command.m_Y = 210.0f;
			Command.m_Command = "say example";
			Command.m_Comment = Localize("Any console command or bind command.");
			const int CommandId = Example.AddNode(Command);
			Example.Connect(EventId, CommandId);
			s_State.m_Initialized = true;
		}
		return s_State;
	}

	CButtonContainer *NextButton(SBlueprintState &State)
	{
		if(State.m_ButtonIndex >= State.m_aButtons.size())
			State.m_ButtonIndex = 0;
		return &State.m_aButtons[State.m_ButtonIndex++];
	}

	void ResetButtonCursor(SBlueprintState &State)
	{
		State.m_ButtonIndex = 0;
	}

	const char *LocalizeWrapper(const char *pText)
	{
		return Localize(pText);
	}

	CUiV2AnimationRuntime *AnimationRuntime(const IUiContext &Ctx)
	{
		// 设置页内容趟会把 Ctx.m_pAnim 置空，必须回退到全局运行时。
		return Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : Ctx.m_pUi->QmAnimationRuntime();
	}

	float ResolveAlpha(const IUiContext &Ctx, uint64_t ScopeHash, uint64_t Id)
	{
		CUiV2AnimationRuntime *pAnim = AnimationRuntime(Ctx);
		if(pAnim == nullptr || (Ctx.m_pUi != nullptr && Ctx.m_pUi->AnimationsBlocked()))
			return 1.0f;
		return ResolveUiAnimValue(*pAnim, BuildUiAnimNodeKey(ScopeHash, Id), EUiAnimProperty::ALPHA, 1.0f, ui_curve::DECELERATE.m_DurationSec, EEasing::EASE_OUT_QUART);
	}

	void DrawPanel(const CUIRect &Rect, const ColorRGBA &Fill, float Radius, const ColorRGBA &Border = ui_token::color::CONTAINER_BORDER)
	{
		DrawRoundedSurface(g_pUi, Rect, Fill, Border, Radius, Border.a > 0.0f ? g_pUi->PixelSize() * 2.0f : 0.0f);
	}

	void DrawLabel(const CUIRect &Rect, const char *pText, float Size, int Align, const ColorRGBA &Color, float Alpha = 1.0f)
	{
		if(g_pUi == nullptr || g_pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		ColorRGBA Tint = Color;
		Tint.a *= std::clamp(Alpha, 0.0f, 1.0f);
		g_pTextRender->TextColor(Tint);
		g_pUi->DoLabel(&Rect, pText, Size, Align);
		g_pTextRender->TextColor(g_pTextRender->DefaultTextColor());
	}

	// 近似宽度：CJK 记 1em、ASCII 记 0.55em。只用于胶囊定宽与换行，
	// 不参与精确排版，避免每帧对每个字符都调 TextWidth。
	float ApproxTextWidth(std::string_view Text, float Size)
	{
		float Width = 0.0f;
		size_t Index = 0;
		while(Index < Text.size())
		{
			const unsigned char Byte = static_cast<unsigned char>(Text[Index]);
			if(Byte < 0x80)
			{
				Width += (Byte == ' ' ? 0.32f : 0.56f) * Size;
				++Index;
			}
			else if((Byte >> 5) == 0x06)
			{
				Width += 1.0f * Size;
				Index += 2;
			}
			else if((Byte >> 4) == 0x0E)
			{
				Width += 1.0f * Size;
				Index += 3;
			}
			else
			{
				Width += 1.0f * Size;
				Index += 4;
			}
		}
		return Width;
	}

	struct SWrappedText
	{
		// 行数不再固定：备注节点要按节点高度铺满，卡片说明要按实际行数占位。
		std::vector<std::string> m_vLines;
		int m_LineCount = 0;
	};

	// 按码位贪心折行：CJK 没有空格，按字符断行才符合中文排版。
	// Ellipsis 为真时，最后一行放不下就截断补省略号（列表/卡片说明用「看得见被截断」）；
	// 为假时按调用方给的 MaxLines 一直铺到用完为止，不补省略号（备注正文用）。
	SWrappedText WrapText(std::string_view Text, float MaxWidth, float Size, int MaxLines, bool Ellipsis = true)
	{
		SWrappedText Out;
		if(Text.empty() || MaxWidth <= 0.0f || MaxLines <= 0)
			return Out;
		// 上界只防御病态输入（空串折行、1px 宽矩形），正常调用不会碰到。
		MaxLines = std::clamp(MaxLines, 1, 64);
		Out.m_vLines.reserve(static_cast<size_t>(MaxLines));
		std::string Line;
		float LineWidth = 0.0f;
		size_t Index = 0;
		while(Index < Text.size())
		{
			const unsigned char Byte = static_cast<unsigned char>(Text[Index]);
			size_t Length = 1;
			if(Byte >= 0xF0)
				Length = 4;
			else if(Byte >= 0xE0)
				Length = 3;
			else if(Byte >= 0xC0)
				Length = 2;
			Length = std::min(Length, Text.size() - Index);
			const std::string_view Glyph = Text.substr(Index, Length);
			const float GlyphWidth = ApproxTextWidth(Glyph, Size);
			if(LineWidth + GlyphWidth > MaxWidth && !Line.empty())
			{
				if(Out.m_LineCount + 1 >= MaxLines)
				{
					if(!Ellipsis)
					{
						// 行数已用完：把当前行放进去就停，由调用方裁剪，不补省略号。
						Out.m_vLines.push_back(Line);
						Out.m_LineCount++;
						return Out;
					}
					// 最后一行也放不下：截断并加省略号。
					const float EllipsisWidth = ApproxTextWidth("...", Size);
					while(!Line.empty() && ApproxTextWidth(Line, Size) + EllipsisWidth > MaxWidth)
						Line.resize(Line.size() - 1);
					Out.m_vLines.push_back(Line + "...");
					Out.m_LineCount++;
					return Out;
				}
				Out.m_vLines.push_back(Line);
				Out.m_LineCount++;
				Line.clear();
				LineWidth = 0.0f;
				if(Glyph == " ")
				{
					++Index;
					continue;
				}
			}
			Line.append(Glyph);
			LineWidth += GlyphWidth;
			Index += Length;
		}
		if(!Line.empty() && Out.m_LineCount < MaxLines)
		{
			Out.m_vLines.push_back(Line);
			Out.m_LineCount++;
		}
		return Out;
	}

	void DrawWrapped(const CUIRect &Rect, const SWrappedText &Text, float Size, float LineHeight, int Align, const ColorRGBA &Color, float Alpha)
	{
		float Y = Rect.y;
		for(int Line = 0; Line < Text.m_LineCount && Line < (int)Text.m_vLines.size(); ++Line)
		{
			CUIRect Row = {Rect.x, Y, Rect.w, LineHeight};
			DrawLabel(Row, Text.m_vLines[Line].c_str(), Size, Align, Color, Alpha);
			Y += LineHeight;
		}
	}

	// 语义色 + 文字双通道：颜色只做加强，标签本身已经说明状态（Law 12）。
	void DrawStatusPill(const CUIRect &Rect, const char *pText, float Size, const ColorRGBA &Color, float Alpha = 1.0f)
	{
		DrawPanel(Rect, ColorRGBA(Color.r, Color.g, Color.b, 0.16f * Alpha), std::min(Rect.h * 0.5f, ui_token::radius::PILL), ColorRGBA(Color.r, Color.g, Color.b, 0.30f * Alpha));
		DrawLabel(Rect, pText, Size, TEXTALIGN_MC, Color, Alpha);
	}

	// 指针增量与 MouseX/Y 不在同一坐标系：MouseX 已换到 UI 空间，
	// MouseDelta 仍是窗口像素（ui.cpp 只对位置做了缩放）。不折算的话，
	// 高分屏下拖拽速度会正好快 PixelSize 倍（本机约 2.1×）。
	vec2 MouseDeltaUi()
	{
		if(g_pUi == nullptr)
			return vec2(0.0f, 0.0f);
		const float Ratio = g_pUi->PixelSize();
		return vec2(g_pUi->MouseDeltaX() * Ratio, g_pUi->MouseDeltaY() * Ratio);
	}

	// 分类、模式、节点类型与预设文案由数据层映射后在运行时 Localize()，
	// 提取器只扫描 Localize("字面量")，这里集中登记一次以保证语言文件收录。
	[[maybe_unused]] static void BindWorkshopStaticLocalizableKeys()
	{
		const char *const apKeys[] = {
			Localize("(no command)"),
			Localize("45 Degree Angle"),
			Localize("Action - Command"),
			Localize("Action - Echo"),
			Localize("All"),
			Localize("Announce and join team 101 with one press."),
			Localize("Apply bind"),
			Localize("Apply to key"),
			Localize("Auto hammer while firing; press again to cancel."),
			Localize("Bind applied"),
			Localize("Bind applied - undo available"),
			Localize("Bind command line too long"),
			Localize("Blueprint"),
			Localize("Built-in"),
			Localize("Camera"),
			Localize("Clamp mouse max distance for a precise 45 degree angle, restore on release."),
			Localize("Command Workshop"),
			Localize("Copied bind command"),
			Localize("Cycle in flow"),
			Localize("Double Hammer Fire"),
			Localize("Dummy"),
			Localize("Dummy Hammer (HDF)"),
			Localize("Edges"),
			Localize("Empty command"),
			Localize("Event - Press"),
			Localize("Execute once"),
			Localize("Export cfg"),
			Localize("Fire and dummy hammer alternate while the key is held."),
			Localize("Flow library"),
			Localize("Flow loaded"),
			Localize("Hold"),
			Localize("Hold to Zoom"),
			Localize("Hold to zoom out, release to restore."),
			Localize("Imported"),
			Localize("Join Team"),
			Localize("Key"),
			Localize("Key occupancy"),
			Localize("Line length"),
			Localize("Lock mouse distance at the moment of the jump."),
			Localize("Low Sensitivity Aim"),
			Localize("Missing press event"),
			Localize("Movement"),
			Localize("Multi"),
			Localize("New flow"),
			Localize("No delay: same-frame only"),
			Localize("No executable action"),
			Localize("No selection"),
			Localize("Node"),
			Localize("Nodes"),
			Localize("Note"),
			Localize("Only one press event is allowed"),
			Localize("Passed"),
			Localize("Precision Jump"),
			Localize("Quick Hammer"),
			Localize("Restored previous bind"),
			Localize("Scroll to browse"),
			Localize("Selected node"),
			Localize("Send the ready message to the team channel."),
			Localize("Show hook collision line and drop mouse sensitivity to 66 while held."),
			Localize("Single"),
			Localize("State - Toggle"),
			Localize("State - Toggle restore"),
			Localize("Status"),
			Localize("Switch Dummy"),
			Localize("Switch to hammer without changing the camera."),
			Localize("Team"),
			Localize("Team Ready"),
			Localize("Toggle"),
			Localize("Toggle between tee and dummy."),
			Localize("Too many nested binds"),
			Localize("Undo"),
			Localize("Unknown error"),
			Localize("Unreachable node"),
			Localize("Untitled flow"),
			Localize("User"),
			Localize("Utility"),
			Localize("Validate"),
			Localize("Validation"),
			Localize("Validation passed"),
			Localize("Variants"),
			Localize("Weapon"),
			Localize("Wheel to zoom - drag empty space to pan"),
			Localize("occupied")};
		(void)apKeys;
		// 蓝图页新增文案：同上，集中登记一次以保证语言文件收录。
		const char *const apBlueprintKeys[] = {
			Localize("(no note)"),
			Localize("A node cannot link to itself"),
			Localize("Command"),
			Localize("connections removed"),
			Localize("Copy"),
			Localize("Delay note"),
			Localize("Disconnect"),
			Localize("Double-click empty canvas to add a node"),
			Localize("Drop on an input port to link"),
			Localize("Duplicate"),
			Localize("Echo"),
			Localize("exec"),
			Localize("Export all"),
			Localize("Export failed"),
			Localize("Exported"),
			Localize("Install and apply"),
			Localize("Installed - the cfg file is in your config folder"),
			Localize("Needs cfg file - install writes it into your config folder"),
			Localize("Cannot install cfg: storage is not ready"),
			Localize("Cannot create the qm_bind folder in the config directory"),
			Localize("Built-in cfg is empty - nothing was installed"),
			Localize("Cannot write cfg file"),
			Localize("This preset has no built-in cfg"),
			Localize("cfg installed"),
			Localize("Sensitivity"),
			Localize("Mouse distance"),
			Localize("Presets"),
			Localize("Failed"),
			Localize("Flow title"),
			Localize("Flow loaded"),
			Localize("flows exported"),
			Localize("Link failed"),
			Localize("Link source is gone"),
			Localize("Linked"),
			Localize("Make press event"),
			Localize("Nesting depth"),
			Localize("New flow"),
			Localize("Next type"),
			Localize("No executable action"),
			Localize("Node created"),
			Localize("Node deleted"),
			Localize("Node duplicated"),
			Localize("Note"),
			Localize("Press event"),
			Localize("Run command"),
			Localize("That link would close a loop"),
			Localize("Those nodes are already linked"),
			Localize("Toggle restore"),
			Localize("Toggle state"),
			Localize("Type"),
		};
		(void)apBlueprintKeys;
		// 预设变体名、分类名与来源标签：bind_workshop.cpp 的数据层（Add 的变体标签、
		// CategoryLabel/ModeLabel/SourceLabel）只给普通字符串，运行时才交给 Localize()，
		// 提取器只扫 Localize("字面量")，所以在这登记一次保证语言文件收录译文。
		const char *const apPresetLabelKeys[] = {
			Localize("Body view"),
			Localize("Classic"),
			Localize("Connect"),
			Localize("Cursor"),
			Localize("Deep fly"),
			Localize("Default zoom toggle"),
			Localize("Defaults"),
			Localize("Dummy hammer"),
			Localize("Dummy only"),
			Localize("F5 disable"),
			Localize("F6 enable"),
			Localize("F7 super kill"),
			Localize("Flying"),
			Localize("Full site set"),
			Localize("Hide AFK"),
			Localize("Hide all"),
			Localize("Hide ninja"),
			Localize("Hint"),
			Localize("Invincible"),
			Localize("Last teleport"),
			Localize("Left"),
			Localize("Lock"),
			Localize("Main only"),
			Localize("Main view"),
			Localize("Ninja"),
			Localize("No stuck"),
			Localize("Plain"),
			Localize("Practice"),
			Localize("Rescue"),
			Localize("Reset both"),
			Localize("Reset control"),
			Localize("Reset variant"),
			Localize("Right"),
			Localize("Sensitivity only"),
			Localize("Spec"),
			Localize("Sticky"),
			Localize("Switch hammer"),
			Localize("Sync"),
			Localize("Team 0"),
			Localize("Team 101"),
			Localize("Teleport"),
			Localize("Unsolo"),
			Localize("Wheel down"),
			Localize("Wheel up"),
			Localize("With main fire"),
			Localize("Zoom 11"),
		};
		(void)apPresetLabelKeys;
	}

	// 节点头颜色由类型决定：事件/命令/echo/注释各一档，画布与卡片列表共用同一套。
	ColorRGBA NodeHeaderColor(ENodeType Type)
	{
		switch(Type)
		{
		case ENodeType::EVENT_PRESS: return workshop_color::NODE_EVENT_ACCENT;
		case ENodeType::ACTION_ECHO: return ui_token::color::WARNING;
		case ENodeType::NOTE: return workshop_color::NODE_NOTE_ACCENT;
		case ENodeType::STATE_TOGGLE:
		case ENodeType::STATE_TOGGLE_RESTORE: return ui_token::color::ACCENT_PRIMARY;
		default: return ui_token::color::SUCCESS;
		}
	}

	// 导出落盘：写到 DDNet 的保存目录（IStorage::TYPE_SAVE），
	// 成功时把完整绝对路径回填给状态栏，用户不必猜文件在哪。
	bool WriteCfgBundle(IStorage *pStorage, const SCfgBundle &Bundle, std::string &rMessage)
	{
		if(pStorage == nullptr || Bundle.m_FileName.empty() || Bundle.m_Content.empty())
			return false;
		char aFullPath[IO_MAX_PATH_LENGTH] = {};
		IOHANDLE File = pStorage->OpenFile(Bundle.m_FileName.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE, aFullPath, sizeof(aFullPath));
		if(File == nullptr)
			return false;
		const bool Written = io_write(File, Bundle.m_Content.data(), static_cast<unsigned>(Bundle.m_Content.size())) == Bundle.m_Content.size();
		io_close(File);
		if(!Written)
			return false;
		char aBuffer[IO_MAX_PATH_LENGTH + 32];
		str_format(aBuffer, sizeof(aBuffer), "%s %s", Localize("Exported"), aFullPath);
		rMessage = aBuffer;
		return true;
	}

	// 安装内置 cfg：把一个预设的全部 cfg 变体一起写到 SAVE 目录（%appdata%/DDNet
	// 这类）下的 qm_bind/ 子目录。开/关两份文件互相 exec，只装一半会把状态机
	// 指向不存在的文件，所以这里按预设整体安装。文件名稳定，已存在直接覆盖，
	// 覆盖即升级到当前版本的正文。写盘失败把可读原因交回去，卡片上直接显示，
	// 不静默变成一个按了没反应的按键。
	bool InstallPresetCfgs(IStorage *pStorage, const SPreset &Preset, std::string &rError)
	{
		if(pStorage == nullptr)
		{
			rError = Localize("Cannot install cfg: storage is not ready");
			return false;
		}
		if(!pStorage->FolderExists(CFG_FOLDER, IStorage::TYPE_SAVE))
		{
			if(!pStorage->CreateFolder(CFG_FOLDER, IStorage::TYPE_SAVE) && !pStorage->FolderExists(CFG_FOLDER, IStorage::TYPE_SAVE))
			{
				rError = Localize("Cannot create the qm_bind folder in the config directory");
				return false;
			}
		}
		size_t NumWritten = 0;
		for(size_t Variant = 0; Variant < Preset.m_vVariants.size(); ++Variant)
		{
			if(!HasBuiltinCfg(Preset, Variant))
				continue;
			const std::string Content = BuildCfgContent(Preset, Variant);
			const std::string FileName = CfgFileName(Preset, Variant);
			if(Content.empty() || FileName.empty())
			{
				rError = Localize("Built-in cfg is empty - nothing was installed");
				return false;
			}
			char aFullPath[IO_MAX_PATH_LENGTH] = {};
			IOHANDLE File = pStorage->OpenFile(FileName.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE, aFullPath, sizeof(aFullPath));
			if(File == nullptr)
			{
				char aBuffer[IO_MAX_PATH_LENGTH + 64];
				str_format(aBuffer, sizeof(aBuffer), "%s %s", Localize("Cannot write cfg file"), FileName.c_str());
				rError = aBuffer;
				return false;
			}
			const bool Written = io_write(File, Content.data(), static_cast<unsigned>(Content.size())) == Content.size();
			io_close(File);
			if(!Written)
			{
				char aBuffer[IO_MAX_PATH_LENGTH + 64];
				str_format(aBuffer, sizeof(aBuffer), "%s %s", Localize("Cannot write cfg file"), FileName.c_str());
				rError = aBuffer;
				return false;
			}
			++NumWritten;
		}
		if(NumWritten == 0)
		{
			rError = Localize("This preset has no built-in cfg");
			return false;
		}
		return true;
	}

	enum class EStepperPress
	{
		NONE,
		MINUS,
		PLUS,
	};

	// 数值步进器骨架：左边标签、右边一组「[-] 值 [+]」，整组贴右，行高与其它行一致。
	// 这里只画、只报「按了哪边」；夹紧与步进留在纯逻辑层（参数走 StepParamValue，
	// 几何字段走 StepNodeGeometry），两条路径共用同一套外观。
	EStepperPress DoValueStepper(const IUiContext &Ctx, const CUIRect &Rect, CButtonContainer *pMinusButton,
		CButtonContainer *pPlusButton, const char *pLabel, const char *pValue, float FontSize, bool Disabled)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return EStepperPress::NONE;
		DrawPanel(Rect, ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::TIGHT);
		// 一行三段：左边标签吃剩余宽度，右边是「[−][数值][+]」一整组，贴右不居中。
		// 组宽固定（两个方形按钮 + 数字位），所以减/加按钮永远在同一列，
		// 不会因为标签长短而左右跳。
		const float Button = std::max(18.0f, Rect.h);
		const float ValueWidth = std::max(Button * 1.5f, ApproxTextWidth("-0000", FontSize) + ui_token::spacing::SM);
		const float GroupWidth = std::min(Rect.w, Button * 2.0f + ValueWidth);
		CUIRect Label, Group, Minus, Value, Plus;
		CUIRect Row = Rect;
		Row.VSplitRight(GroupWidth, &Label, &Group);
		const float LabelGap = std::min(ui_token::spacing::SM, Label.w * 0.5f);
		Label.VSplitRight(LabelGap, &Label, nullptr);
		Group.VSplitLeft(Button, &Minus, &Group);
		Group.VSplitRight(Button, &Value, &Plus);
		// 标签框自动换行会把按钮组挤歪，这里只画一行并裁到标签格内，绝不压到按钮上。
		if(g_pUi != nullptr)
			g_pUi->ClipEnable(&Label);
		DrawLabel(Label, pLabel, FontSize, TEXTALIGN_ML, ui_token::color::TEXT_TIP);
		if(g_pUi != nullptr)
			g_pUi->ClipDisable();
		const bool MinusPressed = ui_widget::SecondaryButton(Ctx, pMinusButton, "-", Minus, Disabled);
		const bool PlusPressed = ui_widget::SecondaryButton(Ctx, pPlusButton, "+", Plus, Disabled);
		DrawLabel(Value, pValue, FontSize, TEXTALIGN_MC, ui_token::color::TEXT_PRIMARY);
		if(Disabled)
			return EStepperPress::NONE;
		if(MinusPressed)
			return EStepperPress::MINUS;
		if(PlusPressed)
			return EStepperPress::PLUS;
		return EStepperPress::NONE;
	}

	// 预设参数步进器：外观复用上面的骨架，夹紧/步进取 StepParamValue。
	bool DoParamStepper(const IUiContext &Ctx, const CUIRect &Rect, CButtonContainer *pMinusButton,
		CButtonContainer *pPlusButton, const SPresetParam &Param, const char *pLabel, int *pValue,
		float FontSize, bool Disabled)
	{
		if(pValue == nullptr)
			return false;
		char aValue[32];
		str_format(aValue, sizeof(aValue), "%d", *pValue);
		const EStepperPress Press = DoValueStepper(Ctx, Rect, pMinusButton, pPlusButton, pLabel, aValue, FontSize, Disabled);
		if(Press == EStepperPress::MINUS)
			*pValue = StepParamValue(Param, *pValue, -1);
		if(Press == EStepperPress::PLUS)
			*pValue = StepParamValue(Param, *pValue, 1);
		return Press != EStepperPress::NONE;
	}

	void SetStatus(std::string &rStatus, double &rTime, IClient *pClient, const char *pMessage)
	{
		rStatus = pMessage != nullptr ? pMessage : "";
		rTime = pClient->LocalTime();
	}
}

// ---------------------------------------------------------------------------
// 指令工坊
// ---------------------------------------------------------------------------
void CMenus::RenderSettingsBindWorkshop(CUIRect MainView, const bool PrewarmOnly)
{
	g_pUi = Ui();
	g_pTextRender = TextRender();
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float Scale = Metrics.m_UiScale;
	IUiContext Ctx = SettingsUiContext("settings_bind_workshop", Scale);
	const uint64_t Scope = MakeUiScopeHash("settings_bind_workshop");

	// 全部尺寸只从两个标尺取：ui_token::spacing（间距）与 ui_token::font（字号）。
	const float CardGap = ui_token::spacing::LG * Scale;
	const float Pad = ui_token::settings::CARD_PADDING * Scale;
	const float HeadlineSize = ui_token::font::HEADLINE * Scale;
	const float BodySize = ui_token::font::BODY * Scale;
	const float SmallSize = ui_token::font::SMALL * Scale;
	const float TipSize = ui_token::font::TIP * Scale;
	const float SmallLine = SmallSize * 1.45f;

	SWorkshopState &State = WorkshopState();
	const std::vector<SPreset> &vPresets = BuiltinPresets();
	const bool Enabled = !ReadOnly && !State.m_ConfirmOpen && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive() && !GameClient()->m_GameConsole.IsActive();

	// 首次进入时探测一遍 cfg 是否已经装过：装过的卡片直接显示 Apply，没装过的
	// 显示 Install and apply。只探测一次，之后由安装动作自己置位，不对每帧做 I/O。
	if(!State.m_CfgProbed)
	{
		State.m_CfgProbed = true;
		IStorage *pStorage = Storage();
		for(size_t Index = 0; Index < vPresets.size() && Index < MAX_PRESETS; ++Index)
		{
			for(size_t Variant = 0; Variant < vPresets[Index].m_vVariants.size(); ++Variant)
			{
				if(!HasBuiltinCfg(vPresets[Index], Variant))
					continue;
				const std::string FileName = CfgFileName(vPresets[Index], Variant);
				if(pStorage != nullptr && !FileName.empty() && pStorage->FileExists(FileName.c_str(), IStorage::TYPE_SAVE))
					State.m_aCfgInstalled[Index] |= (1U << Variant);
			}
		}
	}

	// 版式：工具条 / 网格 / 页脚各占固定高度，网格吃掉剩余空间并自己滚动。
	const SWorkshopLayoutFrame Layout = ResolveWorkshopLayout(MainView, Scale);
	const CUIRect &Toolbar = Layout.m_Toolbar;
	const CUIRect &BodyArea = Layout.m_Body;
	const CUIRect &Footer = Layout.m_Footer;

	// ---- 工具条：分类胶囊（左） + 搜索（右，定宽） ----
	// 分类胶囊统一走设置页的多选一胶囊组件（与顶部「控制 / 绑定 / 指令工坊 / 蓝图」
	// 同一套轨道、滑块与文字色），槽位在工具条内均分，不再自绘轨道和横向滚动。
	CUIRect Search, Chips;
	Toolbar.VSplitRight(220.0f * Scale, &Chips, &Search);
	Chips.VSplitRight(ui_token::spacing::SM * Scale, &Chips, nullptr);
	if(State.m_Undo.CanUndo())
	{
		CUIRect Undo;
		Chips.VSplitRight(78.0f * Scale, &Chips, &Undo);
		Chips.VSplitRight(ui_token::spacing::SM * Scale, &Chips, nullptr);
		if(ui_widget::SecondaryButton(Ctx, &State.m_UndoButton, Localize("Undo"), Undo, !Enabled))
		{
			CBinds &Binds = GameClient()->m_Binds;
			// 整批还原：一组动作写过几条就还几条，原来没绑的键重新解绑（空命令即解绑）。
			// 执行细节走纯逻辑层的 RestoreUndo，行为与单元测试共用同一份实现。
			RestoreUndo(State.m_Undo, [&Binds](const CBindSlot &Slot, const char *pCommand) { Binds.Bind(Slot.m_Key, pCommand, false, Slot.m_ModifierMask); });
			State.m_Undo.Clear();
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Restored previous bind"));
		}
	}

	{
		CUIRect Strip = Chips;
		Strip.y += (Strip.h - WORKSHOP_CHIP_HEIGHT * Scale) * 0.5f;
		Strip.h = WORKSHOP_CHIP_HEIGHT * Scale;

		// 数据：只有 0 条的分类会被跳过，ALL 永远在（它就是「不过滤」这个动作）。
		constexpr size_t MAX_CHIPS = static_cast<size_t>(ECategory::NUM);
		std::array<ECategory, MAX_CHIPS> aChipValues{};
		std::array<std::string, MAX_CHIPS> aChipLabels{};
		std::array<const char *, MAX_CHIPS> apChipTexts{};
		int NumChips = 0;
		for(int Category = 0; Category < static_cast<int>(ECategory::NUM); ++Category)
		{
			const ECategory Value = static_cast<ECategory>(Category);
			const size_t Count = CountCategory(vPresets, Value);
			if(Value != ECategory::ALL && Count == 0)
				continue;
			char aLabel[96];
			str_format(aLabel, sizeof(aLabel), "%s %zu", Localize(CategoryLabel(Value)), Count);
			aChipLabels[NumChips] = aLabel;
			aChipValues[NumChips] = Value;
			++NumChips;
		}
		for(int Index = 0; Index < NumChips; ++Index)
			apChipTexts[Index] = aChipLabels[Index].c_str();

		// 按钮容器以地址作 UI ID，必须是静态的。
		static CButtonContainer s_aCategoryButtons[MAX_CHIPS];
		int AllSlot = 0;
		int CurrentSlot = 0;
		for(int Index = 0; Index < NumChips; ++Index)
		{
			if(aChipValues[Index] == ECategory::ALL)
				AllSlot = Index;
			if(aChipValues[Index] == State.m_Category)
				CurrentSlot = Index;
		}
		// 搜索跨分类：有查询串时分类不参与过滤，滑块临时停在 ALL 上。
		const bool Searching = State.m_Filter.GetString()[0] != '\0';
		const int HighlightSlot = Searching ? AllSlot : CurrentSlot;
		const int PickedSlot = GameClient()->m_Menus.DoSegmentedChoice(s_aCategoryButtons, apChipTexts.data(), NumChips, HighlightSlot, Strip, ui_token::radius::BASE, nullptr, SmallSize);
		// 点分类就是明确要按分类看：顺带清掉查询串，否则搜索仍然跨分类、点了像没反应。
		if(Enabled && PickedSlot >= 0 && PickedSlot < NumChips && (Searching || PickedSlot != CurrentSlot))
		{
			State.m_Category = aChipValues[PickedSlot];
			if(Searching)
				State.m_Filter.Set("");
		}
	}

	// 搜索框与工具条同高，并且**每一趟渲染都画同一个组件**：只把「是否接收输入」
	// 交给 m_ProcessInput。以前只读/预览趟换成自绘面板，同一个 CLineInput 在同帧里
	// 会从「已渲染」变回「没渲染」，渲染校验会把它当离屏输入收掉，点一下焦点就没了。
	CUIRect SearchField = {Search.x, Search.y + (Search.h - WORKSHOP_CHIP_HEIGHT * Scale) * 0.5f, Search.w, WORKSHOP_CHIP_HEIGHT * Scale};
	{
		// 输入框单独一个 IUiContext：焦点环与清除按钮的动画节点不与页面其它控件混在一条轨道上。
		IUiContext SearchCtx = SettingsUiContext("settings_bind_workshop_search", Scale);
		ui_widget::SInputFieldOptions SearchOptions;
		SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		SearchOptions.m_Clearable = true;
		SearchOptions.m_SearchHotkeyEnabled = Enabled;
		SearchOptions.m_ProcessInput = Enabled;
		SearchOptions.m_pPlaceholder = Localize("Search presets");
		SearchOptions.m_FontSize = SmallSize;
		const ui_widget::SInputFieldResult SearchResult = ui_widget::InputField(SearchCtx, &State.m_Filter, SearchField, SearchOptions);
		if(SearchResult.m_Changed || SearchResult.m_Cleared)
			State.m_ScrollRegion.Reset();
		// Esc 只退出输入态：正在打字时 Esc 不该顺手把整个设置页关掉；
		// 回车确认同样交还焦点。两者都要在这里消费掉，否则会被页面层的全局热键接走
		//（Esc 关菜单、回车去按某个顺带获得焦点的按钮）。
		if(Enabled && State.m_Filter.IsActive())
		{
			const bool EnterPressed = Ui()->HotkeyPressed(CUi::HOTKEY_ENTER);
			const bool EscPressed = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
			if(EscPressed || EnterPressed)
			{
				if(EnterPressed)
					Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
				State.m_Filter.Deactivate();
				Ui()->SetActiveItem(nullptr);
			}
		}
	}

	// ---- 过滤结果 + 滚动位置 ----
	const std::vector<size_t> vFiltered = FilterPresets(vPresets, State.m_Category, State.m_Filter.GetString(), &LocalizeWrapper);
	uint64_t Signature = 1469598103934665603ULL;
	for(const char *p = State.m_Filter.GetString(); p != nullptr && *p != '\0'; ++p)
		Signature = (Signature ^ static_cast<unsigned char>(*p)) * 1099511628211ULL;
	Signature ^= static_cast<uint64_t>(State.m_Category) * 1099511628211ULL;
	if(Signature != State.m_ScrollSignature)
	{
		// 换分类或改查询串就回到顶部：沿用旧偏移会落在空白处。
		State.m_ScrollRegion.Reset();
		State.m_ScrollSignature = Signature;
	}

	// ---- 滚动：与设置页列表同一套（SETTINGS_OUTER 策略 → CScrollRegion） ----
	// 同一份滚轮档位（qm_ui_scroll_step × QmPanelWheelStepMultiplier）、同一档平滑
	// 时长（qm_ui_smooth_scroll_time）、同一个强调色滑块、同一套滚轮归属抢占（一帧
	// 只会被一个滚动区消费）。以前这里走 ui_widget::ScrollContainer：它直接读原始
	// 滚轮键，预热趟与渲染趟各加一次冲量、动画也各推进一次，一格滚轮因此跑出双倍
	// 距离，滑块和内容还各按各的节奏走 —— 用户说的「反过来」「错位」「灰白滑块」
	// 都出在这套与设置页不同的第二套滚动状态里。
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, Scale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	// 测量趟只量不滚：暖机与正式渲染共用一份状态，等于每帧把动画走两遍。
	const bool ScrollActive = !Ui()->MeasurementOnly();
	CUIRect BodyContent = BodyArea;
	if(ScrollActive)
	{
		vec2 ScrollOffset;
		State.m_ScrollRegion.Begin(&BodyContent, &ScrollOffset, &ScrollParams);
		BodyContent.y += ScrollOffset.y;
	}

	// 列数按可用宽度推：卡片再窄也要放得下「模式徽标 + 分段方案 + 应用按钮」，
	// 上限保持 3 列，窗口收窄先掉到 2 列、再掉到 1 列。
	constexpr float MIN_CARD_WIDTH = 232.0f;
	const int NumColumns = std::clamp(static_cast<int>((BodyContent.w + CardGap) / (MIN_CARD_WIDTH * Scale + CardGap)), 1, WORKSHOP_COLUMNS);
	const float CardWidth = std::max(1.0f, (BodyContent.w - static_cast<float>(NumColumns - 1) * CardGap) / static_cast<float>(NumColumns));

	// 卡片高度按内容量：说明行数、cfg 提示行、命令框行数、参数行数每张都不一样。
	// 高度写死会让短卡片下面空一大块、长卡片把命令框挤没，也没法错落堆叠。
	// 卡片上「真正会写进去的那条命令」：cfg 变体是 exec 的相对路径，参数变体是替换后的
	// 模板。测量与绘制都走它，用户看到的和确认框里列的永远是同一条。
	const auto CardCommand = [&State](const SPreset &Preset, size_t PresetIndex, size_t VariantIndex) {
		if(VariantRequiresCfg(Preset.m_vVariants[VariantIndex]))
			return CfgExecCommand(Preset, VariantIndex);
		const size_t NumParams = std::min(Preset.m_vParams.size(), MAX_PRESET_PARAMS);
		return ResolveVariantCommand(Preset, VariantIndex, State.m_aaParamValues[PresetIndex].data(), NumParams);
	};

	const auto MeasureCard = [&](const SPreset &Preset, size_t PresetIndex) -> SWorkshopCardMetrics {
		SWorkshopCardMetrics CardMetrics;
		const float InnerWidth = std::max(1.0f, CardWidth - Pad * 2.0f);
		if(Preset.m_vVariants.empty())
		{
			CardMetrics.m_Height = WORKSHOP_CARD_HEIGHT * Scale;
			return CardMetrics;
		}
		const size_t VariantIndex = std::min(State.m_aVariant[PresetIndex], Preset.m_vVariants.size() - 1);
		const SVariant &Variant = Preset.m_vVariants[VariantIndex];
		const SWrappedText Desc = WrapText(Localize(Preset.m_pDescription), InnerWidth, SmallSize, 3);
		CardMetrics.m_DescLines = std::max(1, Desc.m_LineCount);
		CardMetrics.m_DescHeight = static_cast<float>(CardMetrics.m_DescLines) * SmallLine;
		const size_t KeyRows = PresetKeyRows(Preset);
		bool NeedsCfg = false;
		for(size_t Row = 0; Row < KeyRows; ++Row)
			NeedsCfg = NeedsCfg || VariantRequiresCfg(Preset.m_vVariants[VariantForRow(Preset, VariantIndex, Row)]);
		CardMetrics.m_TipHeight = NeedsCfg ? TipSize * 1.5f + ui_token::spacing::XS * Scale : 0.0f;
		const size_t NumParams = std::min(Preset.m_vParams.size(), MAX_PRESET_PARAMS);
		// 命令框：一组动作每个变体各占一行（会写出几条就列几条），互斥方案只显示选中的那版。
		// 行数与绘制处用同一份 WrapText，量出多高就占多高。
		const float CodeWidth = std::max(1.0f, InnerWidth - ui_token::spacing::SM * 2.0f * Scale);
		int CodeLines = 0;
		if(KeyRows > 1)
		{
			for(size_t Row = 0; Row < KeyRows; ++Row)
				CodeLines += std::max(1, WrapText(CardCommand(Preset, PresetIndex, VariantForRow(Preset, VariantIndex, Row)), CodeWidth, SmallSize, 1).m_LineCount);
		}
		else
			CodeLines = WrapText(CardCommand(Preset, PresetIndex, VariantIndex), CodeWidth, SmallSize, NeedsCfg ? 1 : 2).m_LineCount;
		CardMetrics.m_CodeLines = CodeLines;
		CardMetrics.m_CodeHeight = CodeLines > 0 ? static_cast<float>(CodeLines) * SmallLine + ui_token::spacing::SM * 2.0f * Scale : 0.0f;
		// 底栏纵向只问版式层：参数行、模式行、键槽行、应用行由同一份度量算出来，
		// 测量和真正画出来的底栏就不会各说各话。
		const SWorkshopFooterMetrics FooterMetrics = ResolveWorkshopFooterMetrics(InnerWidth, NumParams, KeyRows, Scale);
		const float TopHeight = HeadlineSize + ui_token::spacing::XS * 2.0f * Scale + CardMetrics.m_DescHeight + ui_token::spacing::SM * Scale;
		const float BottomHeight = CardMetrics.m_TipHeight + CardMetrics.m_CodeHeight + ui_token::spacing::SM * Scale + FooterMetrics.Height();
		CardMetrics.m_Height = std::max(WORKSHOP_CARD_HEIGHT * 0.5f * Scale, TopHeight + BottomHeight + Pad * 2.0f);
		return CardMetrics;
	};

	// 量一次缓存一次：只有列宽、缩放、变体、参数或 cfg 安装状态变了才重新量。
	uint64_t CardMetricsKey = 1469598103934665603ULL;
	const auto MixMetricsKey = [&CardMetricsKey](uint64_t Value) {
		CardMetricsKey = (CardMetricsKey ^ Value) * 1099511628211ULL;
	};
	MixMetricsKey(static_cast<uint64_t>(CardWidth * 8.0f));
	MixMetricsKey(static_cast<uint64_t>(Scale * 1024.0f));
	for(size_t KeyIndex = 0; KeyIndex < vPresets.size() && KeyIndex < MAX_PRESETS; ++KeyIndex)
	{
		MixMetricsKey(State.m_aVariant[KeyIndex]);
		MixMetricsKey(State.m_aCfgInstalled[KeyIndex]);
		for(size_t Param = 0; Param < MAX_PRESET_PARAMS; ++Param)
			MixMetricsKey(static_cast<uint64_t>(State.m_aaParamValues[KeyIndex][Param] + 4096));
	}
	if(CardMetricsKey != State.m_CardMetricsKey)
	{
		State.m_CardMetricsKey = CardMetricsKey;
		for(size_t PresetIndex = 0; PresetIndex < vPresets.size() && PresetIndex < MAX_PRESETS; ++PresetIndex)
			State.m_aCardMetrics[PresetIndex] = MeasureCard(vPresets[PresetIndex], PresetIndex);
	}

	if(vFiltered.empty())
	{
		DrawLabel(BodyArea, Localize("No matching presets"), BodySize, TEXTALIGN_MC, ui_token::color::TEXT_SECONDARY);
	}
	else
	{
		// 瀑布流：每张卡放进当前最矮的那一列，从该列底部接着往下堆。卡片高度参差，
		// 所以按列推进 + 逐卡判可见，不再按行 break/continue。
		std::array<float, WORKSHOP_COLUMNS> aColumnBottom{};
		for(size_t Index = 0; Index < vFiltered.size(); ++Index)
		{
			const size_t PresetIndex = vFiltered[Index];
			const SPreset &Preset = vPresets[PresetIndex];
			// 没有变体的预设整张卡都不画：先跳过再登记滚动区，瀑布流里不会留一块空位。
			if(Preset.m_vVariants.empty())
				continue;
			const SWorkshopCardMetrics &CardMetrics = State.m_aCardMetrics[PresetIndex];
			size_t Column = 0;
			for(int Candidate = 1; Candidate < NumColumns; ++Candidate)
			{
				if(aColumnBottom[Candidate] < aColumnBottom[Column] - 0.5f)
					Column = static_cast<size_t>(Candidate);
			}
			CUIRect Card = {BodyContent.x + static_cast<float>(Column) * (CardWidth + CardGap), BodyContent.y + aColumnBottom[Column], CardWidth, CardMetrics.m_Height};
			aColumnBottom[Column] += CardMetrics.m_Height + CardGap;
			// 登记即裁剪：AddRect 同时累积滚动区内容高度并回答「这张卡在不在视口里」，
			// 不可见的卡片不进绘制，也不吃点击。
			if(ScrollActive && !State.m_ScrollRegion.AddRect(Card))
				continue;
			const size_t Variant = std::min(State.m_aVariant[PresetIndex], Preset.m_vVariants.size() - 1);
			const SVariant &SelectedVariant = Preset.m_vVariants[Variant];

			const uint64_t CardKey = (static_cast<uint64_t>(State.m_Category) << 40) ^ (PresetIndex + 1ULL);
			const float Alpha = ResolveAlpha(Ctx, Scope, CardKey);
			// 「已应用」按整批撤销记录判断：一组动作写过几个键都算，不是只看第一个。
			const size_t KeyRows = PresetKeyRows(Preset);
			const std::vector<CBindSlot> &vKeySlots = State.m_aaSKeySlots[PresetIndex];
			bool Applied = false;
			for(size_t Row = 0; Row < KeyRows && Row < vKeySlots.size(); ++Row)
				Applied = Applied || State.m_Undo.Covers(vKeySlots[Row]);

			// 卡片外框直接用按钮组件的表面管线：同一个主题面、同一条描边宽度
			// （SButtonSurfaceOptions 的默认 m_BorderWidth）、同一档悬停/选中边框。
			// 以前自己画一条 1px 淡边，和按钮、开关摆在一起时框细得几乎看不出来。
			ui_widget::SButtonSurfaceOptions CardSurface;
			CardSurface.m_Role = EUiButtonRole::SECONDARY;
			CardSurface.m_Radius = ui_token::radius::CARD * Scale;
			CardSurface.m_Selected = Applied;
			CardSurface.m_pHitRect = &Card;
			{
				// 卡片整体淡入淡出也跟着表面走，否则会先亮出一整块再淡出文字。
				CUiScopedControlAlphaScale CardFade(Ctx.m_pUi, std::clamp(Alpha, 0.0f, 1.0f));
				ui_widget::DrawButtonSurface(Ctx, &State.m_aCardSurfaceIds[PresetIndex], Card, CardSurface);
			}

			CUIRect Inner = Card;
			Inner.VMargin(Pad, &Inner);
			Inner.HMargin(Pad, &Inner);
			CUIRect Title, Description;
			Inner.HSplitTop(HeadlineSize + ui_token::spacing::XS * Scale, &Title, &Inner);
			Inner.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Inner);
			// 说明先按实际行数测量再切高度：以前固定占两行，一行说明下面会空出
			// 一整行，和命令框之间就是一大块空白。行数算出来后间距只留 SM 一档。
			const SWrappedText Desc = WrapText(Localize(Preset.m_pDescription), Inner.w, SmallSize, 3);
			const int DescLines = std::max(1, Desc.m_LineCount);
			Inner.HSplitTop(static_cast<float>(DescLines) * SmallLine, &Description, &Inner);
			Inner.HSplitTop(ui_token::spacing::SM * Scale, nullptr, &Inner);

			// 会写出去的每个变体是否依赖 cfg、那些 cfg 是否都已落盘，决定主按钮的文案与动作：
			// 一组动作是整套，缺一份就不能只应用一半。
			bool NeedsCfg = false;
			bool Installed = true;
			for(size_t Row = 0; Row < KeyRows; ++Row)
			{
				const size_t RowVariant = VariantForRow(Preset, Variant, Row);
				const bool RowNeedsCfg = VariantRequiresCfg(Preset.m_vVariants[RowVariant]);
				NeedsCfg = NeedsCfg || RowNeedsCfg;
				if(RowNeedsCfg && (State.m_aCfgInstalled[PresetIndex] & (1U << RowVariant)) == 0)
					Installed = false;
			}
			const size_t NumParams = std::min(Preset.m_vParams.size(), MAX_PRESET_PARAMS);
			int *pParamValues = State.m_aaParamValues[PresetIndex].data();

			// 一组动作没有「选哪版」这回事：每个变体都会写出去，所以徽标改说「多动作 N」，
			// 变体胶囊不出现，底下改成每个变体一行键槽。
			const bool Grouped = KeyRows > 1;
			const EMode Mode = InferMode(SelectedVariant.m_Command);
			char aGroupedMode[64] = {};
			const char *pMode = nullptr;
			ColorRGBA ModeColor;
			if(Grouped)
			{
				str_format(aGroupedMode, sizeof(aGroupedMode), "%s %zu", Localize("Multi"), KeyRows);
				pMode = aGroupedMode;
				ModeColor = ui_token::color::ACCENT_PRIMARY;
			}
			else
			{
				pMode = Localize(ModeLabel(Mode));
				ModeColor = Mode == EMode::HOLD ? ui_token::color::WARNING : Mode == EMode::TOGGLE ? ui_token::color::SUCCESS :
														     ui_token::color::ACCENT_PRIMARY;
			}
			const float ModeWidth = ApproxTextWidth(pMode, TipSize) + ui_token::spacing::MD * 2.0f * Scale;
			// 每行左侧的变体名列宽按最长的那一个量，布局层还会再按行宽夹一次。
			float KeyLabelWidth = 0.0f;
			if(Grouped)
			{
				for(size_t Row = 0; Row < KeyRows; ++Row)
					KeyLabelWidth = std::max(KeyLabelWidth, ApproxTextWidth(Localize(Preset.m_vVariants[VariantForRow(Preset, Variant, Row)].m_Label.c_str()), TipSize) + ui_token::spacing::SM * Scale);
			}
			char aVariant[96] = {};
			float VariantWidth = 0.0f;
			if(!Grouped && Preset.m_vVariants.size() > 1)
			{
				str_format(aVariant, sizeof(aVariant), "%s %zu/%zu", Localize("Variants"), Variant + 1, Preset.m_vVariants.size());
				VariantWidth = ApproxTextWidth(aVariant, TipSize) + ui_token::spacing::MD * 2.0f * Scale;
				// 段宽先按「最长的方案名 × 段数」要一次：只写序号的分段控件，
				// 点之前看不出是哪一版。版式层还会按行内可用宽度再夹一次，真放不下才退回序号。
				float WidestLabel = 0.0f;
				for(const SVariant &CandidateVariant : Preset.m_vVariants)
					WidestLabel = std::max(WidestLabel, ApproxTextWidth(Localize(CandidateVariant.m_Label.c_str()), TipSize) + 12.0f * Scale);
				VariantWidth = std::max(VariantWidth, WidestLabel * (float)Preset.m_vVariants.size());
			}
			const char *pApplyText = (NeedsCfg && !Installed) ? Localize("Install and apply") : Localize("Apply");
			// 按钮里的字由组件按 ui_token::font::BODY 画，宽度就按同一个字号量：以前用
			// SmallSize 量，按钮比自己的标签还窄，字只能被压扁或加省略号。
			const float ApplyWidth = ApproxTextWidth(pApplyText, ui_token::font::BODY) + ui_token::spacing::MD * 2.0f * Scale;

			// 底栏（模式行 + 可选步进器 + 键槽行 + 应用行）由版式层切，UI 只往里填内容。
			// 一组动作把「一行一个键槽」扩成「每个变体一行」，卡片高度由测量函数跟着算。
			SWorkshopCardFooterRequest FootRequest;
			FootRequest.m_UiScale = Scale;
			FootRequest.m_ModeBadgeWidth = ModeWidth;
			FootRequest.m_VariantWidth = VariantWidth;
			FootRequest.m_ApplyWidth = ApplyWidth;
			FootRequest.m_NumSteppers = NumParams;
			FootRequest.m_NumKeyRows = KeyRows;
			FootRequest.m_KeyLabelWidth = KeyLabelWidth;
			const SWorkshopCardFooter Footer = ResolveWorkshopCardFooter(Inner, FootRequest);

			DrawLabel(Title, Localize(Preset.m_pTitle), HeadlineSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY, Alpha);
			DrawWrapped(Description, Desc, SmallSize, SmallLine, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY, Alpha);

			// 命令框：把这一组动作真正会写进 cfg 的命令按行列出来。
			CUIRect CodeArea = Footer.m_CodeArea;
			if(NeedsCfg)
			{
				CUIRect TipRect;
				CodeArea.HSplitTop(TipSize * 1.5f, &TipRect, &CodeArea);
				CodeArea.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &CodeArea);
				const std::string &Error = State.m_aCfgError[PresetIndex];
				if(!Error.empty())
					DrawLabel(TipRect, Error.c_str(), TipSize, TEXTALIGN_ML, ui_token::color::WARNING, Alpha);
				else
					DrawLabel(TipRect, Installed ? Localize("Installed - the cfg file is in your config folder") : Localize("Needs cfg file - install writes it into your config folder"),
						TipSize, TEXTALIGN_ML, Installed ? ui_token::color::SUCCESS : ui_token::color::WARNING, Alpha);
			}
			if(CodeArea.h > 0.0f)
			{
				// 圆角与卡片外层同档（radius::CARD）：代码框是卡片里的一个面，不是贴片。
				DrawPanel(CodeArea, ui_token::color::PREVIEW_BACKDROP.WithAlpha(0.28f * Alpha), ui_token::radius::CARD * Scale, ui_token::color::SURFACE_HIGHLIGHT);
				CUIRect CodeText = CodeArea;
				CodeText.VMargin(ui_token::spacing::SM * Scale, &CodeText);
				CodeText.HMargin(ui_token::spacing::SM * Scale, &CodeText);
				const ColorRGBA CodeColor = ui_token::color::ADAPTIVE_ON_DARK;
				if(Grouped)
				{
					float LineY = CodeText.y;
					for(size_t Row = 0; Row < KeyRows && LineY + SmallLine <= CodeText.y + CodeText.h + 0.5f; ++Row)
					{
						const SWrappedText Line = WrapText(CardCommand(Preset, PresetIndex, VariantForRow(Preset, Variant, Row)), CodeText.w, SmallSize, 1);
						DrawWrapped(CUIRect{CodeText.x, LineY, CodeText.w, SmallLine}, Line, SmallSize, SmallLine, TEXTALIGN_ML, CodeColor, Alpha);
						LineY += SmallLine;
					}
				}
				else
				{
					const SWrappedText Code = WrapText(CardCommand(Preset, PresetIndex, Variant), CodeText.w, SmallSize, NeedsCfg ? 1 : 2);
					DrawWrapped(CodeText, Code, SmallSize, SmallLine, TEXTALIGN_ML, CodeColor, Alpha);
				}
			}

			// 第一行：[模式徽标][变体][来源]。三者都比标题低一档，不抢视线；
			// 一组动作没有「选哪版」这回事，所以这一行只有徽标和来源。
			DrawStatusPill(Footer.m_ModeBadge, pMode, TipSize, ModeColor, Alpha);
			DrawLabel(Footer.m_SourceLabel, Localize(SourceLabel(Preset.m_Source)), TipSize, TEXTALIGN_MR, ui_token::color::TEXT_TIP, Alpha);
			if(VariantWidth > 0.0f && Footer.m_VariantButton.w > 0.0f)
			{
				// 方案是「同一张卡片的第几张」，用胶囊页签组件的轨道 + 滑块 + 命中表达：
				// 段数 = 方案数，按哪段就切到哪版。组件只负责盒子与选中态，文字仍在这里画 ——
				// 段里优先写方案名，放不下才退回序号，否则用户点之前不知道每段是什么。
				CUIRect Slider = Footer.m_VariantButton;
				constexpr size_t MAX_VARIANT_SEGMENTS = 8;
				const size_t VariantCount = std::clamp<size_t>(Preset.m_vVariants.size(), 1, MAX_VARIANT_SEGMENTS);
				const float SegmentWidth = Slider.w / (float)VariantCount;
				std::array<CUIRect, MAX_VARIANT_SEGMENTS> aSegmentRects{};
				for(size_t Segment = 0; Segment < VariantCount; ++Segment)
					aSegmentRects[Segment] = CUIRect{Slider.x + SegmentWidth * (float)Segment, Slider.y, SegmentWidth, Slider.h};

				// 卡片可能整体淡出（筛选/禁用/读模式），组件配色跟着 Alpha 走，
				// 否则会在淡出的卡片上亮一块。
				ui_widget::SCapsuleTabBarStyle Style = GameClient()->m_Menus.SettingsCapsuleTabBarStyle();
				Style.m_CapsuleColor = Style.m_CapsuleColor.WithAlpha(Style.m_CapsuleColor.a * Alpha);
				Style.m_IndicatorColor = Style.m_IndicatorColor.WithAlpha(Style.m_IndicatorColor.a * Alpha);
				Style.m_ActiveLabelColor = Style.m_ActiveLabelColor.WithAlpha(Style.m_ActiveLabelColor.a * Alpha);
				Style.m_InactiveLabelColor = Style.m_InactiveLabelColor.WithAlpha(Style.m_InactiveLabelColor.a * Alpha);
				const int ActiveSegment = (int)std::min<size_t>(Variant, VariantCount - 1);
				// 每张卡片一个独立动画组：共用组号会让滑块从上一张卡片的方向滑过来。
				const uint64_t CapsuleGroup = BuildUiAnimNodeKey(MakeUiScopeHash("settings_bind_workshop_variant_capsule"), (uint64_t)PresetIndex);
				ui_widget::CapsuleTabBarChrome(Ctx, CapsuleGroup, aSegmentRects.data(), (int)VariantCount, ActiveSegment, Style);

				for(size_t Segment = 0; Segment < VariantCount; ++Segment)
				{
					// 段里只写方案名。退回序号后每段都只剩「1/2/3」，点之前完全看不出是哪一版
					// （三格水跳的分段就这么出现过「1」）。名字为空就不写字，放不下就降一档
					// 字号，绝不换成看不懂的数字。
					const std::string &VariantLabel = Segment < Preset.m_vVariants.size() ? Preset.m_vVariants[Segment].m_Label : std::string();
					if(VariantLabel.empty())
						continue;
					const char *pSegmentLabel = Localize(VariantLabel.c_str());
					const float SegmentPadding = 10.0f * Scale;
					float SegmentFont = TipSize;
					const float LabelWidth = ApproxTextWidth(pSegmentLabel, SegmentFont);
					if(LabelWidth + SegmentPadding > SegmentWidth)
					{
						const float Available = std::max(1.0f, SegmentWidth - SegmentPadding);
						SegmentFont = std::max(TipSize * 0.7f, TipSize * std::min(1.0f, Available / std::max(1.0f, LabelWidth)));
					}
					if(ApproxTextWidth(pSegmentLabel, SegmentFont) + 4.0f * Scale > SegmentWidth)
						continue;
					DrawLabel(aSegmentRects[Segment], pSegmentLabel, SegmentFont, TEXTALIGN_MC,
						Segment == (size_t)ActiveSegment ? Style.m_ActiveLabelColor : Style.m_InactiveLabelColor);
				}

				// 命中由组件判定（含 RenderOnly、裁剪与弹窗遮挡），不再自己按鼠标位置换算。
				if(Enabled)
				{
					const int PressedSegment = ui_widget::CapsuleTabBarPressedIndex(Ctx, aSegmentRects.data(), (int)VariantCount);
					if(PressedSegment >= 0)
						State.m_aVariant[PresetIndex] = (size_t)std::clamp(PressedSegment, 0, (int)VariantCount - 1);
				}
			}

			// 参数行：紧凑步进器，与键槽同高。夹紧与步进全在纯逻辑层。
			for(size_t Param = 0; Param < Footer.m_NumSteppers; ++Param)
			{
				(void)DoParamStepper(Ctx, Footer.m_aSteppers[Param],
					&State.m_aParamMinusButtons[PresetIndex * MAX_PRESET_PARAMS + Param],
					&State.m_aParamPlusButtons[PresetIndex * MAX_PRESET_PARAMS + Param], Preset.m_vParams[Param],
					Localize(Preset.m_vParams[Param].m_pLabel), &pParamValues[Param], TipSize, !Enabled);
			}

			// 第二行：[键槽（吃满剩余宽度）][应用]。未安装时点按钮先装 cfg，装完
			// 同一次点击继续走进确认框，用户不用再点第二次。安装失败把可读原因
			// 留在卡片 TIP 行与状态栏上，不静默。
			for(size_t Row = 0; Row < Footer.m_NumKeyRows && Row < KeyRows; ++Row)
			{
				const CUIRect &SlotRect = Footer.m_aKeySlots[Row];
				if(SlotRect.w <= 0.0f)
					continue;
				if(Grouped)
					DrawLabel(Footer.m_aKeyLabels[Row], Localize(Preset.m_vVariants[VariantForRow(Preset, Variant, Row)].m_Label.c_str()), TipSize, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY, Alpha);
				// 读键/清除按钮按 (卡片, 行) 各一份：同一张卡的两行共用一个 ID 会互相抢焦点。
				if(Row >= vKeySlots.size())
					continue;
				const size_t ButtonIndex = PresetIndex * MAX_PRESET_KEY_ROWS + Row;
				CBindSlot &RowSlot = State.m_aaSKeySlots[PresetIndex][Row];
				const CKeyBinder::CKeyReaderResult KeyResult = GameClient()->m_KeyBinder.DoKeyReader(
					&State.m_aKeyReaderButtons[ButtonIndex], &State.m_aKeyClearButtons[ButtonIndex],
					&SlotRect, RowSlot, false, SmallSize);
				if(!ReadOnly && !State.m_ConfirmOpen && KeyResult.m_Bind != RowSlot)
					RowSlot = KeyResult.m_Bind;
			}

			// 按钮高度至少容得下一行 BODY 字加上下内边距：窗口缩放后行高会小于字号，
			// 字塞进更矮的框里就显大。底边保持不动，只往上长，不动上面的键槽行。
			CUIRect ApplyButton = Footer.m_ApplyButton;
			// 只在行距以内往上长：再多就会压到上面那一行键槽。
			const float ApplyMinHeight = std::min(ui_token::font::BODY * Scale + ui_token::spacing::XS * 2.0f * Scale, ApplyButton.h + WORKSHOP_ROW_GAP * Scale);
			if(ApplyButton.h < ApplyMinHeight)
			{
				ApplyButton.y += ApplyButton.h - ApplyMinHeight;
				ApplyButton.h = ApplyMinHeight;
			}
			if(ui_widget::PrimaryButton(Ctx, &State.m_aApplyButtons[PresetIndex], pApplyText, ApplyButton, !Enabled || State.m_ConfirmOpen))
			{
				bool CanPlan = true;
				if(NeedsCfg && !Installed)
				{
					std::string Error;
					if(InstallPresetCfgs(Storage(), Preset, Error))
					{
						// 一个预设的 cfg 是一整套（开/关互相 exec），装好就全置位。
						for(size_t CfgVariant = 0; CfgVariant < Preset.m_vVariants.size(); ++CfgVariant)
						{
							if(HasBuiltinCfg(Preset, CfgVariant))
								State.m_aCfgInstalled[PresetIndex] |= (1U << CfgVariant);
						}
						State.m_aCfgError[PresetIndex].clear();
						SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("cfg installed"));
					}
					else
					{
						State.m_aCfgError[PresetIndex] = Error;
						SetStatus(State.m_Status, State.m_StatusTime, Client(), Error.c_str());
						CanPlan = false;
					}
				}
				if(CanPlan)
				{
					// 一组动作一次规划全部条目：每条自己的键槽与命令，冲突逐条记在计划里；
					// 互斥方案只算选中的那一版，与以前的单条行为一致。
					bool NowInstalled = true;
					for(size_t Row = 0; Row < KeyRows; ++Row)
					{
						const size_t RowVariant = VariantForRow(Preset, Variant, Row);
						if(VariantRequiresCfg(Preset.m_vVariants[RowVariant]) && (State.m_aCfgInstalled[PresetIndex] & (1U << RowVariant)) == 0)
							NowInstalled = false;
					}
					const std::vector<CBindSlot> vSlots(vKeySlots.begin(), vKeySlots.begin() + std::min(KeyRows, vKeySlots.size()));
					const FBoundCommand GetBoundCommand = [this](const CBindSlot &Slot) {
						return Slot.m_Key == KEY_UNKNOWN ? std::string() : std::string(GameClient()->m_Binds.Get(Slot));
					};
					const SApplyPlan Plan = MakePresetPlan(Preset, Variant, vSlots, GetBoundCommand, pParamValues, NumParams, NowInstalled);
					if(!Plan.Valid())
					{
						// 空计划没有 Diff 可确认：直接报错，既不开确认框，也不碰撤销栈。
						SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("No executable action"));
					}
					else
					{
						State.m_PendingPlan = Plan;
						State.m_PendingPreset = PresetIndex;
						State.m_ConfirmOpen = true;
					}
				}
			}
		}
	}
	if(ScrollActive)
		State.m_ScrollRegion.End();
	// 页脚常驻一行：左侧瞬时状态，右侧结果计数与滚轮提示。

	{
		char aCount[128];
		str_format(aCount, sizeof(aCount), "%zu  %s", vFiltered.size(), Localize("Scroll to browse"));
		DrawLabel(Footer, aCount, TipSize, TEXTALIGN_MR, ui_token::color::TEXT_TIP);
	}
	if(!State.m_Status.empty() && Client()->LocalTime() - State.m_StatusTime < 3.0)
		DrawLabel(Footer, State.m_Status.c_str(), TipSize, TEXTALIGN_ML, ui_token::color::SUCCESS);

	// ---- 应用确认：落笔前必须看到 Diff，并明确可撤销 ----
	if(State.m_ConfirmOpen && !ReadOnly)
	{
		CUIRect Scrim = MainView;
		DrawRoundedSurface(Ui(), Scrim, ui_token::color::SURFACE_OVERLAY, ColorRGBA(), 0.0f);
		const float DialogWidth = std::min(MainView.w * 0.62f, 520.0f * Scale);
		// 高度跟着条目数走：一组动作可能一次写 4 条，固定 196 只会把后几条裁掉。
		const size_t DiffLines = std::max<size_t>(1, State.m_PendingPlan.m_vEntries.size());
		const float DiffHeight = static_cast<float>(DiffLines + (State.m_PendingPlan.HasConflicts() ? 1 : 0)) * SmallLine;
		const float DialogHeight = std::min(MainView.h - ui_token::spacing::MD * 2.0f * Scale,
			HeadlineSize + ui_token::spacing::XS * Scale + ui_token::spacing::MD * Scale + DiffHeight + ui_token::spacing::MD * Scale + 28.0f * Scale + ui_token::spacing::LG * 2.0f * Scale);
		CUIRect Dialog = {MainView.x + (MainView.w - DialogWidth) * 0.5f, MainView.y + (MainView.h - DialogHeight) * 0.5f, DialogWidth, DialogHeight};
		DrawPanel(Dialog, workshop_color::PANEL_SURFACE, ui_token::radius::CARD * Scale);

		CUIRect Inner = Dialog;
		Inner.VMargin(ui_token::spacing::LG * Scale, &Inner);
		Inner.HMargin(ui_token::spacing::LG * Scale, &Inner);
		CUIRect Head, BodyText, Buttons;
		Inner.HSplitTop(HeadlineSize + ui_token::spacing::XS * Scale, &Head, &Inner);
		Inner.HSplitTop(ui_token::spacing::MD * Scale, nullptr, &Inner);
		Inner.HSplitBottom(28.0f * Scale, &Inner, &Buttons);
		Inner.HSplitBottom(ui_token::spacing::MD * Scale, &Inner, nullptr);
		BodyText = Inner;

		DrawLabel(Head, Localize("Apply bind"), HeadlineSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY);
		// 逐条列「键 → 命令」：一组动作会写好几条，只报第一条等于没报。
		// 这一行会顶掉原来的绑定，就在行尾标「会覆盖」；不提「冲突」——两个键各绑各的
		// 本来就合法，唯一要提醒的是「你现在写的会盖掉旧的那条」，Diff 里也已经是 旧 -> 新。
		const SPreset *pPendingPreset = State.m_PendingPreset < vPresets.size() ? &vPresets[State.m_PendingPreset] : nullptr;
		float LineY = BodyText.y;
		for(const SApplyEntry &Entry : State.m_PendingPlan.m_vEntries)
		{
			if(LineY + SmallLine > BodyText.y + BodyText.h + 0.5f)
				break;
			char aKeyName[64] = {};
			GameClient()->m_Binds.GetKeyBindName(Entry.m_Slot.m_Key, Entry.m_Slot.m_ModifierMask, aKeyName, sizeof(aKeyName));
			std::string Line;
			if(pPendingPreset != nullptr && IsActionSet(*pPendingPreset) && Entry.m_Variant < pPendingPreset->m_vVariants.size())
			{
				Line += Localize(pPendingPreset->m_vVariants[Entry.m_Variant].m_Label.c_str());
				Line += "  ";
			}
			Line += aKeyName;
			Line += Entry.m_Overwrite ? ": " : ": + ";
			if(Entry.m_Overwrite)
				Line += Entry.m_Previous + " -> ";
			Line += Entry.m_Command;
			const bool Overwrites = Entry.m_Conflicts != APPLY_CONFLICT_NONE;
			if(Overwrites)
			{
				Line += "  · ";
				Line += Localize("Overwrites");
			}
			const SWrappedText LineText = WrapText(Line, BodyText.w, SmallSize, 1);
			DrawWrapped(CUIRect{BodyText.x, LineY, BodyText.w, SmallLine}, LineText, SmallSize, SmallLine, TEXTALIGN_ML,
				Overwrites ? ui_token::color::WARNING : ui_token::color::TEXT_SECONDARY, 1.0f);
			LineY += SmallLine;
		}
		if(State.m_PendingPlan.HasConflicts() && LineY + SmallLine <= BodyText.y + BodyText.h + 0.5f)
		{
			char aOverwrites[96];
			str_format(aOverwrites, sizeof(aOverwrites), "%zu %s", State.m_PendingPlan.ConflictCount(), Localize("overwrites"));
			DrawLabel(CUIRect{BodyText.x, LineY, BodyText.w, SmallLine}, aOverwrites, TipSize, TEXTALIGN_ML, ui_token::color::WARNING);
		}

		CUIRect Confirm, Cancel;
		Buttons.VSplitRight(104.0f * Scale, &Buttons, &Confirm);
		Buttons.VSplitRight(ui_token::spacing::SM * Scale, &Buttons, nullptr);
		Cancel = Buttons;
		if(ui_widget::SecondaryButton(Ctx, &State.m_CancelButton, Localize("Cancel"), Cancel, false))
			State.m_ConfirmOpen = false;
		if(ui_widget::PrimaryButton(Ctx, &State.m_ConfirmButton, Localize("Apply"), Confirm, false))
		{
			CBinds &Binds = GameClient()->m_Binds;
			// 一次写全部条目；撤销记录整批原值（原来没绑的键撤销时解绑）。
			// 重复槽位已在计划层去掉，这里按顺序直写，不再做「后写顶掉前写」的二次判断。
			for(const SApplyEntry &Entry : State.m_PendingPlan.m_vEntries)
				Binds.Bind(Entry.m_Slot.m_Key, Entry.m_Command.c_str(), false, Entry.m_Slot.m_ModifierMask);
			State.m_Undo.Record(State.m_PendingPlan);
			State.m_ConfirmOpen = false;
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Bind applied - undo available"));
		}
	}
}

// ---------------------------------------------------------------------------
// 蓝图编辑器
// ---------------------------------------------------------------------------
void CMenus::RenderSettingsBindBlueprint(CUIRect MainView, const bool PrewarmOnly)
{
	g_pUi = Ui();
	g_pTextRender = TextRender();
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float Scale = Metrics.m_UiScale;
	IUiContext Ctx = SettingsUiContext("settings_bind_blueprint", Scale);
	const uint64_t Scope = MakeUiScopeHash("settings_bind_blueprint");
	const float Gap = ui_token::spacing::MD * Scale;
	const float Pad = ui_token::spacing::MD * Scale;
	const float HeadlineSize = ui_token::font::HEADLINE * Scale;
	const float SmallSize = ui_token::font::SMALL * Scale;
	const float TipSize = ui_token::font::TIP * Scale;
	const float SmallLine = SmallSize * 1.45f;
	const float RowHeight = 22.0f * Scale;
	const float ButtonHeight = 26.0f * Scale;
	(void)HeadlineSize;

	SBlueprintState &State = BlueprintState();
	ResetButtonCursor(State);
	const bool Enabled = !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	// 命令框正在输入时不抢 Delete/双击：编辑态优先于画布快捷键。
	const bool EditingText = CLineInput::GetActiveInput() != nullptr;

	// 版式仍然只来自纯逻辑层的骨架函数，UI 层不再自己连锁 HSplit。
	const SWorkshopLayoutFrame Layout = ResolveWorkshopLayout(MainView, Scale);
	const CUIRect &Toolbar = Layout.m_Toolbar;
	const CUIRect &BodyArea = Layout.m_Body;
	const CUIRect &Footer = Layout.m_Footer;

	CFlowCollection &Flows = State.m_Flows;
	const size_t ActiveFlow = Flows.ActiveIndex();
	CBlueprintGraph &Graph = Flows.Graph(ActiveFlow);
	const std::string FlowFileName = Flows.FileName(ActiveFlow);
	const SCompileResult Compile = Graph.Compile(BLUEPRINT_KEY_NAME);
	// 任何会改图的动作之前先记一笔；没有变化的动作不会走到这里。
	const auto PushUndo = [&State, ActiveFlow, &Graph]() { State.m_History.Record(ActiveFlow, Graph); };
	const std::vector<SNodeTemplate> &vTemplates = NodeTemplates();
	const std::vector<SCommandSnippet> &vSnippets = CommandSnippets();

	// ---- 工具条：左侧图标签，右侧缩放 / 校验 / 导出。主操作不在工具条。 ----
	{
		CUIRect TabRow = Toolbar;
		// 右侧固定留出校验/导出/缩放那一排，页签从左端排起也不会压过去、抢点击。
		TabRow.VSplitRight(470.0f * Scale, &TabRow, nullptr);
		// 标签栏高度对齐按钮，垂直居中。
		TabRow.y += (TabRow.h - ButtonHeight) * 0.5f;
		TabRow.h = ButtonHeight;
		const size_t MaxTabs = std::min(Flows.Count(), static_cast<size_t>(MAX_FLOW_TABS));
		const bool CanClose = Flows.Count() > 1;
		for(size_t Index = 0; Index < MaxTabs && TabRow.w > 76.0f * Scale; ++Index)
		{
			const std::string Label = Flows.Title(Index);
			const bool Selected = Index == ActiveFlow;
			// 浏览器式标签：标题占满可用宽度，右侧固定一个关闭位；宽度随标题估，
			// 最宽也留得下关闭按钮，最窄也放得下三个汉字。
			const float CloseSize = 16.0f * Scale;
			const float Width = std::clamp(ApproxTextWidth(Label, TipSize) + ui_token::spacing::MD * 2.0f * Scale + CloseSize,
				88.0f * Scale, 184.0f * Scale);
			if(TabRow.w < Width)
				break;
			CUIRect Tab = {TabRow.x, TabRow.y, Width, TabRow.h};
			const bool Hovered = Enabled && Ui()->MouseHovered(&Tab);
			DrawPanel(Tab,
				Selected ? ui_token::color::LIST_ITEM_SELECTED : (Hovered ? ui_token::color::CONTAINER_BORDER : ui_token::color::SURFACE_HIGHLIGHT),
				ui_token::radius::TIGHT * Scale,
				Selected ? ui_token::color::NEUTRAL_WHITE.WithAlpha(0.22f) : ColorRGBA());
			CUIRect TitleRect = Tab;
			TitleRect.x += ui_token::spacing::MD * Scale;
			TitleRect.w = std::max(1.0f, Tab.w - ui_token::spacing::MD * Scale - (CanClose ? CloseSize + ui_token::spacing::SM * Scale : ui_token::spacing::MD * Scale));
			DrawLabel(TitleRect, Label.c_str(), TipSize, TEXTALIGN_ML,
				Selected ? ui_token::color::TEXT_PRIMARY : ui_token::color::TEXT_SECONDARY);
			if(Enabled && Ui()->DoButtonLogic(NextButton(State), Selected ? 1 : 0, &TitleRect, BUTTONFLAG_LEFT))
				State.m_PendingActiveFlow = static_cast<int>(Index);
			if(CanClose)
			{
				CUIRect CloseRect = {Tab.x + Tab.w - CloseSize - ui_token::spacing::SM * Scale, Tab.y + (Tab.h - CloseSize) * 0.5f, CloseSize, CloseSize};
				const bool CloseHovered = Enabled && Ui()->MouseHovered(&CloseRect);
				DrawLabel(CloseRect, "x", TipSize * 1.1f, TEXTALIGN_MC,
					CloseHovered ? ui_token::color::TEXT_PRIMARY : ui_token::color::TEXT_TIP);
				if(Enabled && Ui()->DoButtonLogic(NextButton(State), 0, &CloseRect, BUTTONFLAG_LEFT))
					State.m_PendingCloseFlow = static_cast<int>(Index);
			}
			TabRow.x += Width + ui_token::spacing::XS * Scale;
			TabRow.w -= Width + ui_token::spacing::XS * Scale;
		}
		if(TabRow.w > 28.0f * Scale)
		{
			CUIRect Plus = {TabRow.x, TabRow.y, 28.0f * Scale, TabRow.h};
			if(ui_widget::SecondaryButton(Ctx, NextButton(State), "+", Plus, !Enabled))
				State.m_PendingAddFlow = true;
		}
	}

	{
		CUIRect Tools = Toolbar;
		Tools.VSplitLeft(Toolbar.w - 470.0f * Scale, nullptr, &Tools);
		Tools.y += (Tools.h - ButtonHeight) * 0.5f;
		Tools.h = ButtonHeight;
		CUIRect ExportRect, ExportAllRect, ValidateRect, ZoomIn, ZoomLabel, ZoomOut;
		Tools.VSplitRight(96.0f * Scale, &Tools, &ExportRect);
		Tools.VSplitRight(ui_token::spacing::XS * Scale, &Tools, nullptr);
		Tools.VSplitRight(96.0f * Scale, &Tools, &ExportAllRect);
		Tools.VSplitRight(ui_token::spacing::MD * Scale, &Tools, nullptr);
		Tools.VSplitRight(96.0f * Scale, &Tools, &ValidateRect);
		Tools.VSplitRight(ui_token::spacing::MD * Scale, &Tools, nullptr);
		Tools.VSplitRight(28.0f * Scale, &Tools, &ZoomIn);
		Tools.VSplitRight(48.0f * Scale, &Tools, &ZoomLabel);
		Tools.VSplitRight(28.0f * Scale, &Tools, &ZoomOut);

		if(ui_widget::SecondaryButton(Ctx, NextButton(State), "-", ZoomOut, !Enabled))
			State.m_Zoom = std::max(0.5f, State.m_Zoom - 0.1f);
		if(ui_widget::SecondaryButton(Ctx, NextButton(State), "+", ZoomIn, !Enabled))
			State.m_Zoom = std::min(2.0f, State.m_Zoom + 0.1f);
		{
			char aZoom[32];
			str_format(aZoom, sizeof(aZoom), "%d%%", static_cast<int>(State.m_Zoom * 100.0f + 0.5f));
			DrawLabel(ZoomLabel, aZoom, SmallSize, TEXTALIGN_MC, ui_token::color::TEXT_SECONDARY);
		}
		if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("Validate"), ValidateRect, !Enabled))
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Compile.Ok() ? Localize("Validation passed") : Localize(CompileErrorLabel(Compile.m_vErrors.front())));
		if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("Export cfg"), ExportRect, !Enabled || !Compile.Ok()))
		{
			std::string Message;
			if(WriteCfgBundle(Storage(), Flows.BuildCfg(ActiveFlow, BLUEPRINT_KEY_NAME), Message))
				SetStatus(State.m_Status, State.m_StatusTime, Client(), Message.c_str());
			else
				SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Export failed"));
		}
		if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("Export all"), ExportAllRect, !Enabled))
		{
			const std::vector<SCfgBundle> vBundles = Flows.BuildAllCfg(BLUEPRINT_KEY_NAME);
			size_t Written = 0;
			std::string LastMessage;
			for(const SCfgBundle &Bundle : vBundles)
			{
				std::string Message;
				if(WriteCfgBundle(Storage(), Bundle, Message))
				{
					++Written;
					LastMessage = Message;
				}
			}
			char aBuffer[IO_MAX_PATH_LENGTH + 96];
			if(Written == 0)
				str_format(aBuffer, sizeof(aBuffer), "%s", Localize("Export failed"));
			else
				str_format(aBuffer, sizeof(aBuffer), "%zu / %zu %s  %s", Written, vBundles.size(), Localize("flows exported"), LastMessage.c_str());
			SetStatus(State.m_Status, State.m_StatusTime, Client(), aBuffer);
		}
	}

	// ---- 三栏：流程库 / 画布 / 常用逻辑卡片 + 检视器 ----
	CUIRect Library, Canvas, Inspector, Body = BodyArea;
	Body.VSplitLeft(BLUEPRINT_LEFT_WIDTH * Scale, &Library, &Body);
	Body.VSplitLeft(Gap, nullptr, &Body);
	Body.VSplitRight(BLUEPRINT_RIGHT_WIDTH * Scale, &Body, &Inspector);
	Body.VSplitRight(Gap, &Body, nullptr);
	Canvas = Body;

	DrawPanel(Library, ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::BASE * Scale);
	DrawPanel(Canvas, workshop_color::BLUEPRINT_CANVAS, ui_token::radius::BASE * Scale, ui_token::color::CONTAINER_BORDER);
	DrawPanel(Inspector, ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::BASE * Scale);
	// 左栏：流程库。每行一个内置流程，点一下就切过去。

	{
		CUIRect Inner = Library;
		Inner.VMargin(ui_token::spacing::MD * Scale, &Inner);
		Inner.HMargin(ui_token::spacing::MD * Scale, &Inner);
		CUIRect Head, Rows = Inner;
		Rows.HSplitTop(16.0f * Scale, &Head, &Rows);
		Rows.HSplitTop(ui_token::spacing::SM * Scale, nullptr, &Rows);
		DrawLabel(Head, Localize("Flow library"), TipSize, TEXTALIGN_ML, ui_token::color::TEXT_TIP);

		const std::vector<SPreset> &vPresets = BuiltinPresets();
		for(size_t Index = 0; Index < vPresets.size() && Index < 7; ++Index)
		{
			CUIRect Row;
			Rows.HSplitTop(30.0f * Scale, &Row, &Rows);
			if(Row.h < 30.0f * Scale - 0.5f)
				break;
			Rows.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Rows);
			const bool SelectedRow = static_cast<int>(Index) == State.m_LibraryIndex;
			const bool Hovered = Enabled && Ui()->MouseHovered(&Row);
			DrawPanel(Row, SelectedRow ? ui_token::color::LIST_ITEM_SELECTED : (Hovered ? ui_token::color::LIST_ITEM_HOVER : ui_token::color::SURFACE_HIGHLIGHT), ui_token::radius::TIGHT * Scale,
				SelectedRow ? workshop_color::SOFT_BORDER : ColorRGBA());
			CUIRect Text = Row;
			Text.VMargin(ui_token::spacing::SM * Scale, &Text);
			DrawLabel(Text, Localize(vPresets[Index].m_pTitle), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY);
			if(Enabled && Ui()->MouseButtonClicked(0) && Hovered)
			{
				PushUndo();
				Graph = GraphFromPreset(vPresets[Index], 0);
				Flows.RenameFlow(ActiveFlow, Localize(vPresets[Index].m_pTitle));
				State.m_PanX = 0.0f;
				State.m_PanY = 0.0f;
				State.m_SelectedNode = 0;
				State.m_EditedNode = -1;
				State.m_ContextNode = 0;
				State.m_TypeMenuOpen = false;
				State.m_LibraryIndex = static_cast<int>(Index);
				SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Flow loaded"));
			}
		}

		CUIRect NewFlow;
		Rows.HSplitTop(30.0f * Scale, &NewFlow, &Rows);
		if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("New flow"), NewFlow, !Enabled))
		{
			Graph.Clear();
			SNode Event;
			Event.m_Type = ENodeType::EVENT_PRESS;
			Event.m_X = 24.0f;
			Event.m_Y = 96.0f;
			State.m_SelectedNode = Graph.AddNode(Event);
			State.m_EditedNode = -1;
			State.m_LibraryIndex = -1;
			Flows.RenameFlow(ActiveFlow, Localize("Untitled flow"));
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Flow loaded"));
		}

		// 底部约束说明：蓝图的语义边界，常驻而不是藏进提示。
		CUIRect Hint;
		Library.HSplitBottom(46.0f * Scale, nullptr, &Hint);
		Hint.VMargin(ui_token::spacing::MD * Scale, &Hint);
		Hint.HMargin(ui_token::spacing::MD * Scale, &Hint);
		const SWrappedText HintText = WrapText(Localize("No delay: same-frame only"), Hint.w, TipSize, 2);
		DrawWrapped(Hint, HintText, TipSize, TipSize * 1.5f, TEXTALIGN_ML, ui_token::color::TEXT_TIP, 1.0f);
	}

	// ---- 画布坐标换算 ----
	// 节点尺寸不固定（分组框更大），所以屏幕矩形和坐标点分开给。
	const auto ToScreenRect = [&](float X, float Y, float W, float H) {
		return CUIRect{Canvas.x + (X - State.m_PanX) * State.m_Zoom, Canvas.y + (Y - State.m_PanY) * State.m_Zoom, W * State.m_Zoom, H * State.m_Zoom};
	};
	const auto ToScreenX = [&](float X) { return Canvas.x + (X - State.m_PanX) * State.m_Zoom; };
	const auto ToScreenY = [&](float Y) { return Canvas.y + (Y - State.m_PanY) * State.m_Zoom; };
	const auto ToCanvas = [&](float ScreenX, float ScreenY) {
		return std::pair<float, float>{(ScreenX - Canvas.x) / State.m_Zoom + State.m_PanX, (ScreenY - Canvas.y) / State.m_Zoom + State.m_PanY};
	};

	const bool HoverCanvas = Enabled && !State.m_HelpOpen && Ui()->MouseHovered(&Canvas);
	const auto [PointerCanvasX, PointerCanvasY] = ToCanvas(Ui()->MouseX(), Ui()->MouseY());
	if(HoverCanvas)
	{
		State.m_CursorX = PointerCanvasX;
		State.m_CursorY = PointerCanvasY;
		State.m_HasCursor = true;
	}

	// 滚轮缩放锚定指针下的画布点。
	if(HoverCanvas)
	{
		const float Wheel = Ui()->Input()->KeyPress(KEY_MOUSE_WHEEL_UP) ? 1.0f : (Ui()->Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) ? -1.0f : 0.0f);
		if(Wheel != 0.0f)
		{
			const auto [AnchorX, AnchorY] = ToCanvas(Ui()->MouseX(), Ui()->MouseY());
			const float NewZoom = std::clamp(State.m_Zoom * (Wheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.5f, 2.0f);
			State.m_Zoom = NewZoom;
			State.m_PanX = AnchorX - (Ui()->MouseX() - Canvas.x) / NewZoom;
			State.m_PanY = AnchorY - (Ui()->MouseY() - Canvas.y) / NewZoom;
		}
	}

	const std::vector<int> vVisible = Graph.VisibleNodes(
		State.m_PanX - 60.0f, State.m_PanY - 60.0f,
		State.m_PanX + Canvas.w / State.m_Zoom + 60.0f, State.m_PanY + Canvas.h / State.m_Zoom + 60.0f);

	// ---- 菜单几何先算一次：输入阶段拿它挡住画布点击，绘制阶段直接复用 ----
	const float MenuItemHeight = RowHeight + ui_token::spacing::XS * Scale;
	CUIRect TypeMenuRect{}, ContextMenuRect{};
	if(State.m_TypeMenuOpen)
	{
		const float MenuWidth = 200.0f * Scale;
		const float MenuHeight = static_cast<float>(vTemplates.size()) * MenuItemHeight + ui_token::spacing::XS * Scale * 2.0f;
		const float MenuX = std::clamp(State.m_TypeMenuScreenX, Canvas.x, std::max(Canvas.x, Canvas.x + Canvas.w - MenuWidth));
		const float MenuY = std::clamp(State.m_TypeMenuScreenY, Canvas.y, std::max(Canvas.y, Canvas.y + Canvas.h - MenuHeight));
		TypeMenuRect = {MenuX, MenuY, MenuWidth, MenuHeight};
	}
	static const char *const s_apContextItems[] = {"Duplicate", "Disconnect", "Make press event", "Next type", "Delete"};
	constexpr int CONTEXT_ITEM_COUNT = 5;
	if(State.m_ContextNode != 0)
	{
		const float MenuWidth = 176.0f * Scale;
		const float MenuHeight = static_cast<float>(CONTEXT_ITEM_COUNT) * MenuItemHeight + ui_token::spacing::XS * Scale * 2.0f;
		const float MenuX = std::clamp(State.m_ContextScreenX, Canvas.x, std::max(Canvas.x, Canvas.x + Canvas.w - MenuWidth));
		const float MenuY = std::clamp(State.m_ContextScreenY, Canvas.y, std::max(Canvas.y, Canvas.y + Canvas.h - MenuHeight));
		ContextMenuRect = {MenuX, MenuY, MenuWidth, MenuHeight};
	}
	const bool PointerInMenu = (State.m_TypeMenuOpen && Ui()->MouseHovered(&TypeMenuRect)) || (State.m_ContextNode != 0 && Ui()->MouseHovered(&ContextMenuRect));

	// ---- 画布命中测试：只查指针附近的网格桶 ----
	const SCanvasHit PointerHit = HitTestCanvas(Graph, PointerCanvasX, PointerCanvasY);
	// 分组框右下角的改尺寸手柄：14 屏幕像素见方，缩小时不跟着缩小手感。
	const float ResizeHandleSize = 14.0f;
	State.m_HasGuideX = false;
	State.m_HasGuideY = false;
	const bool LinkActive = State.m_Linking && Graph.FindNode(State.m_LinkFrom) != nullptr;
	const SCanvasHit LinkHover = LinkActive ? HitTestCanvas(Graph, PointerCanvasX, PointerCanvasY) : SCanvasHit{};
	const int LinkTargetId = (LinkActive && LinkHover.m_NodeId != 0 && LinkHover.m_NodeId != State.m_LinkFrom &&
					 (LinkHover.m_Part == ECanvasPart::IN_PORT || LinkHover.m_Part == ECanvasPart::BODY)) ?
					 LinkHover.m_NodeId :
					 0;
	// 连线命中：指针没压在节点上时才查边，否则节点会挡住它后面的线。
	// 容差除以缩放，屏幕上始终是同一段距离。
	const SEdgeHit HoverEdge = (Enabled && PointerHit.Empty() && !LinkActive) ?
					   HitTestEdge(Graph, PointerCanvasX, PointerCanvasY, EDGE_HIT_TOLERANCE / std::max(State.m_Zoom, 0.25f)) :
					   SEdgeHit{};
	const auto IsSelectedEdge = [&State](const SEdge &Edge) { return Edge.m_From == State.m_SelectedEdgeFrom && Edge.m_To == State.m_SelectedEdgeTo; };
	const auto IsHoveredEdge = [&HoverEdge](const SEdge &Edge) { return !HoverEdge.Empty() && Edge.m_From == HoverEdge.m_From && Edge.m_To == HoverEdge.m_To; };
	// 多选沿用 imnodes 的习惯：Shift/Ctrl 点击累加，空白处拖动框选。
	const bool AdditiveSelect = Ui()->Input()->ShiftIsPressed() || Ui()->Input()->ModifierIsPressed();
	const auto IsNodeSelected = [&State](const int Id) {
		return std::find(State.m_vSelectedNodes.begin(), State.m_vSelectedNodes.end(), Id) != State.m_vSelectedNodes.end();
	};
	const auto SelectOnly = [&State](const int Id) {
		State.m_vSelectedNodes.clear();
		if(Id != 0)
			State.m_vSelectedNodes.push_back(Id);
		State.m_SelectedNode = Id;
	};
	const auto ClearSelection = [&State]() {
		State.m_vSelectedNodes.clear();
		State.m_SelectedNode = 0;
	};
	const auto ToggleSelected = [&State](const int Id) {
		auto &vSelected = State.m_vSelectedNodes;
		const auto It = std::find(vSelected.begin(), vSelected.end(), Id);
		if(It == vSelected.end())
			vSelected.push_back(Id);
		else
			vSelected.erase(It);
		State.m_SelectedNode = vSelected.empty() ? 0 : vSelected.back();
	};

	const SNode *pNodeForCorner = PointerHit.m_NodeId != 0 ? Graph.FindNode(PointerHit.m_NodeId) : nullptr;

	// ---- 画布输入 ----
	if(!PointerInMenu && (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)))
	{
		// 菜单外的点击先关闭旧菜单，避免上一帧的菜单常驻在画布上。
		State.m_TypeMenuOpen = false;
		State.m_ContextNode = 0;
	}

	if(Enabled && !PointerInMenu)
	{
		// Delete：优先删选中的连线，其次删选中节点及其全部边（编辑命令时不抢按键）。
		if(!EditingText && Ui()->ConsumeHotkey(CUi::HOTKEY_DELETE))
		{
			if(State.m_SelectedEdgeFrom != 0 || State.m_SelectedEdgeTo != 0)
			{
				PushUndo();
				if(Graph.Disconnect(State.m_SelectedEdgeFrom, State.m_SelectedEdgeTo))
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Link deleted"));
				State.m_SelectedEdgeFrom = 0;
				State.m_SelectedEdgeTo = 0;
			}
			else if(!State.m_vSelectedNodes.empty())
			{
				PushUndo();
				// 删除整个多选集合；主选之外的节点一起走，边随节点一起消失。
				size_t Removed = 0;
				for(const int Id : State.m_vSelectedNodes)
				{
					if(Graph.RemoveNode(Id))
						++Removed;
				}
				ClearSelection();
				State.m_EditedNode = -1;
				if(Removed > 0)
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Node deleted"));
			}
		}

		// 撤销/重做：Ctrl+Z、Ctrl+Shift+Z 或 Ctrl+Y，编辑命令时不抢按键。
		if(!EditingText && Ui()->Input()->ModifierIsPressed() && Ui()->Input()->KeyPress(KEY_Z))
			State.m_PendingHistory = Ui()->Input()->ShiftIsPressed() ? 2 : 1;
		if(!EditingText && Ui()->Input()->ModifierIsPressed() && Ui()->Input()->KeyPress(KEY_Y))
			State.m_PendingHistory = 2;

		if(HoverCanvas && Ui()->MouseButtonClicked(0))
		{
			const bool DoubleClicked = Ui()->DoDoubleClickLogic(&State.m_DoubleClickId);
			if(PointerHit.Empty())
			{
				if(!HoverEdge.Empty())
				{
					// 点中连线：选中它（Delete / 右键删除），不平移画布。
					State.m_SelectedEdgeFrom = HoverEdge.m_From;
					State.m_SelectedEdgeTo = HoverEdge.m_To;
					State.m_SelectedNode = 0;
					State.m_EditedNode = -1;
					State.m_Panning = false;
				}
				else if(DoubleClicked)
				{
					// 双击空白画布：在指针处的网格位新建节点，先弹类型菜单。
					State.m_TypeMenuOpen = true;
					State.m_TypeMenuCanvasX = State.m_CursorX;
					State.m_TypeMenuCanvasY = State.m_CursorY;
					State.m_TypeMenuScreenX = Ui()->MouseX();
					State.m_TypeMenuScreenY = Ui()->MouseY();
					State.m_Panning = false;
				}
				else
				{
					// 空白处左键拖动 = 框选（平移改到中键），松手时结算选中的节点。
					State.m_BoxSelecting = true;
					State.m_BoxStartX = State.m_CursorX;
					State.m_BoxStartY = State.m_CursorY;
					if(!AdditiveSelect)
						ClearSelection();
					State.m_EditedNode = -1;
					State.m_Panning = false;
				}
			}
			else if(PointerHit.m_Part == ECanvasPart::OUT_PORT)
			{
				State.m_Linking = true;
				State.m_LinkFrom = PointerHit.m_NodeId;
				SelectOnly(PointerHit.m_NodeId);
				State.m_Panning = false;
			}
			else if(pNodeForCorner != nullptr &&
				PointerCanvasX >= pNodeForCorner->m_X + pNodeForCorner->m_W - ResizeHandleSize / State.m_Zoom &&
				PointerCanvasY >= pNodeForCorner->m_Y + pNodeForCorner->m_H - ResizeHandleSize / State.m_Zoom)
			{
				// 抓住节点右下角：改尺寸而不是拖动。分组框和普通卡片都能改，
				// 长命令和说明文字放进更宽的卡片里才不会挤成省略号。
				PushUndo();
				SelectOnly(pNodeForCorner->m_Id);
				State.m_ResizeNode = pNodeForCorner->m_Id;
				State.m_DragNode = 0;
				State.m_Panning = false;
			}
			else
			{
				if(AdditiveSelect)
					ToggleSelected(PointerHit.m_NodeId);
				else if(!IsNodeSelected(PointerHit.m_NodeId))
					SelectOnly(PointerHit.m_NodeId);
				State.m_SelectedNode = PointerHit.m_NodeId;
				State.m_SelectedEdgeFrom = 0;
				State.m_SelectedEdgeTo = 0;
				State.m_Panning = false;
				const SNode *pNode = Graph.FindNode(PointerHit.m_NodeId);
				if(DoubleClicked && pNode != nullptr)
				{
					// 双击节点：聚焦并进入该节点的编辑态。
					PushUndo();
					State.m_CommandInput.Set(pNode->m_Type == ENodeType::NOTE ? pNode->m_Comment.c_str() : pNode->m_Command.c_str());
					State.m_EditedNode = pNode->m_Id;
					State.m_FocusCommand = true;
					State.m_DragNode = 0;
				}
				else if(pNode != nullptr)
				{
					PushUndo();
					State.m_DragNode = pNode->m_Id;
					State.m_GrabX = pNode->m_X - State.m_CursorX;
					State.m_GrabY = pNode->m_Y - State.m_CursorY;
				}
			}
		}

		if(Enabled && HoverCanvas && Ui()->MouseButtonClicked(1) && PointerHit.m_NodeId == 0 && !HoverEdge.Empty())
		{
			// 右键连线：直接剪掉这一条边，节点与其它连线保持不动。
			PushUndo();
			if(Graph.Disconnect(HoverEdge.m_From, HoverEdge.m_To))
				SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Link deleted"));
			State.m_SelectedEdgeFrom = 0;
			State.m_SelectedEdgeTo = 0;
		}
		if(HoverCanvas && Ui()->MouseButtonClicked(1) && PointerHit.m_NodeId != 0)
		{
			State.m_ContextNode = PointerHit.m_NodeId;
			State.m_SelectedNode = PointerHit.m_NodeId;
			State.m_EditedNode = -1;
			State.m_ContextScreenX = Ui()->MouseX();
			State.m_ContextScreenY = Ui()->MouseY();
			State.m_TypeMenuOpen = false;
		}
	}

	// 松开左键：只有落到合法目标才建边，其它情况直接取消（不留残线）。
	if(State.m_Linking && (!Ui()->MouseButton(0) || !Enabled))
	{
		if(Enabled)
		{
			PushUndo();
			const ELinkOutcome Outcome = ResolveLinkDrop(Graph, State.m_LinkFrom, PointerCanvasX, PointerCanvasY);
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize(LinkOutcomeLabel(Outcome)));
		}
		State.m_Linking = false;
		State.m_LinkFrom = 0;
	}

	// 松开左键结算框选：框内节点并入选择（Shift/Ctrl 时累加，否则替换）。
	if(State.m_BoxSelecting && (!Ui()->MouseButton(0) || !Enabled))
	{
		if(Enabled)
		{
			const std::vector<int> vPicked = NodesInRect(Graph, State.m_BoxStartX, State.m_BoxStartY, PointerCanvasX, PointerCanvasY);
			if(!AdditiveSelect)
				State.m_vSelectedNodes.clear();
			for(const int Id : vPicked)
			{
				if(!IsNodeSelected(Id))
					State.m_vSelectedNodes.push_back(Id);
			}
			State.m_SelectedNode = State.m_vSelectedNodes.empty() ? 0 : State.m_vSelectedNodes.back();
		}
		State.m_BoxSelecting = false;
	}

	// 左键被框选占用，平移改到中键：按住中键拖动即可移动画布。
	if(Enabled && HoverCanvas && !PointerInMenu && Ui()->MouseButtonClicked(2))
		State.m_Panning = true;

	// 改尺寸：尺寸直接跟着指针走，下限由 SetNodeSize 夹住。
	if(Enabled && State.m_ResizeNode != 0 && Ui()->MouseButton(0))
	{
		const SNode *pResizing = Graph.FindNode(State.m_ResizeNode);
		if(pResizing != nullptr)
			Graph.SetNodeSize(State.m_ResizeNode, PointerCanvasX - pResizing->m_X, PointerCanvasY - pResizing->m_Y);
	}
	else
	{
		State.m_ResizeNode = 0;
	}

	// 拖节点：抓取点偏移 + 指针绝对位置，1:1 跟手。
	if(Enabled && State.m_DragNode != 0 && Ui()->MouseButton(0))
	{
		const auto [DragX, DragY] = ToCanvas(Ui()->MouseX(), Ui()->MouseY());
		const float TargetX = DragX + State.m_GrabX;
		const float TargetY = DragY + State.m_GrabY;
		const SNode *pPrimary = Graph.FindNode(State.m_DragNode);
		if(pPrimary != nullptr)
		{
			// 对齐吸附：靠近别的节点的边或中线时贴过去，并留下参考线。
			const SNodeAlignment Align = ResolveNodeAlignment(Graph, State.m_DragNode, TargetX, TargetY);
			State.m_HasGuideX = Align.m_SnappedX;
			State.m_HasGuideY = Align.m_SnappedY;
			State.m_GuideX = Align.m_GuideX;
			State.m_GuideY = Align.m_GuideY;
			const float SnapX = Align.m_X;
			const float SnapY = Align.m_Y;
			// 多选时整组跟手：位移取自主选节点，其它节点加同一个增量。
			const float DeltaX = SnapX - pPrimary->m_X;
			const float DeltaY = SnapY - pPrimary->m_Y;
			// 拖分组框 = 拖整个框里的节点，成员按同一个增量平移。
			const bool DraggingGroup = pPrimary->m_Type == ENodeType::GROUP;
			if(DraggingGroup)
			{
				const std::vector<int> vMembers = NodesContainedIn(Graph, State.m_DragNode);
				for(const int Id : vMembers)
				{
					const SNode *pNode = Graph.FindNode(Id);
					if(pNode != nullptr)
						Graph.MoveNode(Id, pNode->m_X + DeltaX, pNode->m_Y + DeltaY);
				}
				Graph.MoveNode(State.m_DragNode, SnapX, SnapY);
			}
			else if(State.m_vSelectedNodes.size() > 1 && IsNodeSelected(State.m_DragNode))
			{
				for(const int Id : State.m_vSelectedNodes)
				{
					const SNode *pNode = Graph.FindNode(Id);
					if(pNode != nullptr)
						Graph.MoveNode(Id, pNode->m_X + DeltaX, pNode->m_Y + DeltaY);
				}
			}
			else
			{
				Graph.MoveNode(State.m_DragNode, SnapX, SnapY);
			}
		}
	}
	else if(State.m_DragNode != 0)
	{
		State.m_DragNode = 0;
	}

	// 空白处拖动 = 平移画布。
	// 左键用于框选，所以平移同时接受中键，哪种键按住都能拖。
	if(Enabled && State.m_Panning && (Ui()->MouseButton(0) || Ui()->MouseButton(2)) && !PointerInMenu)
	{
		const vec2 Delta = MouseDeltaUi();
		State.m_PanX -= Delta.x / State.m_Zoom;
		State.m_PanY -= Delta.y / State.m_Zoom;
	}
	else
	{
		State.m_Panning = false;
	}

	Ui()->ClipEnable(&Canvas);

	// 网格：密度随缩放收敛，避免缩小后变成一片噪点。
	const float GridStep = 24.0f * State.m_Zoom;
	if(GridStep >= 10.0f)
	{
		std::vector<IGraphics::CLineItem> vGridLines;
		for(float X = Canvas.x + std::fmod(-State.m_PanX * State.m_Zoom, GridStep); X < Canvas.x + Canvas.w; X += GridStep)
			vGridLines.emplace_back(X, Canvas.y, X, Canvas.y + Canvas.h);
		for(float Y = Canvas.y + std::fmod(-State.m_PanY * State.m_Zoom, GridStep); Y < Canvas.y + Canvas.h; Y += GridStep)
			vGridLines.emplace_back(Canvas.x, Y, Canvas.x + Canvas.w, Y);
		if(!vGridLines.empty())
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(ui_token::color::SURFACE_HIGHLIGHT);
			Graphics()->LinesDraw(vGridLines.data(), vGridLines.size());
			Graphics()->LinesEnd();
		}
	}

	// 对齐参考线：拖到与别的节点对齐时，横竖各一条贯穿画布。
	if(State.m_HasGuideX || State.m_HasGuideY)
	{
		std::vector<IGraphics::CLineItem> vGuides;
		if(State.m_HasGuideX)
			vGuides.emplace_back(ToScreenX(State.m_GuideX), Canvas.y, ToScreenX(State.m_GuideX), Canvas.y + Canvas.h);
		if(State.m_HasGuideY)
			vGuides.emplace_back(Canvas.x, ToScreenY(State.m_GuideY), Canvas.x + Canvas.w, ToScreenY(State.m_GuideY));
		Graphics()->LinesBegin();
		Graphics()->SetColor(ui_token::color::WARNING.WithAlpha(0.85f));
		Graphics()->LinesDraw(vGuides.data(), vGuides.size());
		Graphics()->LinesEnd();
	}

	// 连线：贝塞尔曲线，一次批量提交；与选中节点相连的边走高亮色。
	{
		std::vector<IGraphics::CLineItem> vLinkLines;
		std::vector<IGraphics::CLineItem> vHighlightLines;
		// 悬停/选中的边单独一批：画在最上层，颜色用语义色提示「这条线可以删」。
		std::vector<IGraphics::CLineItem> vEdgeFocusLines;
		for(const SEdge &Edge : Graph.Edges())
		{
			// 画线和点得中用同一份折线几何（ResolveEdgePolyline）。
			SEdgePolyline Line;
			if(!ResolveEdgePolyline(Graph, Edge.m_From, Edge.m_To, Line))
				continue;
			// 贝塞尔曲线：控制点水平伸出，画线和命中测试共用 SampleEdgeCurve 的采样点。
			const float ScreenMinX = ToScreenX(std::min(Line.m_X0, Line.m_X1));
			const float ScreenMaxX = ToScreenX(std::max(Line.m_X0, Line.m_X1));
			const float ScreenMinY = ToScreenY(std::min(Line.m_Y0, Line.m_Y1));
			const float ScreenMaxY = ToScreenY(std::max(Line.m_Y0, Line.m_Y1));
			if(ScreenMaxX < Canvas.x || ScreenMinX > Canvas.x + Canvas.w || ScreenMaxY < Canvas.y || ScreenMinY > Canvas.y + Canvas.h)
				continue;
			const bool Highlighted = IsNodeSelected(Edge.m_From) || IsNodeSelected(Edge.m_To);
			std::vector<IGraphics::CLineItem> &Target = (IsHoveredEdge(Edge) || IsSelectedEdge(Edge)) ? vEdgeFocusLines : (Highlighted ? vHighlightLines : vLinkLines);
			const std::vector<SCanvasPoint> vCurve = SampleEdgeCurve(Line);
			for(size_t Index = 1; Index < vCurve.size(); ++Index)
			{
				Target.emplace_back(ToScreenX(vCurve[Index - 1].m_X), ToScreenY(vCurve[Index - 1].m_Y),
					ToScreenX(vCurve[Index].m_X), ToScreenY(vCurve[Index].m_Y));
			}
		}
		// 拖拽预览线：从输出端口跟随指针，落到合法目标时变绿。
		if(LinkActive)
		{
			const SNode *pFrom = Graph.FindNode(State.m_LinkFrom);
			if(pFrom != nullptr)
			{
				SEdgePolyline Preview;
				Preview.m_X0 = pFrom->m_X + pFrom->m_W;
				Preview.m_Y0 = pFrom->m_Y + pFrom->m_H * 0.5f;
				Preview.m_X1 = PointerCanvasX;
				Preview.m_Y1 = PointerCanvasY;
				const std::vector<SCanvasPoint> vPreview = SampleEdgeCurve(Preview);
				for(size_t Index = 1; Index < vPreview.size(); ++Index)
				{
					vHighlightLines.emplace_back(ToScreenX(vPreview[Index - 1].m_X), ToScreenY(vPreview[Index - 1].m_Y),
						ToScreenX(vPreview[Index].m_X), ToScreenY(vPreview[Index].m_Y));
				}
			}
		}
		if(!vLinkLines.empty())
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(workshop_color::LINK_IDLE);
			Graphics()->LinesDraw(vLinkLines.data(), vLinkLines.size());
			Graphics()->LinesEnd();
		}
		if(!vHighlightLines.empty())
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(LinkActive ? (LinkTargetId != 0 ? ui_token::color::SUCCESS.WithAlpha(0.95f) : ui_token::color::WARNING.WithAlpha(0.95f)) :
							  ui_token::color::BORDER_FOCUS);
			Graphics()->LinesDraw(vHighlightLines.data(), vHighlightLines.size());
			Graphics()->LinesEnd();
		}
		if(!vEdgeFocusLines.empty())
		{
			// 悬停用警告色（提示可删），选中用强调色常驻。
			const ColorRGBA FocusColor = HoverEdge.Empty() ? ui_token::color::ACCENT_PRIMARY.WithAlpha(0.95f) : ui_token::color::DANGER;
			Graphics()->LinesBegin();
			Graphics()->SetColor(FocusColor);
			Graphics()->LinesDraw(vEdgeFocusLines.data(), vEdgeFocusLines.size());
			Graphics()->LinesEnd();
		}
	}

	// 节点：先画底，再画标题、正文与端口，最后是右下角改尺寸手柄。
	for(const int Id : vVisible)
	{
		const SNode *pNode = Graph.FindNode(Id);
		if(pNode == nullptr)
			continue;
		const CUIRect NodeRect = ToScreenRect(pNode->m_X, pNode->m_Y, pNode->m_W, pNode->m_H);
		if(NodeRect.x + NodeRect.w < Canvas.x || NodeRect.x > Canvas.x + Canvas.w)
			continue;
		if(NodeRect.y + NodeRect.h < Canvas.y || NodeRect.y > Canvas.y + Canvas.h)
			continue;

		const bool Selected = IsNodeSelected(Id);
		const bool LinkTarget = Id == LinkTargetId;
		const float Alpha = ResolveAlpha(Ctx, Scope, static_cast<uint64_t>(Id) + 512);
		const ColorRGBA HeaderColor = NodeHeaderColor(pNode->m_Type);

		// 分组框：只有一层半透明底和左上角标题，没有命令、没有端口。
		if(pNode->m_Type == ENodeType::GROUP)
		{
			DrawPanel(NodeRect, ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.10f * Alpha), ui_token::radius::BASE * Scale,
				Selected ? ui_token::color::BORDER_FOCUS : ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.45f));
			CUIRect Label = NodeRect;
			Label.HSplitTop(22.0f * Scale, &Label, nullptr);
			Label.VMargin(ui_token::spacing::SM * Scale, &Label);
			// 右下角改尺寸手柄：常驻一点亮色，指针附近时更亮。
			const float Handle = 10.0f * Scale;
			CUIRect HandleRect = {NodeRect.x + NodeRect.w - Handle, NodeRect.y + NodeRect.h - Handle, Handle, Handle};
			const bool HandleHot = PointerHit.m_NodeId == Id &&
					       PointerCanvasX >= pNode->m_X + pNode->m_W - ResizeHandleSize / State.m_Zoom &&
					       PointerCanvasY >= pNode->m_Y + pNode->m_H - ResizeHandleSize / State.m_Zoom;
			DrawPanel(HandleRect, HandleHot ? ui_token::color::NEUTRAL_WHITE.WithAlpha(0.85f) : ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.55f), 2.0f, ColorRGBA());
			const char *pTitle = pNode->m_Comment.empty() ? Localize("Group") : pNode->m_Comment.c_str();
			DrawLabel(Label, pTitle, SmallSize, TEXTALIGN_ML, ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.95f), Alpha);
			continue;
		}

		DrawPanel(NodeRect, ui_token::color::SURFACE_ELEVATED.WithAlpha(0.96f * Alpha), ui_token::radius::BASE * Scale,
			Selected ? ui_token::color::BORDER_FOCUS : (LinkTarget ? ui_token::color::SUCCESS.WithAlpha(0.90f) : ui_token::color::LIST_ITEM_SELECTED));

		CUIRect Header = NodeRect, NodeBody;
		const float HeaderHeight = std::min(NodeRect.h * 0.42f, 24.0f * Scale * State.m_Zoom);
		Header.h = HeaderHeight;
		NodeBody = NodeRect;
		NodeBody.y += HeaderHeight;
		NodeBody.h -= HeaderHeight;
		DrawRoundedSurface(Ui(), Header, ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.92f * Alpha), ColorRGBA(),
			ui_token::radius::BASE * Scale, 0.0f, IGraphics::CORNER_TL | IGraphics::CORNER_TR);
		CUIRect HeaderText = Header;
		HeaderText.VMargin(ui_token::spacing::SM * Scale, &HeaderText);
		DrawLabel(HeaderText, Localize(NodeTypeLabel(pNode->m_Type)), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_ON_ACCENT, Alpha);

		CUIRect BodyText = NodeBody;
		BodyText.VMargin(ui_token::spacing::SM * Scale, &BodyText);
		BodyText.HMargin(ui_token::spacing::XS * Scale, &BodyText);
		const std::string_view Content = pNode->m_Type == ENodeType::NOTE ?
							 (pNode->m_Comment.empty() ? std::string_view(Localize("(no note)")) : std::string_view(pNode->m_Comment)) :
							 (pNode->m_Command.empty() ? std::string_view(Localize("(no command)")) : std::string_view(pNode->m_Command));
		// 正文行数由节点高度决定：能放几行就用几行，不再固定两行截断，
		// 因此把备注卡片拖高后下半截会被文字铺满，而不是留一大片空。
		// 行用完了不补省略号（Ellipsis = false）；真放不下就靠节点裁剪兜住，
		// 想看全把节点拉高即可，文字绝不会跑到节点外。
		const int BodyLines = std::max(1, static_cast<int>(std::floor(BodyText.h / SmallLine)));
		const SWrappedText Wrapped = WrapText(Content, BodyText.w, SmallSize, BodyLines, pNode->m_Type != ENodeType::NOTE);
		Ui()->ClipEnable(&NodeBody);
		DrawWrapped(BodyText, Wrapped, SmallSize, SmallLine, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY, Alpha);
		Ui()->ClipDisable();

		// 端口：左右各一个圆形接点。悬停/合法目标时放大，明确「这里能落」。
		const float PortSize = 9.0f * Scale;
		const bool HoverOut = PointerHit.m_NodeId == Id && PointerHit.m_Part == ECanvasPart::OUT_PORT;
		const bool HoverIn = (PointerHit.m_NodeId == Id && PointerHit.m_Part == ECanvasPart::IN_PORT) || LinkTarget;
		const float OutPortSize = HoverOut || State.m_LinkFrom == Id ? PortSize + 4.0f * Scale : PortSize;
		const float InPortSize = HoverIn ? PortSize + 4.0f * Scale : PortSize;
		const CUIRect OutPort = {NodeRect.x + NodeRect.w - OutPortSize * 0.5f, NodeRect.y + NodeRect.h * 0.5f - OutPortSize * 0.5f, OutPortSize, OutPortSize};
		const CUIRect InPort = {NodeRect.x - InPortSize * 0.5f, NodeRect.y + NodeRect.h * 0.5f - InPortSize * 0.5f, InPortSize, InPortSize};
		DrawPanel(OutPort, HoverOut ? ui_token::color::NEUTRAL_WHITE : ui_token::color::ACCENT_PRIMARY, OutPortSize * 0.5f, ColorRGBA());
		DrawPanel(InPort, LinkTarget ? ui_token::color::SUCCESS : workshop_color::PORT_IDLE, InPortSize * 0.5f, ColorRGBA());

		// 选中或悬停时显示右下角改尺寸手柄。
		if(Selected || PointerHit.m_NodeId == Id)
		{
			const float Handle = 10.0f * Scale;
			CUIRect HandleRect = {NodeRect.x + NodeRect.w - Handle, NodeRect.y + NodeRect.h - Handle, Handle, Handle};
			const bool HandleHot = PointerHit.m_NodeId == Id &&
					       PointerCanvasX >= pNode->m_X + pNode->m_W - ResizeHandleSize / State.m_Zoom &&
					       PointerCanvasY >= pNode->m_Y + pNode->m_H - ResizeHandleSize / State.m_Zoom;
			DrawPanel(HandleRect, HandleHot ? ui_token::color::NEUTRAL_WHITE.WithAlpha(0.85f) : ui_token::color::ACCENT_PRIMARY.WithAlpha(0.65f), 2.0f, ColorRGBA());
		}
	}

	// 框选矩形：虚线感靠 1px 描边 + 低透明度填充，画在节点之上便于看清范围。
	if(State.m_BoxSelecting)
	{
		const CUIRect Box = {Canvas.x + (std::min(State.m_BoxStartX, PointerCanvasX) - State.m_PanX) * State.m_Zoom,
			Canvas.y + (std::min(State.m_BoxStartY, PointerCanvasY) - State.m_PanY) * State.m_Zoom,
			std::fabs(PointerCanvasX - State.m_BoxStartX) * State.m_Zoom,
			std::fabs(PointerCanvasY - State.m_BoxStartY) * State.m_Zoom};
		if(Box.w > 1.0f && Box.h > 1.0f)
		{
			DrawPanel(Box, ui_token::color::ACCENT_PRIMARY.WithAlpha(0.12f), ui_token::radius::TIGHT, ui_token::color::BORDER_FOCUS);
		}
	}

	// 交互与布局参考 Nelarius/imnodes（MIT）的 mini-map，绘制换成这套 token。
	const SCanvasBounds Bounds = GraphBounds(Graph);
	if(Bounds.m_HasNodes)
	{
		const float MapW = 150.0f * Scale;
		const float MapH = 96.0f * Scale;
		const float MapPad = 6.0f * Scale;
		CUIRect Map = {Canvas.x + Canvas.w - MapW - 12.0f * Scale, Canvas.y + Canvas.h - MapH - 12.0f * Scale, MapW, MapH};
		DrawPanel(Map, workshop_color::PANEL_SURFACE.WithAlpha(0.86f), ui_token::radius::BASE * Scale, ui_token::color::CONTAINER_BORDER);
		const float Fit = std::min((MapW - 2.0f * MapPad) / std::max(Bounds.Width(), 1.0f),
			(MapH - 2.0f * MapPad) / std::max(Bounds.Height(), 1.0f));
		const float OffsetX = Map.x + MapPad + ((MapW - 2.0f * MapPad) - Bounds.Width() * Fit) * 0.5f;
		const float OffsetY = Map.y + MapPad + ((MapH - 2.0f * MapPad) - Bounds.Height() * Fit) * 0.5f;
		const auto ToMap = [&](float X, float Y) { return std::pair<float, float>{OffsetX + (X - Bounds.m_MinX) * Fit, OffsetY + (Y - Bounds.m_MinY) * Fit}; };
		const auto [ViewMinX, ViewMinY] = ToCanvas(Canvas.x, Canvas.y);
		const auto [ViewMaxX, ViewMaxY] = ToCanvas(Canvas.x + Canvas.w, Canvas.y + Canvas.h);
		const auto [ViewRectX, ViewRectY] = ToMap(ViewMinX, ViewMinY);
		const auto [ViewRectMaxX, ViewRectMaxY] = ToMap(ViewMaxX, ViewMaxY);
		CUIRect ViewBox = {ViewRectX, ViewRectY, std::max(2.0f, ViewRectMaxX - ViewRectX), std::max(2.0f, ViewRectMaxY - ViewRectY)};
		// 节点包围盒已经按 Fit 缩进内边距里，不需要再裁剪。
		DrawPanel(ViewBox, ui_token::color::ACCENT_PRIMARY.WithAlpha(0.10f), 0.0f, ui_token::color::ACCENT_PRIMARY.WithAlpha(0.75f));
		for(const SNode &Node : Graph.Nodes())
		{
			const auto [NodeX, NodeY] = ToMap(Node.m_X, Node.m_Y);
			const CUIRect Mini = {NodeX, NodeY, std::max(2.0f, NODE_WIDTH * Fit), std::max(2.0f, NODE_HEIGHT * Fit)};
			const ColorRGBA HeaderColor = NodeHeaderColor(Node.m_Type);
			DrawPanel(Mini, ColorRGBA(HeaderColor.r, HeaderColor.g, HeaderColor.b, 0.85f), 1.0f, ColorRGBA());
		}
		// 点/拖小地图：把该画布点搬到视口中心。
		if(Enabled && !PointerInMenu && Ui()->MouseHovered(&Map) && Ui()->MouseButton(0))
		{
			const float FocusX = (Ui()->MouseX() - OffsetX) / std::max(Fit, 0.0001f) + Bounds.m_MinX;
			const float FocusY = (Ui()->MouseY() - OffsetY) / std::max(Fit, 0.0001f) + Bounds.m_MinY;
			State.m_PanX = FocusX - Canvas.w * 0.5f / State.m_Zoom;
			State.m_PanY = FocusY - Canvas.h * 0.5f / State.m_Zoom;
			State.m_Panning = false;
		}
	}

	Ui()->ClipDisable();

	// 画布空态：没有节点时给一句下一步，而不是一片空格子。
	if(Graph.Nodes().empty())
	{
		CUIRect Empty = Canvas;
		Empty.HSplitTop(Canvas.h * 0.5f - 12.0f * Scale, nullptr, &Empty);
		DrawLabel(Empty, Localize("Start from the flow library, or create a new flow"), SmallSize, TEXTALIGN_TC, ui_token::color::TEXT_TIP);
	}

	// ---- 节点类型菜单（双击空白处） ----
	if(State.m_TypeMenuOpen)
	{
		DrawPanel(TypeMenuRect, workshop_color::PANEL_SURFACE, ui_token::radius::BASE * Scale, workshop_color::SOFT_BORDER);
		CUIRect Inner = TypeMenuRect;
		Inner.VMargin(ui_token::spacing::XS * Scale, &Inner);
		Inner.HMargin(ui_token::spacing::XS * Scale, &Inner);
		for(size_t Index = 0; Index < vTemplates.size(); ++Index)
		{
			CUIRect Row;
			Inner.HSplitTop(RowHeight, &Row, &Inner);
			Inner.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Inner);
			if(Row.h < RowHeight - 0.5f)
				break;
			const bool Hovered = Enabled && Ui()->MouseHovered(&Row);
			DrawPanel(Row, Hovered ? ui_token::color::LIST_ITEM_HOVER : ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::TIGHT * Scale, ColorRGBA());
			const CUIRect Dot = {Row.x + ui_token::spacing::SM * Scale, Row.y + (Row.h - 6.0f * Scale) * 0.5f, 6.0f * Scale, 6.0f * Scale};
			DrawPanel(Dot, NodeHeaderColor(vTemplates[Index].m_Type), 3.0f * Scale, ColorRGBA());
			CUIRect Text = Row;
			Text.x += ui_token::spacing::SM * Scale + 10.0f * Scale;
			Text.w -= ui_token::spacing::SM * Scale + 10.0f * Scale;
			DrawLabel(Text, Localize(vTemplates[Index].m_pTitle), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY);
			if(Enabled && Hovered && Ui()->MouseButtonClicked(0))
			{
				PushUndo();
				const int NewId = AddNodeFromTemplate(Graph, Index, State.m_TypeMenuCanvasX, State.m_TypeMenuCanvasY);
				if(NewId != 0)
				{
					State.m_SelectedNode = NewId;
					State.m_EditedNode = -1;
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Node created"));
				}
				State.m_TypeMenuOpen = false;
			}
		}
	}

	// ---- 节点上下文菜单（右键） ----
	if(State.m_ContextNode != 0)
	{
		const SNode *pContext = Graph.FindNode(State.m_ContextNode);
		if(pContext == nullptr)
		{
			State.m_ContextNode = 0;
		}
		else
		{
			const int ContextId = pContext->m_Id;
			const ENodeType ContextType = pContext->m_Type;
			DrawPanel(ContextMenuRect, workshop_color::PANEL_SURFACE, ui_token::radius::BASE * Scale, workshop_color::SOFT_BORDER);
			CUIRect Inner = ContextMenuRect;
			Inner.VMargin(ui_token::spacing::XS * Scale, &Inner);
			Inner.HMargin(ui_token::spacing::XS * Scale, &Inner);
			for(int Item = 0; Item < CONTEXT_ITEM_COUNT && State.m_ContextNode != 0; ++Item)
			{
				CUIRect Row;
				Inner.HSplitTop(RowHeight, &Row, &Inner);
				Inner.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Inner);
				if(Row.h < RowHeight - 0.5f)
					break;
				const bool Danger = Item == CONTEXT_ITEM_COUNT - 1;
				const bool Hovered = Enabled && Ui()->MouseHovered(&Row);
				DrawPanel(Row, Hovered ? ui_token::color::LIST_ITEM_HOVER : ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::TIGHT * Scale, ColorRGBA());
				CUIRect Text = Row;
				Text.VMargin(ui_token::spacing::SM * Scale, &Text);
				DrawLabel(Text, Localize(s_apContextItems[Item]), SmallSize, TEXTALIGN_ML, Danger ? ui_token::color::DANGER : ui_token::color::TEXT_PRIMARY);
				if(!Enabled || !Hovered || !Ui()->MouseButtonClicked(0))
					continue;
				switch(Item)
				{
				case 0:
				{
					PushUndo();
					const int NewId = Graph.DuplicateNode(ContextId, NODE_WIDTH + 24.0f, 0.0f);
					if(NewId != 0)
					{
						State.m_SelectedNode = NewId;
						State.m_EditedNode = -1;
						SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Node duplicated"));
					}
					break;
				}
				case 1:
				{
					char aBuffer[64];
					PushUndo();
					str_format(aBuffer, sizeof(aBuffer), "%d %s", Graph.DisconnectNode(ContextId), Localize("connections removed"));
					SetStatus(State.m_Status, State.m_StatusTime, Client(), aBuffer);
					break;
				}
				case 2:
					PushUndo();
					Graph.SetNodeType(ContextId, ENodeType::EVENT_PRESS);
					State.m_EditedNode = -1;
					break;
				case 3:
					PushUndo();
					Graph.SetNodeType(ContextId, CycleNodeType(ContextType));
					State.m_EditedNode = -1;
					break;
				default:
					PushUndo();
					Graph.RemoveNode(ContextId);
					if(State.m_SelectedNode == ContextId)
						State.m_SelectedNode = 0;
					State.m_EditedNode = -1;
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Node deleted"));
					break;
				}
				State.m_ContextNode = 0;
			}
		}
	}

	// ---- 右栏上：常用逻辑卡片（点一下就在画布光标处生成节点） ----
	CUIRect Right = Inspector;
	Right.VMargin(Pad, &Right);
	Right.HMargin(Pad, &Right);
	CUIRect ApplyRect, RightBody = Right;
	RightBody.HSplitBottom(ButtonHeight, &RightBody, &ApplyRect);
	RightBody.HSplitBottom(ui_token::spacing::SM * Scale, &RightBody, nullptr);

	// 图标题：每张图各有标题，导出文件名由标题派生（标签栏同名时自动加序号）。
	{
		CUIRect TitleRow;
		RightBody.HSplitTop(ButtonHeight, &TitleRow, &RightBody);
		RightBody.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &RightBody);
		if(State.m_TitleFlow != static_cast<int>(ActiveFlow))
		{
			State.m_TitleInput.Set(Flows.Title(ActiveFlow).c_str());
			State.m_TitleFlow = static_cast<int>(ActiveFlow);
		}
		if(Enabled)
		{
			ui_widget::SInputFieldOptions Options;
			Options.m_pPlaceholder = Localize("Flow title");
			Options.m_FontSize = SmallSize;
			const ui_widget::SInputFieldResult Result = ui_widget::InputField(Ctx, &State.m_TitleInput, TitleRow, Options);
			if(Result.m_Changed && State.m_TitleInput.GetString()[0] != '\0')
				Flows.RenameFlow(ActiveFlow, State.m_TitleInput.GetString());
		}
		else
		{
			ui_widget::ReadOnlyTextField(Ctx, &State.m_TitleInput, TitleRow, Localize("Flow title"), SmallSize);
		}
	}

	{
		const float CardsHeight = std::min(178.0f * Scale, RightBody.h * 0.44f);
		CUIRect CardsArea = RightBody;
		CardsArea.h = CardsHeight;
		RightBody.HSplitTop(CardsHeight + Gap, nullptr, &RightBody);

		CUIRect Head, Rows = CardsArea;
		Rows.HSplitTop(14.0f * Scale, &Head, &Rows);
		Rows.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Rows);
		DrawLabel(Head, Localize("Node cards"), TipSize, TEXTALIGN_ML, ui_token::color::TEXT_TIP);
		// 右上角的 i：节点卡片/连线/分组框/校验各干什么，都从这里进。
		{
			const float InfoSize = 14.0f * Scale;
			CUIRect InfoRect = {Head.x + Head.w - InfoSize, Head.y + (Head.h - InfoSize) * 0.5f, InfoSize, InfoSize};
			const bool InfoHovered = Ui()->MouseHovered(&InfoRect);
			DrawPanel(InfoRect, InfoHovered || State.m_HelpOpen ? ui_token::color::NEUTRAL_WHITE.WithAlpha(0.20f) : ui_token::color::NEUTRAL_WHITE.WithAlpha(0.08f),
				InfoSize * 0.5f, ui_token::color::NEUTRAL_WHITE.WithAlpha(0.22f));
			DrawLabel(InfoRect, "i", TipSize, TEXTALIGN_MC, ui_token::color::TEXT_SECONDARY);
			if(Ui()->DoButtonLogic(NextButton(State), State.m_HelpOpen ? 1 : 0, &InfoRect, BUTTONFLAG_LEFT))
			{
				State.m_HelpOpen = !State.m_HelpOpen;
				State.m_HelpSection = 0;
			}
		}
		for(size_t Index = 0; Index < vTemplates.size(); ++Index)
		{
			CUIRect Row;
			Rows.HSplitTop(RowHeight, &Row, &Rows);
			Rows.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Rows);
			if(Row.h < RowHeight - 0.5f)
				break;
			const bool Hovered = Enabled && Ui()->MouseHovered(&Row);
			DrawPanel(Row, Hovered ? ui_token::color::LIST_ITEM_HOVER : ui_token::color::SURFACE_HIGHLIGHT, ui_token::radius::TIGHT * Scale, ColorRGBA());
			const CUIRect Dot = {Row.x + ui_token::spacing::SM * Scale, Row.y + (Row.h - 6.0f * Scale) * 0.5f, 6.0f * Scale, 6.0f * Scale};
			DrawPanel(Dot, NodeHeaderColor(vTemplates[Index].m_Type), 3.0f * Scale, ColorRGBA());
			CUIRect Text = Row;
			Text.x += ui_token::spacing::SM * Scale + 10.0f * Scale;
			Text.w -= ui_token::spacing::SM * Scale + 10.0f * Scale;
			DrawLabel(Text, Localize(vTemplates[Index].m_pTitle), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY);
			DrawLabel(Text, "+", SmallSize, TEXTALIGN_MR, ui_token::color::TEXT_TIP);
			if(Enabled && Hovered && Ui()->MouseButtonClicked(0))
			{
				PushUndo();
				const int NewId = AddNodeFromTemplate(Graph, Index, State.m_CursorX, State.m_CursorY);
				if(NewId != 0)
				{
					State.m_SelectedNode = NewId;
					State.m_EditedNode = -1;
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Node created"));
				}
			}
		}
	}

	// ---- 检视器：类型切换 / 命令编辑 / 校验 / 输出预览 ----
	{
		CUIRect Cursor = RightBody;
		const auto TakeRow = [&](float Height, CUIRect &Out) {
			if(Cursor.h < Height - 0.01f)
			{
				Out = CUIRect{Cursor.x, Cursor.y, Cursor.w, 0.0f};
				return false;
			}
			Cursor.HSplitTop(Height, &Out, &Cursor);
			return true;
		};
		const auto Section = [&](const char *pText) {
			CUIRect Row;
			if(TakeRow(14.0f * Scale, Row))
				DrawLabel(Row, pText, TipSize, TEXTALIGN_ML, ui_token::color::TEXT_TIP);
			CUIRect Spacer;
			TakeRow(ui_token::spacing::XS * Scale, Spacer);
		};
		const auto DividerRow = [&]() {
			CUIRect Row;
			if(TakeRow(ui_token::spacing::MD * Scale, Row))
			{
				Row.HSplitTop(1.0f, &Row, nullptr);
				DrawPanel(Row, ui_token::color::BORDER_SUBTLE, 0.0f, ColorRGBA());
			}
		};
		const auto FieldRow = [&](const char *pLabel, const char *pValue, const ColorRGBA &Color) {
			CUIRect Row;
			if(!TakeRow(18.0f * Scale, Row))
				return;
			DrawLabel(Row, pLabel, SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY);
			DrawLabel(Row, pValue, SmallSize, TEXTALIGN_MR, Color);
		};
		// 字段下面一句话解释：光有数字和「通过」用户不知道它管什么。
		const auto Hint = [&](const char *pText) {
			CUIRect Row;
			if(!TakeRow(TipSize * 1.5f, Row))
				return;
			DrawLabel(Row, pText, TipSize * 0.92f, TEXTALIGN_ML, ui_token::color::TEXT_TIP);
		};
		DividerRow();
		Section(Localize("Selected node"));
		const SNode *pSelected = Graph.FindNode(State.m_SelectedNode);
		if(State.m_vSelectedNodes.size() > 1)
		{
			// 多选摘要：数量 + 整体包围盒，只读；几何编辑只在单选时给。
			const SCanvasBounds Bounds = SelectionBounds(Graph, State.m_vSelectedNodes);
			char aCount[96];
			str_format(aCount, sizeof(aCount), "%zu %s", State.m_vSelectedNodes.size(), Localize("nodes selected"));
			FieldRow(Localize("Selection"), aCount, ui_token::color::TEXT_PRIMARY);
			if(Bounds.m_HasNodes)
			{
				const SNodeGeometry BoundsGeometry{Bounds.m_MinX, Bounds.m_MinY, Bounds.Width(), Bounds.Height()};
				char aValue[64];
				str_format(aValue, sizeof(aValue), "%d", NodeGeometryDisplayValue(BoundsGeometry, ENodeGeometryField::X));
				FieldRow(Localize("X"), aValue, ui_token::color::TEXT_SECONDARY);
				str_format(aValue, sizeof(aValue), "%d", NodeGeometryDisplayValue(BoundsGeometry, ENodeGeometryField::Y));
				FieldRow(Localize("Y"), aValue, ui_token::color::TEXT_SECONDARY);
				str_format(aValue, sizeof(aValue), "%d", NodeGeometryDisplayValue(BoundsGeometry, ENodeGeometryField::WIDTH));
				FieldRow(Localize("Width"), aValue, ui_token::color::TEXT_SECONDARY);
				str_format(aValue, sizeof(aValue), "%d", NodeGeometryDisplayValue(BoundsGeometry, ENodeGeometryField::HEIGHT));
				FieldRow(Localize("Height"), aValue, ui_token::color::TEXT_SECONDARY);
				Hint(Localize("Bounding box of the whole selection; edit geometry with a single node selected."));
			}
		}
		else if(pSelected == nullptr)
		{
			CUIRect Row;
			if(TakeRow(18.0f * Scale, Row))
				DrawLabel(Row, Localize("No selection"), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_TIP);
		}
		else
		{
			const int SelectedId = pSelected->m_Id;
			const ENodeType SelectedType = pSelected->m_Type;
			const bool IsNote = SelectedType == ENodeType::NOTE;

			// 类型切换：按钮显示当前类型，点一下循环到下一个。
			CUIRect TypeRow;
			if(TakeRow(ButtonHeight, TypeRow))
			{
				CUIRect Label;
				TypeRow.VSplitLeft(TypeRow.w - 138.0f * Scale, &Label, &TypeRow);
				DrawLabel(Label, Localize("Type"), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY);
				char aTypeLabel[96];
				str_format(aTypeLabel, sizeof(aTypeLabel), "%s >", Localize(NodeTypeLabel(SelectedType)));
				if(ui_widget::SecondaryButton(Ctx, NextButton(State), aTypeLabel, TypeRow, !Enabled))
				{
					PushUndo();
					Graph.SetNodeType(SelectedId, CycleNodeType(SelectedType));
					State.m_EditedNode = -1;
				}
			}

			// 几何：X / Y 所有节点都能改；分组框另外给宽高。夹紧、对齐、步进都在
			// bind_workshop.cpp 的纯逻辑层，这里只负责画和收集。
			SNodeGeometry Geometry = ReadNodeGeometry(*pSelected);
			bool GeometryChanged = false;
			const auto GeometryRow = [&](const ENodeGeometryField Field, const char *pLabel, const size_t ButtonIndex) {
				CUIRect Row;
				if(!TakeRow(ButtonHeight, Row))
					return;
				char aValue[32];
				str_format(aValue, sizeof(aValue), "%d", NodeGeometryDisplayValue(Geometry, Field));
				const EStepperPress Press = DoValueStepper(Ctx, Row, &State.m_aGeometryButtons[ButtonIndex],
					&State.m_aGeometryButtons[ButtonIndex + 1], pLabel, aValue, SmallSize, !Enabled);
				if(Press == EStepperPress::NONE)
					return;
				Geometry = StepNodeGeometry(SelectedType, Geometry, Field, Press == EStepperPress::PLUS ? 1 : -1);
				GeometryChanged = true;
			};
			GeometryRow(ENodeGeometryField::X, Localize("X"), 0);
			GeometryRow(ENodeGeometryField::Y, Localize("Y"), 2);
			if(SelectedType == ENodeType::GROUP)
			{
				GeometryRow(ENodeGeometryField::WIDTH, Localize("Width"), 4);
				GeometryRow(ENodeGeometryField::HEIGHT, Localize("Height"), 6);
			}
			if(GeometryChanged)
			{
				PushUndo();
				ApplyNodeGeometry(Graph, SelectedId, Geometry);
			}

			if(State.m_EditedNode != SelectedId)
			{
				State.m_CommandInput.Set(IsNote ? pSelected->m_Comment.c_str() : pSelected->m_Command.c_str());
				State.m_EditedNode = SelectedId;
			}
			CUIRect LabelRow, EditRow;
			if(TakeRow(16.0f * Scale, LabelRow))
				DrawLabel(LabelRow, IsNote ? Localize("Note") : Localize("Command"), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY);
			if(TakeRow(ButtonHeight, EditRow))
			{
				if(State.m_FocusCommand)
				{
					State.m_CommandInput.Activate(EInputPriority::UI);
					State.m_FocusCommand = false;
				}
				if(Enabled)
				{
					ui_widget::SInputFieldOptions Options;
					Options.m_pPlaceholder = IsNote ? Localize("(no note)") : Localize("(no command)");
					Options.m_FontSize = SmallSize;
					Options.m_Clearable = true;
					const ui_widget::SInputFieldResult InputResult = ui_widget::InputField(Ctx, &State.m_CommandInput, EditRow, Options);
					if(InputResult.m_Changed)
					{
						if(IsNote)
							Graph.SetNodeComment(SelectedId, State.m_CommandInput.GetString());
						else
							Graph.SetNodeCommand(SelectedId, State.m_CommandInput.GetString());
					}
				}
				else
				{
					ui_widget::ReadOnlyTextField(Ctx, &State.m_CommandInput, EditRow, IsNote ? Localize("(no note)") : Localize("(no command)"), SmallSize);
				}
			}

			// 命令库：两行常用片段，点一下追加到当前命令末尾。
			if(!IsNote)
			{
				constexpr size_t SNIPPETS_PER_ROW = 3;
				const size_t MaxSnippets = std::min(vSnippets.size(), SNIPPETS_PER_ROW * 2);
				CUIRect SnippetRow;
				for(size_t RowStart = 0; RowStart < MaxSnippets; RowStart += SNIPPETS_PER_ROW)
				{
					if(!TakeRow(22.0f * Scale, SnippetRow))
						break;
					const float ButtonWidth = (SnippetRow.w - (SNIPPETS_PER_ROW - 1) * ui_token::spacing::XS * Scale) / SNIPPETS_PER_ROW;
					for(size_t Col = 0; Col < SNIPPETS_PER_ROW && RowStart + Col < MaxSnippets; ++Col)
					{
						const size_t SnippetIndex = RowStart + Col;
						const CUIRect Button = {SnippetRow.x + Col * (ButtonWidth + ui_token::spacing::XS * Scale), SnippetRow.y, ButtonWidth, SnippetRow.h};
						if(ui_widget::SecondaryButton(Ctx, NextButton(State), vSnippets[SnippetIndex].m_pLabel, Button, !Enabled))
						{
							const std::string Next = InsertSnippet(State.m_CommandInput.GetString(), vSnippets[SnippetIndex].m_pCommand);
							State.m_CommandInput.Set(Next.c_str());
							Graph.SetNodeCommand(SelectedId, State.m_CommandInput.GetString());
						}
					}
				}
			}
		}

		DividerRow();
		Section(Localize("Validation"));
		{
			char aBytes[64];
			str_format(aBytes, sizeof(aBytes), "%zu / %zu", Compile.m_Bytes, qm_bind_editor::MAX_CONFIG_LINE_BYTES);
			FieldRow(Localize("Line length"), aBytes, Compile.m_Bytes <= qm_bind_editor::MAX_CONFIG_LINE_BYTES ? ui_token::color::TEXT_SECONDARY : ui_token::color::DANGER);
			Hint(Localize("Bytes in the generated bind line; over the limit the server rejects it."));
			char aDepth[64];
			str_format(aDepth, sizeof(aDepth), "%d / %d", Compile.m_Depth, MAX_NESTING_DEPTH);
			FieldRow(Localize("Nesting depth"), aDepth, Compile.m_Depth <= MAX_NESTING_DEPTH ? ui_token::color::TEXT_SECONDARY : ui_token::color::DANGER);
			Hint(Localize("How many cfg files this flow goes through by exec; five at most."));
			CUIRect StatusRow;
			if(TakeRow(20.0f * Scale, StatusRow))
			{
				// 左边行标签，右边一枚 Passed / Failed 胶囊。
				DrawLabel(StatusRow, Localize("Status"), SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_SECONDARY);
				const CUIRect Pill = {StatusRow.x + StatusRow.w - 76.0f * Scale, StatusRow.y + (StatusRow.h - 18.0f * Scale) * 0.5f, 76.0f * Scale, 18.0f * Scale};
				DrawStatusPill(Pill, Compile.Ok() ? Localize("Passed") : Localize("Failed"), TipSize, Compile.Ok() ? ui_token::color::SUCCESS : ui_token::color::DANGER);
			}
			Hint(Localize("Nothing is exported until every check passes."));
			for(const ECompileError Error : Compile.m_vErrors)
			{
				CUIRect Row;
				if(!TakeRow(15.0f * Scale, Row))
					break;
				const SWrappedText Text = WrapText(Localize(CompileErrorLabel(Error)), Row.w, TipSize, 1);
				DrawWrapped(Row, Text, TipSize, 15.0f * Scale, TEXTALIGN_ML, ui_token::color::DANGER, 1.0f);
			}
		}

		DividerRow();
		Section(Localize("Output"));
		{
			CUIRect Out, CopyRow;
			if(TakeRow(44.0f * Scale, Out))
			{
				DrawPanel(Out, ui_token::color::PREVIEW_BACKDROP.WithAlpha(0.32f), ui_token::radius::TIGHT * Scale, ui_token::color::SURFACE_HIGHLIGHT);
				CUIRect OutText = Out;
				OutText.VMargin(ui_token::spacing::SM * Scale, &OutText);
				OutText.HMargin(ui_token::spacing::XS * Scale, &OutText);
				const SWrappedText Text = WrapText(Compile.m_ConfigLine, OutText.w, TipSize, 2);
				DrawWrapped(OutText, Text, TipSize, TipSize * 1.6f, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY, 1.0f);
			}
			if(TakeRow(24.0f * Scale, CopyRow))
			{
				if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("Copy"), CopyRow, !Enabled))
				{
					Input()->SetClipboardText(Compile.m_ConfigLine.c_str());
					SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Copied bind command"));
				}
			}
			// 落盘文件名常驻显示：多张图时这是唯一能区分导出文件的地方。
			CUIRect FileRow;
			if(TakeRow(16.0f * Scale, FileRow))
			{
				const SWrappedText FileText = WrapText(FlowFileName, FileRow.w, TipSize, 1);
				DrawWrapped(FileRow, FileText, TipSize, 16.0f * Scale, TEXTALIGN_ML, ui_token::color::TEXT_TIP, 1.0f);
			}
		}
	}

	// 唯一主操作：先看输出与校验，再落笔。
	if(ui_widget::PrimaryButton(Ctx, NextButton(State), Localize("Apply to key"), ApplyRect, !Enabled || !Compile.Ok()))
	{
		GameClient()->m_Binds.Bind(KEY_H, Compile.m_Command.c_str(), false, KeyModifier::NONE);
		SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize("Bind applied"));
	}

	// ---- 页脚：左侧瞬时状态（含导出路径），右侧节点/边计数与操作提示 ----
	{
		char aCount[192];
		str_format(aCount, sizeof(aCount), "%s %zu  ·  %s %zu  ·  %s  ·  %s  ·  %s  ·  %s", Localize("Nodes"), Graph.Nodes().size(), Localize("Edges"), Graph.Edges().size(),
			Localize("Double-click empty canvas to add a node"), Localize("Drag empty space to box-select"), Localize("Middle-drag to pan"), Localize("Ctrl+Z to undo"));
		DrawLabel(Footer, aCount, TipSize, TEXTALIGN_MR, ui_token::color::TEXT_TIP);
	}
	if(!State.m_Status.empty() && Client()->LocalTime() - State.m_StatusTime < 4.0)
	{
		CUIRect StatusArea = Footer;
		StatusArea.VSplitRight(Footer.w * 0.45f, &StatusArea, nullptr);
		const SWrappedText StatusText = WrapText(State.m_Status, StatusArea.w, TipSize, 1);
		DrawWrapped(StatusArea, StatusText, TipSize, Footer.h, TEXTALIGN_ML, ui_token::color::SUCCESS, 1.0f);
	}

	// 撤销/重做在帧尾执行：换图会换掉本帧捕获的引用，等这一帧用完再动。
	if(State.m_PendingHistory != 0)
	{
		const bool Undo = State.m_PendingHistory == 1;
		State.m_PendingHistory = 0;
		size_t SnapshotFlow = 0;
		CBlueprintGraph Snapshot;
		const bool HasSnapshot = Undo ? State.m_History.PopUndo(SnapshotFlow, Snapshot) : State.m_History.PopRedo(SnapshotFlow, Snapshot);
		if(HasSnapshot)
		{
			const size_t CurrentFlow = Flows.ActiveIndex();
			CBlueprintGraph Current = Flows.Graph(CurrentFlow).Clone();
			if(Undo)
				State.m_History.PushRedo(CurrentFlow, std::move(Current));
			else
				State.m_History.PushUndo(CurrentFlow, std::move(Current));
			Flows.SetActive(SnapshotFlow);
			Flows.Graph(SnapshotFlow) = std::move(Snapshot);
			ClearSelection();
			State.m_EditedNode = -1;
			State.m_ContextNode = 0;
			State.m_TypeMenuOpen = false;
			State.m_DragNode = 0;
			State.m_Linking = false;
			State.m_LinkFrom = 0;
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize(Undo ? "Undo" : "Redo"));
		}
		else
		{
			SetStatus(State.m_Status, State.m_StatusTime, Client(), Localize(Undo ? "Nothing to undo" : "Nothing to redo"));
		}
	}

	// 标签栏的切换/新增延迟到帧尾：这一刻之后本帧不再使用图引用。
	// ---- 帮助面板：右上角 i 打开，先列章节，再进内容 ----
	if(State.m_HelpOpen)
	{
		if(Ui()->Input()->KeyPress(KEY_ESCAPE))
			State.m_HelpOpen = false;
		DrawPanel(Canvas, workshop_color::BLUEPRINT_CANVAS_DIM, 0.0f, ColorRGBA());
		const float PanelW = std::min(std::max(380.0f * Scale, Canvas.w * 0.48f), Canvas.w - 24.0f * Scale);
		const float PanelH = std::min(std::max(300.0f * Scale, Canvas.h * 0.66f), Canvas.h - 24.0f * Scale);
		CUIRect PanelRect = {Canvas.x + (Canvas.w - PanelW) * 0.5f, Canvas.y + (Canvas.h - PanelH) * 0.5f, PanelW, PanelH};
		DrawPanel(PanelRect, workshop_color::PANEL_SURFACE, ui_token::radius::BASE * Scale, workshop_color::SOFT_BORDER);
		CUIRect Inner = PanelRect;
		Inner.HMargin(ui_token::spacing::MD * Scale, &Inner);
		Inner.VMargin(ui_token::spacing::MD * Scale, &Inner);

		CUIRect TitleRow, CloseRect;
		Inner.HSplitTop(20.0f * Scale, &TitleRow, &Inner);
		TitleRow.VSplitRight(20.0f * Scale, &TitleRow, &CloseRect);
		DrawLabel(CloseRect, "x", SmallSize, TEXTALIGN_MC, ui_token::color::TEXT_SECONDARY);
		if(Ui()->DoButtonLogic(NextButton(State), 0, &CloseRect, BUTTONFLAG_LEFT))
			State.m_HelpOpen = false;

		const char *apSections[4] = {Localize("Node types"), Localize("Links and gestures"), Localize("Group frame"), Localize("Validation and output")};
		const char *pSectionTitle = State.m_HelpSection == 0 ? Localize("How the blueprint works") : apSections[State.m_HelpSection - 1];
		DrawLabel(TitleRow, pSectionTitle, SmallSize, TEXTALIGN_ML, ui_token::color::TEXT_PRIMARY);
		Inner.HSplitTop(ui_token::spacing::SM * Scale, nullptr, &Inner);

		// 「返回」固定在面板底部，正文单独滚动：文案再长也只是多滚一屏，
		// 不会被面板高度截断成省略号。
		CUIRect BackRow;
		Inner.HSplitBottom(24.0f * Scale, &Inner, &BackRow);
		CUIRect Content = Inner;

		// 换章节回到顶部：沿用上一章的偏移会停在空白处。
		// CScrollRegion 的地址参与 UI 元素 ID，实例必须是静态的。
		static CScrollRegion s_HelpScroll;
		static int s_HelpScrollSection = -1;
		if(s_HelpScrollSection != State.m_HelpSection)
		{
			s_HelpScroll.Reset();
			s_HelpScrollSection = State.m_HelpSection;
		}
		CScrollRegionParams HelpScrollParams;
		// 一直预留滚动条槽位：折行宽度不随滚动条出现/消失变化，正文不会跳行。
		HelpScrollParams.m_ScrollbarAlwaysReserved = true;
		HelpScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::POPUP;
		vec2 HelpScrollOffset;
		s_HelpScroll.Begin(&Content, &HelpScrollOffset, &HelpScrollParams);
		CUIRect Cursor = Content;
		Cursor.y += HelpScrollOffset.y;

		// 段落：按码位折行；不再按剩余高度截断，超出的部分交给滚动区裁剪。
		const auto Text = [&](const char *pText, const ColorRGBA &Color, int MaxLines) {
			const float LineHeight = TipSize * 1.55f;
			const SWrappedText Wrapped = WrapText(pText, Cursor.w, TipSize, MaxLines);
			if(Wrapped.m_LineCount <= 0)
				return;
			const CUIRect Row = {Cursor.x, Cursor.y, Cursor.w, Wrapped.m_LineCount * LineHeight};
			s_HelpScroll.AddRect(Row);
			DrawWrapped(Row, Wrapped, TipSize, LineHeight, TEXTALIGN_ML, Color, 1.0f);
			Cursor.y += Row.h + ui_token::spacing::XS * Scale;
		};

		if(State.m_HelpSection == 0)
		{
			for(size_t Section = 0; Section < 4; ++Section)
			{
				CUIRect Row;
				Cursor.HSplitTop(24.0f * Scale, &Row, &Cursor);
				Cursor.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Cursor);
				// 先登记再画：登记决定滚动区内容高度，不可见的按钮不吃点击。
				if(s_HelpScroll.AddRect(Row) && ui_widget::SecondaryButton(Ctx, NextButton(State), apSections[Section], Row, false))
					State.m_HelpSection = (int)Section + 1;
			}
			Text(Localize("The blueprint never runs by itself: it only writes the bind line at the bottom right."), ui_token::color::TEXT_TIP, 6);
		}
		else if(State.m_HelpSection == 1)
		{
			for(const SNodeTemplate &Template : vTemplates)
			{
				CUIRect Label;
				Cursor.HSplitTop(15.0f * Scale, &Label, &Cursor);
				s_HelpScroll.AddRect(Label);
				DrawLabel(Label, Localize(Template.m_pTitle), TipSize, TEXTALIGN_ML, NodeHeaderColor(Template.m_Type));
				// 可滚动之后不再压行数，节点说明完整可读。
				Text(Localize(NodeTypeHelp(Template.m_Type)), ui_token::color::TEXT_SECONDARY, 6);
			}
		}
		else if(State.m_HelpSection == 2)
		{
			Text(Localize("Drag from the dot on the right of one node to the dot on the left of the next to link them."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Left-drag empty space to box-select, middle-drag to pan, wheel to zoom."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Double-click empty space to add a node, right-click a link to delete it, Delete removes the selection, Ctrl+Z undoes."), ui_token::color::TEXT_SECONDARY, 6);
		}
		else if(State.m_HelpSection == 3)
		{
			Text(Localize("A group frame carries the nodes inside it: drag the frame and the whole group moves with it."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Drag its bottom-right corner to resize. It is never compiled and never appears in the cfg."), ui_token::color::TEXT_SECONDARY, 6);
		}
		else
		{
			Text(Localize("Line length: bytes of the bind line that will be written."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Nesting depth: how many cfg files the flow goes through by exec, five at most."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Status: every check has to pass before anything is exported."), ui_token::color::TEXT_SECONDARY, 6);
			Text(Localize("Output: exactly the line that lands in the cfg file."), ui_token::color::TEXT_SECONDARY, 6);
		}

		s_HelpScroll.End();

		if(State.m_HelpSection != 0)
		{
			// 「返回」在滚动区外，滚动时不会跟着内容跑掉。
			CUIRect Back = {BackRow.x, BackRow.y, 96.0f * Scale, 22.0f * Scale};
			if(ui_widget::SecondaryButton(Ctx, NextButton(State), Localize("Back"), Back, false))
				State.m_HelpSection = 0;
		}
	}
	if(State.m_PendingActiveFlow >= 0)
	{
		Flows.SetActive(static_cast<size_t>(State.m_PendingActiveFlow));
		State.m_PendingActiveFlow = -1;
		ClearSelection();
		State.m_EditedNode = -1;
		State.m_ContextNode = 0;
		State.m_TypeMenuOpen = false;
	}
	if(State.m_PendingAddFlow)
	{
		State.m_PendingAddFlow = false;
		const size_t NewIndex = Flows.AddFlow(Localize("Untitled flow"));
		Flows.SetActive(NewIndex);
		ClearSelection();
		State.m_EditedNode = -1;
	}
	if(State.m_PendingCloseFlow >= 0)
	{
		Flows.RemoveFlow(static_cast<size_t>(State.m_PendingCloseFlow));
		State.m_PendingCloseFlow = -1;
		ClearSelection();
		State.m_EditedNode = -1;
		State.m_ContextNode = 0;
	}
}
