// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_steam[] = {
		{Localizable("Checking Steam..."), nullptr, "正在检测 Steam…"},
		{Localizable("Detect Steam again"), nullptr, "重新检测 Steam"},
		{Localizable("Enter the Steam client program path, without command arguments"), nullptr, "填写 Steam 客户端程序路径，不要附带启动参数"},
		{Localizable("Launch Steam automatically when starting externally"), nullptr, "从外部启动时自动启动 Steam"},
		{Localizable("Manual Steam path is invalid; using automatic detection"), nullptr, "手动填写的 Steam 路径无效，已使用自动检测"},
		{Localizable("Steam client path (leave empty to detect automatically)"), nullptr, "Steam 客户端路径（留空自动检测）"},
		{Localizable("Steam is installed"), nullptr, "Steam 已安装"},
		{Localizable("Steam is installed but not running"), nullptr, "Steam 已安装，尚未启动"},
		{Localizable("Steam is running"), nullptr, "Steam 已运行"},
		{Localizable("Steam not found"), nullptr, "未找到 Steam"},
		{nullptr, "qm_steam_auto_launch", nullptr},
		{nullptr, "qm_steam_client_path", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:steam", qm_steam);
}
