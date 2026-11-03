// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_controller[] = {
		{Localizable("Absolute", "Ingame controller mode"), nullptr, "绝对", "Ingame controller mode"},
		{Localizable("Aim bind"), nullptr, "操作轴绑定"},
		{Localizable("Axis"), nullptr, "轴"},
		{Localizable("Controller"), nullptr, "手柄"},
		{Localizable("Controller jitter tolerance"), nullptr, "摇杆死区"},
		{Localizable("Enable controller"), nullptr, "启用控制器"},
		{Localizable("Ingame controller mode"), nullptr, "游戏内手柄瞄准模式"},
		{Localizable("Ingame controller sens."), nullptr, "游戏内摇杆灵敏度"},
		{Localizable("No controller found. Plug in a controller."), nullptr, "未检测到任何手柄。请连接手柄。"},
		{Localizable("Relative", "Ingame controller mode"), nullptr, "相对", "Ingame controller mode"},
		{Localizable("Status"), nullptr, "状态"},
		{Localizable("UI controller sens."), nullptr, "摇杆灵敏度（界面）"},
		{nullptr, "inp_controller_absolute", nullptr},
		{nullptr, "inp_controller_enable", nullptr},
		{nullptr, "inp_controller_sens", nullptr},
		{nullptr, "inp_controller_tolerance", nullptr},
		{nullptr, "inp_controller_x", nullptr},
		{nullptr, "inp_controller_y", nullptr},
		{nullptr, "ui_controller_sens", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-controller", deck_controls_controller);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_controls_mouse[] = {
		{Localizable("Ingame mouse sens."), nullptr, "游戏内鼠标灵敏度"},
		{Localizable("Maximum cursor distance"), nullptr, "最大光标距离"},
		{Localizable("Minimum cursor distance"), nullptr, "最小光标距离"},
		{Localizable("UI mouse sens."), nullptr, "鼠标灵敏度（界面）"},
		{nullptr, "cl_mouse_max_distance", nullptr},
		{nullptr, "cl_mouse_min_distance", nullptr},
		{nullptr, "inp_mousesens", nullptr},
		{nullptr, "ui_mousesens", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:controls-mouse", deck_controls_mouse);
}
