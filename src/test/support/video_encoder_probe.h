#ifndef TEST_SUPPORT_VIDEO_ENCODER_PROBE_H
#define TEST_SUPPORT_VIDEO_ENCODER_PROBE_H

#include <engine/client/video_encoder.h>

inline video_encoder::SProbeResult MakeVideoEncoderProbe(const char *pName, const char *pDescription, bool Available = true)
{
	video_encoder::SProbeResult Result{};
	str_copy(Result.m_aName, pName);
	str_copy(Result.m_aDescription, pDescription);
	Result.m_Available = Available;
	return Result;
}

#endif
