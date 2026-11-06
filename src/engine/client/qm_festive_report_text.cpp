#include "qm_festive_report_text.h"

#include <string_view>

namespace QmFestiveReportText
{
	std::wstring NormalizeEditNewlines(const std::wstring &Text)
	{
		std::wstring Result;
		Result.reserve(Text.size() + Text.size() / 8);
		for(size_t Index = 0; Index < Text.size(); ++Index)
		{
			if(Text[Index] == L'\n' && (Index == 0 || Text[Index - 1] != L'\r'))
				Result.push_back(L'\r');
			Result.push_back(Text[Index]);
		}
		return Result;
	}

	void ReplaceAll(std::wstring &Text, const std::wstring &Needle, const std::wstring &Replacement)
	{
		size_t Position = 0;
		while((Position = Text.find(Needle, Position)) != std::wstring::npos)
		{
			Text.replace(Position, Needle.size(), Replacement);
			Position += Replacement.size();
		}
	}

	std::wstring MemoryLineValue(const std::wstring &Line, std::wstring_view Prefix)
	{
		auto Value = std::wstring_view(Line).substr(Prefix.size());
		if(Value.starts_with(L' '))
			Value.remove_prefix(1);
		return std::wstring(Value);
	}

	enum class EFestiveReportKind
	{
		GRAPHICS,
		ASSERTION,
		FATAL,
		HANG,
	};

	EFestiveReportKind DetectReportKind(const std::wstring &Title, const std::wstring &Original)
	{
		if(Original.find(L"Report type: fatal-crash") != std::wstring::npos)
			return EFestiveReportKind::FATAL;
		if(Original.find(L"Report type: hang") != std::wstring::npos)
			return EFestiveReportKind::HANG;
		if(Title.find(L"Assertion") != std::wstring::npos || Original.find(L"assertion error occurred") != std::wstring::npos)
			return EFestiveReportKind::ASSERTION;
		// 兜底：报告被截断或缺少 "Report type:" 行时，按 fatal 报告的头部与原因行
		// 判断，避免把致命崩溃归为图形错误并给出无效的排障建议。
		if(Original.find(L"QmClient fatal error report") != std::wstring::npos ||
			Original.find(L"Reason: Unhandled structured exception") != std::wstring::npos ||
			Original.find(L"Reason: Unhandled C++ exception") != std::wstring::npos ||
			Original.find(L"Reason: Vectored exception fallback") != std::wstring::npos ||
			Original.find(L"Reason: Fatal signal") != std::wstring::npos)
			return EFestiveReportKind::FATAL;
		return EFestiveReportKind::GRAPHICS;
	}

	std::wstring TranslateReportLine(const std::wstring &Line, EFestiveReportKind Kind)
	{
		if(Line == L"A graphics error occurred. Please see details and instructions below.")
			return L"发生图形错误，请查看下面的原因和处理建议。";
		if(Line == L"An assertion error occurred. Please take a screenshot and report this error.")
			return L"发生断言错误，请保留本报告并反馈。";
		if(Line == L"Submitting to graphics queue failed.")
			return L"向图形队列提交任务失败。";
		if(Line == L"device lost")
			return L"图形设备已丢失（device lost，通常表示显卡驱动发生重置或停止响应）。";
		if(Line == L"host ran out of memory")
			return L"系统内存不足（host ran out of memory）。";
		if(Line == L"device ran out of memory")
			return L"显卡可用显存不足（device ran out of memory）。";
		if(Line == L"Submitting the render commands failed. Try to update your GPU drivers.")
			return L"提交渲染命令失败，请尝试更新或干净安装显卡驱动。";
		if(Line == L"For detailed troubleshooting instructions please read our Wiki:")
			return L"详细排障说明：";
		if(Line == L"If this did not resolve the issue, please take a screenshot and report this error.")
			return L"如果问题仍然存在，请截图并反馈此错误。";
		if(Line.starts_with(L"Please also share the assert log"))
			return L"反馈时请同时提供配置目录 dumps/QmClient_Crash 中的断言日志和崩溃日志。";
		if(Line == L"QmClient fatal error report")
			return L"QmClient 致命错误报告";
		if(Line == L"QmClient hang diagnostic report")
			return L"QmClient 卡死诊断报告";
		if(Line == L"Report type: fatal-crash")
			return L"报告类型：致命崩溃";
		if(Line == L"Report type: hang")
			return L"报告类型：客户端无响应";
		if(Line.starts_with(L"Timestamp: "))
			return L"发生时间：" + Line.substr(11);
		if(Line == L"Reason: Unhandled structured exception")
			return L"原因：未处理的 Windows 结构化异常";
		if(Line == L"Reason: Unhandled C++ exception (std::terminate)")
			return L"原因：未捕获的 C++ 异常（std::terminate）";
		if(Line == L"Reason: Fatal signal")
			return L"原因：进程收到致命信号";
		if(Line == L"Reason: Vectored exception fallback")
			return L"原因：Windows 向量异常处理器捕获到致命异常";
		if(Line.starts_with(L"Reason: "))
			return L"原因：" + Line.substr(8);
		if(Line.starts_with(L"Process ID: "))
			return L"进程 ID：" + Line.substr(12);
		if(Line.starts_with(L"Thread ID: "))
			return L"线程 ID：" + Line.substr(11);
		if(Line.starts_with(L"Executable path: "))
			return L"客户端路径：" + Line.substr(17);
		if(Line.starts_with(L"Crash log path (.RTP): "))
			return L"崩溃日志路径：" + Line.substr(23);
		if(Line.starts_with(L"Fallback minidump path: "))
			return L"内存转储路径：" + Line.substr(24);
		if(Line.starts_with(L"Fallback minidump written: "))
			return L"内存转储写入状态：" + Line.substr(27);
		if(Line.starts_with(L"Exception code: "))
			return L"异常代码：" + Line.substr(16);
		if(Line.starts_with(L"Exception flags: "))
			return L"异常标志：" + Line.substr(17);
		if(Line.starts_with(L"Exception address: "))
			return L"异常地址：" + Line.substr(19);
		if(Line.starts_with(L"Exception module: "))
			return L"故障模块：" + Line.substr(18);
		if(Line.starts_with(L"Exception module path: "))
			return L"故障模块路径：" + Line.substr(23);
		if(Line.starts_with(L"Exception parameters: "))
			return L"异常参数数量：" + Line.substr(22);
		if(Line == L"Access violation operation: read")
			return L"访问违例操作：读取";
		if(Line == L"Access violation operation: write")
			return L"访问违例操作：写入";
		if(Line == L"Access violation operation: execute")
			return L"访问违例操作：执行";
		if(Line.starts_with(L"Access violation target address: "))
			return L"访问违例目标地址：" + Line.substr(33);
		if(Line.starts_with(L"Context flags: "))
			return L"CPU 上下文标志：" + Line.substr(15);
		if(Line.starts_with(L"RIP="))
			return L"CPU 寄存器：" + Line;
		if(Line.starts_with(L"RAX=") || Line.starts_with(L"RSI=") || Line.starts_with(L"R10="))
			return L"CPU 寄存器：" + Line;
		if(Line.starts_with(L"R14="))
			return L"CPU 寄存器：" + Line;
		if(Line.starts_with(L"Instruction bytes near RIP:"))
			return L"RIP 附近指令字节：" + MemoryLineValue(Line, L"Instruction bytes near RIP:");
		if(Line.starts_with(L"Stack bytes near RSP:"))
			return L"RSP 附近栈字节：" + MemoryLineValue(Line, L"Stack bytes near RSP:");
		if(Line.starts_with(L"Instruction bytes near EIP:"))
			return L"EIP 附近指令字节：" + MemoryLineValue(Line, L"Instruction bytes near EIP:");
		if(Line.starts_with(L"Stack bytes near ESP:"))
			return L"ESP 附近栈字节：" + MemoryLineValue(Line, L"Stack bytes near ESP:");
		if(Line == L"Loaded modules:")
			return L"已加载模块：";
		if(Line.starts_with(L"Signal: "))
			return L"致命信号：" + Line.substr(8);
		if(Line.starts_with(L"Hang timeout threshold: "))
			return L"无响应判定阈值：" + Line.substr(24);
		if(Line.starts_with(L"No heartbeat duration: "))
			return L"未收到心跳的时长：" + Line.substr(23);
		if(Line.starts_with(L"Heartbeat clock: "))
			return L"心跳时钟：" + Line.substr(17);
		if(Line.starts_with(L"Client state: "))
			return L"客户端状态：" + Line.substr(14);
		if(Line.starts_with(L"Current map: "))
			return L"当前地图：" + Line.substr(13);
		if(Line.starts_with(L"Server address: "))
			return L"服务器地址：" + Line.substr(16);
		if(Line.starts_with(L"Report directory: "))
			return L"报告目录：" + Line.substr(18);
		if(Line.starts_with(L"Platform: "))
			return L"平台：" + Line.substr(10);
		if(Line.starts_with(L"Configuration: "))
		{
			std::wstring Configuration = Line.substr(15);
			ReplaceAll(Configuration, L"base", L"基础");
			ReplaceAll(Configuration, L"autoupdate", L"自动更新");
			ReplaceAll(Configuration, L"crashdump", L"崩溃转储");
			ReplaceAll(Configuration, L"videorecorder", L"视频录制");
			ReplaceAll(Configuration, L"debug", L"调试");
			ReplaceAll(Configuration, L"websockets", L"WebSocket");
			return L"构建配置：" + Configuration;
		}
		if(Line.starts_with(L"Game version: "))
			return L"客户端版本：" + Line.substr(14);
		if(Line.starts_with(L"OS version: "))
			return L"操作系统：" + Line.substr(12);
		if(Line.starts_with(L"Configured graphics backend: "))
			return L"当前图形后端：" + Line.substr(29);
		if(Line.starts_with(L"GPU: "))
			return L"显卡：" + Line.substr(5);
		if(Line.starts_with(L"Texture: "))
		{
			std::wstring Memory = Line;
			ReplaceAll(Memory, L"Texture:", L"纹理显存：");
			ReplaceAll(Memory, L"Buffer:", L"缓冲区：");
			ReplaceAll(Memory, L"Streamed:", L"流式缓冲：");
			ReplaceAll(Memory, L"Staging:", L"暂存缓冲：");
			return Memory;
		}
		if(Line.find(L"failed") != std::wstring::npos || Line.find(L"Failed") != std::wstring::npos)
			return (Kind == EFestiveReportKind::GRAPHICS ? L"图形后端报告失败：" : L"报告中的失败信息：") + Line;
		return Line;
	}

	SLocalizedCrashReport LocalizeCrashReport(const std::wstring &Title, const std::wstring &Original)
	{
		SLocalizedCrashReport Report;
		std::wstring Cause;
		std::wstring Advice;
		const EFestiveReportKind Kind = DetectReportKind(Title, Original);
		if(Kind == EFestiveReportKind::HANG)
		{
			Report.m_Title = L"客户端卡住了，好运还在";
			Report.m_Subtitle = L"看门狗检测到主循环长时间没有心跳，诊断报告和内存转储已保存。";
			Cause = L"客户端主线程持续无响应，可能卡在图形驱动、文件读写、网络任务或退出清理中。";
			Advice = L"先等待片刻；若仍无响应，可结束原客户端进程，然后打开崩溃报告进行反馈。";
		}
		else if(Kind == EFestiveReportKind::ASSERTION)
		{
			Report.m_Title = L"程序遇到断言，好运还在";
			Report.m_Subtitle = L"QmClient 检测到不应出现的内部状态，已停止继续运行以保留现场。";
			Cause = L"某个内部条件与程序预期不一致。具体文件、行号和断言内容可在下方报告中查看。";
			Advice = L"重启客户端；如果能重复触发，请记下操作步骤并一并提供断言日志和崩溃报告。";
		}
		else if(Kind == EFestiveReportKind::FATAL && (Original.find(L"EXCEPTION_ACCESS_VIOLATION") != std::wstring::npos || Original.find(L"0xC0000005") != std::wstring::npos))
		{
			Report.m_Title = L"程序访问了无效内存，好运还在";
			Report.m_Subtitle = L"检测到访问违例，故障地址、模块和 CPU 上下文已写入报告。";
			Cause = L"程序尝试读取、写入或执行无效内存，常见于空指针、已释放对象或驱动模块异常。";
			Advice = L"重启后复现相同操作；反馈时请提供 fatal report、dmp 文件和崩溃前的操作步骤。";
		}
		else if(Kind == EFestiveReportKind::FATAL && (Original.find(L"EXCEPTION_STACK_OVERFLOW") != std::wstring::npos || Original.find(L"0xC00000FD") != std::wstring::npos))
		{
			Report.m_Title = L"调用栈被用完了，好运还在";
			Report.m_Subtitle = L"检测到栈溢出，通常由无限递归或过深的函数调用引起。";
			Cause = L"当前线程没有剩余调用栈可用，程序无法继续执行。";
			Advice = L"请保留 dmp 和 fatal report；如果某个地图、菜单或操作能稳定触发，请一并说明。";
		}
		else if(Kind == EFestiveReportKind::FATAL && Original.find(L"std::terminate") != std::wstring::npos)
		{
			Report.m_Title = L"C++ 异常没被接住，好运还在";
			Report.m_Subtitle = L"程序进入 std::terminate，致命报告已在独立进程中打开。";
			Cause = L"一个 C++ 异常越过了所有可处理它的边界，或程序在不允许抛出异常的位置抛出了异常。";
			Advice = L"请提供 fatal report 和 dmp，并说明崩溃前正在进行的操作。";
		}
		else if(Kind == EFestiveReportKind::FATAL)
		{
			Report.m_Title = L"客户端发生致命崩溃，好运还在";
			Report.m_Subtitle = L"独立报告进程已接管界面，原客户端可以安全结束。";
			Cause = L"Windows 或 C++ 运行库报告了无法继续的致命错误，具体代码和故障模块见下方。";
			Advice = L"重启客户端；如果问题重复发生，请提供 fatal report、dmp 和复现步骤。";
		}
		else if(Original.find(L"device lost") != std::wstring::npos)
		{
			Report.m_Title = L"显卡驱动掉线了，好运还在";
			Report.m_Subtitle = L"检测到 device lost：驱动或 GPU 被系统重置，QmClient 已停止继续渲染。";
			Cause = L"图形设备已丢失。常见原因是显卡驱动重置、驱动异常、超频不稳定，或 GPU 长时间无响应。";
			Advice = L"优先更新或干净安装显卡驱动；若重复发生，可改用 OpenGL，并暂时关闭超频或降低图形设置。";
		}
		else if(Original.find(L"ran out of memory") != std::wstring::npos || Original.find(L"Allocation") != std::wstring::npos || Original.find(L"allocation") != std::wstring::npos)
		{
			Report.m_Title = L"显存有点挤，好运还在";
			Report.m_Subtitle = L"检测到内存或显存分配失败，QmClient 已停止继续申请图形资源。";
			Cause = L"图形后端无法继续分配所需内存，可能是显存不足、系统内存压力过高或驱动资源没有及时释放。";
			Advice = L"关闭占用显存的程序并重启客户端；若重复发生，请降低纹理或窗口分辨率并提供崩溃报告。";
		}
		else if(Original.find(L"swap chain") != std::wstring::npos || Original.find(L"surface lost") != std::wstring::npos || Original.find(L"Presenting graphics queue failed") != std::wstring::npos)
		{
			Report.m_Title = L"画面交接出了岔子，好运还在";
			Report.m_Subtitle = L"检测到窗口表面或交换链异常，通常与显示模式、驱动或屏幕切换有关。";
			Cause = L"图形后端无法把新画面提交到窗口，可能发生在切换显示器、全屏模式或显卡驱动重置之后。";
			Advice = L"重新启动客户端；若重复发生，请使用窗口模式、更新驱动，或改用 OpenGL 图形后端。";
		}
		else if(Original.find(L"initialize") != std::wstring::npos || Original.find(L"Creating instance failed") != std::wstring::npos || Original.find(L"No vulkan") != std::wstring::npos)
		{
			Report.m_Title = L"图形后端没能启动，好运还在";
			Report.m_Subtitle = L"Vulkan 初始化没有完成，客户端尚未进入正常渲染阶段。";
			Cause = L"当前驱动或设备未能提供 QmClient 所需的 Vulkan 功能。";
			Advice = L"更新显卡驱动后重试；仍然失败时，请改用 OpenGL 图形后端。";
		}
		else
		{
			Report.m_Title = L"图形系统出了点状况，好运还在";
			Report.m_Subtitle = L"QmClient 已停止继续渲染，诊断信息和崩溃报告都还在。";
			Cause = L"图形后端返回了无法继续运行的错误，具体原因请查看下面的中文化报告。";
			Advice = L"重新启动客户端并更新显卡驱动；若问题重复出现，请打开崩溃报告并一并反馈。";
		}

		std::wstring Translated;
		size_t LineStart = 0;
		while(LineStart <= Original.size())
		{
			const size_t LineEnd = Original.find(L'\n', LineStart);
			std::wstring Line = Original.substr(LineStart, LineEnd == std::wstring::npos ? std::wstring::npos : LineEnd - LineStart);
			if(!Line.empty() && Line.back() == L'\r')
				Line.pop_back();
			Translated += TranslateReportLine(Line, Kind);
			Translated += L"\r\n";
			if(LineEnd == std::wstring::npos)
				break;
			LineStart = LineEnd + 1;
		}

		Report.m_Details =
			L"【问题判断】\r\n" + Cause +
			L"\r\n\r\n【建议操作】\r\n" + Advice +
			L"\r\n\r\n【中文化报告】\r\n" + Translated +
			L"\r\n【原始诊断信息（反馈时请保留）】\r\n" + NormalizeEditNewlines(Original);
		return Report;
	}

}
