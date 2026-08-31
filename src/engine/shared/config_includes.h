// This file can be included several times.

#ifndef SET_CONFIG_DOMAIN
#error "SET_CONFIG_DOMAIN macro not defined"
#define SET_CONFIG_DOMAIN(CONFIGDOMAIN) ;
#endif

// QmClient v3：全部变量（DDNet 官方 + TClient + QmClient）合并进单一 QMCLIENT 域，
// 统一保存到 qmclient/settings.cfg。
SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)
#include "config_variables.h"

SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)
#include "config_variables_tclient.h"

SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)
#include "config_variables_qmclient.h"
