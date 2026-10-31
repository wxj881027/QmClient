#ifndef ENGINE_CLIENT_UPDATER_FILE_H
#define ENGINE_CLIENT_UPDATER_FILE_H

#include <base/str.h>

#include <engine/http.h>
#include <engine/storage.h>
#include <engine/updater.h>

// 仅用于旧更新入口，不改变签名包安装链。
namespace QmLegacyUpdater
{
	inline bool CanInitiate(IUpdater::EUpdaterState State, bool RequestActive) noexcept
	{
		return !RequestActive && (State == IUpdater::CLEAN || State == IUpdater::FAIL || State == IUpdater::NEED_RESTART);
	}

	inline IUpdater::EUpdaterState DownloadState(EHttpState State) noexcept
	{
		if(State == EHttpState::DONE)
			return IUpdater::MOVE_FILES;
		if(State == EHttpState::QUEUED || State == EHttpState::RUNNING)
			return IUpdater::DOWNLOADING;
		return IUpdater::FAIL;
	}

	inline bool FormatPlatformLibraryName(const char *pFile, size_t ExtensionLength, const char *pPlatform, char *pOut, unsigned OutSize) noexcept
	{
		if(OutSize == 0)
			return false;
		pOut[0] = '\0';
		const size_t Length = str_length(pFile);
		const size_t PlatformLength = str_length(pPlatform);
		if(ExtensionLength > Length || OutSize < 2 || Length > OutSize - 2 || PlatformLength > OutSize - 2 - Length)
			return false;
		str_format(pOut, OutSize, "%.*s-%s%s", static_cast<int>(Length - ExtensionLength), pFile, pPlatform, pFile + Length - ExtensionLength);
		return true;
	}

	class CStorageFiles
	{
		IStorage &m_Storage;

	public:
		explicit CStorageFiles(IStorage &Storage) :
			m_Storage(Storage) {}
		bool Exists(const char *pPath) { return m_Storage.FileExists(pPath, IStorage::TYPE_ABSOLUTE); }
		bool FolderExists(const char *pPath) { return m_Storage.FolderExists(pPath, IStorage::TYPE_ABSOLUTE); }
		bool Rename(const char *pOld, const char *pNew) { return m_Storage.RenameFile(pOld, pNew, IStorage::TYPE_ABSOLUTE, false); }
		bool Remove(const char *pPath) { return m_Storage.RemoveFile(pPath, IStorage::TYPE_ABSOLUTE); }
	};

	// 单文件替换事务；已有恢复备份不覆盖，恢复失败继续保留备份和候选。
	// Files 替换文件系统边界，正常 Rename 成功前不得修改源/目标内容。
	template<typename TFiles>
	bool ReplaceFile(TFiles &Files, const char *pCandidate, const char *pDestination, const char *pBackup) noexcept
	{
		bool MovedOld = false;
		bool Installed = false;
		auto Restore = [&]() noexcept {
			if(MovedOld && !Installed)
			{
				try
				{
					Files.Rename(pBackup, pDestination);
				}
				catch(...)
				{
				}
			}
		};
		try
		{
			if(!Files.Exists(pCandidate) || Files.FolderExists(pDestination) || Files.Exists(pBackup) || Files.FolderExists(pBackup))
				return false;
			if(Files.Exists(pDestination))
			{
				if(!Files.Rename(pDestination, pBackup))
					return false;
				MovedOld = true;
			}
			if(!Files.Rename(pCandidate, pDestination))
			{
				Restore();
				return false;
			}
			Installed = true;
			// 安装已完成，清理失败仅留下恢复副本，不回滚新文件。
			if(MovedOld)
				Files.Remove(pBackup);
			return true;
		}
		catch(...)
		{
			Restore();
			return Installed;
		}
	}
}

#endif
