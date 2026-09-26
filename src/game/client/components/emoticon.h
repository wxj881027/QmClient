/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_EMOTICON_H
#define GAME_CLIENT_COMPONENTS_EMOTICON_H
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/emoticon_projectile.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/ui.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace QmEmoticon
{
	struct SSelectorCharge
	{
		static constexpr float REQUIRED_SECONDS = 1.5f;

		void Reset()
		{
			m_TrackedEmote = -1;
			m_Started = 0;
		}

		float Update(int SelectedEmote, int64_t Now, int64_t Frequency)
		{
			if(m_TrackedEmote != SelectedEmote)
			{
				m_TrackedEmote = SelectedEmote;
				m_Started = Now;
			}
			if(SelectedEmote < 0 || Frequency <= 0)
				return 0.0f;
			return std::clamp(static_cast<float>((Now - m_Started) / static_cast<double>(Frequency) / REQUIRED_SECONDS), 0.0f, 1.0f);
		}

	private:
		int m_TrackedEmote = -1;
		int64_t m_Started = 0;
	};

	enum class EEffect
	{
		INVALID,
		NONE,
		SUPER_HEAD,
		PROJECTILE,
		SUPER_PROJECTILE,
	};

	inline EEffect ResolveEffect(int Emoticon, bool LaunchMode, bool SuperLaunch)
	{
		if(Emoticon < 0 || Emoticon >= NUM_EMOTICONS)
			return EEffect::INVALID;
		if(LaunchMode)
			return SuperLaunch ? EEffect::SUPER_PROJECTILE : EEffect::PROJECTILE;
		return SuperLaunch ? EEffect::SUPER_HEAD : EEffect::NONE;
	}

	inline EEffect ConsumeEffect(int Emoticon, bool LaunchMode, bool &SuperPending, bool ForceLaunch = false)
	{
		const bool SuperLaunch = SuperPending;
		SuperPending = false;
		return ResolveEffect(Emoticon, LaunchMode || ForceLaunch, SuperLaunch);
	}

	// 远端表情事件（实时通道收到别人的表情）该产生什么效果：先看表情总开关与忽略名单，
	// 再由两个开关分别拦下「他人的超大表情（头顶大表情）」与「他人的发射表情（投射物）」。
	inline EEffect ResolveRemoteEffect(int Emoticon, bool LaunchMode, bool SuperLaunch, bool ShowEmotes, bool EmoticonIgnored, bool ShowSuper, bool ShowLaunch)
	{
		if(!ShowEmotes || EmoticonIgnored)
			return EEffect::NONE;
		const EEffect Effect = ResolveEffect(Emoticon, LaunchMode, SuperLaunch);
		if((Effect == EEffect::SUPER_HEAD && !ShowSuper) ||
			((Effect == EEffect::PROJECTILE || Effect == EEffect::SUPER_PROJECTILE) && !ShowLaunch))
			return EEffect::NONE;
		return Effect;
	}
}

struct SQmLocalBlinkState
{
	static constexpr int DURATION_TICKS = 4;

	void Trigger(int CurrentTick)
	{
		m_StopTick = CurrentTick + DURATION_TICKS;
	}

	void Reset()
	{
		m_StopTick = 0;
	}

	bool IsActive(int CurrentTick) const
	{
		return CurrentTick >= 0 && CurrentTick < m_StopTick;
	}

private:
	int m_StopTick = 0;
};

class CEmoticon : public CComponent
{
	bool m_WasActive;
	bool m_Active;
	bool m_PresentationInitialized;

	vec2 m_SelectorMouse;
	int m_SelectedEmote;
	int m_SelectedEyeEmote;
	SQmLocalBlinkState m_aLocalBlinkStates[NUM_DUMMIES];

	CUi::CTouchState m_TouchState;
	bool m_TouchPressedOutside;
	std::array<CEmoticonProjectile, 64> m_aProjectiles;
	std::array<QmEmoticon::CAlphaMask, NUM_EMOTICONS> m_aCollisionMasks;
	bool m_LaunchModeActive = false;
	QmEmoticon::SSelectorCharge m_SuperCharge;
	float m_SuperChargeProgress = 0.0f;
	int m_SuperChargeRingEmote = -1;
	float m_SuperChargeRingPhase = 0.0f;
	float m_SuperChargeRingCharge = 0.0f;
	int m_SuperChargeRingExitEmote = -1;
	float m_SuperChargeRingExitPhase = 0.0f;
	float m_SuperChargeRingExitCharge = 0.0f;
	bool m_SuperLaunchPending = false;

	static void ConKeyEmoticon(IConsole::IResult *pResult, void *pUserData);
	static void ConSuperEmote(IConsole::IResult *pResult, void *pUserData);
	static void ConLocalBlink(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleLaunchMode(IConsole::IResult *pResult, void *pUserData);
	void ToggleLaunchMode();
	void UpdateSelection();
	void SetActive(bool Active);
	void RenderProjectiles();
	void SpawnProjectile(vec2 Position, vec2 Direction, int Emoticon, bool Super, int OwnerClientId);
	// 本机自己的头顶大表情；远端玩家的按 ClientId 各存一份（-1 表示没有）。
	int m_LocalSuperHeadEmoticon = -1;
	int m_LocalSuperHeadExpireTick = -1;
	int m_aRemoteSuperHeadEmoticons[MAX_CLIENTS] = {};
	int m_aRemoteSuperHeadExpireTicks[MAX_CLIENTS] = {};

public:
	CEmoticon();
	class CRenderProjectiles : public CComponent
	{
	public:
		CEmoticon *m_pEmoticon = nullptr;
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override { m_pEmoticon->RenderProjectiles(); }
	} m_RenderProjectiles;
	void SetCollisionMask(int Emoticon, const unsigned char *pPixels, int Width, int Height, int Stride)
	{
		if(Emoticon >= 0 && Emoticon < NUM_EMOTICONS)
			m_aCollisionMasks[Emoticon].Build(pPixels, Width, Height, Stride);
	}
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnConsoleInit() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void Emote(int Emoticon, bool ForceLaunch = false);
	void SuperEmote(int Emoticon);
	void EyeEmote(int EyeEmote);
	void TriggerLocalBlink();
	bool ShouldRenderLocalBlink(int ClientId) const;
	// 头顶大表情（super emote）：本机自己与远端玩家的超大表情各记一份，带过期 tick。
	bool IsLocalSuperHeadEmoticon(int ClientId, int Emoticon) const;
	bool IsLaunchModeActive() const { return m_LaunchModeActive; }

	bool IsActive() const { return m_Active; }

	friend class CBindWheel;
};

#endif
