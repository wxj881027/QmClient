#include "demoedit.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/shared/demo.h>
#include <engine/shared/demo_output.h>
#include <engine/storage.h>

#include <filesystem>
#include <stdexcept>

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
	m_pSnapshotDelta(pSnapshotDelta->Clone()),
	m_pSnapshotDeltaSixup(pSnapshotDeltaSixup->Clone()),
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
	str_copy(m_aDemo, Source.c_str());
	str_copy(m_aDst, Destination.c_str());

	m_StartTick = StartTick;
	m_EndTick = EndTick;

	// Init the demoeditor
	m_DemoEditor.Init(&*m_pSnapshotDelta, &*m_pSnapshotDeltaSixup, nullptr, pStorage, nullptr, m_pOperations);
}

void CDemoEdit::Run()
{
	m_Success = false;
	try
	{
		m_Success = m_DemoEditor.Slice(m_aDemo, m_aDst, m_StartTick, m_EndTick, nullptr, nullptr);
		// 输出完成后才移除源文件；清理失败不能把已发布结果改成失败。
		if(m_Success)
		{
			if(!Operations().RemoveSource(*m_pStorage, m_aDemo))
				log_warn("replay", "Replay was saved, but its source could not be removed");
		}
	}
	catch(...)
	{
	}
}
