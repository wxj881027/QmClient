// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_sound_audio_pack[] = {
		{Localizable("Audio pack directory"), nullptr, "音频包目录"},
		{Localizable("Default"), nullptr, "默认"},
		{Localizable("Edit audio pack"), nullptr, "编辑音频包"},
		{nullptr, "snd_enable", nullptr},
		{nullptr, "snd_pack", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:sound-audio-pack", deck_sound_audio_pack);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_sound_toggle[] = {
		{Localizable("Enable game sounds"), nullptr, "启用游戏声音"},
		{Localizable("Enable gun sound"), nullptr, "启用手枪声音"},
		{Localizable("Enable highlighted chat sound"), nullptr, "启用聊天被提及消息提示音"},
		{Localizable("Enable regular chat sound"), nullptr, "启用聊天消息提示音"},
		{Localizable("Enable server message sound"), nullptr, "启用服务器消息提示音"},
		{Localizable("Enable shout when holding fire while in water"), nullptr, "进水时按住开火键发出呐喊"},
		{Localizable("Enable team chat sound"), nullptr, "启用队伍聊天消息提示音"},
		{Localizable("Mute when not active"), nullptr, "在后台时静音"},
		{Localizable("Play background music"), nullptr, "播放背景音乐"},
		{Localizable("Use sounds"), nullptr, "开启声音"},
		{nullptr, "snd_chat", nullptr},
		{nullptr, "snd_enable", nullptr},
		{nullptr, "snd_enable_music", nullptr},
		{nullptr, "snd_game", nullptr},
		{nullptr, "snd_gun", nullptr},
		{nullptr, "snd_highlight", nullptr},
		{nullptr, "snd_long_pain", nullptr},
		{nullptr, "snd_nonactive_mute", nullptr},
		{nullptr, "snd_pack", nullptr},
		{nullptr, "snd_servermessage", nullptr},
		{nullptr, "snd_team_chat", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:sound-toggle", deck_sound_toggle);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_sound_volume[] = {
		{Localizable("Background music volume"), nullptr, "背景音乐音量"},
		{Localizable("Chat sound volume"), nullptr, "聊天提示音量"},
		{Localizable("Game sound volume"), nullptr, "游戏音效音量"},
		{Localizable("Map sound volume"), nullptr, "地图音效音量"},
		{Localizable("Sound volume"), nullptr, "音量"},
		{nullptr, "snd_ambient_volume", nullptr},
		{nullptr, "snd_background_music_volume", nullptr},
		{nullptr, "snd_chat_volume", nullptr},
		{nullptr, "snd_enable", nullptr},
		{nullptr, "snd_game_volume", nullptr},
		{nullptr, "snd_volume", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:sound-volume", deck_sound_volume);
}
