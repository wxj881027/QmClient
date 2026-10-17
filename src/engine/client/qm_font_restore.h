#ifndef ENGINE_CLIENT_QM_FONT_RESTORE_H
#define ENGINE_CLIENT_QM_FONT_RESTORE_H

#include "qm_font_scan_job.h"

#include <algorithm>
#include <array>

// 启动恢复与候选列表开关独立；只在主线程记录请求和接收结果。
class CQmFontRestore
{
public:
	using TSelection = std::array<const char *, 3>;
	using FFindFiles = std::function<std::vector<std::string>(const std::vector<std::string> &)>;

private:
	std::array<std::string, 3> m_aAttemptedNames;
	std::shared_ptr<CQmFontScanJob> m_pJob;

public:
	bool Active() const { return m_pJob != nullptr; }

	template<typename THasFace>
	std::shared_ptr<CQmFontScanJob> Request(const TSelection &apSelection, THasFace &&HasFace, FFindFiles FindFiles, CQmFontScanJob::FRead Read)
	{
		if(Active())
			return nullptr;
		std::vector<std::string> vNames;
		for(size_t Index = 0; Index < apSelection.size(); ++Index)
		{
			const char *pName = apSelection[Index];
			if(m_aAttemptedNames[Index] == pName)
				continue;
			// 缺失或损坏的选择只尝试一次；配置变化后允许再次查找。
			m_aAttemptedNames[Index] = pName;
			if(pName[0] == '\0' || HasFace(pName))
				continue;
			if(std::find(vNames.begin(), vNames.end(), pName) == vNames.end())
				vNames.emplace_back(pName);
		}
		if(vNames.empty())
			return nullptr;
		m_pJob = std::make_shared<CQmFontScanJob>([FindFiles = std::move(FindFiles), vNames = std::move(vNames)] { return FindFiles(vNames); }, std::move(Read));
		return m_pJob;
	}

	bool TakeNext(CQmFontScanJob::SFile &File)
	{
		return m_pJob != nullptr && m_pJob->TakeNext(File);
	}

	template<typename THasFace, typename TApply>
	bool Finish(const TSelection &apSelection, THasFace &&HasFace, TApply &&Apply)
	{
		if(m_pJob == nullptr || !m_pJob->Exhausted())
			return false;
		for(size_t Index = 0; Index < apSelection.size(); ++Index)
		{
			// 始终解析当前选择，不把任务启动时的名字写回；改选到同一集合中的
			// 其他样式也能恢复，清空或仍然缺失的角色继续使用原回退。
			if(apSelection[Index][0] != '\0' && HasFace(apSelection[Index]))
				Apply(Index, apSelection[Index]);
		}
		m_pJob.reset();
		return true;
	}

	void Reset()
	{
		m_pJob.reset();
		m_aAttemptedNames = {};
	}
};

#endif
