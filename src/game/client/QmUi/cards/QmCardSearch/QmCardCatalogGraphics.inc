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
		{Localizable("Render frame rate limit"), nullptr, "渲染帧率上限"},
		{Localizable("Client update rate"), nullptr, "客户端更新频率"},
		{Localizable("Cap update rate to save power"), nullptr, "限制更新频率以节约功耗"},
		{Localizable("Renders your frame rate in the top right"), nullptr, "在右上角显示帧率"},
		{Localizable("Screen"), nullptr, "屏幕"},
		{Localizable("Show FPS"), nullptr, "显示 FPS"},
		{Localizable("V-Sync"), nullptr, "垂直同步"},
		{Localizable("auto"), nullptr, "自动"},
		{Localizable("custom"), nullptr, "自定义"},
		{Localizable("may cause delay"), nullptr, "可能会产生延迟"},
		{nullptr, "cl_languagefile", nullptr},
		{nullptr, "cl_refresh_rate", nullptr},
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
		{Localizable("Graphics hardware details"), nullptr, "图形硬件详情"},
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
		{Localizable("Full"), nullptr, "完整"},
		{Localizable("Full: uses the complete transition everywhere"), nullptr, "完整：所有过渡使用完整时长"},
		{Localizable("Off"), nullptr, "关"},
		{Localizable("Off: disables every interface animation"), nullptr, "关闭：停用全部界面动画"},
		{Localizable("Reduced"), nullptr, "精简"},
		{Localizable("Reduced: uses shorter transitions everywhere"), nullptr, "精简：所有过渡使用更短时长"},
		{Localizable("This is the master switch for all interface animations"), nullptr, "这是所有界面动画的总开关"},
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
		// 输入光标样式：设置页、控制台与聊天输入共用同一个光标组件，检索词要覆盖三个档位名。
		{Localizable("Bar"), nullptr, "竖线"},
		{Localizable("Block"), nullptr, "方块"},
		{Localizable("Text caret style"), nullptr, "输入光标样式"},
		{Localizable("Underscore"), nullptr, "下划线"},
		{Localizable("Bar: a thin vertical line between characters"), nullptr, "竖线：字符之间的一条细竖线"},
		{Localizable("Block: fills the character cell behind the next character"), nullptr, "方块：填充下一个字符所在的字符格"},
		{Localizable("Underscore: a line under the character that follows"), nullptr, "下划线：画在下一个字符下方的一条线"},
		{nullptr, "qm_text_caret_style", nullptr},
		{nullptr, "qm_ui_motion_level", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-interaction", deck_graphics_interaction);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_modes[] = {
		{Localizable("Current"), nullptr, "当前"},
		{Localizable("Current resolution"), nullptr, "当前分辨率"},
		{Localizable("Desktop fullscreen"), nullptr, "无边框全屏"},
		{Localizable("Game window mode"), nullptr, "游戏窗口模式"},
		{Localizable("Fullscreen"), nullptr, "独占全屏"},
		{Localizable("Windowed"), nullptr, "窗口化"},
		{Localizable("Windowed borderless"), nullptr, "无边框窗口"},
		{Localizable("Windowed fullscreen"), nullptr, "窗口化全屏"},
		{nullptr, "gfx_borderless", nullptr},
		{nullptr, "gfx_fullscreen", nullptr},
		{nullptr, "gfx_screen_height", nullptr},
		{nullptr, "gfx_screen_refresh_rate", nullptr},
		{nullptr, "gfx_screen_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-modes", deck_graphics_modes);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	// 「卡片效果」：卡片形状（圆角曲线 / 曲率 / 胶囊端头）与卡片自身的背景、边框、标题效果。
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_card_effects[] = {
		{Localizable("Rainbow card titles"), nullptr, "卡片标题彩虹流动"},
		{Localizable("Settings card background"), nullptr, "设置卡片背景"},
		{Localizable("Settings card border color"), nullptr, "设置卡片边框颜色"},
		{Localizable("Show settings card borders"), nullptr, "显示设置卡片边框"},
		{nullptr, "qm_ui_card_border_color", nullptr},
		{nullptr, "qm_ui_card_borders", nullptr},
		{nullptr, "qm_ui_card_color", nullptr},
		{nullptr, "qm_ui_card_opacity", nullptr},
		{nullptr, "qm_ui_card_rainbow_titles", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-card-effects", deck_graphics_card_effects);

	// 「界面主题」：整块界面共用的表面颜色、文本颜色模式与模糊。
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_graphics_theme[] = {
		{Localizable("Apple capsule"), nullptr, "苹果胶囊"},
		{Localizable("Apple"), nullptr, "苹果"},
		{Localizable("Black"), nullptr, "黑色"},
		{Localizable("Blur mode"), nullptr, "模糊模式"},
		{Localizable("Capsule end strength"), nullptr, "胶囊端部强度"},
		{Localizable("Capsule ends"), nullptr, "胶囊端头"},
		{Localizable("Classic arc"), nullptr, "经典圆弧"},
		{Localizable("Conic Bezier"), nullptr, "圆锥贝塞尔"},
		{Localizable("Control background color"), nullptr, "控件背景颜色"},
		{Localizable("Corner curvature"), nullptr, "圆角曲率"},
		{Localizable("Corner curve"), nullptr, "圆角曲线"},
		{Localizable("Custom text color"), nullptr, "自定义文本颜色"},
		{Localizable("Custom"), nullptr, "自定义"},
		{Localizable("Dual Kawase"), nullptr, "双重 Kawase"},
		{Localizable("Enable backdrop blur"), nullptr, "启用背景模糊"},
		{Localizable("Enable focus glow"), nullptr, "启用聚焦光晕"},
		{Localizable("Fast, lightweight blur with a softer approximation."), nullptr, "快速、轻量的模糊效果，采用较柔和的近似处理。"},
		{Localizable("Gaussian"), nullptr, "高斯"},
		{Localizable("Interface accent color"), nullptr, "界面强调色"},
		{Localizable("Interface surface"), nullptr, "界面表面"},
		{Localizable("Kawase"), nullptr, "Kawase"},
		{Localizable("Multi-resolution blur with a stronger result and balanced GPU cost."), nullptr, "多分辨率模糊，效果更强，并兼顾 GPU 开销。"},
		{Localizable("Plain capsule"), nullptr, "普通胶囊"},
		{Localizable("Popup background color"), nullptr, "弹层背景颜色"},
		{Localizable("Quintic Bezier"), nullptr, "五次贝塞尔"},
		{Localizable("Rainbow"), nullptr, "彩虹色"},
		{Localizable("Silky capsule"), nullptr, "丝滑胶囊"},
		{Localizable("Smooth and accurate blur with the highest GPU cost."), nullptr, "平滑且准确的模糊效果，但 GPU 开销最高。"},
		{Localizable("Superellipse"), nullptr, "超椭圆"},
		{Localizable("Text color mode"), nullptr, "文本颜色模式"},
		{Localizable("White"), nullptr, "白色"},
		{nullptr, "qm_blur_mode", nullptr},
		{nullptr, "qm_gaussian_blur", nullptr},
		{nullptr, "qm_ui_accent_color", nullptr},
		{nullptr, "qm_ui_accent_opacity", nullptr},
		{nullptr, "qm_ui_capsule_apple_strength", nullptr},
		{nullptr, "qm_ui_capsule_style", nullptr},
		{nullptr, "qm_ui_color", nullptr},
		{nullptr, "qm_ui_control_color", nullptr},
		{nullptr, "qm_ui_control_opacity", nullptr},
		{nullptr, "qm_ui_corner_curve", nullptr},
		{nullptr, "qm_ui_corner_smoothing", nullptr},
		{nullptr, "qm_ui_corner_smoothing_apple", nullptr},
		{nullptr, "qm_ui_corner_smoothing_conic", nullptr},
		{nullptr, "qm_ui_corner_smoothing_squircle", nullptr},
		{nullptr, "qm_ui_focus_glow", nullptr},
		{nullptr, "qm_ui_layer_color", nullptr},
		{nullptr, "qm_ui_layer_opacity", nullptr},
		{nullptr, "qm_ui_opacity", nullptr},
		{nullptr, "qm_ui_text_color_mode", nullptr},
		{nullptr, "qm_ui_text_custom_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:graphics-theme", deck_graphics_theme);
}
