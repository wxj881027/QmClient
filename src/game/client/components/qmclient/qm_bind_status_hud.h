// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H

#include <engine/shared/config.h>

#include <game/client/component.h>

#include <string>
#include <utility>
#include <vector>

class IConsole;

// 单条自定义 bind 状态条目：识别一个配置变量的开关状态并显示对应文本
struct SQmBindStatusEntry
{
	std::string m_VarName; // 被识别的配置变量脚本名，如 cl_dummy_hammer
	std::vector<std::pair<int, std::string>> m_vValueTexts; // 状态值 -> 显示文本（精确匹配优先）
	std::string m_NonZeroText; // 非零开关文本（值为 0 时不显示；仅填变量名时默认显示变量名本身）
	bool m_HasNonZeroText = false; // 是否配置了非零开关文本
};

// 解析 qm_bind_status_items 配置字符串为条目列表，非法条目直接跳过（纯函数，可单测）
bool QmParseBindStatusList(const char *pList, std::vector<SQmBindStatusEntry> &vOut);

// 根据变量当前值决定条目显示文本；无匹配时返回 false（纯函数，可单测）
bool QmResolveBindStatusEntry(const SQmBindStatusEntry &Entry, int Value, std::string &OutText);

// 内置默认四项（卡键/锤子/分控/同步），与旧版固定显示一致
const std::vector<SQmBindStatusEntry> &QmDefaultBindStatusEntries();

// 把条目列表序列化回配置字符串（恢复默认/导出用）
std::string QmSerializeBindStatusList(const std::vector<SQmBindStatusEntry> &vEntries);

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
