#ifndef ENGINE_CLIENT_QM_FESTIVE_REPORT_TEXT_H
#define ENGINE_CLIENT_QM_FESTIVE_REPORT_TEXT_H

#include <string>

namespace QmFestiveReportText
{
	struct SLocalizedCrashReport
	{
		std::wstring m_Title;
		std::wstring m_Subtitle;
		std::wstring m_Details;
	};

	// 纯文本准备；保留原始诊断，分配异常由窗口入口按既有消息框回退处理。
	SLocalizedCrashReport LocalizeCrashReport(const std::wstring &Title, const std::wstring &Original);
}

#endif
