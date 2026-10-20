#ifndef GAME_EDITOR_MAPITEMS_LEGACY_SOUND_SOURCE_H
#define GAME_EDITOR_MAPITEMS_LEGACY_SOUND_SOURCE_H

#include <game/mapitems.h>

class CSoundSourceDeprecated
{
public:
	CPoint m_Position;
	int m_Loop;
	int m_TimeDelay;
	int m_FalloffDistance;
	int m_PosEnv;
	int m_PosEnvOffset;
	int m_SoundEnv;
	int m_SoundEnvOffset;
};

namespace EditorMapLoad
{
	// 保留旧声音来源的圆形范围、延迟和包络映射。
	inline CSoundSource ConvertLegacySoundSource(const CSoundSourceDeprecated &Old)
	{
		CSoundSource Source{};
		Source.m_Position = Old.m_Position;
		Source.m_Loop = Old.m_Loop;
		Source.m_Pan = true;
		Source.m_TimeDelay = Old.m_TimeDelay;
		Source.m_Falloff = 0;
		Source.m_PosEnv = Old.m_PosEnv;
		Source.m_PosEnvOffset = Old.m_PosEnvOffset;
		Source.m_SoundEnv = Old.m_SoundEnv;
		Source.m_SoundEnvOffset = Old.m_SoundEnvOffset;
		Source.m_Shape.m_Type = CSoundShape::SHAPE_CIRCLE;
		Source.m_Shape.m_Circle.m_Radius = Old.m_FalloffDistance;
		return Source;
	}
}

#endif
