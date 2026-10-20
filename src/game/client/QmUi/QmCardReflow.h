#ifndef GAME_CLIENT_QMUI_QMCARDREFLOW_H
#define GAME_CLIENT_QMUI_QMCARDREFLOW_H

#include <game/client/ui_rect.h>

#include <base/vmath.h>

#include <string>
#include <unordered_map>

// 卡片重排的 FLIP 追随：记录每个条目上一帧的屏幕矩形，把「布局已经换位」
// 转成一段带阻尼回弹的偏移，让下载完成 / 删除后的整列前移读作滑过去，
// 而不是瞬移。位移只做平移、不改宽度，避免卡片文本缓存按宽度反复失效。
class CQmCardReflow
{
public:
	// 每帧一次。Snap=true 时直接对齐当前布局（滚动中、切页、首帧）。
	void BeginFrame(float Dt, bool Snap);
	// 返回本帧要叠加到 Current 上的偏移。pSeedFrom 只在条目首次出现时生效，
	// 用来让新装好的资源从下载按钮的位置飞入。
	vec2 Track(const char *pKey, const CUIRect &Current, const CUIRect *pSeedFrom = nullptr);
	void Reset();

private:
	struct SState
	{
		float m_OffsetX = 0.0f;
		float m_OffsetY = 0.0f;
		float m_VelocityX = 0.0f;
		float m_VelocityY = 0.0f;
		CUIRect m_LastRect{};
		bool m_Known = false;
		int m_LastFrame = 0;
	};

	static void StepSpring(float &Offset, float &Velocity, float Dt);
	void Reap();

	std::unordered_map<std::string, SState> m_States;
	float m_Dt = 0.0f;
	bool m_Snap = true;
	int m_Frame = 0;
};

#endif // GAME_CLIENT_QMUI_QMCARDREFLOW_H
