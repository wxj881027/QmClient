// This file can be included several times.
#if (!defined(BACKEND_AS_OPENGL_ES) && !defined(ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H)) || \
	(defined(BACKEND_AS_OPENGL_ES) && !defined(ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H_AS_ES))

#if !defined(BACKEND_AS_OPENGL_ES) && !defined(ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H)
#define ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H
#endif

#if defined(BACKEND_AS_OPENGL_ES) && !defined(ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H_AS_ES)
#define ENGINE_CLIENT_BACKEND_OPENGL_OPENGL_QM_SL_PROGRAM_H_AS_ES
#endif

#include "opengl_sl_program.h"

class CGLSLMediaIslandSdfProgram : public CGLSLTWProgram
{
public:
	CGLSLMediaIslandSdfProgram() :
		m_LocData(-1)
	{
	}

	int m_LocData;
};

class CGLSLRoundedRectSdfProgram : public CGLSLTWProgram
{
public:
	CGLSLRoundedRectSdfProgram() :
		m_LocData(-1)
	{
	}

	int m_LocData;
};

class CGLSLProceduralRingProgram : public CGLSLTWProgram
{
public:
	CGLSLProceduralRingProgram() :
		m_LocParams(-1)
	{
	}

	int m_LocParams;
};

class CGLSLGaussianBlurProgram : public CGLSLTWProgram
{
public:
	CGLSLGaussianBlurProgram() :
		m_LocTexelOffset(-1),
		m_LocRadius(-1),
		m_LocMode(-1),
		m_LocPass(-1),
		m_LocWeights(-1)
	{
	}

	int m_LocTexelOffset;
	int m_LocRadius;
	int m_LocMode;
	int m_LocPass;
	int m_LocWeights;
};

#endif
