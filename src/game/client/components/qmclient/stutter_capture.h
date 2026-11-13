#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STUTTER_CAPTURE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STUTTER_CAPTURE_H

#include "stutter_diagnostics.h"

#include <string>
#include <utility>
#include <vector>

class IClient;
class IConfigManager;

class CQmStutterCapture
{
public:
	struct SUiContext
	{
		const char *m_pPage = nullptr;
		const char *m_pOperation = nullptr;
		EQmStutterLimitCause m_LimitCause = EQmStutterLimitCause::NONE;
	};
	struct SFrame
	{
		bool m_Enabled = false;
		bool m_Online = false;
		uint64_t m_Frame = 0;
		double m_FrameMs = 0.0;
		const char *const *m_ppComponentNames = nullptr;
		size_t m_ComponentCount = 0;
		IConfigManager *m_pConfig = nullptr;
		const IClient *m_pClient = nullptr;
		SUiContext (*m_pfnUiContext)(void *) = nullptr;
		void *m_pUser = nullptr;
	};
	struct SFeatures
	{
		std::vector<std::pair<std::string, int>> m_vValues;
		std::string m_Page;
		std::string m_Operation;
		EQmStutterLimitCause m_LimitCause = EQmStutterLimitCause::NONE;
		void Swap(SFeatures &Other) noexcept
		{
			m_vValues.swap(Other.m_vValues);
			m_Page.swap(Other.m_Page);
			m_Operation.swap(Other.m_Operation);
			std::swap(m_LimitCause, Other.m_LimitCause);
		}
	};
	struct SComponent
	{
		CQmStutterSampleSeries m_Update;
		CQmStutterSampleSeries m_Render;
		double m_PendingUpdate = 0.0;
		double m_PendingRender = 0.0;
	};
	// 只替换真实容量准备、快照采集和输出边界；借用对象必须覆盖采集器使用期。
	class COperations
	{
	public:
		virtual ~COperations() = default;
		virtual void PrepareComponents(std::vector<SComponent> &vComponents, size_t Count);
		virtual void PrepareSeries(CQmStutterSampleSeries &Series, double Value);
		virtual SFeatures CaptureFeatures(const SFrame &Frame);
		virtual void Emit(const SFrame &Frame, bool Force, const char *pPayload);
	};

	explicit CQmStutterCapture(COperations *pOperations = nullptr) noexcept :
		m_pOperations(pOperations) {}
	bool PrepareComponents(size_t Count) noexcept;
	void RecordUpdate(size_t Index, double DurationMs) noexcept;
	void RecordRender(size_t Index, double DurationMs) noexcept;
	bool ProcessFrame(const SFrame &Frame) noexcept;
	bool Finish(const SFrame &Frame, EQmStutterFlushReason Reason = EQmStutterFlushReason::SHUTDOWN) noexcept;

	bool Active() const { return m_Tracker.Active(); }
	size_t SampleCount() const { return m_FrameSamples.Count(); }
	uint64_t StutterId() const { return m_Tracker.StutterId(); }
	uint64_t Segment() const { return m_Tracker.Segment(); }
	uint64_t WorstFrame() const { return m_WorstFrame; }

private:
	void ClearPending() noexcept;
	void ResetWindow() noexcept;
	bool Emit(const SFrame &Frame, bool Force, const char *pPayload) noexcept;
	bool EmitWindow(const SFrame &Frame, const SQmStutterFrameDecision &Decision, bool Force);
	bool FinishWindow(const SFrame &Frame, const SQmStutterFrameDecision &Decision, bool Force) noexcept;

	std::vector<SComponent> m_vComponents;
	CQmStutterEpisodeTracker m_Tracker;
	CQmStutterSampleSeries m_FrameSamples;
	SFeatures m_Features;
	bool m_WasEnabled = false;
	bool m_RejectFrame = false;
	uint64_t m_WindowStartFrame = 0;
	uint64_t m_WorstFrame = 0;
	double m_WorstFrameMs = 0.0;
	COperations *m_pOperations;
};

#endif
