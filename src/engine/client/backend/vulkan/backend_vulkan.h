#ifndef ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_H
#define ENGINE_CLIENT_BACKEND_VULKAN_BACKEND_VULKAN_H

#include <algorithm>
#include <cstddef>
#include <limits>

class CCommandProcessorFragment_GLBase;

class CQmVulkanRenderScheduler
{
	// 少量绘制不启动工作线程；粒度需要实机帧时间比较。
	static constexpr size_t MIN_DRAWS_PER_WORKER = 64;
	size_t m_WorkerCount = 0;
	size_t m_DrawsPerWorker = 1;
	size_t m_RecordedDraws = 0;
	size_t m_ThreadIndex = 1;
	bool m_MainThreadOnly = false;

public:
	void StartCommands(size_t ThreadCount, size_t EstimatedDraws)
	{
		const size_t AvailableWorkers = ThreadCount > 1 ? ThreadCount - 1 : 0;
		m_WorkerCount = std::min(AvailableWorkers, EstimatedDraws / MIN_DRAWS_PER_WORKER);
		if(m_WorkerCount < 2)
			m_WorkerCount = 0;
		m_DrawsPerWorker = m_WorkerCount > 0 ? EstimatedDraws / m_WorkerCount + (EstimatedDraws % m_WorkerCount != 0) : 1;
		m_RecordedDraws = 0;
	}

	size_t CurrentThreadIndex() const { return m_MainThreadOnly ? 0 : m_ThreadIndex; }

	size_t ThreadIndex(bool ForceMainThread)
	{
		if(ForceMainThread || m_WorkerCount == 0)
			UseMainThread();
		if(m_MainThreadOnly)
			return 0;
		// 工作段保持单调，避免跨命令缓冲回到已经提交的线程。
		const size_t Worker = std::min(m_RecordedDraws / m_DrawsPerWorker, m_WorkerCount - 1);
		m_ThreadIndex = std::max(m_ThreadIndex, Worker + 1);
		return m_ThreadIndex;
	}

	void RecordDrawCalls(size_t DrawCalls)
	{
		m_RecordedDraws += std::min(DrawCalls, std::numeric_limits<size_t>::max() - m_RecordedDraws);
	}

	void UseMainThread() { m_MainThreadOnly = true; }

	void NewFrame()
	{
		// 主线程尾段执行在所有工作线程之后，只能在帧边界恢复工作线程。
		m_ThreadIndex = 1;
		m_MainThreadOnly = false;
	}
};

struct SVulkanVersion
{
	int m_Major;
	int m_Minor;
	int m_Patch;
};

static constexpr SVulkanVersion gs_BackendVulkanFallbackVersion = {1, 1, 0};
static constexpr SVulkanVersion gs_BackendVulkanMinimumVersion = {1, 1, 0};
static constexpr SVulkanVersion gs_BackendVulkanMaximumVersion = {1, 4, 0};

constexpr bool IsVulkanVersionAtLeast(const SVulkanVersion &Version, const SVulkanVersion &Required)
{
	if(Version.m_Major != Required.m_Major)
		return Version.m_Major > Required.m_Major;
	if(Version.m_Minor != Required.m_Minor)
		return Version.m_Minor > Required.m_Minor;
	return Version.m_Patch >= Required.m_Patch;
}

constexpr SVulkanVersion MinVulkanVersion(const SVulkanVersion &Left, const SVulkanVersion &Right)
{
	return IsVulkanVersionAtLeast(Left, Right) ? Right : Left;
}

constexpr SVulkanVersion ClampVulkanVersionToSupportedRange(const SVulkanVersion &Version)
{
	if(!IsVulkanVersionAtLeast(Version, gs_BackendVulkanMinimumVersion))
		return gs_BackendVulkanMinimumVersion;
	return MinVulkanVersion(Version, gs_BackendVulkanMaximumVersion);
}

constexpr SVulkanVersion ResolveConfiguredVulkanApiVersion(int ConfigValue)
{
	if(ConfigValue == 14)
		return gs_BackendVulkanMaximumVersion;
	if(ConfigValue == 13)
		return {1, 3, 0};
	return gs_BackendVulkanMinimumVersion;
}

constexpr SVulkanVersion ResolveVulkanVersionForLoader(const SVulkanVersion &Requested, const SVulkanVersion &Loader)
{
	if(IsVulkanVersionAtLeast(Loader, Requested))
		return Requested;
	if(IsVulkanVersionAtLeast(Requested, {1, 4, 0}) && IsVulkanVersionAtLeast(Loader, {1, 3, 0}))
		return {1, 3, 0};
	return gs_BackendVulkanMinimumVersion;
}

CCommandProcessorFragment_GLBase *CreateVulkanCommandProcessorFragment();

#endif
