#ifndef ENGINE_CLIENT_INPUT_TSF_H
#define ENGINE_CLIENT_INPUT_TSF_H

#include <base/detect.h>

#include <engine/shared/qm_ime_policy.h>

#if defined(CONF_FAMILY_WINDOWS)
struct ITfThreadMgrEx;
class CQmTsfUiElementSink;

#if defined(_MSC_VER)
struct ITfCandidateListUIElement;
std::optional<SQmImeCandidateList> QmReadTsfCandidates(ITfCandidateListUIElement *pElement);
#endif

// 通过 TSF 接管候选数据与窗口显示；读取失败时请求恢复系统候选窗。
// 仅 MSVC 构建启用，其他 Windows 工具链为空操作。
class CQmTsfUiLess
{
public:
	CQmTsfUiLess() = default;
	~CQmTsfUiLess();
	CQmTsfUiLess(const CQmTsfUiLess &) = delete;
	CQmTsfUiLess &operator=(const CQmTsfUiLess &) = delete;

	// 必须在创建窗口的线程调用，且该线程已初始化 COM。
	bool Activate(CQmImeCandidateState &Candidates);
	void SetTextInputActive(bool Active);
	void Deactivate();
	bool IsActive() const { return m_pThreadMgr != nullptr; }

private:
	ITfThreadMgrEx *m_pThreadMgr = nullptr;
	CQmTsfUiElementSink *m_pSink = nullptr;
	CQmImeCandidateState *m_pCandidates = nullptr;
	unsigned long m_SinkCookie = 0xffffffffUL;
};
#endif

#endif
