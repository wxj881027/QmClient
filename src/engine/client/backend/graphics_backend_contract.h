#ifndef ENGINE_CLIENT_BACKEND_GRAPHICS_BACKEND_CONTRACT_H
#define ENGINE_CLIENT_BACKEND_GRAPHICS_BACKEND_CONTRACT_H

#include <base/detect.h>

#include <engine/graphics.h>

#include <limits>
#include <string_view>

namespace graphics_backend
{
	enum EGraphicsMode
	{
		GRAPHICS_MODE_COMPATIBILITY = 0,
		GRAPHICS_MODE_PERFORMANCE = 1,
	};

	struct SSafeBackendConfig
	{
		const char *m_pBackend;
		int m_GLMajor;
		int m_GLMinor;
		int m_GLPatch;
		int m_FsaaSamples;
		int m_Fullscreen;
		int m_Borderless;
	};

	constexpr SSafeBackendConfig SafeBackendConfig()
	{
		return {"OpenGL", 4, 1, 0, 0, 0, 0};
	}

	// 图形崩溃恢复时避免把全屏用户缩进普通窗口：窗口模式保持窗口，
	// 其他模式统一降级为桌面全屏，绕开独占全屏但保留桌面尺寸。
	constexpr int RecoveryFullscreenMode(int CurrentFullscreenMode)
	{
		return CurrentFullscreenMode == 0 ? 0 : 2;
	}

	struct SRecoveryFailures
	{
		int m_aCount[BACKEND_TYPE_AUTO] = {};

		void Record(EBackendType Backend)
		{
			if(Backend >= BACKEND_TYPE_OPENGL && Backend < BACKEND_TYPE_AUTO && m_aCount[Backend] < std::numeric_limits<int>::max())
				++m_aCount[Backend];
		}

		bool IsBlocked(EBackendType Backend) const
		{
			return Backend >= BACKEND_TYPE_OPENGL && Backend < BACKEND_TYPE_AUTO && m_aCount[Backend] >= 2;
		}
	};

	inline EBackendType BackendFromCrashReport(std::string_view Report)
	{
		constexpr std::string_view Prefix = "Graphics backend: ";
		constexpr std::string_view ConfiguredPrefix = "Configured graphics backend: ";
		std::size_t Start = 0;
		while(Start < Report.size())
		{
			const std::size_t End = Report.find('\n', Start);
			const std::string_view Line = Report.substr(Start, End == std::string_view::npos ? End : End - Start);
			if(Line.substr(0, Prefix.size()) == Prefix || Line.substr(0, ConfiguredPrefix.size()) == ConfiguredPrefix)
			{
				const std::string_view Value = Line.substr(Line.substr(0, Prefix.size()) == Prefix ? Prefix.size() : ConfiguredPrefix.size());
				if(Value == "GLES" || Value == "GLES\r" || Value.substr(0, 5) == "GLES " || Value == "OpenGL ES" || Value == "OpenGL ES\r" || Value.substr(0, 10) == "OpenGL ES ")
					return BACKEND_TYPE_OPENGL_ES;
				if(Value == "OpenGL" || Value == "OpenGL\r" || Value.substr(0, 7) == "OpenGL ")
					return BACKEND_TYPE_OPENGL;
				if(Value == "Vulkan" || Value == "Vulkan\r" || Value.substr(0, 7) == "Vulkan ")
					return BACKEND_TYPE_VULKAN;
				if(Value == "Metal" || Value == "Metal\r" || Value.substr(0, 6) == "Metal ")
					return BACKEND_TYPE_METAL;
				return BACKEND_TYPE_AUTO;
			}
			if(End == std::string_view::npos)
				break;
			Start = End + 1;
		}
		return BACKEND_TYPE_AUTO;
	}

	constexpr bool IsMetalCompiled()
	{
#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
		return true;
#else
		return false;
#endif
	}

	constexpr bool IsBackendCompiled(EBackendType BackendType)
	{
		switch(BackendType)
		{
		case BACKEND_TYPE_OPENGL:
			return true;
		case BACKEND_TYPE_OPENGL_ES:
#if defined(CONF_BACKEND_OPENGL_ES) || defined(CONF_BACKEND_OPENGL_ES3)
			return true;
#else
			return false;
#endif
		case BACKEND_TYPE_VULKAN:
#if defined(CONF_BACKEND_VULKAN)
			return true;
#else
			return false;
#endif
		case BACKEND_TYPE_METAL:
			return IsMetalCompiled();
		case BACKEND_TYPE_AUTO:
		case BACKEND_TYPE_COUNT:
			return false;
		}
		return false;
	}

	constexpr bool IsBackendSelectable(EBackendType BackendType)
	{
		return BackendType != BACKEND_TYPE_AUTO && IsBackendCompiled(BackendType);
	}

	inline const char *BackendNameForGraphicsMode(int Mode)
	{
		if(Mode == GRAPHICS_MODE_PERFORMANCE)
		{
#if defined(CONF_PLATFORM_MACOS)
			return IsBackendCompiled(BACKEND_TYPE_METAL) ? "Metal" : (IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "OpenGL");
#elif defined(CONF_PLATFORM_IOS)
			return IsBackendCompiled(BACKEND_TYPE_METAL) ? "Metal" : (IsBackendCompiled(BACKEND_TYPE_OPENGL_ES) ? "GLES" : "OpenGL");
#elif defined(CONF_PLATFORM_ANDROID)
			return IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "GLES";
#else
			return IsBackendCompiled(BACKEND_TYPE_VULKAN) ? "Vulkan" : "OpenGL";
#endif
		}
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
		return IsBackendCompiled(BACKEND_TYPE_OPENGL_ES) ? "GLES" : "OpenGL";
#else
		return "OpenGL";
#endif
	}

	constexpr bool UsesOpenGLVersionTuple(EBackendType BackendType)
	{
		return BackendType == BACKEND_TYPE_OPENGL || BackendType == BACKEND_TYPE_OPENGL_ES;
	}

	constexpr bool PreservesOpenGLVersionTuple(EBackendType BackendType)
	{
		return BackendType == BACKEND_TYPE_METAL;
	}

	constexpr bool RequiresFrameSerializationWorkaround(EBackendType BackendType)
	{
		return BackendType == BACKEND_TYPE_VULKAN;
	}

	const char *BackendName(EBackendType BackendType);
	bool IsKnownBackendName(const char *pName);
	bool IsKnownUnavailableBackendName(const char *pName);
	EBackendType ParseBackendName(const char *pName, EBackendType Fallback);
	EBackendType ResolveBackend(EBackendType Requested, EBackendType Fallback);
	bool MatchesConfiguredBackend(EBackendType CandidateBackend, const char *pCandidateName, int CandidateMajor, int CandidateMinor, int CandidatePatch, const char *pConfiguredName, int ConfiguredMajor, int ConfiguredMinor, int ConfiguredPatch);

	inline int ModeForRecoveryBackend(EBackendType Backend)
	{
		return Backend == ParseBackendName(BackendNameForGraphicsMode(GRAPHICS_MODE_COMPATIBILITY), BACKEND_TYPE_AUTO) ?
			       GRAPHICS_MODE_COMPATIBILITY :
			       GRAPHICS_MODE_PERFORMANCE;
	}

	inline EBackendType RecoveryBackend(const SRecoveryFailures &Failures, EBackendType CrashedBackend)
	{
		if(CrashedBackend == BACKEND_TYPE_AUTO)
			return BACKEND_TYPE_AUTO;
		for(int Mode = GRAPHICS_MODE_COMPATIBILITY; Mode <= GRAPHICS_MODE_PERFORMANCE; ++Mode)
		{
			const EBackendType Candidate = ParseBackendName(BackendNameForGraphicsMode(Mode), BACKEND_TYPE_AUTO);
			if(Candidate != BACKEND_TYPE_AUTO && Candidate != CrashedBackend && !Failures.IsBlocked(Candidate))
				return Candidate;
		}
		return BACKEND_TYPE_AUTO;
	}
} // namespace graphics_backend

#endif
