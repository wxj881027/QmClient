/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H
#define GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H

#include <base/lock.h>

#include <engine/graphics.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

constexpr int QmNormalizeCountryCode(int CountryCodeValue)
{
	return CountryCodeValue >= ::CountryCode::MINIMUM && CountryCodeValue <= ::CountryCode::MAXIMUM ? CountryCodeValue : ::CountryCode::DEFAULT;
}

// default 是合法负数代码；无效选择不能覆盖玩家设置。
inline bool QmCommitCountrySelection(int *pCountry, int Selection)
{
	if(pCountry == nullptr || Selection < CountryCode::MINIMUM || Selection > CountryCode::MAXIMUM)
		return false;
	*pCountry = Selection;
	return true;
}

// 国旗加载入场动效参数
constexpr float COUNTRY_FLAG_ANIM_DURATION = 0.28f;
constexpr float COUNTRY_FLAG_ANIM_OVERSHOOT = 2.6f;
// 重现重放去抖间隔（秒）：距上次真实渲染超过该时长才视为重新出现，
// 避免单帧渲染抖动或快速滚动列表时反复触发入场动画。
constexpr float COUNTRY_FLAG_ANIM_REAPPEAR_GAP = 0.3f;

// 计算国旗入场弹性缩放：Progress 从 0 -> 1，Scale 从 0 -> 约 1.20 -> 1.00
inline float ComputeCountryFlagEntryScale(float Progress, float Overshoot = COUNTRY_FLAG_ANIM_OVERSHOOT)
{
	if(Progress <= 0.0f)
		return 0.0f;
	if(Progress >= 1.0f)
		return 1.0f;
	const float T = Progress - 1.0f;
	return 1.0f + (Overshoot + 1.0f) * T * T * T + Overshoot * T * T;
}

// 计算国旗入场淡入透明度比例：前 25% 时间从 0 -> 1
inline float ComputeCountryFlagEntryAlpha(float Progress)
{
	if(Progress <= 0.0f)
		return 0.0f;
	if(Progress >= 0.25f)
		return 1.0f;
	return Progress / 0.25f;
}

struct SCountryFlagEntryParams
{
	float m_Duration;
	float m_Overshoot;
};

// 按动效分级解析入场参数（唯一裁决点，勿在调用点重复分支）：
// Full 0.28s / 过冲 2.6（Scale 峰值约 1.20）；Reduced 0.16s / 1.4（收敛更快更克制）；
// 0 档由调用方直接禁用入场。
inline SCountryFlagEntryParams ResolveCountryFlagEntryParams(int MotionLevel)
{
	if(MotionLevel <= 1)
		return {0.16f, 1.4f};
	return {COUNTRY_FLAG_ANIM_DURATION, COUNTRY_FLAG_ANIM_OVERSHOOT};
}

// 计算中心锚点对齐缩放后的包围矩形
inline void ComputeCountryFlagEntryRect(float x, float y, float w, float h, float Scale, float &OutX, float &OutY, float &OutW, float &OutH)
{
	OutW = w * Scale;
	OutH = h * Scale;
	OutX = x + (w - OutW) * 0.5f;
	OutY = y + (h - OutH) * 0.5f;
}

enum class EQmCountryFlagRenderPass
{
	INTERACTIVE,
	BACKGROUND,
	MEASUREMENT,
};

struct SCountryFlagEntryVisual
{
	bool m_Visible = true;
	float m_Scale = 1.0f;
	float m_Alpha = 1.0f;
};

class CCountryFlags : public CComponent
{
public:
	struct CCountryFlag
	{
		int m_CountryCode;
		char m_aCountryCodeString[16];
		IGraphics::CTextureHandle m_Texture;
		bool m_Loaded = false;
		int64_t m_LoadedTimestamp = 0;
		// 动画起始时间，支持持续播放并在重现时重置；mutable 因 Render 持 const 引用。
		mutable int64_t m_AnimStartTimestamp = 0;
		// 后台显示也算可见；仅测量不更新时刻。mutable 因 Render 持 const 引用。
		mutable int64_t m_LastRenderTimestamp = 0;

		// Render 与行为测试共用的生命周期入口；时钟由调用方提供。
		SCountryFlagEntryVisual ResolveEntryAnimation(EQmCountryFlagRenderPass Pass, bool Enabled, int MotionLevel, int64_t CustomStartTime, int64_t Now, int64_t Frequency) const
		{
			SCountryFlagEntryVisual Visual;
			if(Pass == EQmCountryFlagRenderPass::MEASUREMENT)
				return Visual;

			Frequency = std::max<int64_t>(1, Frequency);
			const bool Reappeared = m_LastRenderTimestamp <= 0 || Now - m_LastRenderTimestamp >= (int64_t)(COUNTRY_FLAG_ANIM_REAPPEAR_GAP * Frequency);
			m_LastRenderTimestamp = Now;
			if(!Enabled || MotionLevel <= 0)
			{
				// 禁用时仍维持可见性，重新启用不能恢复已取消的动画。
				m_AnimStartTimestamp = 0;
				return Visual;
			}

			// 自定义错层动画只覆盖本次输出，不污染该纹理的自动入场进度。
			if(CustomStartTime <= 0 && Reappeared)
				m_AnimStartTimestamp = Now;
			const int64_t AnimationStartTime = CustomStartTime > 0 ? CustomStartTime : m_AnimStartTimestamp;
			if(AnimationStartTime <= 0)
				return Visual;
			if(Now < AnimationStartTime)
			{
				Visual.m_Visible = false;
				return Visual;
			}

			const SCountryFlagEntryParams EntryParams = ResolveCountryFlagEntryParams(MotionLevel);
			const float Duration = EntryParams.m_Duration;
			const float Elapsed = (Now - AnimationStartTime) / (float)Frequency;
			if(Elapsed < Duration)
			{
				const float Progress = Elapsed / Duration;
				Visual.m_Scale = ComputeCountryFlagEntryScale(Progress, EntryParams.m_Overshoot);
				Visual.m_Alpha = ComputeCountryFlagEntryAlpha(Progress);
			}
			return Visual;
		}

		bool operator<(const CCountryFlag &Other) const { return str_comp(m_aCountryCodeString, Other.m_aCountryCodeString) < 0; }
	};

	class CCountryFlagLoadJob : public IJob
	{
	public:
		struct SResult
		{
			CImageInfo m_Image;
			int m_CountryCode = CountryCode::DEFAULT;
			size_t m_FlagIndex = 0;
			bool m_Success = false;

			SResult() = default;
			SResult(const SResult &) = delete;
			SResult &operator=(const SResult &) = delete;
			SResult(SResult &&Other) :
				m_Image(std::move(Other.m_Image)), m_CountryCode(Other.m_CountryCode), m_FlagIndex(Other.m_FlagIndex), m_Success(std::exchange(Other.m_Success, false)) {}
			SResult &operator=(SResult &&Other)
			{
				if(this != &Other)
				{
					m_Image = std::move(Other.m_Image);
					m_CountryCode = Other.m_CountryCode;
					m_FlagIndex = Other.m_FlagIndex;
					m_Success = std::exchange(Other.m_Success, false);
				}
				return *this;
			}
			~SResult() { m_Image.Free(); }
		};

	private:
		std::string m_Path;
		const int m_CountryCode;
		const size_t m_FlagIndex;
		IStorage *m_pStorage;
		mutable CLock m_Mutex;
		SResult m_Result;
		bool m_Completed = false;

	protected:
		void Run() override REQUIRES(!m_Mutex);

	public:
		enum class ELoadStatus
		{
			SUCCESS,
			READ_FAILED,
			DECODE_FAILED,
			EXCEPTION,
		};

		CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage, size_t FlagIndex = 0);
		~CCountryFlagLoadJob() override = default;

		// 读取和解码是外部边界；失败也必须发布终态，不能只依赖线程池捕获异常。
		// Read 输出须能由 free 释放；Decode 输出的独立图像缓冲由结果接管。
		template<typename FRead, typename FDecode>
		ELoadStatus Load(FRead &&Read, FDecode &&Decode) REQUIRES(!m_Mutex)
		{
			struct SFileData
			{
				void *m_pData = nullptr;
				~SFileData() { free(m_pData); }
			} File;
			SResult Candidate;
			Candidate.m_CountryCode = m_CountryCode;
			Candidate.m_FlagIndex = m_FlagIndex;
			ELoadStatus Status = ELoadStatus::READ_FAILED;
			try
			{
				unsigned FileSize = 0;
				if(Read(&File.m_pData, &FileSize))
				{
					Status = ELoadStatus::DECODE_FAILED;
					if(Decode(File.m_pData, FileSize, Candidate.m_Image))
					{
						Candidate.m_Success = true;
						Status = ELoadStatus::SUCCESS;
					}
				}
			}
			catch(...)
			{
				Status = ELoadStatus::EXCEPTION;
			}
			if(!Candidate.m_Success)
				Candidate.m_Image.Free();
			{
				CLockScope Lock(m_Mutex);
				m_Result = std::move(Candidate);
				m_Completed = true;
			}
			return Status;
		}

		bool IsCompleted() const REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			return m_Completed;
		}

		SResult GetResult() REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			if(!m_Completed)
				return SResult();
			SResult Result = std::move(m_Result);
			m_Result = SResult();
			return Result;
		}
	};

	static CCountryFlag *FindFlagLoadTarget(std::vector<CCountryFlag> &Flags, const CCountryFlagLoadJob::SResult &Result)
	{
		if(Result.m_FlagIndex >= Flags.size() || Flags[Result.m_FlagIndex].m_CountryCode != Result.m_CountryCode)
			return nullptr;
		return &Flags[Result.m_FlagIndex];
	}

	// 提交返回 false 或抛异常时必须尚未接管作业；真实引擎使用 TryAddJob。
	template<typename TQueue, typename FCreate, typename FSubmit>
	static bool TryStartFlagLoad(TQueue &Pending, std::vector<bool> &Triggered, size_t Index, FCreate &&Create, FSubmit &&Submit)
	{
		if(Index >= Triggered.size() || Triggered[Index])
			return false;
		try
		{
			auto pJob = Create();
			if(!pJob)
				return false;
			Pending.push_back(pJob);
			try
			{
				if(!Submit(pJob))
				{
					Pending.pop_back();
					return false;
				}
			}
			catch(...)
			{
				Pending.pop_back();
				throw;
			}
			Triggered[Index] = true;
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;

	size_t Num() const;
	const CCountryFlag &GetByCountryCode(int CountryCode) const;
	const CCountryFlag &GetByIndex(size_t Index) const;
	void PrewarmByCountryCodes(const std::vector<int> &vCountryCodes);
	void PrewarmByIndices(const std::vector<int> &vIndices);
	bool PrewarmByCountryCodesReady(const std::vector<int> &vCountryCodes);
	bool PrewarmByIndicesReady(const std::vector<int> &vIndices);
	void Render(const CCountryFlag &Flag, ColorRGBA Color, float x, float y, float w, float h);
	void Render(int CountryCode, ColorRGBA Color, float x, float y, float w, float h);
	void Render(const CCountryFlag &Flag, ColorRGBA Color, float x, float y, float w, float h, int64_t CustomStartTime);
	void Render(int CountryCode, ColorRGBA Color, float x, float y, float w, float h, int64_t CustomStartTime);

private:
	enum
	{
		CODE_LB = -999,
		CODE_UB = 999,
		CODE_RANGE = CODE_UB - CODE_LB + 1,
	};
	std::vector<CCountryFlag> m_vCountryFlags;
	size_t m_aCodeIndexLUT[CODE_RANGE];

	int m_FlagsQuadContainerIndex;

	std::deque<std::shared_ptr<CCountryFlagLoadJob>> m_PendingJobs;

	static bool ValidateCountryCodeString(const char *pString);
	static bool ValidateCountryCodeIntegerString(const char *pString);
	void LoadCountryflagsIndexfile();
	void StartFlagLoadJob(size_t Index);
	void ProcessCompletedJobs();
	mutable std::vector<bool> m_vLoadTriggered;
};
#endif
