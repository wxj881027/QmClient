// This file can be included several times.

#ifndef CONFIG_DOMAIN
#error "CONFIG_DOMAIN macro not defined"
#define CONFIG_DOMAIN(Name, ConfigPath, LegacyConfigPath, HasVars) ;
#endif

CONFIG_DOMAIN(DDNET, "settings_ddnet.cfg", nullptr, true)
CONFIG_DOMAIN(QMCLIENT, "QmClient/settings_qmclient.cfg", "settings_qmclient.cfg", true)
CONFIG_DOMAIN(TCLIENTPROFILES, "QmClient/qmclient_profiles.cfg", "qmclient_profiles.cfg", false)
CONFIG_DOMAIN(TCLIENTCHATBINDS, "QmClient/qmclient_chatbinds.cfg", "qmclient_chatbinds.cfg", false)
CONFIG_DOMAIN(TCLIENTWARLIST, "QmClient/qmclient_warlist.cfg", "qmclient_warlist.cfg", false)
