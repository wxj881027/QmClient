// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_SHARED_QM_IME_POLICY_H
#define ENGINE_SHARED_QM_IME_POLICY_H

#include <base/detect.h>

#include <cstddef>
#include <cstdint>
#include <optional>

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

// 提交后是否抑制过期 CHANGECANDIDATE（打开新候选页时解除）
inline bool QmImeShouldSuppressStaleCandidateReload(bool SuppressFlag, bool IsOpenNotify)
{
	if(IsOpenNotify)
		return false;
	return SuppressFlag;
}

#endif
