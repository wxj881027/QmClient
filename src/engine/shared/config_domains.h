// This file can be included several times.

#ifndef CONFIG_DOMAIN
#error "CONFIG_DOMAIN macro not defined"
#define CONFIG_DOMAIN(Name, ConfigPath, HasVars) ;
#endif

CONFIG_DOMAIN(DDNET, "settings_ddnet.cfg", true)
CONFIG_DOMAIN(QMCLIENT, "settings_qmclient.cfg", true)
CONFIG_DOMAIN(TCLIENTPROFILES, "qmclient_profiles.cfg", false)
CONFIG_DOMAIN(TCLIENTCHATBINDS, "qmclient_chatbinds.cfg", false)
CONFIG_DOMAIN(TCLIENTWARLIST, "qmclient_warlist.cfg", false)
