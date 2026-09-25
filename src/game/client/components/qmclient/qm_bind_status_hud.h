// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H

// 内置四项状态行的语义配色档位（关闭彩虹色 HUD 后生效）
enum class EQmBindStatusTone
{
	NONE = 0, // 无分类：沿用默认文字色
	OK, // 开 / 正常
	WARNING, // 警示：Reset Self / HDF / Custom
	DANGER, // 关 / DF
};

// 内置四项状态行标识（与 hud.cpp 内置四项一一对应）
enum class EQmBindStatusLine
{
	KEY_STICKING = 0, // 卡键 cl_dummy_resetonswitch：0=On 1=Off 2=Reset Self
	HAMMER, // 锤子 qm_deepfly_mode：0=Normal 1=DF 2=HDF 3=Custom
	DUMMY_CONTROL, // 分控 cl_dummy_control：0=关 非零=开
	DUMMY_COPY, // 同步 cl_dummy_copy_moves：0=关 非零=开
};

// 由内置状态行的当前值推导语义配色；取值没有对应状态时返回 NONE（纯函数，可单测）
EQmBindStatusTone QmResolveBuiltinBindStatusTone(EQmBindStatusLine Line, int Value);

// 把条目列表序列化回配置字符串（恢复默认/导出用）
std::string QmSerializeBindStatusList(const std::vector<SQmBindStatusEntry> &vEntries);

// 状态面板尺寸（HUD 背景框）
struct SQmBindStatusPanelSize
{
	float m_W;
	float m_H;
};

// 由可见行数与最宽行推导面板尺寸：宽度 = 最宽行 + 左右内边距，高度 = 行高 * 行数 + 上下内边距。
// 行数 <= 0 返回零尺寸（不绘制面板）。行数变化时高度必须同步增长，保证背景包住每一行（纯函数，可单测）
SQmBindStatusPanelSize QmComputeBindStatusPanelSize(int LineCount, float MaxLineWidth, float LineHeight, float PaddingX, float PaddingY);

// 面板实际绘制的一行：文本 + 状态色调（自定义条目没有状态语义，色调为 NONE）
struct SQmBindStatusRenderLine
{
	std::string m_Text;
	EQmBindStatusTone m_Tone = EQmBindStatusTone::NONE;
};

// 内置四项里的一行：只有 m_Show 为真时才参与绘制
struct SQmBindStatusBuiltinLine
{
	bool m_Show = false;
	const char *m_pText = "";
	EQmBindStatusTone m_Tone = EQmBindStatusTone::NONE;
};

// 决定面板实际绘制的行：自定义列表生效时完全替换内置四项，两者互斥，不会把内置行追加到自定义行之后。
// 面板尺寸由返回的行数推导（QmComputeBindStatusPanelSize），保证背景始终包住每一行（纯函数，可单测）
std::vector<SQmBindStatusRenderLine> QmBuildBindStatusRenderLines(bool CustomActive, const std::vector<std::string> &vCustomLines, const std::vector<SQmBindStatusBuiltinLine> &vBuiltinLines);

// 自定义 bind 状态 HUD 模块：解析 qm_bind_status_items 并提供当前应显示的文本行
class CQmBindStatusHud : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;

	// 自定义列表是否生效（qm_bind_status_items 非空）
	bool IsCustomListActive() const;
	// 当前应显示的文本行（按配置顺序；变量值不匹配或变量不存在的条目不显示）
	const std::vector<std::string> &GetVisibleLines();

private:
	static void ConResetDefaults(IConsole::IResult *, void *pUserData);
	void Rebuild();
	void ResolveVariables();

	std::vector<SQmBindStatusEntry> m_vEntries;
	std::vector<const SIntConfigVariable *> m_vpVariables;
	std::vector<std::string> m_vVisibleLines;
	std::vector<int> m_vCachedValues;
	std::string m_LastConfig;
	bool m_ConfigValid = false;
};

#endif
