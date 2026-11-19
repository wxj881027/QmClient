#include "demoedit.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/shared/demo.h>
#include <engine/shared/demo_output.h>
#include <engine/storage.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

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
		demo_output::CCapturedSource Source;
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
