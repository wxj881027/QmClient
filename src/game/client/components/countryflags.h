/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H
#define GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H

#include <base/lock.h>

#include <engine/graphics.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <deque>
#include <vector>

constexpr int QmNormalizeCountryCode(int CountryCodeValue)
{
	return CountryCodeValue >= ::CountryCode::MINIMUM && CountryCodeValue <= ::CountryCode::MAXIMUM ? CountryCodeValue : ::CountryCode::DEFAULT;
}

// 国旗加载入场动效参数
constexpr float COUNTRY_FLAG_ANIM_DURATION = 0.28f;
constexpr float COUNTRY_FLAG_ANIM_OVERSHOOT = 2.6f;

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

// 计算中心锚点对齐缩放后的包围矩形
inline void ComputeCountryFlagEntryRect(float x, float y, float w, float h, float Scale, float &OutX, float &OutY, float &OutW, float &OutH)
{
	OutW = w * Scale;
	OutH = h * Scale;
	OutX = x + (w - OutW) * 0.5f;
	OutY = y + (h - OutH) * 0.5f;
}

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

		bool operator<(const CCountryFlag &Other) const { return str_comp(m_aCountryCodeString, Other.m_aCountryCodeString) < 0; }
	};

	class CCountryFlagLoadJob : public IJob
	{
	public:
		struct SResult
		{
			CImageInfo m_Image;
			int m_CountryCode;
			bool m_Success = false;
		};

	private:
		std::string m_Path;
		IStorage *m_pStorage;
		mutable CLock m_Mutex;
		SResult m_Result;
		bool m_Completed = false;

	protected:
		void Run() override REQUIRES(!m_Mutex);

	public:
		CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage);
		~CCountryFlagLoadJob() override;

		bool IsCompleted() const REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			return m_Completed;
		}

		SResult GetResult() REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			SResult Result = std::move(m_Result);
			m_Result = SResult();
			return Result;
		}
	};

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
	void StartFlagLoadJob(int Index);
	void ProcessCompletedJobs();
	mutable std::vector<bool> m_vLoadTriggered;
};
#endif
