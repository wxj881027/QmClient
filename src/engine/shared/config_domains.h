// This file can be included several times.

#ifndef CONFIG_DOMAIN
#error "CONFIG_DOMAIN macro not defined"
#define CONFIG_DOMAIN(Name, ConfigPath, PreviousConfigPath, LegacyConfigPath, HasVars) ;
#endif

// QmClient v3：全部变量（DDNet 官方 + TClient + QmClient）合并进单一文件 qmclient/settings.cfg。
// 根目录 settings_ddnet.cfg 保留给官方 DDNet/TClient 客户端共享使用，QmClient 只在其作为导入源时读取。
CONFIG_DOMAIN(QMCLIENT, "qmclient/settings.cfg", nullptr, nullptr, true)
// 以下三个无变量域（纯命令流：皮肤配置/聊天绑定/战争名单）保持在 qmclient/ 下；
// Previous/Legacy 路径仅用于 v3 一次性迁移读取，迁移完成后旧文件会被删除。
CONFIG_DOMAIN(TCLIENTPROFILES, "qmclient/qmclient_profiles.cfg", "QmClient/qmclient_profiles.cfg", "qmclient_profiles.cfg", false)
CONFIG_DOMAIN(TCLIENTCHATBINDS, "qmclient/qmclient_chatbinds.cfg", "QmClient/qmclient_chatbinds.cfg", "qmclient_chatbinds.cfg", false)
CONFIG_DOMAIN(TCLIENTWARLIST, "qmclient/qmclient_warlist.cfg", "QmClient/qmclient_warlist.cfg", "qmclient_warlist.cfg", false)
