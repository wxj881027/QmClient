#include "config_tags.h"

#include <base/str.h>
#include <base/system.h>

#include <algorithm>
#include <cstring>

static const SConfigTagInfo s_aConfigTagInfos[] = {
	{EConfigTag::NONE, "none", "None", ""},
	{EConfigTag::VISUAL, "visual", "Visual", ""},
	{EConfigTag::HUD, "hud", "HUD", ""},
	{EConfigTag::INPUT, "input", "Input", ""},
	{EConfigTag::CHAT, "chat", "Chat", ""},
	{EConfigTag::AUDIO, "audio", "Audio", ""},
	{EConfigTag::AUTOMATION, "automation", "Auto", ""},
	{EConfigTag::SOCIAL, "social", "Social", ""},
	{EConfigTag::CAMERA, "camera", "Camera", ""},
	{EConfigTag::GAMEPLAY, "gameplay", "Gameplay", ""},
	{EConfigTag::MISC, "misc", "Misc", ""},
};

const std::vector<SConfigTagInfo> &GetConfigTagInfos()
{
	static std::vector<SConfigTagInfo> s_vInfos(std::begin(s_aConfigTagInfos), std::end(s_aConfigTagInfos));
	return s_vInfos;
}

const char *GetConfigTagName(EConfigTag Tag)
{
	int Index = static_cast<int>(Tag);
	if(Index >= 0 && Index < static_cast<int>(EConfigTag::NUM_TAGS))
		return s_aConfigTagInfos[Index].m_pName;
	return "unknown";
}

const char *GetConfigTagDisplayName(EConfigTag Tag)
{
	int Index = static_cast<int>(Tag);
	if(Index >= 0 && Index < static_cast<int>(EConfigTag::NUM_TAGS))
		return s_aConfigTagInfos[Index].m_pDisplayName;
	return "Unknown";
}

// 全局 Tags 管理器
static CConfigTagsManager *s_pConfigTagsManager = nullptr;

CConfigTagsManager *ConfigTagsManager()
{
	if(!s_pConfigTagsManager)
		s_pConfigTagsManager = new CConfigTagsManager();
	return s_pConfigTagsManager;
}

void InitConfigTags()
{
	// 确保管理器已创建
	ConfigTagsManager();
}

CConfigTagsManager::CConfigTagsManager()
{
	// 注册变量名模式到 Tags 的映射
	// Visual - 视觉效果
	RegisterTag("*laser*", EConfigTag::VISUAL);
	RegisterTag("*rainbow*", EConfigTag::VISUAL);
	RegisterTag("*jelly*", EConfigTag::VISUAL);
	RegisterTag("*skin*", EConfigTag::VISUAL);
	RegisterTag("*trail*", EConfigTag::VISUAL);
	RegisterTag("*particle*", EConfigTag::VISUAL);
	RegisterTag("*emote*", EConfigTag::VISUAL);
	RegisterTag("*outline*", EConfigTag::VISUAL);
	RegisterTag("*entity_overlay*", EConfigTag::VISUAL);
	RegisterTag("*collision_hitbox*", EConfigTag::VISUAL);
	RegisterTag("*hookcoll*", EConfigTag::VISUAL);
	RegisterTag("tc_grenade*", EConfigTag::VISUAL);
	RegisterTag("tc_laser*", EConfigTag::VISUAL);
	RegisterTag("tc_rainbow*", EConfigTag::VISUAL);
	RegisterTag("tc_show_hook*", EConfigTag::VISUAL);
	RegisterTag("tc_frozen_tees*", EConfigTag::VISUAL);
	RegisterTag("tc_freeze*", EConfigTag::VISUAL);
	RegisterTag("tc_anti*", EConfigTag::VISUAL);
	RegisterTag("tc_kill*", EConfigTag::VISUAL);
	RegisterTag("tc_weak*", EConfigTag::VISUAL);
	RegisterTag("tc_color*", EConfigTag::VISUAL);
	RegisterTag("tc_skin*", EConfigTag::VISUAL);
	RegisterTag("tc_custom*", EConfigTag::VISUAL);
	RegisterTag("tc_tee*", EConfigTag::VISUAL);
	RegisterTag("tc_dummy_color*", EConfigTag::VISUAL);
	RegisterTag("tc_warlist*", EConfigTag::VISUAL);
	RegisterTag("cl_skin*", EConfigTag::VISUAL);
	RegisterTag("cl_camera*", EConfigTag::VISUAL);
	RegisterTag("cl_zoom*", EConfigTag::VISUAL);
	RegisterTag("cl_show_entities*", EConfigTag::VISUAL);
	RegisterTag("cl_overlay_entities*", EConfigTag::VISUAL);
	RegisterTag("cl_text*", EConfigTag::VISUAL);
	RegisterTag("cl_message*", EConfigTag::VISUAL);
	RegisterTag("cl_nameplates*", EConfigTag::VISUAL);
	RegisterTag("cl_show_hook_coll*", EConfigTag::VISUAL);
	RegisterTag("cl_show_direction*", EConfigTag::VISUAL);
	RegisterTag("cl_show_fps*", EConfigTag::VISUAL);

	// HUD - 界面显示
	RegisterTag("*hud*", EConfigTag::HUD);
	RegisterTag("*scoreboard*", EConfigTag::HUD);
	RegisterTag("*stats*", EConfigTag::HUD);
	RegisterTag("*island*", EConfigTag::HUD);
	RegisterTag("*overlay*", EConfigTag::HUD);
	RegisterTag("*progress*", EConfigTag::HUD);
	RegisterTag("tc_scoreboard*", EConfigTag::HUD);
	RegisterTag("tc_hud*", EConfigTag::HUD);
	RegisterTag("tc_show_chat*", EConfigTag::HUD);
	RegisterTag("tc_show_ids*", EConfigTag::HUD);
	RegisterTag("tc_show_local*", EConfigTag::HUD);
	RegisterTag("tc_show_direction*", EConfigTag::HUD);
	RegisterTag("tc_show_ips*", EConfigTag::HUD);
	RegisterTag("tc_streamer*", EConfigTag::HUD);
	RegisterTag("cl_scoreboard*", EConfigTag::HUD);
	RegisterTag("cl_showhud*", EConfigTag::HUD);
	RegisterTag("cl_show_chat*", EConfigTag::HUD);
	RegisterTag("cl_show_local_time*", EConfigTag::HUD);
	RegisterTag("cl_show_record*", EConfigTag::HUD);
	RegisterTag("cl_showkill*", EConfigTag::HUD);
	RegisterTag("cl_vibrate*", EConfigTag::HUD);
	RegisterTag("cl_demo*", EConfigTag::HUD);
	RegisterTag("cl_notifications*", EConfigTag::HUD);
	RegisterTag("cl_race_ghost*", EConfigTag::HUD);

	// Input - 输入优化
	RegisterTag("*fast_input*", EConfigTag::INPUT);
	RegisterTag("*deepfly*", EConfigTag::INPUT);
	RegisterTag("*autoswitch*", EConfigTag::INPUT);
	RegisterTag("*input*", EConfigTag::INPUT);
	RegisterTag("tc_fast*", EConfigTag::INPUT);
	RegisterTag("tc_switch*", EConfigTag::INPUT);
	RegisterTag("tc_dummy_copy*", EConfigTag::INPUT);
	RegisterTag("tc_plasma*", EConfigTag::INPUT);
	RegisterTag("tc_control*", EConfigTag::INPUT);
	RegisterTag("cl_control*", EConfigTag::INPUT);
	RegisterTag("cl_mouse*", EConfigTag::INPUT);
	RegisterTag("cl_deepfly*", EConfigTag::INPUT);
	RegisterTag("cl_dummy_control*", EConfigTag::INPUT);
	RegisterTag("cl_dummy_hammer*", EConfigTag::INPUT);

	// Chat - 聊天相关
	RegisterTag("*chat*", EConfigTag::CHAT);
	RegisterTag("*bubble*", EConfigTag::CHAT);
	RegisterTag("*reply*", EConfigTag::CHAT);
	RegisterTag("*keyword*", EConfigTag::CHAT);
	RegisterTag("*repeat*", EConfigTag::CHAT);
	RegisterTag("tc_chat*", EConfigTag::CHAT);
	RegisterTag("tc_show_chat*", EConfigTag::CHAT);
	RegisterTag("cl_chat*", EConfigTag::CHAT);
	RegisterTag("cl_message*", EConfigTag::CHAT);
	RegisterTag("cl_show_chat*", EConfigTag::CHAT);

	// Audio - 语音/音效
	RegisterTag("*voice*", EConfigTag::AUDIO);
	RegisterTag("*qm_voice*", EConfigTag::AUDIO);
	RegisterTag("*sound*", EConfigTag::AUDIO);
	RegisterTag("*audio*", EConfigTag::AUDIO);
	RegisterTag("tc_voice*", EConfigTag::AUDIO);
	RegisterTag("cl_sound*", EConfigTag::AUDIO);
	RegisterTag("cl_music*", EConfigTag::AUDIO);

	// Automation - 自动化功能
	RegisterTag("*gores*", EConfigTag::AUTOMATION);
	RegisterTag("*auto*", EConfigTag::AUTOMATION);
	RegisterTag("*qiafen*", EConfigTag::AUTOMATION);
	RegisterTag("*unspec*", EConfigTag::AUTOMATION);
	RegisterTag("*unfreeze*", EConfigTag::AUTOMATION);
	RegisterTag("tc_auto*", EConfigTag::AUTOMATION);
	RegisterTag("tc_aim*", EConfigTag::AUTOMATION);
	RegisterTag("tc_hook*", EConfigTag::AUTOMATION);
	RegisterTag("tc_vote*", EConfigTag::AUTOMATION);
	RegisterTag("tc_kill*", EConfigTag::AUTOMATION);
	RegisterTag("tc_dummy*", EConfigTag::AUTOMATION);
	RegisterTag("cl_autoswitch*", EConfigTag::AUTOMATION);
	RegisterTag("cl_auto_demo*", EConfigTag::AUTOMATION);
	RegisterTag("cl_auto_race*", EConfigTag::AUTOMATION);

	// Social - 社交功能
	RegisterTag("*friend*", EConfigTag::SOCIAL);
	RegisterTag("*pie*", EConfigTag::SOCIAL);
	RegisterTag("*block*", EConfigTag::SOCIAL);
	RegisterTag("*whitelist*", EConfigTag::SOCIAL);
	RegisterTag("*blacklist*", EConfigTag::SOCIAL);
	RegisterTag("*mute*", EConfigTag::SOCIAL);
	RegisterTag("tc_warlist*", EConfigTag::SOCIAL);
	RegisterTag("tc_trademark*", EConfigTag::SOCIAL);
	RegisterTag("tc_friend*", EConfigTag::SOCIAL);
	RegisterTag("cl_friends*", EConfigTag::SOCIAL);
	RegisterTag("cl_mute*", EConfigTag::SOCIAL);
	RegisterTag("cl_vote*", EConfigTag::SOCIAL);

	// Camera - 相机/视野
	RegisterTag("*camera*", EConfigTag::CAMERA);
	RegisterTag("*fov*", EConfigTag::CAMERA);
	RegisterTag("*aspect*", EConfigTag::CAMERA);
	RegisterTag("*drift*", EConfigTag::CAMERA);
	RegisterTag("tc_camera*", EConfigTag::CAMERA);
	RegisterTag("cl_camera*", EConfigTag::CAMERA);
	RegisterTag("cl_zoom*", EConfigTag::CAMERA);
	RegisterTag("cl_smooth*", EConfigTag::CAMERA);

	// Gameplay - 游戏玩法
	RegisterTag("*trajectory*", EConfigTag::GAMEPLAY);
	RegisterTag("*weapon*", EConfigTag::GAMEPLAY);
	RegisterTag("*hammer*", EConfigTag::GAMEPLAY);
	RegisterTag("*grenade*", EConfigTag::GAMEPLAY);
	RegisterTag("*shotgun*", EConfigTag::GAMEPLAY);
	RegisterTag("*laser*", EConfigTag::GAMEPLAY);
	RegisterTag("*hook*", EConfigTag::GAMEPLAY);
	RegisterTag("tc_predict*", EConfigTag::GAMEPLAY);
	RegisterTag("tc_antiping*", EConfigTag::GAMEPLAY);
	RegisterTag("tc_show_hook*", EConfigTag::GAMEPLAY);
	RegisterTag("cl_predict*", EConfigTag::GAMEPLAY);
	RegisterTag("cl_antiping*", EConfigTag::GAMEPLAY);
	RegisterTag("cl_show_hook_coll*", EConfigTag::GAMEPLAY);
	RegisterTag("cl_show_direction*", EConfigTag::GAMEPLAY);
}

void CConfigTagsManager::RegisterTag(const char *pPattern, EConfigTag Tag)
{
	// 存储模式到 Tag 的映射，延迟到查询时匹配
	// 这里简化处理：直接存储变量名到 Tags 的映射
	// 实际实现中会在 GetTagsForVariable 中进行模式匹配
	
	// 为了简化，我们使用一个特殊的标记来存储模式
	std::string PatternKey = std::string("__PATTERN__") + pPattern;
	auto it = m_VariableTags.find(PatternKey);
	if(it == m_VariableTags.end())
	{
		m_VariableTags[PatternKey] = {Tag};
	}
	else
	{
		// 检查是否已存在
		bool Found = false;
		for(EConfigTag ExistingTag : it->second)
		{
			if(ExistingTag == Tag)
			{
				Found = true;
				break;
			}
		}
		if(!Found)
			it->second.push_back(Tag);
	}
}

std::vector<EConfigTag> CConfigTagsManager::InferTagsFromName(const char *pScriptName) const
{
	std::vector<EConfigTag> Tags;
	
	for(const auto &Pair : m_VariableTags)
	{
		const std::string &PatternKey = Pair.first;
		if(PatternKey.find("__PATTERN__") != 0)
			continue;
		
		const char *pPattern = PatternKey.c_str() + strlen("__PATTERN__");
		size_t PatternLen = strlen(pPattern);
		size_t NameLen = strlen(pScriptName);
		
		bool Matches = false;
		bool StartsWithStar = pPattern[0] == '*';
		bool EndsWithStar = PatternLen > 0 && pPattern[PatternLen - 1] == '*';
		
		if(StartsWithStar && EndsWithStar && PatternLen > 2)
		{
			// 中间匹配: *laser* 匹配包含 laser 的任意字符串
			const char *pMiddle = pPattern + 1;
			size_t MiddleLen = PatternLen - 2;
			for(size_t i = 0; i + MiddleLen <= NameLen; ++i)
			{
				if(str_comp_nocase_num(pScriptName + i, pMiddle, MiddleLen) == 0)
				{
					Matches = true;
					break;
				}
			}
		}
		else if(StartsWithStar)
		{
			// 后缀匹配: *laser 匹配任意以 laser 结尾的字符串
			const char *pSuffix = pPattern + 1;
			size_t SuffixLen = strlen(pSuffix);
			if(NameLen >= SuffixLen && str_comp_nocase(pScriptName + NameLen - SuffixLen, pSuffix) == 0)
				Matches = true;
		}
		else if(EndsWithStar)
		{
			// 前缀匹配: laser* 匹配任意以 laser 开头的字符串
			size_t PrefixLen = PatternLen - 1;
			if(str_comp_nocase_num(pScriptName, pPattern, PrefixLen) == 0)
				Matches = true;
		}
		else
		{
			// 完全匹配
			if(str_comp_nocase(pScriptName, pPattern) == 0)
				Matches = true;
		}
		
		if(Matches)
		{
			for(EConfigTag Tag : Pair.second)
			{
				bool AlreadyHas = false;
				for(EConfigTag Existing : Tags)
				{
					if(Existing == Tag)
					{
						AlreadyHas = true;
						break;
					}
				}
				if(!AlreadyHas)
					Tags.push_back(Tag);
			}
		}
	}
	
	return Tags;
}

std::vector<EConfigTag> CConfigTagsManager::GetTagsForVariable(const char *pScriptName) const
{
	// 首先检查是否有显式注册的 Tags
	auto it = m_VariableTags.find(pScriptName);
	if(it != m_VariableTags.end())
		return it->second;
	
	// 否则从变量名推断
	return InferTagsFromName(pScriptName);
}

bool CConfigTagsManager::HasTag(const char *pScriptName, EConfigTag Tag) const
{
	std::vector<EConfigTag> Tags = GetTagsForVariable(pScriptName);
	for(EConfigTag T : Tags)
	{
		if(T == Tag)
			return true;
	}
	return false;
}

std::vector<EConfigTag> CConfigTagsManager::GetAllAvailableTags() const
{
	std::vector<EConfigTag> Tags;
	for(int i = 1; i < static_cast<int>(EConfigTag::NUM_TAGS); ++i)
		Tags.push_back(static_cast<EConfigTag>(i));
	return Tags;
}
