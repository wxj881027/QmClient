/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H
#define GAME_CLIENT_COMPONENTS_COUNTRYFLAGS_H

#include <engine/graphics.h>
#include <engine/shared/jobs.h>

#include <game/client/component.h>

#include <deque>
#include <mutex>
#include <vector>

class CCountryFlags : public CComponent
{
public:
	struct CCountryFlag
	{
		int m_CountryCode;
		char m_aCountryCodeString[16];
		IGraphics::CTextureHandle m_Texture;
		bool m_Loaded = false;

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
		int m_CountryCode;
		IStorage *m_pStorage;
		std::mutex m_Mutex;
		SResult m_Result;
		bool m_Completed = false;

		void Run() override;

	public:
		CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage);
		~CCountryFlagLoadJob() override;

		bool IsCompleted() const
		{
			std::lock_guard<std::mutex> Lock(const_cast<std::mutex &>(m_Mutex));
			return m_Completed;
		}

		SResult GetResult()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
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
	void Render(const CCountryFlag &Flag, ColorRGBA Color, float x, float y, float w, float h);
	void Render(int CountryCode, ColorRGBA Color, float x, float y, float w, float h);

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

	void LoadCountryflagsIndexfile();
	void StartFlagLoadJob(int Index);
	void ProcessCompletedJobs();
	mutable std::vector<bool> m_vLoadTriggered;
};
#endif
