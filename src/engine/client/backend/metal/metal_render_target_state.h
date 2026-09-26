#ifndef ENGINE_CLIENT_BACKEND_METAL_METAL_RENDER_TARGET_STATE_H
#define ENGINE_CLIENT_BACKEND_METAL_METAL_RENDER_TARGET_STATE_H

class CMetalRenderTargetState
{
public:
	bool Begin(int TargetId);
	bool End();
	bool IsActive() const;
	int ActiveTargetId() const;
	bool IsActiveTarget(int TargetId) const;
	bool CanDestroy(int TargetId) const;
	bool CanDraw(int TargetId) const;
	void Reset();

private:
	int m_ActiveTargetId = -1;
};

#endif
