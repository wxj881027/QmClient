#include "demoedit.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/shared/demo.h>
#include <engine/shared/demo_output.h>
#include <engine/storage.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	bool IsCapturedFile(IOHANDLE Original, const char *pPath)
	{
		std::unique_ptr<void, decltype(&io_close)> Other(io_open_read_regular_checked(pPath, nullptr), &io_close);
		if(!Other)
			return false;
#if defined(CONF_FAMILY_WINDOWS)
		BY_HANDLE_FILE_INFORMATION A{}, B{};
		const auto Info = [](IOHANDLE File, BY_HANDLE_FILE_INFORMATION &Result) {
			return GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(static_cast<FILE *>(File)))), &Result) != 0;
		};
		return Info(Original, A) && Info(Other.get(), B) && (A.nFileIndexHigh || A.nFileIndexLow) &&
		       A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh && A.nFileIndexLow == B.nFileIndexLow;
#else
		struct stat A{}, B{};
		return fstat(fileno(static_cast<FILE *>(Original)), &A) == 0 && fstat(fileno(static_cast<FILE *>(Other.get())), &B) == 0 &&
		       A.st_dev == B.st_dev && A.st_ino == B.st_ino;
#endif
	}

	class CReplaySource
	{
		std::filesystem::path m_Original;
		std::filesystem::path m_Directory;
		std::filesystem::path m_Private;
		std::string m_Filename;
		bool m_OwnDirectory = false;
		bool m_Restore = false;

	public:
		explicit CReplaySource(const char *pSource) :
			m_Original(std::filesystem::u8path(pSource)) {}
		bool Claim(IOHANDLE Original, const char *pSource)
		{
			if(!IsCapturedFile(Original, pSource))
				return false;
			std::error_code Error;
			const std::string Prefix = std::string(pSource) + ".saving";
			for(int Attempt = 0; Attempt < 64; ++Attempt)
			{
				const auto Directory = Attempt ? Prefix + "." + std::to_string(Attempt) : Prefix;
				m_Filename = Directory + "/source.demo";
				if(m_Filename.size() >= IO_MAX_PATH_LENGTH)
					return false;
				m_Directory = std::filesystem::u8path(Directory);
				m_Private = std::filesystem::u8path(m_Filename);
				m_OwnDirectory = std::filesystem::create_directory(m_Directory, Error);
				if(m_OwnDirectory || (Error && Error != std::errc::file_exists))
					break;
			}
			if(!m_OwnDirectory)
				return false;
			std::filesystem::rename(m_Original, m_Private, Error);
			if(Error)
				return false;
			m_Restore = true;
			// 检查与移动之间也可能换入新文件，必须对已接管的目录项再核对一次。
			return IsCapturedFile(Original, Filename());
		}
		const char *Filename() const { return m_Filename.c_str(); }
		void Removed() { m_Restore = false; }
		~CReplaySource() noexcept
		{
			if(!m_OwnDirectory)
				return;
			try
			{
				std::error_code Error;
				if(m_Restore)
				{
					// 原名可被复用，恢复时也只允许创建；恢复失败则保留可找回的源录像。
					std::filesystem::create_hard_link(m_Private, m_Original, Error);
					if(Error && Error != std::errc::file_exists)
						std::filesystem::copy_file(m_Private, m_Original, std::filesystem::copy_options::none, Error);
					if(Error)
					{
						log_warn("replay", "Source recording retained at '%s'", Filename());
						return;
					}
					std::filesystem::remove(m_Private, Error);
				}
				std::filesystem::remove(m_Directory, Error);
			}
			catch(...)
			{
			}
		}
	};
}

bool CDemoEdit::COperations::RemoveSource(IStorage &Storage, const char *pPath)
{
	return Storage.RemoveFile(pPath, fs_is_relative_path(pPath) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE);
}

CDemoEdit::COperations &CDemoEdit::Operations()
{
	static COperations s_Operations;
	return m_pOperations ? *m_pOperations : s_Operations;
}

CDemoEdit::CDemoEdit(const char *pNetVersion, CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, IStorage *pStorage, const char *pDemo, const char *pDst, int StartTick, int EndTick, COperations *pOperations) :
	m_pSnapshotDelta(std::make_unique<CSnapshotDelta>(*pSnapshotDelta)),
	m_pSnapshotDeltaSixup(std::make_unique<CSnapshotDelta>(*pSnapshotDeltaSixup)),
	m_pStorage(pStorage),
	m_pOperations(pOperations)
{
	std::string Source, Destination;
	if(!demo_output::Resolve(*pStorage, pDemo, Source) ||
		!demo_output::Resolve(*pStorage, pDst, Destination) || !demo_output::ValidDestination(*pStorage, pDst))
		throw std::invalid_argument("Invalid replay path");
	std::error_code Error;
	const bool Same = std::filesystem::equivalent(std::filesystem::u8path(Source), std::filesystem::u8path(Destination), Error);
	if(Same || (Error && Error != std::errc::no_such_file_or_directory))
		throw std::invalid_argument("Replay destination must differ from its source");
	m_pSourceFile.reset(io_open_read_regular_checked(Source.c_str(), nullptr));
	if(!m_pSourceFile)
		throw std::invalid_argument("Replay source could not be opened");
	str_copy(m_aDemo, Source.c_str());
	str_copy(m_aDst, Destination.c_str());

	m_StartTick = StartTick;
	m_EndTick = EndTick;

	// Init the demoeditor
	m_DemoEditor.Init(m_pSnapshotDelta.get(), m_pSnapshotDeltaSixup.get(), pStorage, nullptr, m_pOperations);
}

void CDemoEdit::Run()
{
	m_Success = false;
	try
	{
		auto pSourceFile = std::move(m_pSourceFile);
		CReplaySource Source(m_aDemo);
		if(!pSourceFile || !Source.Claim(pSourceFile.get(), m_aDemo))
			return;
		pSourceFile.reset();
		m_Success = m_DemoEditor.Slice(Source.Filename(), m_aDst, m_StartTick, m_EndTick, nullptr, nullptr);
		// 输出完成后才移除源文件；清理失败不能把已发布结果改成失败。
		if(m_Success)
		{
			if(Operations().RemoveSource(*m_pStorage, Source.Filename()))
				Source.Removed();
			else
				log_warn("replay", "Replay was saved, but its source could not be removed");
		}
	}
	catch(...)
	{
	}
}
