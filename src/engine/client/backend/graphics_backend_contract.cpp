#include "graphics_backend_contract.h"

#include <base/str.h>

namespace graphics_backend
{
	const char *BackendName(EBackendType BackendType)
	{
		switch(BackendType)
		{
		case BACKEND_TYPE_OPENGL:
			return "OpenGL";
		case BACKEND_TYPE_OPENGL_ES:
			return "GLES";
		case BACKEND_TYPE_VULKAN:
			return "Vulkan";
		case BACKEND_TYPE_METAL:
			return "Metal";
		case BACKEND_TYPE_AUTO:
			return "Auto";
		case BACKEND_TYPE_COUNT:
			return "Unknown";
		}
		return "Unknown";
	}

	bool IsKnownBackendName(const char *pName)
	{
		if(pName == nullptr)
			return false;
		return str_comp_nocase(pName, "OpenGL") == 0 || str_comp_nocase(pName, "GLES") == 0 || str_comp_nocase(pName, "OpenGL ES") == 0 || str_comp_nocase(pName, "Vulkan") == 0 || str_comp_nocase(pName, "Metal") == 0;
	}

	bool IsKnownUnavailableBackendName(const char *pName)
	{
		return IsKnownBackendName(pName) && ParseBackendName(pName, BACKEND_TYPE_AUTO) == BACKEND_TYPE_AUTO;
	}

	EBackendType ParseBackendName(const char *pName, EBackendType Fallback)
	{
		if(pName == nullptr)
			return Fallback;
		if(str_comp_nocase(pName, "OpenGL") == 0)
			return BACKEND_TYPE_OPENGL;
		if(str_comp_nocase(pName, "GLES") == 0 || str_comp_nocase(pName, "OpenGL ES") == 0)
			return ResolveBackend(BACKEND_TYPE_OPENGL_ES, Fallback);
		if(str_comp_nocase(pName, "Vulkan") == 0)
			return ResolveBackend(BACKEND_TYPE_VULKAN, Fallback);
		if(str_comp_nocase(pName, "Metal") == 0)
			return ResolveBackend(BACKEND_TYPE_METAL, Fallback);
		return Fallback;
	}

	EBackendType ResolveBackend(EBackendType Requested, EBackendType Fallback)
	{
		return IsBackendSelectable(Requested) ? Requested : Fallback;
	}

	bool MatchesConfiguredBackend(EBackendType CandidateBackend, const char *pCandidateName, int CandidateMajor, int CandidateMinor, int CandidatePatch, const char *pConfiguredName, int ConfiguredMajor, int ConfiguredMinor, int ConfiguredPatch)
	{
		if(!IsBackendSelectable(CandidateBackend) || pCandidateName == nullptr || pConfiguredName == nullptr)
			return false;

		if(ParseBackendName(pConfiguredName, BACKEND_TYPE_AUTO) != CandidateBackend || str_comp_nocase(pCandidateName, BackendName(CandidateBackend)) != 0)
			return false;

		if(!UsesOpenGLVersionTuple(CandidateBackend))
			return true;

		return CandidateMajor == ConfiguredMajor && CandidateMinor == ConfiguredMinor && CandidatePatch == ConfiguredPatch;
	}
} // namespace graphics_backend
