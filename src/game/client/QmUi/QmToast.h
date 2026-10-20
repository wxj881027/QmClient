#ifndef GAME_CLIENT_QMUI_QMTOAST_H
#define GAME_CLIENT_QMUI_QMTOAST_H

#include <cstddef>
#include <deque>
#include <functional>
#include <string>

class CUi;
struct CUIRect;

// 轻提示语义：文字本身必须能独立读懂，颜色与色条只是辅助
//（灰度截图下信息不能丢）。
enum class EQmToastKind
{
	INFO = 0,
	SUCCESS,
	WARNING,
	FAILED,
};

namespace qm_ui
{
	struct SToastOptions
	{
		const char *m_pActionLabel = nullptr;
		std::function<void()> m_Action;
		float m_HoldSec = 3.5f;
	};

	// 轻提示队列：纯数据 + 时间推进，不依赖 UI 上下文，便于行为测试。
	class CToastQueue
	{
	public:
		struct SEntry
		{
			std::string m_Text;
			std::string m_ActionLabel;
			std::function<void()> m_Action;
			EQmToastKind m_Kind = EQmToastKind::INFO;
			double m_BornSec = 0.0;
			float m_HoldSec = 3.5f;
			// 按钮身份令牌：地址在 deque 内稳定，复用为 DoButtonLogic 的 Id。
			int m_ButtonToken = 0;
		};

		static constexpr size_t MAX_VISIBLE = 3;
		static constexpr float FADE_IN_SEC = 0.18f;
		static constexpr float FADE_OUT_SEC = 0.12f;

		void Push(EQmToastKind Kind, const char *pText, const SToastOptions &Options, double NowSec);
		// 丢弃已过「停留 + 退场」的条目。
		void Expire(double NowSec);
		// 取当前可见条目的透明度（入场淡入 × 退场淡出），受动效等级影响。
		float EntryAlpha(const SEntry &Entry, double NowSec, int MotionLevel) const;
		// 入场上浮距离（像素）。
		float EntryOffset(const SEntry &Entry, double NowSec, int MotionLevel) const;

		size_t Size() const { return m_vEntries.size(); }
		const SEntry &At(size_t Index) const { return m_vEntries[Index]; }
		SEntry &At(size_t Index) { return m_vEntries[Index]; }
		void Erase(size_t Index) { m_vEntries.erase(m_vEntries.begin() + Index); }
		void Clear() { m_vEntries.clear(); }

	private:
		std::deque<SEntry> m_vEntries;
	};

	// 菜单域轻提示：不阻断输入、不抢焦点、不进入 Tab 链，到时自动消失。
	// 只承载「结果」，不承载需要用户决策的内容——那属于 Alert / Confirm。
	void Toast(EQmToastKind Kind, const char *pText, const SToastOptions &Options = {});
	void ClearToasts();
	CToastQueue &Toasts();

	// 在菜单渲染末尾调用；toast 位于弹层之上，且永不接管输入。
	void RenderToasts(CUi *pUi);
} // namespace qm_ui

#endif // GAME_CLIENT_QMUI_QMTOAST_H
