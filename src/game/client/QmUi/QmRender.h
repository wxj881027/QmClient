/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_UI_QM_RENDER_H
#define GAME_CLIENT_QM_UI_QM_RENDER_H

struct SUiV2RenderStats
{
	int m_DrawCalls = 0;
	int m_ClipPushes = 0;
};

class CUiV2RenderBridge
{
public:
	void BeginFrame();
	void RecordDrawCall();
	void RecordClipPush();
	const SUiV2RenderStats &LastFrameStats() const;

private:
	SUiV2RenderStats m_LastFrameStats;
};

#endif
