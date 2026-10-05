#ifndef ENGINE_CLIENT_QM_STORAGE_MODE_H
#define ENGINE_CLIENT_QM_STORAGE_MODE_H

#include <engine/storage.h>

// 客户端存储模式由构建决定，发布二进制不提供运行时切换入口。
constexpr IStorage::EInitializationType QmClientStorageModeForBuild(bool Portable, bool IsolatedTest = false)
{
	return Portable ? IStorage::EInitializationType::CLIENT_PORTABLE :
			  (IsolatedTest ? IStorage::EInitializationType::CLIENT_TEST : IStorage::EInitializationType::CLIENT);
}

constexpr bool IsQmClientPortableBuild()
{
#if defined(CONF_QMCLIENT_PORTABLE)
	return true;
#else
	return false;
#endif
}

constexpr IStorage::EInitializationType QmClientStorageMode()
{
#if defined(CONF_QMCLIENT_TEST_STORAGE)
	return QmClientStorageModeForBuild(false, true);
#else
	return QmClientStorageModeForBuild(IsQmClientPortableBuild());
#endif
}

#endif
