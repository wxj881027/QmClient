// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_display[] = {
		{Localizable("Allows maps to render with more detail"), nullptr, "允许地图渲染更多细节"},
		{Localizable("Compatibility mode"), nullptr, "兼容模式"},
		{Localizable("FSAA samples"), nullptr, "全屏抗锯齿（FSAA）采样倍数"},
		{Localizable("Graphics card"), nullptr, "显卡"},
		{Localizable("Graphics mode"), nullptr, "图形模式"},
		{Localizable("High Detail"), nullptr, "显示地图细节"},
		{Localizable("Hz", "Hertz"), nullptr, "Hz", "Hertz"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Performance mode"), nullptr, "性能模式"},
		{Localizable("Refresh Rate"), nullptr, "刷新率"},
		{Localizable("Renders your frame rate in the top right"), nullptr, "在右上角显示帧率"},
		{Localizable("Screen"), nullptr, "屏幕"},
		{Localizable("Show FPS"), nullptr, "显示 FPS"},
		{Localizable("V-Sync"), nullptr, "垂直同步"},
		{Localizable("auto"), nullptr, "自动"},
		{Localizable("custom"), nullptr, "自定义"},
		{Localizable("may cause delay"), nullptr, "可能会产生延迟"},
		{nullptr, "cl_languagefile", nullptr},
		{nullptr, "cl_showfps", nullptr},
		{nullptr, "gfx_backend", nullptr},
		{nullptr, "gfx_fsaa_samples", nullptr},
		{nullptr, "gfx_gl_major", nullptr},
		{nullptr, "gfx_gl_minor", nullptr},
		{nullptr, "gfx_gl_patch", nullptr},
		{nullptr, "gfx_gpu_name", nullptr},
		{nullptr, "gfx_high_detail", nullptr},
		{nullptr, "gfx_refresh_rate", nullptr},
		{nullptr, "gfx_screen", nullptr},
		{nullptr, "gfx_vsync", nullptr},
		{nullptr, "qm_graphics_mode", nullptr},
		{nullptr, "qm_vulkan_api_version", nullptr},
		{Localizable("Could not open system graphics settings."), nullptr, "无法打开系统图形设置。"},
		{Localizable("Current GPU"), nullptr, "当前显卡"},
		{Localizable("Current driver"), nullptr, "当前驱动"},
		{Localizable("Current graphics backend"), nullptr, "当前图形后端"},
		{Localizable("Error"), nullptr, "错误"},
		{Localizable("Ok"), nullptr, "确定"},
		{Localizable("Unknown"), nullptr, "未知"},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-display", deck_graphics_display);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_icons[] = {
		{Localizable("Black"), nullptr, "黑色"},
		{Localizable("Bold"), nullptr, "粗体"},
		{Localizable("Fill"), nullptr, "填充"},
		{Localizable("Light"), nullptr, "轻体"},
		{Localizable("Rainbow"), nullptr, "彩虹"},
		{Localizable("Regular"), nullptr, "常规"},
		{Localizable("UI icon color"), nullptr, "界面图标颜色"},
		{Localizable("UI icon custom color"), nullptr, "界面图标自定义颜色"},
		{Localizable("UI icon style"), nullptr, "界面图标样式"},
		{Localizable("Use custom UI icon color"), nullptr, "使用自定义图标颜色"},
		{Localizable("White"), nullptr, "白色"},
		{Localizable("Solid friend icon"), nullptr, "实心好友图标"},
		{Localizable("Solid favorite icon"), nullptr, "实心收藏图标"},
		{Localizable("Quality tier"), nullptr, "按连接质量"},
		{Localizable("Follow UI icon color"), nullptr, "跟随界面图标颜色"},
		{Localizable("Connection icon color"), nullptr, "连接图标颜色"},
		{Localizable("Connection icon custom color"), nullptr, "连接图标自定义颜色"},
		{Localizable("Fade"), nullptr, "淡入淡出"},
		{Localizable("Morph"), nullptr, "形变"},
		{Localizable("Password icon animation"), nullptr, "密码图标动画"},
		{nullptr, "qm_ui_icon_color", nullptr},
		{nullptr, "qm_ui_icon_custom_color", nullptr},
		{nullptr, "qm_ui_icon_custom_color_enabled", nullptr},
		{nullptr, "qm_ui_icon_weight", nullptr},
		{Localizable("Favorite icon color"), nullptr, "收藏图标颜色"},
		{Localizable("Friend icon color"), nullptr, "好友图标颜色"},
		{nullptr, "qm_ui_favorite_icon_color", nullptr},
		{nullptr, "qm_ui_friend_icon_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-icons", deck_graphics_icons);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_interaction[] = {
		{Localizable("Animate chat box, emote selector, scoreboard, and spectate selection"), nullptr, "聊天框、表情选择、计分板和观战选择的展示动画"},
		{Localizable("Animate settings card expand and collapse height changes"), nullptr, "展开或折叠设置卡片时播放高度过渡动画"},
		{Localizable("Animate settings card lists when entering a page"), nullptr, "进入设置页面时播放卡片列表入场动画"},
		{Localizable("Animate settings card reorder and layout reflow"), nullptr, "拖动排序或布局重排时播放卡片位置过渡动画"},
		{Localizable("Card height animation"), nullptr, "设置卡片高度过渡动画"},
		{Localizable("Card list entry animation"), nullptr, "设置卡片列表入场动画"},
		{Localizable("Card reflow animation"), nullptr, "设置卡片重排动画"},
		{Localizable("Full"), nullptr, "完整"},
		{Localizable("Full: each enabled animation category uses its complete transition"), nullptr, "完整：按下方各项开关播放完整过渡和展示动画"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Off: disables all interface animations while preserving the options below"), nullptr, "关闭：停用全部界面动画，但保留下面各项开关的设置"},
		{Localizable("Presentation animations"), nullptr, "界面展示动画"},
		{Localizable("Reduced"), nullptr, "精简"},
		{Localizable("Reduced: uses shorter transitions and disables presentation animations"), nullptr, "精简：保留更短的过渡效果，并停用展示动画"},
		{Localizable("This is the master switch for all interface animation categories"), nullptr, "这是所有界面动画类别的总开关"},
		{Localizable("UI motion level"), nullptr, "界面动效强度"},
		{Localizable("Scroll feel"), nullptr, "滚动手感、平滑滚动、弹簧滚动"},
		{Localizable("Every wheel tick restarts from rest, matching the original client"), nullptr, "原始滚动 Original"},
		{Localizable("Consecutive wheel ticks carry their velocity into one continuous glide"), nullptr, "平滑滚动 Smooth"},
		{Localizable("A damped spring drives the offset; reversing slows it before it turns"), nullptr, "弹簧滚动 Spring"},
		{Localizable("Spring response"), nullptr, "弹簧响应"},
		{Localizable("Spring damping"), nullptr, "弹簧阻尼"},
		{Localizable("End bounce"), nullptr, "末端回弹"},
		{Localizable("Scroll step"), nullptr, "滚动步长"},
		{Localizable("Wheel distance per tick; hold Alt for 3x"), nullptr, "每格滚轮距离；按住 Alt 为 3 倍"},
		{nullptr, "qm_extra_animations", nullptr},
		{nullptr, "qm_ui_card_height_animations", nullptr},
		{nullptr, "qm_ui_card_reflow_animations", nullptr},
		{nullptr, "qm_ui_list_entry_animations", nullptr},
		{nullptr, "qm_ui_motion_level", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-interaction", deck_graphics_interaction);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_modes[] = {
		{Localizable("Current"), nullptr, "当前"},
		{Localizable("Desktop fullscreen"), nullptr, "无边框全屏"},
		{Localizable("Fullscreen"), nullptr, "独占全屏"},
		{Localizable("Windowed"), nullptr, "窗口化"},
		{Localizable("Windowed borderless"), nullptr, "无边框窗口"},
		{Localizable("Windowed fullscreen"), nullptr, "窗口化全屏"},
		{nullptr, "gfx_color_depth", nullptr},
		{nullptr, "gfx_screen_height", nullptr},
		{nullptr, "gfx_screen_refresh_rate", nullptr},
		{nullptr, "gfx_screen_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-modes", deck_graphics_modes);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_visual[] = {
		{Localizable("Auto"), nullptr, "自动"},
		{Localizable("Black"), nullptr, "黑色"},
		{Localizable("Blur mode"), nullptr, "模糊模式"},
		{Localizable("Button background color"), nullptr, "按钮背景颜色"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Custom text color"), nullptr, "自定义文本颜色"},
		{Localizable("Dual Kawase"), nullptr, "双重 Kawase"},
		{Localizable("Enable backdrop blur"), nullptr, "启用背景模糊"},
		{Localizable("Expanded dropdown background color"), nullptr, "展开列表背景颜色"},
		{Localizable("Fast, lightweight blur with a softer approximation."), nullptr, "快速、轻量的模糊效果，采用较柔和的近似处理。"},
		{Localizable("Gaussian"), nullptr, "高斯"},
		{Localizable("Input background color"), nullptr, "输入框背景颜色"},
		{Localizable("Interface accent color"), nullptr, "界面强调色"},
		{Localizable("Interface surface"), nullptr, "界面表面"},
		{Localizable("Kawase"), nullptr, "Kawase"},
		{Localizable("Multi-resolution blur with a stronger result and balanced GPU cost."), nullptr, "多分辨率模糊，效果更强，并兼顾 GPU 开销。"},
		{Localizable("Rainbow card titles"), nullptr, "卡片标题彩虹流动"},
		{Localizable("Scoreboard surface"), nullptr, "计分板表面"},
		{Localizable("Secondary menu background color"), nullptr, "二级菜单背景颜色"},
		{Localizable("Selected item color"), nullptr, "选中项颜色"},
		{Localizable("Settings card background"), nullptr, "设置卡片背景"},
		{Localizable("Settings card border color"), nullptr, "设置卡片边框颜色"},
		{Localizable("Show settings card borders"), nullptr, "显示设置卡片边框"},
		{Localizable("Smooth and accurate blur with the highest GPU cost."), nullptr, "平滑且准确的模糊效果，但 GPU 开销最高。"},
		{Localizable("Text color mode"), nullptr, "文本颜色模式"},
		{Localizable("Text input focus ring color"), nullptr, "文本输入焦点环颜色"},
		{Localizable("Used by active shared text and numeric input fields"), nullptr, "用于已激活的共享文本与数值输入框"},
		{Localizable("White"), nullptr, "白色"},
		{nullptr, "qm_blur_mode", nullptr},
		{nullptr, "qm_gaussian_blur", nullptr},
		{nullptr, "qm_scoreboard_color", nullptr},
		{nullptr, "qm_scoreboard_opacity", nullptr},
		{nullptr, "qm_ui_accent_color", nullptr},
		{nullptr, "qm_ui_accent_opacity", nullptr},
		{nullptr, "qm_ui_card_border_color", nullptr},
		{nullptr, "qm_ui_card_borders", nullptr},
		{nullptr, "qm_ui_card_color", nullptr},
		{nullptr, "qm_ui_card_opacity", nullptr},
		{nullptr, "qm_ui_card_rainbow_titles", nullptr},
		{nullptr, "qm_ui_color", nullptr},
		{nullptr, "qm_ui_dropdown_color", nullptr},
		{nullptr, "qm_ui_dropdown_list_color", nullptr},
		{nullptr, "qm_ui_dropdown_list_opacity", nullptr},
		{nullptr, "qm_ui_dropdown_opacity", nullptr},
		{nullptr, "qm_ui_focus_color", nullptr},
		{nullptr, "qm_ui_input_color", nullptr},
		{nullptr, "qm_ui_input_opacity", nullptr},
		{nullptr, "qm_ui_opacity", nullptr},
		{nullptr, "qm_ui_popup_color", nullptr},
		{nullptr, "qm_ui_popup_opacity", nullptr},
		{nullptr, "qm_ui_selected_color", nullptr},
		{nullptr, "qm_ui_text_color_mode", nullptr},
		{nullptr, "qm_ui_text_custom_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-visual", deck_graphics_visual);
}
