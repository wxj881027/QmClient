// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_profiles[] = {
		{Localizable("After Load"), nullptr, "加载后"},
		{Localizable("Clan: %s"), nullptr, "战队：%s"},
		{Localizable("Enable Deleting"), nullptr, "启用删除"},
		{Localizable("Name: %s"), nullptr, "名称：%s"},
		{Localizable("Skin: %s"), nullptr, "皮肤：%s"},
		{Localizable("Your profile"), nullptr, "您的个人资料"},
		{Localizable("Bind Wheel"), nullptr, "绑定轮盘"},
		{nullptr, "qm_profile_clan", nullptr},
		{nullptr, "qm_profile_colors", nullptr},
		{nullptr, "qm_profile_emote", nullptr},
		{nullptr, "qm_profile_flag", nullptr},
		{nullptr, "qm_profile_name", nullptr},
		{nullptr, "qm_profile_overwrite_clan_with_empty", nullptr},
		{nullptr, "qm_profile_skin", nullptr},
		{Localizable("Custom colors"), nullptr, "自定义颜色"},
		{Localizable("Dummy"), nullptr, "分身"},
		{Localizable("Overwrite clan even if empty"), nullptr, "即使战队为空也覆盖"},
		{Localizable("Save/Load Clan"), nullptr, "保存/加载战队"},
		{Localizable("Save/Load Colors"), nullptr, "保存/加载颜色"},
		{Localizable("Save/Load Emote"), nullptr, "保存/加载表情"},
		{Localizable("Save/Load Flag"), nullptr, "保存/加载国旗"},
		{Localizable("Save/Load Name"), nullptr, "保存/加载名称"},
		{Localizable("Save/Load Skin"), nullptr, "保存/加载皮肤"},
		{Localizable("Profiles file"), nullptr, "配置文件"},
		{Localizable("Identity preset"), nullptr, "身份 ID 名字 昵称"},
		{Localizable("Chat phrase"), nullptr, "语录 快捷用语"},
		{Localizable("Asset preset"), nullptr, "资源 贴图"},
		{Localizable("Bind Wheel Key"), nullptr, "轮盘 按键"},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-profiles", deck_tclient_profiles);
}
