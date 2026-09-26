// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "monitoring.h"

#include <base/system.h>

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <utility>
#include <vector>

#if defined(CONF_FAMILY_UNIX)
#include <sys/resource.h>
#endif

#if defined(CONF_PLATFORM_MACOS)
#include <mach/mach.h>
#endif

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#define IStorage IStorageCOM
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#undef IStorage

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace
{

	static float SampleProcessCpuUsagePct()
	{
#if defined(CONF_FAMILY_UNIX)
		static int64_t s_LastWallNs = 0;
		static int64_t s_LastCpuNs = 0;

		struct rusage Usage;
		if(getrusage(RUSAGE_SELF, &Usage) != 0)
			return -1.0f;

		const int64_t WallNs = time_get_nanoseconds().count();
		const int64_t CpuNs =
			(int64_t)Usage.ru_utime.tv_sec * 1000000000LL + (int64_t)Usage.ru_utime.tv_usec * 1000LL +
			(int64_t)Usage.ru_stime.tv_sec * 1000000000LL + (int64_t)Usage.ru_stime.tv_usec * 1000LL;

		if(s_LastWallNs == 0 || WallNs <= s_LastWallNs || CpuNs < s_LastCpuNs)
		{
			s_LastWallNs = WallNs;
			s_LastCpuNs = CpuNs;
			return -1.0f;
		}

		const int64_t WallDeltaNs = WallNs - s_LastWallNs;
		const int64_t CpuDeltaNs = CpuNs - s_LastCpuNs;
		s_LastWallNs = WallNs;
		s_LastCpuNs = CpuNs;

		const float RawCpuUsagePct = (float)std::max((double)CpuDeltaNs / (double)WallDeltaNs * 100.0, 0.0);
		return QmNormalizeProcessCpuUsagePct(RawCpuUsagePct, std::thread::hardware_concurrency());
#elif defined(CONF_FAMILY_WINDOWS)
		static uint64_t s_LastWall100Ns = 0;
		static uint64_t s_LastCpu100Ns = 0;

		FILETIME CreationTime, ExitTime, KernelTime, UserTime, SystemTime;
		if(!GetProcessTimes(GetCurrentProcess(), &CreationTime, &ExitTime, &KernelTime, &UserTime))
			return -1.0f;
#if _WIN32_WINNT >= 0x0602
		GetSystemTimePreciseAsFileTime(&SystemTime);
#else
		GetSystemTimeAsFileTime(&SystemTime);
#endif

		ULARGE_INTEGER Wall;
		Wall.LowPart = SystemTime.dwLowDateTime;
		Wall.HighPart = SystemTime.dwHighDateTime;
		ULARGE_INTEGER Kernel;
		Kernel.LowPart = KernelTime.dwLowDateTime;
		Kernel.HighPart = KernelTime.dwHighDateTime;
		ULARGE_INTEGER User;
		User.LowPart = UserTime.dwLowDateTime;
		User.HighPart = UserTime.dwHighDateTime;

		const uint64_t Wall100Ns = Wall.QuadPart;
		const uint64_t Cpu100Ns = Kernel.QuadPart + User.QuadPart;
		if(s_LastWall100Ns == 0 || Wall100Ns <= s_LastWall100Ns || Cpu100Ns < s_LastCpu100Ns)
		{
			s_LastWall100Ns = Wall100Ns;
			s_LastCpu100Ns = Cpu100Ns;
			return -1.0f;
		}

		const uint64_t WallDelta100Ns = Wall100Ns - s_LastWall100Ns;
		const uint64_t CpuDelta100Ns = Cpu100Ns - s_LastCpu100Ns;
		s_LastWall100Ns = Wall100Ns;
		s_LastCpu100Ns = Cpu100Ns;

		const float RawCpuUsagePct = (float)std::max((double)CpuDelta100Ns / (double)WallDelta100Ns * 100.0, 0.0);
		return QmNormalizeProcessCpuUsagePct(RawCpuUsagePct, std::thread::hardware_concurrency());
#else
		return -1.0f;
#endif
	}

	static float SampleTotalCpuUsagePct()
	{
#if defined(CONF_PLATFORM_LINUX)
		static uint64_t s_LastIdle = 0;
		static uint64_t s_LastTotal = 0;

		FILE *pFile = std::fopen("/proc/stat", "r");
		if(pFile == nullptr)
			return -1.0f;

		char aLabel[8] = {};
		uint64_t User = 0;
		uint64_t Nice = 0;
		uint64_t System = 0;
		uint64_t Idle = 0;
		uint64_t Iowait = 0;
		uint64_t Irq = 0;
		uint64_t Softirq = 0;
		uint64_t Steal = 0;
		// fscanf 不报告哪个转换失败；ReadCount < 5 已把字段缺失/非数字当作读取失败处理。
		// NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
		const int ReadCount = std::fscanf(pFile, "%7s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64, aLabel, &User, &Nice, &System, &Idle, &Iowait, &Irq, &Softirq, &Steal);
		std::fclose(pFile);
		if(ReadCount < 5 || std::strcmp(aLabel, "cpu") != 0)
			return -1.0f;

		const uint64_t IdleAll = Idle + Iowait;
		const uint64_t Total = User + Nice + System + Idle + Iowait + Irq + Softirq + Steal;
		if(s_LastTotal == 0 || Total <= s_LastTotal || IdleAll < s_LastIdle)
		{
			s_LastIdle = IdleAll;
			s_LastTotal = Total;
			return -1.0f;
		}

		const float UsagePct = QmComputeTotalCpuUsagePct(s_LastIdle, s_LastTotal, IdleAll, Total);
		s_LastIdle = IdleAll;
		s_LastTotal = Total;
		return UsagePct;
#elif defined(CONF_FAMILY_WINDOWS)
		static uint64_t s_LastIdle100Ns = 0;
		static uint64_t s_LastKernel100Ns = 0;
		static uint64_t s_LastUser100Ns = 0;

		FILETIME IdleTime, KernelTime, UserTime;
		if(!GetSystemTimes(&IdleTime, &KernelTime, &UserTime))
			return -1.0f;

		ULARGE_INTEGER Idle;
		Idle.LowPart = IdleTime.dwLowDateTime;
		Idle.HighPart = IdleTime.dwHighDateTime;
		ULARGE_INTEGER Kernel;
		Kernel.LowPart = KernelTime.dwLowDateTime;
		Kernel.HighPart = KernelTime.dwHighDateTime;
		ULARGE_INTEGER User;
		User.LowPart = UserTime.dwLowDateTime;
		User.HighPart = UserTime.dwHighDateTime;

		if(s_LastKernel100Ns == 0 || Kernel.QuadPart < s_LastKernel100Ns || User.QuadPart < s_LastUser100Ns || Idle.QuadPart < s_LastIdle100Ns)
		{
			s_LastIdle100Ns = Idle.QuadPart;
			s_LastKernel100Ns = Kernel.QuadPart;
			s_LastUser100Ns = User.QuadPart;
			return -1.0f;
		}

		const uint64_t PreviousTotal = s_LastKernel100Ns + s_LastUser100Ns;
		const uint64_t CurrentTotal = Kernel.QuadPart + User.QuadPart;
		const float UsagePct = QmComputeTotalCpuUsagePct(s_LastIdle100Ns, PreviousTotal, Idle.QuadPart, CurrentTotal);
		s_LastIdle100Ns = Idle.QuadPart;
		s_LastKernel100Ns = Kernel.QuadPart;
		s_LastUser100Ns = User.QuadPart;

		return UsagePct;
#else
		return -1.0f;
#endif
	}

	static float SampleProcessMemoryMb()
	{
#if defined(CONF_PLATFORM_MACOS)
		mach_task_basic_info Info;
		mach_msg_type_number_t Count = MACH_TASK_BASIC_INFO_COUNT;
		if(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&Info), &Count) != KERN_SUCCESS)
			return -1.0f;
		return (float)Info.resident_size / (1024.0f * 1024.0f);
#elif defined(CONF_FAMILY_UNIX)
		struct rusage Usage;
		if(getrusage(RUSAGE_SELF, &Usage) != 0)
			return -1.0f;
#if defined(CONF_PLATFORM_LINUX)
		return (float)Usage.ru_maxrss / 1024.0f;
#else
		return (float)Usage.ru_maxrss / (1024.0f * 1024.0f);
#endif
#elif defined(CONF_FAMILY_WINDOWS)
		PROCESS_MEMORY_COUNTERS_EX Counters;
		if(!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&Counters), sizeof(Counters)))
			return -1.0f;
		return (float)Counters.WorkingSetSize / (1024.0f * 1024.0f);
#else
		return -1.0f;
#endif
	}

#if defined(CONF_FAMILY_WINDOWS)
	class CWindowsDevicePerfSource
	{
		bool m_GpuCountersAttemptedInit = false;
		bool m_GpuCountersAvailable = false;
		bool m_GpuCountersPrimed = false;
		PDH_HQUERY m_Query = nullptr;
		std::vector<PDH_HCOUNTER> m_vCounters;
		bool m_AdapterAttemptedInit = false;
		IDXGIAdapter3 *m_pAdapter = nullptr;
		uint64_t m_LastReadBytes = 0;
		uint64_t m_LastReadTickNs = 0;

		void EnsureGpuCountersInitialized()
		{
			if(m_GpuCountersAttemptedInit)
				return;
			m_GpuCountersAttemptedInit = true;
			if(PdhOpenQueryW(nullptr, 0, &m_Query) != ERROR_SUCCESS)
				return;

			DWORD PathSize = 0;
			if(PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &PathSize, 0) != PDH_MORE_DATA || PathSize == 0)
				return;

			std::vector<wchar_t> vPaths(PathSize);
			if(PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Utilization Percentage", vPaths.data(), &PathSize, 0) != ERROR_SUCCESS)
				return;

			for(const wchar_t *pPath = vPaths.data(); *pPath != L'\0'; pPath += std::wcslen(pPath) + 1)
			{
				PDH_HCOUNTER Counter = nullptr;
				if(PdhAddEnglishCounterW(m_Query, pPath, 0, &Counter) == ERROR_SUCCESS)
					m_vCounters.push_back(Counter);
			}
			m_GpuCountersAvailable = !m_vCounters.empty();
			if(m_GpuCountersAvailable)
			{
				PdhCollectQueryData(m_Query);
				m_GpuCountersPrimed = true;
			}
		}

		void EnsureAdapterInitialized()
		{
			if(m_AdapterAttemptedInit)
				return;
			m_AdapterAttemptedInit = true;
			IDXGIFactory1 *pFactory = nullptr;
			if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&pFactory))))
			{
				IDXGIAdapter1 *pAdapter1 = nullptr;
				if(SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter1)))
				{
					pAdapter1->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&m_pAdapter));
					pAdapter1->Release();
				}
				pFactory->Release();
			}
		}

	public:
		~CWindowsDevicePerfSource()
		{
			for(PDH_HCOUNTER Counter : m_vCounters)
			{
				if(Counter != nullptr)
					PdhRemoveCounter(Counter);
			}
			m_vCounters.clear();
			if(m_Query != nullptr)
				PdhCloseQuery(m_Query);
			m_Query = nullptr;
			if(m_pAdapter != nullptr)
				m_pAdapter->Release();
			m_pAdapter = nullptr;
		}

		float SampleGpuUtilPct()
		{
			EnsureGpuCountersInitialized();
			if(!m_GpuCountersAvailable || m_Query == nullptr)
				return -1.0f;
			if(!m_GpuCountersPrimed)
			{
				m_GpuCountersPrimed = PdhCollectQueryData(m_Query) == ERROR_SUCCESS;
				return -1.0f;
			}
			if(PdhCollectQueryData(m_Query) != ERROR_SUCCESS)
				return -1.0f;

			double TotalUtil = 0.0;
			bool AnyValue = false;
			for(PDH_HCOUNTER Counter : m_vCounters)
			{
				PDH_FMT_COUNTERVALUE Value = {};
				if(PdhGetFormattedCounterValue(Counter, PDH_FMT_DOUBLE, nullptr, &Value) == ERROR_SUCCESS && Value.CStatus == ERROR_SUCCESS)
				{
					TotalUtil += Value.doubleValue;
					AnyValue = true;
				}
			}
			return AnyValue ? std::clamp((float)TotalUtil, 0.0f, 100.0f) : -1.0f;
		}

		void SampleGpuMemory(SQmDevicePerfSample &Sample)
		{
			EnsureAdapterInitialized();
			if(m_pAdapter == nullptr)
				return;

			DXGI_QUERY_VIDEO_MEMORY_INFO LocalInfo = {};
			if(SUCCEEDED(m_pAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &LocalInfo)))
			{
				Sample.m_GpuDedicatedVramMb = (float)LocalInfo.CurrentUsage / (1024.0f * 1024.0f);
				Sample.m_GpuDedicatedVramBudgetMb = (float)LocalInfo.Budget / (1024.0f * 1024.0f);
			}

			DXGI_QUERY_VIDEO_MEMORY_INFO NonLocalInfo = {};
			if(SUCCEEDED(m_pAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &NonLocalInfo)))
				Sample.m_GpuSharedVramMb = (float)NonLocalInfo.CurrentUsage / (1024.0f * 1024.0f);
		}

		SQmDevicePerfSample Sample()
		{
			SQmDevicePerfSample Sample;
			const uint64_t NowNs = (uint64_t)time_get_nanoseconds().count();

			Sample.m_GpuUtilPct = SampleGpuUtilPct();
			SampleGpuMemory(Sample);

			IO_COUNTERS IoCounters = {};
			if(GetProcessIoCounters(GetCurrentProcess(), &IoCounters))
			{
				Sample.m_DiskReadMbPerSec = QmComputeDiskReadMbPerSec(m_LastReadBytes, m_LastReadTickNs, IoCounters.ReadTransferCount, NowNs);
				m_LastReadBytes = IoCounters.ReadTransferCount;
				m_LastReadTickNs = NowNs;
			}

			Sample.m_Available =
				Sample.m_GpuUtilPct >= 0.0f ||
				Sample.m_GpuDedicatedVramMb >= 0.0f ||
				Sample.m_GpuDedicatedVramBudgetMb >= 0.0f ||
				Sample.m_GpuSharedVramMb >= 0.0f ||
				Sample.m_DiskReadMbPerSec >= 0.0f;
			return Sample;
		}
	};
#endif

} // namespace

SQmDevicePerfSample QmSampleProcessPerf()
{
	SQmDevicePerfSample Sample;
	Sample.m_CpuUsagePct = SampleProcessCpuUsagePct();
	Sample.m_TotalCpuUsagePct = SampleTotalCpuUsagePct();
	Sample.m_MemoryUsageMb = SampleProcessMemoryMb();
	Sample.m_Available = Sample.m_CpuUsagePct >= 0.0f || Sample.m_TotalCpuUsagePct >= 0.0f || Sample.m_MemoryUsageMb >= 0.0f;
	return Sample;
}

SQmDevicePerfSnapshot CQmDevicePerfSnapshotCache::Publish(const SQmDevicePerfSample &Sample)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_Snapshot.m_Sample = Sample;
	++m_Snapshot.m_Version;
	return m_Snapshot;
}

SQmDevicePerfSnapshot CQmDevicePerfSnapshotCache::Snapshot() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_Snapshot;
}

void CQmDevicePerfSnapshotCache::Reset()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_Snapshot = {};
}

CQmAsyncDevicePerfSampler::CQmAsyncDevicePerfSampler(FSampleOverride SampleOverride, std::chrono::milliseconds PollInterval) :
	m_SampleOverride(std::move(SampleOverride)),
	m_PollInterval(PollInterval.count() > 0 ? PollInterval : std::chrono::milliseconds(1))
{
}

CQmAsyncDevicePerfSampler::~CQmAsyncDevicePerfSampler()
{
	Stop();
}

void CQmAsyncDevicePerfSampler::EnsureStarted()
{
	std::lock_guard<std::mutex> Lock(m_StateMutex);
	if(m_Started || m_StopRequested)
		return;
	m_Started = true;
	m_Thread = std::thread(&CQmAsyncDevicePerfSampler::Run, this);
}

void CQmAsyncDevicePerfSampler::SetEnabled(bool Enabled)
{
	{
		std::lock_guard<std::mutex> Lock(m_StateMutex);
		if(m_Enabled == Enabled)
			return;
		m_Enabled = Enabled;
		++m_EnableGeneration;
		if(!Enabled)
			m_Cache.Reset();
	}
	m_StateCv.notify_all();
}

void CQmAsyncDevicePerfSampler::Stop()
{
	std::thread Thread;
	{
		std::lock_guard<std::mutex> Lock(m_StateMutex);
		if(!m_Started)
			return;
		if(m_StopRequested)
			return;
		m_StopRequested = true;
		m_Enabled = false;
		Thread = std::move(m_Thread);
	}
	m_StateCv.notify_all();
	if(Thread.joinable())
		Thread.join();
	{
		std::lock_guard<std::mutex> Lock(m_StateMutex);
		m_Cache.Reset();
		m_Started = false;
		m_StopRequested = false;
	}
}

SQmDevicePerfSnapshot CQmAsyncDevicePerfSampler::Snapshot() const
{
	return m_Cache.Snapshot();
}

void CQmAsyncDevicePerfSampler::Run()
{
#if defined(CONF_FAMILY_WINDOWS)
	std::unique_ptr<CWindowsDevicePerfSource> pWindowsSource;
	uint64_t SourceGeneration = 0;
#endif

	std::unique_lock<std::mutex> Lock(m_StateMutex);
	while(!m_StopRequested)
	{
		if(!m_Enabled)
		{
#if defined(CONF_FAMILY_WINDOWS)
			// 系统计数器的关闭也可能等待驱动，只能在状态锁外由工作线程回收。
			Lock.unlock();
			pWindowsSource.reset();
			Lock.lock();
#endif
			m_StateCv.wait(Lock, [&]() { return m_StopRequested || m_Enabled; });
			continue;
		}

		const uint64_t Generation = m_EnableGeneration;
		const std::chrono::milliseconds PollInterval = m_PollInterval;
		Lock.unlock();
		SQmDevicePerfSample Sample;
		if(m_SampleOverride)
			Sample = m_SampleOverride();
#if defined(CONF_FAMILY_WINDOWS)
		else
		{
			if(!pWindowsSource || SourceGeneration != Generation)
			{
				pWindowsSource.reset();
				pWindowsSource = std::make_unique<CWindowsDevicePerfSource>();
				SourceGeneration = Generation;
			}
			Sample = pWindowsSource->Sample();
		}
#endif
		Lock.lock();
		if(m_StopRequested)
			break;
		// 关闭后又立即开启时，也不能发布上一次会话仍在途的查询结果。
		if(!m_Enabled || Generation != m_EnableGeneration)
			continue;
		m_Cache.Publish(Sample);
		m_StateCv.wait_for(Lock, PollInterval, [&]() { return m_StopRequested || Generation != m_EnableGeneration; });
	}
}

void QmUpdateDevicePerfSamplerState(CQmAsyncDevicePerfSampler &Sampler, bool Enabled)
{
	if(!Enabled)
	{
		// 关闭只暂停采样；退出和析构仍通过 Stop 等待线程结束。
		Sampler.SetEnabled(false);
		return;
	}

	Sampler.EnsureStarted();
	Sampler.SetEnabled(true);
}
