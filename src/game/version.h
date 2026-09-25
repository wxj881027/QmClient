/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_VERSION_H
#define GAME_VERSION_H

// ddnet
#define GAME_NAME "DDNet"
#define DDNET_VERSION_NUMBER 20000
extern const char *GIT_SHORTREV_HASH;
#ifndef GAME_RELEASE_VERSION_INTERNAL
#define GAME_RELEASE_VERSION_INTERNAL 20.0
#endif
#define GAME_RELEASE_VERSION STRINGIFY(GAME_RELEASE_VERSION_INTERNAL)

// teeworlds
#define CLIENT_VERSION7 0x0705
// For compatibility with DDNet client 15.8 and older we need to include the prefix `0.6` in the version string
// because this was used for a "Compatible version" filter in the server browser.
#define GAME_VERSION "0.6, " GAME_RELEASE_VERSION
#define GAME_NETVERSION "0.6 626fce9a778df4d4"
#define GAME_NETVERSION7 "0.7 802f1be60a05665f"

// QmClient
// 正式版本按 V3、V3.1 递增；开发测试版本独立使用 V3.13.0 格式。
#define QMCLIENT_STABLE_VERSION "3"
#define QMCLIENT_DEV_VERSION "3.13.0"

#if defined(QMCLIENT_STABLE_BUILD)
#define QMCLIENT_VERSION QMCLIENT_STABLE_VERSION
#define QMCLIENT_IS_DEVELOPMENT_BUILD 0
#else
#define QMCLIENT_VERSION QMCLIENT_DEV_VERSION
#define QMCLIENT_IS_DEVELOPMENT_BUILD 1
#endif

#define CLIENT_NAME "QmClient"
#define CLIENT_RELEASE_VERSION "V" QMCLIENT_VERSION

#endif
