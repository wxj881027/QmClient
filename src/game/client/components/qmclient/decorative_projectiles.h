#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DECORATIVE_PROJECTILES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DECORATIVE_PROJECTILES_H

#include "decorative_throw_policy.h"
#include "emoticon_projectile.h"

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>

#include <array>

class CQmDecorativeProjectiles : public CComponent
{
	std::array<CEmoticonProjectile, 32> m_aProjectiles;
	std::array<QmEmoticon::CAlphaMask, QmDecorativeThrow::COUNT> m_aMasks;
	std::array<IGraphics::CTextureHandle, QmDecorativeThrow::COUNT> m_aTextures;
	std::array<int64_t, MAX_CLIENTS> m_aRemoteLastThrow{};
	int64_t m_LastThrow = 0;
	bool m_ResourcesAttempted = false;

	static void ConThrow(IConsole::IResult *pResult, void *pUserData);
	void EnsureResources();
	bool Spawn(int Type, int Owner, vec2 Origin, vec2 Direction);
	void Draw(int Type, vec2 Position, float Size, float Alpha, float Angle = 0.0f);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnRender() override;
	void Throw(int Type);
	void RenderSelector(vec2 Center, int Selected, float Scale, float Alpha);
};

#endif
