#ifndef QM_UPDATE_UPDATER_SESSION_H
#define QM_UPDATE_UPDATER_SESSION_H

#include "updater_arguments.h"

namespace QmUpdate
{
	// 精确删除会话拥有的下载附件；单个文件失败不能阻止其他附件回收。
	inline bool CleanupDownloadedFiles(const SArguments &Arguments)
	{
		bool Success = true;
		for(const auto *pPath : {&Arguments.m_Package, &Arguments.m_PackageSignature, &Arguments.m_Manifest, &Arguments.m_ManifestSignature})
		{
			if(pPath->empty())
				continue;
			std::error_code Error;
			std::filesystem::remove(std::filesystem::path(*pPath), Error);
			Success &= !Error;
		}
		return Success;
	}

	enum class ESetupResult
	{
		SUCCEEDED,
		FAILED,
		STILL_RUNNING,
	};

	// 只有持有安装器的会话负责清理；等待异常时保留运行中安装器的文件。
	template<typename TVerify, typename TRun, typename TCleanup>
	ESetupResult RunSetupSession(TVerify Verify, TRun Run, TCleanup Cleanup)
	{
		const ESetupResult Result = Verify() ? Run() : ESetupResult::FAILED;
		if(Result != ESetupResult::STILL_RUNNING)
			Cleanup();
		return Result;
	}
}

#endif
