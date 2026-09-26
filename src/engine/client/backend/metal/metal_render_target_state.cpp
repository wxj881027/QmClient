#include "metal_render_target_state.h"

bool CMetalRenderTargetState::Begin(int TargetId)
{
	if(TargetId < 0 || IsActive())
		return false;
	m_ActiveTargetId = TargetId;
	return true;
}

bool CMetalRenderTargetState::End()
{
	if(!IsActive())
		return false;
	m_ActiveTargetId = -1;
	return true;
}

bool CMetalRenderTargetState::IsActive() const
{
	return m_ActiveTargetId >= 0;
}

int CMetalRenderTargetState::ActiveTargetId() const
{
	return m_ActiveTargetId;
}

bool CMetalRenderTargetState::IsActiveTarget(int TargetId) const
{
	return TargetId >= 0 && m_ActiveTargetId == TargetId;
}

bool CMetalRenderTargetState::CanDestroy(int TargetId) const
{
	return TargetId >= 0 && !IsActiveTarget(TargetId);
}

bool CMetalRenderTargetState::CanDraw(int TargetId) const
{
	return TargetId >= 0 && !IsActiveTarget(TargetId);
}

void CMetalRenderTargetState::Reset()
{
	m_ActiveTargetId = -1;
}
