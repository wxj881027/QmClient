#include "search_answers.h"

#include <base/str.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace qm_search_answers
{

	const std::vector<SAnswer> &Answers()
	{
		// 参考作者的 Q&A.md，按当前卡片和命令整理；不在运行时读取或修改原文。
		// 首版只维护中文。新增条目同时补充口语别名和真实卡片 ID。
		static const std::vector<SAnswer> s_vAnswers = {
			{"scoreboard-cursor", "计分板怎么呼出鼠标？",
				"在控制设置的“杂项”中绑定“计分板鼠标”，打开计分板后按绑定键切换鼠标。也可以在 F1 控制台绑定 toggle_scoreboard_cursor。示例中的 key 需要替换成你想用的按键。",
				{"计分板鼠标", "计分板光标", "计分板呼出鼠标", "toggle_scoreboard_cursor"},
				{"deck:controls-miscellaneous"}, "bind key toggle_scoreboard_cursor"},
			{"hitbox", "身上的十字架怎么关？",
				"打开下方的“碰撞箱”卡片，关闭显示碰撞箱的选项。这里控制角色碰撞范围及武器交互的可视化。",
				{"身上十字架", "十字架", "碰撞箱", "碰撞体积", "hitbox"}, {"qm:collision_hitbox"}},
			{"notifications", "右边黑黑的气泡怎么调整？",
				"右侧弹出的系统提示可以在“通知栏”卡片中调整。关闭“重要服务器提示显示为通知”和“Echo 消息改走通知栏”可停用这两类通知，也可以调整外观和过滤条件。",
				{"右边黑黑气泡", "右边气泡", "黑色气泡", "黑黑气泡", "右侧通知", "通知栏", "系统提示"}, {"qm:hud_notifications"}},
			{"screenshots", "截图保存在哪里？",
				"截图保存在客户端数据目录下的 screenshots 文件夹。点击“打开目录”会打开当前客户端实际使用的位置；使用便携配置或自定义存储路径时也以此位置为准。",
				{"截图", "截屏", "screenshots"}, {}, nullptr, "screenshots"},
			{"performance", "客户端卡顿应该怎么排查？",
				"在“调试模式”卡片中启用性能日志，重现卡顿后收集数据目录 dumps/QmClient_Perf 下新生成的日志。反馈时附上客户端版本、触发操作和当时开启的功能。完成收集后可以关闭调试模式。",
				{"卡顿", "卡卡", "掉帧", "性能日志", "性能排查", "调试模式", "帧数低", "fps低"}, {"qm:debug_mode"}, nullptr, "dumps/QmClient_Perf"},
			{"bind-status", "HUD 的卡键和分身状态在哪里关？",
				"在“DDRace HUD Pro”卡片中关闭不需要的卡键、卡锤、分身控制或分身同步状态显示。这些选项控制 HUD 提示。",
				{"卡键状态", "卡锤状态", "分身状态", "分身同步状态", "hud卡键", "ddracehudpro"}, {"qm:bind_status_hud"}},
			{"repeat", "复读怎么用？",
				"先在“梦的百宝箱”卡片中启用复读，再到“控制”的“聊天”卡片绑定复读按键。连续按两次绑定键触发复读，请避免刷屏。",
				{"复读", "复读按键", "repeat"}, {"qm:mini_features", "deck:controls-chat"}},
			{"trajectory", "武器辅助线在哪里开？",
				"在“武器”控制卡片中绑定武器弹道辅助线按键，再用“武器弹道辅助线”卡片调整显示。下面的示例将武器辅助线与钩子辅助线绑定到同一个键，使用前把 key 换成自己的键位。",
				{"武器辅助线", "武器弹道", "武器弹道辅助线", "辅助线绑定", "+showweapontrajectory"}, {"qm:weapon_trajectory", "deck:controls-weapon"}, "bind key \"+showweapontrajectory;+showhookcoll\""},
			{"game-sound", "游戏没有声音怎么办？",
				"先在“声音”设置中确认已启用声音，再检查主音量和游戏音量。若只有切到后台时没有声音，检查后台静音选项；还可以确认系统音量混合器没有将客户端静音。",
				{"游戏没声音", "游戏没有声音", "游戏声音", "游戏音量", "主音量", "没声音", "没有声音"}, {"deck:sound-toggle", "deck:sound-volume"}},
			{"voice-sound", "语音没有声音怎么办？",
				"在“语音”卡片中检查启用状态、输入和输出设备、音量及静音设置。使用卡片中的状态信息和本地测试区分麦克风采集、播放与连接问题；使用按住说话时也要检查对应绑定。",
				{"语音没声音", "语音没有声音", "语音听不到", "听不到队友", "麦克风", "没声音", "没有声音"}, {"qm:voice"}},
			{"music-sound", "音乐没有声音怎么办？",
				"先确认音乐播放器本身能播放声音。客户端的歌曲信息和歌词显示不会代替播放器输出声音。如果指的是游戏内菜单音乐，请检查声音设置中的音乐音量。",
				{"音乐没声音", "音乐没有声音", "音乐听不到", "音乐音量", "没声音", "没有声音"}, {"deck:sound-volume"}},
			{"voice-distance", "同一语音房间走远后声音变小怎么办？",
				"在“语音”卡片的距离设置中开启“同房间全图收听”，让同房间语音不受地图距离限制。“立体声定位”和距离半径也在该卡片中调整。",
				{"语音距离", "语音衰减", "走远声音变小", "同房间全图收听", "全图收听", "qm_voice_group_global"}, {"qm:voice"}, "qm_voice_group_global 1"},
			{"lyrics", "放歌时灵动岛为什么没有歌词？",
				"先确认播放器正在播放，并检查客户端能否取得歌曲信息。然后在“歌词”卡片启用歌词和“在灵动岛显示歌词”，同时检查灵动岛是否启用。若有歌名但无歌词，再检查歌词来源及网络访问；部分歌曲可能没有可用歌词。",
				{"歌词", "灵动岛歌词", "没有歌词", "不显示歌词", "歌名", "歌曲信息", "smtc", "网易云"}, {"qm:lyrics", "qm:dynamic_island"}},
			{"spectator-eye", "旁观的小眼睛去哪里了？",
				"启用灵动岛时，旁观相关提示会显示在顶部灵动岛中。可以在“灵动岛”卡片调整显示，或关闭灵动岛使用原有 HUD 样式。",
				{"旁观眼睛", "旁观小眼睛", "小眼睛", "旁观提示"}, {"qm:dynamic_island"}},
			{"spectate-key", "旁观模式选人的按键是什么？",
				"在“控制”的“杂项”卡片中绑定“旁观模式”。也可以使用下面的 +spectate 绑定命令；把 key 替换为想使用的按键。",
				{"旁观选人", "旁观模式按键", "旁观按键", "+spectate"}, {"deck:controls-miscellaneous"}, "bind key \"+spectate\""},
			{"server-info", "服务器信息不显示怎么办？",
				"在“梦的百宝箱”卡片中关闭“进图时隐藏服务器信息”。如果手动将 cl_motd_time 设为了 0，可以改成 10 或其他非零值。",
				{"服务器信息", "进图信息", "motd", "cl_motd_time"}, {"qm:mini_features"}, "cl_motd_time 10"},
			{"title-backup", "重装电脑前如何备份赞助头衔？",
				"备份客户端数据目录 qmclient 下的 title_token.txt，并在新环境的数据目录中放回同一相对位置。点击“打开目录”可定位到该文件所在目录。这个文件包含头衔凭据，请自行保管，不要发到群里。",
				{"备份头衔", "头衔备份", "重装头衔", "title_token.txt", "头衔丢失"}, {"deck:qmclient-contributors-title"}, nullptr, "qmclient"},
			{"translation", "聊天翻译怎么用？",
				"在“翻译”卡片中选择翻译服务和目标语言，并开启自动翻译接收消息。需要凭据的服务要先填写相应配置，再用卡片中的测试按钮检查服务。接收翻译、发送翻译和译文显示可以分别调整。",
				{"翻译", "聊天翻译", "翻译服务", "翻译测试"}, {"qm:translate"}},
		};
		return s_vAnswers;
	}

	namespace
	{
		std::string Normalize(const char *pText)
		{
			std::string Result;
			while(*pText)
			{
				int Codepoint = str_utf8_decode(&pText);
				if(Codepoint < 0)
					return {};
				if(Codepoint >= 0xff01 && Codepoint <= 0xff5e)
					Codepoint -= 0xfee0;
				if(Codepoint >= 'A' && Codepoint <= 'Z')
					Codepoint += 'a' - 'A';
				if(Codepoint <= ' ' || Codepoint == 0x3000 || Codepoint == 0x3001 || Codepoint == 0x3002 ||
					Codepoint == '?' || Codepoint == '!' || Codepoint == ',' || Codepoint == ':' || Codepoint == ';' ||
					Codepoint == '"' || Codepoint == '\'' || Codepoint == 0x201c || Codepoint == 0x201d)
					continue;
				char aEncoded[4];
				const int Length = str_utf8_encode(aEncoded, Codepoint);
				Result.append(aEncoded, Length);
			}
			// 只去掉询问和操作措辞；保留“没有”“不显示”等能区分故障的内容。
			for(const char *pWord : {"开启", "关闭", "打开", "关掉", "调整", "请问", "我想", "能不能", "可不可以", "应该", "怎么办", "如何用", "怎么用", "怎么关", "怎么开", "如何", "为什么", "为啥", "怎么", "在哪里关", "在哪里开", "在哪关", "在哪开", "在哪里", "在哪", "哪里", "是什么", "一下", "我的", "这个", "那个", "的", "我", "呀", "啊", "呢", "吗"})
			{
				const size_t Length = str_length(pWord);
				for(size_t Pos = Result.find(pWord); Pos != std::string::npos; Pos = Result.find(pWord, Pos))
					Result.erase(Pos, Length);
			}
			return Result;
		}

		struct SIndexedAnswer
		{
			const SAnswer *m_pAnswer;
			std::vector<std::string> m_vPhrases;
		};

		const std::vector<SIndexedAnswer> &Index()
		{
			static const auto s_vIndex = [] {
				std::vector<SIndexedAnswer> vIndex;
				for(const SAnswer &Answer : Answers())
				{
					SIndexedAnswer Entry{&Answer, {Normalize(Answer.m_pQuestion)}};
					for(const char *pAlias : Answer.m_vAliases)
						Entry.m_vPhrases.push_back(Normalize(pAlias));
					vIndex.push_back(std::move(Entry));
				}
				return vIndex;
			}();
			return s_vIndex;
		}
	}

	SResult Search(const char *pQuery)
	{
		SResult Result;
		if(pQuery == nullptr)
			return Result;
		// 先按既有字节上限截断扫描，超长输入不再先遍历整个 C 字符串。
		size_t Length = 0;
		while(Length <= 512 && pQuery[Length] != '\0')
			++Length;
		if(Length > 512 || !str_utf8_check(pQuery))
			return Result;
		const std::string Query = Normalize(pQuery);
		if(Query.empty())
			return Result;
		const auto Codepoints = std::count_if(Query.begin(), Query.end(), [](unsigned char Character) { return (Character & 0xc0) != 0x80; });
		struct SCandidate
		{
			const SAnswer *m_pAnswer;
			int m_Score;
		};
		std::vector<SCandidate> vCandidates;
		for(const auto &Entry : Index())
		{
			int Score = 0;
			for(const std::string &Phrase : Entry.m_vPhrases)
			{
				if(Phrase.empty())
					continue;
				if(Query == Phrase)
					Score = 100;
				else if(Query.find(Phrase) != std::string::npos)
					Score = std::max(Score, static_cast<int>(80 * Phrase.size() / Query.size()));
				else if(Codepoints >= 2 && Query.size() >= 3 && Phrase.find(Query) != std::string::npos)
					Score = std::max(Score, static_cast<int>(60 * Query.size() / Phrase.size()));
			}
			if(Score > 0)
				vCandidates.push_back({Entry.m_pAnswer, Score});
		}
		std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const auto &Left, const auto &Right) { return Left.m_Score > Right.m_Score; });
		// 只有唯一完整匹配才直接答复；部分匹配或同分歧义交给玩家选择。
		if(!vCandidates.empty() && vCandidates.front().m_Score == 100 && (vCandidates.size() == 1 || vCandidates[1].m_Score < 100))
			Result.m_pAnswer = vCandidates.front().m_pAnswer;
		for(size_t Index = 0; Index < std::min(size_t{5}, vCandidates.size()); ++Index)
			Result.m_vCandidates.push_back(vCandidates[Index].m_pAnswer);
		return Result;
	}

	bool TrySearch(const char *pQuery, SResult &Out)
	{
		try
		{
			SResult Candidate = Search(pQuery);
			Out.m_vCandidates.swap(Candidate.m_vCandidates);
			std::swap(Out.m_pAnswer, Candidate.m_pAnswer);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}
