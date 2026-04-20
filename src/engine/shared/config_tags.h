#ifndef ENGINE_SHARED_CONFIG_TAGS_H
#define ENGINE_SHARED_CONFIG_TAGS_H

#include <base/types.h>

#include <vector>
#include <unordered_map>
#include <string>

// Config Tags 枚举 - 用于分类筛选
enum class EConfigTag : uint8_t
{
	NONE = 0,
	VISUAL,      // 视觉效果 (皮肤、彩虹、激光等)
	HUD,         // 界面显示 (计分板、灵动岛、统计等)
	INPUT,       // 输入优化 (Fast Input、Deepfly等)
	CHAT,        // 聊天相关 (气泡、复读、关键词等)
	AUDIO,       // 语音/音效 (QmVoice等)
	AUTOMATION,  // 自动化功能 (Gores、自动切换等)
	SOCIAL,      // 社交功能 (好友、饼菜单、屏蔽词等)
	CAMERA,      // 相机/视野 (漂移、动态FOV等)
	GAMEPLAY,    // 游戏玩法 (武器轨迹、碰撞盒等)
	MISC,        // 其他杂项
	NUM_TAGS
};

// Tag 信息结构
struct SConfigTagInfo
{
	EConfigTag m_Tag;
	const char *m_pName;        // 显示名称 (如 "Visual")
	const char *m_pDisplayName; // 本地化显示名称 (如 "视觉效果")
	const char *m_pIcon;        // 图标 (可选)
};

// 获取所有 Tag 信息
const std::vector<SConfigTagInfo> &GetConfigTagInfos();

// 获取 Tag 名称
const char *GetConfigTagName(EConfigTag Tag);

// 获取 Tag 显示名称
const char *GetConfigTagDisplayName(EConfigTag Tag);

// 配置变量 Tags 管理器
class CConfigTagsManager
{
public:
	CConfigTagsManager();

	// 获取变量的所有 Tags
	std::vector<EConfigTag> GetTagsForVariable(const char *pScriptName) const;

	// 检查变量是否有指定 Tag
	bool HasTag(const char *pScriptName, EConfigTag Tag) const;

	// 获取所有可用的 Tags (用于 UI 显示)
	std::vector<EConfigTag> GetAllAvailableTags() const;

private:
	// 变量名到 Tags 的映射
	std::unordered_map<std::string, std::vector<EConfigTag>> m_VariableTags;

	// 根据变量名模式自动推断 Tags
	std::vector<EConfigTag> InferTagsFromName(const char *pScriptName) const;

	// 手动注册变量 Tags
	void RegisterTag(const char *pPattern, EConfigTag Tag);
	void RegisterVariableTags(const char *pScriptName, std::initializer_list<EConfigTag> Tags);
};

// 全局 Tags 管理器实例
CConfigTagsManager *ConfigTagsManager();

// 初始化 Tags 系统
void InitConfigTags();

#endif // ENGINE_SHARED_CONFIG_TAGS_H
