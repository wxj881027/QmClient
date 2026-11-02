#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_CONTROLS_H

#include <base/detect.h>
#include <base/vmath.h>

#include <engine/input.h>

#include <generated/protocol.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace qm_mobile_spectator
{
	constexpr bool Enabled()
	{
#if defined(CONF_PLATFORM_ANDROID)
		return true;
#else
		return false;
#endif
	}

	enum EAction
	{
		PREVIOUS,
		SELECT,
		NEXT,
		// 跟随当前视野中心最近的 Tee：拖着画面看人时不用再去列表里按 ID 找人。
		NEAREST_TEE,
		// 观战端专用：计分板面板、实体层循环、取景方式切换。
		SCOREBOARD,
		ENTITY_LAYER,
		CAMERA_MODE,
		MENU,
		CHAT,
		PAUSE,
		NUM_ACTIONS,
	};

	// 视野中心最近的 Tee：跳过自己、旁观者（没有实体的项由调用方过滤）。
	struct SQmNearestTeeCandidate
	{
		int m_ClientId = 0;
		bool m_Active = false;
		bool m_Local = false;
		vec2 m_Pos = vec2(0.0f, 0.0f);
	};

	inline int QmNearestTeeClientId(const SQmNearestTeeCandidate *pCandidates, int NumCandidates, vec2 Center)
	{
		int Best = -1;
		float BestDist = 0.0f;
		for(int i = 0; i < NumCandidates; ++i)
		{
			const SQmNearestTeeCandidate &Candidate = pCandidates[i];
			if(!Candidate.m_Active || Candidate.m_Local)
				continue;
			const float Dist = distance(Candidate.m_Pos, Center);
			if(!std::isfinite(Dist))
				continue;
			if(Best < 0 || Dist < BestDist)
			{
				Best = Candidate.m_ClientId;
				BestDist = Dist;
			}
		}
		return Best;
	}

	// 取景方式：轮盘（左下摇杆推进）与拖动（单指拖画面）。
	enum ECameraMode
	{
		CAMERA_MODE_JOYSTICK = 0,
		CAMERA_MODE_DRAG = 1,
	};

	// 实体层循环：背景层（只看地图）→ 叠加 → 游戏层（实体铺满）。
	// cl_overlay_entities 就是这三档：0 只画地图瓦片，100 只画实体层，中间值两者叠加。
	inline int NextEntityLayerValue(int Current)
	{
		if(Current <= 0)
			return 50;
		if(Current < 100)
			return 100;
		return 0;
	}

	// 拖动取景：画面跟着手指走，手指右滑镜头中心左移。
	// 归一化位移乘上“当前缩放下的可见世界尺寸”才是 1:1：手指划过整屏时画面正好走过
	// 一整屏的世界距离。乘 Zoom 的反例：拉远（Zoom>1）时画面只走 1/Zoom，手感发飘。
	inline vec2 DragPan(vec2 NormalizedDelta, vec2 WorldExtent)
	{
		if(!std::isfinite(WorldExtent.x) || !std::isfinite(WorldExtent.y) || WorldExtent.x <= 0.0f || WorldExtent.y <= 0.0f ||
			!std::isfinite(NormalizedDelta.x) || !std::isfinite(NormalizedDelta.y))
			return vec2(0.0f, 0.0f);
		return -NormalizedDelta * WorldExtent;
	}

	struct SLayout
	{
		CUIRect m_Joystick;
		// 底部观战灵动岛：常驻显示当前取景对象，点一下回到自由视角。
		CUIRect m_Island;
		// 拖动取景时摇杆隐藏，也不能再吞掉左下的拖动起手。
		bool m_JoystickActive = true;
		std::array<CUIRect, NUM_ACTIONS> m_aButtons;
	};

	inline SLayout Layout(vec2 Screen, bool JoystickActive = true)
	{
		SLayout Result{};
		Result.m_JoystickActive = JoystickActive;
		Result.m_Joystick = {18.0f, Screen.y - 142.0f, 104.0f, 104.0f};
		// 右下 3x2：上一人/选择/下一人，最近 Tee/计分板/实体层。
		for(int i = PREVIOUS; i <= NEXT; ++i)
			Result.m_aButtons[i] = {Screen.x - 180.0f + (i % 3) * 54.0f, Screen.y - 118.0f, 48.0f, 38.0f};
		for(int i = NEAREST_TEE; i <= ENTITY_LAYER; ++i)
			Result.m_aButtons[i] = {Screen.x - 180.0f + (i - NEAREST_TEE) * 54.0f, Screen.y - 74.0f, 48.0f, 38.0f};
		// 右上：取景方式、暂停、聊天、菜单。
		Result.m_aButtons[CAMERA_MODE] = {Screen.x - 212.0f, 16.0f, 44.0f, 34.0f};
		Result.m_aButtons[MENU] = {Screen.x - 62.0f, 16.0f, 44.0f, 34.0f};
		Result.m_aButtons[CHAT] = {Screen.x - 112.0f, 16.0f, 44.0f, 34.0f};
		Result.m_aButtons[PAUSE] = {Screen.x - 162.0f, 16.0f, 44.0f, 34.0f};
		const float IslandWidth = std::min(Screen.x * 0.5f, 360.0f);
		Result.m_Island = {(Screen.x - IslandWidth) * 0.5f, Screen.y - 46.0f, IslandWidth, 32.0f};
		return Result;
	}

	inline bool InsideControl(const SLayout &Layout, vec2 Position)
	{
		if(Layout.m_JoystickActive && distance(Position, Layout.m_Joystick.Center()) <= Layout.m_Joystick.w * 0.5f)
			return true;
		if(Layout.m_Island.Inside(Position))
			return true;
		return std::any_of(Layout.m_aButtons.begin(), Layout.m_aButtons.end(), [&](const CUIRect &Button) { return Button.Inside(Position); });
	}

	// 手指身份类型独立于输入后端，生产路径使用 IInput 的不透明身份。
	template<typename TIdentity>
	class CPinchState
	{
		std::optional<std::array<TIdentity, 2>> m_Fingers;
		float m_PreviousDistance = 0.0f;

	public:
		void Reset()
		{
			m_Fingers.reset();
			m_PreviousDistance = 0.0f;
		}
		bool Active() const { return m_Fingers.has_value(); }
		float Update(const TIdentity &FirstId, vec2 First, const TIdentity &SecondId, vec2 Second)
		{
			const bool SamePair = m_Fingers.has_value() &&
					      (((*m_Fingers)[0] == FirstId && (*m_Fingers)[1] == SecondId) ||
						      ((*m_Fingers)[0] == SecondId && (*m_Fingers)[1] == FirstId));
			m_Fingers = std::array<TIdentity, 2>{FirstId, SecondId};
			const float Distance = distance(First, Second);
			if(!std::isfinite(Distance) || Distance < 8.0f)
			{
				// 距离无效时仍保留触摸归属，恢复到有效距离后重新建立基线。
				m_PreviousDistance = 0.0f;
				return 1.0f;
			}
			const float Factor = SamePair && m_PreviousDistance > 0.0f ? m_PreviousDistance / Distance : 1.0f;
			m_PreviousDistance = Distance;
			return Factor;
		}
	};

	inline vec2 Direction(vec2 Offset, float Radius)
	{
		const float Length = length(Offset);
		if(!std::isfinite(Length) || Radius <= 0.0f || Length <= Radius * 0.12f)
			return vec2(0.0f, 0.0f);
		const float Amount = std::clamp((Length / Radius - 0.12f) / 0.88f, 0.0f, 1.0f);
		return Offset * (Amount / Length);
	}

	inline vec2 Movement(vec2 Direction, float Zoom, float FrameTime)
	{
		if(!std::isfinite(FrameTime) || !std::isfinite(Zoom) || Zoom <= 0.0f)
			return vec2(0.0f, 0.0f);
		return Direction * (1000.0f * Zoom * std::clamp(FrameTime, 0.0f, 0.05f));
	}

	inline void NeutralizeGameplayInput(CNetObj_PlayerInput &Input)
	{
		Input.m_Direction = 0;
		Input.m_Jump = 0;
		Input.m_Hook = 0;
		Input.m_Fire = (Input.m_Fire + (Input.m_Fire & 1)) & INPUT_STATE_MASK;
		Input.m_WantedWeapon = 0;
		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
	}

	class CTouchState
	{
		std::vector<IInput::CTouchFinger> m_vPrevious;
		std::vector<IInput::CTouchFinger> m_vMapFingers;
		std::optional<IInput::CTouchFinger> m_JoystickFinger;
		CPinchState<IInput::CTouchFinger> m_Pinch;
		float m_ZoomFactor = 1.0f;
		vec2 m_Direction = vec2(0.0f, 0.0f);
		// 本帧空白区域单指位移（归一化屏幕坐标），拖动取景用；与捏合互斥。
		vec2 m_PanDelta = vec2(0.0f, 0.0f);
		bool m_WaitForRelease = false;
		bool m_OpenSelectorOnRelease = false;

		void RememberFingers(const std::vector<IInput::CTouchFingerState> &vFingers)
		{
			m_vPrevious.clear();
			for(const auto &Finger : vFingers)
				m_vPrevious.push_back(Finger.m_Finger);
		}

	public:
		void Cancel()
		{
			m_JoystickFinger.reset();
			m_Pinch.Reset();
			m_vMapFingers.clear();
			m_ZoomFactor = 1.0f;
			m_Direction = vec2(0.0f, 0.0f);
			m_PanDelta = vec2(0.0f, 0.0f);
			m_WaitForRelease = true;
			m_OpenSelectorOnRelease = false;
		}

		vec2 JoystickDirection() const { return m_Direction; }
		vec2 PanDelta() const { return m_PanDelta; }
		float ZoomFactor() const { return m_ZoomFactor; }

		unsigned Update(const std::vector<IInput::CTouchFingerState> &vFingers, const SLayout &Layout, vec2 Screen)
		{
			unsigned Actions = 0;
			m_Direction = vec2(0.0f, 0.0f);
			m_PanDelta = vec2(0.0f, 0.0f);
			m_ZoomFactor = 1.0f;
			if(m_WaitForRelease)
			{
				if(!vFingers.empty())
					return 0;
				m_WaitForRelease = false;
			}
			m_vMapFingers.erase(std::remove_if(m_vMapFingers.begin(), m_vMapFingers.end(), [&](const auto &Identity) {
				return std::none_of(vFingers.begin(), vFingers.end(), [&](const auto &Finger) { return Finger.m_Finger == Identity; });
			}),
				m_vMapFingers.end());
			for(const auto &Finger : vFingers)
				if(std::find(m_vPrevious.begin(), m_vPrevious.end(), Finger.m_Finger) == m_vPrevious.end() && !InsideControl(Layout, Finger.m_Position * Screen))
					m_vMapFingers.push_back(Finger.m_Finger);
			const bool CanPinch = vFingers.size() == 2 &&
					      std::all_of(vFingers.begin(), vFingers.end(), [&](const auto &Finger) {
						      return std::find(m_vMapFingers.begin(), m_vMapFingers.end(), Finger.m_Finger) != m_vMapFingers.end();
					      });
			if(m_Pinch.Active() && !CanPinch)
			{
				// 捏合结束后不把剩余手指转交给摇杆或按钮。
				Cancel();
				if(vFingers.empty())
				{
					m_WaitForRelease = false;
					RememberFingers(vFingers);
				}
				return 0;
			}
			if(CanPinch)
			{
				m_ZoomFactor = m_Pinch.Update(vFingers[0].m_Finger, vFingers[0].m_Position * Screen,
					vFingers[1].m_Finger, vFingers[1].m_Position * Screen);
				RememberFingers(vFingers);
				return 0;
			}
			bool JoystickPresent = false;
			for(const auto &Finger : vFingers)
			{
				const vec2 Position = Finger.m_Position * Screen;
				const bool NewPress = std::find(m_vPrevious.begin(), m_vPrevious.end(), Finger.m_Finger) == m_vPrevious.end();
				// 拖动取景：只有落在空白区域的那一根手指算镜头手势，按钮与摇杆优先。
				if(vFingers.size() == 1 && std::find(m_vMapFingers.begin(), m_vMapFingers.end(), Finger.m_Finger) != m_vMapFingers.end())
					m_PanDelta += Finger.m_Delta;
				if(NewPress && Layout.m_JoystickActive && !m_JoystickFinger.has_value() && distance(Position, Layout.m_Joystick.Center()) <= Layout.m_Joystick.w * 0.5f)
					m_JoystickFinger = Finger.m_Finger;
				if(m_JoystickFinger.has_value() && Finger.m_Finger == *m_JoystickFinger)
				{
					JoystickPresent = true;
					m_Direction = Direction(Position - Layout.m_Joystick.Center(), Layout.m_Joystick.w * 0.5f);
				}
				else if(NewPress)
				{
					for(int i = 0; i < NUM_ACTIONS; ++i)
						if(Layout.m_aButtons[i].Inside(Position))
						{
							if(i == SELECT)
								m_OpenSelectorOnRelease = true;
							else
								Actions |= 1U << i;
						}
				}
			}
			// 选择器会读取原始触摸；等打开按钮的手指松开，避免同次点击误选或关闭面板。
			if(m_OpenSelectorOnRelease)
			{
				m_Direction = vec2(0.0f, 0.0f);
				m_JoystickFinger.reset();
				if(vFingers.empty())
				{
					Actions |= 1U << SELECT;
					m_OpenSelectorOnRelease = false;
				}
			}
			if(!JoystickPresent)
				m_JoystickFinger.reset();
			RememberFingers(vFingers);
			return Actions;
		}
	};

	class CJoinState
	{
		double m_NextRequest = 0.0;

	public:
		void Reset() { m_NextRequest = 0.0; }
		bool Update(bool Online, bool HasLocalInfo, bool Spectating, double Now)
		{
			if(!Online || Spectating)
			{
				Reset();
				return false;
			}
			if(!HasLocalInfo || Now < m_NextRequest)
				return false;
			m_NextRequest = Now + 3.0;
			return true;
		}
	};
}

#endif
