// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_SHARED_QM_IME_POLICY_H
#define ENGINE_SHARED_QM_IME_POLICY_H

#include <base/detect.h>

#include <engine/keys.h>
#include <engine/shared/qm_ime_provider.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

inline bool QmImeEmbeddedEngineAllowed()
{
#if defined(CONF_FAMILY_WINDOWS)
	return true;
#else
	return false;
#endif
}

inline bool QmImeShouldUseSystemCandidateUi()
{
#if defined(CONF_FAMILY_WINDOWS)
	return false;
#else
	return true;
#endif
}

inline bool QmImeShouldRenderCustomCandidateUi()
{
	if(QmImeShouldUseSystemCandidateUi())
		return false;
	return true;
}

inline bool QmImeNotifyFlagsIncludeCandidateList(unsigned CandidateListFlags, unsigned CandidateListIndex)
{
	if(CandidateListIndex >= 32)
		return false;
	if(CandidateListFlags == 0)
		return CandidateListIndex == 0;
	return (CandidateListFlags & (1u << CandidateListIndex)) != 0;
}

inline unsigned QmImeCandidatePageSizeOrCount(unsigned PageSize, unsigned CandidateCount)
{
	return PageSize > 0 ? PageSize : CandidateCount;
}

inline size_t QmImeCandidateOffsetCapacity(size_t BufferSize, size_t OffsetTableStart)
{
	if(BufferSize < OffsetTableStart)
		return 0;
	return (BufferSize - OffsetTableStart) / sizeof(uint32_t);
}

inline std::optional<size_t> QmImeBoundedUtf16Length(const unsigned char *pBuffer, size_t BufferSize, size_t Offset)
{
	constexpr size_t CodeUnitSize = sizeof(uint16_t);
	if(pBuffer == nullptr || Offset % CodeUnitSize != 0 || Offset > BufferSize || BufferSize - Offset < CodeUnitSize)
		return {};

	for(size_t Position = Offset; BufferSize - Position >= CodeUnitSize; Position += CodeUnitSize)
	{
		if(pBuffer[Position] == 0 && pBuffer[Position + 1] == 0)
			return (Position - Offset) / CodeUnitSize;
	}
	return {};
}

// 弹窗可见性以候选列表为准：组合串可能被 IME 短暂清空，不能单独作为隐藏条件
inline bool QmImePopupShouldBeVisible(int CandidateCount)
{
	return CandidateCount > 0;
}

// 部分 IME 在翻页或选词时短暂清空组合串，仍有效的候选也属于组合输入。
inline bool QmImeCompositionIsActive(bool HasCompositionText, int CandidateCount)
{
	return HasCompositionText || CandidateCount > 0;
}

// 组合输入期间键盘由 IME 处理，鼠标仍须能切换输入框、清除组合或操作页面。
inline bool QmImeShouldSuppressPress(int Key, bool CompositionActive)
{
	return CompositionActive && !(Key >= KEY_MOUSE_1 && Key <= KEY_MOUSE_WHEEL_RIGHT);
}

// 空 TEXTEDITING 不应清空候选：搜狗等 IME 在页边界/高亮时会短暂发空串
inline bool QmImeEmptyTextEditingShouldClearCandidates()
{
	return false;
}

enum class EQmImeCandidateReloadAction
{
	KEEP_PREVIOUS = 0,
	REPLACE,
	CLEAR,
};

// CHANGECANDIDATE 读列表失败时保留上一帧，避免弹窗隐现闪烁。
// SuppressStaleReload：TEXTINPUT 提交后忽略未伴随 OPENCANDIDATE 的过期刷新。
inline EQmImeCandidateReloadAction QmImeResolveCandidateReloadAction(bool LoadSucceeded, bool IsOpenNotify, bool SuppressStaleReload = false)
{
	if(IsOpenNotify)
		return LoadSucceeded ? EQmImeCandidateReloadAction::REPLACE : EQmImeCandidateReloadAction::CLEAR;
	if(SuppressStaleReload)
		return EQmImeCandidateReloadAction::KEEP_PREVIOUS;
	return LoadSucceeded ? EQmImeCandidateReloadAction::REPLACE : EQmImeCandidateReloadAction::KEEP_PREVIOUS;
}

// 提交后是否抑制过期 CHANGECANDIDATE（新的组合串或打开通知解除）。
inline bool QmImeShouldSuppressStaleCandidateReload(bool SuppressFlag, bool IsOpenNotify)
{
	if(IsOpenNotify)
		return false;
	return SuppressFlag;
}

// IMM 快照保留分页；SDL 快照在原生通知缺失或列表尚未就绪时补齐当前页。
struct SQmImeCandidateList
{
	std::vector<std::string> m_vCandidates;
	int m_SelectedIndex = -1;
	int m_PageStart = 0;
	int m_PageSize = 0;
	int m_TotalCount = 0;
};

class CQmImeCandidateState
{
public:
	const SQmImeCandidateList &Candidates() const { return m_Candidates; }
	bool AcceptsSdlCandidates() const { return !m_SuppressStaleReload; }
	void ResumeComposition() { m_SuppressStaleReload = false; }
	bool ResumeAfterSystemKey(int Key, int Modifiers, bool Repeat)
	{
		if(!m_SuppressStaleReload || Key < KEY_A || Key > KEY_Z || Repeat ||
			(Modifiers & (QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_ALT | QM_IME_KEYMASK_GUI)) != 0)
			return false;
		ResumeComposition();
		return true;
	}

	void UpdateFromEngine(const SQmImeProviderState &State)
	{
		Clear();
		m_SuppressStaleReload = false;
		if(!State.m_Active)
			return;
		m_Candidates.m_vCandidates = State.m_vCandidates;
		const int Count = static_cast<int>(m_Candidates.m_vCandidates.size());
		m_Candidates.m_SelectedIndex = State.m_Selected >= 0 && State.m_Selected < Count ? State.m_Selected : -1;
		m_Candidates.m_PageStart = State.m_PageStart > 0 ? State.m_PageStart : 0;
		m_Candidates.m_PageSize = State.m_PageSize > 0 ? State.m_PageSize : Count;
		m_Candidates.m_TotalCount = State.m_TotalCount >= Count ? State.m_TotalCount : Count;
	}

	void Clear()
	{
		m_Candidates.m_vCandidates.clear();
		m_Candidates.m_SelectedIndex = -1;
		m_Candidates.m_PageStart = 0;
		m_Candidates.m_PageSize = 0;
		m_Candidates.m_TotalCount = 0;
	}

	void EndComposition()
	{
		Clear();
		m_SuppressStaleReload = true;
	}

	void UpdateFromNative(std::optional<SQmImeCandidateList> List, bool IsOpenNotify, bool HasActiveComposition = false)
	{
		// 提交后可能漏收下一轮打开/编辑通知；仅由当前 IMM 组合串确认恢复。
		if(HasActiveComposition)
			ResumeComposition();
		const bool Suppress = QmImeShouldSuppressStaleCandidateReload(m_SuppressStaleReload, IsOpenNotify);
		if(IsOpenNotify)
			ResumeComposition();
		switch(QmImeResolveCandidateReloadAction(List.has_value(), IsOpenNotify, Suppress))
		{
		case EQmImeCandidateReloadAction::REPLACE:
			m_Candidates = std::move(*List);
			break;
		case EQmImeCandidateReloadAction::CLEAR:
			Clear();
			break;
		case EQmImeCandidateReloadAction::KEEP_PREVIOUS:
			break;
		}
	}

	void UpdateFromSdl(const char *const *ppCandidates, int Count, int SelectedIndex, bool WindowsNumberedCandidates, std::optional<SQmImeCandidateList> NativeCandidates = std::nullopt, bool HasActiveComposition = false)
	{
		if(Count <= 0 || ppCandidates == nullptr)
		{
			Clear();
			return;
		}
		if(HasActiveComposition)
			ResumeComposition();
		if(!AcceptsSdlCandidates())
			return;

		if(NativeCandidates.has_value() && !NativeCandidates->m_vCandidates.empty())
		{
			m_Candidates = std::move(*NativeCandidates);
			return;
		}

		Clear();
		m_Candidates.m_vCandidates.reserve(Count);
		for(int i = 0; i < Count; ++i)
		{
			const char *pCandidate = ppCandidates[i] != nullptr ? ppCandidates[i] : "";
			// SDL 的 Windows 后端已加上一位数字和空格，候选窗会自行绘制序号。
			if(WindowsNumberedCandidates && pCandidate[0] >= '0' && pCandidate[0] <= '9' && pCandidate[1] == ' ')
				pCandidate += 2;
			m_Candidates.m_vCandidates.emplace_back(pCandidate);
		}
		m_Candidates.m_SelectedIndex = SelectedIndex >= 0 && SelectedIndex < Count ? SelectedIndex : -1;
		m_Candidates.m_PageSize = m_Candidates.m_TotalCount = Count;
	}

private:
	SQmImeCandidateList m_Candidates;
	bool m_SuppressStaleReload = false;
};

// 正在输入法组合输入期间，回车、退格、删除与方向键由 IME 内部消费，避免误篡改外部输入框内容。
inline bool QmImeKeyConsumedByComposition(int Key)
{
	return Key == KEY_BACKSPACE || Key == KEY_DELETE ||
	       Key == KEY_LEFT || Key == KEY_RIGHT ||
	       Key == KEY_HOME || Key == KEY_END ||
	       Key == KEY_RETURN || Key == KEY_KP_ENTER;
}

// 空文本输入事件仅代表 IME 状态重置或刷新，不得用于替换已有文本或清空选区。
inline bool QmImeTextEventShouldMutateBuffer(const char *pText)
{
	return pText != nullptr && pText[0] != '\0';
}

#endif
