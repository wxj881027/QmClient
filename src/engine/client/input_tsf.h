#ifndef ENGINE_CLIENT_INPUT_TSF_H
#define ENGINE_CLIENT_INPUT_TSF_H

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
struct ITfThreadMgrEx;
struct ITfUIElementSink;

// SDL3 只用 IMM32 的 ISC_SHOWUI 标志隐藏输入法界面，搜狗等 TSF 输入法不遵守，
// 会和客户端候选栏同时显示。这里开启 TSF UILess 模式并拒绝显示输入法自带界面。
// 仅 MSVC 构建启用，其他 Windows 工具链为空操作。
class CQmTsfUiLess
{
public:
	CQmTsfUiLess() = default;
	~CQmTsfUiLess();
	CQmTsfUiLess(const CQmTsfUiLess &) = delete;
	CQmTsfUiLess &operator=(const CQmTsfUiLess &) = delete;

	// 必须在创建窗口的线程调用，且该线程已初始化 COM。
	bool Activate();
	void Deactivate();
	bool IsActive() const { return m_pThreadMgr != nullptr; }

private:
	ITfThreadMgrEx *m_pThreadMgr = nullptr;
	ITfUIElementSink *m_pSink = nullptr;
	unsigned long m_SinkCookie = 0;
};
#endif

#endif
