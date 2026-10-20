#include "QmToast.h"

#include <base/system.h>

#include <algorithm>

namespace qm_ui
{
	namespace
	{
		constexpr float TOAST_SLIDE_DISTANCE = 8.0f;

		CToastQueue s_Queue;

		float EaseOutQuad(float T)
		{
			const float Clamped = std::clamp(T, 0.0f, 1.0f);
			return 1.0f - (1.0f - Clamped) * (1.0f - Clamped);
		}
	} // namespace

	void CToastQueue::Push(EQmToastKind Kind, const char *pText, const SToastOptions &Options, double NowSec)
	{
		if(pText == nullptr || pText[0] == '\0')
			return;
		// 连续同文合并：重复触发只续命，不堆叠成三条一样的提示。
		for(auto &Entry : m_vEntries)
		{
			if(Entry.m_Text == pText && Entry.m_Kind == Kind)
			{
				Entry.m_BornSec = NowSec;
				Entry.m_HoldSec = Options.m_HoldSec > 0.0f ? Options.m_HoldSec : 3.5f;
				return;
			}
		}
		SEntry Entry;
		Entry.m_Text = pText;
		Entry.m_Kind = Kind;
		Entry.m_BornSec = NowSec;
		Entry.m_HoldSec = Options.m_HoldSec > 0.0f ? Options.m_HoldSec : 3.5f;
		if(Options.m_pActionLabel != nullptr && Options.m_pActionLabel[0] != '\0' && Options.m_Action)
		{
			Entry.m_ActionLabel = Options.m_pActionLabel;
			Entry.m_Action = Options.m_Action;
		}
		m_vEntries.push_back(std::move(Entry));
		while(m_vEntries.size() > MAX_VISIBLE)
			m_vEntries.pop_front();
	}

	void CToastQueue::Expire(double NowSec)
	{
		const double Lifetime = static_cast<double>(FADE_IN_SEC) + static_cast<double>(FADE_OUT_SEC);
		m_vEntries.erase(
			std::remove_if(m_vEntries.begin(), m_vEntries.end(), [NowSec, Lifetime](const SEntry &Entry) {
				return NowSec - Entry.m_BornSec > static_cast<double>(Entry.m_HoldSec) + Lifetime;
			}),
			m_vEntries.end());
	}

	float CToastQueue::EntryAlpha(const SEntry &Entry, double NowSec, int MotionLevel) const
	{
		if(MotionLevel <= 0)
			return 1.0f;
		const double Age = NowSec - Entry.m_BornSec;
		const float In = EaseOutQuad(static_cast<float>(Age / FADE_IN_SEC));
		const float Out = EaseOutQuad(static_cast<float>((Age - Entry.m_HoldSec) / FADE_OUT_SEC));
		return std::clamp(In * (1.0f - Out), 0.0f, 1.0f);
	}

	float CToastQueue::EntryOffset(const SEntry &Entry, double NowSec, int MotionLevel) const
	{
		// 只有完整动效档才做位移；减弱档保留淡入淡出，避免大幅运动。
		if(MotionLevel < 2)
			return 0.0f;
		const double Age = NowSec - Entry.m_BornSec;
		const float In = EaseOutQuad(static_cast<float>(Age / FADE_IN_SEC));
		const float Out = EaseOutQuad(static_cast<float>((Age - Entry.m_HoldSec) / FADE_OUT_SEC));
		return (1.0f - In) * TOAST_SLIDE_DISTANCE + Out * TOAST_SLIDE_DISTANCE;
	}

	void Toast(EQmToastKind Kind, const char *pText, const SToastOptions &Options)
	{
		s_Queue.Push(Kind, pText, Options, time_get());
	}

	void ClearToasts()
	{
		s_Queue.Clear();
	}

	CToastQueue &Toasts()
	{
		return s_Queue;
	}
} // namespace qm_ui
